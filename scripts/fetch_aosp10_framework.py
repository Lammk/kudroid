#!/usr/bin/env python3
"""
Fetch core Java framework sources from official AOSP Android 10 (android-10.0.0_r47) repository.
Cleans internal annotations (@UnsupportedAppUsage, @hide, etc.) so standard javac can compile seamlessly for KuART.
"""

import base64
import os
import re
import sys
import urllib.request

GOOG_BASE = "https://android.googlesource.com/platform/frameworks/base/+/refs/tags/android-10.0.0_r47/"
GOOG_LIBCORE = "https://android.googlesource.com/platform/libcore/+/refs/tags/android-10.0.0_r47/"

FILES_TO_FETCH = [
    # android.util
    ("android/util/SparseArray.java", GOOG_BASE + "core/java/android/util/SparseArray.java?format=TEXT"),
    ("android/util/ArrayMap.java", GOOG_BASE + "core/java/android/util/ArrayMap.java?format=TEXT"),
    ("android/util/ArraySet.java", GOOG_BASE + "core/java/android/util/ArraySet.java?format=TEXT"),
    ("android/util/LongSparseArray.java", GOOG_BASE + "core/java/android/util/LongSparseArray.java?format=TEXT"),
    ("android/util/SparseIntArray.java", GOOG_BASE + "core/java/android/util/SparseIntArray.java?format=TEXT"),
    ("android/util/SparseBooleanArray.java", GOOG_BASE + "core/java/android/util/SparseBooleanArray.java?format=TEXT"),
    ("android/util/SparseLongArray.java", GOOG_BASE + "core/java/android/util/SparseLongArray.java?format=TEXT"),
    ("android/util/Base64.java", GOOG_BASE + "core/java/android/util/Base64.java?format=TEXT"),
    ("android/util/DisplayMetrics.java", GOOG_BASE + "core/java/android/util/DisplayMetrics.java?format=TEXT"),
    ("android/util/Pair.java", GOOG_BASE + "core/java/android/util/Pair.java?format=TEXT"),
    ("android/util/Size.java", GOOG_BASE + "core/java/android/util/Size.java?format=TEXT"),
    ("android/util/SizeF.java", GOOG_BASE + "core/java/android/util/SizeF.java?format=TEXT"),
    ("android/util/TypedValue.java", GOOG_BASE + "core/java/android/util/TypedValue.java?format=TEXT"),
    ("android/util/LruCache.java", GOOG_BASE + "core/java/android/util/LruCache.java?format=TEXT"),
    ("android/util/Range.java", GOOG_BASE + "core/java/android/util/Range.java?format=TEXT"),
    ("android/util/Rational.java", GOOG_BASE + "core/java/android/util/Rational.java?format=TEXT"),
    ("android/util/Half.java", GOOG_BASE + "core/java/android/util/Half.java?format=TEXT"),
    ("android/util/AtomicFile.java", GOOG_BASE + "core/java/android/util/AtomicFile.java?format=TEXT"),

    # android.graphics math
    ("android/graphics/Rect.java", GOOG_BASE + "graphics/java/android/graphics/Rect.java?format=TEXT"),
    ("android/graphics/RectF.java", GOOG_BASE + "graphics/java/android/graphics/RectF.java?format=TEXT"),
    ("android/graphics/Point.java", GOOG_BASE + "graphics/java/android/graphics/Point.java?format=TEXT"),
    ("android/graphics/PointF.java", GOOG_BASE + "graphics/java/android/graphics/PointF.java?format=TEXT"),
    ("android/graphics/Insets.java", GOOG_BASE + "graphics/java/android/graphics/Insets.java?format=TEXT"),

    # android.os helpers
    ("android/os/CancellationSignal.java", GOOG_BASE + "core/java/android/os/CancellationSignal.java?format=TEXT"),
    ("android/os/OperationCanceledException.java", GOOG_BASE + "core/java/android/os/OperationCanceledException.java?format=TEXT"),
    ("android/os/StatFs.java", GOOG_BASE + "core/java/android/os/StatFs.java?format=TEXT"),

    # java.util & math from libcore ojluni
    ("java/util/BitSet.java", GOOG_LIBCORE + "ojluni/src/main/java/java/util/BitSet.java?format=TEXT"),
    ("java/util/PriorityQueue.java", GOOG_LIBCORE + "ojluni/src/main/java/java/util/PriorityQueue.java?format=TEXT"),
    ("java/util/StringJoiner.java", GOOG_LIBCORE + "ojluni/src/main/java/java/util/StringJoiner.java?format=TEXT"),
    ("java/util/Optional.java", GOOG_LIBCORE + "ojluni/src/main/java/java/util/Optional.java?format=TEXT"),
    ("java/util/OptionalInt.java", GOOG_LIBCORE + "ojluni/src/main/java/java/util/OptionalInt.java?format=TEXT"),
    ("java/util/OptionalLong.java", GOOG_LIBCORE + "ojluni/src/main/java/java/util/OptionalLong.java?format=TEXT"),
    ("java/util/OptionalDouble.java", GOOG_LIBCORE + "ojluni/src/main/java/java/util/OptionalDouble.java?format=TEXT"),
    ("java/math/RoundingMode.java", GOOG_LIBCORE + "ojluni/src/main/java/java/math/RoundingMode.java?format=TEXT"),
    ("java/math/MathContext.java", GOOG_LIBCORE + "ojluni/src/main/java/java/math/MathContext.java?format=TEXT"),
    ("java/math/BigDecimal.java", GOOG_LIBCORE + "ojluni/src/main/java/java/math/BigDecimal.java?format=TEXT"),
    ("java/math/BigInteger.java", GOOG_LIBCORE + "ojluni/src/main/java/java/math/BigInteger.java?format=TEXT"),
]

