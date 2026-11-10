static void crashHandler(int sig, siginfo_t* info, void* ucontext) {
    if (sig == SIGSYS && bionic_handle_guest_syscall_trap(ucontext)) {
        return;
    }
#if defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))
    if ((sig == SIGBUS || sig == SIGSEGV) && info && ucontext) {
        const uintptr_t fault = reinterpret_cast<uintptr_t>(info->si_addr);
        const auto& aliases = kudroid::GuestElfWritableAliases();
        if (aliases.ContainsFault(fault)) {
            auto* uc = static_cast<ucontext_t*>(ucontext);
            uint32_t instruction = 0;
            vm_size_t got = 0;
            const auto& ss = uc->uc_mcontext->__ss;
            if (vm_read_overwrite(mach_task_self(), ss.__pc, sizeof(instruction),
                                  reinterpret_cast<vm_address_t>(&instruction), &got) == KERN_SUCCESS &&
                got == sizeof(instruction)) {
                kudroid::Arm64StoreState state;
                for (unsigned r = 0; r < 29; ++r) state.x[r] = ss.__x[r];
                state.x[29] = ss.__fp;
                state.x[30] = ss.__lr;
                state.x[31] = ss.__sp;
                state.pc = ss.__pc;
                std::memcpy(state.v, uc->uc_mcontext->__ns.__v, sizeof(state.v));
                if (kudroid::EmulateElfAliasStore(instruction, fault, state, aliases)) {
                    // Only addressing writeback and PC change; source registers stay intact.
                    auto& out = uc->uc_mcontext->__ss;
                    for (unsigned r = 0; r < 29; ++r) out.__x[r] = state.x[r];
                    out.__fp = state.x[29];
                    out.__lr = state.x[30];
                    out.__sp = state.x[31];
                    out.__pc = state.pc;
                    return;
                }
            }
        }
    }
#endif
    // Diagnostic: which fatal signal actually arrives. Per-signal caps: TRAP
    // fires constantly (TLS emulation), the rest are rare.
    if (sig == SIGTRAP || sig == SIGILL || sig == SIGBUS || sig == SIGSEGV ||
        sig == SIGABRT) {
        static std::atomic<int> s_trapSeen{0};
        static std::atomic<int> s_fatalSeen{0};
        bool log = false;
        if (sig == SIGTRAP) {
            if (s_trapSeen.load() < 3) {
                ++s_trapSeen;
                log = true;
            }
        } else if (s_fatalSeen.load() < 8) {
            ++s_fatalSeen;
            log = true;
        }
        if (log) {
            char line[384];
            int n = std::snprintf(line, sizeof(line), "fatal signal %d", sig);
            (void)n;  // consumed only under the arm64 detail block below
            // The android log is the only channel a captured run reliably shows, while
            // the fault identity (si_addr, pc, lr) normally lives only in the on-device
            // breadcrumb file. Detail here costs nothing extra for a hardware fault:
            // scalar reads plus the try-lock guest-module lookup already used by the
            // fatal-signal breadcrumb below. SIGTRAP stays bare: it fires per TLS
            // emulation and its pc is KuDroid's own trap site.
#if (defined(__aarch64__) || defined(__arm64__)) && (defined(__APPLE__) || defined(__linux__))
            if (sig != SIGTRAP && n > 0 && static_cast<size_t>(n) < sizeof(line)) {
                if (info != nullptr) {
                    n += std::snprintf(line + n, sizeof(line) - static_cast<size_t>(n),
                                       " fault=%p", info->si_addr);
                }
                if (ucontext != nullptr &&
                    n > 0 && static_cast<size_t>(n) < sizeof(line)) {
                    uint64_t tpc = 0, tlr = 0;
#if defined(__APPLE__)
                    const ucontext_t* uc = static_cast<const ucontext_t*>(ucontext);
                    tpc = uc->uc_mcontext->__ss.__pc;
                    tlr = uc->uc_mcontext->__ss.__lr;
#elif defined(__linux__)
                    const ucontext_t* uc = static_cast<const ucontext_t*>(ucontext);
                    tpc = uc->uc_mcontext.pc;
                    tlr = uc->uc_mcontext.regs[30];
#endif
                    // Short buffers on purpose: this line goes to logcat, and the
                    // module name is what identifies the fault. A long install path
                    // no longer costs the name (the lookup falls back to the file
                    // name), and a lookup that still fails is labelled from the
                    // lock-free range test instead of being called host text.
                    char pcMod[96] = {0};
                    if (tpc != 0) {
                        if (!kudroid::kudroid_lookup_guest_module(
                                reinterpret_cast<void*>(tpc), pcMod, sizeof(pcMod))) {
                            std::snprintf(pcMod, sizeof(pcMod), "0x%llx (%s)",
                                          static_cast<unsigned long long>(tpc),
                                          kudroid::kudroid_guest_module_contains(
                                              reinterpret_cast<void*>(tpc))
                                              ? "guest"
                                              : "host");
                        }
                    }
                    char lrMod[96] = {0};
                    if (tlr != 0) {
                        if (!kudroid::kudroid_lookup_guest_module(
                                reinterpret_cast<void*>(tlr), lrMod, sizeof(lrMod))) {
                            std::snprintf(lrMod, sizeof(lrMod), "0x%llx (%s)",
                                          static_cast<unsigned long long>(tlr),
                                          kudroid::kudroid_guest_module_contains(
                                              reinterpret_cast<void*>(tlr))
                                              ? "guest"
                                              : "host");
                        }
                    }
                    if (pcMod[0] != '\0' || lrMod[0] != '\0') {
                        std::snprintf(line + n, sizeof(line) - static_cast<size_t>(n),
                                      " pc=%s lr=%s",
                                      pcMod[0] != '\0' ? pcMod : "?",
                                      lrMod[0] != '\0' ? lrMod : "?");
                    }
                }
            }
#endif
            signal_safe_log("KuDroidTrap", line);
#if defined(__APPLE__) && defined(__arm64__)
            if (sig == SIGBUS || sig == SIGSEGV) {
                log_fault_context(ucontext, info != nullptr ? info->si_addr : nullptr);
            }
#endif
        }
    }
    if (sig == SIGTRAP) {
        if (kudroid::bionic_handle_tpidr_trap(ucontext)) {
            return; // handled successfully, resuming execution!
        }
        if (kudroid::bionic_handle_sysreg_trap(ucontext)) {
            static std::atomic<int> s_sysregTrapEmulated{0};
            if (s_sysregTrapEmulated.load() < 5) {
                ++s_sysregTrapEmulated;
                signal_safe_log("KuDroidTrap", "SIGTRAP emulated as ARM64 sysreg");
            }
            return;
        }
        if (kudroid::bionic_handle_fmod_vorbis_trap(ucontext)) {
            return;
        }
        if (kudroid::bionic_handle_jit26_trap(ucontext)) {
            static std::atomic<int> s_jit26Seen{0};
            if (s_jit26Seen.load() < 5) {
                ++s_jit26Seen;
                signal_safe_log("KuDroidTrap",
                                "JIT26 prepare/detach trap with no script; "
                                "stepped over (unprepared)");
            }
            return;
        }
#if defined(__APPLE__) && defined(__arm64__)
        if (ucontext != nullptr) {
            const auto& state = static_cast<const ucontext_t*>(ucontext)->uc_mcontext->__ss;
            uint64_t registers[8];
            for (unsigned i = 0; i < 8; ++i) registers[i] = state.__x[i];
            kudroid::record_trap_diagnostics(sig, state.__pc, state.__lr, state.__sp, registers);
        }
#endif
        static std::atomic<int> s_trapUnhandled{0};
        if (s_trapUnhandled.load() < 8) {
            ++s_trapUnhandled;
            signal_safe_log("KuDroidTrap", "SIGTRAP not emulated");
        }
        // A SIGTRAP that is not one of KuDroid's TLS breakpoints belongs to whoever
        // installed a handler before us — a debugger bridge, a crash reporter, or
        // LiveContainer's own dyld interception, all of which use breakpoints for
        // their own purposes. Reporting it as a KuDroid crash would both lose their
        // event and produce a misleading log.
        if (chainToPreviousHandler(sig)) {
            return;
        }
    }
    // The same TLS breakpoints can arrive as SIGILL (observed at teardown while
    // startup traps arrive as SIGTRAP). Pattern-gated like above: a genuine
    // illegal instruction never matches our BRK range and falls through to the
    // crash reporter untouched.
    if (sig == SIGILL) {
        if (kudroid::bionic_handle_tpidr_trap(ucontext)) {
            static std::atomic<int> s_illEmulated{0};
            if (s_illEmulated.load() < 5) {
                ++s_illEmulated;
                signal_safe_log("KuDroidTrap", "SIGILL emulated as TPIDR");
            }
            return;
        }
        if (kudroid::bionic_handle_sysreg_trap(ucontext)) {
            static std::atomic<int> s_sysregIllEmulated{0};
            if (s_sysregIllEmulated.load() < 5) {
                ++s_sysregIllEmulated;
                signal_safe_log("KuDroidTrap", "SIGILL emulated as ARM64 sysreg");
            }
            return;
        }
        if (kudroid::bionic_handle_fmod_vorbis_trap(ucontext)) {
            return;
        }
#if defined(__APPLE__) && defined(__arm64__)
        if (ucontext != nullptr) {
            const auto& state = static_cast<const ucontext_t*>(ucontext)->uc_mcontext->__ss;
            uint64_t registers[8];
            for (unsigned i = 0; i < 8; ++i) registers[i] = state.__x[i];
            kudroid::record_trap_diagnostics(sig, state.__pc, state.__lr, state.__sp, registers);
        }
#endif
    }

    // Record the fault BEFORE anyone gets a chance to swallow it.
    //
    // Everything below this point can return without writing a thing: the guest's own
    // handler may report the fault true and resume, and the JNI_OnLoad shield jumps out
    // entirely. Both are correct behaviours and both used to leave no KuDroid-side trace
    // at all.
    //
    // That cost a full round. One run took a SIGSEGV at 0x64696f72646e6140 that only
    // exists in the log as libunity's own tombstone text, arriving through
    // __android_log_print with no thread, no KuDroid context and no way to place it in
    // the timeline — while KuDroid's crash log described a different, later fault and
    // presented it as the crash. One line here, in the same file as the watchdog and the
    // thread samples, is what makes the order of events readable.
    //
    // It carries pc/lr and the abort message for the same reason, learned the same way.
    // A guest handler that does not return reaches none of the reporting below: the
    // ULTRAKILL run that took sig=6 with guest_handler=1 wrote no crash log at all,
    // because il2cpp's SIGABRT handler siglongjmps out to its own recovery point. The
    // fault address for an abort() is just the pc of abort inside libsystem and says
    // nothing; pc, lr and the module they fall in are what identify the caller, and
    // android_set_abort_message is the guest's own statement of what went wrong. All of
    // it was already in memory and none of it was written anywhere.
    //
    // Everything here is async-signal-safe: reads of scalars, snprintf into a local,
    // kudroid_lookup_guest_module (a try_lock that gives up rather than blocking) and
    // one O_APPEND write.
    {
        char mark[1536];
        char tname[64] = "?";
#if defined(__APPLE__)
        pthread_getname_np(pthread_self(), tname, sizeof(tname));
#else
        (void)pthread_getname_np(pthread_self(), tname, sizeof(tname));
#endif
        // Signalled (raise/pthread_kill/sigqueue) vs faulted (hardware): si_code is the
        // only field that tells them apart, and which side decided to abort is exactly
        // what a SIGABRT with pc inside libsystem leaves ambiguous.
        int si_code_snap = 0;
        if (info != nullptr) si_code_snap = info->si_code;
        g_crash_signal_si_code = si_code_snap;

        // pc/lr, and the guest module they land in.
        //
        // Named pc_mod/lr_mod rather than symbolicated: symbolicateAddr falls through to
        // dladdr, which takes the dynamic linker's lock and is not safe here. The guest
        // module lookup is, and a module+offset is what actually needs decoding offline
        // anyway — a raw address alone cannot even be attributed to a library.
        uint64_t pc = 0, lr = 0;
        bool have_pc = false;
        char pc_mod[256] = {0};
        char lr_mod[256] = {0};
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            const ucontext_t* uc = static_cast<const ucontext_t*>(ucontext);
            pc = uc->uc_mcontext->__ss.__pc;
            lr = uc->uc_mcontext->__ss.__lr;
            have_pc = true;
        }
#elif defined(__aarch64__) && defined(__linux__)
        if (ucontext != nullptr) {
            const ucontext_t* uc = static_cast<const ucontext_t*>(ucontext);
            pc = uc->uc_mcontext.pc;
            lr = uc->uc_mcontext.regs[30];
            have_pc = true;
        }
#endif
        if (have_pc) {
            if (!kudroid::kudroid_lookup_guest_module(reinterpret_cast<void*>(pc),
                                                     pc_mod, sizeof(pc_mod))) {
                snprintf(pc_mod, sizeof(pc_mod), "0x%llx (host or unknown)",
                         (unsigned long long)pc);
            }
            if (!kudroid::kudroid_lookup_guest_module(reinterpret_cast<void*>(lr),
                                                     lr_mod, sizeof(lr_mod))) {
                snprintf(lr_mod, sizeof(lr_mod), "0x%llx (host or unknown)",
                         (unsigned long long)lr);
            }
        }

        // The first GUEST frame above the fault, which for an abort() is the only thing
        // that identifies the caller.
        //
        // pc and lr are not enough and cannot be. The captured ULTRAKILL run reported
        // `pc=0x1da7b81dc (host or unknown) lr=0x213e07c1c (host or unknown)`: pc was
        // exactly the fault address, both were inside libsystem, and no frame belonged to
        // libil2cpp — because abort() and _sigtramp sit between the handler and whoever
        // decided to abort. Adding pc/lr answered the question by showing it was the
        // wrong question.
        //
        // Walking finds the frame that matters. Every step is bounded by this thread's
        // REAL stack — queried through pthread_get_stackaddr_np/get_stacksize_np rather
        // than a plausibility test — because a diagnostic that faults takes down the
        // process it was supposed to explain. That is not hypothetical: the run after this
        // one was added died inside the guard diagnostic, which still used a plausibility
        // test, at fault_addr=0x100000000050. FrameWalker is the shared implementation
        // both now use.
        char guest_frame[256] = {0};
        int guest_frame_depth = -1;
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            const ucontext_t* uc = static_cast<const ucontext_t*>(ucontext);
            kudroid::FrameWalker walker(static_cast<uintptr_t>(uc->uc_mcontext->__ss.__fp),
                                        kudroid::query_thread_stack_bounds());
            for (int depth = 0; depth < 24 && walker.valid(); ++depth) {
                uintptr_t ret = 0;
                if (!walker.return_address(&ret)) break;
                if (kudroid::kudroid_lookup_guest_module(reinterpret_cast<void*>(ret),
                                                        guest_frame, sizeof(guest_frame))) {
                    guest_frame_depth = walker.depth();
                    break;
                }
                if (!walker.next()) break;
            }
        }
