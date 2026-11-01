# Constrained Flutter runtime catalog

The catalog substitutes independently built Android ARM64 Flutter engines that enable Dart's existing smaller compressed-heap reservation path. The Dart target, pointer representation, snapshot format, and real host mapping semantics stay unchanged.

Exact source pins, checkout layouts and GN options for 3.32.5, 3.24.5, 3.22.2, 3.16.9 and 3.10.6 live in `versions.json`. The existing 3.32.5 artifact must keep SHA-256 `df2715a416aac8dbcd63d81bc2171246af378e8dd9a92d2ece2f0b4489f6e3f1`. Other releases require their own build and static preflight before entering the bundled manifest.

## Build and stage

The cache is `${KUDROID_FLUTTER_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/kudroid/flutter-runtime}`. On this workspace it links to `/run/media/kuzei/Stuff/Kudroid/linux/flutter-runtime`, inside the HDD ext4 volume. Mount it with `/run/media/kuzei/Stuff/Kudroid/mount-builds.sh` after reconnecting or rebooting. Keep large checkouts, caches and outputs on that volume.

```sh
timeout 1650 bash scripts/flutter-runtime/build.sh --prepare --version 3.24.5
timeout 600 python3 scripts/flutter-runtime/fetch-validation.py --version 3.24.5
timeout 400 bash scripts/flutter-runtime/build.sh --build --version 3.24.5
timeout 300 python3 scripts/flutter-runtime/stage.py --version 3.24.5
```

Dependency preparation is bounded at 900 seconds for sync and 600 seconds for hooks. Ninja batches are bounded at 240 seconds; rerunning `--build` resumes completed objects. A timeout means the engine is incomplete. Compilation defaults to one job; `KUDROID_FLUTTER_JOBS` accepts a positive integer. Full link-time optimization is disabled to bound link memory while preserving release/product mode. `--matrix` processes the four additional releases and then 3.32.5; `--describe --version TAG` prints its source pin without building.

`fetch-validation.py` obtains each official ARM64 engine and matching Dart SDK/frontend/AOT generator from the exact engine revision. It records artifact URLs, SHA-256 and original engine identity, and creates a minimal `dart:ui` release AOT fixture. Each version has its own validation directory on the HDD.

`stage.py` strips a completed candidate, runs `verify.py` against that release's official reference and fixture, and adds successful candidates with both Flutter/Dart licenses and a preflight report. Unreviewed import additions, missing exports/dependencies/JNI descriptors, wrong target/version/hash, or a missing reservation diagnostic fail preflight. The existing reviewed vararg imports use their guest shims; process exit follows the existing host semantics. Any additional ABI difference requires review before acceptance.

## Automatic selection

The main app bundle contains `runtime/manifest.json` and version directories. Both CMake and xtool include nested assets. Each manifest record retains the original engine build ID, both VM/isolate snapshot hashes and exact feature strings, candidate build ID and SHA-256, exact source pins, relative path, and static/device verification status. Arrays allow distinct original or feature variants sharing snapshot hashes.

When `libflutter.so` loads, the resolver reads the catalog, validates the original engine and companion AOT snapshots, then checks the matching candidate's bytes before mapping it. Unsafe paths, absent/tampered artifacts and ambiguous matches reject substitution. Invalid or unverified records cannot select. Unknown families retain their original APK engine, which may still encounter the original address-space constraint. The library manager preserves original APK/JNI identities and aliases; changing an already resident substituted engine requires restarting the host.

Host tests may configure `KUDROID_FLUTTER_RUNTIME_DIRECTORY`; iOS reads only its bundled runtime directory.

```sh
timeout 240 python3 scripts/flutter-runtime/test-selector.py --version 3.24.5 \
  --reference /path/to/validation/3.24.5/libflutter.so \
  --snapshot /path/to/validation/3.24.5/libapp.so \
  --candidate ios-app/KuDroidShell/Resources/runtime/3.24.5/libflutter.so
timeout 240 env CXX=clang++ python3 scripts/flutter-runtime/test-selector.py --sanitize \
  --reference /path/to/validation/libflutter.so \
  --snapshot /path/to/validation/libapp.so \
  --candidate ios-app/KuDroidShell/Resources/runtime/3.32.5/libflutter.so
timeout 120 python3 scripts/flutter-runtime/verify-asset.py --asset --require-all \
  ios-app/KuDroidShell/Resources/runtime
timeout 120 python3 scripts/flutter-runtime/verify-asset.py --require-all /path/to/KuDroidShell.ipa
```

Host tests use C++17 and OpenSSL and model ARM64 mapping without executing guest code. Static verification is separate from observed device startup/rendering. New records remain `device_verified: false` until tested in a fresh host process with JIT enabled under the free-team profile.
