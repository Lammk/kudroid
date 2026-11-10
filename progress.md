# Progress

## Hard-exit investigation (19:27 device run)
- Read the four fresh logs and independently pulled device copies and Apple's matching crash report.
- Confirmed two SIGSEGVs: invalid class pointer 0x5 in regex method resolution, then invalid metadata in SwiftUI on the host main thread. Apple's report confirms the latter caused process exit.
- The native ELF/protection repair completes successfully in these logs. The invalid metadata's source is still unproven; no speculative fallback or pointer substitution was added.
- Added a bounded history of method resolution sources and addresses to crash reports, plus register memory snapshots starting at offset zero. The reporter reads copied atomic data and skips busy records instead of waiting in a signal handler.
- Signal-report regression: 5/5 checks pass. Rebuilt real-framework regex suite: 28 checks, 0 failures. Diagnostic iOS build and packaged asset validation are in progress.
- Final diagnostic iOS build and asset validation exited 0. IPA is 43,455,761 bytes; symbol inspection confirms the packaged recorder and reporter. Git diff --check passes.
- The next device reproduction is needed to distinguish an invalid cached pointer from class-table metadata damage. Previous Unity and startup repairs are preserved.

## Fresh configuration crash repair
- Unity startup is confirmed working by the user.
- Diagnosed the earlier native SIGBUS as V8 command-line flag metadata being written before prepared-image segment protections.
- Added a separate post-protection runtime configuration callback while preserving pre-protection named interposition and pre-constructor policy setup.
- Red ordering regression: 105 PASS, 2 FAIL. Green verification and iOS rebuild are in progress; no device crash disappearance is claimed yet.
- Final ordering regression uses native page boundaries: 110 PASS, 0 FAIL. Alias stores: 118/118; reservations: 37/37; address-space-pressure recovery passes.
- iOS build and all packaged asset checks exited 0. IPA: ios-app/KuDroidShell/xtool/KuDroidShell.ipa, 43,451,332 bytes. Packaged executable includes the new configuration lifecycle.
- Diff check passed. No commit, push, deployment, or changes to the confirmed Unity fix or measured loader optimization. Fresh Minecraft device launch remains pending.

## 2026-11-04
- User ended USB provisioning investigation after fixing it independently.
- Began full logs/ crash and boot audit; identified native class-hierarchy corruption, a preceding V8 permission failure, and a 3538-ms main ELF load phase.
- Started the requested scripts/build-local-ios.sh --run under timeout 240; log: /tmp/kudroid-build-local-ios.log.
- C++ configuration, compilation, asset validation, and archive packaging passed; Swift build/deployment is still running.
- Preserved all pre-existing TLS and IPA packaging changes.
- Build/run exited 1 after successful Swift build; device discovery failed. Independent usbmux protocol and libimobiledevice probes cannot retrieve a device list.
- Next action: restart the unresponsive usbmuxd service, then recheck discovery before rerunning deployment.
- The service stalled during stop-sigterm. A targeted systemctl kill --signal=KILL followed by systemctl start restored it. Pairing validation and xtool USB discovery passed.
- Rerunning the original build/run path with timeout 240 to verify deployment.
- Second run built successfully and reached Apple provisioning, then exited 1 with a ProfilesCreateInstance device-ID conflict. Inspecting team device registration and provisioning lookup before making changes.

