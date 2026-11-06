# ANGLE iOS builds from Linux

The source revisions and patch series are pinned in `source-lock.json`. The driver
uses an installed Clang toolchain and the Darwin SDK toolset linker, creates a local runtime layout,
and links device libraries without Xcode bundling or signing. It preserves the
shipping frameworks and stages validated candidates separately.

Build the Metal baseline first, using a GN binary compatible with the pinned source:

```sh
timeout 255 python3 scripts/build-angle-ios-linux.py \
  --gn .planning/vulkan-backend/gn-tool/gn \
  --backend metal --jobs 1 --timeout 240 \
  --stage .planning/vulkan-backend/candidate-metal
```

Exit 124 means the build slice expired. Run the same command to resume compilation.
Other nonzero exits require inspecting the reported error. A staging directory is
created only after both libraries link and pass their architecture, platform,
deployment-target and install-name checks.

Then build the Vulkan candidate:

```sh
timeout 255 python3 scripts/build-angle-ios-linux.py \
  --gn .planning/vulkan-backend/gn-tool/gn \
  --backend vulkan --jobs 1 --timeout 240 \
  --metal-baseline .planning/vulkan-backend/candidate-metal \
  --stage .planning/vulkan-backend/candidate-vulkan
```

The driver applies source patches only to the expected revisions. It never resets
the checkout. Candidate frameworks include source/build patches, configuration and
hash metadata. Do not replace the shipping libraries after a partial build.

Select the candidate host route with CMake's `-DKUDROID_GLES_BACKEND=vulkan`
or `scripts/build-local-ios.sh --gles-backend=vulkan`.
The ordinary build retains `metal` until device validation. Vulkan uses the bundled
MoltenVK framework directly and retains the supplied CAMetalLayer for the surface
lifetime. Explicit guest EGL platform/context attributes are preserved.

The startup probe records actual Vulkan features and attempts ESSL 310 graphics
and compute readbacks. Successful Linux probe tests validate the probe logic;
actual GLES support and surface presentation require the iOS candidate to run.
Geometry, provoking-vertex and other required features continue to limit the
backend's advertised GLES version.
