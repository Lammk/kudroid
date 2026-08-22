#!/usr/bin/env python3
"""Nhúng framework/build/framework.jar vào header C++.

Không dùng `xxd -i` như scripts/embed_framework_jar.sh vì nó sinh tên biến theo
đường dẫn file (framework_build_framework_jar), lệch với tên mà DexAotCache và
kudroid_jni tham chiếu.
"""
import sys, pathlib

jar = pathlib.Path('framework/build/framework.jar')
out = pathlib.Path('include/kudroid/framework_jar_bytes.h')
if not jar.is_file():
    sys.exit(f'ERROR: {jar} not found — run framework/build.sh first')

data = jar.read_bytes()
lines = ['// Auto-generated from framework/build/framework.jar',
         '#pragma once',
         '#include <cstdint>',
         '#include <cstddef>',
         '',
         f'static const size_t g_framework_jar_size = {len(data)};',
         'static const uint8_t g_framework_jar_bytes[] = {']
for i in range(0, len(data), 12):
    lines.append('    ' + ', '.join(f'0x{b:02x}' for b in data[i:i + 12]) + ',')
lines.append('};')
out.write_text('\n'.join(lines) + '\n')
print(f'Generated {out} ({len(data)} bytes)')