## 2026-11-02
- Read repository instructions and selected diagnosis/planning/verification workflows.
- Began inspecting logs/kudroid_android_logs.txt and build entry points.
- No runtime changes yet.
- Resumed from saved planning files; preserved the pre-existing IPA compression edit.
- Read new native breadcrumbs and stderr; isolated a concrete missing Android TLS API causing uncaught HTTPS-worker failures.
- AOSP frameworks-base URL returned 404; SSLSockets belongs to libcore, so switched to the libcore source.
- Confirmed provider-only API contract from AOSP Conscrypt publicapi source and documented host session-ticket controls.
- Added a DEX/JNI regression harness and local TLS fixture. Configured a core-only host build successfully; baseline runtime compilation is in progress under timeout 240.
- Baseline host build completed successfully. The new JNI regression failed with NoClassDefFoundError for android.net.ssl.SSLSockets, reproducing the copied log's missing API defect.
- Implemented provider detection, guarded session-ticket configuration, and native option propagation for both TLS backends; retained the existing native create overload.
- Host TLS regression passed: 17 Java framework checks, JNI API resolution, two encrypted TLS 1.2 request/response exchanges with ALPN, and the server's session-ticket extension trace.
- Native-looper regression passed all five checks.
- The sandbox blocked loopback socket creation; the approved unsandboxed run passed, recorded in /tmp/kudroid-tls-after-unsandboxed.log.
- Started timeout 240 env APPIMAGE_EXTRACT_AND_RUN=1 ./scripts/build-local-ios.sh; output is in /tmp/kudroid-ui-ios-build.log.
- Full iOS build exited 0. IPA: ios-app/KuDroidShell/xtool/KuDroidShell.ipa (43,448,108 bytes). Build checks verified the Flutter runtime catalog and Android system fonts.
- Python test runner syntax check and git diff --check passed. The generated framework.dex was rebuilt with the restored public TLS API.
- New regression files were marked intent-to-add so they appear in Git's diff despite the ignored kuart-tests directory. No commit or push was made.
- On-device disappearance of the startup freeze is not verified; supplied logs establish the repaired HTTPS-worker failure, not the final game rendering outcome.

## Current runtime repair session
- Implemented TTS API compatibility, shared allocation/VM entry synchronization, concrete ancestry optimization, and capability-based V8 interpreter mode.
- Optimized the measured main ELF loading work; compared exact transformed code on main, Unity and IL2CPP images.
- Completed logs2 audit and fixed the lifetime of unused startup reservations without shortening guest mappings or releasing claimed cages.
- Passed allocation, VM-entry, language API, ownership, virtual-address pressure, mmap, ELF and TLS checks.
- iOS build passed; a final rebuild includes the last ancestry edge-case and diagnostic refinements. Fresh device launch verification is pending.

- Final iOS build exited 0; IPA is 43449804 bytes. Packaged executable contains the capability policy; bundled source adapter still matches its verified asset.
- Final focused tests: TTS 8/8, assignability 16/16, builtin VM exclusion, concurrent allocation integrity, and VM pressure recovery all pass. Git diff --check passes.

## 20:38 crash follow-up
- Mapped both faulting strcmp and stale TLS-trap return address to the matching executable. Identified unsafely enlarged guest alternate stack as the source of writes into unrelated allocations.
- Saved complete pre-turn diff and original GuestSignals.cpp in .planning/signal-stack-corruption/.
- Added scripts/test-guest-altstack.py using the real translation function's Darwin branch with real host signal delivery. Original source corrupts adjacent canaries; repaired source passes all 23 bounds, query, failure, alias and thread-state checks.
- Removed size inflation and corrected old-stack state reporting. Host/iOS rebuilds and prior regression checks are next.
- Host core rebuild exits 0. Existing ELF ordering/interposition regressions pass 110 checks, cage reservations pass 37, address-space-pressure recovery passes, copied signal-history reporter passes 5.
- Full iOS build and packaged runtime/font checks exit 0. IPA: ios-app/KuDroidShell/xtool/KuDroidShell.ipa, 43,455,271 bytes. Packaged arm64 translator passes the caller size directly to the host, without the old 32-KiB inflation.
- Compared the entire tracked diff against the saved pre-turn diff: only src/abi/GuestSignals.cpp changed this turn. Earlier framework, Unity reservation, V8 lifecycle and loader-speed repairs are unchanged. New regression script is untracked. Git diff --check passes.
- Device disappearance of the Minecraft crash is not yet verified; deliver rebuilt IPA for a fresh launch.
- Rebuilt the original real-framework regex regression from kuart-tests/test_kuart_regex_classes.cpp against the fresh host core: 28 checks, 0 failures. The smaller CPU-input audit also passes its four cases.

