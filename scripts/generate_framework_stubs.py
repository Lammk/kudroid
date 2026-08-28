#!/usr/bin/env python3
"""Generate empty Java stubs for boot-classpath classes KuART could not resolve.

Input is the classes.log written by DexClassLinker::FindClass when it auto-stubs a
missing class. On device that file lives next to the app's other logs (pull it with
`tools/kdb/kdb.js dump classes.log`); the path can also be passed explicitly.

Log format, one line per distinct class:

    [2026-08-27 22:15:16] MISSING_FRAMEWORK_CLASS: android.util.Foo (descriptor: Landroid/util/Foo;)

A generated stub only makes the class resolvable — every method still has to be
filled in by hand. The point is to turn "app died on a missing class" into "app
runs and the missing behaviour is visible", one round-trip at a time.
"""
import argparse
import os
import re
import sys

# Packages KuDroid is responsible for shipping. Kept in sync with
# isBootClasspathDescriptor() in src/kuart/DexClassLinker.cpp — anything the linker
# will auto-stub is something this script may need to generate.
BOOT_PACKAGE_PREFIXES = (
    "android.",
    "androidx.",
    "java.",
    "javax.",
    "dalvik.",
    "sun.",
    "libcore.",
    "com.android.",
    "org.apache.harmony.",
    "org.w3c.dom.",
    "org.xml.sax.",
    "org.xmlpull.",
    "org.json.",
)

DEFAULT_LOG_PATHS = ("logs/classes.log", "classes.log")

# Both the current runtime format and the older "[n] [CLASS] name" format some
# saved logs still use.
LINE_PATTERNS = (
    re.compile(r"MISSING(?:_FRAMEWORK)?_CLASS:\s*([A-Za-z0-9_$.]+)"),
    re.compile(r"\[\d+\]\s+\[(?:CLASS|INTERFACE)\]\s+([A-Za-z0-9_$.]+)"),
)

# Descriptors ending in '$' + digits are anonymous classes; a stub cannot stand in
# for one because only the code that created it ever names it.
ANONYMOUS_INNER = re.compile(r"\$\d+$")


def find_log(explicit):
    if explicit:
        if not os.path.isfile(explicit):
            sys.exit(f"error: {explicit} not found")
        return explicit
    for candidate in DEFAULT_LOG_PATHS:
        if os.path.isfile(candidate):
            return candidate
    sys.exit(
        "error: no classes.log found. Looked for: "
        + ", ".join(DEFAULT_LOG_PATHS)
        + "\n       Pass a path explicitly, or pull one off a device with:\n"
        + "         node tools/kdb/kdb.js  then  dump classes.log"
    )


def parse_log(log_path):
    """Return class names in first-seen order, deduplicated."""
    names = []
    seen = set()
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            for pattern in LINE_PATTERNS:
                m = pattern.search(line)
                if not m:
                    continue
                name = m.group(1).strip().strip(".")
                if name and name not in seen:
                    seen.add(name)
                    names.append(name)
                break
    return names


def group_by_top_level(names, prefixes):
    """Group "pkg.Outer$Inner" entries under their top-level class."""
    groups = {}
    skipped = []
    for full_name in names:
        if not full_name.startswith(prefixes):
            skipped.append(full_name)
            continue
        if ANONYMOUS_INNER.search(full_name):
            skipped.append(full_name)
            continue

        parts = full_name.split("$")
        top_name = parts[0]
        if "." not in top_name:
            skipped.append(full_name)  # default package: no directory to place it in
            continue

        package, _, class_name = top_name.rpartition(".")
        entry = groups.setdefault(
            top_name, {"package": package, "name": class_name, "inners": []}
        )
        for inner in parts[1:]:
            if inner and inner not in entry["inners"]:
                entry["inners"].append(inner)
    return groups, skipped


def render_inner(name, indent="    "):
    return (
        f"{indent}public static class {name} {{\n"
        f"{indent}    public {name}() {{}}\n"
        f"{indent}}}\n"
    )


def render_stub(top_name, info):
    out = [f"package {info['package']};\n\n"]
    out.append(f"/** Auto-generated stub for {top_name} — methods still need filling in. */\n")
    out.append(f"public class {info['name']} {{\n")
    out.append(f"    public {info['name']}() {{}}\n")
    for inner in info["inners"]:
        out.append("\n")
        out.append(render_inner(inner))
    out.append("}\n")
    return "".join(out)


def add_missing_inners(file_path, info, dry_run):
    """Append inner classes that an existing file does not declare yet."""
    with open(file_path, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()

    missing = [
        inner
        for inner in info["inners"]
        if f"class {inner}" not in content and f"interface {inner}" not in content
    ]
    if not missing:
        return False

    last_brace = content.rfind("}")
    if last_brace == -1:
        print(f"  warn: {file_path} has no closing brace, skipped")
        return False

    addition = "\n" + "\n".join(render_inner(inner) for inner in missing)
    if dry_run:
        print(f"  would add to {file_path}: {', '.join(missing)}")
        return True

    with open(file_path, "w", encoding="utf-8") as f:
        f.write(content[:last_brace] + addition + content[last_brace:])
    print(f"  updated {file_path}: {', '.join(missing)}")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", nargs="?", help="path to classes.log")
    parser.add_argument("--framework-dir", default="framework",
                        help="root of the Java framework tree (default: framework)")
    parser.add_argument("--only", action="append", metavar="PREFIX",
                        help="restrict to these package prefixes, e.g. --only android. "
                             "(repeatable; default: every boot-classpath package)")
    parser.add_argument("--dry-run", action="store_true",
                        help="report what would change without writing files")
    args = parser.parse_args()

    log_path = find_log(args.log)
    framework_dir = os.path.abspath(args.framework_dir)
    if not os.path.isdir(framework_dir):
        sys.exit(f"error: framework dir not found: {framework_dir}")

    prefixes = tuple(args.only) if args.only else BOOT_PACKAGE_PREFIXES

    names = parse_log(log_path)
    if not names:
        print(f"No MISSING_FRAMEWORK_CLASS entries in {log_path} — nothing to do.")
        return

    groups, skipped = group_by_top_level(names, prefixes)
    print(f"{log_path}: {len(names)} missing classes, "
          f"{len(groups)} top-level stubs to consider, {len(skipped)} skipped")
    if skipped:
        preview = ", ".join(skipped[:5])
        more = f" (+{len(skipped) - 5} more)" if len(skipped) > 5 else ""
        print(f"  skipped (app package, anonymous, or default package): {preview}{more}")

    created = updated = 0
    for top_name, info in sorted(groups.items()):
        pkg_dir = os.path.join(framework_dir, info["package"].replace(".", os.sep))
        file_path = os.path.join(pkg_dir, f"{info['name']}.java")

        if os.path.isfile(file_path):
            if add_missing_inners(file_path, info, args.dry_run):
                updated += 1
            continue

        if args.dry_run:
            print(f"  would create {file_path}")
            created += 1
            continue

        os.makedirs(pkg_dir, exist_ok=True)
        with open(file_path, "w", encoding="utf-8") as f:
            f.write(render_stub(top_name, info))
        print(f"  created {file_path}")
        created += 1

    verb = "would create" if args.dry_run else "created"
    print(f"\n{verb} {created} stub file(s), updated {updated} existing file(s).")
    if created or updated:
        print("Next: run framework/build.sh to rebuild framework.dex, then fill in the "
              "method bodies that the app actually needs.")


if __name__ == "__main__":
    main()
