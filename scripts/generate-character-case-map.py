#!/usr/bin/env python3
"""Generate Unicode 13.0 simple case mappings for java.lang.Character."""
import argparse
import hashlib
from pathlib import Path


def ranges(data, column):
    result = []
    for line in data.splitlines():
        fields = line.split(';')
        if not fields[column]:
            continue
        codepoint, mapped = int(fields[0], 16), int(fields[column], 16)
        delta = mapped - codepoint
        if result and result[-1][1] + 1 == codepoint and result[-1][2] == delta:
            result[-1][1] = codepoint
        else:
            result.append([codepoint, codepoint, delta])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('unicode_data', type=Path)
    parser.add_argument('--output', type=Path, default=Path('framework/java/lang/CharacterCaseMap.java'))
    args = parser.parse_args()
    source = args.unicode_data.read_bytes()
    digest = hashlib.sha256(source).hexdigest()
    # Reject a different database to keep generated mappings reproducible.
    expected = 'bdbffbbfc8ad4d3a6d01b5891510458f3d36f7170422af4ea2bed3211a73e8bb'
    if digest != expected:
        raise SystemExit(f'Expected UnicodeData 13.0.0 ({expected}), got {digest}')
    code = ['package java.lang;', '', '// Generated Unicode 13.0 simple case maps; see scripts/generate-character-case-map.py.',
            'final class CharacterCaseMap {', '    private CharacterCaseMap() {}']
    for name, column in [('UPPER', 12), ('LOWER', 13)]:
        code.append(f'    private static final int[] {name} = {{')
        for start, end, delta in ranges(source.decode('utf-8'), column):
            code.append(f'        0x{start:X}, 0x{end:X}, {delta},')
        code.append('    };')
    code += [
        '    static int upper(int c) { return map(c, UPPER); }',
        '    static int lower(int c) { return map(c, LOWER); }',
        '    private static int map(int c, int[] ranges) {',
        '        int low = 0, high = ranges.length / 3 - 1;',
        '        while (low <= high) {',
        '            int mid = (low + high) >>> 1, at = mid * 3;',
        '            if (c < ranges[at]) high = mid - 1;',
        '            else if (c > ranges[at + 1]) low = mid + 1;',
        '            else return c + ranges[at + 2];',
        '        }',
        '        return c;',
        '    }',
        '}',
    ]
    args.output.write_text('\n'.join(code) + '\n')


if __name__ == '__main__':
    main()