## Black-screen investigation (2026-11-05)
- Read current log inventory and startup/tail events, restored earlier repair context, saved pre-turn tracked diff.
- User clarified black screen and manual stop. Investigating first-swap presentation and surface ownership.

- Real-framework surface regression baseline: 12 checks, 10 failures. Initial harness needed Looper.prepareMainLooper before NativeActivity.onCreate; corrected fixture before accepting the baseline.
- Removed automatic Window callback registration/direct Activity dispatch; NativeActivity now explicitly takes Window surface per AOSP. Existing view traversal covers all SurfaceViews and host loss.
- Added bounded EGL presentation summaries outside the startup quota; success milestone now requires EGL_TRUE; counters use relaxed atomics for multiple render threads.

- Verification passed: surface lifecycle 12, EGL 22, regex 28, ELF relocation 110, alias stores 118, reservations 37, mmap 13, TTS 8, assignability 16, trace reporting 5; 96000 concurrent allocations and VM-pressure recovery pass. Alternate signal-stack regression passes.
- TTS initially ran during iOS framework regeneration and could not read the temporary missing DEX; rerun after DEX generation passed 8/8. VM-entry test rerun with its required DEX argument passed.
- Full iOS build and runtime/font asset verification exit 0. IPA is 43,458,649 bytes. Fresh final surface regression passes 12/12.
- USB install/run started with the existing authorized build script. Device rendering is not yet confirmed.

- USB deployment exited 0. User supplied fresh 13:07 logs: single surface creation verified, black screen remains. Continuing from sampled clear-only frames and native worker stacks.

- User ran ~2 minutes: black but interactive sunsetting warning. Paused TLS optimization and restored its only tracked experimental change. Saved long-run logs. Continuing graphics submission/capability diagnosis.

- Diagnostic host build passes. Fresh EGL capability suite: 22 checks; surface lifecycle: 12; alias store: 118; reservations: 37; ELF relocation regression exit 0.
- iOS diagnostic build and asset validation pass. IPA size 43,461,741 bytes. Deployment running via the existing USB build/run script.
- Rendering is still unresolved; next device run must show submission census and retained later GL errors. No TLS rewrite is included.

- USB deployment completed successfully (exit 0; installation and verification 100%). Await a fresh two-minute JIT-enabled run.

## Renderer submission follow-up
- Restored previous investigation and inspected the fresh log. Saved current diff/source/log before further edits.
- Tracing native draw imports, engine branch predicates, and capability query ABI. No new runtime edit yet.

- Added targeted boundary traces after auditing existing native draw routes. No speculative rendering repair. Host build and 22 EGL/12 lifecycle checks passed with timeouts.
- User agreed to a fresh 30-second JIT-enabled reproduction after installation. iOS build/deployment is running under timeout 240.

- Full iOS build and USB installation/verification exited 0. Packaged executable contains draw backend, attempt/vertex census and program caller trace strings. Git diff --check passes.
- Reported installed build readiness to user. First follow-up pull at 14:27 still contains the previous 13:41 run; waiting for new reproduction, not a renderer-fix claim.

## Execute .planning/plan.md
- Read and audited plan; saved baseline in .planning/framework-plan-implementation.
- Continuing generic framework repairs; no fabricated licensing responses or app-specific Integrity hooks.

