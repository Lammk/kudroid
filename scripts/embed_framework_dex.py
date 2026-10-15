#!/usr/bin/env python3
"""Embed framework/build/framework.dex into C++ header for KuART.

DEX must reside in binary (not loaded from file) because iOS app bundle is read-only
and KuART needs the framework before mounting any VFS.
"""
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parent.parent
dex = root / 'framework/build/framework.dex'
out = root / 'include/kudroid/framework_dex_bytes.h'

if not dex.is_file():
    sys.exit(f'ERROR: {dex} does not exist — run framework/build.sh first')

data = dex.read_bytes()
if data[:4] != b'dex\n':
    sys.exit(f'ERROR: {dex} is not a valid DEX file (magic = {data[:4]!r})')

lines = [
    '// Auto-generated from framework/build/framework.dex — DO NOT EDIT MANUALLY.',
    '// Regenerate: bash framework/build.sh',
    '#pragma once',
    '#include <cstddef>',
    '#include <cstdint>',
    '',
    f'static const size_t g_framework_dex_size = {len(data)};',
    'static const uint8_t g_framework_dex_bytes[] = {',
]
for i in range(0, len(data), 12):
    lines.append('    ' + ', '.join(f'0x{b:02x}' for b in data[i:i + 12]) + ',')
lines.append('};')

out.parent.mkdir(parents=True, exist_ok=True)
out.write_text('\n'.join(lines) + '\n')
print(f'Generated {out} ({len(data)} bytes)')
