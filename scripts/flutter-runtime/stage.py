#!/usr/bin/env python3
"""Strip, preflight, and stage a pinned catalog candidate."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--candidate", type=Path)
    args = parser.parse_args()
    scripts = Path(__file__).resolve().parent
    pins = json.loads((scripts / "versions.json").read_text())
    pin = next(p for p in pins if p["flutter_tag"] == args.version)
    if args.version == "3.32.5":
        raise ValueError("Keep the existing device-verified artifact unchanged")
    cache = Path(os.environ.get("KUDROID_FLUTTER_CACHE", Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "kudroid/flutter-runtime")).resolve(strict=True)
    source = cache / "versions" / args.version / "src/flutter"
    candidate = args.candidate or source.parent / "out/android_release_arm64/libflutter.so"
    fixture = cache / "validation" / args.version
    bundle = scripts.parents[1] / "ios-app/KuDroidShell/Resources/runtime"
    (cache / "tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="stage-", dir=cache / "tmp") as temporary:
        directory = Path(temporary)
        stripped = directory / "libflutter.so"
        report_path = directory / "validation.json"
        subprocess.run(["aarch64-linux-gnu-strip", "--strip-unneeded", str(candidate), "-o", str(stripped)], check=True, timeout=120)
        subprocess.run(["python3", str(scripts / "verify.py"), "--version", args.version,
                        "--reference", str(fixture / "libflutter.so"), "--snapshot", str(fixture / "libapp.so"),
                        "--candidate", str(stripped), "--output", str(report_path)], check=True, timeout=120)
        report = json.loads(report_path.read_text())
        assert report["preflight_passed"] is True
        destination = bundle / args.version
        destination.mkdir(exist_ok=True)
        shutil.copy2(stripped, destination / "libflutter.so")
        shutil.copy2(report_path, destination / "validation.json")
        shutil.copy2(source / "LICENSE", destination / "FLUTTER_LICENSE.txt")
        shutil.copy2(cache / "versions" / args.version / pin["dart_path"] / "LICENSE", destination / "DART_LICENSE.txt")
        notices = fixture / "android-arm64-release-artifacts/LICENSE.flutter_jar_zip.md"
        if not notices.is_file():
            # Older artifacts retain these notices in the pinned source tree.
            notices = source / "sky/packages/sky_engine/LICENSE"
        shutil.copy2(notices, destination / "THIRD_PARTY_LICENSES.md")
        fields = ("flutter_version", "flutter_revision", "engine_revision", "dart_revision", "dart_version",
                  "original_build_id", "reference_sha256", "candidate_build_id", "candidate_sha256",
                  "vm_snapshot_hash", "vm_snapshot_features", "isolate_snapshot_hash", "isolate_snapshot_features")
        record = {key: report[key] for key in fields}
        record.update(path=args.version + "/libflutter.so", target="android-arm64-release",
                      patch="scripts/flutter-runtime/" + pin["patch"], static_verified=True, device_verified=False)
        manifest_path = bundle / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["engines"] = [r for r in manifest["engines"] if r["flutter_version"] != args.version]
        manifest["engines"].append(record)
        pending = manifest_path.with_suffix(".json.tmp")
        pending.write_text(json.dumps(manifest, indent=2) + "\n")
        pending.replace(manifest_path)
    subprocess.run(["python3", str(scripts / "verify-asset.py"), "--asset", str(bundle)], check=True, timeout=120)
    print("Staged statically verified Flutter", args.version)


if __name__ == "__main__":
    main()
