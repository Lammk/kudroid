#!/usr/bin/env python3
"""Verify surface callback ownership through the real framework DEX on Linux."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def run(command, timeout=120):
    subprocess.run([str(part) for part in command], check=True, timeout=timeout)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=root / "build-host-tls")
    parser.add_argument("--framework-dex", type=Path, default=root / "framework/build/framework.dex")
    parser.add_argument("--skip-build", action="store_true", help="Use the existing host test binary")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if not args.skip_build:
        run(["cmake", "-S", root, "-B", build, "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release", "-DKUDROID_BUILD_HOST_TESTS=OFF"])
        run(["cmake", "--build", build, "--target", "test_surface_lifecycle", "-j1"], 240)
    with tempfile.TemporaryDirectory(prefix="kudroid-surface-lifecycle-") as temporary:
        directory = Path(temporary)
        classes = directory / "classes"
        classes.mkdir()
        run(["javac", "-source", "8", "-target", "8", "-nowarn", "-bootclasspath",
             root / "framework/build/classes", "-d", classes,
             root / "tests/java/android/app/SurfaceLifecycleTests.java"])
        d8 = root / "third_party/d8/r8.jar"
        if not d8.exists():
            d8 = root / "third_party/d8/d8.jar"
        run(["java", "-cp", d8, "com.android.tools.r8.D8", "--min-api", "29",
             "--lib", root / "framework/build/classes", "--output", directory,
             *sorted(classes.rglob("*.class"))])
        run([build / "test_surface_lifecycle", args.framework_dex.resolve(),
             directory / "classes.dex"], 30)


if __name__ == "__main__":
    main()
