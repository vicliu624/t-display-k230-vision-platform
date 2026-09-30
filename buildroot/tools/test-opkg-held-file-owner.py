#!/usr/bin/env python3
"""Adversarial native opkg file ownership tests; no target code is executed."""
import io
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

OPKG = str(Path(sys.argv.pop(1)).resolve())


class ProtectedOwnership(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='tdvp-owner-fixture-')
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.root = self.work / 'root'
        self.info = self.root / 'var/lib/opkg/info'
        self.info.mkdir(parents=True)
        (self.root / 'tmp').mkdir()
        self.config = self.work / 'opkg.conf'
        self.config.write_text('dest root /\noption info_dir /var/lib/opkg/info\n'
            'option status_file /var/lib/opkg/status\narch all 1\n')
        self.base = 'image-base'
        self.legacy = 'legacy-runtime'

    def command(self, *arguments):
        return subprocess.run([OPKG, '-f', str(self.config), '-o', str(self.root), *arguments],
            capture_output=True, text=True)

    def seed(self, reverse=False, legacy_name='legacy-runtime'):
        self.legacy = legacy_name
        shared = self.root / 'usr/lib/shared'
        shared.mkdir(parents=True)
        (shared / 'base.so.1').write_bytes(b'protected base\n')
        (shared / 'base.so.1').chmod(0o755)
        (shared / 'base.so').symlink_to('base.so.1')
        (shared / 'private').write_bytes(b'legacy private\n')
        base_paths = ['/usr/lib/shared', '/usr/lib/shared/base.so.1', '/usr/lib/shared/base.so']
        legacy_paths = base_paths + ['/usr/lib/shared/private']
        (self.info / (self.base + '.list')).write_text('\n'.join(base_paths) + '\n')
        (self.info / (self.legacy + '.list')).write_text('\n'.join(legacy_paths) + '\n')
        records = [f'Package: {self.base}\nVersion: 1\nArchitecture: all\n'
            'Essential: yes\nStatus: install hold installed\n',
            f'Package: {self.legacy}\nVersion: 1\nArchitecture: all\nStatus: install ok installed\n']
        if reverse:
            records.reverse()
        (self.root / 'var/lib/opkg/status').write_text('\n'.join(records) + '\n')
        self.base_paths = set(base_paths)

    def assert_base(self):
        shared = self.root / 'usr/lib/shared'
        self.assertEqual((shared / 'base.so.1').read_bytes(), b'protected base\n')
        self.assertEqual((shared / 'base.so.1').stat().st_mode & 0o7777, 0o755)
        self.assertTrue((shared / 'base.so').is_symlink())
        self.assertEqual(os.readlink(shared / 'base.so'), 'base.so.1')
        paths = {line.split('\t')[0] for line in
            (self.info / (self.base + '.list')).read_text().splitlines()}
        self.assertEqual(paths, self.base_paths)
        status = (self.root / 'var/lib/opkg/status').read_text()
        record = next(record for record in status.split('\n\n')
            if record.startswith('Package: ' + self.base + '\n'))
        self.assertIn('Status: install hold installed', record)
        self.assertIn('Essential: yes', record)

    def package(self, name, version, payload=None):
        archive = self.work / (name + '-' + version + '.ipk')
        parts = self.work / ('parts-' + name + '-' + version)
        parts.mkdir()
        (parts / 'debian-binary').write_text('2.0\n')
        control = f'Package: {name}\nVersion: {version}\nArchitecture: all\nDescription: inert fixture\n'.encode()
        for filename, members in [('control.tar.gz', {'./control': control}),
                                  ('data.tar.gz', payload or {})]:
            with tarfile.open(parts / filename, 'w:gz') as output:
                for path, content in members.items():
                    info = tarfile.TarInfo(path)
                    info.size = len(content)
                    info.mode = 0o644
                    output.addfile(info, io.BytesIO(content))
        subprocess.run(['ar', 'cr', str(archive), 'debian-binary', 'control.tar.gz', 'data.tar.gz'],
            cwd=parts, check=True, capture_output=True)
        return str(archive)

    def test_remove_duplicate_owner_preserves_base_in_both_status_orders(self):
        # opkg 0.7.0 traverses djb2 package hash buckets (1024):
        # legacy-a=296, image-base=336, legacy-runtime=523.
        # These names cover both actual ownership initialization orders.
        for name in ('legacy-a', 'legacy-runtime'):
            for reverse in (False, True):
                with self.subTest(name=name, reverse=reverse):
                    self.temp.cleanup()
                    self.setUp()
                    self.seed(reverse, name)
                    result = self.command('remove', self.legacy)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assert_base()
                    self.assertFalse((self.root / 'usr/lib/shared/private').exists())

    def test_empty_upgrade_then_remove_preserves_base(self):
        self.seed()
        result = self.command('install', self.package(self.legacy, '2'))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_base()
        self.assertFalse((self.root / 'usr/lib/shared/private').exists())
        result = self.command('remove', self.legacy)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_base()

    def test_conflicting_new_payload_is_rejected(self):
        self.seed()
        result = self.command('install', self.package('conflict', '1',
            {'./usr/lib/shared/base.so.1': b'conflicting bytes\n'}))
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_base()

    def test_conflicting_legacy_upgrade_is_rejected(self):
        self.seed()
        result = self.command('install', self.package(self.legacy, '2',
            {'./usr/lib/shared/base.so.1': b'conflicting upgrade\n'}))
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_base()

    def test_unprotected_ownership_transfer_still_works(self):
        self.seed()
        status = self.root / 'var/lib/opkg/status'
        status.write_text(status.read_text().replace('Essential: yes\n', '').replace('install hold', 'install ok'))
        result = self.command('--force-overwrite', 'install', self.package('new-owner', '1',
            {'./usr/lib/shared/base.so.1': b'ordinary overwrite\n'}))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / 'usr/lib/shared/base.so.1').read_bytes(), b'ordinary overwrite\n')


if __name__ == '__main__':
    unittest.main(verbosity=2)
