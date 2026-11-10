# Findings

## Crash and boot logs, 2026-11-04
- logs/ contains five files and 6562 lines: classes.log (3), Android log (931), crash log (1131), breadcrumbs (2745), stderr (1752).
- Fatal SIGSEGV is in host DexClass::IsSubClassOf called recursively from CheckInstanceField; invalid class pointer is 0xfffffffeff332623. The existing receiver-class registry check does not validate hierarchy edges.
- Before the host crash, V8 reports a failed reservation SetPermissions for write-code unprotection at 17:09:02.201. SIGILL/sysreg and BRK handling follow; investigate the allocation backend instead of masking the fatal check.
- Boot milestones: DEX ready 98 ms; main ELF load 146 to 3684 ms (3538 ms); libraries done 3770 ms; JNI 4616 ms; EGL initialize 6637 to 6660 ms; first swap 7142 ms.
- classes.log reports missing TextToSpeech.setLanguage, isLanguageAvailable, and stop. These are secondary API gaps, not the fatal stack.

## Local USB run, 2026-11-04
- Requested build completed C++ and Swift successfully; xtool exited 1 with `Error: Operation not permitted` while waiting for a device.
- USB enumerates the phone. Connecting to /run/usbmuxd succeeds, but an explicit ListDevices request times out; idevice_id also cannot retrieve the list.
- usbmuxd remains running after immediately removing the connected device and reporting a lockdownd preflight error. This isolates a host device-discovery failure before app installation.
- Passwordless sudo is available for a targeted service restart.
- usbmuxd ignored the normal stop signal. Terminating only that service with SIGKILL and starting it again restored device discovery; idevicepair validates and xtool devices identifies the USB device.
- Second build/run reached provisioning, then Apple ProfilesCreateInstance returned HTTP 409 ENTITY_ERROR: no current IOS devices on the team match the provided device IDs. USB discovery is repaired; provisioning is the next confirmed blocker.

- The supplied log is 886 lines, covering startup at 22:15:40 through approximately 22:15:47.
- Native libraries load and the graphics context is gained; the final entries involve graphics startup and worker synchronization.
- Existing uncommitted change: scripts/embed_standalone_launcher.py.
- Repository instructions require generic fixes and timeouts on all tests/builds.
- Newly copied stderr.log (1798 lines) shows three uncaught NoClassDefFoundError failures on OkHttp Dispatcher in Android10SocketAdapter.matchesSocket: android.net.ssl.SSLSockets.isSupportedSocket is absent.
- A first EGL swap occurs at 7293 ms. Startup continues through resource-pack scanning and audio initialization; this proves startup progressed beyond graphics-context creation, but does not prove a visible game frame.
- Native breadcrumbs show continuing iOS main-thread heartbeats, touch delivery, and timed waits resolving. No fatal native hang stack identifies a synchronization defect; do not modify wait semantics based on idle-worker entries alone.
- Existing libcore.net.ssl.KuSSLSocket implements TLS, SSLParameters ALPN configuration, and selected ALPN retrieval. The Android public SSLSockets API is absent.
- Confirmed public API semantics from AOSP external/conscrypt/publicapi: provider-only isSupportedSocket and IllegalArgumentException from setUseSessionTickets for unsupported sockets.
- The installed SecureTransport SDK documents kSSLSessionOptionEnableSessionTickets (false by default). OpenSSL provides SSL_OP_NO_TICKET. Session tickets can be configured rather than accepted as a no-op.
- Several older CMake test targets reference files absent from this checkout, including test_kuart_netjava.cpp and build-java-test.sh. Use a self-contained targeted regression harness rather than claiming those suites ran.

