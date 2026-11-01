#!/usr/bin/env python3
"""Exercise compatibility rejection and original-path loader identity."""

import argparse
import copy
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--version", default="3.32.5")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    original_data = args.reference.read_bytes()
    app_data = args.snapshot.read_bytes()
    candidate_data = args.candidate.read_bytes()
    manifest = json.loads((args.manifest or root / "ios-app/KuDroidShell/Resources/runtime/manifest.json").read_text())
    full_catalog = copy.deepcopy(manifest)
    record = next(r for r in manifest["engines"] if r["flutter_version"] == args.version)
    record = copy.deepcopy(record)
    record["path"] = args.version + "/libflutter.so"
    manifest = {"schema_version": 1, "engines": [record]}
    tests = 0
    with tempfile.TemporaryDirectory(prefix="kudroid-flutter-selector-") as temp:
        directory = Path(temp)
        executable = directory / "selector-test"
        flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"] if args.sanitize else []
        run([os.environ.get("CXX", "c++"), *flags, "-std=c++17", "-DKUDROID_HAS_OPENSSL=1",
             "-I", str(root / "include"), str(root / "scripts/flutter-runtime/test-selector.cpp"),
             str(root / "src/FlutterRuntime.cpp"), str(root / "src/library_manager.cpp"),
             "-lcrypto", "-pthread", "-o", str(executable)])
        original = directory / "libflutter.so"
        app = directory / "libapp.so"
        runtime = directory / "runtime"
        candidate = runtime / record["path"]
        candidate.parent.mkdir(parents=True)
        other = directory / "other/libflutter.so"
        other.parent.mkdir()
        other.write_bytes(original_data)
        shutil.copy2(args.snapshot, other.parent / "libapp.so")
        alias = directory / "alias"
        alias.symlink_to(directory, target_is_directory=True)

        def check(label, expected, original_bytes=original_data, app_bytes=app_data,
                  candidate_bytes=candidate_data, mode="selector", requested=None,
                  catalog=manifest):
            nonlocal tests
            manifest_path = runtime / "manifest.json"
            if catalog is None:
                manifest_path.unlink(missing_ok=True)
            else:
                manifest_path.write_text(catalog if isinstance(catalog, str) else json.dumps(catalog))
            original.write_bytes(original_bytes)
            if app_bytes is None:
                app.unlink(missing_ok=True)
            else:
                app.write_bytes(app_bytes)
            if candidate_bytes is None:
                candidate.unlink(missing_ok=True)
            else:
                candidate.write_bytes(candidate_bytes)
            command = [str(executable), mode, str(requested or original), str(candidate), str(int(expected))]
            if mode in ("loader", "map-failure"):
                command.append(str(other))
            run(command)
            tests += 1
            print("PASS:", label)

        check("pinned engine and both AOT snapshots", True)
        check("full bundled catalog picks the matching identity", True, catalog=full_catalog)
        check("unknown original engine", False,
              original_bytes=original_data.replace(bytes.fromhex(record["original_build_id"]), b"\0" * 20))
        check("missing AOT library", False, app_bytes=None)
        check("malformed ELF header", False, app_bytes=b"\x7fELF")
        changed = bytearray(app_data)
        struct.pack_into("<Q", changed, 32, len(changed) + 4096)
        check("out-of-range program headers", False, app_bytes=changed)
        features = record["vm_snapshot_features"].encode()
        positions = []
        position = -1
        while True:
            position = app_data.find(features + b"\0", position + 1)
            if position == -1:
                break
            positions.append(position)
        assert len(positions) == 2, "expected separate VM and isolate snapshot feature strings"
        for index, position in enumerate(positions):
            changed = bytearray(app_data)
            changed[position] = ord("x")
            check(f"snapshot {index + 1} features rejected independently", False, app_bytes=changed)
            changed = bytearray(app_data)
            changed[position - 32] = ord("x")
            check(f"snapshot {index + 1} version rejected independently", False, app_bytes=changed)
            changed = bytearray(app_data)
            struct.pack_into("<q", changed, position - 48, len(changed) * 2)
            check(f"snapshot {index + 1} oversized extent rejected", False, app_bytes=changed)
        check("missing runtime asset", False, candidate_bytes=None)
        check("truncated runtime asset", False, candidate_bytes=candidate_data[:64])
        changed = bytearray(candidate_data)
        changed[-1] ^= 1
        check("modified candidate with intact build ID", False, candidate_bytes=changed)
        check("missing catalog", False, catalog=None)
        check("malformed JSON", False, catalog="{")
        check("trailing JSON rejected", False, catalog=json.dumps(manifest) + " {}")
        check("deeply nested JSON rejected", False, catalog="[" * 1000 + "]" * 1000)
        check("oversized JSON rejected", False, catalog=" " * 65537)
        changed_manifest = copy.deepcopy(manifest)
        changed_manifest["schema_version"] = 2
        check("unknown schema rejected", False, catalog=changed_manifest)
        for field in ("static_verified", "candidate_sha256", "candidate_build_id", "vm_snapshot_hash", "isolate_snapshot_features"):
            changed_manifest = copy.deepcopy(manifest)
            del changed_manifest["engines"][0][field]
            check(f"missing {field} rejected", False, catalog=changed_manifest)
        changed_manifest = copy.deepcopy(manifest)
        changed_manifest["engines"][0]["static_verified"] = False
        check("unverified record rejected", False, catalog=changed_manifest)
        for unsafe in ("../libflutter.so", str(candidate), "../runtime/" + record["path"]):
            changed_manifest = copy.deepcopy(manifest)
            changed_manifest["engines"][0]["path"] = unsafe
            check("unsafe relative or absolute path rejected", False, catalog=changed_manifest)
        escape = runtime / "escape"
        escape.symlink_to(directory, target_is_directory=True)
        changed_manifest = copy.deepcopy(manifest)
        changed_manifest["engines"][0]["path"] = "escape/libflutter.so"
        check("symlink escaping runtime rejected", False, catalog=changed_manifest)
        changed_manifest = copy.deepcopy(manifest)
        changed_manifest["engines"].append(copy.deepcopy(record))
        check("ambiguous catalog rejected", False, catalog=changed_manifest)
        changed_manifest = copy.deepcopy(manifest)
        variant = copy.deepcopy(record)
        variant["vm_snapshot_hash"] = "2" * 32
        changed_manifest["engines"].append(variant)
        check("different snapshot variants remain distinct", True, catalog=changed_manifest)
        changed_manifest = copy.deepcopy(manifest)
        variant = copy.deepcopy(record)
        variant["isolate_snapshot_features"] += " extra-feature"
        changed_manifest["engines"].append(variant)
        check("same snapshot hash with different features remains distinct", True, catalog=changed_manifest)
        changed_manifest = copy.deepcopy(manifest)
        changed_manifest["engines"].append({"static_verified": False})
        check("an unfinished unrelated record does not hide a valid engine", True, catalog=changed_manifest)
        changed_manifest = copy.deepcopy(manifest)
        variant = copy.deepcopy(record)
        variant["original_build_id"] = "1" * 40
        changed_manifest["engines"].append(variant)
        check("same snapshot in a different original identity", True, catalog=changed_manifest,
              original_bytes=original_data.replace(bytes.fromhex(record["original_build_id"]), bytes.fromhex(variant["original_build_id"])))
        check("one loader for original path, soname and physical alias", True, mode="loader")
        check("missing override retains original identity", False, candidate_bytes=None, mode="loader")
        check("snapshot mismatch retains original identity", False,
              app_bytes=app_data.replace(features, features.replace(b"product", b"xroduct")), mode="loader")
        check("failed mapping removes aliases before a new selection", True, mode="map-failure")
        check("symlinked original path remains a JNI alias", True, mode="loader", requested=alias / "libflutter.so")
    print(f"{tests} checks passed")


if __name__ == "__main__":
    main()
