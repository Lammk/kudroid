# KuDroid - Project Context for AI Agents

**Target Audience:** Any AI Agent joining this workspace (e.g., GLM 5.1, Claude, etc.) to assist with KuDroid development.

## 1. Project Core Philosophy
**KuDroid is NOT an emulator.**
It is a **native compatibility layer** (similar in philosophy to Wine or Rosetta) designed to run Android ARM64 native binaries (`.so` libraries) directly on iOS ARM64. Since both Android and iOS share the ARM64 architecture, there is no need for CPU emulation. 

Instead, KuDroid focuses on:
- Translating Android Bionic libc calls and Linux syscalls to iOS/Darwin equivalents.
- Simulating a Java Native Interface (JNI) environment via `miniJVM`.
- Bridging Android Graphics (EGL/Vulkan/OpenGL ES) to Apple Metal (via ANGLE / MoltenVK).
- Hooking the dynamic loader (`dlopen`, `dlsym`) to intercept and reroute system calls.

## 2. Current Architecture & Modules

The codebase is mainly written in C++ and Swift, bridging the iOS host app with the Android `.so` libraries.

### Core Shims (`src/shims/`)
We have recently refactored the monolithic `BionicShim.cpp` into modular components:
- **`SyscallShim.cpp`**: Handles low-level Linux/Bionic operations, POSIX threads (pthread), memory allocation, IO, and Thread Local Storage (TLS, dealing with `tpidr_el0` on ARM64).
- **`GraphicsShim.cpp`**: Bridges Android's `ANativeWindow` and `EGL` to iOS's `CAMetalLayer`. It intercepts `eglGetDisplay` and translates it to `eglGetPlatformDisplayEXT` to force ANGLE to use the correct backend (Vulkan/Metal) since default display initialization fails on iOS.
- **`InputShim.cpp`**: Provides stubs and bridges for Android's `ASensorManager` and other input events.
- **`BionicShim.cpp`**: Now acts strictly as a centralized loader/dispatcher that builds the symbol tables from the modular shims and resolves them dynamically.

### ELF Loader (`src/elf/`)
- Custom ELF parsing and loading mechanism to load Android ELF `.so` files natively into iOS memory, handling relocations and dynamic linking because the iOS dynamic linker cannot process Android ELFs.

### Swift Bridge (`ios-app/` & `src/kudroid_bridge.cpp`)
- `ContentView.swift` (iOS UI) dynamically fetches screen resolution and passes the `CAMetalLayer` pointer via `kudroid_set_metal_layer(layer, width, height)`.
- `kudroid_bridge.cpp` initializes the JVM, registers JNI functions, and launches the Android app's native lifecycle.

## 3. Recent Progress
- **Modularization**: Successfully broke down `BionicShim` into logical modules without breaking the build.
- **Dynamic Resolution**: iOS screen resolution is now accurately passed down to `GraphicsShim` to avoid hardcoded 1080x1920 sizes.
- **ANGLE / EGL Fix**: Resolved a critical bug where `eglGetDisplay` returned `0x0` by implementing a fallback chain using `eglGetPlatformDisplayEXT` targeting Vulkan/Metal backends.

## 4. Next Steps & Missing Features (How you can help)
If you are joining this project, please prioritize the following areas (subject to the User's immediate instructions):
- **Audio Support**: Implementing shims for `libOpenSLES.so` and `libaaudio.so` and mapping them to iOS CoreAudio or `miniaudio`.
- **Touch Screen Input**: Bridging iOS `UIView` touch events to Android `MotionEvent` and injecting them into the Android app's event queue.
- **Incomplete Syscalls**: Fleshing out dummy symbol fallbacks for complex Linux syscalls (e.g., `epoll`, `futex`, `eventfd`, `timerfd`) that are currently stubbed but might be heavily used by complex Android games.

## 5. Coding Rules
1. **If it works, don't touch it**: Do not over-engineer or rewrite core working logic unless explicitly requested. The priority is getting practical features (like rendering a triangle) to work natively.
2. **Never break the build**: KuDroid heavily depends on CMake and complex linking across C++, Swift, and Assembly. Double-check headers and namespaces when adding new shims.
3. **No Emulation**: Remember, we do not emulate instructions. We only bridge APIs.
