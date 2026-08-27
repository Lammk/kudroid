# AOSP ART libdexfile (Modified for KuDroid / KuART)

This directory contains source code derived from the Android Open Source Project (AOSP) ART runtime, licensed under the **Apache License, Version 2.0**.

## Upstream Source
- **Project**: Android Open Source Project (AOSP) ART
- **Tag**: `android-10.0.0_r47` (Android 10)
- **License**: Apache-2.0 (see `NOTICE` file in this directory)

## Modifications for KuDroid
The original AOSP source has been adapted and pruned for standalone usage in the KuDroid project:
1. **Standalone Extraction**: Extracted `libdexfile` and essential `libartbase/base` headers without dependencies on the rest of the AOSP runtime, compiler, or `libcore`.
2. **Logging Abstraction**: Replaced `android-base/logging` with standard C++ stream / POSIX logging to allow clean compilation on Linux, macOS, and iOS (Darwin clang).
3. **Memory Mapping**: Pruned `mem_map` implementation in favor of standard POSIX `mmap` integrated with KuDroid's platform memory layer.
4. **Stripped Unneeded Modules**: Removed verification (`dex_file_verifier`), compilation tools (`dex2oat`), and profiling utilities.
