#!/usr/bin/env python3
"""Generate empty Java stubs for boot-classpath classes KuART auto-stubbed.

Input: classes.log from DexClassLinker::FindClass. A stub only makes the class
resolvable; methods still need filling in by hand.
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

# Missing FIELDS on classes that already exist. These cannot be stubbed (object
# layout is already fixed), so they must be written into the framework by hand.
#
# Format: MISSING_FRAMEWORK_FIELD: pkg.Class.name : Descriptor
FIELD_PATTERN = re.compile(
    r"MISSING_FRAMEWORK(?:_CLASS)?_FIELD:\s*([A-Za-z0-9_$.]+)\.([A-Za-z0-9_$]+)\s*:\s*(\S+)"
)

# Service names getSystemService() had no manager for. The class usually exists;
# only the Context mapping is absent, which is indistinguishable from the outside.
#
# Format: MISSING_SYSTEM_SERVICE: getSystemService("name") -> null
SERVICE_PATTERN = re.compile(r'MISSING_SYSTEM_SERVICE:\s*getSystemService\("([^"]+)"\)')

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


def parse_missing_fields(log_path):
    """Return {class_name: [(field, descriptor), ...]} in first-seen order."""
    fields = {}
    seen = set()
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = FIELD_PATTERN.search(line)
            if not m:
                continue
            cls, name, desc = (g.strip() for g in m.groups())
            key = (cls, name, desc)
            if key in seen:
                continue
            seen.add(key)
            fields.setdefault(cls, []).append((name, desc))
    return fields


def java_type_of(descriptor):
    """Descriptor -> Java source type, for the suggested field declaration."""
    arrays = 0
    while descriptor.startswith("["):
        arrays += 1
        descriptor = descriptor[1:]
    primitives = {
        "Z": "boolean", "B": "byte", "C": "char", "S": "short",
        "I": "int", "J": "long", "F": "float", "D": "double", "V": "void",
    }
    if descriptor in primitives:
        base = primitives[descriptor]
    elif descriptor.startswith("L") and descriptor.endswith(";"):
        base = descriptor[1:-1].replace("/", ".").replace("$", ".")
    else:
        base = descriptor
    return base + "[]" * arrays


def parse_missing_services(log_path):
    """Return service names getSystemService() answered null for, first-seen order."""
    names = []
    seen = set()
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = SERVICE_PATTERN.search(line)
            if not m:
                continue
            name = m.group(1).strip()
            if name and name not in seen:
                seen.add(name)
                names.append(name)
    return names


def report_missing_services(names):
    """Print the service names to wire up.

    Not automated for the same reason as fields: the branch has to construct the
    right manager, and roughly half of them are process singletons that apps compare
    by identity — handing out a fresh instance per call is a different bug, not a fix.
    """
    if not names:
        return
    print(f"\n{len(names)} system service(s) returned null.")
    print("The manager class often already exists under framework/ — only the mapping\n"
          "in Context.getSystemService is missing, which an app cannot tell apart from\n"
          "the class being absent.")
    for name in sorted(names):
        print(f'      if (name.equals("{name}")) return new /* manager */();')
    print("\nAdd the constant next to the others in framework/android/content/Context.java,\n"
          "then a branch in getSystemService. Managers apps compare by identity (the IME\n"
          "manager, for one) need a holder so every call returns the same instance.")


def report_missing_fields(fields):
    """Print the fields to add. Deliberately not automated.

    A field has to go into the right class with the right type, and the surrounding
    code often needs it initialised to something meaningful — Android's defaults are
    rarely zero. Generating a bare `public int x;` would compile and still be wrong,
    so this prints what to add and leaves the judgement to a person.
    """
    if not fields:
        return
    total = sum(len(v) for v in fields.values())
    print(f"\n{total} missing field(s) on {len(fields)} existing class(es).")
    print("These cannot be auto-stubbed: object layout is fixed once a class is "
          "linked, so\nthe field has to be written into the framework source by hand.")
    for cls in sorted(fields):
        print(f"\n  {cls}")
        for name, desc in sorted(fields[cls]):
            print(f"      public {java_type_of(desc)} {name};   // {desc}")


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
    fields = parse_missing_fields(log_path)
    services = parse_missing_services(log_path)

    if not names:
        # Still report the other two kinds. An early return here meant a log with no
        # missing CLASS entries printed "nothing to do" and dropped the missing fields
        # and services on the floor — which is the common case once the class set is
        # complete, and exactly the case that hid the IMM failure.
        print(f"No MISSING_FRAMEWORK_CLASS entries in {log_path}.")
        report_missing_fields(fields)
        report_missing_services(services)
        if not fields and not services:
            print("Nothing to do.")
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

    report_missing_fields(fields)
    report_missing_services(services)


if __name__ == "__main__":
    main()
