#!/usr/bin/env python3
"""vá makefile của avian cho các bản dựng ios arm64.

trình chuyển đổi `binarytoobject` của avian tạo ra một đối tượng với kiến trúc 'không xác định'
trên ios/mach-o, gây lỗi tại thời điểm liên kết. nó được sử dụng để nhúng hai tệp jar
(classpath.jar và javahome.jar) dưới dạng mảng byte. chúng tôi thay thế cả hai quy tắc
trình chuyển đổi bằng một phần mô phỏng hợp ngữ nhỏ nhúng tệp jar qua `.incbin` và hiển thị
cùng các ký hiệu mà avian đã tham chiếu
(`_binary_<name>_jar_start` / `_binary_<name>_jar_end`).

ở cấp độ mach-o asm, một ký hiệu c như `_binary_classpath_jar_start` có thêm một
dấu gạch dưới ở đầu, do đó là `__binary_..._jar_start` trong phần mô phỏng. bởi vì
tên ký hiệu không thay đổi, không cần sửa đổi nguồn c++. việc sử dụng
các nhãn hợp ngữ (thay vì `ld -sectcreate`) cũng tránh được giới hạn 16 ký tự
tên phần của mach-o.

chạy từ thư mục nguồn avian (third_party/jvm/avian).
"""
import sys


def converter_rule(target_var, jar_var, sym):
    """văn bản quy tắc dựa trên trình chuyển đổi ban đầu cho một tệp jar/ký hiệu nhất định."""
    return (
        f"{target_var}: {jar_var} $(converter)\n"
        "\t@echo \"creating $(@)\"\n"
        f"\t$(converter) $(<) $(@) _binary_{sym}_jar_start \\\n"
        f"\t\t_binary_{sym}_jar_end $(target-format) $(arch)"
    )


def incbin_rule(target_var, jar_var, sym):
    """quy tắc thay thế nhúng tệp jar qua một phần mô phỏng hợp ngữ .incbin."""
    return (
        f"{target_var}: {jar_var}\n"
        "\t@echo \"creating $(@) via .incbin stub\"\n"
        f"\t@printf '.global __binary_{sym}_jar_start\\n"
        f".global __binary_{sym}_jar_end\\n"
        ".section __DATA,__const\\n"
        ".balign 8\\n"
        f"__binary_{sym}_jar_start:\\n"
        ".incbin \"$(<)\"\\n"
        f"__binary_{sym}_jar_end:\\n' > $(build)/{sym}-jar.s\n"
        f"\t$(cc) $(asmflags) -c $(build)/{sym}-jar.s -o $(@)"
    )


PATCHES = [
    ("$(classpath-object)", "$(build)/classpath.jar", "classpath",
     "makefile (classpath-jar.o)"),
    ("$(javahome-object)", "$(build)/javahome.jar", "javahome",
     "makefile (javahome-jar.o)"),
]

# vá quy tắc classpath.jar để cũng hợp nhất các lớp khuôn khổ kudroid
# (framework/build/framework.jar) vào tệp jar, để các lớp android.* nằm trên
# classpath khởi động khi chạy.
CLASSPATH_JAR_OLD = (
    "$(build)/classpath.jar: $(classpath-dep) $(classpath-jar-dep)\n"
    "\t@echo \"creating $(@)\"\n"
    "\t(wd=$$(pwd) && \\\n"
    "\t cd $(classpath-build) && \\\n"
    "\t $(jar) c0f \"$$($(native-path) \"$${wd}/$(@)\")\" .)"
)

CLASSPATH_JAR_NEW = (
    "$(build)/classpath.jar: $(classpath-dep) $(classpath-jar-dep)\n"
    "\t@echo \"creating $(@)\"\n"
    "\t(wd=$$(pwd) && \\\n"
    "\t cd $(classpath-build) && \\\n"
    "\t if [ -f \"$${wd}/../../../framework/build/framework.jar\" ]; then \\\n"
    "\t   echo \"merging framework classes into classpath\"; \\\n"
    "\t   $(jar) xf \"$${wd}/../../../framework/build/framework.jar\"; \\\n"
    "\t fi && \\\n"
    "\t $(jar) c0f \"$$($(native-path) \"$${wd}/$(@)\")\" .)"
)

