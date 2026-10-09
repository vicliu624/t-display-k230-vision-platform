"""Relocate installed libtool dependencies without guessing absent/ambiguous archives."""
import importlib.util
import shlex
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("sdk_export", Path(__file__).with_name("export-tdvp-sdk.py"))
export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(export)


class LibtoolRelocation(unittest.TestCase):
    def test_build_and_toolchain_archives_resolve_to_installed_sdk_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            libraries = root / "usr/lib"
            libraries.mkdir(parents=True)
            for name in ("libvorbis.la", "libstdc++.la", "libassuan.la"):
                (libraries / name).write_text("dependency_libs=''\n")
            consumer = libraries / "consumer.la"
            consumer.write_text("dependency_libs='=/runner/build/libvorbis/lib/libvorbis.la "
                                "=/toolchain/bin/../lib/libstdc++.la =/lib64/lp64d/libassuan.la -lm'\n")
            export.relocate_development_files(root, Path("/runner/host/triple/sysroot"),
                                             Path("/runner/build"), Path("/toolchain"))
            self.assertEqual(consumer.read_text(), "dependency_libs='=/usr/lib/libvorbis.la "
                             "=/usr/lib/libstdc++.la =/usr/lib/libassuan.la -lm'\n")

    def test_absent_and_ambiguous_installed_archives_are_rejected(self):
        for duplicates in (False, True):
            with self.subTest(duplicates=duplicates), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                libraries = root / "usr/lib"
                libraries.mkdir(parents=True)
                if duplicates:
                    (libraries / "libmissing.la").write_text("dependency_libs=''\n")
                    (libraries / "private").mkdir()
                    (libraries / "private/libmissing.la").write_text("dependency_libs=''\n")
                consumer = libraries / "consumer.la"
                consumer.write_text("dependency_libs='=/runner/build/package/libmissing.la'\n")
                with self.assertRaisesRegex(ValueError, "one installed archive"):
                    export.relocate_development_files(root, Path("/runner/host/triple/sysroot"), Path("/runner/build"))

    def test_source_prefix_siblings_are_not_rebound(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            libraries = root / "usr/lib"
            libraries.mkdir(parents=True)
            (libraries / "libsame.la").write_text("dependency_libs=''\n")
            consumer = libraries / "consumer.la"
            original = "dependency_libs='=/runner/build-backup/package/libsame.la'\n"
            consumer.write_text(original)
            export.relocate_development_files(root, Path("/runner/host/triple/sysroot"), Path("/runner/build"))
            value = shlex.split(consumer.read_text().strip().split("=", 1)[1])
            self.assertEqual(value, ["=/runner/build-backup/package/libsame.la"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
