"""Export the canonical patch contract for the Python-free Windows installer."""
import argparse
import importlib.util
import json
from pathlib import Path

_definition = importlib.util.spec_from_file_location(
    'lightshot_copy_contract', Path(__file__).with_name('package_copy.py'))
_copy = importlib.util.module_from_spec(_definition)
_definition.loader.exec_module(_copy)


def create_spec(dll_path):
    payload = Path(dll_path).read_bytes()
    _copy.validate_new_dll(payload)
    return {
        'schema_version': 1,
        'original_hashes': dict(_copy.EXPECTED_HASHES),
        'new_dll_sha256': _copy.sha256(payload),
        'patches': [
            {'offset': offset, 'original': before.hex(), 'replacement': after.hex()}
            for offset, before, after, _ in _copy.PATCHES],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    specification = create_spec(args.dll)
    args.output.write_text(json.dumps(specification, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
