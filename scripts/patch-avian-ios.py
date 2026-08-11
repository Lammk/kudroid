#!/usr/bin/env python3
"""Patch Avian's makefile for iOS ARM64 builds.

Avian's `binaryToObject` converter emits an object with architecture 'unknown'
on iOS/Mach-O, which fails at link time. It is used to embed two jars
(classpath.jar and javahome.jar) as byte arrays. We replace both converter
rules with a tiny assembly stub that embeds the jar via `.incbin` and exposes
the same symbols Avian already references
(`_binary_<name>_jar_start` / `_binary_<name>_jar_end`).

At the Mach-O asm level, a C symbol like `_binary_classpath_jar_start` gains an
extra leading underscore, hence `__binary_..._jar_start` in the stub. Because
the symbol names are unchanged, no C++ source needs modification. Using
assembly labels (rather than `ld -sectcreate`) also avoids Mach-O's 16-char
section-name limit.

Run from the Avian source directory (third_party/jvm/avian).
"""
import sys


def converter_rule(target_var, jar_var, sym):
    """Original converter-based rule text for a given jar/symbol."""
    return (
        f"{target_var}: {jar_var} $(converter)\n"
        "\t@echo \"creating $(@)\"\n"
        f"\t$(converter) $(<) $(@) _binary_{sym}_jar_start \\\n"
        f"\t\t_binary_{sym}_jar_end $(target-format) $(arch)"
    )


def incbin_rule(target_var, jar_var, sym):
    """Replacement rule that embeds the jar via a .incbin assembly stub."""
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

# Patch the classpath.jar rule to also merge the KuDroid framework classes
# (framework/build/framework.jar) into the jar, so android.* classes are on
# the boot classpath at runtime.
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

    with open("makefile", "w") as f:
        f.write(content)


if __name__ == "__main__":
    sys.exit(main())