#endif

        // Field names are uniform with the rest of this file on purpose. `sig=` already
        // means a Java type signature in native-enter/native-exit — 76 lines of one run
        // used it that way against this line's one — so grepping `sig=` for signals
        // returns almost entirely the wrong records. `signo=` is unambiguous, and
        // `thread_id=` is the numeric id every other breadcrumb prints, which is what
        // makes this line joinable to the watchdog, the stall reports and the thread
        // samples instead of only readable next to them.
        int n = snprintf(mark, sizeof(mark),
                         "fatal-signal signo=%d si_code=%d fault_addr=%p thread=%s "
                         "thread_id=%llu guest_handler=%d jni_guard=%d",
                         sig,
                         si_code_snap,
                         info != nullptr ? info->si_addr : nullptr,
                         tname[0] != '\0' ? tname : "?",
                         currentThreadIdForCrash(),
                         kudroid::guest_signal_has_handler(sig) ? 1 : 0,
                         g_jniGuardActive ? 1 : 0);
        if (n > 0 && static_cast<size_t>(n) < sizeof(mark) && have_pc) {
            n += snprintf(mark + n, sizeof(mark) - static_cast<size_t>(n),
                          " pc=%s lr=%s", pc_mod, lr_mod);
        }
        // The guest frame, when the walk found one. `frame_depth` says how many host
        // frames sat between the signal and it, which is what distinguishes "the guest
        // faulted" (depth 0-1) from "the guest called into libsystem and that aborted"
        // (deeper) — the case pc/lr cannot describe at all.
        if (n > 0 && static_cast<size_t>(n) < sizeof(mark) && guest_frame_depth >= 0) {
            n += snprintf(mark + n, sizeof(mark) - static_cast<size_t>(n),
                          " guest_frame=%s frame_depth=%d", guest_frame, guest_frame_depth);
        } else if (n > 0 && static_cast<size_t>(n) < sizeof(mark) && have_pc) {
            n += snprintf(mark + n, sizeof(mark) - static_cast<size_t>(n),
                          " guest_frame=none-in-24-frames");
        }
        // The guest's own words, last, because it is the only variable-length part and
        // the fields above must not be pushed out by a long message.
        if (n > 0 && static_cast<size_t>(n) < sizeof(mark) && g_abortMessage[0] != '\0') {
            snprintf(mark + n, sizeof(mark) - static_cast<size_t>(n),
                     " abort_message=\"%.512s\"", g_abortMessage);
        }
        kudroid_persistent_breadcrumb(mark);
    }

    // Stop the watchdog timing this thread's work, HERE — at the one point every path
    // passes through.
    //
    // This used to sit further down, past the guest-handler dispatch and past the
    // JNI_OnLoad shield, and that was the same mistake twice over: those are exits, not
    // the entry. A guest handler that does not return — il2cpp's SIGABRT handler
    // siglongjmps out — skipped it entirely, so the last run reported
    // `native_call_id=14 native_elapsed_ms=9429` for nine seconds after UnityMain had
    // taken the signal, timing a call whose thread was gone. The `fatal-signal` line
    // above was written from the right place and was the only reason that was visible.
    //
    // The cost of being early is a false positive: a fault the guest genuinely fixes and
    // resumes from also stops the watchdog. That is the right trade — a stopped watchdog
    // says "stopped, reason=fatal-signal" and can be reasoned about, while a watchdog
    // reporting a dead thread's call as a live hang sends the reader somewhere else
    // entirely. Two relaxed stores, safe in a signal handler.
    kudroid::native_note_fatal_signal(sig,
                                      static_cast<unsigned long long>(currentThreadIdForCrash()));

    // For worker threads (e.g. Unity Job.Worker), try instruction load/store skip FIRST.
    // Unity's crash handler writes a tombstone without fixing memory or moving PC, which
    // aborts the thread or crashes the process. Skipping the faulting instruction lets
    // worker jobs safely finish without bringing down the game.
    {
#if defined(__APPLE__)
        const bool isHostMain = pthread_main_np() != 0;
#else
        const bool isHostMain =
            g_mainThread != 0 && pthread_equal(pthread_self(), g_mainThread);
#endif
        const unsigned long long tid = currentThreadIdForCrash();
        WorkerBudget* budget =
            (!kudroid_fault_is_fatal(tid, isHostMain) && (sig == SIGSEGV || sig == SIGBUS))
                ? worker_budget_for(tid)
                : nullptr;
        unsigned long long faultPc = 0;
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            faultPc = static_cast<ucontext_t*>(ucontext)->uc_mcontext->__ss.__pc;
        }