def clean_source(code: str) -> str:
    # Remove unsupported annotations that prevent standard javac compilation
    code = re.sub(r'@UnsupportedAppUsage(\([^)]*\))?', '', code)
    code = re.sub(r'@libcore\.util\.NonNull', '', code)
    code = re.sub(r'@libcore\.util\.Nullable', '', code)
    code = re.sub(r'@NonNull', '', code)
    code = re.sub(r'@Nullable', '', code)
    code = re.sub(r'@SystemApi(\([^)]*\))?', '', code)
    code = re.sub(r'@TestApi(\([^)]*\))?', '', code)
    code = re.sub(r'@Widget', '', code)
    code = re.sub(r'@CriticalNative', '', code)
    code = re.sub(r'@FastNative', '', code)
    code = re.sub(r'import\s+annotation\.compat\.UnsupportedAppUsage;', '', code)
    code = re.sub(r'import\s+dalvik\.annotation\.compat\.UnsupportedAppUsage;', '', code)
    code = re.sub(r'import\s+libcore\.util\.NonNull;', '', code)
    code = re.sub(r'import\s+libcore\.util\.Nullable;', '', code)
    code = re.sub(r'import\s+android\.annotation\.NonNull;', '', code)
    code = re.sub(r'import\s+android\.annotation\.Nullable;', '', code)
    code = re.sub(r'import\s+android\.annotation\.SystemApi;', '', code)
    code = re.sub(r'import\s+android\.annotation\.TestApi;', '', code)
    code = re.sub(r'import\s+android\.annotation\.UnsupportedAppUsage;', '', code)
    return code

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    framework_dir = os.path.join(root, "framework")
    
    print(f"[*] Fetching AOSP 10 (android-10.0.0_r47) Java files into {framework_dir}...")
    success_count = 0
    
    for rel_path, url in FILES_TO_FETCH:
        dest_path = os.path.join(framework_dir, rel_path)
        os.makedirs(os.path.dirname(dest_path), exist_ok=True)
        print(f" -> Downloading {rel_path}...")
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req, timeout=15) as response:
                raw_data = response.read()
                # If format=TEXT from googlesource, decode base64
                if url.endswith("?format=TEXT"):
                    content = base64.b64decode(raw_data).decode('utf-8', errors='replace')
                else:
                    content = raw_data.decode('utf-8', errors='replace')
                cleaned = clean_source(content)
                with open(dest_path, 'w', encoding='utf-8') as f:
                    f.write(cleaned)
                success_count += 1
        except Exception as e:
            print(f" [!] Error fetching {rel_path}: {e}")

    print(f"[+] Finished: successfully fetched {success_count}/{len(FILES_TO_FETCH)} files.")

if __name__ == "__main__":
    main()
