#!/usr/bin/env python3
"""Verify every catalog engine in a runtime directory, app bundle, or IPA."""

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path, PurePosixPath
import zipfile

BASE_SHA256 = 'df2715a416aac8dbcd63d81bc2171246af378e8dd9a92d2ece2f0b4489f6e3f1'
BASE_BUILD_ID = 'd512a1385f6d73f28f618369888b65cef4fe6629'


def build_id(data):
    if len(data) < 64 or data[:7] != b'\x7fELF\x02\x01\x01' or struct.unpack_from('<HH', data, 16) != (3, 183):
        raise ValueError('Expected an Android ARM64 ELF shared library')
    offset = struct.unpack_from('<Q', data, 32)[0]
    size, count = struct.unpack_from('<HH', data, 54)
    if size != 56 or not count or offset + size * count > len(data):
        raise ValueError('Invalid program headers')
    for index in range(count):
        kind, _, start, _, _, length, memory, _ = struct.unpack_from('<IIQQQQQQ', data, offset + index * size)
        if start + length > len(data) or (kind == 1 and length > memory):
            raise ValueError('Invalid ELF segment')
        if kind != 4:
            continue
        end = start + length
        while start + 12 <= end:
            namesz, descsz, note = struct.unpack_from('<III', data, start)
            start += 12
            name_bytes, desc_bytes = (namesz + 3) & ~3, (descsz + 3) & ~3
            if start + name_bytes + desc_bytes > end:
                raise ValueError('Invalid ELF note')
            if note == 3 and namesz == 4 and descsz == 20 and data[start:start + 4] == b'GNU\0':
                return data[start + name_bytes:start + name_bytes + descsz].hex()
            start += name_bytes + desc_bytes
    raise ValueError('GNU build ID missing')


def safe_path(name):
    path = PurePosixPath(name)
    if not name or '\\' in name or '\0' in name or path.is_absolute() or any(p in ('.', '..') for p in name.split('/')):
        raise ValueError('Unsafe runtime path')
    return path


def verify_catalog(read):
    manifest = json.loads(read('manifest.json'))
    if manifest.get('schema_version') != 1 or not isinstance(manifest.get('engines'), list) or not manifest['engines']:
        raise ValueError('Invalid runtime catalog')
    pins = {r['flutter_tag']: r for r in json.loads((Path(__file__).parent / 'versions.json').read_text())}
    identities = set()
    versions = []
    for record in manifest['engines']:
        version = record['flutter_version']
        pin = pins[version]
        for field in ('flutter_revision', 'engine_revision', 'dart_revision', 'dart_version'):
            if record[field] != pin[field]:
                raise ValueError(f'{version}: incorrect {field}')
        for field in ('vm_snapshot_hash', 'vm_snapshot_features', 'isolate_snapshot_hash', 'isolate_snapshot_features'):
            if record[field] != pin[field]:
                raise ValueError(f'{version}: incorrect {field}')
        if record['original_build_id'] != pin['reference_build_id']:
            raise ValueError(f'{version}: incorrect original engine identity')
        if record['target'] != 'android-arm64-release' or record.get('static_verified') is not True or not isinstance(record.get('device_verified'), bool):
            raise ValueError(f'{version}: invalid verification status or target')
        for field, length in (('original_build_id', 40), ('candidate_build_id', 40), ('candidate_sha256', 64), ('vm_snapshot_hash', 32), ('isolate_snapshot_hash', 32)):
            if not isinstance(record[field], str) or not re.fullmatch('[0-9a-f]{' + str(length) + '}', record[field]):
                raise ValueError(f'{version}: invalid {field}')
        for field in ('vm_snapshot_features', 'isolate_snapshot_features'):
            if not isinstance(record[field], str) or not record[field] or '\0' in record[field]:
                raise ValueError(f'{version}: invalid snapshot features')
        identity = tuple(record[field] for field in ('original_build_id', 'vm_snapshot_hash', 'vm_snapshot_features', 'isolate_snapshot_hash', 'isolate_snapshot_features'))
        if identity in identities:
            raise ValueError('Ambiguous catalog identity')
        identities.add(identity)
        path = safe_path(record['path'])
        if len(path.parts) < 2 or path.parts[0] != version or path.name != 'libflutter.so':
            raise ValueError(f'{version}: incorrect artifact path')
        data = read(str(path))
        if hashlib.sha256(data).hexdigest() != record['candidate_sha256'] or build_id(data) != record['candidate_build_id']:
            raise ValueError(f'{version}: artifact identity differs')
        if version == '3.32.5' and (record['candidate_sha256'] != BASE_SHA256 or record['candidate_build_id'] != BASE_BUILD_ID):
            raise ValueError('Existing verified 3.32.5 artifact changed')
        for field in ('vm_snapshot_hash', 'isolate_snapshot_hash'):
            if record[field].encode() + b'\0' not in data:
                raise ValueError(f'{version}: snapshot hash missing from candidate')
        if b'[KuDroidFlutterVM] compressed heap base=' not in data:
            raise ValueError(f'{version}: reservation diagnostic missing')
        for license_name in ('FLUTTER_LICENSE.txt', 'DART_LICENSE.txt'):
            if not read(str(path.parent / license_name)).strip():
                raise ValueError(f'{version}: license missing')
        if version != '3.32.5':
            if not read(str(path.parent / 'THIRD_PARTY_LICENSES.md')).strip():
                raise ValueError(f'{version}: upstream notices missing')
            report = json.loads(read(str(path.parent / 'validation.json')))
            if report.get('preflight_passed') is not True or report.get('candidate_sha256') != record['candidate_sha256'] or report.get('candidate_build_id') != record['candidate_build_id']:
                raise ValueError(f'{version}: preflight report differs')
        versions.append(version)
        print(f"Verified Flutter {version}: {len(data)} bytes, device_verified={record['device_verified']}")
    if '3.32.5' not in versions:
        raise ValueError('Existing 3.32.5 catalog record missing')
    return versions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path', type=Path)
    parser.add_argument('--asset', action='store_true')
    parser.add_argument('--require-all', action='store_true')
    args = parser.parse_args()
    if args.path.suffix == '.ipa':
        with zipfile.ZipFile(args.path) as archive:
            names = archive.namelist()
            roots = [name[:-10] for name in names if name.startswith('Payload/') and name.count('/') == 2 and name.endswith('/Info.plist')]
            if len(roots) != 1:
                raise ValueError('Expected one launcher app in IPA')
            def read(name):
                safe_path(name)
                member = roots[0] + 'runtime/' + name
                if names.count(member) != 1:
                    raise ValueError('Missing or duplicate runtime member: ' + member)
                return archive.read(member)
            versions = verify_catalog(read)
    else:
        runtime = (args.path if args.asset else args.path / 'runtime').resolve(strict=True)
        def read(name):
            safe_path(name)
            file = (runtime / name).resolve(strict=True)
            if not file.is_relative_to(runtime):
                raise ValueError('Runtime file escapes directory')
            return file.read_bytes()
        versions = verify_catalog(read)
    if args.require_all and set(versions) != {'3.32.5', '3.24.5', '3.22.2', '3.16.9', '3.10.6'}:
        raise ValueError('Catalog does not contain all planned releases')
    print(f'Verified {len(versions)} catalog entries in {args.path}')


if __name__ == '__main__':
    main()
