#!/usr/bin/env python3
"""Generate src/platform/FrameworkResourcesData.inc from the AOSP framework resources.

Input : <aosp>/core/res/res/values/*.xml  (default configuration only)
Output: a read-only table of the framework's (package 0x01) scalar resources plus the
        name of every public resource, so that a reference such as @android:color/white
        and Resources.getIdentifier(name, type, "android") resolve without framework-res.

The input is AOSP (Apache-2.0, android-10.0.0_r47); the output is data derived from it.
Run with --aosp pointing at core/res/res (default: third_party/aosp/... in this repo).
Stdlib only, deterministic. `--verify` re-checks the public ids against api/current.txt.
"""
import argparse
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

TAG = "android-10.0.0_r47"

# Res_value data types (androidfw/ResourceTypes.h).
TYPE_NULL = 0x00
TYPE_REFERENCE = 0x01
TYPE_ATTRIBUTE = 0x02
TYPE_STRING = 0x03
TYPE_FLOAT = 0x04
TYPE_DIMENSION = 0x05
TYPE_FRACTION = 0x06
TYPE_INT_DEC = 0x10
TYPE_INT_HEX = 0x11
TYPE_INT_BOOLEAN = 0x12
TYPE_INT_COLOR_ARGB8 = 0x1C
TYPE_INT_COLOR_RGB8 = 0x1D
TYPE_INT_COLOR_ARGB4 = 0x1E
TYPE_INT_COLOR_RGB4 = 0x1F

# Frozen type ids of the public types (aapt2 assigns them in this order).
TYPE_IDS = {
    "attr": 0x01, "id": 0x02, "style": 0x03, "string": 0x04, "dimen": 0x05,
    "color": 0x06, "array": 0x07, "drawable": 0x08, "layout": 0x09, "anim": 0x0A,
    "animator": 0x0B, "interpolator": 0x0C, "mipmap": 0x0D, "integer": 0x0E,
    "transition": 0x0F, "raw": 0x10, "bool": 0x11,
}
# Non-public types that carry scalars; assigned after the public ones.
EXTRA_TYPE_IDS = {"fraction": 0x12}

SCALAR_FILES = re.compile(
    r"^(colors.*|dimens.*|integers|bools|config|config_material)\.xml$")
STRING_FILES = ("strings.xml", "donottranslate.xml", "donottranslate_material.xml")
STYLE_FILES = ("themes.xml", "themes_holo.xml", "themes_material.xml", "themes_device_defaults.xml",
               "styles.xml", "styles_holo.xml", "styles_material.xml", "styles_device_defaults.xml")

UNITS = {
    "px": (TYPE_DIMENSION, 0, 1.0), "dip": (TYPE_DIMENSION, 1, 1.0),
    "dp": (TYPE_DIMENSION, 1, 1.0), "sp": (TYPE_DIMENSION, 2, 1.0),
    "pt": (TYPE_DIMENSION, 3, 1.0), "in": (TYPE_DIMENSION, 4, 1.0),
    "mm": (TYPE_DIMENSION, 5, 1.0), "%": (TYPE_FRACTION, 0, 1.0 / 100),
    "%p": (TYPE_FRACTION, 1, 1.0 / 100),
}


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def parse_number_with_unit(text):
    """Port of ResTable::stringToFloat (androidfw/ResourceTypes.cpp)."""
    m = re.match(r"^([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)(.*)$", text.strip())
    if not m:
        return None
    number, rest = m.group(1), m.group(2).strip()
    f = f32(float(number))
    if rest == "":
        return TYPE_FLOAT, struct.unpack("<I", struct.pack("<f", f))[0]
    if rest not in UNITS:
        return None
    dtype, unit, scale = UNITS[rest]
    f = f32(f * f32(scale))
    neg = f < 0
    if neg:
        f = -f
    bits = int(f32(f * (1 << 23)) + 0.5) if f * (1 << 23) < 2**63 else 0
    if (bits & 0x7FFFFF) == 0:
        radix, shift = 0, 23
    elif (bits & 0xFFFFFFFFFF800000) == 0:
        radix, shift = 3, 0
    elif (bits & 0xFFFFFFFF80000000) == 0:
        radix, shift = 2, 8
    elif (bits & 0xFFFFFF8000000000) == 0:
        radix, shift = 1, 16
    else:
        radix, shift = 0, 23
    mantissa = (bits >> shift) & 0xFFFFFF
    if neg:
        mantissa = (-mantissa) & 0xFFFFFF
    return dtype, unit | (radix << 4) | (mantissa << 8)


