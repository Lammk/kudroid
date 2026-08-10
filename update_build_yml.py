import re

with open('.github/workflows/build.yml', 'r') as f:
    content = f.read()

# 1. Add build-minijvm-rt job before ios-build
new_job = """
  build-minijvm-rt:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Set up JDK 8
        uses: actions/setup-java@v4
        with:
          java-version: '8'
          distribution: 'temurin'

      - name: Build minijvm_rt.jar
        run: |
          cd third_party/jvm/miniJVM/binary
          ./build_jar.sh

      - name: Upload minijvm_rt.jar artifact
        uses: actions/upload-artifact@v4
        with:
          name: minijvm-rt-jar
          path: third_party/jvm/miniJVM/binary/lib/minijvm_rt.jar

"""
content = content.replace("  ios-build:", new_job + "  ios-build:")

# 2. Update needs arrays for ios-build and macos-build
content = content.replace("needs: [build-multi-elf-so-arm64, build-gpu-test-so-arm64]", "needs: [build-multi-elf-so-arm64, build-gpu-test-so-arm64, build-minijvm-rt]")

# 3. Add minijvm_rt.jar download to ios-build
download_step = """
      - name: Download minijvm_rt.jar
        uses: actions/download-artifact@v4
        with:
          name: minijvm-rt-jar
          path: tests/
"""
content = content.replace("          ls -la tests/test_gpu_opengl_arm64.so\n", "          ls -la tests/test_gpu_opengl_arm64.so\n          ls -la tests/minijvm_rt.jar\n")
# Find the download step for gpu test and add minijvm_rt download after it
gpu_download = """
      - name: Download GPU Test ELFs
        uses: actions/download-artifact@v4
        with:
          name: gpu-test-arm64-so
          path: tests/
"""
content = content.replace(gpu_download, gpu_download + download_step)

# 4. Copy minijvm_rt.jar in ios-build
cp_step = """
          if [ -d third_party/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework ]; then
            cp -R third_party/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework KuDroidShell.app/Frameworks/
          fi
          
          # Copy minijvm_rt.jar
          cp tests/minijvm_rt.jar KuDroidShell.app/
"""
content = content.replace("""
          if [ -d third_party/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework ]; then
            cp -R third_party/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework KuDroidShell.app/Frameworks/
          fi
""", cp_step)

with open('.github/workflows/build.yml', 'w') as f:
    f.write(content)
