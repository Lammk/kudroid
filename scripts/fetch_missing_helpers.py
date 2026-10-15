#!/usr/bin/env python3
import base64
import os
import re
import urllib.request

GOOG_BASE = "https://android.googlesource.com/platform/frameworks/base/+/refs/tags/android-10.0.0_r47/"
GOOG_LIBCORE = "https://android.googlesource.com/platform/libcore/+/refs/tags/android-10.0.0_r47/"

EXTRA_FILES = [
    ("libcore/util/EmptyArray.java", GOOG_LIBCORE + "luni/src/main/java/libcore/util/EmptyArray.java?format=TEXT"),
    ("com/android/internal/util/ArrayUtils.java", GOOG_BASE + "core/java/com/android/internal/util/ArrayUtils.java?format=TEXT"),
    ("com/android/internal/util/GrowingArrayUtils.java", GOOG_BASE + "core/java/com/android/internal/util/GrowingArrayUtils.java?format=TEXT"),
]

root = '/home/kuzei/Documents/Kudroid/framework'

for rel, url in EXTRA_FILES:
    dest = os.path.join(root, rel)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    try:
        with urllib.request.urlopen(req, timeout=15) as res:
            content = base64.b64decode(res.read()).decode('utf-8', errors='replace')
            content = re.sub(r'@UnsupportedAppUsage(\([^)]*\))?', '', content)
            content = re.sub(r'@libcore\.util\.NonNull', '', content)
            content = re.sub(r'@libcore\.util\.Nullable', '', content)
            content = re.sub(r'@NonNull', '', content)
            content = re.sub(r'@Nullable', '', content)
            content = re.sub(r'@SystemApi(\([^)]*\))?', '', content)
            content = re.sub(r'@TestApi(\([^)]*\))?', '', content)
            content = re.sub(r'import\s+annotation\.compat\.UnsupportedAppUsage;', '', content)
            content = re.sub(r'import\s+dalvik\.annotation\.compat\.UnsupportedAppUsage;', '', content)
            content = re.sub(r'import\s+libcore\.util\.NonNull;', '', content)
            content = re.sub(r'import\s+libcore\.util\.Nullable;', '', content)
            content = re.sub(r'import\s+android\.annotation\.NonNull;', '', content)
            content = re.sub(r'import\s+android\.annotation\.Nullable;', '', content)
            content = re.sub(r'import\s+android\.annotation\.SystemApi;', '', content)
            content = re.sub(r'import\s+android\.annotation\.TestApi;', '', content)
            content = re.sub(r'import\s+android\.annotation\.UnsupportedAppUsage;', '', content)
            with open(dest, 'w', encoding='utf-8') as f:
                f.write(content)
            print(f"OK {rel}")
    except Exception as e:
        print(f"FAIL {rel}: {e}")
