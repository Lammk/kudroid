#!/usr/bin/env python3
"""Cross-build pinned ANGLE iOS libraries with the installed Darwin SDK."""

import argparse
import hashlib
import json
import os
import plistlib
from pathlib import Path
import re
import shutil
import shlex
import signal
import subprocess
import struct
import sys
import tempfile


def run(command, timeout):
    process = subprocess.Popen([str(value) for value in command], start_new_session=True)
    try:
        result = process.wait(timeout=timeout)
    except BaseException:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=5)
        raise
    if result:
        raise subprocess.CalledProcessError(result, command)


def apply_patch(repository, patch):
    command = ["git", "-C", repository, "apply", "--check", patch]
    forward = subprocess.run(command, capture_output=True, timeout=30)
    if forward.returncode == 0:
        run(["git", "-C", repository, "apply", patch], 30)
        return
    reverse = subprocess.run(["git", "-C", repository, "apply", "--reverse", "--check", patch],
                             capture_output=True, timeout=30)
    if reverse.returncode != 0:
        raise ValueError(f"Patch does not match the checkout: {patch}\n{forward.stderr.decode()}")


def prepare_source(source, patch_directory, backend, baseline):
    lock = json.loads((patch_directory.parent / "source-lock.json").read_text())
    for repository, expected in ((source, lock["angle_candidate"]),
                                 (source / "build", lock["chromium_build"])):
        revision = subprocess.check_output(["git", "-C", repository, "rev-parse", "HEAD"],
                                           text=True, timeout=10).strip()
        if revision != expected:
            raise ValueError(f"Expected revision {expected} in {repository}, got {revision}")
    for name in ("0000-explicit-ios-sdk-build.patch", "0000b-custom-clang-flags.patch"):
        apply_patch(source / "build", patch_directory / name)
    if backend == "vulkan":
        if baseline is None:
            raise ValueError("The Vulkan build requires --metal-baseline from a validated Metal build")
        manifest = json.loads((baseline / "manifest.json").read_text())
        arguments = (baseline / "args.gn").read_bytes()
        if manifest.get("source_revision") != lock["angle_candidate"] or \
                manifest.get("build_revision") != lock["chromium_build"] or \
                manifest.get("args_sha256") != hashlib.sha256(arguments).hexdigest() or \
                'angle_enable_vulkan = false' not in (baseline / "args.gn").read_text():
            raise ValueError("Metal baseline source/configuration does not match this candidate")
        for name in ("libEGL", "libGLESv2"):
            info = validate_binary(baseline / f"{name}.framework" / name, name)
            if info["sha256"] != manifest[name]["sha256"]:
                raise ValueError(f"Metal baseline {name} hash does not match")
        apply_patch(source, patch_directory / "0001-ios-vulkan-display.patch")
        apply_patch(source, patch_directory / "0002-vulkan-portability-subset.patch")
        apply_patch(source, patch_directory / "0003-shared-vulkan-loader-dependency.patch")