#endif
        const unsigned long long faultAddr =
            info != nullptr ? reinterpret_cast<unsigned long long>(info->si_addr) : 0;
        // A dropped store one iteration earlier and this fault from the same loop
        // are one episode. Report the pair before the skip attempt decides whether
        // this fault is recovered too, so an episode that ends in a skip is still
        // visible in the crash log.
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (sig == SIGSEGV || sig == SIGBUS) {
            store_drop_report(tid, faultPc, faultAddr);
        }
#endif
        // Emergency stop for a walked-off-the-cliff walk: past 512 steps of
        // the same load over uncommitted memory the walk is not recovering
        // anything — every skip poisons one destination and walks further into
        // the reservation. Retire the skip path here (budget reads spent, so
        // the second block parks the thread like any other exhausted worker)
        // instead of feeding the storm thousands more skips and a SIGBUS at
        // the cliff edge. Cap < kMaxWalkRecoveries keeps ordinary bounded
        // walks (which terminate) unaffected.
        static constexpr int kMaxSaneWalk = 512;
        if (budget != nullptr &&
            budget->walk.load(std::memory_order_relaxed) >= kMaxSaneWalk) {
            budget = nullptr;
        }
        // Distributed-storm gate: per-thread budgets cannot see a lockstep
        // fleet walking one load. Retire the skip path for this pc process-
        // wide once its global budget is spent, so the fault falls through to
        // the fatal path and the shell sees the crash instead of a soft-locked
        // engine that is silently corrupting itself.
        if (budget != nullptr && !pc_skip_peek(faultPc)) {
            budget = nullptr;
        }
        if (worker_budget_peek(budget, faultPc, faultAddr) &&
            kudroid_try_skip_fault(sig, info, ucontext)) {
            const long long nowNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            worker_budget_note(budget, nowNs, faultPc, faultAddr);
            pc_skip_note(faultPc);
            return;
        }
    }

    // A SIGSEGV at pc=0 (or any unmapped low pc) after a null-probe skip means
    // the fabricated zero fed an indirect call: `blr xN` jumped to 0 and lr
    // points right after the call. Resuming is impossible (there is no
    // instruction to return to) and parking the thread without a report hides
    // the chain, so write the connection down and let the fatal path run —
    // the modal tells the user the skip caused the crash, not just "signal 11".
    // (Observed live, skip-tutorial: skip #1 fabricated x8=0; the very next
    // instruction was blr x8; crash pc=0x0, lr = skip_pc + 4, x8 = 0.)
    {
        unsigned long long crashPc = 0;
        unsigned long long crashLr = 0;
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            crashPc = static_cast<ucontext_t*>(ucontext)->uc_mcontext->__ss.__pc;
            crashLr = static_cast<unsigned long long>(
                static_cast<ucontext_t*>(ucontext)->uc_mcontext->__ss.__lr);
        }
#endif
        if ((sig == SIGSEGV || sig == SIGBUS) && crashPc < 0x10000) {
            char mark[192];
            const int m = std::snprintf(
                mark, sizeof(mark),
                "null-call crash: pc=0x%llx lr=0x%llx — indirect call through a "
                "pointer a null-probe skip fabricated",
                crashPc, crashLr);
            if (m > 0) kudroid_persistent_breadcrumb(mark);
            // Ensure the report below is treated as fatal regardless of role.
            g_hasCrashed.store(true);
            g_lastCrashTail[0] = '\0';
        }
    }

    // The guest's own handler, for the signals KuDroid must keep installed.
    //
    // Gate on the faulting pc being in a registered guest module. A fault in host
    // text (the shell itself, KuART) is a KuDroid bug and must go down the fatal
    // path below: handing it to the guest reporter makes the guest write a
    // tombstone for OUR bug and resume into the same fault — the screen freezes
    // with the crash logged but no modal, exactly the invisible-crash report.
    // (Observed: SIGSEGV in kuart::DexClass::IsSubClassOf while the guest had a
    // handler installed; the host fault never reached kudroid_crash handling.)
    {
        void* faultPcHost = nullptr;
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            faultPcHost = reinterpret_cast<void*>(
                static_cast<ucontext_t*>(ucontext)->uc_mcontext->__ss.__pc);
        }
