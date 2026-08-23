#!/usr/bin/env python3
import os
import re
import sys

def main():
    log_path = "logs/classes.log"
    if not os.path.isfile(log_path):
        print(f"Error: {log_path} not found")
        sys.exit(1)

    framework_dir = os.path.abspath("framework")

    with open(log_path, "r", encoding="utf-8") as f:
        lines = f.readlines()

    pattern = re.compile(r"\[\d+\]\s+\[(CLASS|INTERFACE)\]\s+([a-zA-Z0-9_$.]+)")

    entries = []
    for line in lines:
        m = pattern.search(line)
        if m:
            kind = m.group(1)
            full_name = m.group(2).strip()
            # Chỉ tạo stub cho android.* và androidx.*
            if full_name.startswith("android.") or full_name.startswith("androidx."):
                entries.append((kind, full_name))

    print(f"Filtered {len(entries)} android/androidx entries from {log_path}")

    top_level_map = {}

    for kind, full_name in entries:
        parts = full_name.split("$")
        top_name = parts[0]
        pkg_parts = top_name.split(".")
        pkg_name = ".".join(pkg_parts[:-1])
        class_name = pkg_parts[-1]

        if top_name not in top_level_map:
            top_level_map[top_name] = {
                'kind': 'CLASS',
                'package': pkg_name,
                'name': class_name,
                'inners': []
            }

        if len(parts) == 1:
            top_level_map[top_name]['kind'] = kind
        else:
            inner_name = parts[-1]
            existing_inners = [in_info['name'] for in_info in top_level_map[top_name]['inners']]
            if inner_name not in existing_inners:
                top_level_map[top_name]['inners'].append({
                    'kind': kind,
                    'name': inner_name
                })

    created_count = 0
    updated_count = 0

    for top_name, info in top_level_map.items():
        pkg_path = os.path.join(framework_dir, info['package'].replace(".", "/"))
        file_path = os.path.join(pkg_path, f"{info['name']}.java")

        if os.path.isfile(file_path):
            with open(file_path, "r", encoding="utf-8") as f:
                content = f.read()

            inners_to_add = []
            for inner in info['inners']:
                if f"class {inner['name']}" not in content and f"interface {inner['name']}" not in content:
                    inners_to_add.append(inner)

            if inners_to_add:
                last_brace_idx = content.rfind("}")
                if last_brace_idx != -1:
                    inner_code = "\n"
                    for inner in inners_to_add:
                        if inner['kind'] == 'INTERFACE':
                            inner_code += f"    public interface {inner['name']} {{\n    }}\n\n"
                        else:
                            inner_code += f"    public static class {inner['name']} {{\n        public {inner['name']}() {{}}\n    }}\n\n"

                    new_content = content[:last_brace_idx] + inner_code + content[last_brace_idx:]
                    with open(file_path, "w", encoding="utf-8") as f:
                        f.write(new_content)
                    updated_count += 1
            continue

        os.makedirs(pkg_path, exist_ok=True)
        java_code = f"package {info['package']};\n\n"
        java_code += f"/** Stub sinh tự động cho {top_name} */\n"
        
        if info['kind'] == 'INTERFACE':
            java_code += f"public interface {info['name']} {{\n"
        else:
            java_code += f"public class {info['name']} {{\n"
            java_code += f"    public {info['name']}() {{}}\n"

        for inner in info['inners']:
            if inner['kind'] == 'INTERFACE':
                java_code += f"\n    public interface {inner['name']} {{\n    }}\n"
            else:
                java_code += f"\n    public static class {inner['name']} {{\n        public {inner['name']}() {{}}\n    }}\n"

        java_code += "}\n"

        with open(file_path, "w", encoding="utf-8") as f:
            f.write(java_code)
        created_count += 1

    print(f"Successfully generated {created_count} new Java stub files and updated {updated_count} existing files.")

if __name__ == "__main__":
    main()
