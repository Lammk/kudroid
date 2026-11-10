# Source modules

Owned source files have a 2,000-line limit. Run `timeout 120 python3 scripts/check-source-size.py` to check tracked and new sources. Dependency trees and generated build outputs are excluded.

| Entry point | Implementation modules |
| --- | --- |
| `src/abi/SyscallShim.cpp` | `src/abi/syscall/`: logging, pthreads, file metadata, memory, direct syscalls, descriptors, sockets, signals, loader, events, TLS, properties, libc, semaphores, fortify and symbol registration |
| `src/platform/GraphicsShim.cpp` | `src/platform/graphics/`: windows/layers, procedure lookup, EGL display/surfaces, Vulkan, GLES forwarding, stack ABI and symbol registration |
| `src/kuart/LibCore.cpp` | `src/kuart/libcore/`: strings/objects, reflection, Java language operations, files/SQL, Android graphics/services, memory/resources |
| `src/kuart/Interpreter.cpp` | `src/kuart/interpreter/`: resolution/invocation and bytecode execution |
| `src/bridge/CrashHandling.cpp` | `src/bridge/crash/`: fault context, recovery and signal reports |
| `src/VFSPathRemapper.cpp` | `src/vfs/`: root/identity, pseudo files, archive index, path mapping, stream tracking/IO and file operations |
| `src/APKExtractor.cpp` | `src/apk/`: archives, binary manifest, extraction, text manifest/package previews |
| `src/platform/FmodVorbisFallback.cpp` | `src/platform/fmod/`: contiguous setup payload and lookup entries |
| `src/platform/FrameworkResourcesData.inc` | `src/platform/framework_resources/`: scalar, string, style, name and text tables |
| `include/jni.h` | `include/jni/Environment.inc`: the standard C++ environment facade |
| `framework/android/view/View.java` | `ViewAttributes`, `ViewDrawing`, `ViewGeometry`, `ViewInteraction` |
| `framework/android/animation/AnimatorSet.java` | `AnimatorSetNode` |
| `framework/java/math/BigDecimal.java` | `BigDecimalRounding`, `BigDecimalConversions` |
| `framework/java/util/regex/HashDecompositions.java` | `HashDecompositionData`, `HashDecompositionSupplement` |
| `framework/com/android/internal/R.java` | `RStyleableArrays` |
| `ios-app/KuDroidShell/ContentView.swift` | `AppsView`, `PermissionsView`, `HostLifecycle`, `RuntimeTools`, `NativeRunner` |

Native `.inc` implementation modules are compiled through their entry point. They share its lexical namespaces and private state; include order preserves declaration visibility, initialization order, symbol registration and signal-handler storage. They are not standalone translation units. Edit the module responsible for the operation, and keep each function and preprocessing conditional together. CMake compiler dependencies track module changes automatically.

Java API methods and instance fields remain on their original declaring classes. View helpers receive the original instance and call its methods so subclass dispatch remains intact. Package access permits helpers to use the existing state without creating duplicate storage. Private method declarations remain private; internal bridges retain synchronized and native call boundaries. Resource fields remain on `R.attr` and `R.styleable`; helper methods create their array payloads in the original initialization order.

Regenerate resource modules with `scripts/gen_internal_r.py` and `scripts/gen_framework_resources.py`. `tools/gen_fmod_vorbis_table.py` writes payload and entry modules to `src/platform/fmod`, or to a directory passed as its second argument. Do not edit generated payloads directly.

`scripts/source_modules.py` expands local modules for source-extraction regressions. Run `timeout 120 python3 scripts/test-framework-modules.py` after building `test_framework_startup` to exercise the Java helper paths through the real DEX interpreter.