#endif
        // Containment, not formatting. This gate used to ask for the module's text
        // into a 128-byte buffer, and the container's install path alone is longer
        // than that, so every guest fault in a real install answered "not in a
        // guest module" and took the host-fatal path below -- the guest's own
        // SIGSEGV handler never ran, and the report blamed host text. The lookup
        // also gives up rather than block on the loader mutex, which is the same
        // answer for the wrong reason. kudroid_guest_module_contains() answers from
        // a lock-free range mirror, so only a genuinely unmapped pc lands here.
        const bool pcInGuest = faultPcHost != nullptr &&
                               kudroid::kudroid_guest_module_contains(faultPcHost);
        if (!pcInGuest) {
            kudroid_persistent_breadcrumb(
                "host-pc fault: guest handler skipped, taking fatal path");
            goto host_fatal_path;
        }
    }
    if (kudroid::guest_signal_dispatch(sig, info, ucontext)) {
        // The guest's own crash reporter (Unity's tombstone) took the signal and
        // resumed, so the fatal path below never runs and kudroid_crash.log stays
        // empty — yet the app may be dying inside (frozen screen, dead input).
        // Leave a one-line record so a guest-handled fault is not invisible.
        if (sig == SIGSEGV || sig == SIGABRT || sig == SIGBUS) {
            static std::atomic<int> s_guestHandled{0};
            if (s_guestHandled.fetch_add(1, std::memory_order_relaxed) < 8) {
                char note[512];
                const uintptr_t fa =
                    info != nullptr ? reinterpret_cast<uintptr_t>(info->si_addr) : 0;
                int n = std::snprintf(note, sizeof(note),
                                      "guest-handler resolved fatal sig=%d fault=0x%llx\n",
                                      sig, static_cast<unsigned long long>(fa));
                if (n > 0) {
                    kudroid_persistent_breadcrumb(note);
                    // Also append to kudroid_crash.log itself.
                    char path[1200];
                    size_t dl = std::strlen(g_logDir);
                    if (dl >= sizeof(path) - 32) dl = sizeof(path) - 32;
                    std::memcpy(path, g_logDir, dl);
                    std::memcpy(path + dl, "/kudroid_crash.log", 19);
                    const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
                    if (fd >= 0) {
                        // snprintf's return is the length it would have written; passing
                        // it to write() unclamped reads past the buffer when the note is
                        // truncated. crashWriteLine exists exactly for this.
                        crashWriteLine(fd, note, n, sizeof(note));
                        ::close(fd);
                    }
                }
            }
        }
        kudroid_persistent_breadcrumb("fatal-signal resolved-by-guest-handler");
        // The guest handled it, but the faulting thread rarely continues usefully
        // (it resumed into the same fault last time this path ran). The shell's
        // 0.25s poll reads g_hasCrashed to show the modal and run teardown, so
        // a guest-handled fatal must not stay invisible.
        if (sig == SIGSEGV || sig == SIGABRT || sig == SIGBUS) {
            g_hasCrashed.store(true);
            g_lastCrashTail[0] = '\0';
            extractLastLines(g_crashBuf, (size_t)g_crashLen, g_lastCrashTail,
                             sizeof(g_lastCrashTail), 30);
        }
        return;
    }

host_fatal_path:;

    // Inside a guarded JNI_OnLoad invocation: if this library aborts/segfaults,
    // skip it rather than killing the entire process. Only async-signal-safe calls used here;
    // logging deferred to caller upon siglongjmp return.
    //
    // Capture the machine state first. This path never reaches the crash-log code
    // below, so without this a swallowed fault left no pc, no fault address and no
    // stack — only a WARNING line saying a signal happened. Copying scalars is
    // async-signal-safe; formatting and writing are left to the caller.
    if (g_jniGuardActive && (sig == SIGABRT || sig == SIGSEGV || sig == SIGBUS ||
                             sig == SIGILL  || sig == SIGTRAP)) {
        JniGuardFault& f = g_jniGuardFault;
        f.signal = sig;
        if (info != nullptr) {
            f.si_code = info->si_code;
            f.fault_addr = info->si_addr;
        }
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            ucontext_t* uc = static_cast<ucontext_t*>(ucontext);
            f.pc = uc->uc_mcontext->__ss.__pc;
            f.lr = uc->uc_mcontext->__ss.__lr;
            f.sp = uc->uc_mcontext->__ss.__sp;
            f.fp = uc->uc_mcontext->__ss.__fp;
            for (int i = 0; i < 9; ++i) f.x[i] = uc->uc_mcontext->__ss.__x[i];
            // Copy the stack words only when sp is genuinely inside this thread's stack.
            //
            // This used to test `sp > 0x1000 && sp < 0x7fffffffffff`, which excludes null
            // and nothing else — the same plausibility test that let the guard diagnostic
            // dereference 0x100000000018 and turned a diagnostic into the crash. Here the
            // consequence would be worse still: a double fault inside the signal handler,
            // which is precisely what the JNI_OnLoad guard exists to avoid.
            {
                const kudroid::StackBounds bounds = kudroid::query_thread_stack_bounds();
                const uintptr_t sp = static_cast<uintptr_t>(f.sp);
                const size_t want = sizeof(f.stack);
                if (bounds.valid && (sp & 0x7) == 0 && sp >= bounds.low &&
                    sp + want <= bounds.high) {
                    const uint64_t* stack = reinterpret_cast<const uint64_t*>(sp);
                    for (int i = 0; i < 32; ++i) f.stack[i] = stack[i];
                }
            }
            f.have_regs = true;
        }
#else
        (void)ucontext;
#endif
        g_jniGuardActive = 0;
        g_jniGuardSignal = sig;
        siglongjmp(g_jniGuardJmp, 1);
    }

    // Fast path for recoverable worker faults, BEFORE the crash dump below.
    //
    // A recovered fault killed nothing, but the dump path writes ~256KB per
    // fault (full report + log buffer + stderr tail): 16 recovered faults
    // produced a 4.7MB crash log in 25ms and saturated I/O for no diagnostic
    // gain. A skipped fault gets one breadcrumb and resumes; only faults
    // that actually park or crash pay for the full report.
    //
    // Placement is deliberate: after guest dispatch (Unity gets first
    // refusal) and after the JNI-OnLoad guard (which longjmps out when
    // active), but before fflush and the kudroid_crash.log dump. The
    // watchdog stop above already ran and stays — a skipped fault still
    // interrupted the call it is measured against.
    {
#if defined(__APPLE__)
        const bool isHostMain = pthread_main_np() != 0;
#else
        const bool isHostMain =
            g_mainThread != 0 && pthread_equal(pthread_self(), g_mainThread);
#endif
        const unsigned long long tid = currentThreadIdForCrash();
        WorkerBudget* budget =
            (!kudroid_fault_is_fatal(tid, isHostMain) && (sig == SIGSEGV || sig == SIGBUS))
                ? worker_budget_for(tid)
                : nullptr;
        unsigned long long faultPc = 0;
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            faultPc = static_cast<ucontext_t*>(ucontext)->uc_mcontext->__ss.__pc;
        }