## Framework plan verification
- Surface red: saved DEX fails 6/19 checks; current DEX passes 19/19.
- RSA red: saved DEX rejects SHA1withRSA; current DEX passes 46 checks with valid and tampered signatures. Test-only OpenSSL config accommodates Fedora SHA1 policy.
- Service red: saved DEX cannot resolve a registered action; current implementation passes 24 service lifecycle/resolution checks.
- Typed manifest red: native parser loses negative priority; corrected decode passes 24 binary/text manifest checks.
- Existing suites pass: symmetric crypto 110, loading 33, startup 84, collections 803, EGL 22, reservations 37, mmap 13, aliases 118, ELF relocations, VM-pressure recovery.
- Scope audit confirms all 39 unrelated pre-existing tracked patches unchanged. Release IPA build in progress.

- Release IPA build and launcher/runtime/font validation exit 0. USB installation underway.

- Release USB installation and device verification reached 100%, command exit 0. Requested the next 30-second JIT-enabled visual test.
- Implementation delivery is verified; the black-screen outcome remains pending device reproduction.

## JIT helper refresh
- Downloaded official StikDebug 3.2.0; GitHub release SHA256 matched.
- Initial installation hit Apple free-profile three-app limit. User authorized deleting expired StikDebug 3.1.10; removed only that app, then installed 3.2.0 successfully.
- Copied the current authenticated USB pairing record to StikDebug Documents/pairingFile.plist; read-back bytes matched. Pairing credentials were not printed or saved to repository files.
- Updated runtime and JIT helper are installed. Await a fresh 30-second Minecraft run with JIT to verify rendering.

## Fresh black-screen investigation (2026-11-05)
- Read current logs and matching full device logs; confirmed the updated runtime is running and zero draws persist.
- Saved baseline diff/log under `.planning/black-screen-after-framework/` before new changes.
- Continued native submission audit beyond uniform/resource processing; found zero vertex-stream gates before both attribute setup and draw dispatch.
- Auditing upstream item creation and generic host boundaries. No speculative behavior fix added.

- Added bounded standard-API traces for debug group/object labels, buffer input metadata, writable map results, and separate vertex/index payload-call counters. Optional debug procedures retain backend availability; no buffer contents or GL error queries are added.
- Host core build passed. The full host-test CMake switch cannot generate in this checkout because ten pre-existing test sources are absent; restored its prior OFF setting and will link the relevant regression directly against the built core.

- Corrected the test-only EGL procedure declaration to the official function-pointer typedef; manually linked regression passes 36/36. Surface lifecycle passes 19/19. Git diff --check and saved-baseline scope comparison pass.
- Release iOS packaging underway under timeout 240. No rendering fix is claimed.

- Release IPA build and launcher/runtime/font checks passed. Packaged executable contains all new diagnostic strings. USB install/verify reached 100% and exited 0.
- Asked for a 45–60-second JIT-enabled reproduction of the installed build to inspect render-pass labels and geometry payload inputs. Await device evidence; black screen is unresolved.

- User will test later. Leave the installed diagnostic ready; resume this phase from the next device logs without repeating framework fixes or claiming a rendering repair.
- Optional follow-up log pull could not connect to usbmuxd after installation. No new reproduction was requested because the user will test later; the verified installed IPA remains the delivered state.

## Submission-input follow-up (2026-11-06)
- User ran the new build. Local files were old, so pulled matching fresh device logs and refreshed logs/.
- Confirmed payload uploads with zero vertex setup/draw attempts. Debug labels identify the sole bound program as internal text; real scene shader/material readiness and submission consumption need audit.
- Saved current tracked baseline under `.planning/render-input-followup/` before source changes.
- Audit-only material-suffix references saved as material-load1.s/material-load2.s. No application instruction edits or runtime offsets added.
- Developer instrumentation requires a remote tunnel; local lockdown service rejected DVT. Started a temporary timeout-bounded tunneld for read-only debugger access, but it has not discovered the device. Ordinary USB log access succeeds.
- Direct authenticated lockdown tunnel succeeded and DVT process/screenshot access works. The tested runtime process is no longer present, so live submission inspection cannot use that completed run. Remote debugserver negotiation also returned a stream reset. No app was relaunched or modified.
- USB subsequently disconnected and usbmuxd stopped; asset verification could not reconnect. Requested a connected live black-screen session with the same installed build. Prepared live-inspection.md and the matching host symbol binary for entry-point auditing.
- Final tracked-baseline comparison and diff whitespace check pass: no new tracked source edits in this follow-up. Waiting for the requested live session; the renderer defect remains unresolved.
- User requested reinstall. Verified the existing IPA contains build 2026-11-05T17:29:10 and submission-input markers, then reinstalled it over USB. Installation and verification reached 100%, command exit 0. Same diagnostic build, no new renderer repair.