## Current repairs and verification
- Added the three TextToSpeech APIs and Android language-status constants. AOSP's unavailable-service defaults are LANG_NOT_SUPPORTED for language methods and ERROR for stop; no speech backend is advertised. Original real-DEX/JNI regression reproduced four failures.
- The original shared DexHeap crashes in a concurrent allocation regression. A heap mutex now protects block selection, bump offsets and accounting; requests that overflow alignment are rejected. VM entry locking now covers native builtins/bodyless methods and synchronous framework initialization/lookup.
- Concrete ancestry uses superclass chains; actual interfaces and unresolved placeholders retain interface traversal. This removes the unnecessary traversal in the reported instance-field stack. The exact origin of the supplied interface-pointer overwrite remains unproven; do not claim that the device metadata corruption is conclusively diagnosed.
- V8's Android source defaults write_code_using_rwx to true. The logged prepared-memory host has no guest executable reservation capability. The matched runtime now receives --jitless through V8::SetFlagsFromString before constructors; the exported flag is checked. V8 documents this mode for iOS: JavaScript and regexes use interpreters, while WebAssembly is unavailable. No memory-mapping success is fabricated. Reference: https://v8.dev/blog/jitless and source tag 9.4.146.24 src/flags/flag-definitions.h.
- Boot optimization: avoid the virtual-register fallback walk after the existing pass proves no rewrite is needed; retain only x18 references in the rename list. All bounds, unknown-instruction, data and liveness checks remain.
- Three timed, address-controlled full-image host runs: main ELF map median 2322.657 -> 970.510 ms (58.2% reduction). Executable digest af1e2364ecbaed92 matches in all six runs. This is loader work, not measured total device boot.
- Real Unity and IL2CPP libraries were read from the installed application for audit. With ASLR disabled for comparison, transformed executable digests match before/after (Unity a8d2623e75224070, IL2CPP c5417049ee6cfe92). Unity retains 3017 renamed references and 2361 stubs.
- logs2 contains Android (141 lines), stderr (409), native breadcrumbs (250), and crash (351). NativeLoader/JNI startup succeeds. A sixth 536854528-byte anonymous reservation fails ENOMEM, then a commit at 0x4000 fails, followed by the null allocation-table write. No frame is presented. Later pause/detach timeouts are teardown effects.
- Unity run's early map has a 6 GiB gap. An unused 4 GiB cage is retained even though no supplying V8 runtime loads, leaving a 1.5 GiB largest gap after host setup. Unclaimed early reservations are now released after the native-library loading phase, before JNI/engine initialization. Cages transferred to matching runtime owners are preserved.
- Reservation ownership regression: 37 checks pass. A separate 6 GiB virtual-address-budget regression reproduces ENOMEM with an unused 4 GiB reservation; cleanup restores a full 3 GiB mapping and its writable tail. Full 8 GiB/fixed-NOREPLACE mmap suite: 13 checks pass. ELF interposition and alias-store suites pass; alias stores have 118 checks.
- Heap regression: 96000 concurrent allocations, no overlaps, corruption or accounting errors. Clang ASan/UBSan also passes. GCC sanitizer linkage is broken locally; Clang's sanitizer runtime was used successfully.
- Existing TLS regression still passes all 17 checks, certificate validation, encrypted exchange, ALPN and session-ticket negotiation.
- Firebase/Play Games provider initialization warnings remain a separate framework lifecycle gap; they are not the shown native fatal stack or the three entries in classes.log.
- Device disappearance of either crash and total boot-speed improvement require fresh launches with the rebuilt IPA. No new device launch is claimed.

- Final framework TTS regression: 8 checks; final assignability regression: 16 checks. Final iOS build and IPA verification exited 0.
# Fresh native configuration crash (2026-11-04)
- User confirmed the Unity crash is gone. Fresh logs/ stop during the main ELF relocation, before native constructors or framework activity startup.
- SIGBUS PC +0x749dedc is `str w22, [x21, #0x2c]`, with w22=3. Pinned V8 flags.cc identifies this field as `Flag::set_by_`, where 3 is kCommandLine. The frame chain reaches SetFlagsFromString and installRuntimeCageAdapter.
- Target +0x14018adc belongs to a non-executable PF_R|PF_W PT_LOAD starting +0x1400a010. Device region is still r-x/max=rwx: prepared mappings expose an RX guest view until applyProtections opens writable data pages.
- This proves the regression is the flag setter being called from the pre-protection relocation finalizer. The flag table is POD initialized, explicitly designed to work before static constructors; delaying configuration until after constructors would be the wrong lifecycle.
- Keep binding interposition in the finalizer, then apply protections/alias registration, then call capability-based execution configuration, then guest constructors. No application-specific patches or offsets are used in the implementation.
- Synthetic ELF ordering regression fails twice with initialization before protections and passes after the move. It also verifies writable globals, closed binding interposition, constructor visibility, finalizer rejection, and initializer failure propagation.

