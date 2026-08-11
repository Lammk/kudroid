# KuDroid Bug Bash & Stabilization Plan

This plan addresses the massive list of architectural and implementation flaws identified in the KuDroid codebase. The issues range from fatal crashes (Red) to high-risk behaviors (Orange) and minor flaws (Yellow). 

## User Review Required
> [!IMPORTANT]
> The `DT_RELR` (Relative Relocations) implementation in the ELF loader is complex. I will implement a parser for the RELR format to apply these relocations properly.
> For `ashmem` and `ALooper`, I will implement basic pipe draining and fix the callback signatures. 
> Do you approve this plan to tackle the Red and Orange issues first?

## Proposed Changes

---

### SyscallShim.cpp (Core Syscalls & Memory)
Fixing critical crashes in memory management, TLS, and thread primitives.

#### [MODIFY] [SyscallShim.cpp](file:///home/kuzei/Documents/Kudroid/src/shims/SyscallShim.cpp)
- **`bionic_dlclose`**: Add a check `if (handle == DUMMY_HANDLE) return 0;` before calling `::dlclose`. (🔴 Issue 1)
- **`pthread_key_create`**: Change `memcpy` to assign the lower 32-bits (or map the 8-byte key to a 4-byte ID if needed, but since Darwin keys are typically small integers, casting to `int` is safe). `*(int*)guestKey = (int)hostKey;` (🔴 Issue 3)
- **TLS Trap (`bionic_handle_tpidr_trap`)**: Add a lazy-allocation fallback. If `pthread_getspecific(tls_key)` returns NULL, allocate a basic TLS block (just for the main slot) so JNI threads don't crash. (🔴 Issue 5)
- **ALooper & epoll**: 
  - Drain the wake pipe in `bionic_ALooper_pollAll` by reading from it when readable. (🔴 Issue 6)
  - Execute the callbacks registered via `ALooper_addFd` before returning the ident. (🔴 Issue 6)
  - Remove `udata` coalescing in `epoll_wait` so multiple events on different FDs return properly. (🟠 Issue 7)
- **`ioctl` & `getauxval`**: 
  - Set `errno = ENOTTY` and return `-1` for unknown ioctls instead of `0`. (🟠 Issue 8)
  - Return the actual page size for `AT_PAGESZ` (e.g. `16384` on iOS) in `getauxval`. (🟠 Issue 9)
- **`mmap` flags**: Strip `MAP_NORESERVE`, `MAP_POPULATE`, etc., before calling Darwin's `mmap`. (🟠 Issue 10)
- **`pthread_create`**: Pass through the stack size from `attr` if available. Remove the duplicate `pthread_create` entry in the shim table. (🟠 Issue 11)

---

### elf_loader.cpp (ELF Parsing & Memory)
Addressing modern Android ELF standards and memory safety.

#### [MODIFY] [elf_loader.cpp](file:///home/kuzei/Documents/Kudroid/src/elf_loader.cpp)
- **`DT_RELR` Relocations**: Implement parsing of `DT_RELR` (tag 36) in `relocate()`. Decode the bitmap and apply `vaddr += base_` for all relative pointers to support NDK r23+ binaries. (🔴 Issue 2)
- **AOT patch write-protect**: Move `pthread_jit_write_protect_np(1)` (lock) to *after* the `brk_inst` loop (line 334). (🟠 Issue 15)
- **Memory Leak & Pointer Safety**: 
  - Store the original allocated base address (before `minVaddr` adjustment) in a separate `allocBase_` variable to ensure safe `munmap` in the destructor. 
  - Implement `~ElfLoader()` to unmap the memory. (🟠 Issue 16)
- **TLS Layout**: Allocate a zeroed block for TLS and set up `tprel` correctly. (🟠 Issue 17)

---

### kudroid_bridge.cpp & kudroid_jni.cpp (JNI & App Lifecycle)
Fixing JNI threading and mock Android activity callbacks.

#### [MODIFY] [kudroid_bridge.cpp](file:///home/kuzei/Documents/Kudroid/src/kudroid_bridge.cpp)
- **Lifecycle Callbacks**: Fix the mock callback signatures in `kudroid_app_run`. E.g., `reinterpret_cast<void (*)(ANativeActivity*, ANativeWindow*)>(fn)(&mock_activity, window_ptr);` (🔴 Issue 4)
- **`kudroid_load_apk`**: Call `kudroid_jni_init_jvm()` before calling `jniOnLoad()` to ensure `get_javavm()` doesn't return NULL. Fix the thread-unsafe static `log` variable. (🟠 Issue 13)

#### [MODIFY] [kudroid_jni.cpp](file:///home/kuzei/Documents/Kudroid/src/kudroid_jni.cpp)
- **JNIEnv Threading**: Stop returning `g_env` for unregistered threads. If a thread isn't attached, call `AttachCurrentThread` and use thread-local storage (or `pthread_key`) to remember to `DetachCurrentThread` on thread exit. (🟠 Issue 12)

---

### AudioShim.cpp (Audio Translation)
#### [MODIFY] [AudioShim.cpp](file:///home/kuzei/Documents/Kudroid/src/shims/AudioShim.cpp)
- **Silent Audio**: Return a specific error code (or properly mock a dummy processing thread that calls the callback) instead of returning `SUCCESS` and doing nothing, which hangs games waiting for audio buffer consumption. (🟠 Issue 14)

---

## Verification Plan

### Automated Tests
- Run the Vulkan and OpenGL `.so` tests to verify regressions.

### Manual Verification
- Request the user to deploy `test_triangle.apk` and observe if it correctly clears the JNI load and avoids TLS/Looper hangs.
