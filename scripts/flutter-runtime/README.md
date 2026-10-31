# Constrained Flutter runtime candidate

This prototype enables Dart's existing smaller compressed-heap reservation path
for Android ARM64. Each reservation remains a real host mapping. The Dart target,
pointer representation, and snapshot format stay unchanged.

The candidate is pinned to Flutter 3.32.5, source revision
`fcf2c11572af6f390246c056bc905eca609533a0`, Dart revision
`05589740efb305ceef593b3db6cab2910c17d480`, and snapshot hash
`830f4f59e7969c70b595182826435c19`. Other engine families require separate builds
and validation.

Build with bounded commands:

```sh
timeout 3000 scripts/flutter-runtime/build.sh --prepare
timeout 400 scripts/flutter-runtime/build.sh --build
```

Dependencies and build outputs live in
`${KUDROID_FLUTTER_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/kudroid/flutter-runtime}`.
Compilation defaults to one job and disables full link-time optimization to bound
link memory usage. Release/product mode and snapshot features are preserved.
Each Ninja invocation has a 240-second timeout;
rerunning `--build` resumes from completed objects. A timeout means compilation
is incomplete, and does not indicate a usable candidate.

Preflight the completed candidate against the original engine and an AOT library:

```sh
timeout 120 python3 scripts/flutter-runtime/verify.py \
  --reference /path/to/original/libflutter.so \
  --snapshot /path/to/libapp.so \
  --candidate /path/to/rebuilt/libflutter.so
```

The check uses ELF symbols and snapshot headers to inspect the architecture,
baseline build ID, snapshot version/features, exports, imports, dependencies, JNI
signature strings, and reservation diagnostic. It requires Python 3 and
`readelf`. Static preflight does not prove the candidate's runtime feature string
or JNI registration works; the VM checks snapshot compatibility during startup.

The reviewed extra imports `vprintf` and `__vsprintf_chk` use the existing guest
vararg shims. `exit` uses the host C library's process-termination semantics.
These imports come from logging and error-handling code retained without LTO.
New imports require an ABI review before being added to the reviewed list.

The pinned stripped artifact and its source manifest live in
`ios-app/KuDroidShell/Resources/runtime/`. Both CMake/Xcode and xtool package it
at `runtime/libflutter.so` in the main app bundle. The loader preserves the APK's
original library identity while mapping this artifact. Selection requires the
original engine build ID, both AOT snapshot versions/features, and the exact
candidate build ID and SHA-256. Unknown families retain their original engine.
Changing an already resident engine requires restarting the host process.

Run the focused compatibility and identity checks:

```sh
timeout 240 python3 scripts/flutter-runtime/test-selector.py \
  --reference /path/to/original/libflutter.so \
  --snapshot /path/to/libapp.so \
  --candidate ios-app/KuDroidShell/Resources/runtime/libflutter.so
timeout 120 python3 scripts/flutter-runtime/verify-asset.py /path/to/KuDroidShell.ipa
```

The host checks require a C++17 compiler and OpenSSL. Host development can select
an explicit candidate with `KUDROID_FLUTTER_RUNTIME`; iOS uses only the bundled
artifact. The loader tests model mapping and do not execute ARM64 guest code.

Before deployment, fully close the host process. The bundled replacement leaves
the APK's extracted engine unchanged.
On the device, record the diagnostic's actual heap base/capacity, verify snapshot
loading and real page commits, and check startup and rendering under the existing
free-team profile. Runtime failures require a fresh process with the original
selected. Device startup and rendering remain required for production acceptance.
