#!/usr/bin/env python3
"""Nhúng framework/build/framework.dex vào header C++ cho KuART.

DEX phải nằm trong binary (không đọc từ file) vì trên iOS app bundle là read-only
và KuART cần framework ngay trước khi mount được bất cứ thứ gì.
"""
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parent.parent
dex = root / 'framework/build/framework.dex'
out = root / 'include/kudroid/framework_dex_bytes.h'

if not dex.is_file():
    sys.exit(f'ERROR: {dex} không tồn tại — chạy framework/build.sh trước')

data = dex.read_bytes()
if data[:4] != b'dex\n':
    sys.exit(f'ERROR: {dex} không phải file DEX (magic = {data[:4]!r})')

lines = [
    '// Tự sinh từ framework/build/framework.dex — ĐỪNG sửa tay.',
    '// Sinh lại: bash framework/build.sh',
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
print(f'Đã sinh {out} ({len(data)} bytes)')