# vá quy tắc thư viện tĩnh để libavian.a chứa cả boot.o (hàm classpathJar)
# và classpath-jar.o (jar nhúng). mặc định avian chỉ đưa 2 object này vào
# executable/dynamic-library, nhưng kudroid chỉ link libavian.a -> dlsym("classpathJar")
# trả NULL -> boot classpath rỗng -> FindClass trả NULL (JNI test abort).
STATIC_LIBRARY_OLD = (
    "$(static-library): $(vm-objects) $(classpath-objects) $(vm-heapwalk-objects) \\\n"
    "\t\t$(javahome-object) $(boot-javahome-object) $(lzma-decode-objects)"
)

STATIC_LIBRARY_NEW = (
    "$(static-library): $(vm-objects) $(classpath-objects) $(vm-heapwalk-objects) \\\n"
    "\t\t$(boot-object) $(vm-classpath-objects) \\\n"
    "\t\t$(javahome-object) $(boot-javahome-object) $(lzma-decode-objects)"
)


def patch_rule(content, target_var, jar_var, sym, label):
    old = converter_rule(target_var, jar_var, sym)
    new = incbin_rule(target_var, jar_var, sym)
    if new.split("\n")[0] in content and old not in content:
        print(f"{label}: already patched, skipping")
        return content
    if old not in content:
        print(f"WARNING: {label}: pattern not found, skipping")
        return content
    print(f"Patched {label}")
    return content.replace(old, new)


def main():
    with open("makefile", "r") as f:
        content = f.read()
    for target_var, jar_var, sym, label in PATCHES:
        content = patch_rule(content, target_var, jar_var, sym, label)

    if CLASSPATH_JAR_NEW.split("\n")[0] in content and CLASSPATH_JAR_OLD not in content:
        print("makefile (classpath.jar merge): already patched, skipping")
    elif CLASSPATH_JAR_OLD not in content:
        print("WARNING: makefile (classpath.jar merge): pattern not found, skipping")
    else:
        content = content.replace(CLASSPATH_JAR_OLD, CLASSPATH_JAR_NEW)
        print("Patched makefile (classpath.jar merge)")

    if STATIC_LIBRARY_NEW in content and STATIC_LIBRARY_OLD not in content:
        print("makefile (static-library): already patched, skipping")
    elif STATIC_LIBRARY_OLD not in content:
        print("WARNING: makefile (static-library): pattern not found, skipping")
    else:
        content = content.replace(STATIC_LIBRARY_OLD, STATIC_LIBRARY_NEW)
        print("Patched makefile (static-library): boot.o + classpath-jar.o into libavian.a")

    with open("makefile", "w") as f:
        f.write(content)


# ─────────────────────────────────────────────────────────────────────────────
# src/codegen/compiler.cpp — UB fix: saveState gọi `c->saved->count()` khi
# `c->saved` có thể NULL (saved==0 nghĩa là danh sách rỗng). Gọi member function
# trên con trỏ NULL là UB — clang -O2/-O3 (cả AppleClang trên iOS) được phép
# giả định `this != NULL` và bỏ null check → deref 0x8 → SIGSEGV/abort khi Avian
# JIT compile class đầu tiên từ boot classpath nhúng. -O0 che giấu bug nên build
# debug chạy được, build fast (mode=fast, -O3) crash. Null-guard giữ nguyên ý
# định (đếm 0 khi list rỗng).
# ─────────────────────────────────────────────────────────────────────────────

COMPILER_CPP_OLD = (
    "  unsigned elementCount = frameFootprint(c, c->stack) + c->saved->count();"
)

