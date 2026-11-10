# Runtime startup and crash repairs

## Goal
Audit every file in logs/, repair the confirmed runtime crashes with generic framework/ABI fixes, and reduce measured startup overhead with regression and iOS build verification.

## Phases
### Phase 1: Diagnose
**Status:** complete
- Read the complete startup log and correlate blocked work with runtime code.
- Inspect existing tests and build configuration.
- Compare repair options and record a focused design.

### Phase 2: Implement
**Status:** complete
- Add a reproducible regression for the confirmed defect.
- Apply the generic framework or ABI fix and review edge cases.

### Phase 3: Verify
**Status:** complete
- Run regression and relevant runtime tests with timeouts.
- Run the iOS build with a timeout.
- Review the diff and report evidence and any remaining device validation.

## Next Step
Collect a JIT-enabled run from the installed submission diagnostic build, with the new counters and content log. The interactive black-screen rendering failure remains unresolved.

### Phase 4: Local USB build/run error
**Status:** complete
- Run scripts/build-local-ios.sh --run with a timeout.
- Trace the reported error and apply a focused generic fix if needed.
- Verify the same build/run path and record remaining external blockers.
- User fixed provisioning independently and ended that task.

### Phase 5: Crash and startup audit
**Status:** complete
- Inventory and inspect all five logs; correlate Java, native crash, VM memory, and startup milestones.
- Confirm root causes and record a minimal repair design with edge cases.
- Identify avoidable work on the measured startup path.

### Phase 6: Runtime repairs and startup optimization
**Status:** complete
- Reproduce confirmed defects and apply generic fixes.
- Benchmark the targeted startup work before and after.

### Phase 7: Verification and delivery
**Status:** complete
- Run meaningful regression tests and the iOS build under explicit timeouts.
- Review source changes and state which on-device outcomes remain unverified.

## Design and implementation
- Restore android.net.ssl.SSLSockets.isSupportedSocket and setUseSessionTickets with Android's provider-only contract: support KuSSLSocket, return false for other sockets, reject unsupported setters.
- Use the existing SSLParameters/ALPN path. Wire session-ticket configuration to documented SecureTransport and OpenSSL options before handshake; preserve the existing four-argument native create entry point.
- Prefer this framework repair over changing idle-worker wait semantics (no deadlock evidence), or returning false for every socket (would bypass the platform ALPN adapter).
- Add a real-framework DEX/JNI regression covering socket detection, invalid sockets, ticket configuration, and ALPN preservation. Verify native option propagation and encrypted local TLS exchange if host backend is available.
- The checkout lacks several mandatory legacy host test files. Add a default-on host-test build switch so a core-only host build can support the focused regression without changing default behavior.
- Run all regression and build commands with timeout, then build the full iOS IPA with scripts/build-local-ios.sh.

## Constraints
- Preserve the existing edit to scripts/embed_standalone_launcher.py.
- No application-specific offsets or behavior.
- All builds and tests use timeout.
- No push.

## Errors
- Apple profile creation returned HTTP 409: no current IOS devices match the provided device IDs. Under investigation.
- Local USB run: unresponsive usbmuxd caused device discovery failure; restored with a targeted service termination/start. Deployment verification in progress.
- Git fsmonitor socket error: use git -c core.fsmonitor=false.
- Several guessed AOSP source paths returned 404; resolved SSLSockets at external/conscrypt/publicapi/src/main/java/android/net/ssl/SSLSockets.java.
- Local TLS test socket creation was denied in the sandbox. Reran the same test with approved escalation; all checks passed.

## Current repair design (2026-11-04)
1. Minecraft first: add the three missing TTS APIs using AOSP unavailable-service results. Verify real framework DEX/JNI resolution.
2. Synchronize the shared bump allocator; baseline concurrent allocation crashes. Acquire the VM lock before builtin/bodyless dispatch. Validate allocation uniqueness, contents and accounting.
3. Restrict concrete ancestry checks to superclass chains. Preserve interface and array assignability.
4. On prepared-memory hosts without guest executable reservations, configure the matched V8 runtime through public SetFlagsFromString(--jitless) before constructors. Verify exported flag acceptance. This uses V8's documented interpreter support; WebAssembly remains unavailable in that mode.
5. Benchmark the full main ELF map before changing startup work. Avoid repeated virtual-register analysis only when the existing pass establishes there are no references; compare transformed executable bytes before/after.
6. Then audit all logs2 files and repair Unity's confirmed address-space exhaustion. Keep full mapping lengths and fixed-address semantics.
7. Run timeout-bounded regressions and iOS build. Device launch remains separate evidence.

