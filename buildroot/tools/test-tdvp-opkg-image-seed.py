#!/usr/bin/env python3
"""Exercise image ownership with real IPKs and optional native opkg 0.7.0."""

import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
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
             symlink="libmount.so.1.1.0", depends=None, mode=0o644, version="1-1"):
    directory.mkdir(parents=True, exist_ok=True)
    if depends is None:
        depends = "tdvp-platform-abi (= " + SEED.ABI_VERSION + ")"
    control = ("Package: " + package + "\nVersion: " + version + "\nArchitecture: riscv64\n"
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
        ipk = directory / (package + "_" + version + "_riscv64.ipk")
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


def add_local_feed(root, ipk):
    """Expose a real newer candidate to the resolver without network access."""
    control = subprocess.check_output(["ar", "p", str(ipk), "control.tar.gz"])
    with tarfile.open(fileobj=io.BytesIO(control), mode="r:gz") as archive:
        text = archive.extractfile("./control").read().decode()
    data = ipk.read_bytes()
    text += ("Filename: " + ipk.name + "\nSize: " + str(len(data)) +
             "\nMD5Sum: " + hashlib.md5(data).hexdigest() +
             "\nSHA256sum: " + hashlib.sha256(data).hexdigest() + "\n\n")
    (root / "var/lib/opkg/lists/tdvp_test").write_text(text)
    config = root / "etc/opkg/opkg.conf"
    config.write_text(config.read_text() + "src tdvp_test " + ipk.parent.as_uri() + "\n")


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

    def test_status_uses_opkg_want_flag_status_order(self):
        info, build = make_build_info(self.work)
        SEED.seed(self.root, build_info=info, build_dir=build)
        records = (self.root / "var/lib/opkg/status").read_text().strip().split("\n\n")
        self.assertEqual(len(records), 3)
        for record in records:
            fields = dict(line.split(": ", 1) for line in record.splitlines())
            self.assertEqual(fields["Status"], "install hold installed")
            self.assertEqual(fields["Essential"], "yes")
            control = self.root / ("var/lib/opkg/info/" + fields["Package"] + ".control")
            self.assertEqual(control.read_text().strip(), record)

    def test_native_gate_precedes_full_image_build(self):
        workflow = (PROJECT / ".github/workflows/ci.yml").read_text()
        extract = 'bash buildroot/tools/build-k230-sdk-rm69a10.sh "$TDVP_WORKTREE" opkg-extract'
        native = 'bash buildroot/tools/test-tdvp-opkg-image-native.sh'
        image = '- name: Build the bootable SD image'
        self.assertEqual(workflow.count(native), 1)
        self.assertLess(workflow.index(extract), workflow.index(native))
        self.assertLess(workflow.index(native), workflow.index(image))

    def test_verify_rejects_legacy_status_in_both_database_copies(self):
        SEED.seed(self.root)
        for path in [self.root / "var/lib/opkg/status"] + list((self.root / "var/lib/opkg/info").glob("*.control")):
            path.write_text(path.read_text().replace("Status: install hold installed", "Status: hold ok installed"))
        with self.assertRaisesRegex(ValueError, "installed package control differs"):
            SEED.verify(self.root)

    def run_opkg(self, *arguments):
        result = subprocess.run([os.environ["TDVP_TEST_OPKG"], "-f", str(self.root / "etc/opkg/opkg.conf"),
                                 "-o", str(self.root)] + list(arguments),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertNotIn(b"Internal error", result.stdout, result.stdout.decode())
        self.assertNotIn(b"Status: unknown", result.stdout, result.stdout.decode())
        return result

    def assert_held_packages(self, expected):
        result = self.run_opkg("status")
        self.assertEqual(result.returncode, 0, result.stdout.decode())
        records = {}
        for record in result.stdout.decode().strip().split("\n\n"):
            fields = dict(line.split(": ", 1) for line in record.splitlines())
            records[fields["Package"]] = fields
        for name, version in expected.items():
            self.assertEqual(records[name]["Version"], version)
            self.assertEqual(records[name]["Status"], "install hold installed")
            self.assertEqual(records[name]["Essential"], "yes")
        return records

    @unittest.skipUnless(os.environ.get("TDVP_TEST_OPKG"), "native opkg binary not provided")
    def test_native_opkg_reads_every_seeded_hold_flag_without_parser_errors(self):
        info, build = make_build_info(self.work)
        SEED.seed(self.root, build_info=info, build_dir=build)
        manifest = json.loads((self.root / SEED.MANIFEST.lstrip("/")).read_text())
        expected = {name: fields["Version"] for name, fields in manifest["installed_packages"].items()}
        self.assertEqual(set(self.assert_held_packages(expected)), set(expected))

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
        expected = {name: fields["Version"] for name, fields in manifest["installed_packages"].items()}
        self.assert_held_packages(expected)
        result = self.run_opkg("install", str(app))
        self.assertEqual(result.returncode, 0, result.stdout.decode())
        self.assert_held_packages(expected)
        old_runtime = make_ipk(self.work / "old", payload=b"incompatible")
        result = self.run_opkg("install", str(old_runtime))
        self.assertNotEqual(result.returncode, 0, result.stdout.decode())
        self.assertIn(package.encode(), result.stdout)
        self.assertEqual((self.root / "usr/lib/libmount.so.1.1.0").read_bytes(), b"image-library")
        self.assert_held_packages(expected)
        result = self.run_opkg("remove", "tdvp-fixture")
        self.assertEqual(result.returncode, 0, result.stdout.decode())
        self.assertFalse((self.root / "usr/share/tdvp-fixture/libmount.so.1.1.0").exists())
        self.assert_held_packages(expected)

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

    def make_privileged_image(self):
        (self.root / "usr/sbin").mkdir()
        for name, mode in (("unix_chkpwd", 0o4755), ("setgid-helper", 0o2755),
                           ("sticky-file", 0o1644), ("ordinary-helper", 0o755)):
            path = self.root / "usr/sbin" / name
            path.write_bytes(b"inert permission fixture\n")
            path.chmod(mode)
        # Exercise quoted debugfs paths and both kinds of link.
        (self.root / "usr/lib/library alias.so").symlink_to("libmount.so.1.1.0")
        os.link(str(self.root / "usr/sbin/unix_chkpwd"), str(self.root / "usr/sbin/helper-hardlink"))
        info, build = make_build_info(self.work)
        SEED.seed(self.root, build_info=info, build_dir=build)
        image = self.work / "privileged.ext4"
        result = subprocess.run(["mkfs.ext4", "-q", "-F", "-d", str(self.root), str(image), "8192"],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 0, result.stdout.decode())
        return image

    def check_ext4(self, image):
        return subprocess.run(["bash", str(PROJECT / "buildroot/k230-sdk-overlay/board/tdvp/verify-opkg-rootfs.sh"),
                               str(image)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def test_ext4_special_modes_are_read_from_image(self):
        image = self.make_privileged_image()
        before = hashlib.sha256(image.read_bytes()).hexdigest()
        actual_modes = SEED.ext4_file_modes(image, SEED.inventory(self.root))
        self.assertEqual(actual_modes["/usr/sbin/unix_chkpwd"], 0o4755)
        self.assertEqual(actual_modes["/usr/sbin/setgid-helper"], 0o2755)
        self.assertEqual(actual_modes["/usr/sbin/sticky-file"], 0o1644)
        self.assertEqual(actual_modes["/usr/lib/library alias.so"], 0o777)
        self.assertEqual(actual_modes["/lib"], 0o777)
        extracted = self.work / "readback"
        extracted.mkdir()
        subprocess.run(["debugfs", "-R", "rdump / " + str(extracted), str(image)],
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        # Reproduce the regression explicitly on the e2fsprogs used by CI.
        self.assertEqual((extracted / "usr/sbin/unix_chkpwd").stat().st_mode & 0o7777, 0o755)
        with self.assertRaisesRegex(ValueError, "unix_chkpwd.*mode expected=04755 actual=00755"):
            SEED.verify(extracted, require_buildroot=True)
        SEED.verify(extracted, require_buildroot=True, rootfs_image=image)
        result = self.check_ext4(image)
        self.assertEqual(result.returncode, 0, result.stdout.decode())
        self.assertEqual(hashlib.sha256(image.read_bytes()).hexdigest(), before)

    def test_ext4_rejects_removed_added_and_changed_permission_bits(self):
        image = self.make_privileged_image()
        for name, mode, expected in (("unix_chkpwd", "0100755", "04755"),
                                     ("setgid-helper", "0100755", "02755"),
                                     ("sticky-file", "0100644", "01644"),
                                     ("ordinary-helper", "0104755", "00755"),
                                     ("ordinary-helper", "0100777", "00755")):
            with self.subTest(name=name, mode=mode):
                bad = self.work / "bad.ext4"
                shutil.copyfile(str(image), str(bad))
                subprocess.run(["debugfs", "-w", "-R", "set_inode_field /usr/sbin/{} mode {}".format(name, mode), str(bad)],
                               check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                result = self.check_ext4(bad)
                self.assertNotEqual(result.returncode, 0, result.stdout.decode())
                self.assertIn((name + ": mode expected=" + expected).encode(), result.stdout)
                self.assertIn(b"actual=", result.stdout)

    def test_ext4_still_rejects_payload_and_missing_file_drift(self):
        image = self.make_privileged_image()
        for change in ("content", "missing"):
            with self.subTest(change=change):
                bad = self.work / "bad.ext4"
                shutil.copyfile(str(image), str(bad))
                path = "/usr/lib/libmount.so.1.1.0"
                subprocess.run(["debugfs", "-w", "-R", "rm " + path, str(bad)],
                               check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                if change == "content":
                    replacement = self.work / "replacement"
                    replacement.write_bytes(b"changed image library")
                    subprocess.run(["debugfs", "-w", "-R", "write {} {}".format(replacement, path), str(bad)],
                                   check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                result = self.check_ext4(bad)
                self.assertNotEqual(result.returncode, 0, result.stdout.decode())
                self.assertIn((path + ": ").encode(), result.stdout)
                self.assertIn(b"sha256 expected=" if change == "content" else b"missing image path", result.stdout)

    def test_ext4_inode_reader_rejects_missing_and_unsafe_paths(self):
        image = self.make_privileged_image()
        with self.assertRaisesRegex(ValueError, "ext4 inode read incomplete"):
            SEED.ext4_file_modes(image, {"/missing-file": {"type": "file"}})
        for path in ('/usr/lib/a"b', "/usr/lib/a\\b", "/usr/lib/a\nb"):
            with self.subTest(path=path):
                with self.assertRaisesRegex(ValueError, "unsupported debugfs path"):
                    SEED.ext4_file_modes(image, {path: {"type": "file"}})

    @unittest.skipUnless(os.environ.get("TDVP_TEST_OPKG"), "native opkg binary not provided")
    def test_native_opkg_rejects_overwrite_and_installs_new_application(self):
        SEED.seed(self.root)
        manifest = json.loads((self.root / SEED.MANIFEST.lstrip("/")).read_text())
        expected = {name: fields["Version"] for name, fields in manifest["installed_packages"].items()}
        self.assert_held_packages(expected)
        library = self.root / "usr/lib/libmount.so.1.1.0"
        before = library.read_bytes()
        for prefix in ("usr/lib", "lib"):
            # Failed transactions can retain an unconfigured package record.
            # Distinct names ensure the second check reaches file-clash logic
            # instead of merely rejecting a reinstall of the first package.
            package = "libmount-" + prefix.replace("/", "-")
            ipk = make_ipk(self.work / package, package=package, payload=b"incompatible", prefix=prefix)
            result = self.run_opkg("install", str(ipk))
            self.assertNotEqual(result.returncode, 0, result.stdout.decode())
            self.assertIn(b"tdvp-image-base", result.stdout)
            self.assertEqual(before, library.read_bytes())
            self.assert_held_packages(expected)
        # A package adding a new path still works with the same held base seed.
        ipk = make_ipk(self.work / "new", package="tdvp-fixture", prefix="usr/share/tdvp-fixture")
        result = self.run_opkg("install", str(ipk))
        self.assertEqual(result.returncode, 0, result.stdout.decode())
        self.assertEqual(before, library.read_bytes())
        self.assertTrue((self.root / "usr/share/tdvp-fixture/libmount.so.1.1.0").is_file())
        self.assert_held_packages(expected)

    @unittest.skipUnless(os.environ.get("TDVP_TEST_OPKG"), "native opkg binary not provided")
    def test_native_opkg_holds_upgrade_install_and_essential_removal(self):
        info, build = make_build_info(self.work)
        SEED.seed(self.root, build_info=info, build_dir=build)
        manifest = json.loads((self.root / SEED.MANIFEST.lstrip("/")).read_text())
        expected = {name: fields["Version"] for name, fields in manifest["installed_packages"].items()}
        package = "tdvp-image-util-linux-libs"
        library = self.root / "usr/lib/libmount.so.1.1.0"
        before = library.read_bytes()
        newer = make_ipk(self.work / "newer", package=package, version="999-1", payload=b"incompatible")
        same = make_ipk(self.work / "same", package=package, version=expected[package], payload=b"incompatible")
        add_local_feed(self.root, newer)
        cases = ((("upgrade", package), 0, b"marked hold"),
                 (("upgrade",), 0, b"marked hold"),
                 (("install", str(newer)), 0, b"due to held package"),
                 (("install", str(same)), 255, b"matches the installed version"),
                 (("remove", package), 255, b"Refusing to remove essential package"))
        for arguments, code, message in cases:
            with self.subTest(arguments=arguments):
                result = self.run_opkg(*arguments)
                self.assertEqual(result.returncode, code, result.stdout.decode())
                self.assertIn(message, result.stdout)
                self.assertEqual(before, library.read_bytes(), result.stdout.decode())
                self.assert_held_packages(expected)
        # Positive control: the very same candidate must really be resolvable
        # and installable once HOLD is removed in this disposable fixture.
        result = self.run_opkg("flag", "ok", package)
        self.assertEqual(result.returncode, 0, result.stdout.decode())
        result = self.run_opkg("upgrade", package)
        self.assertEqual(result.returncode, 0, result.stdout.decode())
        self.assertIn(b"999-1", result.stdout)
        self.assertEqual(library.read_bytes(), b"incompatible")


if __name__ == "__main__":
    unittest.main()