## Longer run and shader-resource audit (2026-11-06)
- Read and pulled the new 223-second run: 10440 successful swaps, zero draw attempts; shader labels remain internal debug/clear resources.
- Pulled actual cached UI materials with current-run timestamps. Confirmed ESSL_310-only shader platforms and #version 310 es versus the backend's ES 3.0 / GLSL ES 3.00 context. This establishes a real renderer compatibility blocker.
- Developer transport and debugserver address can now be obtained sequentially. Swift's LLDB lacks libpython3.13; the system LLDB works. Connection attempt timed out before launching a process; no app was relaunched or modified. Temporary tunnel was timeout-bounded.
- Downloaded parser source for audit only; no app asset edits, capability spoofing, or runtime fixes were made. Next implementation must provide real compatible backend support and preserve existing app behavior.

## Vulkan backend implementation
- User authorized switching to ANGLE Vulkan/MoltenVK and chose Linux cross-builds.
- Read current backend/loader code and native capability constraints; source build currently stubs iOS Vulkan display creation out.
- Saved baseline, design, implementation plan and source lock under .planning/vulkan-backend/. Pulling isolated ANGLE source without replacing existing binaries.
- Current host has about 2 GiB available memory; use one compiler job and timeout-bounded incremental builds.
- Created isolated host worktree /home/kuzei/Documents/Kudroid-vulkan on codex/angle-vulkan-ios, carrying the current tracked runtime changes. ANGLE source is pinned and on its own kudroid/ios-vulkan-linux branch.
- Added a Linux GN/link driver in the isolated worktree; syntax/help checks pass. Configuration currently stops because the dependency sync has not generated build/config/gclient_args.gni yet.
- Dependency fetching progressed to about 3.2 GiB but hit timeout bounds twice. Reduced unnecessary test/toolchain fetches, enabled diagnostic sync output and HTTP/1.1, and kept all shipping libraries intact.
- Corrected an initial config-path mistake and terminated duplicate bootstrap processes before restarting dependency sync sequentially. The depot_tools bypass phrase is "chrome operations", not "chromium operations".
- Prepared a dry-run-checked ANGLE patch for iOS Vulkan display/surface factories, Metal surface loading, and direct packaged MoltenVK routing. Patch lives in the isolated worktree and is not yet applied to the source build.
- Verified MoltenVK swapchain initialization sets the layer's Metal device and drawable size; the new surface reuses/retains the existing layer and reads current pixel dimensions instead of attaching nested layers.
- Verified ANGLE already upgrades ordinary backwards-compatible GLES contexts to the backend's highest conformant version, while preserving explicit strict/WebGL versions. No guest context-attribute rewriting is needed for this path.
- Dependencies include large hermetic toolchains that this cross-build will not use. Disabled non-git binary downloads and use the installed compiler/Python plus the separately pinned GN tool; source dependency sync is still progressing under bounded runs.
- Generated checkout GN flags using the pinned DEPS and upstream safe parser while source fetches completed. Linux Metal configuration now passes (1585 targets). Added explicit iOS SDK build declarations to the Chromium GN config; SDK build metadata comes from its SystemVersion.plist (24A94232).
- The iOS Vulkan patch remains unapplied while the unmodified renderer baseline compiles. Removed one obsolete GN argument after configuration warned it had no effect.