### Phase 8: Device confirmation
**Status:** in progress
- User confirmed Unity launches without the previous crash.
- Fresh Minecraft logs show SIGBUS in SetFlagsFromString during relocation finalization: flag metadata is still RX because segment protections have not run.

### Phase 9: Runtime configuration lifecycle
**Status:** complete; device validation pending
1. Add a post-protection initializer callback to relocation. Test executable code, writable globals, finalizer exclusion, and callback failure propagation with a synthetic ELF.
2. Move capability-based V8 flag configuration into that callback. Keep named interposition before protections and all guest constructors after configuration.
3. Reproduce the ordering failure, then run relocation/alias/reservation regressions and the iOS build with explicit timeouts. Review side effects and deliver the rebuilt IPA for device validation.
- Final verification: 110 relocation checks, 118 alias checks, 37 reservation checks, and address-space-pressure recovery pass. iOS build/IPA validation exit 0; git diff --check passes.
- Minecraft requires a fresh device run with the rebuilt IPA. Unity confirmation and previous boot-speed optimizations are preserved.

### Phase 10: Hard-exit diagnosis
**Status:** in progress
- Fresh 19:27 logs confirm adapter installation and interpreter mode complete, followed by a successful main ELF load at 2163 ms.
- First SIGSEGV: EnsureInitialized receives class pointer 0x5 from the resolved method during Lexer.movePointer; the referenced static method is inherited getPredefinedClass.
- Second SIGSEGV: SwiftUI dynamic-property reflection reads a pointer containing the bytes of the shell bundle filename. Apple's matching crash report confirms this main-thread fault causes process exit.
- Host regex regression passes 28 checks. Method-table lifetime and cache lookup were audited; the writer/source of the invalid metadata is still unproven. Do not label a pointer check or cache reset as the root-cause fix.
- Add bounded resolution snapshots to crash reports. Record cache versus class lookup, runtime/context, DEX reference, target address and lookup class; copy records before faults so reporting does not dereference damaged metadata.
- Signal-handler regression passes 5 checks: copied records, thread filtering, eviction, bounded text, and errno preservation. Build a diagnostic IPA for the next device reproduction.
- Diagnostic IPA build and asset checks exited 0; final regex regression remains 28/28. Packaged executable contains the history recorder/reporter. Await a fresh device run; the crash itself is not fixed yet.

### Phase 11: Alternate signal-stack corruption
**Status:** implementation verified; device launch pending
- Latest crash is FindDirectMethod comparing a null method name; surrounding method fields are also overwritten.
- A damaged code-item slot contains crashHandler's TLS-trap return address (+0xe6c), with signal number 5 in another field. This identifies a previous signal-handler frame inside method storage.
- guest_sigaltstack inflated caller sizes below 32 KiB without allocating backing memory. XNU kern_sig.c accepts 8 KiB and reports ENOMEM below it; preserve the caller's actual bounds and host failures.
- Fix previous-stack reporting using guest state before installation/disable, including aliased input/output.
- Real-signal regression fails on the saved source: handler outside supplied buffer and adjacent allocation corruption. Corrected source passes all 23 checks.
- Verification complete: 23 alternate-stack checks, 28 real-framework regex checks, 110 ELF checks, 37 reservation checks, VM-pressure recovery and 5 trace checks pass. iOS build/IPA asset validation pass. Only GuestSignals.cpp differs from the saved prior tracked patches.
- Next: fresh device launch with the rebuilt IPA to verify the game gets beyond the prior crash.

### Phase 12: Black screen in 2026-11-05 logs
**Status:** in_progress
- User confirms black screen and manual stop after about 20 seconds.
- Trace successful EGL startup/presentation, native-window lifecycle, and ongoing engine activity.
- Reproduce a confirmed defect before changing generic framework/ABI behavior.
- Compare against the saved pre-turn diff, run timeout-bounded regressions and rebuild iOS.

## Current next step
Trace the invisible but interactive warning screen: long run has 3360 successful swaps and zero intercepted draws. TLS optimization set aside.

### Phase 12 validation status
- Baseline surface lifecycle reproduction: 10 failures / 12 checks.
- Framework repair implemented; full DEX regeneration and host verification underway.
- Device screenshot service unavailable (Developer disk image not mounted).

