import re

with open('.github/workflows/build.yml', 'r') as f:
    content = f.read()

download_step = """
      - name: Download minijvm_rt.jar
        uses: actions/download-artifact@v4
        with:
          name: minijvm-rt-jar
          path: tests/
"""

# Insert download step in ios-build
if "Download minijvm_rt.jar" not in content.split("macos-build:")[0]:
    gpu_download = """
      - name: Download GPU Test ELFs
        uses: actions/download-artifact@v4
        with:
          name: gpu-test-arm64-so
          path: tests/
"""
    content = content.replace(gpu_download, gpu_download + download_step, 1)

# Update macos-build needs
content = content.replace("needs: [build-multi-elf-so-arm64, build-gpu-test-so-arm64]", "needs: [build-multi-elf-so-arm64, build-gpu-test-so-arm64, build-minijvm-rt]")

# Insert download step in macos-build
if "Download minijvm_rt.jar" not in content.split("macos-build:")[1]:
    content = content.replace(gpu_download, gpu_download + download_step, 2)

with open('.github/workflows/build.yml', 'w') as f:
    f.write(content)
