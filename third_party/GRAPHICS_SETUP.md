# KuDroid ANGLE + MoltenVK Setup

KuDroid's graphics stack must not link OpenGLES.framework or GLKit.framework.
The target pipeline is ANGLE EGL/GLES -> Vulkan -> MoltenVK -> Metal.

## MoltenVK

The official iOS archive is reproducibly installed with:

```bash
./scripts/fetch-moltenvk-ios.sh
```

Expected files:

```text
third_party/MoltenVK/MoltenVK/include/vulkan/
third_party/MoltenVK/MoltenVK/include/MoltenVK/
third_party/MoltenVK/MoltenVK/static/MoltenVK.xcframework/ios-arm64/libMoltenVK.a
```

## ANGLE

Google does not publish an official prebuilt iOS libEGL/libGLESv2 release.
Build the official source repository on macOS with Xcode:

```bash
./scripts/fetch-build-angle-ios.sh
```

Source repository:

```text
https://github.com/google/angle.git
```

The script installs depot_tools, bootstraps ANGLE dependencies, and builds an
iPhoneOS ARM64 static stack configured for ANGLE's Vulkan backend. Expected files:

```text
third_party/ANGLE/include/EGL/
third_party/ANGLE/include/GLES2/
third_party/ANGLE/include/GLES3/
third_party/ANGLE/lib/ios-arm64/libEGL.a
third_party/ANGLE/lib/ios-arm64/libGLESv2.a
```

The ANGLE source checkout and build tools are intentionally not committed:

```text
third_party/angle-src/
third_party/build-tools/
```

After both stacks exist, configure the graphics shim/CMake integration to link
ANGLE, MoltenVK, Metal.framework, QuartzCore.framework, Foundation.framework, and
UIKit.framework. Do not add OpenGLES.framework or GLKit.framework.