### Phase 13: Renderer submission audit (2026-11-05)
**Status:** in_progress
- Current log: 2280 successful swaps, 4560 clears, no counted draws; shaders, textures, and program selection continue.
- Audit draw import routes, capability/driver outputs and the branch that prevents draws; reproduce any confirmed ABI defect.
- Preserve pre-turn changes, apply a generic repair, run timeout-bounded regressions and build iOS.

## Next Step
Await the agreed fresh JIT-enabled run of the installed draw-boundary diagnostic build, then pull logs and select a reproduced generic rendering repair.

### Phase 14: Execute framework plan
**Status:** in progress
- Implement format lifecycle, SHA1-RSA in Java and both native backends, and manifest-backed action/package service resolution.
- Preserve signature validation. A licensed response needs authority-signed data; 0x0100 is not LVL LICENSED (0).
- Audit Integrity failure completion; the application already forwards Task exceptions to its native error callback.
- Run real-DEX regressions, manifest parser checks, host regressions, and release IPA build with timeouts.

### Phase 14 build status
- Generic implementation and host verification complete. Release IPA and asset validation pass.
- USB installation is underway. Visual black-screen outcome awaits a new JIT-enabled device run.
- Licensing service remains unavailable; no fabricated success response was added.

- Release USB installation and device verification reached 100%, command exit 0. Requested the next 30-second JIT-enabled visual test.
- Implementation delivery is verified; the black-screen outcome remains pending device reproduction.

### Phase 15: Black screen after framework fixes
**Status:** in_progress
- New 16:58:48–16:59:53 device run confirms the installed 16:35:41 build; black screen persists.
- Saved current tracked diff and Android log in `.planning/black-screen-after-framework/`; matching device logs are in `.planning/unity-native-loader/device-after/20261105-170151/`.
- Renderer census: 1440 successful swaps, 2880 clears, zero draw attempts and zero vertex setup; all observed draw backend pointers exist.
- Audit the native submission skip predicates and their generic ABI inputs before selecting a repair. No new behavior change is justified yet.

## Next action after latest device run
Execute .planning/vulkan-backend/implementation-plan.md: establish a Linux iOS ANGLE build, implement Vulkan display/surface support, and validate real capabilities before switching shipping binaries.

### Phase 16: ANGLE Vulkan backend
**Status:** in_progress
- User authorized Vulkan/MoltenVK integration and selected Linux cross-builds.
- Saved tracked baseline and source lock under .planning/vulkan-backend/.
- Pulling isolated ANGLE source. Current shipping libraries remain unchanged.
- Target verified ES 3.1 first; expose ES 3.2 only after its complete feature gate passes.

### Phase 15 diagnostic verification
- Host core build passes; EGL and new input-forwarding regression passes 36/36; unchanged real-DEX surface lifecycle passes 19/19.
- Only GraphicsShim.cpp and its existing EGL regression differ from the saved tracked baseline. The framework DEX and previous framework/ABI fixes are unchanged.
- Full host-test CMake generation is unavailable because this checkout lacks existing referenced test sources; relevant EGL regression was linked directly against the fresh core.
- Release IPA build, launcher/runtime/font validation, packaged diagnostic-string checks, and USB installation/verification all passed. Requested a fresh 45–60-second JIT-enabled run.
- Diagnostic delivery is complete; phase remains in progress because no generic black-screen cause/fix has yet been established.

## Large-file modularization

Goal: Split every first-party source file over 2,000 physical lines into coherent modules, directly in this checkout, preserving framework/API/ABI behavior and the prior rendering fixes. External dependencies and generated build outputs are outside scope; checked-in generated tables and the JNI binding are included.

### Phase 17: Inventory and module boundaries
**Status:** complete
- Inventory oversized source files and separate executable code, API facades and embedded data.
- Record interfaces/shared state and a per-file extraction plan before modifying code.

### Phase 18: Extract modules
**Status:** complete
- Refactor native runtime/shims, framework Java helpers, Swift UI modules and JNI binding headers.
- Keep original symbol names, public Java declaring classes, field layouts, synchronization and signal safety.
- Update generators/build lists with the modules and verify after each group.

### Phase 19: Verify modularization
**Status:** complete
- Confirm no owned source remains above 2,000 lines.
- Compare API/symbol/data inventories with checkpoint 0186700.
- Run relevant host/DEX regressions and Linux iOS cross-builds with explicit timeouts and a single compile job.

## Current next action
Large-file modularization is complete. Module map: src/MODULES.md. The Vulkan candidate still awaits the previously deferred phone test. The signed Vulkan candidate remains preserved and its phone validation remains deferred.
