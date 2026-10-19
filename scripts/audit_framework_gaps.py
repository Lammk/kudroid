#!/usr/bin/env python3
"""Report boot-classpath classes/members that app libraries reference but the framework lacks.

Scans .class files (jars, or AARs' classes.jar) for method/field references into boot
packages and resolves each against the framework's compiled classes, following the
superclass/interface chain. Optionally classifies every miss against an AOSP source tree
so that API added after the framework's SDK level can be told apart from real gaps.

usage: audit_framework_gaps.py --classes framework/build/classes [--aosp DIR]... LIB...
"""
import argparse
import collections
import io
import os
import re
import struct
import sys
import zipfile

BOOT_PREFIXES = ('android/', 'java/', 'javax/', 'dalvik/', 'libcore/', 'sun/', 'org/xmlpull/',
                 'org/json/', 'org/w3c/', 'org/xml/', 'com/android/')


class ClassFile:
    __slots__ = ('name', 'super', 'interfaces', 'methods', 'fields', 'refs')

    def __init__(self):
        self.name = ''
        self.super = None
        self.interfaces = []
        self.methods = set()
        self.fields = set()
        self.refs = []  # (kind, owner, name, descriptor)


def parse_class(data):
    if data[:4] != b'\xca\xfe\xba\xbe':
        return None
    count = struct.unpack_from('>H', data, 8)[0]
    pos = 10
    pool = [None] * count
    i = 1
    while i < count:
        tag = data[pos]
        if tag == 1:
            n = struct.unpack_from('>H', data, pos + 1)[0]
            pool[i] = (1, data[pos + 3:pos + 3 + n].decode('utf-8', 'replace'))
            pos += 3 + n
        elif tag in (3, 4):
            pos += 5
        elif tag in (5, 6):
            pos += 9
            i += 1
        elif tag in (7, 8, 16, 19, 20):
            pool[i] = (tag, struct.unpack_from('>H', data, pos + 1)[0])
            pos += 3
        elif tag in (9, 10, 11, 12, 17, 18):
            pool[i] = (tag, struct.unpack_from('>HH', data, pos + 1))
            pos += 5
        elif tag == 15:
            pos += 4
        else:
            return None
        i += 1

    def utf(idx):
        return pool[idx][1]

    def cls(idx):
        return utf(pool[idx][1])

    cf = ClassFile()
    pos += 2
    this_idx, super_idx, iface_count = struct.unpack_from('>HHH', data, pos)
    pos += 6
    cf.name = cls(this_idx)
    cf.super = cls(super_idx) if super_idx else None
    for _ in range(iface_count):
        cf.interfaces.append(cls(struct.unpack_from('>H', data, pos)[0]))
        pos += 2
    for member_set in (cf.fields, cf.methods):
        n = struct.unpack_from('>H', data, pos)[0]
        pos += 2
        for _ in range(n):
            _, name_idx, desc_idx, attr_count = struct.unpack_from('>HHHH', data, pos)
            pos += 8
            member_set.add((utf(name_idx), utf(desc_idx)))
            for _ in range(attr_count):
                length = struct.unpack_from('>I', data, pos + 2)[0]
                pos += 6 + length
    for entry in pool:
        if entry and entry[0] in (9, 10, 11):
            class_idx, nat_idx = entry[1]
            name_idx, desc_idx = pool[nat_idx][1]
            kind = {9: 'field', 10: 'method', 11: 'method'}[entry[0]]
            cf.refs.append((kind, cls(class_idx), utf(name_idx), utf(desc_idx)))
    return cf


def load_framework(classes_dir):
    index = {}
    for root, _, files in os.walk(classes_dir):
        for f in files:
            if f.endswith('.class'):
                with open(os.path.join(root, f), 'rb') as fh:
                    cf = parse_class(fh.read())
                if cf:
                    index[cf.name] = cf
    return index


def class_blobs(path):
    """Yield raw class bytes from a jar, or from the classes.jar inside an aar."""
    with zipfile.ZipFile(path) as z:
        names = z.namelist()
        if 'classes.jar' in names:
            with zipfile.ZipFile(io.BytesIO(z.read('classes.jar'))) as inner:
                for n in inner.namelist():
                    if n.endswith('.class'):
                        yield inner.read(n)
        for n in names:
            if n.endswith('.class'):
                yield z.read(n)


def resolve(index, owner, name, desc, kind, seen=None):
    cf = index.get(owner)
    if cf is None:
        return False
    seen = seen if seen is not None else set()
    if owner in seen:
        return False
    seen.add(owner)
    members = cf.methods if kind == 'method' else cf.fields
    if (name, desc) in members:
        return True
    parents = ([cf.super] if cf.super else []) + cf.interfaces
    return any(resolve(index, p, name, desc, kind, seen) for p in parents)


def aosp_has(roots, owner, name):
    rel = owner.split('$')[0] + '.java'
    for root in roots:
        path = os.path.join(root, rel)
        if os.path.exists(path):
            with open(path, encoding='utf-8', errors='replace') as f:
                text = f.read()
            if name in ('<init>', '<clinit>'):
                return True
            return re.search(r'\b' + re.escape(name) + r'\b', text) is not None
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--classes', required=True)
    ap.add_argument('--aosp', action='append', default=[])
    ap.add_argument('--show-late', action='store_true', help='also list members AOSP lacks')
    ap.add_argument('libs', nargs='+')
    args = ap.parse_args()

    index = load_framework(args.classes)
    refs = collections.defaultdict(set)  # (kind, owner, name, desc) -> libs
    for lib in args.libs:
        for blob in class_blobs(lib):
            cf = parse_class(blob)
            if not cf:
                continue
            for kind, owner, name, desc in cf.refs:
                if owner.startswith('[') or not owner.startswith(BOOT_PREFIXES):
                    continue
                refs[(kind, owner, name, desc)].add(os.path.basename(lib))

    missing_classes = collections.defaultdict(set)
    missing_members = []
    for (kind, owner, name, desc), libs in sorted(refs.items()):
        if owner not in index:
            missing_classes[owner].add(name)
            continue
        if not resolve(index, owner, name, desc, kind):
            missing_members.append((kind, owner, name, desc, libs))

    print('== missing classes ==')
    for owner in sorted(missing_classes):
        state = aosp_has(args.aosp, owner, '<init>') if args.aosp else None
        tag = {True: 'aosp', False: '?', None: 'no-src'}[state is True if state is not None else None]
        print(f'{owner}  [{tag}] uses={len(missing_classes[owner])}')
    print('== missing members ==')
    late = 0
    for kind, owner, name, desc, libs in missing_members:
        known = aosp_has(args.aosp, owner, name) if args.aosp else None
        if known is False and not args.show_late:
            late += 1
            continue
        print(f'{owner}.{name}{desc if kind == "method" else ""} [{kind}] <- {",".join(sorted(libs))[:60]}')
    print(f'# classes missing: {len(missing_classes)}, members missing: {len(missing_members)}'
          f' (not in AOSP source, hidden: {late})', file=sys.stderr)


if __name__ == '__main__':
    main()
