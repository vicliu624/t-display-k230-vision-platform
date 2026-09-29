#!/usr/bin/env python3
"""Exercise the real opkg resolver with isolated, inert offline databases.

Usage: test-opkg-alternative-solver.py /absolute/path/to/opkg
No downloads, package installation, root privileges or target hardware required.
"""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

OPKG = str(Path(sys.argv.pop(1)).resolve()) if len(sys.argv) > 1 else os.environ.get("TDVP_TEST_OPKG")
if not OPKG:
    raise SystemExit("provide the real opkg binary; this test must not silently skip")


class Alternatives(unittest.TestCase):
    def check_plan(self, dependency, expected, installed=True, flag="hold", version="1", transitive=False):
        with tempfile.TemporaryDirectory(prefix="tdvp-opkg-or-") as directory:
            root = Path(directory)
            for subdir in ("var/lib/opkg/info", "var/lib/opkg/lists", "tmp"):
                (root / subdir).mkdir(parents=True)
            config = root / "opkg.conf"
            config.write_text("dest root /\noption lists_dir /var/lib/opkg/lists\n"
                              "option info_dir /var/lib/opkg/info\noption status_file /var/lib/opkg/status\n"
                              "arch riscv64 10\nsrc test file:///unused-test-feed\n")
            status = root / "var/lib/opkg/status"
            status.write_text(("Package: image-runtime\nVersion: " + version +
                               "\nArchitecture: riscv64\nStatus: install " + flag +
                               " installed\nEssential: yes\n\n") if installed else "")
            (root / "var/lib/opkg/info/image-runtime.list").write_text("")
            index = ""
            for name in ("runtime", "other-runtime", "consumer", "application"):
                depends = dependency if name == "consumer" else ("consumer (= 1)" if name == "application" else "")
                index += ("Package: " + name + "\nVersion: 1\nArchitecture: riscv64\n" +
                          ("Depends: " + depends + "\n" if depends else "") +
                          "Filename: " + name + ".ipk\n\n")
            (root / "var/lib/opkg/lists/test").write_text(index)
            before = hashlib.sha256(status.read_bytes()).hexdigest()
            result = subprocess.run([OPKG, "-f", str(config), "-o", str(root),
                                     "--noaction", "install", "application" if transitive else "consumer"],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertNotIn("error:", result.stdout.lower(), result.stdout)
            plan = set(re.findall(r"^Installing (\S+) ", result.stdout, flags=re.M))
            self.assertEqual(plan, set(expected), result.stdout)
            self.assertEqual(hashlib.sha256(status.read_bytes()).hexdigest(), before)

    def test_installed_alternative_in_either_position_and_hold_state(self):
        for flag in ("hold", "ok"):
            for dependency in ("runtime (= 1) | image-runtime", "image-runtime | runtime (= 1)"):
                with self.subTest(flag=flag, dependency=dependency):
                    self.check_plan(dependency, ["consumer"], flag=flag)

    def test_transitive_dependency_reuses_installed_image(self):
        self.check_plan("runtime (= 1) | image-runtime", ["consumer", "application"], transitive=True)

    def test_missing_image_uses_runtime_fallback(self):
        for dependency in ("runtime (= 1) | image-runtime", "image-runtime | runtime (= 1)"):
            with self.subTest(dependency=dependency):
                self.check_plan(dependency, ["runtime", "consumer"], installed=False)

    def test_uninstalled_or_group_selects_only_one_runtime(self):
        self.check_plan("runtime (= 1) | other-runtime (= 1)", ["runtime", "consumer"], installed=False)

    def test_unsuitable_installed_version_uses_valid_fallback(self):
        self.check_plan("image-runtime (= 2) | runtime (= 1)", ["runtime", "consumer"], flag="ok")

    def test_and_dependencies_remain_required(self):
        self.check_plan("image-runtime, runtime (= 1)", ["runtime", "consumer"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