def parse_color(text):
    t = text.strip()
    if not t.startswith("#"):
        return None
    h = t[1:]
    if not re.fullmatch(r"[0-9a-fA-F]+", h):
        return None
    v = int(h, 16)
    if len(h) == 8:
        return TYPE_INT_COLOR_ARGB8, v
    if len(h) == 6:
        return TYPE_INT_COLOR_RGB8, v
    if len(h) == 4:
        return TYPE_INT_COLOR_ARGB4, v
    if len(h) == 3:
        return TYPE_INT_COLOR_RGB4, v
    return None


def parse_int(text):
    t = text.strip()
    if re.fullmatch(r"-?\d+", t):
        v = int(t)
        if -(2**31) <= v < 2**31:
            return TYPE_INT_DEC, v & 0xFFFFFFFF
    if re.fullmatch(r"0[xX][0-9a-fA-F]+", t):
        v = int(t, 16)
        if v < 2**32:
            return TYPE_INT_HEX, v
    return None


def unescape_string(el):
    """Resource-string unescaping (aapt2 StringBuilder), without style spans."""
    raw = "".join(el.itertext())
    out, i, quoted = [], 0, False
    prev_space = True
    while i < len(raw):
        c = raw[i]
        if c == "\\" and i + 1 < len(raw):
            n = raw[i + 1]
            if n == "n":
                out.append("\n")
            elif n == "t":
                out.append("\t")
            elif n == "u" and i + 5 < len(raw) + 0 and re.fullmatch(r"[0-9a-fA-F]{4}", raw[i + 2:i + 6]):
                out.append(chr(int(raw[i + 2:i + 6], 16)))
                i += 4
            else:
                out.append(n)
            i += 2
            prev_space = False
            continue
        if c == '"':
            quoted = not quoted
            i += 1
            continue
        if not quoted and c.isspace():
            if not prev_space:
                out.append(" ")
            prev_space = True
            i += 1
            continue
        out.append(c)
        prev_space = False
        i += 1
    return "".join(out).strip()


def read_public(path):
    """[(type, name, id)] with explicit ids; <public-group> expands first-id + index."""
    root = ET.parse(path).getroot()
    entries = []
    for el in root:
        if el.tag == "public":
            if "id" not in el.attrib:
                raise SystemExit("public entry without id: %s" % el.attrib)
            entries.append((el.attrib["type"], el.attrib["name"], int(el.attrib["id"], 16)))
        elif el.tag == "public-group":
            first = int(el.attrib["first-id"], 16)
            for i, child in enumerate(el):
                if child.tag == "public":
                    entries.append((child.attrib["type"], child.attrib["name"], first + i))
    return entries


def read_values(dirpath, wanted_strings):
    """{(type, name): (kind, payload)} from the default-config values files."""
    raw = {}
    for fname in sorted(os.listdir(dirpath)):
        is_scalar = SCALAR_FILES.match(fname) is not None
        is_string = fname in STRING_FILES
        if not (is_scalar or is_string):
            continue
        root = ET.parse(os.path.join(dirpath, fname)).getroot()
        for el in root:
            tag = el.tag
            name = el.attrib.get("name")
            if tag == "item" and "type" in el.attrib:
                tag = el.attrib["type"]
                fmt = el.attrib.get("format")
                if tag == "dimen" and fmt == "float":
                    tag = "float-dimen"
                elif tag == "dimen" and fmt == "integer":
                    tag = "int-dimen"
            if tag in ("color", "dimen", "integer", "bool", "fraction", "float-dimen", "int-dimen"):
                if name is None:
                    continue
                kind = {"float-dimen": "dimen", "int-dimen": "dimen"}.get(tag, tag)
                key = (kind, name)
                if key not in raw:
                    raw[key] = (tag, (el.text or "").strip())
            elif tag == "string" and is_string and name in wanted_strings:
                key = ("string", name)
                if key not in raw and "translatable" not in el.attrib:
                    raw[key] = ("string", unescape_string(el))
    return raw


