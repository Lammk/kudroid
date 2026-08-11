#!/usr/bin/env python3
"""Patch Avian's makefile for iOS ARM64 builds.

Avian's `binaryToObject` converter emits an object with architecture 'unknown'
on iOS/Mach-O, which fails at link time. We replace the `classpath-object` rule
with a tiny assembly stub that embeds the classpath jar via `.incbin` and
exposes the exact symbols Avian's original boot.cpp already references
(`_binary_classpath_jar_start` / `_binary_classpath_jar_end`).

At the Mach-O asm level, the C symbol `_binary_classpath_jar_start` gains an
extra leading underscore, hence `__binary_classpath_jar_start` in the stub.
Because the symbol names are unchanged, boot.cpp needs no modification.

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
    "\t@echo \"creating $(@) via .incbin stub\"\n"
    "\t@printf '.global __binary_classpath_jar_start\\n"
    ".global __binary_classpath_jar_end\\n"
    ".section __DATA,__const\\n"
    ".balign 8\\n"
    "__binary_classpath_jar_start:\\n"
    ".incbin \"$(<)\"\\n"
    "__binary_classpath_jar_end:\\n' > $(build)/classpath-jar.s\n"
    "\t$(cc) $(asmflags) -c $(build)/classpath-jar.s -o $(@)"
)


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


if __name__ == "__main__":
    sys.exit(main())