# Hard exit after configuration repair (2026-11-04, 19:27 run)
- Reviewed all four supplied logs and independently pulled the device logs without replacing logs/. Adapter installation and capability configuration complete; main ELF finishes loading in 2163 ms.
- Two crash records occur 329 ms apart. First: host Interpreter::EnsureInitialized reads status at class 0x5+12. InvokeMethod loaded this value from resolved target 0x11489a140+24.
- Matched local Mach-O addresses and the embedded DEX. The call is Lexer.movePointer at DEX PC 320, invoking CharClass.getPredefinedClass(String,boolean), inherited from its superclass.
- Second: main-thread SwiftUI _DynamicPropertyBuffer.addFields dereferences 0x5364696f7244754b+8. Register bytes contain KuDroidShell.app. This is invalid metadata; its writer is not identified by the crash reports.
- Pulled /KuDroidShell-2026-11-04-192728.ips to /tmp/kudroid-hard-crash.ips. It confirms EXC_BAD_ACCESS/SIGSEGV on thread 0, with the matching shell image UUID. This exit is not a jetsam event.
- Existing real-framework regex suite compiled and ran under timeout: 28 checks, 0 failures. This does not reproduce the device corruption. No speculative runtime fallback has been applied.


# Signal-stack overwrite (20:38 run)
- Read all four latest logs. Main ELF loading finishes at 2035 ms; V8 capability configuration completes. Fatal host lookup sees a null name in a valid direct-method vector, with a plausible neighboring entry.
- Runtime image slide is 0x3cc000. Damaged code_item 0x10063841c maps exactly to local 0x10026c41c: crashHandler's return from bionic_handle_tpidr_trap. The same damaged header carries signal 5, two nearby context pointers and a frame pointer. This is a stale SIGTRAP frame written into allocation storage.
- The confirmed unsafe writer is guest_sigaltstack's Darwin-only size inflation: it registered up to 32 KiB over the caller's smaller buffer. Real signal delivery in the regression writes beyond the caller's supplied 16 KiB range on the original source; preserving size keeps all adjacent canaries untouched.
- XNU source: https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/kern_sig.c, sigaltstack accepts OLDMINSIGSTKSZ=8*1024 and returns ENOMEM below it. SDK recommended minimum is 32768, but increasing a registered region without allocation violates supplied bounds. The repair forwards exact size and preserves host errors.
- Additional pre-existing ABI bug: old-stack output used guestInstalled after mutation; first install leaked the host's stack and disable hid the previous guest stack. Snapshot before installation fixes that without changing backing ownership.
- No method-pointer fallback, cache resets, or app-specific behavior added. Previous Unity cage release, V8 ordering and x18 loader changes remain intact.

## Black-screen run (2026-11-05 12:47)
- Three logs, 3130 lines; no crash report. User confirms black screen followed by manual stop.
- ELF main loaded at 2072 ms; EGL ES 3 context succeeds, first swap at 5484 ms. Engine continues network, audio, options and storage work through 20 seconds.
- Multiple native windows share host layer 0x15931e340; EGL window uses older native window 0x1558d8340 after a second wrapper with different size appears. Investigate presentation before concluding this is a Java initialization fault.
- Play Games and Firebase providers remain uninitialized, but their errors predate continued engine work.

- Confirmed two generic extra callback sources: Activity.attach registers arbitrary Callback Activities on Window, and ActivityThread directly invokes them in addition to holder dispatch. AOSP Activity.attach does neither; AOSP NativeActivity.onCreate explicitly takes its Window surface.
- Connected device is available outside sandbox, but screenshotr is unavailable without Developer disk image; do not claim on-device visibility from CLI.
- GPU logging cuts off at 256 calls; first-swap marker also ignores the EGL return value. Need bounded presentation result logs.

