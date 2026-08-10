#!/usr/bin/env python3
import os
import sys
import zipfile
import subprocess
import hashlib
import shutil

def get_file_hash(filepath):
    hasher = hashlib.md5()
    with open(filepath, 'rb') as f:
        buf = f.read()
        hasher.update(buf)
    return hasher.hexdigest()

def compile_dex_to_jar(apk_path, cache_dir):
    if not os.path.exists(apk_path):
        print(f"[-] APK not found: {apk_path}")
        return False
        
    os.makedirs(cache_dir, exist_ok=True)
    apk_hash = get_file_hash(apk_path)
    out_jar = os.path.join(cache_dir, f"{apk_hash}_classes.jar")
    
    if os.path.exists(out_jar):
        print(f"[+] Cache hit: {out_jar} already exists.")
        return True
        
    print(f"[*] Cache miss. Extracting DEX from {apk_path}...")
    temp_dir = os.path.join(cache_dir, f"tmp_{apk_hash}")
    os.makedirs(temp_dir, exist_ok=True)
    
    dex_files = []
    with zipfile.ZipFile(apk_path, 'r') as z:
        for info in z.infolist():
            if info.filename.endswith('.dex'):
                z.extract(info, temp_dir)
                dex_files.append(os.path.join(temp_dir, info.filename))
                
    if not dex_files:
        print("[-] No classes.dex found in APK.")
        shutil.rmtree(temp_dir)
        return False
        
    print(f"[*] Found {len(dex_files)} DEX files. Translating to JAR using d2j-dex2jar...")
    
    # Require d2j-dex2jar installed on the host system
    dex2jar_cmd = ["d2j-dex2jar", "-f", "-o", out_jar] + dex_files
    
    try:
        subprocess.run(dex2jar_cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        print(f"[+] Successfully compiled DEX to {out_jar}")
        shutil.rmtree(temp_dir)
        return True
    except subprocess.CalledProcessError as e:
        print(f"[-] Failed to translate DEX: {e.stderr.decode('utf-8')}")
        shutil.rmtree(temp_dir)
        return False

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 compile_dex_cache.py <path_to_apk> <cache_dir>")
        sys.exit(1)
        
    apk_path = sys.argv[1]
    cache_dir = sys.argv[2]
    compile_dex_to_jar(apk_path, cache_dir)
