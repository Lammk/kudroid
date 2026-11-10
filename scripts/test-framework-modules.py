#!/usr/bin/env python3
"""Exercise extracted framework helpers through the real DEX interpreter."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def run(command, seconds=120):
    subprocess.run([str(part) for part in command], check=True, timeout=seconds)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=root / "build-host-tls")
    parser.add_argument("--framework-dex", type=Path, default=root / "framework/build/framework.dex")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="kudroid-framework-modules-") as temporary:
        directory = Path(temporary)
        classes = directory / "classes"
        classes.mkdir()
        run(["javac", "-source", "8", "-target", "8", "-nowarn", "-bootclasspath",
             root / "framework/build/classes", "-d", classes,
             root / "tests/java/FrameworkModuleTests.java"])
        d8 = root / "third_party/d8/r8.jar"
        if not d8.exists():
            d8 = root / "third_party/d8/d8.jar"
        run(["java", "-cp", d8, "com.android.tools.r8.D8", "--min-api", "29",
             "--lib", root / "framework/build/classes", "--output", directory,
             *sorted(classes.rglob("*.class"))])
        run([args.build_dir / "test_framework_startup", "FrameworkModuleTests",
             args.framework_dex.resolve(), directory / "classes.dex"], 60)


if __name__ == "__main__":
    main()