## Surface repair verification
- Real-framework DEX lifecycle regression: 12/12 after repair, compared with 10 failures before. Both duplicate Activity paths were responsible for additional creations and incorrect holder identity.
- NativeActivity now explicitly registers via Window.takeSurface, matching AOSP. All SurfaceViews receive readiness/loss via existing traversal; callbacks on removed registrations stay removed.
- Presentation diagnostics use sampled info-level census after the startup quota, return the real EGL result, preserve driver errors, and do not enable framebuffer reads. first-swap is emitted only after successful EGL presentation.
- iOS build/IPA checks pass. Earlier changes are byte-for-byte unchanged in the tracked diff except the intended additions to CMakeLists.txt and GraphicsShim.cpp. New Java edits affect Activity, ActivityThread, NativeActivity and Window only.

## Device follow-up after surface repair (13:07)
- User confirms black screen persists. Logs now contain exactly one onSurfaceCreatedNative and the same native window identity on lookup, confirming duplicate callback repair on device.
- Frame census records 720 successful swaps, 1440 clears, zero intercepted draws. Actual content is not being submitted. At roughly 22 seconds, native samples show the engine and two streaming workers burning CPU inside host code; inspect symbolized stacks next.
- Do not call the surface repair a complete black-screen fix.

## Two-minute run (13:22)
- User confirms black but interactive screen. Taps open https://aka.ms/MinecraftSunsetting (also in stderr), redirected to help article 30298767427597. Rendering remains black through 3360 swaps, 6720 clears, zero intercepted draws.
- TLS signal overhead is real startup overhead but not a sufficient black-screen explanation. Rejected the multi-instruction far-stub proposal because indirect entry and LR preservation were not established; restored the temporary compile-guard change. No new TLS implementation remains.
- Trace graphics command submission and warning UI; do not fake supported GPU features.

## Submission diagnostics
- The supplied long run reaches an interactive native UI and opens MinecraftSunsetting twice. EGL 3.0 initialization, shader compile/link, surface creation and all sampled swaps succeed. The host GL exports and guest draw import routes are present. No confirmed draw-path defect is established yet.
- Existing GL error logging stopped after 32 errors, exhausted by initialization format probes. Later failures were invisible. Changed to bounded per-caller sampling while forwarding each original error exactly once.
- Added sampled submission counts (shader sources, program links/binds, buffer/texture uploads) and surface/viewport/scissor/FBO/program state using valid read-only queries. No pixel reads, capability spoofing, or guest instruction rewrite added.
- Enabled content_log_file in the installed guest options, with a saved exact backup in .planning/bedrock-black-screen/long-run/options.before-content-log.txt. Content logs were empty before enabling it.
- Compared with the surface repair baseline: only GraphicsShim.cpp has new source changes. The framework DEX hash remains 9e5da802ced228c06dfe045a2e1d78619811a1aa; baseline omitted the already-added surface test files.

## Renderer audit follow-up (2026-11-05 13:41 log)
- Read current logs/ inventory: only Android log, 1151 lines. Runtime does not crash; 2280 successful swaps and 4560 clears, zero counted draws.
- Submission census confirms viewport/scissor 1600x786, FBO 0, program 3, 11 shader sources, 9 links, 43 buffer uploads and 1424 texture uploads. Later GL errors are absent; all logged errors belong to initialization probes.
- Saved pre-turn diff and GraphicsShim.cpp in .planning/render-submission/. Existing surface and runtime fixes remain separate.
- Core question: why does the native submission path never reach an intercepted draw? Audit, do not invent an application-specific workaround.

- Matching additional device logs pulled to .planning/unity-native-loader/device-after/20261105-135917/. No shader failure or fatal rendering stack; runtime activity continues. classes.log adds a missing TTS.isSpeaking method, unrelated to the no-draw frame loop.
- Native draw imports and instanced draw procedure routes audit correctly. The draw counter increments only after successful backend lookup, so zero counts do not yet distinguish no calls from lookup misses.
- Added bounded attempted-draw, vertex-setup, program-caller and backend-pointer observations; renderer behavior is unchanged. Host core build passes; EGL 22/22 and surface lifecycle 12/12 pass. Entire tracked diff against the saved baseline differs only in GraphicsShim.cpp.

