#!/usr/bin/env python3
"""Run framework startup regressions through the real DEX and JNI."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import struct
import xml.etree.ElementTree as ET


def run(command, timeout=120, env=None):
    subprocess.run([str(part) for part in command], check=True, timeout=timeout, env=env)


def manifest_fixture(source):
    root = ET.parse(source).getroot()
    strings = []
    def index(value):
        value = value.split("}")[-1]
        if value not in strings:
            strings.append(value)
        return strings.index(value)
    for element in root.iter():
        index(element.tag)
        for key, value in element.attrib.items():
            index(key)
            index(value)
    encoded, offsets = bytearray(), []
    for value in strings:
        raw = value.encode("utf-8")
        assert len(raw) < 128 and len(value) < 128
        offsets.append(len(encoded))
        encoded.extend(bytes([len(value), len(raw)]) + raw + b"\0")
    encoded.extend(b"\0" * (-len(encoded) % 4))
    pool_size = 28 + 4 * len(strings) + len(encoded)
    pool = struct.pack("<HH6I", 1, 28, pool_size, len(strings), 0, 0x100,
                       28 + 4 * len(strings), 0)
    pool += struct.pack("<" + "I" * len(offsets), *offsets) + encoded
    chunks = bytearray(pool)
    def node(element):
        count = len(element.attrib)
        chunks.extend(struct.pack("<HHIII", 0x102, 16, 36 + 20 * count, 1, 0xffffffff))
        chunks.extend(struct.pack("<II6H", 0xffffffff, index(element.tag), 20, 20, count, 0, 0, 0))
        for key, value in element.attrib.items():
            raw, kind, typed = index(value), 3, index(value)
            if key.split("}")[-1] in ("enabled", "exported"):
                raw, kind, typed = 0xffffffff, 0x12, 0xffffffff if value == "true" else 0
            elif key.split("}")[-1] == "priority":
                raw, kind, typed = 0xffffffff, 0x10, int(value) & 0xffffffff
            chunks.extend(struct.pack("<IIIHBBI", 0xffffffff, index(key), raw,
                                      8, 0, kind, typed))
        for child in element:
            node(child)
        chunks.extend(struct.pack("<HHIIIII", 0x103, 16, 24, 1, 0xffffffff,
                                  0xffffffff, index(element.tag)))
    node(root)
    return struct.pack("<HHI", 3, 8, 8 + len(chunks)) + chunks


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=root / "build-host-tls")
    parser.add_argument("--framework-dex", type=Path, default=root / "framework/build/framework.dex")
    parser.add_argument("--test", action="append")
    parser.add_argument("--skip-build", action="store_true", help="Use the existing host test binaries")
    parser.add_argument("--openssl-config", type=Path,
                        help="OpenSSL configuration for regression subprocesses only")
    args = parser.parse_args()
    environment = os.environ.copy()
    if args.openssl_config:
        environment["OPENSSL_CONF"] = str(args.openssl_config.resolve())
    build = args.build_dir.resolve()
    if not args.skip_build:
        run(["cmake", "-S", root, "-B", build, "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release", "-DKUDROID_BUILD_HOST_TESTS=OFF"])
        run(["cmake", "--build", build, "--target", "test_framework_startup", "test_manifest_services", "-j1"], 240)
    with tempfile.TemporaryDirectory(prefix="kudroid-framework-startup-") as temporary:
        directory = Path(temporary)
        fixture = root / "tests/fixtures/service_manifest.xml"
        binary = directory / "manifest.axml"
        binary.write_bytes(manifest_fixture(fixture))
        if not args.test:
            run([build / "test_manifest_services", binary, fixture], 30)
        classes = directory / "classes"
        classes.mkdir()
        sources = [root / "kuart-tests/java" / name for name in
                   ("RsaTestVectors.java", "SecurityStartupTests.java", "SymmetricCryptoTests.java",
                    "LoadingFrameworkTests.java", "StartupFrameworkTests.java", "OrderedCollectionTests.java")]
        service = root / "kuart-tests/java/android/app/ServiceBindingTests.java"
        if service.exists():
            sources.append(service)
        run(["javac", "-source", "8", "-target", "8", "-nowarn", "-bootclasspath",
             root / "framework/build/classes", "-d", classes, *sources])
        d8 = root / "third_party/d8/r8.jar"
        if not d8.exists():
            d8 = root / "third_party/d8/d8.jar"
        run(["java", "-cp", d8, "com.android.tools.r8.D8", "--min-api", "29",
             "--lib", root / "framework/build/classes", "--output", directory,
             *sorted(classes.rglob("*.class"))])
        for test in args.test or ["SecurityStartupTests", "SymmetricCryptoTests",
                                  "LoadingFrameworkTests", "StartupFrameworkTests",
                                  "OrderedCollectionTests", "android/app/ServiceBindingTests"]:
            run([build / "test_framework_startup", test, args.framework_dex.resolve(),
                 directory / "classes.dex"], 60, env=environment)


if __name__ == "__main__":
    main()
