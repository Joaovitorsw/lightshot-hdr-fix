import importlib.util
from pathlib import Path
import tempfile
import unittest

from test_package_copy import dll_fixture, pkg


class InstallerSpecTests(unittest.TestCase):
    def load_exporter(self):
        filename = Path(__file__).resolve().parents[1] / 'scripts' / 'export_installer_spec.py'
        spec = importlib.util.spec_from_file_location('installer_spec', filename)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_export_binds_exact_payload_and_canonical_patch_contract(self):
        exporter = self.load_exporter()
        with tempfile.TemporaryDirectory() as directory:
            dll = Path(directory) / 'capture.dll'
            dll.write_bytes(dll_fixture())
            result = exporter.create_spec(dll)
        self.assertEqual(result['schema_version'], 1)
        self.assertEqual(result['original_hashes'], pkg.EXPECTED_HASHES)
        self.assertEqual(result['new_dll_sha256'], pkg.sha256(dll_fixture()))
        self.assertEqual(result['patches'], [
            {'offset': offset, 'original': before.hex(), 'replacement': after.hex()}
            for offset, before, after, _ in pkg.PATCHES])

    def test_invalid_payload_is_rejected(self):
        exporter = self.load_exporter()
        with tempfile.TemporaryDirectory() as directory:
            dll = Path(directory) / 'capture.dll'
            dll.write_bytes(b'not a DLL')
            with self.assertRaises(ValueError):
                exporter.create_spec(dll)


if __name__ == '__main__':
    unittest.main()
