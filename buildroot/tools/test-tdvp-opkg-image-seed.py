#!/usr/bin/env python3
"""Exercise image ownership with real IPKs and optional native opkg 0.7.0."""

import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest


PROJECT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("image_seed", PROJECT / "buildroot/k230-sdk-overlay/board/tdvp/seed-opkg-image.py")
SEED = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SEED)


def make_root(root):
    for name in ("usr/bin", "usr/lib", "etc/opkg", "var/lib/opkg/info", "var/lib/opkg/lists"):
        (root / name).mkdir(parents=True, exist_ok=True)
    (root / "lib").symlink_to("usr/lib")
    (root / "usr/bin/opkg").write_bytes(b"image-opkg-fixture")
    (root / "usr/lib/libmount.so.1.1.0").write_bytes(b"image-library")
    (root / "usr/lib/libmount.so.1").symlink_to("libmount.so.1.1.0")
    (root / "etc/opkg/opkg.conf").write_text(
        "dest root /\noption lists_dir /var/lib/opkg/lists\n"
        "option info_dir /var/lib/opkg/info\noption status_file /var/lib/opkg/status\narch riscv64 10\n")
    (root / "var/lib/opkg/status").write_text(
        "Package: tdvp-platform-abi\nVersion: " + SEED.ABI_VERSION + "\n"
        "Architecture: riscv64\nStatus: install ok installed\n\n")


