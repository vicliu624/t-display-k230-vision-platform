#!/usr/bin/env python3
"""Exercise real opkg upgrade planning with inert local IPKs and databases."""
import hashlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

OPKG = str(Path(sys.argv.pop(1)).resolve()) if len(sys.argv) > 1 else os.environ.get('TDVP_TEST_OPKG')
if not OPKG:
    raise SystemExit('provide a real opkg binary; this test must not silently skip')


class CoordinatedUpgrade(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='tdvp-upgrade-')
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.root = self.work / 'root'
        self.info = self.root / 'var/lib/opkg/info'
        self.lists = self.root / 'var/lib/opkg/lists'
        for directory in (self.info, self.lists, self.root / 'tmp'):
            directory.mkdir(parents=True, exist_ok=True)
        self.config = self.work / 'opkg.conf'
        self.config.write_text('dest root /\noption lists_dir /var/lib/opkg/lists\n'
            'option info_dir /var/lib/opkg/info\noption status_file /var/lib/opkg/status\n'
            'arch all 10\nsrc test ' + self.work.as_uri() + '\n')
        self.names = ('a-library', 'z-consumer', 'zz-profile')
        self.status = self.root / 'var/lib/opkg/status'
        records = []
        for name in self.names:
            dependency = {'a-library': '', 'z-consumer': 'a-library (= 1)', 'zz-profile': 'z-consumer (= 1)'}[name]
            records.append(f'Package: {name}\nVersion: 1\nArchitecture: all\n' +
                (f'Depends: {dependency}\n' if dependency else '') + 'Status: install user installed\n')
            (self.info / (name + '.list')).write_text('')
        self.status.write_text('\n'.join(records) + '\n')
        self.index = []
        for name in self.names:
            dependency = {'a-library': '', 'z-consumer': 'a-library (= 2)', 'zz-profile': 'z-consumer (= 2)'}[name]
            self.package(name, dependency)
        self.write_index()

    def package(self, name, dependency):
        parts = self.work / ('parts-' + name)
        parts.mkdir()
        (parts / 'debian-binary').write_text('2.0\n')
        control = (f'Package: {name}\nVersion: 2\nArchitecture: all\nDescription: inert fixture\n' +
                   (f'Depends: {dependency}\n' if dependency else '')).encode()
        with tarfile.open(parts / 'control.tar.gz', 'w:gz') as archive:
            member = tarfile.TarInfo('./control')
            member.mode = 0o644
            member.size = len(control)
            archive.addfile(member, io.BytesIO(control))
        with tarfile.open(parts / 'data.tar.gz', 'w:gz'):
            pass
        ipk = self.work / (name + '-2.ipk')
        subprocess.run(['ar', 'crD', str(ipk), 'debian-binary', 'control.tar.gz', 'data.tar.gz'], cwd=parts, check=True, capture_output=True)
        # The inert native test build disables SHA256/GPG; MD5 is supported.
        # Production target tests separately retain SHA256 and signatures.
        digest = hashlib.md5(ipk.read_bytes()).hexdigest()
        self.index.append(control.decode() + f'Filename: {ipk.name}\nSize: {ipk.stat().st_size}\nMD5Sum: {digest}\n\n')

    def write_index(self):
        (self.lists / 'test').write_text(''.join(self.index))

    def command(self, *arguments):
        return subprocess.run([OPKG, '-V2', '-f', str(self.config), '-o', str(self.root), *arguments],
            capture_output=True, text=True, env=dict(os.environ, IPKG_INSTROOT=str(self.root), PKG_ROOT=str(self.root)))

    def check_plan(self, *arguments):
        before = self.status.read_bytes()
        result = self.command('--noaction', *arguments)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('Upgrading a-library', result.stdout + result.stderr)
        self.assertEqual(self.status.read_bytes(), before)

    def test_default_whole_system_upgrade(self):
        self.check_plan('upgrade')

    def test_multiple_packages_in_provider_first_order(self):
        self.check_plan('upgrade', *self.names)

    def test_explicit_combine_and_duplicate_requests(self):
        self.check_plan('--combine', 'upgrade', *self.names, 'a-library', 'z-consumer', 'zz-profile')

    def test_unrequested_consumer_keeps_provider_old(self):
        before = self.status.read_bytes()
        result = self.command('--noaction', '--combine', 'upgrade', 'a-library', 'a-library')
        self.assertNotIn('Upgrading a-library', result.stdout + result.stderr)
        self.assertEqual(self.status.read_bytes(), before)

    def test_held_consumer_rejects_incompatible_batch(self):
        self.status.write_text(self.status.read_text().replace('Package: zz-profile\nVersion: 1\nArchitecture: all\nDepends: z-consumer (= 1)\nStatus: install user installed',
            'Package: zz-profile\nVersion: 1\nArchitecture: all\nDepends: z-consumer (= 1)\nStatus: install hold installed'))
        before = self.status.read_bytes()
        result = self.command('--noaction', 'upgrade')
        self.assertIn('marked hold', result.stdout + result.stderr)
        self.assertNotIn('Upgrading a-library', result.stdout + result.stderr)
        self.assertNotIn('Upgrading z-consumer', result.stdout + result.stderr)
        self.assertNotIn('Upgrading zz-profile', result.stdout + result.stderr)
        self.assertEqual(self.status.read_bytes(), before)

    def test_missing_dependency_rejects_plan(self):
        self.index[1] = self.index[1].replace('Depends: a-library (= 2)', 'Depends: a-library (= 2), absent-runtime (= 1)')
        self.write_index()
        before = self.status.read_bytes()
        result = self.command('--noaction', 'upgrade')
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.status.read_bytes(), before)

    def test_actual_default_upgrade_configures_all_versions(self):
        result = self.command('upgrade')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        # Offline roots defer configuration; fixtures contain no maintainer
        # scripts, so completing their state transition is safe and explicit.
        result = self.command('--force-postinstall', 'configure')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for name in self.names:
            self.assertIn(f'Package: {name}\nVersion: 2\n', self.status.read_text())
        self.assertEqual(self.status.read_text().count(' installed'), 3)


if __name__ == '__main__':
    unittest.main(verbosity=2)