COMPILER_CPP_NEW = (
    "  unsigned elementCount = frameFootprint(c, c->stack)\n"
    "      + (c->saved ? c->saved->count() : 0);  // saved==0 == empty list; "
    "null-guard (UB otherwise — clang -O3 removes the check)"
)


def patch_compiler_cpp():
    path = "src/codegen/compiler.cpp"
    try:
        with open(path, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"WARNING: unable to read {path}: {e}")
        return
    if COMPILER_CPP_NEW.split("\n")[0] in content and COMPILER_CPP_OLD not in content:
        print("compiler.cpp (saveState null-guard): already patched, skipping")
        return
    if COMPILER_CPP_OLD not in content:
        print("WARNING: compiler.cpp (saveState null-guard): pattern not found, skipping")
        return
    content = content.replace(COMPILER_CPP_OLD, COMPILER_CPP_NEW)
    with open(path, "w") as f:
        f.write(content)
    print("Patched compiler.cpp (saveState null-guard)")


def patch_posix_cpp():
    path = "src/system/posix.cpp"
    try:
        with open(path, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"WARNING: unable to read {path}: {e}")
        return

    # 1. Sửa mmap check (MAP_FAILED == (void*)-1, not 0)
    old_mmap = "void* data = mmap(0, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);\n        if (data) {"
    new_mmap = "void* data = mmap(0, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);\n        if (data && data != MAP_FAILED) {"
    if old_mmap in content:
        content = content.replace(old_mmap, new_mmap)
        print("Patched posix.cpp (mmap MAP_FAILED check)")

    # 2. Sửa dlopen cho main executable trên iOS (name == 0 -> dlopen(0, ...))
    old_load = (
        "    if (isMain) {\n"
        "      pathOfExecutable(this, &name, &nameLength);\n"
        "    }\n"
        "    void* p = dlopen(name, RTLD_LAZY | RTLD_LOCAL);"
    )
    new_load = (
        "    void* p = 0;\n"
        "    if (isMain) {\n"
        "      p = dlopen(0, RTLD_LAZY | RTLD_GLOBAL);\n"
        "    } else {\n"
        "      p = dlopen(name, RTLD_LAZY | RTLD_LOCAL);\n"
        "    }"
    )
    if old_load in content:
        content = content.replace(old_load, new_load)
        print("Patched posix.cpp (dlopen isMain handle)")

    # 3. Sửa resolve để fallback RTLD_DEFAULT và underscore prefix trên Darwin
    old_resolve = (
        "    virtual void* resolve(const char* function)\n"
        "    {\n"
        "      return dlsym(p, function);\n"
        "    }"
    )
    new_resolve = (
        "    virtual void* resolve(const char* function)\n"
        "    {\n"
        "      void* res = dlsym(p, function);\n"
        "      if (!res) res = dlsym(RTLD_DEFAULT, function);\n"
        "      if (!res && function && function[0] != '_') {\n"
        "        char under[128] = \"_\";\n"
        "        strncat(under, function, sizeof(under) - 2);\n"
        "        res = dlsym(p, under);\n"
        "        if (!res) res = dlsym(RTLD_DEFAULT, under);\n"
        "      }\n"
        "      return res;\n"
        "    }"
    )
    if old_resolve in content:
        content = content.replace(old_resolve, new_resolve)
        print("Patched posix.cpp (dlsym fallback + Darwin underscore)")

    with open(path, "w") as f:
        f.write(content)


def patch_crash_cpp():
    path = "src/system/posix/crash.cpp"
    try:
        with open(path, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"WARNING: unable to read {path}: {e}")
        return

    old_crash = "NO_RETURN void crash()\n{\n  abort();\n}"
    new_crash = (
        "NO_RETURN void crash()\n"
        "{\n"
        '  const char msg[] = "\\n[AVIAN_CRASH] avian::system::crash() called — internal JVM abort!\\n";\n'
        "  (void)!write(2, msg, sizeof(msg) - 1);\n"
        "  (void)!write(1, msg, sizeof(msg) - 1);\n"
        "  abort();\n"
        "}"
    )
    if old_crash in content:
        content = content.replace(old_crash, new_crash)
        if '#include <unistd.h>' not in content:
            content = '#include <unistd.h>\n' + content
        with open(path, "w") as f:
            f.write(content)
        print("Patched crash.cpp (unbuffered crash diagnostic message)")