## Framework plan audit
- SurfaceView currently reports UNKNOWN and ignores format selection. Callback state must track format changes. Host default is RGBA_8888; AOSP defaults RGB_565 and normalizes OPAQUE to RGB_565.
- SHA1withRSA is absent in Java and Apple/OpenSSL native algorithm maps.
- PackageManager has no resolveService and no declared-service registry; action/package binding cannot resolve a real service.
- No draw attempts in the fresh diagnostic log. Licensing failure is observed but is not proof of the rendering root cause.

- Integrity bytecode confirms bindService(false) fails all pending Tasks via zzj.zza(Exception); MainActivity already forwards Task failures to its native error callback. No app-specific Activity methods required.
- New lifecycle regression exposed component-enabled settings stored per PackageManager instance, allowing start/stop to resolve different services. SystemPackageManager now shares settings across contexts.
- Fedora OpenSSL rejects SHA1 signatures by crypto policy (invalid digest), including openssl dgst. Regression uses an explicit process-only configuration; runtime policy remains unchanged.
- Action/category service filters are supported; data-constrained filters remain unresolved until URI/MIME matching is implemented.

## Black screen after framework fixes (2026-11-05)
- Current run executes build `vT1.6 2026-11-05T16:35:41 ff8c99d+dirty`, framework 5838028 bytes. The previous implementation did not resolve the symptom.
- Matching full device logs: `.planning/unity-native-loader/device-after/20261105-170151/`. Final census has 1440 swaps, 2880 clears, zero draw attempts, zero vertex setups, 4320 program binds; viewport/scissor match the 1600x786 surface.
- Program-binding caller relative to native ELF base is `0x11da86e8`, inside normal renderer submission. This does not establish absence of renderer items.
- Assembly audit: submitted byte stream mask is loaded at `0x11da9498`, copied to current state at `0x11da9598`; zero current mask branches back to the item loop at `0x11da9634` and `0x11da97f8`, bypassing attributes and draws. These addresses are audit evidence only, never runtime offsets.
- Program 3 initializes four attributes and a 114688-byte vertex / 12288-byte index buffer, consistent with debug text resources. The exact relationship to actual per-frame items needs further audit.
- Cond timed waits log Darwin ETIMEDOUT=60 before translation; existing `guest_errno` converts it to guest 110. No timeout repair is warranted from that diagnostic.
- No new runtime implementation was made during this phase yet.

- DEX audit confirms no Sunsetting URL string in the four app DEX files, and MainActivity.displayDialog is a no-op. Its unused AlertDialog builder only creates Ok/Cancel controls; do not assign the observed native warning to that Java dialog.
- Native samples move from V8 parsing/preparsing through bytecode generation into later engine work; this is progress, not proof of an indefinitely stuck parser. Rendering worker is mostly waiting. Existing TLS trap overhead remains known and is not established as the zero-draw cause.
- Added diagnostic metadata at standard GL buffer upload/map and debug group/object-label APIs. Only provided optional debug procedures get wrappers. No buffer payload reads, per-app offsets, or GL error queries are added.
- Forwarding regression passes 36 checks, including 64-bit sizes/offsets, null results, invalid lengths without pointer inspection, unavailable optional procedures, and preservation of pending GL errors. Surface regression passes 19 checks. Baseline scope comparison identifies only GraphicsShim.cpp and tests/test_egl_capabilities.cpp as changed tracked patches.

## Submission-input device result (2026-11-06)
- Local logs initially matched the previous run byte-for-byte. Pulled actual device logs into `.planning/unity-native-loader/device-after/20261106-133022/` and refreshed `logs/` from that snapshot.
- New run executes build `2026-11-05T17:29:10`; all new diagnostics are present. Final sampled census: 720 swaps, 1440 clears, zero draw attempts/vertex setups, 39 buffer calls, 9 vertex payload calls, 11 index payload calls, no writable maps. Additional index uploads follow that census.
- Object labels identify program 3 as debug font; other observed shaders are clear shaders. This confirms the previous resource identification and does not establish actual scene submissions.
- Geometry reaches GL buffer upload wrappers, including later 224-byte vertex and 18-byte index updates. Empty geometry creation is no longer a sufficient explanation. Trace renderer consumption/material readiness and generic inputs; do not change app code or invent draw submissions.

