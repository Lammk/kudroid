# Vulkan compatibility fix

## Evidence and design

The captured Vulkan driver accepts swapchain creation, submission and presentation. Vulkan currently presents into the view backing layer, while the working EGL path uses a dedicated Metal child layer. Use the existing child-layer geometry and main-thread lifecycle machinery for a separate Vulkan child; retain it across swapchain replacement and synchronize it when the host changes size. Record successful Vulkan presents in the frame watchdog.

The crash stack reaches host syslog with guest AAPCS64 variadic arguments. Add register-capture and guest-va_list syslog bridges using the existing formatter and log pipeline, preserving errno.

Before the formatting crash, the log reports a pure-virtual call from a native frame callback during pacing-object replacement. ALooper is currently process-wide, and the timer can also execute native callbacks itself. This permits callbacks to outlive the owner thread and provides a mechanism for the observed failure. Give each thread its own native looper and keep native Choreographer callbacks on that owner thread. Retire its callbacks when the thread exits. Java frame delivery keeps its existing timer integration.

## Constraints

- Generic platform and ABI behavior only; no package names, per-library offsets or signal skipping.
- Retain the existing EGL behavior and lazy native looper creation needed by Java/native integration.
- All builds and tests have a timeout.

## Implementation

- [x] Add guest syslog/vsyslog bridges and an arm64 register-capture regression.
- [x] Implement per-thread ALooper ownership, safe wake lifetime, standard poll results and callback registration identity.
- [x] Keep native frame callbacks on their owning looper and clear them on owner exit; cover replacement/retirement and slow owner polling.
- [x] Generalize the existing Metal child-layer helpers for independent Vulkan layers, resize/rebind and teardown.
- [x] Run bounded host regressions, arm64 varargs checks and an iOS core build. Device rendering requires a subsequent device run.

## Validation

- Native looper/callback lifetime and syslog: 14 checks passed, including 100 thread replacements.
- Frame pacer: 57 checks passed; Java Choreographer: 40 checks passed.
- Existing EGL ownership: 8 checks passed.
- Syscall suite: 274 checks passed outside the sandbox. Sandboxed execution blocks getifaddrs and process_vm_readv cases.
- Freestanding arm64 varargs harness passed, including syslog integer, string, floating-point and stack-overflow arguments.
- iOS arm64 core compiled; debug IPA packaged at ios-app/KuDroidShell/xtool/KuDroidShell.ipa.
- Actual Vulkan pixels and crash disappearance still require validation on the device.

## Follow-up: presentation clock

The 19:11 capture no longer crashes, but remains black. The child layer is attached correctly and hundreds of presents succeed; layer isolation alone did not resolve visibility.

Swappy uses VK_GOOGLE_display_timing. MoltenVK 1.4.2 forwards desiredPresentTime directly to Metal presentAtTime, and reports presentation times on Metal's host timeline. Android CLOCK_MONOTONIC excludes suspend, whereas Darwin's identically named clock includes it. The old guest mapping therefore produced timestamps ahead of Metal by accumulated suspend time. The capture does not log desiredPresentTime, so its actual offset cannot be measured retrospectively.

- Map guest monotonic, raw and coarse clocks to Darwin CLOCK_UPTIME_RAW, which reads mach_absolute_time. Preserve CLOCK_BOOTTIME separately.
- Share the clock mapping with Choreographer, System.nanoTime, uptime and touch timestamps. Keep elapsedRealtime on the suspend-aware clock.
- Read guest absolute futex, condition and sleep deadlines using that same mapping.
- Return errors for unsupported clock IDs; clock_getres must describe the selected clock.
- Sample requested presentation timestamps and the suspend offset independently of the general GPU log cap. Preserve Vulkan timing arguments and extension support.
- Existing timing checks now sample the guest clock API instead of Darwin's different host clock. No new test cases were added or executed in this follow-up.

The primitive-restart message is emitted by reportWarning in MVKPipeline.mm; it does not set pipeline creation failure.

On-device verification is still required for actual visible Vulkan pixels and the new vkTiming values.

## Sources

- Android NDK Looper: https://developer.android.com/ndk/reference/group/looper
- Android NDK Choreographer: https://developer.android.com/ndk/reference/group/choreographer
- Apple arm64 ABI: https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms
- Swappy teardown joins its owner looper thread: https://android.googlesource.com/platform/frameworks/opt/gamesdk/+/219552e7a43e99ea62c3feb257e4bae55879d174/src/swappy/common/ChoreographerThread.cpp
- Linux clock semantics: https://man7.org/linux/man-pages/man2/clock_gettime.2.html
- Apple clock implementations: https://github.com/apple-oss-distributions/Libc/blob/main/gen/clock_gettime.c
- MoltenVK timed presentation: https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/MoltenVK/MoltenVK/GPUObjects/MVKImage.mm#L1501
