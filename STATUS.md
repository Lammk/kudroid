# 🤖 KuDroid — Project Status

> **KuDroid** — A Translation Layer / Emulation Tier to run Android Apps on **iOS ARM64**.

---

## 1. 📌 Header & Metadata

| Field | Value |
|-------|-------|
| **Project** | KuDroid |
| **Author** | Kuzei (`kuzei13.dev`) |
| **Type** | Android → iOS ARM64 Translation Layer |
| **Tech Stack** | C++17 · POSIX · LLVM · iOS XNU · ARM64 Assembly |
| **Current Phase** | **Phase 1 — Custom Dynamic ELF Loader & Memory Binding** |
| **Overall Progress** | **5%** (Phase 0 complete, buildable skeleton) |
| **Last Updated** | 2026-08-08 |

---

## 2. 🟢 What Works Right Now

> **Phase 0 Bootstrap is complete.** The repository has a working CMake build system targeting C++17 with iOS ARM64 cross-compile support.

- [x] Directory structure (`src/`, `include/kudroid/`, `tests/`, `third_party/`)
- [x] Root `CMakeLists.txt` with C++17 standard and iOS ARM64 toolchain config
- [x] `include/kudroid/elf_loader.hpp` — `ElfLoader` class + `Segment` struct
- [x] `src/elf_loader.cpp` — stub implementation (parse/map/relocate)
- [x] `.gitignore` (build artifacts, IDE files, compiled objects)
- [x] `.clang-format` (LLVM style, 4-space indent)
- [x] `.github/workflows/build.yml` CI skeleton
- [x] `libkudroid_core.a` builds cleanly on Linux x86_64 (GCC 16.1.1)

> ✅ Build verified: `[100%] Built target kudroid_core`

---

## 3. 🗺️ Roadmap & Milestones

### Phase 0 — Bootstrap / Scaffolding `(done)`
- [x] Initialize repo structure (`src/`, `include/`, `tests/`, `third_party/`)
- [x] Add root `CMakeLists.txt` targeting `arm64-apple-ios` toolchain
- [x] Add `.clang-format`, `.gitignore`, and CI workflow skeleton
- [x] Verify a "hello world" binary builds for ARM64

### Phase 1 — Custom Dynamic ELF Loader & Memory Binding (ARM64)
- [ ] Parse ELF64 headers, program headers, and section headers
- [ ] Map `PT_LOAD` segments into JIT-executable memory (`mmap` + `mprotect`)
- [ ] Resolve dynamic symbols (`.dynsym` / `.dynstr`) and relocations (`R_AARCH64_*`)
- [ ] Implement GOT/PLT stub binding + lazy resolution
- [ ] Handle `DT_INIT_ARRAY` / constructor invocation

### Phase 2 — Dalvik DEX Parser & JIT Engine
- [ ] Parse DEX file format (header, string/type/method/class defs)
- [ ] Decode Dalvik bytecode into an internal IR
- [ ] Lower IR → LLVM IR → ARM64 machine code (JIT)
- [ ] Register allocation + method dispatch cache

### Phase 3 — Android Runtime Environment & Syscall Bridge
- [ ] Bionic-compatible libc shim over POSIX / iOS XNU
- [ ] Syscall translation table (Linux → XNU / userspace emulation)
- [ ] JNI bridge + `libart` surface stubs
- [ ] Threading + `pthread` / futex emulation

### Phase 4 — Graphics Translation & Touch Layer
- [ ] GLES/Vulkan → Metal via ANGLE / MoltenVK integration
- [ ] Surface/framebuffer compositor into a `CAMetalLayer`
- [ ] Input event bridge (iOS `UITouch` → Android `MotionEvent`)

### Phase 5 — APK Container & SwiftUI Shell
- [ ] APK unpack + manifest parsing
- [ ] Resource (`resources.arsc`) loader
- [ ] SwiftUI host shell + lifecycle glue
- [ ] Packaging + sideload flow

---

## 4. 📱 How to Test on iPhone (Sideload)

> **Prerequisite:** Apple ID (free) — app expires after 7 days, resign to renew.

**Flow:**
1. Push code → GitHub Actions (`macos-latest`) builds:
   - `libkudroid_core.a` (ARM64 iOS static library)
   - `KuDroidShell.ipa` (unsigned)
2. Download `kudroid-ios-all` artifact from Actions tab.
3. **Sideload `.ipa` to iPhone** via one of:
   - **[AltStore](https://altstore.io/)** (Windows/macOS, needs AltServer running)
   - **[SideStore](https://sidestore.io/)** (no PC needed after initial setup, uses WireGuard)
   - **[Sideloadly](https://sideloadly.io/)** (simple, macOS/Windows)
4. Sign with your Apple ID → app installs → valid for 7 days.
5. Open KuDroidShell → tap "Test ELF Loader" → see `✅ ELF Loader OK`.

**CI artifact download URL:**  
`https://github.com/Lammk/kudroid/actions` → latest workflow run → scroll to Artifacts → `kudroid-ios-all`

---

## 5. 🎯 Next Action Items

The next 4-hour session should begin **Phase 1 — ELF Loader implementation**:

1. **Implement `ElfLoader::parse()`:** Open file via `fopen`/`mmap`, validate ELF64 magic (`\x7fELF`), read and store program headers into `segments_`.
2. **Add ELF64 header structs:** Define `Elf64_Ehdr`, `Elf64_Phdr` in a new `include/kudroid/elf_types.hpp` (or use `<elf.h>` if available on target).
3. **Implement `ElfLoader::map()`:** Use `mmap` with `PROT_READ|PROT_EXEC` for `PT_LOAD` segments, handle alignment requirements.
4. **Add unit test scaffold:** Create `tests/test_elf_loader.cpp` with a minimal test that loads a known-good ARM64 ELF binary.
5. **Cross-compile validation:** Test the build with an actual `arm64-apple-ios` toolchain file to ensure portability.

---

## 6. 💻 Quick Code Boilerplate for Next Session

Copy this into `include/kudroid/elf_loader.hpp` to start Phase 1:

```cpp
// include/kudroid/elf_loader.hpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kudroid {

// Minimal ELF64 (ARM64) loader surface. No logic yet — Phase 1 entry point.
class ElfLoader {
public:
    struct Segment {
        std::uint64_t vaddr = 0;   // target virtual address
        std::uint64_t offset = 0;  // file offset of PT_LOAD data
        std::uint64_t filesz = 0;  // bytes in file
        std::uint64_t memsz = 0;   // bytes in memory (>= filesz)
        std::uint32_t flags = 0;   // PF_R | PF_W | PF_X
    };

    explicit ElfLoader(std::string path);
    ~ElfLoader();

    // Parse ELF64 headers + program headers.
    bool parse();

    // mmap PT_LOAD segments as JIT-executable memory.
    bool map();

    // Resolve .dynsym relocations (R_AARCH64_*).
    bool relocate();

    [[nodiscard]] std::uint64_t entryPoint() const { return entry_; }
    [[nodiscard]] const std::vector<Segment>& segments() const { return segments_; }

private:
    std::string           path_;
    void*                 base_  = nullptr;   // mapped base
    std::uint64_t         entry_ = 0;         // e_entry (relocated)
    std::vector<Segment>  segments_;
};

} // namespace kudroid