## Shader-profile mismatch established (2026-11-06, after reinstall)
- New 14:16:08–14:19:52 run executes the expected diagnostic build. Final sampled census: 10440 successful swaps, 20880 clears, zero draw attempts/vertex setups, 9 vertex payload calls, 15 index payload calls. Resource/geometry counters settle after startup; this is not a brief loading interval.
- Pulled matching full device logs to `.planning/unity-native-loader/device-after/20261106-142140/`.
- Correct installed bundle is `XTL-5L5HW6K48W.com.kudroid.shell`; using the unsigned IPA identifier in HouseArrest closes the service. Assets are served from base.apk and cached under android_root/data/cache/jar_entries, rather than an extracted assets directory.
- Current-run cache timestamps prove GamefaceStandard.material.bin loaded at 14:16:29 and UIBlit.material.bin at 14:16:48. Both downloaded resources contain only ESSL_310 platform markers and GLSL `#version 310 es`; no ESSL_300 alternative is present. This is a concrete shader/backend incompatibility.
- Current-run GL context explicitly reports OpenGL ES 3.0 / GLSL ES 3.00 on ANGLE Metal. Previous official-source/backend audit confirms Metal is capped at ES 3.0. Real ES 3.1 support is required for these shaders; version spoofing or simply changing EGL attributes cannot supply it.
- The ANGLE fetch/build script currently disables Vulkan Mac display creation on iOS via false/null stubs despite compiling Vulkan code. Enabling an iOS Vulkan display and validating ES 3.1 on MoltenVK is a potential generic backend route, not an already verified fix. Full ES 3.2 remains a separate unsupported geometry-shader requirement.
- No tracked runtime changes made in this follow-up. A direct native trace of shader selection is still useful to establish the exact skip path; the observed shader-version incompatibility must be resolved in any case.

## Vulkan backend capability audit
- Upstream enables portability enumeration on Apple, but the pinned Vulkan renderer omitted enabling the advertised VK_KHR_portability_subset device extension. Prepared a generic required-extension patch without setting unsupported features.
- MoltenVK 1.4.2 documents that private Metal API support must be compiled in before runtime configuration can enable it. Its provokingVertexLast feature depends on that support; ANGLE otherwise caps this Vulkan path to GLES 2.0. Actual device features remain to be measured.
- ANGLE Metal already contains a GPU index-rewriting ProvokingVertexHelper for points/lines/triangles/strips. If native provoking support is absent, this is a useful emulation reference, but a Vulkan port must verify primitive restart, vertex IDs, transform feedback, indirect/indexed draws and ordering before lifting the version gate.
- GLES 3.1 core calls with several stack arguments already have ABI wrappers. The remaining unwrapped core calls with stack input have only one stack argument, so they do not introduce Darwin's packed-multiple-argument mismatch. Macro-generated Boolean routes must be included in a complete procedure audit.

## Large-file modularization inventory
- User explicitly requested all files over 2,000 lines be split into modules; continue directly in the main folder.
- Checkpoint 0186700 contains the completed pre-refactor work. Working tree initially has only the existing planning files untracked.
- 15 owned source/header files exceed the threshold. SyscallShim.cpp is 9,597 lines; FmodVorbisFallback.cpp is 7,852 but most is one contiguous 119,593-byte data payload. LibCore.cpp has natural per-class dispatch families; GraphicsShim.cpp has window/layer, EGL, Vulkan, GLES and registry responsibilities.
- Public nested R classes and JNI table layout must remain unchanged. Large data and standard binding headers need source/generator changes that preserve their identities and binary layout.
- Expanded the source audit to implementation/data .inc files and found one more owned generated table: FrameworkResourcesData.inc (13,168 lines). Split scalar/string/style/name/text payloads with generator support; source total is now 16 oversized originals.
