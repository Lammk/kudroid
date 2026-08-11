#!/usr/bin/env python3
"""Patch Avian's makefile + boot.cpp for iOS ARM64 builds.

binaryToObject produces an object with architecture 'unknown' on iOS/macho,
which fails at link time. We replace it with `ld -r -sectcreate` and switch
boot.cpp to read the embedded jar via getsectdata() (the Apple API).

Run from the Avian source directory (third_party/jvm/avian).
"""
import sys

MAKEFILE_OLD = (
    "$(classpath-object): $(build)/classpath.jar $(converter)\n"
    "\t@echo \"creating $(@)\"\n"
    "\t$(converter) $(<) $(@) _binary_classpath_jar_start \\\n"
    "\t\t_binary_classpath_jar_end $(target-format) $(arch)"
)

MAKEFILE_NEW = (
    "$(classpath-object): $(build)/classpath.jar\n"
    "\t@echo \"creating $(@) via ld -r -sectcreate\"\n"
    "\tld -r -sectcreate __TEXT __kudroid_classpath $(<) -o $(@) -arch $(arch)"
)

BOOT_OLD = """extern "C" {
extern const uint8_t SYMBOL(start)[];
extern const uint8_t SYMBOL(end)[];

AVIAN_EXPORT const uint8_t* classpathJar(size_t* size)
{
  *size = SYMBOL(end) - SYMBOL(start);
  return SYMBOL(start);
}
}"""

BOOT_NEW = """#if defined(__APPLE__)
#include <mach-o/getsect.h>
extern "C" {
AVIAN_EXPORT const uint8_t* classpathJar(size_t* size)
{
  unsigned long s = 0;
  const uint8_t* p =
    reinterpret_cast<const uint8_t*>(getsectdata("__TEXT", "__kudroid_classpath", &s));
  if (size) *size = s;
  return p;
}
}
#else
extern "C" {
extern const uint8_t SYMBOL(start)[];
extern const uint8_t SYMBOL(end)[];

AVIAN_EXPORT const uint8_t* classpathJar(size_t* size)
{
  *size = SYMBOL(end) - SYMBOL(start);
  return SYMBOL(start);
}
}
#endif"""


def patch(path, old, new, label):
    with open(path, "r") as f:
        content = f.read()
    if new.split("\n")[0] in content and old not in content:
        print(f"{label}: already patched, skipping")
        return
    if old not in content:
        print(f"WARNING: {label}: pattern not found, skipping")
        return
    content = content.replace(old, new)
    with open(path, "w") as f:
        f.write(content)
    print(f"Patched {label}")


def main():
    patch("makefile", MAKEFILE_OLD, MAKEFILE_NEW, "makefile (classpath-jar.o)")
    patch("src/boot.cpp", BOOT_OLD, BOOT_NEW, "boot.cpp (classpathJar)")


if __name__ == "__main__":
    sys.exit(main())
