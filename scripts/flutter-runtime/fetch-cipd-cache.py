#!/usr/bin/env python3
"""Resume an exact CIPD package into the shared build cache."""

import argparse
import base64
import hashlib
import os
from pathlib import Path
import re
import urllib.request


def identity(path):
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").digest()
    return base64.urlsafe_b64encode(digest + b"\x02").decode().rstrip("=")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package")
    parser.add_argument("instance_id")
    parser.add_argument("--partial", type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]{44}", args.instance_id) or not re.fullmatch(r"[A-Za-z0-9_./-]+", args.package):
        raise ValueError("Invalid CIPD package or instance ID")
    cache = Path(os.environ.get("KUDROID_FLUTTER_CACHE", Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "kudroid/flutter-runtime")).resolve(strict=True)
    directory = cache / "cipd-cache/instances"
    directory.mkdir(parents=True, exist_ok=True)
    destination = directory / args.instance_id
    partial = destination.with_suffix(".part")
    if destination.exists():
        if identity(destination) != args.instance_id:
            raise ValueError("Cached package identity differs")
        print("Pinned CIPD package already cached:", destination)
        return
    url = "https://chrome-infra-packages.appspot.com/dl/" + args.package + "/+/" + args.instance_id
    if args.partial:
        if partial.exists() or not args.partial.is_file():
            raise ValueError("Cannot adopt requested partial")
        with urllib.request.urlopen(urllib.request.Request(url, headers={"Range": "bytes=0-1023"}), timeout=30) as response:
            prefix = response.read(1024)
        with args.partial.open("rb") as stream:
            if stream.read(1024) != prefix:
                raise ValueError("Partial belongs to a different package")
        args.partial.rename(partial)
    offset = partial.stat().st_size if partial.exists() else 0
    if offset and identity(partial) == args.instance_id:
        partial.rename(destination)
        print("Pinned CIPD package cached:", destination)
        return
    print("Resuming", args.package, "from", offset, "bytes", flush=True)
    request = urllib.request.Request(url, headers={"Range": f"bytes={offset}-"} if offset else {})
    with urllib.request.urlopen(request, timeout=30) as response:
        if offset and response.status != 206:
            raise ValueError("Server did not honor the requested resume range")
        if offset and not response.headers.get("Content-Range", "").startswith(f"bytes {offset}-"):
            raise ValueError("Incorrect response range")
        received = offset
        next_update = received + 64 * 1024 * 1024
        with partial.open("ab" if offset else "wb") as stream:
            while block := response.read(1024 * 1024):
                stream.write(block)
                received += len(block)
                if received >= next_update:
                    print("Downloaded", received, "bytes", flush=True)
                    next_update = received + 64 * 1024 * 1024
    if identity(partial) != args.instance_id:
        raise ValueError("Downloaded package identity differs")
    partial.rename(destination)
    print("Pinned CIPD package cached:", destination)


if __name__ == "__main__":
    main()
