#!/usr/bin/env python3
"""Small, offline SDK export/verification regressions (no cross compiler needed)."""

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


EXPORT = load("sdk_export", HERE / "export-tdvp-sdk.py")
VERIFY = load("sdk_verify", HERE / "sdk/verify-sdk.py")


class SdkTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "sdk"
        self.root.mkdir()

    def manifest(self):
        (self.root / "sysroot/usr/lib").mkdir(parents=True, exist_ok=True)
        library = self.root / "sysroot/usr/lib/libfixture.so.1"
        library.write_bytes(b"image-library")
        (self.root / "metadata").mkdir(exist_ok=True)
        (self.root / "metadata/image-meta").write_bytes(b"image")
        records = EXPORT.tree_inventory(self.root)
        return {"schema": 1, "kind": "tdvp-cpu0-application-sdk", "target": EXPORT.TRIPLE,
                "march": EXPORT.ARCH, "mabi": "lp64d", "files": records,
                "files_sha256": hashlib.sha256(json.dumps(records, sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
                "image_bindings": {"image-meta": EXPORT.sha256(self.root / "metadata/image-meta")},
                "image_library_sha256": {"/usr/lib/libfixture.so.1": EXPORT.sha256(library)}}

    def test_roundtrip_inventory_and_image_binding(self):
        manifest = self.manifest()
        VERIFY.verify_tree(self.root, manifest)
        manifest["image_bindings"]["image-meta"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "metadata differs"):
            VERIFY.verify_tree(self.root, manifest)

    def test_payload_changes_are_rejected(self):
        for mutation in ("bytes", "mode", "missing", "extra"):
            with self.subTest(mutation=mutation):
                manifest = self.manifest()
                path = self.root / "sysroot/usr/lib/libfixture.so.1"
                if mutation == "bytes":
                    path.write_bytes(b"different")
                elif mutation == "mode":
                    path.chmod(0o777)
                elif mutation == "missing":
                    path.unlink()
                else:
                    (self.root / "extra").write_bytes(b"unexpected")
                with self.assertRaisesRegex(ValueError, "payload differs"):
                    VERIFY.verify_tree(self.root, manifest)
                if mutation == "extra":
                    (self.root / "extra").unlink()
                elif mutation == "mode":
                    path.chmod(0o644)

    def test_manifest_digest_and_image_library_identity(self):
        manifest = self.manifest()
        digest = manifest["files_sha256"]
        manifest["files_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "inventory digest"):
            VERIFY.verify_tree(self.root, manifest)
        manifest["files_sha256"] = digest
        manifest["image_library_sha256"]["/usr/lib/libfixture.so.1"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "final-image library"):
            VERIFY.verify_tree(self.root, manifest)

    def test_absolute_sysroot_links_become_relative(self):
        sysroot = self.root / "sysroot"
        (sysroot / "usr/lib").mkdir(parents=True)
        (sysroot / "lib").symlink_to("/usr/lib")
        (sysroot / "usr/lib/libx.so").write_bytes(b"lib")
        (sysroot / "usr/lib/alias.so").symlink_to("/old/staging/usr/lib/libx.so")
        (sysroot / "mtab").symlink_to("/proc/mounts")
        EXPORT.relocate_links(sysroot, Path("/old/staging"), self.root)
        self.assertEqual((sysroot / "lib/libx.so").read_bytes(), b"lib")
        self.assertEqual((sysroot / "usr/lib/alias.so").read_bytes(), b"lib")
        self.assertEqual(os.readlink(sysroot / "mtab"), "proc/mounts")
        EXPORT.tree_inventory(self.root)

    def test_escaping_symlink_is_rejected(self):
        (self.root / "escape").symlink_to("../../outside")
        with self.assertRaisesRegex(ValueError, "invalid SDK link"):
            EXPORT.tree_inventory(self.root)

    def test_development_paths_are_relocated_without_editing_elf(self):
        sysroot = self.root / "sysroot"
        directory = sysroot / "usr/lib/cmake"
        directory.mkdir(parents=True)
        for name in ("x.pc", "x.la", "x.cmake", "x.so"):
            (directory / name).write_text("/old/staging/usr/lib")
        EXPORT.relocate_development_files(sysroot, Path("/old/staging"))
        self.assertEqual((directory / "x.pc").read_text(), "/usr/lib")
        self.assertEqual((directory / "x.la").read_text(), "/usr/lib")
        self.assertEqual((directory / "x.cmake").read_text(), "${CMAKE_CURRENT_LIST_DIR}/../../.." + "/usr/lib")
        self.assertEqual((directory / "x.so").read_text(), "/old/staging/usr/lib")

    def test_elf_policy_rejects_vector_other_isa_abi_and_rpath(self):
        header = "ELF64 RISC-V double-float ABI"
        attributes = 'Tag_RISCV_arch: "rv64i2p1_m2p0_a2p1_f2p2_d2p2_c2p0_zicsr2p0_zifencei2p0"'
        with patch.object(VERIFY, "run", side_effect=[header, attributes, ""]):
            VERIFY.verify_elf(self.root, Path("fixture"))
        with patch.object(VERIFY, "run", side_effect=[header, attributes, "(RUNPATH) []"]):
            VERIFY.verify_elf(self.root, Path("fixture"))
        for bad in (attributes[:-1] + '_v1p0"', attributes[:-1] + '_xtheadvector1p0"', ""):
            with patch.object(VERIFY, "run", side_effect=[header, bad, ""]):
                with self.assertRaises(ValueError):
                    VERIFY.verify_elf(self.root, Path("fixture"))
        with patch.object(VERIFY, "run", return_value="ELF64 RISC-V soft-float ABI"):
            with self.assertRaisesRegex(ValueError, "lp64d"):
                VERIFY.verify_elf(self.root, Path("fixture"))
        with patch.object(VERIFY, "run", side_effect=[header, attributes, "(RUNPATH) [/old/sdk]"]):
            with self.assertRaisesRegex(ValueError, "runtime search path"):
                VERIFY.verify_elf(self.root, Path("fixture"))
        with patch.object(VERIFY, "run", side_effect=[header, attributes, "(RUNPATH) [:]"]):
            with self.assertRaisesRegex(ValueError, "runtime search path"):
                VERIFY.verify_elf(self.root, Path("fixture"))

    def test_cpu0_config_rejects_wrong_policy_before_export(self):
        path = self.root / ".config"
        path.write_text('BR2_GCC_TARGET_ABI="lp64"\n')
        with self.assertRaisesRegex(ValueError, "unsupported SDK policy"):
            EXPORT.configuration(path)

    def test_only_paired_reviewed_pixman_has_runtime_dispatch_exception(self):
        manifest = self.manifest()
        library = self.root / "sysroot/usr/lib/libpixman-1.so.0.44.2"
        library.write_bytes(b"reviewed-pixman")
        image_path = "/usr/lib/" + library.name
        manifest["image_library_sha256"][image_path] = EXPORT.sha256(library)
        packages = self.root / "metadata/tdvp-buildroot-packages.json"
        packages.write_text(json.dumps({"pixman": {"version": "0.44.2"}}))
        with patch.object(VERIFY, "verify_elf", return_value=[]) as check:
            VERIFY.verify_image_dependency(self.root, library, manifest)
            check.assert_called_once_with(self.root, library, runtime_rvv=True)
        with patch.object(VERIFY, "verify_elf", return_value=[]) as check:
            other = self.root / "sysroot/usr/lib/libfixture.so.1"
            VERIFY.verify_image_dependency(self.root, other, manifest)
            check.assert_called_once_with(self.root, other, runtime_rvv=False)
        packages.write_text(json.dumps({"pixman": {"version": "0.44.4"}}))
        with self.assertRaisesRegex(ValueError, "requires version"):
            VERIFY.verify_image_dependency(self.root, library, manifest)
        library.write_bytes(b"different-library")
        with self.assertRaisesRegex(ValueError, "not a verified final-image library"):
            VERIFY.verify_image_dependency(self.root, library, manifest)

    def test_runtime_dispatch_exception_does_not_accept_vendor_isa(self):
        header = "ELF64 RISC-V double-float ABI"
        arch = 'Tag_RISCV_arch: "rv64i2p1_m2p0_a2p1_f2p2_d2p2_c2p0_v1p0_zve64d1p0_zvl128b1p0"'
        with patch.object(VERIFY, "run", side_effect=[header, arch, ""]):
            VERIFY.verify_elf(self.root, Path("pixman"), runtime_rvv=True)
        with patch.object(VERIFY, "run", side_effect=[header, arch[:-1] + '_xtheadvector1p0"', ""]):
            with self.assertRaisesRegex(ValueError, "unsupported CPU0 ISA"):
                VERIFY.verify_elf(self.root, Path("pixman"), runtime_rvv=True)

    def test_cpu0_config_accepts_scalar_and_rejects_vector(self):
        values = {"BR2_riscv": "y", "BR2_RISCV_64": "y", "BR2_GCC_TARGET_ABI": "lp64d",
                  "BR2_TOOLCHAIN_EXTERNAL_PREINSTALLED": "y", "BR2_TOOLCHAIN_EXTERNAL_CUSTOM_PREFIX": EXPORT.TRIPLE,
                  "BR2_TOOLCHAIN_EXTERNAL_CUSTOM_GLIBC": "y", "BR2_TOOLCHAIN_EXTERNAL_GCC_14": "y",
                  "BR2_PIC_PIE": "y", "BR2_SSP_STRONG": "y", "BR2_RELRO_FULL": "y",
                  "BR2_FORTIFY_SOURCE_1": "y", "BR2_TARGET_OPTIMIZATION": "-mcpu=c908 -mtune=c908"}
        values.update({"BR2_RISCV_ISA_RV" + extension: "y" for extension in "IMAFDC"})
        path = self.root / ".config"
        path.write_text("\n".join(key + "=" + json.dumps(value) for key, value in values.items()))
        self.assertEqual(EXPORT.configuration(path), values)
        with path.open("a") as stream:
            stream.write("\nBR2_RISCV_ISA_RVV=y\n")
        with self.assertRaisesRegex(ValueError, "cannot enable RVV"):
            EXPORT.configuration(path)

    def test_sdk_permissions_are_readable_and_unprivileged(self):
        helper = self.root / "helper"
        helper.write_bytes(b"fixture")
        helper.chmod(0o4750)
        data = self.root / "data"
        data.write_bytes(b"fixture")
        data.chmod(0o600)
        (self.root / "helper-link").symlink_to("helper")
        EXPORT.development_permissions(self.root)
        self.assertEqual(helper.stat().st_mode & 0o7777, 0o755)
        self.assertEqual(data.stat().st_mode & 0o7777, 0o644)
        self.assertTrue((self.root / "helper-link").is_symlink())

    def test_export_rejects_image_mismatch_before_copying(self):
        worktree = self.root / "worktree"
        output = worktree / "output" / EXPORT.PROFILE
        output.mkdir(parents=True)
        config = output / ".config"
        config.write_text("fixture")
        (output / "images").mkdir()
        (output / "images/rootfs.ext2").write_bytes(b"image")
        bundle = self.root / "bundle"
        bundle.mkdir()
        (bundle / "candidate.img.gz").write_bytes(b"compressed")
        (bundle / "tdvp-image-manifest").write_text("rootfs.ext2_sha256=" + "0" * 64 + "\n")
        values = {"BR2_TOOLCHAIN_EXTERNAL_PATH": "/opt/toolchain/" + EXPORT.TOOLCHAIN}
        with patch.object(EXPORT, "configuration", return_value=values), patch.object(EXPORT, "copy_toolchain") as copy:
            with self.assertRaisesRegex(ValueError, "image provenance mismatch"):
                EXPORT.export(worktree, bundle, "candidate")
            copy.assert_not_called()
        self.assertFalse((bundle / "candidate-cpu0-sdk.tar.gz").exists())

    def test_collector_and_ci_require_sdk_checks(self):
        collector = (HERE / "collect-release-bundle.sh").read_text()
        workflow = (HERE.parents[1] / ".github/workflows/ci.yml").read_text()
        self.assertIn('export-tdvp-sdk.py', collector)
        self.assertIn('"${RELEASE_NAME}-cpu0-sdk.tar.gz"', collector)
        self.assertIn('tdvp-sdk-manifest.json', collector)
        self.assertIn('test-tdvp-sdk-relocation.sh', workflow)
        self.assertLess(workflow.index('test-tdvp-sdk-relocation.sh'), workflow.index('- name: Upload candidate image'))

    def test_repeated_export_refuses_to_overwrite_before_reading_inputs(self):
        for name in ("candidate-cpu0-sdk.tar.gz", "tdvp-sdk-manifest.json"):
            with self.subTest(name=name):
                path = self.root / name
                path.write_bytes(b"existing-output")
                with patch.object(EXPORT, "configuration") as config:
                    with self.assertRaisesRegex(ValueError, "output already exists"):
                        EXPORT.export(self.root / "absent-build", self.root, "candidate")
                    config.assert_not_called()
                self.assertEqual(path.read_bytes(), b"existing-output")
                path.unlink()
        (self.root / "tdvp-sdk-manifest.json").symlink_to("missing")
        with self.assertRaisesRegex(ValueError, "output already exists"):
            EXPORT.export(self.root / "absent-build", self.root, "candidate")

    def test_release_names_cannot_escape_output_directory(self):
        for name in ("", "..", "../candidate", "/candidate", "candidate/name", "with spaces"):
            with self.subTest(name=name):
                with self.assertRaisesRegex(ValueError, "invalid release name"):
                    EXPORT.export(self.root, self.root, name)

    def test_external_manifest_and_image_must_match(self):
        manifest = self.manifest()
        bundle = self.root / "bundle"
        bundle.mkdir()
        payload = json.dumps(manifest)
        (self.root / "tdvp-sdk-manifest.json").write_text(payload)
        (bundle / "tdvp-sdk-manifest.json").write_text(payload)
        (bundle / "image-meta").write_bytes(b"image")
        VERIFY.verify_bundle(self.root, bundle, manifest)
        (bundle / "tdvp-sdk-manifest.json").write_text(payload + "\n")
        with self.assertRaisesRegex(ValueError, "manifest differs"):
            VERIFY.verify_bundle(self.root, bundle, manifest)
        (bundle / "tdvp-sdk-manifest.json").write_text(payload)
        (bundle / "image-meta").write_bytes(b"wrong image")
        with self.assertRaisesRegex(ValueError, "binding differs"):
            VERIFY.verify_bundle(self.root, bundle, manifest)

    def test_production_relocation_rejects_bad_release_before_docker(self):
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        docker = bin_dir / "docker"
        docker.write_text("#!/bin/sh\necho DOCKER_SHOULD_NOT_RUN >&2\nexit 88\n")
        docker.chmod(0o755)
        env = dict(os.environ, PATH=str(bin_dir) + os.pathsep + os.environ["PATH"])
        for scenario, error in (("outside", "inside its release bundle"), ("name", "name differs"),
                                ("hash", "FAILED"), ("gzip", "not in gzip format"),
                                ("manifest", "differ")):
            with self.subTest(scenario=scenario):
                bundle = self.root / scenario
                bundle.mkdir()
                archive = bundle / "candidate-cpu0-sdk.tar.gz"
                archive.write_bytes(b"not a gzip file")
                manifest = bundle / "tdvp-sdk-manifest.json"
                manifest.write_text(json.dumps({"archive": archive.name}))
                if scenario == "manifest":
                    inside = bundle / "staged/tdvp-sdk"
                    inside.mkdir(parents=True)
                    (inside / "tdvp-sdk-manifest.json").write_text("{}\n")
                    with tarfile.open(archive, "w:gz") as tar:
                        tar.add(inside, arcname="tdvp-sdk")
                digest = "0" * 64 if scenario == "hash" else EXPORT.sha256(archive)
                (bundle / "SHA256SUMS").write_text(digest + "  " + archive.name + "\n" +
                                                  EXPORT.sha256(manifest) + "  " + manifest.name + "\n")
                argument = archive
                if scenario == "outside":
                    argument = self.root / archive.name
                    argument.write_bytes(archive.read_bytes())
                elif scenario == "name":
                    argument = archive.rename(bundle / "wrong-name.tar.gz")
                result = subprocess.run(["bash", str(HERE / "test-tdvp-sdk-relocation.sh"), str(argument), str(bundle)],
                                        env=env, capture_output=True, text=True, timeout=30)
                output = result.stdout + result.stderr
                self.assertNotEqual(result.returncode, 0, output)
                self.assertIn(error, output)
                self.assertNotIn("DOCKER_SHOULD_NOT_RUN", output)


if __name__ == "__main__":
    unittest.main(verbosity=2)