def patch_memory_cpp():
    path = "src/system/posix/memory.cpp"
    try:
        with open(path, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"WARNING: unable to read {path}: {e}")
        return

    old_extra = (
        "#ifdef MAP_32BIT\n"
        "  // map to the lower 32 bits of memory when possible so as to avoid\n"
        "  // expensive relative jumps\n"
        "  const unsigned Extra = MAP_32BIT;\n"
        "#else\n"
        "  const unsigned Extra = 0;\n"
        "#endif"
    )
    new_extra = (
        "  unsigned Extra = 0;\n"
        "#ifdef MAP_32BIT\n"
        "  Extra |= MAP_32BIT;\n"
        "#endif\n"
        "#if defined(__APPLE__)\n"
        "#ifndef MAP_JIT\n"
        "#define MAP_JIT 0x0800\n"
        "#endif\n"
        "  if (perms & Execute) {\n"
        "    Extra |= MAP_JIT;\n"
        "  }\n"
        "#endif"
    )
    if old_extra in content:
        content = content.replace(old_extra, new_extra)

    old_failed = "  if (p == MAP_FAILED) {\n    return util::Slice<uint8_t>(0, 0);"
    new_failed = (
        "  if (p == MAP_FAILED) {\n"
        "#if defined(__APPLE__)\n"
        "    if (perms & Execute) {\n"
        "      // Fallback: nếu không có JIT entitlement, allocate RW thông thường để Interpreter vẫn chạy ngon lành!\n"
        "      p = mmap(0, sizeInBytes, (prot & ~PROT_EXEC), MAP_PRIVATE | MAP_ANON, -1, 0);\n"
        "    }\n"
        "#endif\n"
        "  }\n"
        "  if (p == MAP_FAILED) {\n"
        "    return util::Slice<uint8_t>(0, 0);"
    )
    if old_failed in content:
        content = content.replace(old_failed, new_failed)

    with open(path, "w") as f:
        f.write(content)
    print("Patched memory.cpp (MAP_JIT flag + non-JIT RW fallback for iOS)")


def patch_assert_h():
    """UNREACHABLE(msg)/ASSERT(that) trong Avian gọi ::abort() TRẦN — không in gì.
    Đây là một trong các nguồn "abort câm" khiến mọi crash phải đoán mò. Cho nó
    in file:line + biểu thức ra fd 2 (write, không stdio → không cần flush)."""
    path = "include/avian/util/assert.h"
    try:
        with open(path, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"WARNING: unable to read {path}: {e}")
        return

    old = (
        "#define UNREACHABLE_ ::abort()\n"
        "\n"
        "// TODO: print msg in debug mode\n"
        "#define UNREACHABLE(msg) ::abort()"
    )
    new = (
        "// KuDroid: in lý do trước khi abort — abort() trần không in gì và không\n"
        "// flush stdio, nên lý do chết bị mất hoàn toàn trên iOS.\n"
        "#define AVIAN_ABORT_MSG(where)                                     \\\n"
        "  do {                                                             \\\n"
        "    const char m[] = \"\\n[AVIAN_ABORT] \" where \" at \" __FILE__ \"\\n\"; \\\n"
        "    (void)!::write(2, m, sizeof(m) - 1);                            \\\n"
        "  } while (0)\n"
        "\n"
        "#define UNREACHABLE_          \\\n"
        "  do {                        \\\n"
        "    AVIAN_ABORT_MSG(\"UNREACHABLE\"); \\\n"
        "    ::abort();                \\\n"
        "  } while (0)\n"
        "\n"
        "#define UNREACHABLE(msg)          \\\n"
        "  do {                            \\\n"
        "    AVIAN_ABORT_MSG(\"UNREACHABLE: \" #msg); \\\n"
        "    ::abort();                    \\\n"
        "  } while (0)"
    )
    if old in content:
        content = content.replace(old, new)
        if "#include <unistd.h>" not in content:
            content = content.replace("#include <stdlib.h>",
                                      "#include <stdlib.h>\n#include <unistd.h>")
        with open(path, "w") as f:
            f.write(content)
        print("Patched assert.h (UNREACHABLE/ASSERT now print reason)")