#endif
        const unsigned long long faultAddr =
            info != nullptr ? reinterpret_cast<unsigned long long>(info->si_addr) : 0;
        if (worker_budget_peek(budget, faultPc, faultAddr) &&
            kudroid_try_skip_fault(sig, info, ucontext)) {
            // Per-thread progress check lives in worker_budget_note: a 30s+
            // gap since this thread's previous skip resets its budget, so a
            // long session does not die on an isolated fault #129. A storm
            // keeps its count and stays fatal.
            const long long nowNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            worker_budget_note(budget, nowNs, faultPc, faultAddr);
            return;
        }
    }

    if (g_logDir[0]) {
        // construct '<dir>/kudroid_crash.log' path without heap allocation.
        char path[1200];
        size_t dl = strlen(g_logDir);
        if (dl >= sizeof(path) - 32) dl = sizeof(path) - 32;
        memcpy(path, g_logDir, dl);
        const char* suffix = "/kudroid_crash.log";
        memcpy(path + dl, suffix, strlen(suffix) + 1);

        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            const char* hdr = "[kudroid_core] CRASH — fatal signal caught\n";
            (void)!write(fd, hdr, strlen(hdr));
            const char* stamp = kudroid_build_stamp();
            char stampLine[512];
            int sm = snprintf(stampLine, sizeof(stampLine), "build: %s\n", stamp);
            crashWriteLine(fd, stampLine, sm, sizeof(stampLine));
            char sigline[2048];
            int m = snprintf(sigline, sizeof(sigline), "signal = %d\n", sig);
            crashWriteLine(fd, sigline, m, sizeof(sigline));

            // Record faulting thread name for diagnostic context.
            {
                char tname[64] = "?";
#if defined(__APPLE__)
                pthread_getname_np(pthread_self(), tname, sizeof(tname));
#else
                (void)pthread_getname_np(pthread_self(), tname, sizeof(tname));
#endif
                m = snprintf(sigline, sizeof(sigline), "thread = %s (0x%llx)\n",
                             tname[0] ? tname : "?",
                             (unsigned long long)(uintptr_t)pthread_self());
                crashWriteLine(fd, sigline, m, sizeof(sigline));
            }

            // log faulting memory address
            if (info) {
                m = snprintf(sigline, sizeof(sigline),
                    "fault_addr = %p\nsi_code = %d\n",
                    info->si_addr, info->si_code);
                crashWriteLine(fd, sigline, m, sizeof(sigline));
                {
                    // Region and permissions of the faulting address: separates
                    // "mapped but not committed with this access" from "no such
                    // mapping" without needing a live debugger.
                    char cur[4] = "???", max[4] = "???";
                    uint64_t rbase = 0, rsize = 0;
                    if (kudroid::QueryRegionProt(info->si_addr, cur, max,
                                                 &rbase, &rsize)) {
                        m = snprintf(sigline, sizeof(sigline),
                            "fault_region = 0x%llx+0x%llx cur=%s max=%s\n",
                            (unsigned long long)rbase,
                            (unsigned long long)rsize, cur, max);
                    } else {
                        m = snprintf(sigline, sizeof(sigline),
                                     "fault_region = <none>\n");
                    }
                    crashWriteLine(fd, sigline, m, sizeof(sigline));
                }
            }

#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
            if (ucontext && info) {
                ucontext_t* uc = reinterpret_cast<ucontext_t*>(ucontext);
                const uint64_t pc = uc->uc_mcontext->__ss.__pc;
                char mod[256] = {0};
                if (!kudroid::kudroid_lookup_guest_module(reinterpret_cast<void*>(pc), mod, sizeof(mod))) {
                    m = snprintf(sigline, sizeof(sigline), "fault_skip_diag: pc not in guest module\n");
                } else if ((pc & 3) != 0) {
                    m = snprintf(sigline, sizeof(sigline), "fault_skip_diag: unaligned pc\n");
                } else {
                    const uint32_t w = *reinterpret_cast<const uint32_t*>(pc);
                    auto regVal = [&](unsigned r) -> uint64_t {
                        if (r == 31) return 0;
                        if (r == 30) return arm_thread_state64_get_lr(uc->uc_mcontext->__ss);
                        if (r == 29) return arm_thread_state64_get_fp(uc->uc_mcontext->__ss);
                        if (r > 28) return 0;
                        return uc->uc_mcontext->__ss.__x[r];
                    };
                    const unsigned rn = (w >> 5) & 31;
                    const uint64_t baseVal = (rn == 31) ? arm_thread_state64_get_sp(uc->uc_mcontext->__ss) : regVal(rn);
                    const uint64_t rmVal = regVal((w >> 16) & 31);
                    const kudroid::FaultSkipPlan p = kudroid::fault_decode_skip(w, pc, baseVal, rmVal);
                    const bool storeEligible =
                        !fault_addr_is_nullish(reinterpret_cast<uintptr_t>(info->si_addr)) &&
                        unmapped_addr_is_store_safe(reinterpret_cast<uintptr_t>(info->si_addr));
                    // Same-page tolerance as the decoder gate below: inside the
                    // first page the exact byte carries no signal (nothing is
                    // mapped there), so demanding equality here reported the
                    // wrong reason for plan-shaped null faults.
                    const uintptr_t fa = reinterpret_cast<uintptr_t>(info->si_addr);
                    const bool decodeExplains =
                        p.effAddr == fa || (p.effAddr < 0x1000u && fa < 0x1000u);
                    // Mirror of the branch-through-fabricated-register gate in
                    // fault_skip_load_store: a load whose result the next word
                    // branches through is refused even when the plan decodes —
                    // this crash was exactly that shape (ldr x8,[x9,#0x28];
                    // blr x8, x9=0). Without naming it, the diag printed the
                    // budget-cap line and sent the investigation the wrong way.
                    auto nextBranchesThroughPlanReg = [&]() {
                        if (!p.isLoad || p.rt >= 32) return false;
                        if ((pc & 0xFFFu) + 4 >= 0x1000u) return false;
                        const uint32_t next =
                            *reinterpret_cast<const uint32_t*>(pc + 4);
                        if (kudroid::fault_skip_branches_through(next, p.rt))
                            return true;
                        if (p.isPair && p.rt2 < 32 &&
                            kudroid::fault_skip_branches_through(next, p.rt2))
                            return true;
                        if (p.isVector) {
                            for (unsigned i = 1; i < p.rtCount && p.rt + i < 32; ++i) {
                                if (kudroid::fault_skip_branches_through(next,
                                                                         p.rt + i))
                                    return true;
                            }
                        }
                        return false;
                    };
                    if (!p.skippable) {
                        m = snprintf(sigline, sizeof(sigline), "fault_skip_diag: instruction 0x%08x not skippable\n", w);
                    } else if (nextBranchesThroughPlanReg()) {
                        m = snprintf(sigline, sizeof(sigline),
                                     "fault_skip_diag: next insn branches through fabricated r%u (call-via-null); real fault reported\n",
                                     p.rt);
                    } else if (p.isLoad && !p.isVector &&
                               (p.rt >= 29 || (p.isPair && p.rt2 >= 29))) {
                        m = snprintf(sigline, sizeof(sigline),
                                     "fault_skip_diag: fabricated destination r%u is not a transfer result register; real fault reported\n",
                                     p.rt >= 29 ? p.rt : p.rt2);
                    } else if (p.isLoad && !p.isVector &&
                               (p.rt == 18 || (p.isPair && p.rt2 == 18))) {
                        m = snprintf(sigline, sizeof(sigline),
                                     "fault_skip_diag: fabricated destination is the platform register r18; real fault reported\n");
                    } else if (!decodeExplains) {
                        m = snprintf(sigline, sizeof(sigline), "fault_skip_diag: effAddr 0x%llx != si_addr %p\n",
                                     (unsigned long long)p.effAddr, info->si_addr);
                    } else if (!fault_addr_is_nullish(
                                   reinterpret_cast<uintptr_t>(info->si_addr))) {
                        // The drop path admits stores only, so a decoded non-nullish
                        // load is refused by the load gate in kudroid_try_skip_fault
                        // and never reaches a drop. Reporting it through the store
                        // branch named a recovery the engine had already refused.
                        // Observed live: `ldr w12,[x2,x13,lsl #2]` with a garbage
                        // index into an address far outside every mapping, reported
                        // as an unmapped store. `rm` is the index register of the
                        // register-offset form, which is where a walk goes wrong.
                        if (p.isLoad) {
                            m = snprintf(sigline, sizeof(sigline),
                                         "fault_skip_diag: wild load, recovery refused"
                                         " (drop path is store-only) effAddr=0x%llx"
                                         " base=r%u:0x%llx rm=r%u:0x%llx\n",
                                         (unsigned long long)p.effAddr, rn,
                                         (unsigned long long)baseVal, (w >> 16) & 31u,
                                         (unsigned long long)rmVal);
                        } else if (storeEligible) {
                            m = snprintf(sigline, sizeof(sigline),
                                         "fault_skip_diag: unmapped store, drop path available"
                                         " effAddr=0x%llx base=r%u:0x%llx rm=r%u:0x%llx\n",
                                         (unsigned long long)p.effAddr, rn,
                                         (unsigned long long)baseVal, (w >> 16) & 31u,
                                         (unsigned long long)rmVal);
                        } else {
                            m = snprintf(sigline, sizeof(sigline),
                                         "fault_skip_diag: plan decodes, fault addr 0x%llx is not nullish"
                                         " (recovery refused) effAddr=0x%llx base=r%u:0x%llx\n",
                                         (unsigned long long)reinterpret_cast<uintptr_t>(info->si_addr),
                                         (unsigned long long)p.effAddr, rn,
                                         (unsigned long long)baseVal);
                        }
                    } else {
                        m = snprintf(sigline, sizeof(sigline), "fault_skip_diag: skippable plan ok (fatal thread or budget cap)\n");
                    }
                }
                crashWriteLine(fd, sigline, m, sizeof(sigline));
            }
#endif

            // in thanh ghi pc (arm64)
#if defined(__aarch64__) || defined(__arm64__)
            if (ucontext) {
#if defined(__APPLE__)
                ucontext_t* uc = (ucontext_t*)ucontext;
                uint64_t pc = uc->uc_mcontext->__ss.__pc;
                uint64_t lr = uc->uc_mcontext->__ss.__lr;
                uint64_t sp = uc->uc_mcontext->__ss.__sp;
                uint64_t fp = uc->uc_mcontext->__ss.__fp;
                uint64_t cpsr = uc->uc_mcontext->__ss.__cpsr;
                m = snprintf(sigline, sizeof(sigline),
                    "pc = 0x%llx\nlr = 0x%llx\nsp = 0x%llx\nfp = 0x%llx\ncpsr = 0x%llx\n",
                    (unsigned long long)pc, (unsigned long long)lr, (unsigned long long)sp,
                    (unsigned long long)fp, (unsigned long long)cpsr);
                crashWriteLine(fd, sigline, m, sizeof(sigline));

                // All 29 general-purpose registers, not the first nine.
                //
                // x0-x8 alone cannot identify the base register of a faulting access, and
                // that is the one thing a BUS_ADRALN or a bad load needs: the last round
                // reported fault_addr=0x16be80148 with the pointer it came from sitting in
                // x19 and x23, neither of which was printed. The tombstone libunity writes
                // to logcat has all of them, so the two logs described the same fault at
                // different levels of detail and the fuller one was the guest's.
                //
                // Four per line to keep the log readable and the snprintf well inside its
                // buffer.
                for (int r = 0; r < 29; r += 4) {
                    char regbuf[256];
                    int rn = 0;
                    if (r + 3 < 29) {
                        rn = snprintf(regbuf, sizeof(regbuf),
                                      "x%-2d = 0x%016llx  x%-2d = 0x%016llx  "
                                      "x%-2d = 0x%016llx  x%-2d = 0x%016llx\n",
                                      r,     (unsigned long long)uc->uc_mcontext->__ss.__x[r],
                                      r + 1, (unsigned long long)uc->uc_mcontext->__ss.__x[r + 1],
                                      r + 2, (unsigned long long)uc->uc_mcontext->__ss.__x[r + 2],
                                      r + 3, (unsigned long long)uc->uc_mcontext->__ss.__x[r + 3]);
                    } else {
                        rn = snprintf(regbuf, sizeof(regbuf), "x%-2d = 0x%016llx\n",
                                      r, (unsigned long long)uc->uc_mcontext->__ss.__x[r]);
                    }
                    crashWriteLine(fd, regbuf, rn, sizeof(regbuf));
                }

                // The faulting instruction itself.
                //
                // For SIGBUS/BUS_ADRALN the instruction is the whole answer and the
                // registers are only supporting evidence: the encoding names the base
                // register, the offset and the access width, which is what says whether the
                // pointer was wrong or the access was wider than the pointer's alignment.
                // Without it a misaligned 16-byte LDP and a misaligned 8-byte LDR look
                // identical in the log, and they have different causes.
                //
                // Reading at pc is safe — it was just executed, so the page is mapped and
                // readable. The neighbours are only read when they share pc's page, which
                // keeps a diagnostic from becoming a second fault at a page boundary.
                {
                    const uint64_t page = pc & ~static_cast<uint64_t>(0xFFF);
                    const uint32_t* at_pc = reinterpret_cast<const uint32_t*>(pc);
                    m = snprintf(sigline, sizeof(sigline),
                                 "\n--- instruction at pc ---\ninst = 0x%08x\n",
                                 (pc & 3) == 0 ? *at_pc : 0u);
                    crashWriteLine(fd, sigline, m, sizeof(sigline));
                    for (int d = -2; d <= 2; ++d) {
                        const uint64_t a = pc + static_cast<uint64_t>(d * 4);
                        if ((a & ~static_cast<uint64_t>(0xFFF)) != page) continue;
                        if ((a & 3) != 0) continue;
                        int n = snprintf(sigline, sizeof(sigline), "  %s0x%llx: 0x%08x\n",
                                         d == 0 ? "-> " : "   ",
                                         (unsigned long long)a,
                                         *reinterpret_cast<const uint32_t*>(a));
                        crashWriteLine(fd, sigline, n, sizeof(sigline));
                    }
                }

                // Where the fault address sits relative to this thread's stack.
                //
                // Last round's fault_addr was 20808 bytes ABOVE the value of
                // pthread_self() and the log could not say so, which left "stack overflow"
                // and "wrong pointer" equally consistent with the evidence. Printing the
                // bounds and the verdict answers it in the crash log instead of by hand.
                {
                    char* const stack_top =
                        static_cast<char*>(pthread_get_stackaddr_np(pthread_self()));
                    const size_t stack_size = pthread_get_stacksize_np(pthread_self());
                    char* const stack_low = stack_top - stack_size;
                    const char* fa = info != nullptr
                                         ? static_cast<const char*>(info->si_addr)
                                         : nullptr;
                    const char* verdict = "no fault address";
                    if (fa != nullptr) {
                        if (fa >= stack_low && fa < stack_top) verdict = "inside this thread's stack";
                        else if (fa >= stack_top)              verdict = "ABOVE the top of this thread's stack";
                        else if (fa >= stack_low - 0x10000)    verdict = "just BELOW the stack (guard page / overflow)";
                        else                                    verdict = "not on this thread's stack";
                    }
                    m = snprintf(sigline, sizeof(sigline),
                                 "\n--- stack bounds ---\nstack = [0x%llx, 0x%llx) size=%llu\n"
                                 "fault_addr is %s\n",
                                 (unsigned long long)(uintptr_t)stack_low,
                                 (unsigned long long)(uintptr_t)stack_top,
                                 (unsigned long long)stack_size, verdict);
                    crashWriteLine(fd, sigline, m, sizeof(sigline));
                }

                // Symbolicate PC/LR for meaningful source/function attribution.
                char symPc[512], symLr[512];
                symbolicateAddr((uintptr_t)pc, symPc, sizeof(symPc));
                symbolicateAddr((uintptr_t)lr, symLr, sizeof(symLr));
                m = snprintf(sigline, sizeof(sigline), "pc_sym: %s\nlr_sym: %s\n", symPc, symLr);
                crashWriteLine(fd, sigline, m, sizeof(sigline));

                // Raw stack dump from faulting SP to recover caller frames and register state.
                //
                // Bounded by the thread's real stack, and clamped rather than skipped: a
                // fault near the top of the stack still has useful words below sp, and
                // refusing the whole dump would lose them. This read had NO check at all —
                // 1024 bytes from whatever sp happened to be — which in a crash handler
                // means a double fault that destroys the report for the original crash.
                (void)!write(fd, "\n--- stack from sp ---\n", 22);
                {
                    const kudroid::StackBounds bounds = kudroid::query_thread_stack_bounds();
                    const uintptr_t spv = static_cast<uintptr_t>(sp);
                    int words = 0;
                    if (bounds.valid && (spv & 0x7) == 0 && spv >= bounds.low &&
                        spv < bounds.high) {
                        const uintptr_t available = bounds.high - spv;
                        words = static_cast<int>(available / sizeof(uint64_t));
                        if (words > 128) words = 128;
                        words &= ~3;  // whole rows of four, which is how it is printed
                    }
                    if (words == 0) {
                        int n0 = snprintf(sigline, sizeof(sigline),
                                          "sp=0x%llx is outside this thread's stack "
                                          "[0x%llx, 0x%llx) — not dumped\n",
                                          (unsigned long long)spv,
                                          (unsigned long long)bounds.low,
                                          (unsigned long long)bounds.high);
                        crashWriteLine(fd, sigline, n0, sizeof(sigline));
                    }
                    const uint64_t* stack = reinterpret_cast<const uint64_t*>(spv);
                    for (int i = 0; i < words; i += 4) {
                        int n = snprintf(sigline, sizeof(sigline),
                            "sp%+04d: %016llx  %016llx  %016llx  %016llx\n",
                            i * 8,
                            (unsigned long long)stack[i],
                            (unsigned long long)stack[i + 1],
                            (unsigned long long)stack[i + 2],
                            (unsigned long long)stack[i + 3]);
                        crashWriteLine(fd, sigline, n, sizeof(sigline));
                    }
                }

                // Guest return addresses among the words around sp, resolved to module+offset.
                // When a function returns through a corrupted frame, pc and lr are data and
                // the callers are only recoverable from what is left on the stack, including
                // the words just below sp that the dump above does not cover.
                {
                    const kudroid::StackBounds bounds = kudroid::query_thread_stack_bounds();
                    const uintptr_t spv = static_cast<uintptr_t>(sp);
                    if (bounds.valid && (spv & 0x7) == 0 && spv >= bounds.low &&
                        spv < bounds.high) {
                        constexpr uintptr_t kBelow = 0x400;
                        constexpr uintptr_t kAbove = 0x1800;
                        uintptr_t from = spv > bounds.low + kBelow ? spv - kBelow : bounds.low;
                        uintptr_t to = spv + kAbove < bounds.high ? spv + kAbove : bounds.high;
                        (void)!write(fd, "\n--- stack scan (guest code pointers) ---\n", 43);
                        int printed = 0;
                        for (uintptr_t a = from & ~uintptr_t(7); a + 8 <= to && printed < 40;
                             a += 8) {
                            const uint64_t v = *reinterpret_cast<const uint64_t*>(a);
                            char where[256];
                            if (v < 0x100000 ||
                                !kudroid::kudroid_lookup_guest_module(reinterpret_cast<void*>(v),
                                                                      where, sizeof(where))) {
                                continue;
                            }
                            int n = snprintf(sigline, sizeof(sigline), "sp%+05ld: %016llx %s\n",
                                             static_cast<long>(a) - static_cast<long>(spv),
                                             (unsigned long long)v, where);
                            crashWriteLine(fd, sigline, n, sizeof(sigline));
                            ++printed;
                        }
                    }
                }

                // Walk frame chain from faulting FP (ucontext) to locate __cxa_guard_acquire frame:
                // [fp+8] = saved LR (caller), [fp+56] = x19 = guard pointer.
                // slot56 (guard) — ch dump raw, decode offline.
                //
                // Through FrameWalker, bounded by this thread's real stack. The previous
                // version read p[0], p[1] and p[7] straight off any fp that passed
                // `> 0x1000 && < 0x7fffffffffff`, which is the same unchecked read that
                // crashed the guard diagnostic — and here it would fault INSIDE the crash
                // handler, losing the report for the original fault entirely.
                //
                // Bounds come from the faulting thread, which is this one: the handler
                // runs on the thread that took the signal.
                (void)!write(fd, "\n--- fp chain ---\n", 17);
                {
                    const kudroid::StackBounds bounds = kudroid::query_thread_stack_bounds();
                    kudroid::FrameWalker walker(static_cast<uintptr_t>(fp), bounds);
                    if (!walker.valid()) {
                        int n0 = snprintf(sigline, sizeof(sigline),
                                          "fp=0x%llx is outside this thread's stack "
                                          "[0x%llx, 0x%llx) — no chain to walk\n",
                                          (unsigned long long)fp,
                                          (unsigned long long)bounds.low,
                                          (unsigned long long)bounds.high);
                        crashWriteLine(fd, sigline, n0, sizeof(sigline));
                    }
                    for (int i = 0; i < 32 && walker.valid(); ++i) {
                        uintptr_t savedLr = 0;
                        const bool haveLr = walker.return_address(&savedLr);
                        uintptr_t slot56 = 0;
                        const bool haveSlot = walker.slot(56, &slot56);

                        char lrMod[256] = {0};
                        const bool inGuest =
                            haveLr && kudroid::kudroid_lookup_guest_module(
                                          reinterpret_cast<void*>(savedLr), lrMod, sizeof(lrMod));
                        // When not in a guest ELF it is host code; symbolicate it so aborts name the function.
                        char lrSym[512] = {0};
                        if (haveLr && !inGuest) {
                            symbolicateAddr(savedLr, lrSym, sizeof(lrSym));
                        }
                        const bool guardLike =
                            haveSlot && slot56 > 0x100000000ULL && slot56 < 0x7fffffffffffULL;
                        int n2 = snprintf(sigline, sizeof(sigline),
                            "fp%02d: f=0x%llx lr=0x%llx %s slot56=0x%llx%s\n",
                            i, (unsigned long long)walker.fp(),
                            (unsigned long long)savedLr,
                            haveLr ? (inGuest ? lrMod : lrSym) : "(no return address)",
                            (unsigned long long)slot56,
                            guardLike ? " <-- guard?" : (haveSlot ? "" : " (slot56 out of bounds)"));
                        crashWriteLine(fd, sigline, n2, sizeof(sigline));
                        if (!haveLr) break;
                        if (!walker.next()) break;
                    }
                }
#elif defined(__linux__)
                ucontext_t* uc = (ucontext_t*)ucontext;
                uint64_t pc = uc->uc_mcontext.pc;
                uint64_t lr = uc->uc_mcontext.regs[30];
                m = snprintf(sigline, sizeof(sigline),
                    "pc = 0x%llx\nlr = 0x%llx\n",
                    (unsigned long long)pc, (unsigned long long)lr);
                crashWriteLine(fd, sigline, m, sizeof(sigline));

                char symPc[512], symLr[512];
                symbolicateAddr((uintptr_t)pc, symPc, sizeof(symPc));
                symbolicateAddr((uintptr_t)lr, symLr, sizeof(symLr));
                m = snprintf(sigline, sizeof(sigline), "pc_sym: %s\nlr_sym: %s\n", symPc, symLr);
                crashWriteLine(fd, sigline, m, sizeof(sigline));
#endif
            }
#endif

            kudroid::kuart::WriteMethodResolutionTrace(fd, (uintptr_t)pthread_self());
            (void)!write(fd, "\n--- backtrace ---\n", 19);
            writeBacktrace(fd);

            if (g_abortMessage[0]) {
                (void)!write(fd, "\n--- abort message ---\n", 23);
                (void)!write(fd, g_abortMessage, strlen(g_abortMessage));
                (void)!write(fd, "\n", 1);
            }

            (void)!write(fd, "\n--- log up to crash ---\n", 25);
            (void)!write(fd, g_crashBuf, (size_t)g_crashLen);

            // Also dump the stderr tail (KuART abort reasons go there); all calls are async-signal-safe.
            {
                char errPath[1200];
                size_t dl = strlen(g_logDir);
                if (dl < sizeof(errPath) - 32) {
                    memcpy(errPath, g_logDir, dl);
                    memcpy(errPath + dl, "/stderr.log", 12);
                    int errFd = open(errPath, O_RDONLY);
                    if (errFd >= 0) {
                        const char* stderrHdr = "\n--- stderr tail (kuart abort reason) ---\n";
                        (void)!write(fd, stderrHdr, strlen(stderrHdr));
                        off_t errSize = lseek(errFd, 0, SEEK_END);
                        const off_t maxTail = 4096;
                        if (errSize > maxTail) {
                            lseek(errFd, -maxTail, SEEK_END);
                        } else {
                            lseek(errFd, 0, SEEK_SET);
                        }
                        char errBuf[512];
                        ssize_t errN;
                        while ((errN = read(errFd, errBuf, sizeof(errBuf))) > 0) {
                            (void)!write(fd, errBuf, (size_t)errN);
                        }
                        (void)!write(fd, "\n", 1);
                        close(errFd);
                    }
                }
            }

            const char* traceStr = kudroid::bionic_shim_trace();
            if (traceStr && *traceStr) {
                (void)!write(fd, "\n--- bionic shim trace ---\n", 27);
                (void)!write(fd, traceStr, strlen(traceStr));
            }

            close(fd);
        }
    }

    // Mark crashed state and keep the last 30 log lines for the Swift warning.
    //
    // Reaching here means the fault was NOT recovered by the fast path above
    // (fatal thread, over budget, unskippable instruction, or a non-memory
    // signal): the full report above was worth writing. Fatal threads stop
    // the app; anything else parks with a warning breadcrumb.
    {
#if defined(__APPLE__)
        const bool isHostMain = pthread_main_np() != 0;
#else
        const bool isHostMain =
            g_mainThread != 0 && pthread_equal(pthread_self(), g_mainThread);
#endif
        const unsigned long long tid = currentThreadIdForCrash();
        // Fatal threads never consume a budget slot; workers look theirs up
        // (claiming if new). No slot left fails closed: spent reads as max.
        const bool roleFatal = kudroid_fault_is_fatal(tid, isHostMain);
        WorkerBudget* budget = roleFatal ? nullptr : worker_budget_for(tid);
        const int spent =
            budget != nullptr ? budget->count.load(std::memory_order_relaxed) : kMaxWorkerRecoveries;
        // Walk storm = fatal. The walk budget lets one bounded table walk live
        // thousands of skips long, but a walk that far exceeds any plausible
        // relocation table has a broken source pointer (observed live: 4000+
        // skips at one pc over an uncommitted 0x392000000 region, faulting
        // every 0x80 bytes) — report it as a crash so the shell shows the
        // modal instead of leaving a dead worker inside a running app.
        const int walked = budget != nullptr ? budget->walk.load(std::memory_order_relaxed) : 0;
        // A crash whose pc sits in the first page is an indirect call through a
        // null pointer — with lr pointing right after a `blr`, that call was
        // fed by a fabricated zero from the null-probe skip above. The thread
        // cannot resume (there is nothing to return to), so a parked-worker
        // budget is meaningless: report the crash so the modal appears and the
        // teardown runs instead of freezing the game behind a dead worker.
        unsigned long long fatalPc = 0;
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            fatalPc = static_cast<ucontext_t*>(ucontext)->uc_mcontext->__ss.__pc;
        }