def prepare_toolchain(source, sdk, compiler, linker):
    version_text = subprocess.check_output([compiler, "--version"], text=True, timeout=10)
    match = re.search(r"clang version (\d+)", version_text)
    if not match:
        raise ValueError("Cannot determine Clang's resource directory version")
    version = match[1]
    installed = re.search(r"^InstalledDir: (.+)$", version_text, re.MULTILINE)
    compiler = Path(installed[1]) / "clang" if installed else compiler.absolute()
    resources = Path(subprocess.check_output([compiler, "-print-resource-dir"], text=True,
                                            timeout=10).strip())
    developer = sdk.parents[4]
    darwin = developer / f"Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/{version}/lib/darwin"
    if not (darwin / "libclang_rt.ios.a").is_file():
        raise ValueError(f"Missing matching Clang {version} Darwin runtime: {darwin}")
    builtins = Path(subprocess.check_output(
        [compiler, "--rtlib=compiler-rt", "--print-libgcc-file-name"], text=True, timeout=10).strip())
    if not builtins.is_file():
        raise ValueError(f"Missing host compiler runtime: {builtins}")
    root = source / f"out/linux-toolchain-clang{version}"
    resource_root = root / f"lib/clang/{version}"

    def link(target, destination):
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.is_symlink():
            if destination.resolve() == target.resolve():
                return
            destination.unlink()
        elif destination.exists():
            raise ValueError(f"Refusing to replace toolchain file: {destination}")
        destination.symlink_to(target)

    link(resources / "include", resource_root / "include")
    link(darwin, resource_root / "lib/darwin")
    link(builtins, resource_root / "lib/x86_64-unknown-linux-gnu/libclang_rt.builtins.a")
    for name in ("clang", "clang++"):
        executable = compiler.parent / name
        wrapper = root / "bin" / name
        wrapper.parent.mkdir(parents=True, exist_ok=True)
        wrapper.write_text(f"#!/bin/sh\nexec {shlex.quote(str(executable))} "
                           f"-resource-dir={shlex.quote(str(resource_root))} "
                           f"-B{shlex.quote(str(root / 'bin'))} \"$@\"\n")
        wrapper.chmod(0o755)
    if not linker.is_file():
        raise ValueError(f"Missing Darwin linker: {linker}")
    link(linker.resolve(), root / "bin/ld64.lld")
    for name in ("ld.lld", "llvm-ar", "llvm-strip", "llvm-install-name-tool",
                 "llvm-libtool-darwin", "llvm-nm", "llvm-objdump", "llvm-otool", "dsymutil"):
        executable = compiler.parent / name
        if not executable.exists():
            executable = Path(shutil.which(name) or "")
        if not executable.is_file():
            raise ValueError(f"Missing tool: {name}")
        link(executable, root / "bin" / name)
    link((root / "bin/llvm-libtool-darwin").resolve(), root / "bin/libtool")
    return root, version


def configure(source, output, sdk, compiler, linker, gn, backend, timeout):
    settings = plistlib.loads((sdk / "SDKSettings.plist").read_bytes())
    version = str(settings["Version"])
    system = sdk / "System/Library/CoreServices/SystemVersion.plist"
    sdk_build = plistlib.loads(system.read_bytes()).get("ProductBuildVersion", "") if system.exists() else ""
    if not (sdk / "System/Library/Frameworks/Metal.framework").is_dir():
        raise ValueError("The selected SDK does not contain Metal.framework")
    clang_root, clang_version = prepare_toolchain(source, sdk, compiler, linker)
    args = {
        "target_os": "ios",
        "target_cpu": "arm64",
        "target_environment": "device",
        "target_platform": "iphoneos",
        "ios_deployment_target": "15.0",
        "ios_sdk_path": str(sdk),
        "ios_sdk_name": "iphoneos",
        "ios_sdk_version": version,
        "ios_sdk_build": sdk_build,
        "ios_platform_build": sdk_build,
        "ios_sdk_platform": "iPhoneOS",
        "ios_sdk_platform_path": str(sdk.parent.parent),
        "ios_toolchains_path": str(sdk.parents[4] / "Toolchains/XcodeDefault.xctoolchain"),
        "ios_bin_path": str(clang_root / "bin") + "/",
        "use_system_xcode": True,
        "ios_enable_code_signing": False,
        "clang_base_path": str(clang_root),
        "clang_version": clang_version,
        "clang_use_chrome_plugins": False,
        "use_lld": True,
        "use_thin_lto": False,
        "use_custom_libcxx": False,
        "use_custom_libcxx_for_host": False,
        "use_sysroot": False,
        "use_remoteexec": False,
        "use_siso": False,
        "enable_rust": False,
        "enable_rust_cxx": False,
        "symbol_level": 0,
        "is_debug": False,
        "is_component_build": False,
        "treat_warnings_as_errors": False,
        "fatal_linker_warnings": False,
        "angle_enable_vulkan": backend == "vulkan",
        "angle_shared_libvulkan": True,
        "angle_use_custom_libvulkan": False,
        "angle_enable_metal": True,
        "angle_enable_gl": False,
        "angle_enable_null": False,
        "angle_enable_swiftshader": False,
        "angle_enable_wgpu": False,
        "angle_enable_cl": False,
        "angle_enable_perfetto": False,
        "angle_build_tests": False,
        "build_angle_deqp_tests": False,
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "args.gn").write_text("".join(f"{key} = {json.dumps(value)}\n" for key, value in args.items()))
    run([gn, "gen", output, "--root=" + str(source), "--script-executable=" + sys.executable], timeout)


