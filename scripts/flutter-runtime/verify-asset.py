#!/usr/bin/env python3
"""Verify a staged runtime or its exact location inside an app/IPA."""

import argparse
import hashlib
import json
from pathlib import Path
import zipfile

SHA256 = "df2715a416aac8dbcd63d81bc2171246af378e8dd9a92d2ece2f0b4489f6e3f1"
BUILD_ID = "d512a1385f6d73f28f618369888b65cef4fe6629"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    parser.add_argument("--asset", action="store_true", help="path is the staged runtime directory")
    args = parser.parse_args()
    if args.path.suffix == ".ipa":
        with zipfile.ZipFile(args.path) as archive:
            roots = [name[:-10] for name in archive.namelist()
                     if name.startswith("Payload/") and name.count("/") == 2 and name.endswith("/Info.plist")]
            if len(roots) != 1:
                raise ValueError("Expected one launcher app in IPA")
            data = archive.read(roots[0] + "runtime/libflutter.so")
            manifest = json.loads(archive.read(roots[0] + "runtime/manifest.json"))
    else:
        runtime = args.path if args.asset else args.path / "runtime"
        data = (runtime / "libflutter.so").read_bytes()
        manifest = json.loads((runtime / "manifest.json").read_text())
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError("Runtime artifact SHA-256 differs")
    if manifest.get("candidate_sha256") != SHA256 or manifest.get("candidate_build_id") != BUILD_ID:
        raise ValueError("Runtime manifest differs")
    print(f"Verified pinned runtime in {args.path}: {len(data)} bytes")


if __name__ == "__main__":
    main()
