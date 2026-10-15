#!/usr/bin/env python3
"""Verify that the checked-in C++ framework image is byte-for-byte current."""
import hashlib
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
dex_path = root / "framework/build/framework.dex"
header_path = root / "include/kudroid/framework_dex_bytes.h"

if not dex_path.is_file():
    sys.exit(f"ERROR: missing {dex_path}; run framework/build.sh first")
if not header_path.is_file():
    sys.exit(f"ERROR: missing {header_path}; run framework/build.sh first")

dex = dex_path.read_bytes()
header = header_path.read_text(encoding="utf-8")
size_match = re.search(r"g_framework_dex_size\s*=\s*(\d+)\s*;", header)
if not size_match:
    sys.exit("ERROR: framework header has no g_framework_dex_size")
declared = int(size_match.group(1))
if declared != len(dex):
    sys.exit(f"ERROR: size mismatch: header={declared}, dex={len(dex)}")

array_match = re.search(r"g_framework_dex_bytes\[\]\s*=\s*\{(.*?)\};", header, re.S)
if not array_match:
    sys.exit("ERROR: framework header has no byte array")
tokens = re.findall(r"0x([0-9a-fA-F]{1,2})", array_match.group(1))
embedded = bytes(int(token, 16) for token in tokens)
if embedded != dex:
    common = min(len(embedded), len(dex))
    mismatch = next((i for i in range(common) if embedded[i] != dex[i]), common)
    sys.exit("ERROR: framework bytes differ at offset 0x%x (header=%s dex=%s)" % (
        mismatch,
        embedded[mismatch:mismatch + 4].hex(),
        dex[mismatch:mismatch + 4].hex(),
    ))

digest = hashlib.sha256(dex).hexdigest()
print(f"framework.dex verified: {len(dex)} bytes sha256={digest}")