def make_ipk(directory, package="libmount-1", payload=b"image-library", prefix="usr/lib",
             symlink="libmount.so.1.1.0", depends=None, mode=0o644):
    directory.mkdir(parents=True, exist_ok=True)
    if depends is None:
        depends = "tdvp-platform-abi (= " + SEED.ABI_VERSION + ")"
    control = ("Package: " + package + "\nVersion: 1-1\nArchitecture: riscv64\n"
               "Depends: " + depends + "\nDescription: Test runtime\n").encode()
    with tempfile.TemporaryDirectory() as temporary:
        staging = Path(temporary)
        (staging / "debian-binary").write_bytes(b"2.0\n")
        with tarfile.open(str(staging / "control.tar.gz"), "w:gz") as archive:
            member = tarfile.TarInfo("./control")
            member.size = len(control)
            archive.addfile(member, io.BytesIO(control))
        with tarfile.open(str(staging / "data.tar.gz"), "w:gz") as archive:
            member = tarfile.TarInfo("./" + prefix + "/libmount.so.1.1.0")
            member.size, member.mode = len(payload), mode
            archive.addfile(member, io.BytesIO(payload))
            member = tarfile.TarInfo("./" + prefix + "/libmount.so.1")
            member.type, member.linkname = tarfile.SYMTYPE, symlink
            archive.addfile(member)
        ipk = directory / (package + "_1-1_riscv64.ipk")
        subprocess.run(["ar", "rD", str(ipk), "debian-binary", "control.tar.gz", "data.tar.gz"],
                       cwd=str(staging), check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return ipk


def make_build_info(work):
    build = work / "build"
    metadata = {
        "util-linux-libs": {"type": "target", "version": "2.40.2", "install_target": True,
                            "stamp_dir": "build/util-linux-libs-2.40.2", "dependencies": ["host-pkgconf"]},
        "host-pkgconf": {"type": "host", "install_target": False},
    }
    directory = build / "util-linux-libs-2.40.2"
    directory.mkdir(parents=True)
    (directory / ".files-list.txt").write_text(
        "util-linux-libs,./lib/libmount.so.1.1.0\n"
        "util-linux-libs,./usr/lib/libmount.so.1\n"
        "util-linux-libs,./usr/include/removed-by-finalize.h\n")
    info = work / "show-info.json"
    info.write_text(json.dumps(metadata))
    return info, build


class ImageSeed(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="tdvp-image-seed-test-")
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.root = self.work / "root"
        make_root(self.root)

    def test_base_reserves_preinstalled_files_and_aliases(self):
        SEED.seed(self.root)
        owned = (self.root / "var/lib/opkg/info/tdvp-image-base.list").read_text().splitlines()
        self.assertIn("/usr/lib/libmount.so.1.1.0", owned)
        self.assertIn("/lib/libmount.so.1.1.0", owned)
        self.assertIn(SEED.MANIFEST, owned)
        self.assertIn("Essential: yes", (self.root / "var/lib/opkg/status").read_text())
        before = (self.root / SEED.MANIFEST.lstrip("/")).read_bytes()
        SEED.seed(self.root)
        self.assertEqual(before, (self.root / SEED.MANIFEST.lstrip("/")).read_bytes())

    def test_exact_catalog_gets_real_package_records(self):
        catalog = self.work / "catalog"
        make_ipk(catalog)
        SEED.seed(self.root, catalog)
        status = (self.root / "var/lib/opkg/status").read_text()
        self.assertIn("Package: libmount-1\nVersion: 1-1\n", status)
        self.assertNotIn("/usr/lib/libmount.so.1.1.0\n", (self.root / "var/lib/opkg/info/tdvp-image-base.list").read_text())
        self.assertIn("/usr/lib/libmount.so.1.1.0\n", (self.root / "var/lib/opkg/info/libmount-1.list").read_text())
        manifest = json.loads((self.root / SEED.MANIFEST.lstrip("/")).read_text())
        self.assertEqual(manifest["runtime_packages"], ["libmount-1"])

    def test_invalid_catalog_never_changes_status(self):
        for kwargs in ({"payload": b"different-library"}, {"symlink": "wrong-target"},
                       {"depends": "missing-runtime (= 1-1)"}, {"mode": 0o755}, {"prefix": "usr/share"}):
            with self.subTest(kwargs=kwargs):
                catalog = self.work / hashlib.sha256(repr(kwargs).encode()).hexdigest()
                make_ipk(catalog, **kwargs)
                before = (self.root / "var/lib/opkg/status").read_bytes()
                with self.assertRaises(ValueError):
                    SEED.seed(self.root, catalog)
                self.assertEqual(before, (self.root / "var/lib/opkg/status").read_bytes())
                self.assertFalse((self.root / "var/lib/opkg/info/tdvp-image-base.list").exists())

    def test_duplicate_ownership_is_rejected(self):
        catalog = self.work / "catalog"
        make_ipk(catalog)
        make_ipk(catalog, package="another-owner")
        with self.assertRaisesRegex(ValueError, "duplicate image ownership"):
            SEED.seed(self.root, catalog)

    def test_buildroot_records_final_bytes_and_real_source_version(self):
        info, build = make_build_info(self.work)
        SEED.seed(self.root, build_info=info, build_dir=build)
        manifest = json.loads((self.root / SEED.MANIFEST.lstrip("/")).read_text())
        package = "tdvp-image-util-linux-libs"
        self.assertEqual(manifest["ownership_mode"], "buildroot")
        self.assertEqual(manifest["owners"]["/usr/lib/libmount.so.1.1.0"], package)
        self.assertEqual(manifest["package_sources"][package]["source_version"], "2.40.2")
        self.assertRegex(manifest["installed_packages"][package]["Version"], r"^2\.40\.2\+tdvp\.[0-9a-f]{64}$")
        owned = (self.root / ("var/lib/opkg/info/" + package + ".list")).read_text()
        self.assertIn("/lib/libmount.so.1.1.0\n", owned)
        self.assertNotIn("removed-by-finalize", owned)
        self.assertNotIn("host-pkgconf", (self.root / "var/lib/opkg/status").read_text())
        self.assertEqual(manifest["files"]["/usr/lib/libmount.so.1.1.0"]["mode"], 0o644)

    def test_ambiguous_buildroot_paths_stay_with_image_base(self):
        info, build = make_build_info(self.work)
        metadata = json.loads(info.read_text())
        metadata["util-linux"] = dict(metadata["util-linux-libs"], stamp_dir="build/util-linux-2.40.2")
        other = build / "util-linux-2.40.2"
        other.mkdir()
        (other / ".files-list.txt").write_text("util-linux,./usr/lib/libmount.so.1.1.0\n")
        info.write_text(json.dumps(metadata))
        SEED.seed(self.root, build_info=info, build_dir=build)
        manifest = json.loads((self.root / SEED.MANIFEST.lstrip("/")).read_text())
        self.assertEqual(manifest["owners"]["/usr/lib/libmount.so.1.1.0"], SEED.BASE)
        self.assertEqual(manifest["ambiguous_buildroot_paths"]["/usr/lib/libmount.so.1.1.0"],
                         ["util-linux", "util-linux-libs"])

    def test_missing_or_unsafe_buildroot_account_fails_before_writes(self):
        info, build = make_build_info(self.work)
        metadata = json.loads(info.read_text())
        before = (self.root / "var/lib/opkg/status").read_bytes()
        for stamp in ("build", "build/absent", "build/../../outside", "/etc"):
            with self.subTest(stamp=stamp):
                metadata["util-linux-libs"]["stamp_dir"] = stamp
                info.write_text(json.dumps(metadata))
                with self.assertRaises(ValueError):
                    SEED.seed(self.root, build_info=info, build_dir=build)
                self.assertEqual(before, (self.root / "var/lib/opkg/status").read_bytes())

    @unittest.skipUnless(os.environ.get("TDVP_TEST_OPKG"), "native opkg binary not provided")
    def test_native_opkg_uses_preinstalled_buildroot_dependency(self):
        info, build = make_build_info(self.work)
        SEED.seed(self.root, build_info=info, build_dir=build)
        manifest = json.loads((self.root / SEED.MANIFEST.lstrip("/")).read_text())
        package = "tdvp-image-util-linux-libs"
        version = manifest["installed_packages"][package]["Version"]
        app = make_ipk(self.work / "app", package="tdvp-fixture", prefix="usr/share/tdvp-fixture",
                       depends=package + " (= " + version + ")")
        command = [os.environ["TDVP_TEST_OPKG"], "-f", str(self.root / "etc/opkg/opkg.conf"), "-o", str(self.root)]
        result = subprocess.run(command + ["install", str(app)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 0, result.stdout.decode())
        old_runtime = make_ipk(self.work / "old", payload=b"incompatible")
        result = subprocess.run(command + ["install", str(old_runtime)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertNotEqual(result.returncode, 0, result.stdout.decode())
        self.assertIn(package.encode(), result.stdout)
        self.assertEqual((self.root / "usr/lib/libmount.so.1.1.0").read_bytes(), b"image-library")

    def test_final_inventory_rejects_content_mode_and_database_drift(self):
        info, build = make_build_info(self.work)
        SEED.seed(self.root, build_info=info, build_dir=build)
        SEED.verify(self.root, require_buildroot=True)
        for relative in ("usr/lib/libmount.so.1.1.0", "var/lib/opkg/status",
                         "var/lib/opkg/info/tdvp-image-util-linux-libs.list",
                         "var/lib/opkg/info/tdvp-image-util-linux-libs.control"):
            with self.subTest(path=relative):
                path = self.root / relative
                original = path.read_bytes()
                path.write_bytes(original + b"drift\n")
                with self.assertRaises(ValueError):
                    SEED.verify(self.root, require_buildroot=True)
                path.write_bytes(original)
        (self.root / "usr/lib/libmount.so.1.1.0").chmod(0o755)
        with self.assertRaisesRegex(ValueError, "content/mode differs"):
            SEED.verify(self.root, require_buildroot=True)

    def test_production_fakeroot_hook_and_ext4_export(self):
        info, build = make_build_info(self.work)
        (build / "tdvp-package-info.json").write_bytes(info.read_bytes())
        (self.root / "etc/passwd").write_text("tdvp:x:1000:1000:User:/home/tdvp:/bin/sh\n")
        (self.root / "etc/shadow").write_text("tdvp:!:1:0:99999:7:::\n")
        board = PROJECT / "buildroot/k230-sdk-overlay/board/tdvp"
        result = subprocess.run(["bash", str(board / "post-fakeroot.sh"), str(self.root)],
                                env=dict(os.environ, BUILD_DIR=str(build)),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 0, result.stdout.decode())
        self.assertIn("tdvp:$5$tdvp-repro-2026$", (self.root / "etc/shadow").read_text())
        image = self.work / "rootfs.ext2"
        result = subprocess.run(["mkfs.ext4", "-q", "-F", "-d", str(self.root), str(image), "8192"],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 0, result.stdout.decode())
        exported = self.work / "export"
        command = ["bash", str(board / "verify-opkg-rootfs.sh"), str(image), str(exported)]
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 0, result.stdout.decode())
        self.assertEqual((exported / "tdvp-image-base.json").read_bytes(),
                         (self.root / SEED.MANIFEST.lstrip("/")).read_bytes())
        self.assertEqual((exported / "tdvp-opkg-status").read_bytes(),
                         (self.root / "var/lib/opkg/status").read_bytes())
        with tarfile.open(str(exported / "tdvp-opkg-info.tar.gz")) as archive:
            self.assertIn("info/tdvp-image-util-linux-libs.list", archive.getnames())
        subprocess.run(["debugfs", "-w", "-R", "rm /var/lib/opkg/info/tdvp-image-base.list", str(image)],
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertNotEqual(result.returncode, 0, result.stdout.decode())

    @unittest.skipUnless(os.environ.get("TDVP_TEST_OPKG"), "native opkg binary not provided")
    def test_native_opkg_rejects_overwrite_and_installs_new_application(self):
        opkg = os.environ["TDVP_TEST_OPKG"]
        SEED.seed(self.root)
        library = self.root / "usr/lib/libmount.so.1.1.0"
        before = library.read_bytes()
        for prefix in ("usr/lib", "lib"):
            # Failed transactions can retain an unconfigured package record.
            # Distinct names ensure the second check reaches file-clash logic
            # instead of merely rejecting a reinstall of the first package.
            package = "libmount-" + prefix.replace("/", "-")
            ipk = make_ipk(self.work / package, package=package, payload=b"incompatible", prefix=prefix)
            result = subprocess.run([opkg, "-f", str(self.root / "etc/opkg/opkg.conf"),
                                     "-o", str(self.root), "install", str(ipk)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.assertNotEqual(result.returncode, 0, result.stdout.decode())
            self.assertIn(b"tdvp-image-base", result.stdout)
            self.assertEqual(before, library.read_bytes())
        # A package adding a new path still works with the same held base seed.
        ipk = make_ipk(self.work / "new", package="tdvp-fixture", prefix="usr/share/tdvp-fixture")
        result = subprocess.run([opkg, "-f", str(self.root / "etc/opkg/opkg.conf"),
                                 "-o", str(self.root), "install", str(ipk)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 0, result.stdout.decode())
        self.assertEqual(before, library.read_bytes())
        self.assertTrue((self.root / "usr/share/tdvp-fixture/libmount.so.1.1.0").is_file())


if __name__ == "__main__":
    unittest.main()