def patch_finder_cpp_debug():
    """DebugFind = false làm BuiltinElement::init() nuốt sạch lỗi resolve
    [classpathJar]: không load được symbol → index=0 → mọi FindClass fail mà
    KHÔNG một dòng log nào. Bật lên để lỗi bootclasspath hiện ra ngay."""
    path = "src/finder.cpp"
    try:
        with open(path, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"WARNING: unable to read {path}: {e}")
        return

    old = "const bool DebugFind = false;"
    new = "const bool DebugFind = true;  // KuDroid: cần thấy lỗi resolve bootclasspath"
    if old in content:
        content = content.replace(old, new)
        with open(path, "w") as f:
            f.write(content)
        print("Patched finder.cpp (DebugFind enabled)")


def patch_process_cpp():
    path = "src/process.cpp"
    try:
        with open(path, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"WARNING: unable to read {path}: {e}")
        return

    if "findBuiltinNativeFallback" in content:
        print("process.cpp: already patched, skipping")
        return

    if "#if !defined(PLATFORM_WINDOWS)\n#include <dlfcn.h>\n#endif" not in content:
        content = content.replace(
            '#include <avian/util/runtime-array.h>',
            '#include <avian/util/runtime-array.h>\n#if !defined(PLATFORM_WINDOWS)\n#include <dlfcn.h>\n#endif'
        )

    old_res = (
        'void* resolveNativeMethod(Thread* t,\n'
        '                          const char* undecorated,\n'
        '                          const char* decorated)\n'
        '{\n'
        '  for (System::Library* lib = t->m->libraries; lib; lib = lib->next()) {\n'
        '    void* p = lib->resolve(undecorated);\n'
        '    if (p) {\n'
        '      return p;\n'
        '    } else {\n'
        '      p = lib->resolve(decorated);\n'
        '      if (p) {\n'
        '        return p;\n'
        '      }\n'
        '    }\n'
        '  }\n'
        '\n'
        '  return 0;\n'
        '}'
    )

    new_res = (
        'extern "C" AVIAN_EXPORT int64_t JNICALL Avian_avian_Classes_makeString(Thread* t, object, uintptr_t* arguments);\n'
        'extern "C" AVIAN_EXPORT void JNICALL Avian_avian_Classes_initialize(Thread* t, object, uintptr_t* arguments);\n'
        'extern "C" AVIAN_EXPORT int64_t JNICALL Avian_avian_Classes_resolveVMClass(Thread* t, object, uintptr_t* arguments);\n'
        'extern "C" AVIAN_EXPORT int64_t JNICALL Avian_avian_Classes_defineVMClass(Thread* t, object, uintptr_t* arguments);\n'
        'extern "C" AVIAN_EXPORT int64_t JNICALL Avian_avian_Classes_toVMClass(Thread* t, object, uintptr_t* arguments);\n'
        'extern "C" AVIAN_EXPORT int64_t JNICALL Avian_avian_Classes_toVMMethod(Thread* t, object, uintptr_t* arguments);\n'
        'extern "C" AVIAN_EXPORT int64_t JNICALL Avian_avian_SystemClassLoader_appLoader(Thread* t, object, uintptr_t*);\n'
        'extern "C" AVIAN_EXPORT int64_t JNICALL Avian_avian_SystemClassLoader_findLoadedVMClass(Thread* t, object, uintptr_t* arguments);\n'
        'extern "C" AVIAN_EXPORT int64_t JNICALL Avian_avian_SystemClassLoader_findVMClass(Thread* t, object, uintptr_t* arguments);\n'
        'extern "C" AVIAN_EXPORT int64_t JNICALL Avian_avian_SystemClassLoader_vmClass(Thread* t, object, uintptr_t* arguments);\n'
        'extern "C" AVIAN_EXPORT int64_t JNICALL Avian_avian_SystemClassLoader_getClass(Thread* t, object, uintptr_t* arguments);\n'
        'extern "C" AVIAN_EXPORT int64_t JNICALL Avian_avian_Machine_dumpHeap(Thread* t, object, uintptr_t* arguments);\n'
        'extern "C" AVIAN_EXPORT int64_t JNICALL Avian_avian_Machine_tryNative(Thread* t, object, uintptr_t* arguments);\n'
        'extern "C" AVIAN_EXPORT void JNICALL Avian_java_lang_Runtime_exit(Thread* t, object, uintptr_t* arguments);\n'
        'extern "C" AVIAN_EXPORT int64_t JNICALL Avian_java_lang_Runtime_freeMemory(Thread* t, object, uintptr_t*);\n'
        'extern "C" AVIAN_EXPORT int64_t JNICALL Avian_java_lang_Runtime_totalMemory(Thread* t, object, uintptr_t*);\n'
        'extern "C" AVIAN_EXPORT int64_t JNICALL Avian_java_lang_Runtime_maxMemory(Thread* t, object, uintptr_t*);\n\n'
        'static void* findBuiltinNativeFallback(const char* name) {\n'
        '    if (!name) return 0;\n'
        '    if (!strcmp(name, "Avian_avian_Classes_makeString")) return reinterpret_cast<void*>(&Avian_avian_Classes_makeString);\n'
        '    if (!strcmp(name, "Avian_avian_Classes_initialize")) return reinterpret_cast<void*>(&Avian_avian_Classes_initialize);\n'
        '    if (!strcmp(name, "Avian_avian_Classes_resolveVMClass")) return reinterpret_cast<void*>(&Avian_avian_Classes_resolveVMClass);\n'
        '    if (!strcmp(name, "Avian_avian_Classes_defineVMClass")) return reinterpret_cast<void*>(&Avian_avian_Classes_defineVMClass);\n'
        '    if (!strcmp(name, "Avian_avian_Classes_toVMClass")) return reinterpret_cast<void*>(&Avian_avian_Classes_toVMClass);\n'
        '    if (!strcmp(name, "Avian_avian_Classes_toVMMethod")) return reinterpret_cast<void*>(&Avian_avian_Classes_toVMMethod);\n'
        '    if (!strcmp(name, "Avian_avian_SystemClassLoader_appLoader")) return reinterpret_cast<void*>(&Avian_avian_SystemClassLoader_appLoader);\n'
        '    if (!strcmp(name, "Avian_avian_SystemClassLoader_findLoadedVMClass")) return reinterpret_cast<void*>(&Avian_avian_SystemClassLoader_findLoadedVMClass);\n'
        '    if (!strcmp(name, "Avian_avian_SystemClassLoader_findVMClass")) return reinterpret_cast<void*>(&Avian_avian_SystemClassLoader_findVMClass);\n'
        '    if (!strcmp(name, "Avian_avian_SystemClassLoader_vmClass")) return reinterpret_cast<void*>(&Avian_avian_SystemClassLoader_vmClass);\n'
        '    if (!strcmp(name, "Avian_avian_SystemClassLoader_getClass")) return reinterpret_cast<void*>(&Avian_avian_SystemClassLoader_getClass);\n'
        '    if (!strcmp(name, "Avian_avian_Machine_dumpHeap")) return reinterpret_cast<void*>(&Avian_avian_Machine_dumpHeap);\n'
        '    if (!strcmp(name, "Avian_avian_Machine_tryNative")) return reinterpret_cast<void*>(&Avian_avian_Machine_tryNative);\n'
        '    if (!strcmp(name, "Avian_java_lang_Runtime_exit")) return reinterpret_cast<void*>(&Avian_java_lang_Runtime_exit);\n'
        '    if (!strcmp(name, "Avian_java_lang_Runtime_freeMemory")) return reinterpret_cast<void*>(&Avian_java_lang_Runtime_freeMemory);\n'
        '    if (!strcmp(name, "Avian_java_lang_Runtime_totalMemory")) return reinterpret_cast<void*>(&Avian_java_lang_Runtime_totalMemory);\n'
        '    if (!strcmp(name, "Avian_java_lang_Runtime_maxMemory")) return reinterpret_cast<void*>(&Avian_java_lang_Runtime_maxMemory);\n'
        '    return 0;\n'
        '}\n\n'
        'void* resolveNativeMethod(Thread* t,\n'
        '                          const char* undecorated,\n'
        '                          const char* decorated)\n'
        '{\n'
        '  for (System::Library* lib = t->m->libraries; lib; lib = lib->next()) {\n'
        '    void* p = lib->resolve(undecorated);\n'
        '    if (p) {\n'
        '      return p;\n'
        '    } else {\n'
        '      p = lib->resolve(decorated);\n'
        '      if (p) {\n'
        '        return p;\n'
        '      }\n'
        '    }\n'
        '  }\n\n'
        '  void* p = findBuiltinNativeFallback(undecorated);\n'
        '  if (p) return p;\n'
        '  p = findBuiltinNativeFallback(decorated);\n'
        '  if (p) return p;\n\n'
        '#if !defined(PLATFORM_WINDOWS)\n'
        '  p = dlsym(RTLD_DEFAULT, undecorated);\n'
        '  if (p) return p;\n'
        '  p = dlsym(RTLD_DEFAULT, decorated);\n'
        '  if (p) return p;\n'
        '#endif\n\n'
        '  return 0;\n'
        '}'
    )

    if old_res in content:
        content = content.replace(old_res, new_res)
        with open(path, "w") as f:
            f.write(content)
        print("Patched process.cpp (findBuiltinNativeFallback + dlsym fallback)")


