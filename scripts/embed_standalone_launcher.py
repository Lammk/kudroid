#!/usr/bin/env python3
"""Preserve the executable launcher in an unsigned app or IPA."""
from pathlib import Path
import os
import copy
import plistlib
import shutil
import struct
import sys
import tempfile
import zipfile

MAGIC = b'KUDLCH01'
RESOURCE = 'StandaloneLauncher.bin'


def check_header(header):
    if len(header) < 16 or header[:4] != b'\xcf\xfa\xed\xfe' or struct.unpack_from('<I', header, 12)[0] != 2:
        raise ValueError('A native arm64 executable launcher is required')


def embed_app(path):
    info = plistlib.loads((path / 'Info.plist').read_bytes())
    executable = path / info['CFBundleExecutable']
    with executable.open('rb') as source:
        check_header(source.read(16))
        source.seek(0)
        target = path / RESOURCE
        with target.open('wb') as output:
            output.write(MAGIC)
            shutil.copyfileobj(source, output, 1024 * 1024)
    target.chmod(0o644)


def embed_ipa(path):
    fd, temporary = tempfile.mkstemp(prefix=path.name + '-', suffix='.tmp', dir=path.parent)
    os.close(fd)
    try:
        with zipfile.ZipFile(path) as source, zipfile.ZipFile(temporary, 'w', allowZip64=True) as output:
            plists = [name for name in source.namelist() if name.startswith('Payload/') and name.count('/') == 2 and name.endswith('/Info.plist')]
            if len(plists) != 1:
                raise ValueError('IPA must contain exactly one launcher app')
            root = plists[0][:-len('Info.plist')]
            info = plistlib.loads(source.read(plists[0]))
            executable = root + info['CFBundleExecutable']
            with source.open(executable) as binary:
                check_header(binary.read(16))
            for entry in source.infolist():
                if entry.filename == root + RESOURCE:
                    continue
                with source.open(entry) as incoming, output.open(copy.copy(entry), 'w', force_zip64=entry.file_size >= 0xffffffff) as outgoing:
                    shutil.copyfileobj(incoming, outgoing, 1024 * 1024)
            entry = zipfile.ZipInfo(root + RESOURCE)
            entry.create_system = 3
            entry.external_attr = 0o100644 << 16
            entry.file_size = source.getinfo(executable).file_size + len(MAGIC)
            with source.open(executable) as binary, output.open(entry, 'w', force_zip64=entry.file_size >= 0xffffffff) as outgoing:
                outgoing.write(MAGIC)
                shutil.copyfileobj(binary, outgoing, 1024 * 1024)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


if __name__ == '__main__':
    path = Path(sys.argv[1]).resolve()
    if path.is_dir():
        embed_app(path)
    else:
        embed_ipa(path)
    print('Embedded standalone launcher:', path)