def validate_binary(binary, name):
    data = binary.read_bytes()
    if len(data) < 32:
        raise ValueError(f"Truncated Mach-O binary: {binary}")
    magic, cpu, subtype, filetype, count, command_bytes, flags, reserved = struct.unpack_from("<8I", data)
    if magic != 0xFEEDFACF or cpu != 0x0100000C or filetype != 6:
        raise ValueError(f"Expected an arm64 Mach-O dylib: {binary}")
    end = 32 + command_bytes
    if end > len(data):
        raise ValueError(f"Truncated Mach-O load commands: {binary}")
    offset = 32
    version = None
    install_name = None
    dependencies = []
    for _ in range(count):
        if offset + 8 > end:
            raise ValueError(f"Missing Mach-O load command: {binary}")
        command, size = struct.unpack_from("<2I", data, offset)
        if size < 8 or offset + size > end:
            raise ValueError(f"Invalid Mach-O load command: {binary}")
        if command == 0x32 and size >= 24:
            platform, minimum, sdk_version = struct.unpack_from("<3I", data, offset + 8)
            if platform != 2:
                raise ValueError(f"Expected the iOS device platform: {binary}")
            version = (minimum, sdk_version)
        elif command == 0x25 and size >= 16:
            version = struct.unpack_from("<2I", data, offset + 8)
        elif command in (0xD, 0xC, 0x80000018, 0x8000001F):
            if size < 24:
                raise ValueError(f"Truncated dylib command: {binary}")
            string_offset = struct.unpack_from("<I", data, offset + 8)[0]
            if string_offset < 24 or string_offset >= size:
                raise ValueError(f"Invalid dylib name offset: {binary}")
            value = data[offset + string_offset:offset + size].split(b"\0", 1)[0].decode()
            if command == 0xD:
                install_name = value
            else:
                dependencies.append(value)
        offset += size
    if offset != end or version is None or version[0] != (15 << 16):
        raise ValueError(f"Expected an iOS 15.0 deployment target: {binary}")
    expected = f"@rpath/{name}.framework/{name}"
    if install_name != expected:
        raise ValueError(f"Expected install name {expected}, got {install_name}")
    return {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
            "minimum_os": "15.0", "sdk_encoded": version[1],
            "install_name": install_name, "dependencies": dependencies}


def source_patch(repository):
    patch = subprocess.check_output(["git", "-C", repository, "diff", "HEAD", "--binary"], timeout=30)
    untracked = subprocess.check_output(
        ["git", "-C", repository, "ls-files", "--others", "--exclude-standard", "-z",
         "--", "src", "include", "build_overrides", "config"], timeout=30)
    for path in filter(None, untracked.decode().split("\0")):
        result = subprocess.run(["git", "diff", "--no-index", "--binary", "--", "/dev/null", path],
                                cwd=repository, capture_output=True, timeout=30)
        if result.returncode not in (0, 1):
            raise ValueError(f"Cannot capture source file {path}: {result.stderr.decode()}")
        patch += result.stdout
    return patch


