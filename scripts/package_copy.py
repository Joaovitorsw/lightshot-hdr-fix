"""Create a version-locked experimental copy. Never launch or alter the source.

Python 3.10+, standard library only. The source must be the internal version
directory, not the installation root/updater. Destination must not exist.
This separates application files only: mutex, registry and user preferences
may still be shared. No recursive deletion or automatic rollback is performed.
On an I/O failure, inspect the partial destination; select a fresh destination
for another attempt. Do not run this concurrently with modifications to inputs.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import struct


EXPECTED_HASHES = {
    'Lightshot.dll': '04a27f0d1e89341722461119e00a10e00ec2a52f5e305961161ec4378e610e93',
    'DXGIODScreenshot.dll': '6acdcd817cc5df637aa4cd101c25c9e0a69c778347a7a40ce7511eeea26fd6f0',
    'Lightshot.exe': '0b899508777d7ed5159e2a99a5eff60c54d0724493df3d630525b837fa43aa51',
}
# Verified independently using pefile and the saved REA/Ghidra assembly.
# Image base 0x10000000; .text RVA 0x1000, raw start 0x400.
PATCHES = (
    (0x1061b, bytes.fromhex('757b'), bytes.fromhex('9090'),
     'Attempt alternate capture even when GDI reports success; RVA 0x1121b'),
    (0x10658, bytes.fromhex('a2cd880310'), bytes.fromhex('9090909090'),
     'Keep alternate engine available after transient failure; RVA 0x11258'),
    # The removed A2 instruction's absolute immediate has a HIGHLOW fixup.
    # Keep its offset but change type 3 to ABSOLUTE (0), so ASLR skips it.
    (0x76fae, bytes.fromhex('5932'), bytes.fromhex('5902'),
     'Disable relocation for removed store operand at RVA 0x11259'),
)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def patch_lightshot(data, *, expected_sha256):
    if sha256(data) != expected_sha256:
        raise ValueError('Lightshot.dll SHA-256 does not match the supported original')
    for offset, original, replacement, description in PATCHES:
        if data[offset:offset + len(original)] != original:
            raise ValueError(f'Unexpected patch bytes at file offset {offset:#x}')
    result = bytearray(data)
    for offset, original, replacement, description in PATCHES:
        result[offset:offset + len(original)] = replacement
    return bytes(result)


def reject_links(path):
    """Check lexical ancestry before resolving links (including Windows junctions)."""
    path = Path(path).absolute()
    for component in (path, *path.parents):
        try:
            info = component.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError(f'Symbolic link/reparse path refused: {component}')


def validate_new_dll(data):
    """Architecture guard; the build/harness separately verifies the export ABI."""
    try:
        if data[:2] != b'MZ':
            raise ValueError('Missing MZ')
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('Missing PE')
        machine = struct.unpack_from('<H', data, pe + 4)[0]
        flags = struct.unpack_from('<H', data, pe + 22)[0]
        if machine != 0x14c or not flags & 0x2000:
            raise ValueError('Wrong architecture or not a DLL')
    except (ValueError, struct.error) as error:
        raise ValueError('New DLL must be an x86 PE DLL') from error


def source_inventory(source):
    directories, files = [], []
    for current, children, names in os.walk(source, followlinks=False):
        for name in children:
            path = Path(current) / name
            reject_links(path)
            directories.append(path.relative_to(source))
        for name in names:
            path = Path(current) / name
            reject_links(path)
            if not stat.S_ISREG(path.stat().st_mode):
                raise ValueError(f'Source contains a nonregular file: {path}')
            files.append(path.relative_to(source))
    return directories, files


def package_copy(*, source, destination, new_dll):
    source, destination, new_dll = map(Path, (source, destination, new_dll))
    for path in (source, destination, new_dll):
        reject_links(path)
    source = source.resolve(strict=True)
    destination = destination.resolve(strict=False)
    new_dll = new_dll.resolve(strict=True)
    if (source == destination or source in destination.parents
            or destination in source.parents):
        raise ValueError('Source and destination must be separate directory trees')
    if destination.exists():
        raise ValueError('Destination already exists; choose a fresh directory')
    if not source.is_dir() or not destination.parent.is_dir():
        raise ValueError('Source and destination parent must be existing directories')
    reserved = {'backup', 'hdr-package-manifest.json'}
    if any(path.name.casefold() in reserved for path in source.iterdir()):
        raise ValueError('Source contains a reserved package output name')
    originals = {}
    for name, expected in EXPECTED_HASHES.items():
        reject_links(source / name)
        data = (source / name).read_bytes()
        if sha256(data) != expected:
            raise ValueError(f'{name} SHA-256 does not match supported original')
        originals[name] = data
    patched = patch_lightshot(originals['Lightshot.dll'],
                             expected_sha256=EXPECTED_HASHES['Lightshot.dll'])
    new_data = new_dll.read_bytes()
    validate_new_dll(new_data)
    directories, files = source_inventory(source)
    manifest = {
        'format_version': 1,
        'created_utc': datetime.now(timezone.utc).isoformat(),
        'source': str(source), 'destination': str(destination),
        'new_dll_source': str(new_dll),
        'original_hashes': {name: sha256(data) for name, data in originals.items()},
        'new_dll_sha256': sha256(new_data),
        'patched_lightshot_sha256': sha256(patched),
        'patches': [{'offset': hex(offset), 'original': before.hex(),
                     'replacement': after.hex(), 'reason': reason}
                    for offset, before, after, reason in PATCHES],
        'launcher': 'Lightshot.exe (internal executable; never launched by packager)',
        'isolation_limitations': 'Application files only; mutex, registry and user preferences may be shared.',
        'copied_files': {},
    }
    # Claim a fresh directory exclusively. No overwrite/merge mode is supported.
    reject_links(destination)
    destination.mkdir()
    for relative in directories:
        target = destination / relative
        reject_links(target)
        target.mkdir()
    for relative in files:
        original, target = source / relative, destination / relative
        reject_links(original)
        reject_links(target)
        with original.open('rb') as incoming, target.open('xb') as outgoing:
            shutil.copyfileobj(incoming, outgoing)
        manifest['copied_files'][relative.as_posix()] = sha256(target.read_bytes())
    # Detect required-file changes during copying before patching the copy.
    for name, data in originals.items():
        if (destination / name).read_bytes() != data:
            raise ValueError(f'Source changed while copying: {name}')
    backup = destination / 'backup'
    backup.mkdir()
    for name in ('Lightshot.dll', 'DXGIODScreenshot.dll'):
        with (backup / name).open('xb') as stream:
            stream.write(originals[name])
    for name, data in (('Lightshot.dll', patched), ('DXGIODScreenshot.dll', new_data)):
        target = destination / name
        reject_links(target)
        target.write_bytes(data)
        manifest['copied_files'][name] = sha256(target.read_bytes())
        if manifest['copied_files'][name] != sha256(data):
            raise OSError(f'Post-write verification failed: {name}')
    with (destination / 'HDR-package-manifest.json').open('x', encoding='utf-8') as stream:
        json.dump(manifest, stream, indent=2, ensure_ascii=False)
        stream.write('\n')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path,
                        help='Original internal version directory (5.5.0.7)')
    parser.add_argument('--destination', required=True, type=Path,
                        help='Fresh separate application directory; parent must exist')
    parser.add_argument('--new-dll', required=True, type=Path,
                        help='Built and harness-validated x86 capture DLL')
    arguments = parser.parse_args()
    try:
        manifest = package_copy(source=arguments.source, destination=arguments.destination,
                                new_dll=arguments.new_dll)
    except (ValueError, OSError) as error:
        parser.exit(1, f'Packaging refused/failed: {error}\n')
    print(json.dumps(manifest, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()