def convert(kind_tag, text, resolve):
    """(type, data) or None. `resolve(type, name)` maps a reference to its id or None."""
    if text.startswith("@") or text.startswith("?"):
        is_attr = text.startswith("?")
        body = text[1:].lstrip("*")
        if body in ("null", "empty"):
            return (TYPE_REFERENCE, 0) if body == "null" else None
        m = re.fullmatch(r"(?:android:)?([a-z]+)/(.+)", body)
        if not m:
            return None
        rid = resolve("attr" if is_attr else m.group(1), m.group(2))
        if rid is None:
            return None
        return (TYPE_ATTRIBUTE if is_attr else TYPE_REFERENCE), rid
    if kind_tag == "color":
        return parse_color(text)
    if kind_tag in ("dimen", "float-dimen", "fraction"):
        return parse_number_with_unit(text)
    if kind_tag in ("integer", "int-dimen"):
        return parse_int(text)
    if kind_tag == "bool":
        if text == "true":
            return TYPE_INT_BOOLEAN, 0xFFFFFFFF
        if text == "false":
            return TYPE_INT_BOOLEAN, 0
        return None
    return None


def read_attr_enums(dirpath):
    """{attr: (is_flag, {name: value})} for every enum/flag attr declared in attrs.xml."""
    result = {}
    root = ET.parse(os.path.join(dirpath, "attrs.xml")).getroot()
    for el in root.iter("attr"):
        name = el.attrib.get("name")
        kids = [k for k in el if k.tag in ("enum", "flag")]
        if not name or not kids or name in result:
            continue
        table = {}
        for k in kids:
            try:
                table[k.attrib["name"]] = int(k.attrib["value"], 0) & 0xFFFFFFFF
            except (KeyError, ValueError):
                pass
        result[name] = (kids[0].tag == "flag", table)
    return result


def style_name(ref):
    """Bare style name of `parent="..."`: @style/X, android:X and *android:X all map to X."""
    ref = ref.strip()
    if ref.startswith("@"):
        ref = ref[1:]
    ref = ref.lstrip("*")
    if ref.startswith("android:"):
        ref = ref[len("android:"):]
    if ref.startswith("style/"):
        ref = ref[len("style/"):]
    return ref


def read_styles(dirpath):
    """{name: (parent_name_or_None, [(item, text)])} from the default-config style files."""
    styles = {}
    for fname in STYLE_FILES:
        path = os.path.join(dirpath, fname)
        if not os.path.exists(path):
            continue
        for el in ET.parse(path).getroot():
            if el.tag != "style" or "name" not in el.attrib:
                continue
            name = el.attrib["name"]
            if "parent" in el.attrib:
                parent = style_name(el.attrib["parent"]) or None
            else:
                parent = name.rpartition(".")[0] or None
            items = [(i.attrib["name"], (i.text or "").strip())
                     for i in el if i.tag == "item" and "name" in i.attrib]
            if name in styles:
                items = styles[name][1] + items
            styles[name] = (parent, items)
    return styles


def convert_style_item(attr, text, enums, resolve):
    """(type, data) for a style item of `attr`, or None for values a table cannot hold."""
    if text.startswith("@") or text.startswith("?"):
        return convert(None, text, resolve)
    if attr in enums:
        is_flag, table = enums[attr]
        tokens = [t.strip() for t in text.split("|")] if is_flag else [text]
        if tokens and all(t in table for t in tokens):
            value = 0
            for t in tokens:
                value |= table[t]
            return (TYPE_INT_HEX if is_flag else TYPE_INT_DEC), value
    if text in ("true", "false"):
        return convert("bool", text, resolve)
    if text.startswith("#"):
        return parse_color(text)
    as_int = parse_int(text)
    if as_int is not None:
        return as_int
    return parse_number_with_unit(text)


