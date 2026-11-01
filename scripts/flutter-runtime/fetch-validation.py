#!/usr/bin/env python3
"""Fetch pinned official engine tools and build a minimal Android AOT fixture."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import urllib.request
import zipfile

from verify import inspect


def download(url, path):
    if path.exists():
        with zipfile.ZipFile(path) as archive:
            if archive.testzip() is not None:
                raise ValueError("Corrupt cached archive: " + str(path))
        return
    partial = path.with_suffix(path.suffix + ".part")
    offset = partial.stat().st_size if partial.exists() else 0
    request = urllib.request.Request(url, headers={"Range": f"bytes={offset}-"} if offset else {})
    with urllib.request.urlopen(request, timeout=30) as response:
        mode = "ab" if offset and response.status == 206 else "wb"
        with partial.open(mode) as stream:
            shutil.copyfileobj(response, stream, length=1024 * 1024)
    with zipfile.ZipFile(partial) as archive:
        if archive.testzip() is not None:
            raise ValueError("Corrupt downloaded archive: " + str(partial))
    partial.replace(path)


def extract(path, destination):
    marker = destination / ".complete"
    if marker.exists():
        return
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path) as archive:
        for member in archive.infolist():
            relative = PurePosixPath(member.filename)
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError("Unsafe archive member")
            target = Path(archive.extract(member, destination))
            permissions = (member.external_attr >> 16) & 0o777
            if permissions:
                target.chmod(permissions)
    marker.write_text(hashlib.sha256(path.read_bytes()).hexdigest() + "\n")


def run(command, cwd):
    subprocess.run(command, cwd=cwd, check=True, timeout=120)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--fetch-only", action="store_true")
    args = parser.parse_args()
    records = json.loads((Path(__file__).parent / "versions.json").read_text())
    pin = next(r for r in records if r["flutter_tag"] == args.version)
    cache = Path(os.environ.get("KUDROID_FLUTTER_CACHE", Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "kudroid/flutter-runtime"))
    directory = cache.resolve(strict=True) / "validation" / args.version
    directory.mkdir(parents=True, exist_ok=True)
    base = "https://storage.googleapis.com/flutter_infra_release/flutter/" + pin["engine_revision"] + "/"
    items = ("android-arm64-release/artifacts.zip", "android-arm64-release/linux-x64.zip",
             "linux-x64/artifacts.zip", "flutter_patched_sdk_product.zip", "dart-sdk-linux-x64.zip")
    provenance = {}
    for item in items:
        name = item.replace("/", "-")
        archive = directory / name
        print("Fetching", args.version, item, flush=True)
        download(base + item, archive)
        extract(archive, directory / name.removesuffix(".zip"))
        provenance[item] = {"url": base + item, "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}
    reference = directory / "libflutter.so"
    with zipfile.ZipFile(directory / "android-arm64-release-artifacts/flutter.jar") as archive:
        reference.write_bytes(archive.read("lib/arm64-v8a/libflutter.so"))
    identity = inspect(reference)
    if identity["build_id"] != pin["reference_build_id"] or identity["sha256"] != pin["reference_sha256"]:
        raise ValueError("Official engine artifact differs from the pinned release")
    (directory / "reference.json").write_text(json.dumps({
        **pin, "reference_build_id": identity["build_id"], "reference_sha256": identity["sha256"],
        "downloads": provenance,
    }, indent=2) + "\n")
    if args.fetch_only:
        return
    sdk = directory / "dart-sdk-linux-x64/dart-sdk"
    frontend = directory / "linux-x64-artifacts/frontend_server_aot.dart.snapshot"
    runtime = sdk / "bin/dartaotruntime"
    if not frontend.exists():
        frontend = directory / "linux-x64-artifacts/frontend_server.dart.snapshot"
        runtime = sdk / "bin/dart"
    fixture = directory / "main.dart"
    fixture.write_text("""import 'dart:ui';

@pragma('vm:entry-point')
void main() {
  window.onDrawFrame = () {
    final recorder = PictureRecorder();
    final canvas = Canvas(recorder);
    canvas.drawRect(Offset.zero & window.physicalSize,
        Paint()..color = const Color(0xff2458a6));
    final picture = recorder.endRecording();
    final scene = (SceneBuilder()..addPicture(Offset.zero, picture)).build();
    window.render(scene);
    scene.dispose();
    picture.dispose();
  };
  window.scheduleFrame();
}
""")
    packages = directory / "package_config.json"
    packages.write_text('{"configVersion":2,"packages":[]}\n')
    platform = directory / "flutter_patched_sdk_product/flutter_patched_sdk_product"
    dill = directory / "app.dill"
    run([str(runtime), str(frontend), "--sdk-root=" + str(platform) + "/", "--aot", "--tfa",
         "--target=flutter", "--target-os=android", "--packages=" + str(packages),
         "--output-dill=" + str(dill), str(fixture)], directory)
    generator = directory / "android-arm64-release-linux-x64/gen_snapshot"
    if not generator.exists():
        generator = directory / "android-arm64-release-linux-x64/linux-x64/gen_snapshot"
    run([str(generator), "--deterministic", "--snapshot_kind=app-aot-elf",
         "--elf=" + str(directory / "libapp.so"), "--strip", str(dill)], directory)
    print("Official reference and AOT fixture ready:", directory)


if __name__ == "__main__":
    main()