def patch_cpp20_math_opcodes():
    files = ["src/machine.cpp", "src/compile.cpp", "src/debug-util.cpp"]
    for path in files:
        try:
            with open(path, "r") as f:
                content = f.read()
        except Exception as e:
            print(f"WARNING: unable to read {path}: {e}")
            continue

        for op in ["fadd", "fsub", "fmul", "fdiv"]:
            content = content.replace(f"case {op}:", f"case vm::{op}:")

        with open(path, "w") as f:
            f.write(content)
        print(f"Patched {path} (vm:: opcode namespace for C++20/cmath)")


def patch_java_net_cpp():
    path = "classpath/java-net.cpp"
    try:
        with open(path, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"WARNING: unable to read {path}: {e}")
        return
    if '#include "jni.h"' in content:
        content = content.replace('#include "jni.h"', '#include "avian/jnienv.h"')
        with open(path, "w") as f:
            f.write(content)
        print("Patched java-net.cpp (jnienv.h include)")


def patch_java_lang_cpp():
    path = "classpath/java-lang.cpp"
    try:
        with open(path, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"WARNING: unable to read {path}: {e}")
        return

    if "Java_java_lang_System_arraycopy" in content:
        print("java-lang.cpp (arraycopy): already patched, skipping")
        return

    stub = '''
extern "C" JNIEXPORT void JNICALL
Java_java_lang_System_arraycopy(JNIEnv* env, jclass,
                                jobject src, jint srcPos,
                                jobject dst, jint dstPos, jint length)
{
    if (!src || !dst) {
        jclass npe = env->FindClass("java/lang/NullPointerException");
        if (npe) env->ThrowNew(npe, "src or dst is null");
        return;
    }
    if (srcPos < 0 || dstPos < 0 || length < 0) {
        jclass aioobe = env->FindClass("java/lang/IndexOutOfBoundsException");
        if (aioobe) env->ThrowNew(aioobe, "negative index or length");
        return;
    }
    if (length == 0) return;

    jsize srcLen = env->GetArrayLength(static_cast<jarray>(src));
    jsize dstLen = env->GetArrayLength(static_cast<jarray>(dst));
    if (srcPos + length > srcLen || dstPos + length > dstLen) {
        jclass aioobe = env->FindClass("java/lang/IndexOutOfBoundsException");
        if (aioobe) env->ThrowNew(aioobe, "array index out of bounds");
        return;
    }

    jclass byteArrCls = env->FindClass("[B");
    if (byteArrCls && env->IsInstanceOf(src, byteArrCls)) {
        jbyte* buf = static_cast<jbyte*>(malloc(length * sizeof(jbyte)));
        if (buf) {
            env->GetByteArrayRegion(static_cast<jbyteArray>(src), srcPos, length, buf);
            env->SetByteArrayRegion(static_cast<jbyteArray>(dst), dstPos, length, buf);
            free(buf);
        }
        env->DeleteLocalRef(byteArrCls);
        return;
    }
    if (byteArrCls) env->DeleteLocalRef(byteArrCls);

    jclass charArrCls = env->FindClass("[C");
    if (charArrCls && env->IsInstanceOf(src, charArrCls)) {
        jchar* buf = static_cast<jchar*>(malloc(length * sizeof(jchar)));
        if (buf) {
            env->GetCharArrayRegion(static_cast<jcharArray>(src), srcPos, length, buf);
            env->SetCharArrayRegion(static_cast<jcharArray>(dst), dstPos, length, buf);
            free(buf);
        }
        env->DeleteLocalRef(charArrCls);
        return;
    }
    if (charArrCls) env->DeleteLocalRef(charArrCls);

    jclass intArrCls = env->FindClass("[I");
    if (intArrCls && env->IsInstanceOf(src, intArrCls)) {
        jint* buf = static_cast<jint*>(malloc(length * sizeof(jint)));
        if (buf) {
            env->GetIntArrayRegion(static_cast<jintArray>(src), srcPos, length, buf);
            env->SetIntArrayRegion(static_cast<jintArray>(dst), dstPos, length, buf);
            free(buf);
        }
        env->DeleteLocalRef(intArrCls);
        return;
    }
    if (intArrCls) env->DeleteLocalRef(intArrCls);

    if (src == dst && srcPos < dstPos) {
        for (jint i = length - 1; i >= 0; --i) {
            jobject elem = env->GetObjectArrayElement(static_cast<jobjectArray>(src), srcPos + i);
            env->SetObjectArrayElement(static_cast<jobjectArray>(dst), dstPos + i, elem);
            if (elem) env->DeleteLocalRef(elem);
        }
    } else {
        for (jint i = 0; i < length; ++i) {
            jobject elem = env->GetObjectArrayElement(static_cast<jobjectArray>(src), srcPos + i);
            env->SetObjectArrayElement(static_cast<jobjectArray>(dst), dstPos + i, elem);
            if (elem) env->DeleteLocalRef(elem);
        }
    }
}
'''
    content += stub
    with open(path, "w") as f:
        f.write(content)
    print("Patched java-lang.cpp (Java_java_lang_System_arraycopy)")


if __name__ == "__main__":
    main()
    patch_compiler_cpp()
    patch_posix_cpp()
    patch_crash_cpp()
    patch_memory_cpp()
    patch_assert_h()
    patch_finder_cpp_debug()
    patch_process_cpp()
    patch_cpp20_math_opcodes()
    patch_java_net_cpp()
    patch_java_lang_cpp()
    sys.exit(0)