def build(aosp):
    values_dir = os.path.join(aosp, "values")
    public = read_public(os.path.join(values_dir, "public.xml"))
    public_ids = {}
    max_entry = {}
    for t, n, rid in public:
        if (t, n) in public_ids:
            raise SystemExit("duplicate public %s/%s" % (t, n))
        public_ids[(t, n)] = rid
        max_entry[t] = max(max_entry.get(t, -1), rid & 0xFFFF)
    if len(set(public_ids.values())) != len(public_ids):
        raise SystemExit("duplicate public ids")

    public_string_names = {n for t, n, _ in public if t == "string"}
    raw = read_values(values_dir, public_string_names)

    # Private scalars get ids after the last public entry of their type, in name order.
    ids = dict(public_ids)
    type_id = dict(TYPE_IDS)
    type_id.update(EXTRA_TYPE_IDS)
    for kind in ("color", "dimen", "integer", "bool", "fraction"):
        names = sorted(n for (k, n) in raw if k == kind and (k, n) not in public_ids)
        nxt = max_entry.get(kind, -1) + 1
        for n in names:
            ids[(kind, n)] = (0x01 << 24) | (type_id[kind] << 16) | nxt
            nxt += 1

    # Private styles follow the last public style, in name order.
    style_defs = read_styles(values_dir)
    nxt = max_entry.get("style", -1) + 1
    for n in sorted(n for n in style_defs if ("style", n) not in ids):
        ids[("style", n)] = (0x01 << 24) | (type_id["style"] << 16) | nxt
        nxt += 1

    def resolve(t, n):
        return ids.get((t, n))

    scalars = {}
    strings = {}
    skipped = []
    for (kind, name), (tag, text) in raw.items():
        rid = ids.get((kind, name))
        if rid is None:
            continue
        if kind == "string":
            strings[rid] = text
            continue
        conv = convert(tag, text, resolve)
        if conv is None:
            skipped.append((kind, name, text))
            continue
        scalars[rid] = conv

    # A public id with no value in values/ is file-backed, an attr, an id or a style: it
    # still needs a name so identifiers and references resolve.
    names = {rid: (t, n) for (t, n), rid in ids.items()}

    # Styles: {id: (parent id, {attr id: (type, data)})}. Items on non-public attrs are
    # dropped. An item whose value a table cannot hold (strings, references to file-backed
    # resources) becomes TYPE_NULL instead: it must still hide the parent's value, or the
    # parent's now-wrong one would show through.
    enums = read_attr_enums(values_dir)
    resolvable = set(scalars) | set(strings) | {ids[("style", n)] for n in style_defs}
    styles = {}
    dropped = 0
    for sname, (parent, items) in style_defs.items():
        sid = ids[("style", sname)]
        parent_id = ids.get(("style", parent)) if parent else 0
        if parent and parent_id is None:
            print("style %s: unknown parent %s" % (sname, parent), file=sys.stderr)
            parent_id = 0
        attrs = {}
        for iname, text in items:
            aid = ids.get(("attr", iname[len("android:"):] if iname.startswith("android:") else iname))
            if not aid:
                dropped += 1
                continue
            conv = convert_style_item(iname.replace("android:", "", 1), text, enums, resolve)
            if conv is not None and conv[0] == TYPE_REFERENCE and conv[1] != 0 \
                    and conv[1] not in resolvable:
                conv = None
            if conv is None:
                conv = (TYPE_NULL, 0)
                dropped += 1
            attrs[aid] = conv
        styles[sid] = (parent_id, attrs)
    print("styles: %d, items masked or dropped: %d" % (len(styles), dropped), file=sys.stderr)
    return scalars, strings, names, skipped, public_ids, styles