## Linux Vulkan implementation continuation
- Added a task-local Clang 21 toolchain layout using matching Darwin runtimes; renderer feature checks remain unchanged. External-toolchain build flags omit unsupported new diagnostics while retaining sanitizer traps.
- Disabled optional Perfetto dependencies; baseline Metal compilation is progressing under bounded single-job slices.
- Added explicit backend routing and a GLES 3.1 draw/compute readback probe. Both 8-check routing suites pass; real Mesa 26.2.6 ESSL 310 draw/compute readbacks pass (probe validation only, not iOS Vulkan evidence).
- Prepared a portability-subset extension patch and identified MoltenVK provoking-vertex support as an additional actual feature gate.
- User requested working only in /home/kuzei/Documents/Kudroid. Stopped the build, compared all tracked sources, preserved 15 implementation files with hash verification, saved the worktree patch, and removed /home/kuzei/Documents/Kudroid-vulkan. Continuing in the main checkout by explicit user instruction.
- Shipping frameworks and the installed application remain unchanged.
- Main-folder CMake backend tests pass (2 tests / 16 checks); Mesa GLES 3.2 readback probe passes. Artifact validation accepts both existing iOS frameworks and rejects a host ELF.
- Restricted Vulkan headers to the probe source to avoid rebuilding unrelated core files. Host-core compilation is incremental and currently deferred while ANGLE builds because memory/swap pressure increased; only one compiler workload continues.
- Baseline Metal cross-build completed and staged. Clang 21's linker rejected the new SDK, and the system linker rejected arm64e.x1 tags. The SDK's DarwinTools linker parses the stubs and links; its platform warnings are retained with fatal_linker_warnings=false. Both binaries validate arm64/iOS 15.0 and framework install names, with no missing public EGL/GLES exports. Device loading is not yet proven.
- Vulkan GN configuration passes and the prepared iOS surface/direct loader/portability-subset patches are now applied after baseline validation. Candidate build is progressing (665 initial steps), single job, bounded slices.
- Updated GPU probe waits use 5-second GL fences before draw and compute readbacks; the revised probe passes again on Mesa GLES 3.2.
- Fixed a verified GN dependency issue: shared system/MoltenVK loader builds no longer pull the unused bundled Vulkan loader. Its missing filesystem configuration macros were the build failure, not a required runtime component. The patch preserves bundled-loader and directly linked configurations.
- Vulkan compilation resumed past shader tools into libANGLE; new iOS backend object compilation is still pending.
- Both new ANGLE iOS display/surface Objective-C++ objects compile successfully with the real SDK.
- Preflighted all four changed iOS host objects with Vulkan selection. Fixed an Apple EGLNativeDisplayType=int versus extension API void* compile error by passing nullptr to eglGetPlatformDisplayEXT. iOS object preflight passes.
- Refreshed both routing suites (16 checks) and the fence-bounded Mesa draw/compute probe; all pass.
- Added an explicit build-local-ios.sh backend option, defaulting to Metal, so candidate CMake cache settings cannot accidentally change normal builds.
- Resumed the full Vulkan library build after focused preflights; remaining backend compilation/linking and device validation are pending.

