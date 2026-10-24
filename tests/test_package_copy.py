"""Offline packaging contracts; fixtures never read or run an installed application."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts' / 'package_copy.py'
spec = importlib.util.spec_from_file_location('package_copy', SCRIPT)
pkg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pkg)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def dll_fixture():
    data = bytearray(256)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<H', data, 0x84, 0x14c)
    struct.pack_into('<H', data, 0x96, 0x2000)
    return bytes(data)


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / '5.5.0.7'
        self.source.mkdir()
        self.destination = self.root / 'Lightshot-HDR'
        self.lightshot = bytearray(0x77000)
        self.lightshot[0x1061b:0x1061d] = b'\x75\x7b'
        self.lightshot[0x10658:0x1065d] = b'\xa2\xcd\x88\x03\x10'
        self.lightshot[0x76fae:0x76fb0] = b'\x59\x32'
        self.files = {'Lightshot.dll': bytes(self.lightshot),
                      'DXGIODScreenshot.dll': b'original capture',
                      'Lightshot.exe': b'internal launcher'}
        for name, data in self.files.items():
            (self.source / name).write_bytes(data)
        (self.source / 'resources').mkdir()
        (self.source / 'resources' / 'language.txt').write_text('resource')
        self.new = self.root / 'new.dll'
        self.new.write_bytes(dll_fixture())
        self.hashes = {name: digest(data) for name, data in self.files.items()}
        self.addCleanup(patch.stopall)
        patch.object(pkg, 'EXPECTED_HASHES', self.hashes).start()

    def run_package(self, destination=None):
        return pkg.package_copy(source=self.source,
                                destination=destination or self.destination,
                                new_dll=self.new)

    def test_copy_preserves_source_resources_backups_and_auditable_manifest(self):
        manifest = self.run_package()
        copied = (self.destination / 'Lightshot.dll').read_bytes()
        self.assertEqual(copied[0x1061b:0x1061d], b'\x90\x90')
        self.assertEqual(copied[0x10658:0x1065d], b'\x90' * 5)
        for name, original in self.files.items():
            self.assertEqual((self.source / name).read_bytes(), original)
        self.assertEqual((self.destination / 'backup' / 'Lightshot.dll').read_bytes(), self.files['Lightshot.dll'])
        self.assertEqual((self.destination / 'backup' / 'DXGIODScreenshot.dll').read_bytes(), self.files['DXGIODScreenshot.dll'])
        self.assertEqual((self.destination / 'DXGIODScreenshot.dll').read_bytes(), self.new.read_bytes())
        self.assertEqual((self.destination / 'resources' / 'language.txt').read_text(), 'resource')
        self.assertEqual(manifest, json.loads((self.destination / 'HDR-package-manifest.json').read_text()))
        self.assertEqual(manifest['original_hashes'], self.hashes)
        self.assertEqual(manifest['new_dll_sha256'], digest(self.new.read_bytes()))
        self.assertEqual(manifest['patched_lightshot_sha256'], digest(copied))
        self.assertEqual(len(manifest['patches']), 3)
        self.assertIn('registry', manifest['isolation_limitations'])

    def test_hash_mismatch_fails_before_destination_creation(self):
        for name in self.files:
            with self.subTest(name=name):
                path = self.source / name
                path.write_bytes(b'wrong version')
                with self.assertRaisesRegex(ValueError, 'SHA-256'):
                    self.run_package()
                self.assertFalse(self.destination.exists())
                path.write_bytes(self.files[name])

    def test_patch_bytes_must_match_even_with_matching_digest(self):
        for offset in (0x1061b, 0x10658, 0x76fae):
            with self.subTest(offset=offset):
                damaged = bytearray(self.lightshot)
                damaged[offset] = 0
                with self.assertRaisesRegex(ValueError, 'patch bytes'):
                    pkg.patch_lightshot(bytes(damaged), expected_sha256=digest(damaged))

    def test_disabled_absolute_store_has_no_aslr_relocation_into_nops(self):
        result = pkg.patch_lightshot(bytes(self.lightshot),
                                    expected_sha256=digest(self.lightshot))
        relocation, = struct.unpack_from('<H', result, 0x76fae)
        self.assertEqual(relocation >> 12, 0, 'Removed instruction immediate must not be relocated')
        self.assertEqual(relocation & 0xfff, 0x259)
        # Emulate Windows HIGHLOW handling for this independently verified page.
        relocated = bytearray(result)
        kind, offset = relocation >> 12, relocation & 0xfff
        if kind == 3:
            operand_file_offset = 0x11000 + offset - 0x1000 + 0x400
            old, = struct.unpack_from('<I', relocated, operand_file_offset)
            struct.pack_into('<I', relocated, operand_file_offset, (old + 0x10000000) & 0xffffffff)
        self.assertEqual(relocated[0x10658:0x1065d], b'\x90' * 5)

    def test_patch_wrong_digest_rejected(self):
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            pkg.patch_lightshot(bytes(self.lightshot), expected_sha256='0' * 64)

    def test_rejects_same_nested_or_ancestor_destination(self):
        for destination in (self.source, self.source / 'copy', self.root):
            with self.subTest(destination=destination):
                with self.assertRaises(ValueError):
                    self.run_package(destination)
        self.assertFalse((self.source / 'copy').exists())

    def test_existing_destination_is_never_overwritten(self):
        self.destination.mkdir()
        marker = self.destination / 'keep.txt'
        marker.write_text('keep')
        with self.assertRaises(ValueError):
            self.run_package()
        self.assertEqual(marker.read_text(), 'keep')

    def test_rejects_linked_destination_ancestor(self):
        real = self.root / 'real'
        real.mkdir()
        link = self.root / 'link'
        try:
            link.symlink_to(real, target_is_directory=True)
        except OSError as error:
            self.skipTest(f'Symlink creation unavailable: {error}')
        with self.assertRaisesRegex(ValueError, 'link|reparse'):
            self.run_package(link / 'copy')
        self.assertFalse((real / 'copy').exists())

    def test_rejects_reparse_attribute_without_following(self):
        fake = type('Stat', (), {'st_file_attributes': 0x400, 'st_mode': 0o100644})()
        with patch.object(Path, 'lstat', return_value=fake):
            with self.assertRaisesRegex(ValueError, 'link|reparse'):
                pkg.reject_links(self.destination)

    @unittest.skipUnless(os.name == 'nt', 'Windows junction contract')
    def test_rejects_real_windows_junction_without_symlink_privilege(self):
        real = self.root / 'real'
        real.mkdir()
        junction = self.root / 'junction'
        result = subprocess.run(['cmd', '/c', 'mklink', '/J', str(junction), str(real)],
                                capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        with self.assertRaisesRegex(ValueError, 'link|reparse'):
            self.run_package(junction / 'copy')
        self.assertFalse((real / 'copy').exists())

    @unittest.skipUnless(os.name == 'nt', 'Windows junction contract')
    def test_rejects_junction_in_source_tree_before_copy(self):
        junction = self.source / 'linked-resource'
        result = subprocess.run(['cmd', '/c', 'mklink', '/J', str(junction), str(self.root)],
                                capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        with self.assertRaisesRegex(ValueError, 'link|reparse'):
            self.run_package()
        self.assertFalse(self.destination.exists())

    def test_rejects_invalid_or_non_x86_new_dll_before_copy(self):
        for data in (b'not a PE', dll_fixture()[:120],
                     dll_fixture()[:0x84] + b'\x64\x86' + dll_fixture()[0x86:]):
            with self.subTest(data=data[:4]):
                self.new.write_bytes(data)
                with self.assertRaisesRegex(ValueError, 'x86|PE'):
                    self.run_package()
                self.assertFalse(self.destination.exists())

    def test_rejects_reserved_output_names_in_source(self):
        (self.source / 'backup').mkdir()
        with self.assertRaisesRegex(ValueError, 'reserved'):
            self.run_package()
        self.assertFalse(self.destination.exists())


if __name__ == '__main__':
    unittest.main()
