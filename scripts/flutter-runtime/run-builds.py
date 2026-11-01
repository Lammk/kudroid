#!/usr/bin/env python3
"""Compile and stage prepared catalog engines in bounded resumable batches."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", action="append")
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--max-batches", type=int, default=48)
    args = parser.parse_args()
    if args.jobs < 1 or args.max_batches < 1:
        raise ValueError("Jobs and batch limit must be positive")
    versions = args.version or ["3.24.5", "3.22.2", "3.16.9", "3.10.6"]
    scripts = Path(__file__).resolve().parent
    pins = {r["flutter_tag"]: r for r in json.loads((scripts / "versions.json").read_text())}
    if any(v not in pins or v == "3.32.5" for v in versions):
        raise ValueError("Choose additional catalog releases; preserve the existing 3.32.5 artifact")
    cache = Path(os.environ.get("KUDROID_FLUTTER_CACHE", Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "kudroid/flutter-runtime")).resolve(strict=True)
    runners = cache / "runner-scripts"
    runners.mkdir(exist_ok=True)
    snapshot = Path(tempfile.mkdtemp(prefix="builds-", dir=runners))
    for name in ("build.sh", "versions.json", "dart-constrained-reservations.patch"):
        shutil.copy2(scripts / name, snapshot / name)
    shutil.copytree(scripts / "patches", snapshot / "patches")
    logs = cache / "build-logs"
    logs.mkdir(exist_ok=True)
    environment = {**os.environ, "KUDROID_FLUTTER_JOBS": str(args.jobs)}
    for version in versions:
        ninja_log = cache / "versions" / version / "src/out/android_release_arm64/.ninja_log"
        stalled = 0
        for batch in range(1, args.max_batches + 1):
            before = ninja_log.stat().st_size if ninja_log.exists() else 0
            log = logs / f"{version}-batch-{batch}.log"
            print(f"Building {version}, batch {batch}: {log}", flush=True)
            with log.open("w") as stream:
                result = subprocess.run(["timeout", "-k", "5", "400", "bash", str(snapshot / "build.sh"),
                                         "--build", "--version", version], env=environment,
                                        stdout=stream, stderr=subprocess.STDOUT, timeout=410)
            after = ninja_log.stat().st_size if ninja_log.exists() else 0
            if result.returncode == 0:
                print("Compilation complete:", version, flush=True)
                break
            if result.returncode != 124:
                raise RuntimeError(f"Build failed ({result.returncode}); inspect {log}")
            stalled = stalled + 1 if after <= before else 0
            if stalled >= 3:
                raise RuntimeError(f"Three batches made no recorded object progress; inspect {log}")
        else:
            raise RuntimeError("Build batch limit reached for " + version)
        subprocess.run(["python3", str(scripts / "stage.py"), "--version", version], check=True, timeout=300)
    print("Requested engine builds and static staging completed", flush=True)


if __name__ == "__main__":
    main()