def stage(source, output, destination, timeout):
    binaries = {}
    metadata = {}
    for name in ("libEGL", "libGLESv2"):
        binary = output / "obj" / name
        if not binary.is_file():
            raise ValueError(f"Missing linked {name} binary: {binary}")
        binaries[name] = binary
        metadata[name] = validate_binary(binary, name)
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True, timeout=10).strip()
    if destination.exists():
        raise ValueError("Use a new staging directory; existing artifacts are preserved")
    patches = {"ANGLE_PATCH.diff": source_patch(source), "BUILD_PATCH.diff": source_patch(source / "build")}
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=destination.name + ".", dir=destination.parent))
    try:
        for name, binary in binaries.items():
            framework = temporary / f"{name}.framework"
            framework.mkdir()
            shutil.copy2(binary, framework / name)
            info = {"CFBundleIdentifier": f"com.google.angle.{name}", "CFBundleExecutable": name,
                    "CFBundleName": name, "CFBundlePackageType": "FMWK", "CFBundleVersion": "1",
                    "CFBundleShortVersionString": "1.0", "MinimumOSVersion": "15.0",
                    "CFBundleSupportedPlatforms": ["iPhoneOS"]}
            (framework / "Info.plist").write_bytes(plistlib.dumps(info))
            bundle = temporary / "xcframeworks" / f"{name}.xcframework"
            slice_directory = bundle / "ios-arm64"
            slice_directory.mkdir(parents=True)
            shutil.copytree(framework, slice_directory / framework.name)
            bundle_info = {"AvailableLibraries": [{"BinaryPath": f"{name}.framework/{name}",
                "LibraryIdentifier": "ios-arm64", "LibraryPath": framework.name,
                "SupportedArchitectures": ["arm64"], "SupportedPlatform": "ios"}],
                "CFBundlePackageType": "XFWK", "XCFrameworkFormatVersion": "1.0"}
            (bundle / "Info.plist").write_bytes(plistlib.dumps(bundle_info))
        (temporary / "ANGLE_REVISION").write_text(revision + "\n")
        shutil.copy2(output / "args.gn", temporary / "args.gn")
        shutil.copy2(source / "build/config/gclient_args.gni", temporary / "gclient_args.gni")
        for name, patch in patches.items():
            (temporary / name).write_bytes(patch)
        metadata["source_revision"] = revision
        metadata["build_revision"] = subprocess.check_output(
            ["git", "-C", source / "build", "rev-parse", "HEAD"], text=True, timeout=10).strip()
        metadata["patch_sha256"] = {name: hashlib.sha256(patch).hexdigest() for name, patch in patches.items()}
        metadata["args_sha256"] = hashlib.sha256((temporary / "args.gn").read_bytes()).hexdigest()
        metadata["gclient_args_sha256"] = hashlib.sha256((temporary / "gclient_args.gni").read_bytes()).hexdigest()
        (temporary / "manifest.json").write_text(json.dumps(metadata, indent=2) + "\n")
        os.rename(temporary, destination)
    except BaseException:
        shutil.rmtree(temporary)
        raise
    print(f"Validated candidate frameworks staged at {destination}")


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=root / "third_party/angle-src")
    parser.add_argument("--sdk", type=Path, default=Path.home() / ".swiftpm/swift-sdks/darwin.artifactbundle/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk")
    parser.add_argument("--compiler", type=Path, default=Path.home() / ".local/share/swiftly/bin/clang")
    parser.add_argument("--linker", type=Path)
    parser.add_argument("--gn", type=Path)
    parser.add_argument("--backend", choices=("metal", "vulkan"), default="metal")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument("--stage", type=Path)
    parser.add_argument("--metal-baseline", type=Path)
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=240)
    args = parser.parse_args()
    if args.jobs < 1 or args.timeout < 1:
        parser.error("jobs and timeout must be positive")
    source = args.source_dir.resolve()
    prepare_source(source, root / "scripts/angle/patches", args.backend, args.metal_baseline)
    output = args.build_dir or source / f"out/ios-arm64-linux-{args.backend}"
    gn = args.gn or source / "buildtools/linux64/gn"
    sdk = args.sdk.resolve()
    linker = args.linker or sdk.parents[5] / "toolset/bin/orig/ld64.lld"
    configure(source, output.resolve(), sdk, args.compiler, linker,
              gn.resolve(), args.backend, args.timeout)
    if not args.configure_only:
        run(["ninja", "-C", output, "-j", args.jobs, "obj/libEGL", "obj/libGLESv2"], args.timeout)
        if args.stage:
            stage(source, output, args.stage.resolve(), args.timeout)


if __name__ == "__main__":
    try:
        main()
    except subprocess.TimeoutExpired as error:
        print(f"Build slice timed out after {error.timeout}s; outputs remain incremental", file=sys.stderr)
        sys.exit(124)
    except (ValueError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
