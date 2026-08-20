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
        "        char under[128];\n"
        "        snprintf(under, sizeof(under), \"_%s\", function);\n"
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


def patch_finder_cpp():
    path = "src/finder.cpp"
    try:
        with open(path, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"WARNING: unable to read {path}: {e}")
        return

    # Fallback trực tiếp con trỏ classpathJar nếu dlsym trả NULL
    old_resolve_call = "void* p = library->resolve(symbolName);"
    new_resolve_call = (
        "void* p = library ? library->resolve(symbolName) : 0;\n"
        "        if (!p && strcmp(symbolName, \"classpathJar\") == 0) {\n"
        "          extern const uint8_t* classpathJar(size_t*) __attribute__((weak));\n"
        "          if (classpathJar) p = reinterpret_cast<void*>(classpathJar);\n"
        "        }"
    )
    if old_resolve_call in content:
        content = content.replace(old_resolve_call, new_resolve_call)
        print("Patched finder.cpp (direct classpathJar weak fallback)")

    with open(path, "w") as f:
        f.write(content)


if __name__ == "__main__":
    main()
    patch_compiler_cpp()
    patch_posix_cpp()
    patch_finder_cpp()
    sys.exit(0)
