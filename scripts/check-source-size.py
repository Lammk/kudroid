#!/usr/bin/env python3
"""Report owned source files exceeding the module size limit."""

from pathlib import Path
import subprocess


def main():
    root = Path(__file__).resolve().parents[1]
    sources = subprocess.check_output(
        ["git", "ls-files", "-c", "-o", "--exclude-standard", "-z"],
        cwd=root, timeout=20,
    ).decode().split("\0")
    extensions = {".c", ".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh", ".hxx",
                  ".inc", ".m", ".mm", ".java", ".kt", ".kts", ".swift",
                  ".py", ".sh", ".js", ".ts", ".tsx", ".jsx", ".mjs", ".cjs",
                  ".rs", ".go", ".dart", ".html", ".css", ".scss", ".sass", ".s", ".S"}
    failures = []
    for name in sorted(set(sources)):
        path = root / name
        if name.startswith(("third_party/", "node_modules/", ".planning/")):
            continue
        if path.suffix not in extensions or not path.is_file():
            continue
        count = len(path.read_bytes().splitlines())
        if count > 2000:
            failures.append((name, count))
    for name, count in failures:
        print(f"{name}: {count} lines (limit: 2000)")
    if failures:
        raise SystemExit(1)
    print("All owned source files are at most 2,000 lines.")


if __name__ == "__main__":
    main()