## Vulkan candidate delivery checkpoint
- ANGLE Vulkan cross-build completed and staged. Both device frameworks validate arm64/iOS 15.0, retain all prior public EGL/GLES exports, and include all four added iOS source files in provenance. Packaged XCFramework copies match their staged hashes.
- Scoped the Vulkan build definition to the three backend-aware core sources; generated compile commands verify it is absent from unrelated sources. Full iOS core build passes.
- Added once-only startup probing before guest launch, with ordinary Metal builds retaining a no-op preparation path. Explicit SwiftPM candidate framework selection preserves shipping artifacts.
- Release IPA build and real provisioning/signing pass. Provisioning CMS signature verifies using the existing SHA1 verification fixture; target device is included and bundle ID matches. All signed framework code/data sections match the staged candidate; signing metadata changes byte hashes as expected.
- Signed candidate: /home/kuzei/Documents/Kudroid/.planning/vulkan-backend/KuDroidShell-vulkan.ipa (97,194,700 bytes; SHA256 d594dae84f2651d5025cf3dfdff38480cd9aa6e5efb64281a30715297d75974f). Bundle ID XTL-5L5HW6K48W.com.kudroid.shell.
- Final local verification: 36 graphics forwarding checks, both backend-routing suites (16 checks), Mesa ESSL 310 draw/compute readbacks, and 19 real-DEX surface lifecycle checks pass. Scope audit verifies 58 unrelated prior repairs remain byte-identical to the saved baseline.
- Phone disconnected at 15:53; USB discovery confirms no phone. User explicitly chose to connect later. Candidate is NOT installed; actual GLES version, native window presentation, Unity regressions and original black-screen outcome remain pending device validation.
- Next: install the saved signed candidate via xtool install --usb --udid 00008030-000438A43C51402E, launch the installed bundle and read startup Vulkan/GLES probe logs. Do not rebuild against shipping frameworks while selecting Vulkan. No fake version/extension flags are enabled.

## Modularization task started
- User created checkpoint commit 0186700 before the split. Inventoried 15 oversized owned files and inspected existing module/build patterns.
- Presented responsibility-based extraction design; continuing under explicit user instructions to split all oversized files and continue. No implementation changes yet.

## Modularization continuation
- Recovered the pending inventory; saved all 15 original files under .planning/large-file-modularization/baseline and wrote responsibility/verification boundaries. No source changes existed at restart.
- Extracted native shared-state implementation modules and JNI environment facade; corrected extraction seams to complete functions. Swift screens/host support and Java View, decimal arithmetic, animation nodes, decomposition tables and generated resource arrays are split. Public API snapshot saved before framework rebuild.
- First Java compile exposed receiver/local-name collisions; corrected method-call identification and static-field qualification. The second Java compile passes. Stopped its duplicate D8 process after CMake requested the same framework generation; CMake now owns generation serially.
- Public/protected API inventories match for 3,264 members across eight key class/nested-class files. Framework DEX/embed consistency verified. Regenerating resources reproduces all original tables. Decimal Javadocs retained in full with compact formatting.
- Native looper regression passes 5 checks; timer deadline regression passes all 8 checks against Linux timerfd and the extracted shim. New framework helper smoke checks run on both checkpoint/current DEX. The first clone-duration expectation failed identically on both; replaced it with direct child-clone independence verification, keeping unrelated baseline timing behavior unchanged.
- Fresh host tests pass: graphics 36, surface lifecycle 19, manifest services 24; startup/security/collections suites pass 1,092 checks and the extracted-helper suite passes 17. Native TTS, assignability, VM entry locking, memory/ELF reservations, relocation, aliasing, traces and VM-pressure recovery all exit 0. iOS core cross-build is underway with one compile job.
- iOS core build passes. Final Java dispatch audit restored all seven original private View methods and routes private synchronized/native access through bridges. This preserves nonvirtual private dispatch and the per-view run-queue monitor. Regenerating the framework and final dependent objects before shell verification.
- Final host/iOS dependent core objects and test binaries relink successfully. Current and checkpoint DEX both pass 18 helper checks, including private-method shadowing. All original private View method declarations and synchronization flags match. Direct SwiftPM invocation lacks the iOS platform setup; using the existing xtool cross-build driver for shell compilation/packaging.
- Modularization complete: all 16 oversized owned originals split; all owned source/data modules are at most 2,000 lines. Fresh source/API/private-dispatch/data/symbol audits pass. Host core, iOS core, release shell/IPA and launcher/runtime/font checks pass. New helpers pass 18 checks on both original/current DEX; final surface/startup/graphics regressions pass. Module map is src/MODULES.md. No device launch was performed.
