#!/usr/bin/env python3
"""Check a pinned engine candidate before device validation."""

import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
from pathlib import Path


REVIEWED_IMPORTS = {
    "__vsprintf_chk": "bionic___vsprintf_chk",
    "vprintf": "bionic_vprintf",
    "exit": "host C library process exit",
    "log10": "host libm double(double) via kSyscallSymbols",
}


def readelf(path, option):
    return subprocess.run(
        ["readelf", option, "--wide", str(path)], check=True,
        capture_output=True, text=True, timeout=30,
        env={**os.environ, "LC_ALL": "C"},
    ).stdout


def jni_signatures(data):
    jni_type = rb"(?:\[*[ZBCSIJFD]|\[*L[^;\x00]+;)"
    descriptor = rb"(?:^|\x00)(\(" + jni_type + rb"*\)(?:V|" + jni_type + rb"))(?=\x00)"
    return set(re.findall(descriptor, data))


def inspect(path):
    data = path.read_bytes()
    if len(data) < 64 or data[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError(f"{path}: expected little-endian ELF64")
    kind, machine = struct.unpack_from("<HH", data, 16)
    if kind != 3 or machine != 183:
        raise ValueError(f"{path}: expected ARM64 shared library")
    symbols = {}
    exports = set()
    imports = set()
    for line in readelf(path, "--dyn-syms").splitlines():
        parts = line.split()
        if len(parts) < 8 or not parts[0].rstrip(":").isdigit():
            continue
        if parts[6] != "UND":
            name = parts[7].split("@", 1)[0]
            symbols[name] = (int(parts[1], 16), int(parts[2], 0))
            if parts[4] in ("GLOBAL", "WEAK") and parts[5] in ("DEFAULT", "PROTECTED"):
                exports.add(name)
        else:
            imports.add(parts[7].split("@", 1)[0])
    dependencies = set(re.findall(r"\(NEEDED\).*\[([^\]]+)\]", readelf(path, "--dynamic")))
    build_id = re.search(r"Build ID: ([0-9a-f]+)", readelf(path, "--notes"))
    return {
        "data": data, "symbols": symbols, "exports": exports, "imports": imports,
        "dependencies": dependencies, "jni_signatures": jni_signatures(data),
        "build_id": build_id.group(1) if build_id else None,
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def snapshot_features(elf, symbol):
    data = elf["data"]
    address, size = elf["symbols"][symbol]
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phsize, phcount = struct.unpack_from("<HH", data, 54)
    if phsize < 56 or phoff + phsize * phcount > len(data):
        raise ValueError("Invalid ELF program headers")
    for index in range(phcount):
        kind, _, offset, vaddr, _, filesz, _, _ = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + index * phsize)
        if kind != 1 or address < vaddr or address - vaddr + size > filesz:
            continue
        start = offset + address - vaddr
        if size < 53 or start + size > len(data):
            raise ValueError("Invalid snapshot extent")
        snapshot = data[start:start + size]
        magic, length, snapshot_kind = struct.unpack_from("<Iqq", snapshot)
        if magic != 0xdcdcf5f5 or snapshot_kind != 3 or not 53 <= length + 4 <= size:
            raise ValueError("Expected a full AOT snapshot")
        version = snapshot[20:52].decode("ascii")
        end = snapshot.find(b"\x00", 52, length + 4)
        if end == -1:
            raise ValueError("Unterminated snapshot features")
        return version, snapshot[52:end].decode("ascii")
    raise ValueError("Snapshot symbol is outside ELF file mappings")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--version", default="3.32.5")
    parser.add_argument("--reference-metadata", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    reference = inspect(args.reference)
    candidate = inspect(args.candidate)
    app = inspect(args.snapshot)
    errors = []
    pins = json.loads((Path(__file__).parent / "versions.json").read_text())
    pin = next(p for p in pins if p["flutter_tag"] == args.version)
    if reference["build_id"] != pin["reference_build_id"] or reference["sha256"] != pin["reference_sha256"]:
        errors.append("Reference is outside the pinned engine family")
    if args.version != "3.32.5":
        metadata = json.loads((args.reference_metadata or args.reference.parent / "reference.json").read_text())
        for key in ("flutter_revision", "engine_revision", "dart_revision", "dart_version"):
            if metadata[key] != pin[key]:
                errors.append("Official reference metadata revision differs: " + key)
        if reference["build_id"] != metadata["reference_build_id"] or reference["sha256"] != metadata["reference_sha256"]:
            errors.append("Reference differs from the downloaded official artifact")
    snapshots = {}
    for name in ("_kDartVmSnapshotData", "_kDartIsolateSnapshotData"):
        version, features = snapshot_features(app, name)
        snapshots[name] = (version, features)
        if not re.fullmatch("[0-9a-f]{32}", version) or not features.startswith("product ") or not {"arm64", "android", "compressed-pointers"} <= set(features.split()):
            errors.append(f"{name}: unsupported snapshot version or target features")
        prefix = "vm" if name == "_kDartVmSnapshotData" else "isolate"
        if version != pin[prefix + "_snapshot_hash"] or features != pin[prefix + "_snapshot_features"]:
            errors.append(f"{name}: snapshot version or features differ from the pinned release")
    for label, engine in (("reference", reference), ("candidate", candidate)):
        for version, _ in snapshots.values():
            if version.encode() + b"\x00" not in engine["data"]:
                errors.append(f"{label}: snapshot hash missing")
        if (pin["dart_version"] + " (stable)").encode() not in engine["data"] or b'"android_arm64"' not in engine["data"]:
            errors.append(f"{label}: Dart platform/version differs")
        if "JNI_OnLoad" not in engine["exports"]:
            errors.append(f"{label}: JNI_OnLoad missing")
    if b"[KuDroidFlutterVM] compressed heap base=" not in candidate["data"]:
        errors.append("Candidate reservation diagnostic missing")
    missing_exports = sorted(reference["exports"] - candidate["exports"])
    extra_dependencies = sorted(candidate["dependencies"] - reference["dependencies"])
    extra_imports = sorted(candidate["imports"] - reference["imports"])
    unreviewed_imports = sorted(set(extra_imports) - REVIEWED_IMPORTS.keys())
    missing_signatures = reference["jni_signatures"] - candidate["jni_signatures"]
    if missing_exports:
        errors.append("Candidate removes exported symbols")
    if extra_dependencies:
        errors.append("Candidate adds shared-library dependencies")
    if unreviewed_imports:
        errors.append("Candidate adds imported symbols requiring shim review")
    if missing_signatures:
        errors.append("Candidate removes JNI signature strings")
    report = {
        "flutter_version": args.version,
        "flutter_revision": pin["flutter_revision"],
        "engine_revision": pin["engine_revision"],
        "dart_revision": pin["dart_revision"], "dart_version": pin["dart_version"],
        "original_build_id": reference["build_id"], "reference_sha256": reference["sha256"],
        "candidate_sha256": candidate["sha256"],
        "candidate_build_id": candidate["build_id"],
        "vm_snapshot_hash": snapshots["_kDartVmSnapshotData"][0],
        "vm_snapshot_features": snapshots["_kDartVmSnapshotData"][1],
        "isolate_snapshot_hash": snapshots["_kDartIsolateSnapshotData"][0],
        "isolate_snapshot_features": snapshots["_kDartIsolateSnapshotData"][1],
        "missing_exports": missing_exports, "extra_dependencies": extra_dependencies,
        "extra_imports": extra_imports,
        "reviewed_extra_imports": {name: REVIEWED_IMPORTS[name] for name in extra_imports
                                   if name in REVIEWED_IMPORTS},
        "unreviewed_imports": unreviewed_imports,
        "missing_jni_signatures": sorted(s.decode("ascii", "replace") for s in missing_signatures),
        "preflight_passed": not errors, "device_verified": False,
        "errors": errors,
    }
    encoded = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end="")
    return bool(errors)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, struct.error, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))