#endif
        const bool nullCall =
            (sig == SIGSEGV || sig == SIGBUS) && fatalPc != 0 && fatalPc < 0x10000;
        const bool fatal = roleFatal || spent >= kMaxWorkerRecoveries ||
                           walked >= kMaxWalkRecoveries || nullCall;
        if (fatal) {
            g_hasCrashed.store(true);
            g_lastCrashTail[0] = '\0';  // filled below from g_crashBuf
        } else {
            // The parked worker below never runs again: the engine deadlocks the
            // first time it synchronises on this thread's fence (observed: frames
            // keep swapping for two minutes, then the screen freezes with three
            // more threads dead and no modal). A parked thread is a dead thread —
            // the session is over, so report it as a crash instead of timing it.
            g_hasCrashed.store(true);
            g_lastCrashTail[0] = '\0';
            if (budget != nullptr) budget->count.fetch_add(1, std::memory_order_relaxed);
            char mark[256];
            const int n = snprintf(
                mark, sizeof(mark),
                "worker-fault-isolated signo=%d thread_id=%llu "
                "faults_so_far=%d (parked; session reported as crashed)",
                sig, tid, spent + 1);
            if (n > 0) kudroid_persistent_breadcrumb(mark);
        }
    }

    // Collect the last 30 log lines.
    extractLastLines(g_crashBuf, (size_t)g_crashLen, g_lastCrashTail, sizeof(g_lastCrashTail), 30);

    // If the tail is too short, append signal and PC.
    if (strlen(g_lastCrashTail) < 30) {
        char fallbackSummary[512];
        snprintf(fallbackSummary, sizeof(fallbackSummary),
                 "[Crash Signal: %d] Fault at %p (Thread 0x%llx)",
                 sig, info ? info->si_addr : nullptr, (unsigned long long)(uintptr_t)pthread_self());
        strncat(g_lastCrashTail, fallbackSummary, sizeof(g_lastCrashTail) - strlen(g_lastCrashTail) - 1);
    }

    // On a background thread: never lock a mutex in the signal handler.
