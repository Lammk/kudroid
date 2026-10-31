#!/usr/bin/env python3
"""Verify the Android font configuration and packaged system font resources."""

import argparse
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile


def verify(read):
    config = read("etc/fonts.xml")
    root = ET.fromstring(config)
    assert root.tag == "familyset", "invalid Android font configuration"
    families = root.findall("family")
    assert any(f.get("name") == "sans-serif" for f in families), "default family missing"
    for font in root.findall(".//font"):
        name = (font.text or "").strip()
        assert name and Path(name).name == name, "invalid font filename"
        data = read("fonts/" + name)
        assert data[:4] in (b"\x00\x01\x00\x00", b"OTTO"), "invalid font data"
    assert b"SIL OPEN FONT LICENSE" in read("NOTO_LICENSE.txt"), "font license missing"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ipa", nargs="?")
    parser.add_argument("--asset", type=Path, default=Path(__file__).resolve().parents[1]
                        / "ios-app/KuDroidShell/Resources/system")
    args = parser.parse_args()
    verify(lambda name: (args.asset / name).read_bytes())
    if args.ipa:
        with zipfile.ZipFile(args.ipa) as archive:
            suffix = ".app/system/etc/fonts.xml"
            matches = [name for name in archive.namelist() if name.endswith(suffix)]
            assert len(matches) == 1, "packaged font configuration missing or duplicated"
            prefix = matches[0][:-len("etc/fonts.xml")]
            verify(lambda name: archive.read(prefix + name))
            for relative in ("etc/fonts.xml", "fonts/NotoSans.ttf", "NOTO_LICENSE.txt"):
                assert archive.read(prefix + relative) == (args.asset / relative).read_bytes(), \
                    "packaged font asset differs: " + relative
    print("Verified Android system font resources" + (" in " + args.ipa if args.ipa else ""))


if __name__ == "__main__":
    main()