def emit(path, scalars, strings, names, styles):
    blob = bytearray()
    offsets = {}

    def intern(s):
        if s not in offsets:
            offsets[s] = len(blob)
            blob.extend(s.encode("utf-8") + b"\0")
        return offsets[s]

    lines = []
    w = lines.append
    w("// Generated by scripts/gen_framework_resources.py -- do not edit.")
    w("// Source: AOSP %s, frameworks/base/core/res/res/values (Apache License 2.0)." % TAG)
    w("// Scalars: default-configuration color/dimen/integer/bool/fraction values.")
    w("// Names:   every public resource id plus the private scalar and style entries.")
    w("// Styles:  default-configuration framework themes/styles, public attrs only.")
    w("")
    w("struct FrameworkScalarEntry { uint32_t id; uint8_t type; uint32_t data; };")
    w("static const FrameworkScalarEntry kFrameworkScalars[] = {")
    for rid in sorted(scalars):
        t, d = scalars[rid]
        w("    {0x%08x, 0x%02x, 0x%08xu}," % (rid, t, d))
    w("};")
    w("")
    w("struct FrameworkStringEntry { uint32_t id; uint32_t offset; };")
    w("static const FrameworkStringEntry kFrameworkStrings[] = {")
    for rid in sorted(strings):
        w("    {0x%08x, %du}," % (rid, intern(strings[rid])))
    w("};")
    w("")
    w("struct FrameworkStyleEntry { uint32_t id; uint32_t parent; uint32_t first; uint32_t count; };")
    w("struct FrameworkStyleItem { uint32_t attr; uint8_t type; uint32_t data; };")
    w("static const FrameworkStyleItem kFrameworkStyleItems[] = {")
    entries = []
    first = 0
    for sid in sorted(styles):
        parent_id, attrs = styles[sid]
        entries.append((sid, parent_id, first, len(attrs)))
        for aid in sorted(attrs):
            t, d = attrs[aid]
            w("    {0x%08x, 0x%02x, 0x%08xu}," % (aid, t, d))
        first += len(attrs)
    w("    {0, 0, 0u},")
    w("};")
    w("static const FrameworkStyleEntry kFrameworkStyles[] = {")
    for sid, parent_id, first, count in entries:
        w("    {0x%08x, 0x%08x, %du, %du}," % (sid, parent_id, first, count))
    w("};")
    w("")
    w("struct FrameworkNameEntry { uint32_t id; uint32_t offset; };")
    w("static const FrameworkNameEntry kFrameworkNames[] = {")
    for rid in sorted(names):
        w("    {0x%08x, %du}," % (rid, intern(names[rid][1])))
    w("};")
    w("")
    w("static const char* const kFrameworkTypeNames[] = {")
    type_names = {v: k for k, v in list(TYPE_IDS.items()) + list(EXTRA_TYPE_IDS.items())}
    for i in range(0, max(type_names) + 1):
        w('    %s,' % ('"%s"' % type_names[i] if i in type_names else "nullptr"))
    w("};")
    w("")
    w("// Entry names and public strings, NUL separated.")
    w("static const char kFrameworkText[] =")
    chunk = bytes(blob)
    pos = 0
    while pos < len(chunk):
        piece = chunk[pos:pos + 100]
        pos += 100
        # A hex escape swallows following hex digits, so end the literal after each one.
        esc = "".join(
            '\\x%02x" "' % b if (b < 0x20 or b >= 0x7F or chr(b) in '"\\?') else chr(b)
            for b in piece)
        w('    "%s"' % esc)
    w("    ;")
    w("")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def verify(aosp, public_ids):
    base = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(aosp))))
    api = os.path.join(base, "api", "current.txt")
    if not os.path.exists(api):
        print("verify: %s not found, skipped" % api)
        return
    seen = {}
    cls = None
    for line in open(api, encoding="utf-8"):
        m = re.match(r"\s*public static final class (\w+) \{", line)
        if m and line.strip().startswith("public static final class"):
            cls = m.group(1)
        m = re.match(r"\s*field public static final int (\w+) = (\d+);", line)
        if m and cls:
            seen[(cls, m.group(1))] = int(m.group(2))
    mismatches = 0
    for (t, n), rid in public_ids.items():
        api_val = seen.get((t, n.replace(".", "_")))
        if api_val is not None and api_val != rid:
            mismatches += 1
            print("MISMATCH %s/%s public=0x%08x api=0x%08x" % (t, n, rid, api_val))
    print("verify: %d public ids checked, %d mismatches" % (len(public_ids), mismatches))
    if mismatches:
        sys.exit(1)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--aosp", default=os.path.join(
        root, "third_party/aosp/platform_frameworks_base/core/res/res"))
    ap.add_argument("--out", default=os.path.join(root, "src/platform/FrameworkResourcesData.inc"))
    ap.add_argument("--verify", action="store_true")
    args = ap.parse_args()
    scalars, strings, names, skipped, public_ids, styles = build(args.aosp)
    for kind, name, text in sorted(skipped):
        print("skipped %s/%s = %r" % (kind, name, text), file=sys.stderr)
    emit(args.out, scalars, strings, names, styles)
    print("wrote %s: %d scalars, %d strings, %d names, %d styles" %
          (args.out, len(scalars), len(strings), len(names), len(styles)))
    if args.verify:
        verify(args.aosp, public_ids)


if __name__ == "__main__":
    main()