#if defined(__APPLE__)
    const bool isBackground = !pthread_main_np();
#else
    const bool isBackground = g_mainThread != 0 && !pthread_equal(pthread_self(), g_mainThread);
#endif
    if (isBackground) {
        // A guest thread that faulted is parked here permanently so the launcher UI
        // stays alive and the Swift timer can show the crash modal.
        //
        // The loop is not cosmetic. pause() returns -1/EINTR as soon as ANY handler on
        // this thread returns, and falling out of it returns from crashHandler, which
        // resumes the faulting instruction and faults again — an endless fault → handler
        // → pause → fault cycle on a thread that is already dead. Each pass appends
        // another crash block, which is why one run produced two unrelated-looking
        // tombstones for the same thread and no way to tell which fault came first.
        //
        // Blocking every signal first means the next fault on this thread cannot even
        // re-enter the handler: the thread stops here, once, for good.
        sigset_t block_all;
        sigfillset(&block_all);
        pthread_sigmask(SIG_BLOCK, &block_all, nullptr);
        // Parking the worker alone leaves the engine running on a dead thread:
        // the frames log keeps scrolling while the screen is frozen and no modal
        // ever appears, because the shell's poll checks g_hasCrashed but the
        // main thread never observes the crash itself. Everything that reaches
        // this park has already written its report and set g_hasCrashed (both
        // the fatal branch and the worker-isolated branch above), so wake the
        // main thread: its wait fails with EINTR and the loop reaches the modal
        // path with the report on disk.
        // Only inside a live guest session (kudroid_set_log_dir also records a
        // main thread in hosts that merely exercise this reporter, e.g. the
        // breadcrumb test, and those depend on the park-and-hold behaviour).
        // Only into a disposition we own or the default: the guest sigaction
        // shim installs its trampoline over ours without chaining, and waking
        // into a foreign handler would run guest/debugger code on the main
        // thread instead of interrupting it. Default still terminates, which
        // beats a freeze with the report already on disk.
        if (g_hasCrashed.load(std::memory_order_relaxed) && s_isApkRunning.load(std::memory_order_relaxed) &&
            g_mainThread != 0 && !pthread_equal(pthread_self(), g_mainThread)) {
            struct sigaction cur;
            std::memset(&cur, 0, sizeof(cur));
            const bool known = ::sigaction(SIGUSR2, nullptr, &cur) == 0;
            const bool ours = known && (cur.sa_flags & SA_SIGINFO) == 0 &&
                              cur.sa_handler == kudroid_wake_handler;
            const bool dfl = known && cur.sa_handler == SIG_DFL;
            if (ours || dfl) {
                pthread_kill(g_mainThread, SIGUSR2);
            }
        }
        for (;;) {
            pause();
            // pause() also returns for a signal that is merely unblocked-and-ignored,
            // so sleeping keeps this from becoming a spin if that ever happens.
            struct timespec forever = {3600, 0};
            nanosleep(&forever, nullptr);
        }
    } else {
        // Crash on the main thread itself: restore default and re-raise so the
        // process dies at once instead of parking the thread the shell runs on.
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

