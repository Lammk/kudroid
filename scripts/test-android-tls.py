#!/usr/bin/env python3
"""Run the Android TLS API regression through the framework DEX and local TLS."""

import argparse
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def run(command, timeout=120):
    subprocess.run([str(part) for part in command], check=True, timeout=timeout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build-host-tls"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    run(["cmake", "-S", root, "-B", build, "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", "-DKUDROID_BUILD_HOST_TESTS=OFF"])
    run(["cmake", "--build", build, "--target", "test_kuart_android_tls", "-j6"], 240)
    with tempfile.TemporaryDirectory(prefix="kudroid-android-tls-") as temporary:
        directory = Path(temporary)
        classes = directory / "classes"
        classes.mkdir()
        run(["javac", "-source", "8", "-target", "8", "-nowarn", "-bootclasspath",
             root / "framework/build/classes", "-d", classes,
             root / "kuart-tests/java/AndroidTlsTests.java"])
        d8 = root / "third_party/d8/r8.jar"
        if not d8.exists():
            d8 = root / "third_party/d8/d8.jar"
        run(["java", "-cp", d8, "com.android.tools.r8.D8", "--min-api", "29",
             "--lib", root / "framework/build/classes", "--output", directory,
             *sorted(classes.rglob("*.class"))])
        cert = directory / "cert.pem"
        key = directory / "key.pem"
        run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
             "-subj", "/CN=localhost", "-keyout", key, "-out", cert])
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        output = directory / "server.log"
        with output.open("w") as server_log:
            server = subprocess.Popen(["openssl", "s_server", "-accept", f"127.0.0.1:{port}",
                                       "-cert", str(cert), "-key", str(key), "-tls1_2",
                                       "-alpn", "http/1.1", "-www", "-tlsextdebug"],
                                      stdout=server_log, stderr=server_log)
            try:
                deadline = time.monotonic() + 5
                while True:
                    if server.poll() is not None:
                        raise RuntimeError(output.read_text())
                    try:
                        with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                            break
                    except OSError:
                        if time.monotonic() >= deadline:
                            raise RuntimeError("local TLS server did not start")
                        time.sleep(0.05)
                run([build / "test_kuart_android_tls", root / "framework/build/framework.dex",
                     directory / "classes.dex", port], 30)
            finally:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=5)
        trace = output.read_text()
        assert trace.count('TLS client extension "session ticket" (id=35)') == 1, trace
        print("PASS: only the enabled connection advertises TLS session tickets")


if __name__ == "__main__":
    main()
