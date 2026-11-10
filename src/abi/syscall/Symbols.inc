const SymbolEntry kSyscallSymbols[] = {
    {"__register_atfork", reinterpret_cast<void*>(&bionic_register_atfork)},
    {"pthread_create", reinterpret_cast<void*>(&bionic_pthread_create)},
    {"pthread_attr_init", reinterpret_cast<void*>(&bionic_pthread_attr_init)},
    {"pthread_attr_destroy", reinterpret_cast<void*>(&bionic_pthread_attr_destroy)},
    {"pthread_attr_setstacksize", reinterpret_cast<void*>(&bionic_pthread_attr_setstacksize)},
    {"pthread_attr_getstack", reinterpret_cast<void*>(&bionic_pthread_attr_getstack)},
    {"pthread_attr_setdetachstate", reinterpret_cast<void*>(&bionic_pthread_attr_setdetachstate)},
    {"pthread_getattr_np", reinterpret_cast<void*>(&bionic_pthread_getattr_np)},
    {"pthread_condattr_init", reinterpret_cast<void*>(&bionic_pthread_condattr_init)},
    {"pthread_condattr_destroy", reinterpret_cast<void*>(&bionic_pthread_condattr_destroy)},
    {"pthread_mutexattr_init", reinterpret_cast<void*>(&bionic_pthread_mutexattr_init)},
    {"pthread_mutexattr_destroy", reinterpret_cast<void*>(&bionic_pthread_mutexattr_destroy)},
    {"pthread_mutexattr_settype", reinterpret_cast<void*>(&bionic_pthread_mutexattr_settype)},
    {"pthread_mutexattr_gettype", reinterpret_cast<void*>(&bionic_pthread_mutexattr_gettype)},
    {"pthread_mutex_trylock", reinterpret_cast<void*>(&bionic_pthread_mutex_trylock)},
    {"pthread_key_create", reinterpret_cast<void*>(&bionic_pthread_key_create)},
    {"pthread_setspecific", reinterpret_cast<void*>(&bionic_pthread_setspecific)},
    {"pthread_getspecific", reinterpret_cast<void*>(&bionic_pthread_getspecific)},
    {"pthread_key_delete", reinterpret_cast<void*>(&bionic_pthread_key_delete)},
    {"pthread_once", reinterpret_cast<void*>(&bionic_pthread_once)},
    {"sigaction", reinterpret_cast<void*>(&bionic_sigaction)},
    {"sigaltstack", reinterpret_cast<void*>(&bionic_sigaltstack)},
    // rt_sigaction is the symbol bionic's own sigaction() is built on, and some guests
    // import it directly.
    {"rt_sigaction", reinterpret_cast<void*>(&bionic_rt_sigaction)},
    // The OUTBOUND half of signal translation, which was missing entirely.
    //
    // sigaction translated the number on the way in, so a handler for Linux SIGPWR (30)
    // was installed on the host signal that carries it — Darwin's SIGINFO (29). raise,
    // kill and pthread_kill were not here at all, so they bound to the HOST libc through
    // dlsym(RTLD_DEFAULT) and the guest's Linux number went straight through:
    // pthread_kill(tid, 30) delivered Darwin signal 30, which is SIGUSR1, an unhandled
    // slot whose default action terminates the process.
    //
    // That is Mono's SIG_SUSPEND — how il2cpp stops threads for GC. Twelve of the
    // thirty-one named signals are misdirected this way, and neither side can see it: the
    // install succeeds and so does the send.
    {"raise", reinterpret_cast<void*>(&bionic_raise)},
    {"kill", reinterpret_cast<void*>(&bionic_kill)},
    {"pthread_kill", reinterpret_cast<void*>(&bionic_pthread_kill)},
    {"tkill", reinterpret_cast<void*>(&bionic_tkill)},
    {"abort", reinterpret_cast<void*>(&bionic_abort)},
    {"__libcpp_verbose_abort", reinterpret_cast<void*>(&bionic___libcpp_verbose_abort)},
    {"__assert_fail", reinterpret_cast<void*>(&bionic___assert_fail)},
    {"__assert_rtn", reinterpret_cast<void*>(&bionic___assert_rtn)},
    {"__assert2", reinterpret_cast<void*>(&bionic___assert2)},
    {"signal", reinterpret_cast<void*>(&bionic_signal)},
    {"bsd_signal", reinterpret_cast<void*>(&bionic_signal)},
    {"sysv_signal", reinterpret_cast<void*>(&bionic_signal)},
    // Masks: both the signal numbers in the set AND `how` differ. Linux numbers
    // SIG_BLOCK/UNBLOCK/SETMASK 0/1/2, Darwin numbers them 1/2/3, so an unshimmed
    // sigprocmask does not fail — it performs a different operation. A runtime that
    // blocks its suspend signal and unblocks it after ends up blocking it twice.
    {"sigprocmask", reinterpret_cast<void*>(&bionic_sigprocmask)},
    {"rt_sigprocmask", reinterpret_cast<void*>(&bionic_sigprocmask)},
    {"pthread_sigmask", reinterpret_cast<void*>(&bionic_pthread_sigmask)},
    {"sigsuspend", reinterpret_cast<void*>(&bionic_sigsuspend)},
    {"rt_sigsuspend", reinterpret_cast<void*>(&bionic_sigsuspend)},
    {"sigwait", reinterpret_cast<void*>(&bionic_sigwait)},
    // sigset_t operations on the GUEST's mask. Bit (n-1) is Linux signal n; the host's
    // macros number their bits by host signal, so binding these to the host would mix
    // the two schemes inside one word and have sigprocmask translate it a second time.
    {"sigemptyset", reinterpret_cast<void*>(&bionic_sigemptyset)},
    {"sigfillset", reinterpret_cast<void*>(&bionic_sigfillset)},
    {"sigaddset", reinterpret_cast<void*>(&bionic_sigaddset)},
    {"sigdelset", reinterpret_cast<void*>(&bionic_sigdelset)},
    {"sigismember", reinterpret_cast<void*>(&bionic_sigismember)},
    {"futex", reinterpret_cast<void*>(&bionic_futex)},
    {"__futex", reinterpret_cast<void*>(&bionic_futex)},
    {"futex_time64", reinterpret_cast<void*>(&bionic_futex)},
    {"__futex_time64", reinterpret_cast<void*>(&bionic_futex)},
    {"mremap", reinterpret_cast<void*>(&bionic_mremap)},
    {"gettid", reinterpret_cast<void*>(&bionic_gettid)},
    {"syscall", reinterpret_cast<void*>(&bionic_syscall)},
    {"getauxval", reinterpret_cast<void*>(&bionic_getauxval)},
    {"sysconf", reinterpret_cast<void*>(&bionic_sysconf)},
    {"timerfd_create", reinterpret_cast<void*>(&bionic_timerfd_create)},
    {"timerfd_settime", reinterpret_cast<void*>(&bionic_timerfd_settime)},
    {"eventfd", reinterpret_cast<void*>(&bionic_eventfd)},
    {"epoll_create", reinterpret_cast<void*>(&bionic_epoll_create)},
    {"epoll_create1", reinterpret_cast<void*>(&bionic_epoll_create1)},
    {"epoll_ctl", reinterpret_cast<void*>(&bionic_epoll_ctl)},
    {"epoll_wait", reinterpret_cast<void*>(&bionic_epoll_wait)},
    {"pthread_cond_init", reinterpret_cast<void*>(&bionic_pthread_cond_init)},
    {"pthread_cond_destroy", reinterpret_cast<void*>(&bionic_pthread_cond_destroy)},
    {"pthread_cond_wait", reinterpret_cast<void*>(&bionic_pthread_cond_wait)},
    {"pthread_cond_timedwait", reinterpret_cast<void*>(&bionic_pthread_cond_timedwait)},
    {"pthread_cond_signal", reinterpret_cast<void*>(&bionic_pthread_cond_signal)},
    {"pthread_cond_broadcast", reinterpret_cast<void*>(&bionic_pthread_cond_broadcast)},
    {"pthread_rwlock_init", reinterpret_cast<void*>(&bionic_pthread_rwlock_init)},
    {"pthread_rwlock_destroy", reinterpret_cast<void*>(&bionic_pthread_rwlock_destroy)},
    {"pthread_rwlock_rdlock", reinterpret_cast<void*>(&bionic_pthread_rwlock_rdlock)},
    {"pthread_rwlock_wrlock", reinterpret_cast<void*>(&bionic_pthread_rwlock_wrlock)},
    {"pthread_rwlock_unlock", reinterpret_cast<void*>(&bionic_pthread_rwlock_unlock)},
    
#ifdef __APPLE__
    {"__errno", reinterpret_cast<void*>(&__error)},
#else
    {"__errno", reinterpret_cast<void*>(&__errno_location)},
#endif
    // Variadic: routed through a register-capturing trampoline on arm64, because a guest
    // built for Linux AAPCS64 puts varargs in x0-x7/v0-v7 while Apple's arm64 ABI puts
    // them all on the stack. Forwarding straight to the host's snprintf made it read the
    // stack and print unrelated data — plausible-looking numbers, never an error.
#if defined(__aarch64__)
    {"snprintf", reinterpret_cast<void*>(&kudroid_snprintf_trampoline)},
    {"sprintf", reinterpret_cast<void*>(&kudroid_sprintf_trampoline)},
#if defined(__APPLE__)
    {"swprintf", reinterpret_cast<void*>(&kudroid_swprintf_trampoline)},
    {"vswprintf", reinterpret_cast<void*>(&bionic_vswprintf)},
#endif
    {"fprintf", reinterpret_cast<void*>(&kudroid_fprintf_trampoline)},
    {"printf", reinterpret_cast<void*>(&kudroid_printf_trampoline)},
    {"__fprintf_chk", reinterpret_cast<void*>(&kudroid_fprintf_chk_trampoline)},
    {"__printf_chk", reinterpret_cast<void*>(&kudroid_printf_chk_trampoline)},
    {"asprintf", reinterpret_cast<void*>(&kudroid_asprintf_trampoline)},
    {"__asprintf_chk", reinterpret_cast<void*>(&kudroid_asprintf_chk_trampoline)},
    {"dprintf", reinterpret_cast<void*>(&kudroid_dprintf_trampoline)},
    {"__dprintf_chk", reinterpret_cast<void*>(&kudroid_dprintf_chk_trampoline)},
    {"__android_log_buf_print", reinterpret_cast<void*>(&kudroid_log_buf_print_trampoline)},
    {"__syslog_chk", reinterpret_cast<void*>(&kudroid_syslog_chk_trampoline)},
#else
    {"snprintf", reinterpret_cast<void*>(&snprintf)},
    {"sprintf", reinterpret_cast<void*>(&sprintf)},
#endif
    {"memcpy", reinterpret_cast<void*>(&memcpy)},

    {"malloc", reinterpret_cast<void*>(&bionic_malloc)},
    {"calloc", reinterpret_cast<void*>(&calloc)},
    {"realloc", reinterpret_cast<void*>(&realloc)},
    {"free", reinterpret_cast<void*>(&bionic_free)},
    {"mmap", reinterpret_cast<void*>(&bionic_mmap)},
    {"mmap64", reinterpret_cast<void*>(&bionic_mmap64)},
    {"mprotect", reinterpret_cast<void*>(&bionic_mprotect)},
    {"madvise", reinterpret_cast<void*>(&bionic_madvise)},
    {"munmap", reinterpret_cast<void*>(&bionic_munmap)},
    {"open", reinterpret_cast<void*>(&vfs_open)},
    {"open64", reinterpret_cast<void*>(&vfs_open64)},
    {"close", reinterpret_cast<void*>(&bionic_close)},
    {"read", reinterpret_cast<void*>(&bionic_read)},
    {"write", reinterpret_cast<void*>(&bionic_write)},
    {"socket", reinterpret_cast<void*>(&bionic_socket)},
    {"bind", reinterpret_cast<void*>(&bionic_bind)},
    {"connect", reinterpret_cast<void*>(&bionic_connect)},
    {"setsockopt", reinterpret_cast<void*>(&bionic_setsockopt)},
    {"getsockopt", reinterpret_cast<void*>(&bionic_getsockopt)},
    {"fcntl", reinterpret_cast<void*>(&bionic_fcntl)},
    {"send", reinterpret_cast<void*>(&bionic_send)},
    {"recv", reinterpret_cast<void*>(&bionic_recv)},
    {"sendto", reinterpret_cast<void*>(&bionic_sendto)},
    {"__sendto_chk", reinterpret_cast<void*>(&bionic___sendto_chk)},
    {"recvfrom", reinterpret_cast<void*>(&bionic_recvfrom)},
    {"__recvfrom_chk", reinterpret_cast<void*>(&bionic___recvfrom_chk)},
    {"accept", reinterpret_cast<void*>(&bionic_accept)},
    {"accept4", reinterpret_cast<void*>(&bionic_accept4)},
    {"listen", reinterpret_cast<void*>(&bionic_listen)},
    {"shutdown", reinterpret_cast<void*>(&bionic_shutdown)},
    {"getsockname", reinterpret_cast<void*>(&bionic_getsockname)},
    {"getpeername", reinterpret_cast<void*>(&bionic_getpeername)},
    {"sendmsg", reinterpret_cast<void*>(&bionic_sendmsg)},
    {"recvmsg", reinterpret_cast<void*>(&bionic_recvmsg)},
    {"sendmmsg", reinterpret_cast<void*>(&bionic_sendmmsg)},
    {"recvmmsg", reinterpret_cast<void*>(&bionic_recvmmsg)},
    {"getaddrinfo", reinterpret_cast<void*>(&bionic_getaddrinfo)},
    {"android_getaddrinfofornet", reinterpret_cast<void*>(&bionic_android_getaddrinfofornet)},
    {"freeaddrinfo", reinterpret_cast<void*>(&bionic_freeaddrinfo)},
    {"gai_strerror", reinterpret_cast<void*>(&bionic_gai_strerror)},
    {"if_nametoindex", reinterpret_cast<void*>(&bionic_if_nametoindex)},
    {"inet_addr", reinterpret_cast<void*>(&::inet_addr)},
    {"inet_aton", reinterpret_cast<void*>(&::inet_aton)},
    {"inet_ntoa", reinterpret_cast<void*>(&::inet_ntoa)},
    {"htons", reinterpret_cast<void*>(&bionic_htons)},
    {"ntohs", reinterpret_cast<void*>(&bionic_htons)},
    {"htonl", reinterpret_cast<void*>(&bionic_htonl)},
    {"ntohl", reinterpret_cast<void*>(&bionic_htonl)},
    {"getifaddrs", reinterpret_cast<void*>(&bionic_getifaddrs)},
    {"freeifaddrs", reinterpret_cast<void*>(&bionic_freeifaddrs)},
    {"inet_ntop", reinterpret_cast<void*>(&bionic_inet_ntop)},
    {"inet_pton", reinterpret_cast<void*>(&bionic_inet_pton)},
    {"fopen", reinterpret_cast<void*>(&vfs_fopen)},
    {"fopen64", reinterpret_cast<void*>(&vfs_fopen64)},
    {"freopen", reinterpret_cast<void*>(&vfs_freopen)},
    {"fread", reinterpret_cast<void*>(&vfs_fread)},
    // fwrite is NOT vfs: the FILE* is already a host stream on a remapped path,
    // so this only adds the tracing that makes a save visible.
    {"fwrite", reinterpret_cast<void*>(&kudroid_fwrite)},
    {"fclose", reinterpret_cast<void*>(&vfs_fclose)},
    {"fseek", reinterpret_cast<void*>(&vfs_fseek)},
    {"fseeko", reinterpret_cast<void*>(&vfs_fseeko)},
    {"fseeko64", reinterpret_cast<void*>(&vfs_fseeko)},
    {"ftell", reinterpret_cast<void*>(&vfs_ftell)},
    {"ftello", reinterpret_cast<void*>(&vfs_ftello)},
    {"ftello64", reinterpret_cast<void*>(&vfs_ftello)},
    {"fileno", reinterpret_cast<void*>(&fileno)},
    {"feof", reinterpret_cast<void*>(&vfs_feof)},
    {"ferror", reinterpret_cast<void*>(&vfs_ferror)},
    {"clearerr", reinterpret_cast<void*>(&vfs_clearerr)},
    {"rewind", reinterpret_cast<void*>(&vfs_rewind)},
    {"access", reinterpret_cast<void*>(&vfs_access)},
    {"stat", reinterpret_cast<void*>(&vfs_stat)},
    {"stat64", reinterpret_cast<void*>(&vfs_stat64)},
    {"lstat", reinterpret_cast<void*>(&vfs_lstat)},
    {"lstat64", reinterpret_cast<void*>(&vfs_lstat64)},
    {"fstat", reinterpret_cast<void*>(&vfs_fstat)},
    {"fstat64", reinterpret_cast<void*>(&vfs_fstat64)},
    {"chmod", reinterpret_cast<void*>(&vfs_chmod)},
    {"chown", reinterpret_cast<void*>(&vfs_chown)},
    {"unlink", reinterpret_cast<void*>(&vfs_unlink)},
    {"remove", reinterpret_cast<void*>(&vfs_remove)},
    {"rename", reinterpret_cast<void*>(&vfs_rename)},
    // Free-space queries. statfs64/fstatfs64 are the same call on arm64 (bionic defines
    // both names against one 64-bit layout), so they share an implementation.
    {"statfs", reinterpret_cast<void*>(&bionic_statfs)},
    {"statfs64", reinterpret_cast<void*>(&bionic_statfs)},
    {"fstatfs", reinterpret_cast<void*>(&bionic_fstatfs)},
    {"fstatfs64", reinterpret_cast<void*>(&bionic_fstatfs)},
    {"ioctl", reinterpret_cast<void*>(&bionic_ioctl)},
    {"poll", reinterpret_cast<void*>(&bionic_poll)},
    {"ppoll", reinterpret_cast<void*>(&bionic_ppoll)},
    {"pselect", reinterpret_cast<void*>(&bionic_pselect)},
    {"epoll_pwait", reinterpret_cast<void*>(&bionic_epoll_pwait)},
    {"socketpair", reinterpret_cast<void*>(&bionic_socketpair)},
    {"getnameinfo", reinterpret_cast<void*>(&bionic_getnameinfo)},
    {"gethostbyname", reinterpret_cast<void*>(&bionic_gethostbyname)},
    {"gethostbyname2", reinterpret_cast<void*>(&bionic_gethostbyname2)},
    {"gethostbyaddr", reinterpret_cast<void*>(&bionic_gethostbyaddr)},
    {"gethostbyname_r", reinterpret_cast<void*>(&bionic_gethostbyname_r)},
    {"readv", reinterpret_cast<void*>(&bionic_readv)},
    {"writev", reinterpret_cast<void*>(&bionic_writev)},
    {"dup3", reinterpret_cast<void*>(&bionic_dup3)},
    {"prctl", reinterpret_cast<void*>(&bionic_prctl)},
    {"uname", reinterpret_cast<void*>(&bionic_uname)},
    {"__uname", reinterpret_cast<void*>(&bionic_uname)},
    {"pipe2", reinterpret_cast<void*>(&bionic_pipe2)},
    {"openat", reinterpret_cast<void*>(&bionic_openat)},
    {"openat64", reinterpret_cast<void*>(&bionic_openat)},
    {"fstatat", reinterpret_cast<void*>(&bionic_newfstatat)},
    {"fstatat64", reinterpret_cast<void*>(&bionic_newfstatat)},
    {"newfstatat", reinterpret_cast<void*>(&bionic_newfstatat)},
    {"getrandom", reinterpret_cast<void*>(&bionic_getrandom)},
    {"clock_gettime", reinterpret_cast<void*>(&bionic_clock_gettime)},
    {"__clock_gettime", reinterpret_cast<void*>(&bionic_clock_gettime)},
    {"clock_gettime64", reinterpret_cast<void*>(&bionic_clock_gettime64)},
    {"gettimeofday", reinterpret_cast<void*>(&bionic_gettimeofday)},
    {"clock_getres", reinterpret_cast<void*>(&bionic_clock_getres)},
    {"timer_create", reinterpret_cast<void*>(&bionic_timer_create)},
    {"timer_settime", reinterpret_cast<void*>(&bionic_timer_settime)},
    {"timer_gettime", reinterpret_cast<void*>(&bionic_timer_gettime)},
    {"timer_delete", reinterpret_cast<void*>(&bionic_timer_delete)},
    {"__isfinite", reinterpret_cast<void*>(&bionic___isfinite)},
    {"__isfinitef", reinterpret_cast<void*>(&bionic___isfinitef)},
    {"__isfinitel", reinterpret_cast<void*>(&bionic___isfinitel)},
    {"isfinite", reinterpret_cast<void*>(&bionic___isfinite)},
    {"ashmem_create_region", reinterpret_cast<void*>(&bionic_ashmem_create_region)},
    {"ashmem_set_prot_region", reinterpret_cast<void*>(&bionic_ashmem_set_prot_region)},
    {"ashmem_set_name", reinterpret_cast<void*>(&bionic_ashmem_set_name)},
    {"mkdir", reinterpret_cast<void*>(&vfs_mkdir)},
    {"rmdir", reinterpret_cast<void*>(&vfs_rmdir)},
    {"opendir", reinterpret_cast<void*>(&vfs_opendir)},
    {"readdir", reinterpret_cast<void*>(&vfs_readdir)},
    // readdir64 is a distinct symbol in bionic and the one 64-bit guest code is built
    // against. Left unshimmed it resolved through dlsym to Darwin's readdir, whose
    // struct dirent has a different layout — d_name lands at the wrong offset, so the
    // guest reads filenames out of the middle of other fields and sees garbage rather
    // than an error.
    {"readdir64", reinterpret_cast<void*>(&vfs_readdir)},
    {"closedir", reinterpret_cast<void*>(&vfs_closedir)},
    {"readlink", reinterpret_cast<void*>(&vfs_readlink)},
    {"realpath", reinterpret_cast<void*>(&vfs_realpath)},
    {"pthread_join", reinterpret_cast<void*>(&bionic_pthread_join)},
    {"pthread_mutex_init", reinterpret_cast<void*>(&bionic_pthread_mutex_init)},
    {"pthread_mutex_lock", reinterpret_cast<void*>(&bionic_pthread_mutex_lock)},
    {"pthread_mutex_unlock", reinterpret_cast<void*>(&bionic_pthread_mutex_unlock)},
    {"pthread_mutex_destroy", reinterpret_cast<void*>(&bionic_pthread_mutex_destroy)},
    {"__stack_chk_guard", reinterpret_cast<void*>(&gStackCheckGuard)},
    {"__stack_chk_fail", reinterpret_cast<void*>(&bionic_stack_chk_fail)},
#if defined(__aarch64__)
    {"__android_log_print", reinterpret_cast<void*>(&kudroid_android_log_print_trampoline)},
#else
    {"__android_log_print", reinterpret_cast<void*>(&bionic_android_log_print)},
#endif
    {"__cxa_finalize", reinterpret_cast<void*>(&bionic_cxa_finalize)},
    {"__cxa_atexit", reinterpret_cast<void*>(&bionic_cxa_atexit)},
    {"__cxa_thread_atexit_impl", reinterpret_cast<void*>(&bionic___cxa_thread_atexit_impl)},
    {"__cxa_guard_acquire", reinterpret_cast<void*>(&bionic___cxa_guard_acquire)},
    {"__cxa_guard_release", reinterpret_cast<void*>(&bionic___cxa_guard_release)},
    {"__cxa_guard_abort", reinterpret_cast<void*>(&bionic___cxa_guard_abort)},
    {"_ITM_registerTMCloneTable", reinterpret_cast<void*>(&bionic_runtime_noop)},
    {"_ITM_deregisterTMCloneTable", reinterpret_cast<void*>(&bionic_runtime_noop)},
    {"__gmon_start__", reinterpret_cast<void*>(&bionic_runtime_noop)},

    // Exceptions and Unwinding
    {"_Unwind_Resume", reinterpret_cast<void*>(&_Unwind_Resume)},
    {"_Unwind_RaiseException", reinterpret_cast<void*>(&_Unwind_RaiseException)},
    {"_Unwind_DeleteException", reinterpret_cast<void*>(&_Unwind_DeleteException)},
    {"_Unwind_GetLanguageSpecificData", reinterpret_cast<void*>(&_Unwind_GetLanguageSpecificData)},
    {"_Unwind_GetRegionStart", reinterpret_cast<void*>(&_Unwind_GetRegionStart)},
    {"_Unwind_SetGR", reinterpret_cast<void*>(&_Unwind_SetGR)},
    {"_Unwind_SetIP", reinterpret_cast<void*>(&_Unwind_SetIP)},
    {"_Unwind_GetIP", reinterpret_cast<void*>(&_Unwind_GetIP)},
    {"_Unwind_GetGR", reinterpret_cast<void*>(&_Unwind_GetGR)},
    {"_Unwind_Backtrace", reinterpret_cast<void*>(&_Unwind_Backtrace)},
    {"__cxa_allocate_exception", reinterpret_cast<void*>(&abi::__cxa_allocate_exception)},
    {"__cxa_free_exception", reinterpret_cast<void*>(&abi::__cxa_free_exception)},
    {"__cxa_throw", reinterpret_cast<void*>(&abi::__cxa_throw)},
    {"__cxa_begin_catch", reinterpret_cast<void*>(&abi::__cxa_begin_catch)},
    {"__cxa_end_catch", reinterpret_cast<void*>(&abi::__cxa_end_catch)},
    {"__gxx_personality_v0", reinterpret_cast<void*>(&__gxx_personality_v0)},

    // ALooper (Android event loop)
    {"ALooper_prepare", reinterpret_cast<void*>(&bionic_ALooper_prepare)},
    {"ALooper_forThread", reinterpret_cast<void*>(&bionic_ALooper_forThread)},
    {"ALooper_acquire", reinterpret_cast<void*>(&bionic_ALooper_acquire)},
    {"ALooper_release", reinterpret_cast<void*>(&bionic_ALooper_release)},
    {"ALooper_addFd", reinterpret_cast<void*>(&bionic_ALooper_addFd)},
    {"ALooper_removeFd", reinterpret_cast<void*>(&bionic_ALooper_removeFd)},
    {"ALooper_wake", reinterpret_cast<void*>(&bionic_ALooper_wake)},
    {"ALooper_pollOnce", reinterpret_cast<void*>(&bionic_ALooper_pollOnce)},
    {"ALooper_pollAll", reinterpret_cast<void*>(&bionic_ALooper_pollAll)},


    // Memory alignment
    {"memalign", reinterpret_cast<void*>(&bionic_memalign)},

    // Android property system
    {"__system_property_get", reinterpret_cast<void*>(&bionic_system_property_get)},
    {"__system_property_find", reinterpret_cast<void*>(&bionic_system_property_find)},
    {"__system_property_read", reinterpret_cast<void*>(&bionic_system_property_read)},
    {"__system_property_read_callback", reinterpret_cast<void*>(&bionic_system_property_read_callback)},
    {"__system_property_foreach", reinterpret_cast<void*>(&bionic_system_property_foreach)},

    // ELF iteration
    {"dl_iterate_phdr", reinterpret_cast<void*>(&bionic_dl_iterate_phdr)},

    // Dynamic loader (dlfcn) — must route through the shim so the guest never
    // calls the real host dlopen with a nonexistent Android .so path.
    {"dlopen", reinterpret_cast<void*>(&bionic_dlopen)},
    {"dlsym", reinterpret_cast<void*>(&bionic_dlsym)},
    {"dlclose", reinterpret_cast<void*>(&bionic_dlclose)},
    {"dlerror", reinterpret_cast<void*>(&bionic_dlerror)},
    {"android_dlopen_ext", reinterpret_cast<void*>(&bionic_android_dlopen_ext)},
    {"__loader_dlopen", reinterpret_cast<void*>(&bionic_loader_dlopen)},
    {"__loader_android_dlopen_ext", reinterpret_cast<void*>(&bionic_loader_android_dlopen_ext)},

    // File operations (32/64-bit unified on 64-bit host)
    {"lseek", reinterpret_cast<void*>(&bionic_lseek64)},
    {"lseek64", reinterpret_cast<void*>(&bionic_lseek64)},
    {"pread", reinterpret_cast<void*>(&bionic_pread64)},
    {"pread64", reinterpret_cast<void*>(&bionic_pread64)},
    {"pwrite", reinterpret_cast<void*>(&bionic_pwrite64)},
    {"pwrite64", reinterpret_cast<void*>(&bionic_pwrite64)},
    {"ftruncate", reinterpret_cast<void*>(&bionic_ftruncate64)},
    {"ftruncate64", reinterpret_cast<void*>(&bionic_ftruncate64)},
    {"pipe2", reinterpret_cast<void*>(&bionic_pipe2)},
    {"clock_nanosleep", reinterpret_cast<void*>(&bionic_clock_nanosleep)},
    {"usleep", reinterpret_cast<void*>(&bionic_usleep)},
    {"tgkill", reinterpret_cast<void*>(&bionic_tgkill)},
    {"sendfile", reinterpret_cast<void*>(&bionic_sendfile)},
    {"sendfile64", reinterpret_cast<void*>(&bionic_sendfile)},
    {"sched_getscheduler", reinterpret_cast<void*>(&bionic_sched_getscheduler)},
    {"sched_setscheduler", reinterpret_cast<void*>(&bionic_sched_setscheduler)},
    {"sched_getparam", reinterpret_cast<void*>(&bionic_sched_getparam)},
    {"posix_fadvise", reinterpret_cast<void*>(&bionic_posix_fadvise)},
    {"posix_fadvise64", reinterpret_cast<void*>(&bionic_posix_fadvise)},
    {"posix_fallocate", reinterpret_cast<void*>(&bionic_posix_fallocate)},
    {"posix_fallocate64", reinterpret_cast<void*>(&bionic_posix_fallocate)},
    {"vmsplice", reinterpret_cast<void*>(&bionic_vmsplice)},
    {"tdestroy", reinterpret_cast<void*>(&bionic_tdestroy)},
    {"mkstemp", reinterpret_cast<void*>(&bionic_mkstemp)},
    {"mkstemp64", reinterpret_cast<void*>(&bionic_mkstemp)},
    {"setmntent", reinterpret_cast<void*>(&bionic_setmntent)},
    {"getmntent_r", reinterpret_cast<void*>(&bionic_getmntent_r)},
    {"endmntent", reinterpret_cast<void*>(&bionic_endmntent)},
    {"omp_in_parallel", reinterpret_cast<void*>(&bionic_omp_in_parallel)},
    {"__strchr_chk", reinterpret_cast<void*>(&bionic___strchr_chk)},
    {"__strncpy_chk2", reinterpret_cast<void*>(&bionic___strncpy_chk2)},
    {"__FD_CLR_chk", reinterpret_cast<void*>(&bionic___FD_CLR_chk)},
    {"__cmsg_nxthdr", reinterpret_cast<void*>(&bionic___cmsg_nxthdr)},

    {"splice", reinterpret_cast<void*>(&bionic_splice)},
    {"copy_file_range", reinterpret_cast<void*>(&bionic_copy_file_range)},
    {"__assert2", reinterpret_cast<void*>(&bionic___assert2)},
#if defined(__aarch64__)
    {"__android_log_assert", reinterpret_cast<void*>(&kudroid_log_assert_trampoline)},
#else
    {"__android_log_assert", reinterpret_cast<void*>(&bionic___android_log_assert)},
#endif
    {"__android_log_vprint", reinterpret_cast<void*>(&bionic_android_log_vprint)},
    {"__android_log_write", reinterpret_cast<void*>(&bionic_android_log_write)},
    {"__android_log_buf_write", reinterpret_cast<void*>(&bionic_android_log_buf_write)},

    // Character classification
    {"__ctype_get_mb_cur_max", reinterpret_cast<void*>(&bionic_ctype_get_mb_cur_max)},
    {"_ctype_", reinterpret_cast<void*>(&g_ctype_ptr)},

    // Bionic libm math functions (host standard <cmath>)
    {"sin", reinterpret_cast<void*>(static_cast<double(*)(double)>(&sin))},
    {"sinf", reinterpret_cast<void*>(&sinf)},
    {"cos", reinterpret_cast<void*>(static_cast<double(*)(double)>(&cos))},
    {"cosf", reinterpret_cast<void*>(&cosf)},
    {"tan", reinterpret_cast<void*>(static_cast<double(*)(double)>(&tan))},
    {"tanf", reinterpret_cast<void*>(&tanf)},
    {"asin", reinterpret_cast<void*>(static_cast<double(*)(double)>(&asin))},
    {"asinf", reinterpret_cast<void*>(&asinf)},
    {"acos", reinterpret_cast<void*>(static_cast<double(*)(double)>(&acos))},
    {"acosf", reinterpret_cast<void*>(&acosf)},
    {"atan", reinterpret_cast<void*>(static_cast<double(*)(double)>(&atan))},
    {"atanf", reinterpret_cast<void*>(&atanf)},
    {"atan2", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&atan2))},
    {"atan2f", reinterpret_cast<void*>(&atan2f)},
    {"sinh", reinterpret_cast<void*>(static_cast<double(*)(double)>(&sinh))},
    {"sinhf", reinterpret_cast<void*>(&sinhf)},
    {"cosh", reinterpret_cast<void*>(static_cast<double(*)(double)>(&cosh))},
    {"coshf", reinterpret_cast<void*>(&coshf)},
    {"tanh", reinterpret_cast<void*>(static_cast<double(*)(double)>(&tanh))},
    {"tanhf", reinterpret_cast<void*>(&tanhf)},
    {"exp", reinterpret_cast<void*>(static_cast<double(*)(double)>(&exp))},
    {"expf", reinterpret_cast<void*>(&expf)},
    {"exp2", reinterpret_cast<void*>(static_cast<double(*)(double)>(&exp2))},
    {"exp2f", reinterpret_cast<void*>(&exp2f)},
    {"expm1", reinterpret_cast<void*>(static_cast<double(*)(double)>(&expm1))},
    {"expm1f", reinterpret_cast<void*>(&expm1f)},
    {"log", reinterpret_cast<void*>(static_cast<double(*)(double)>(&::log))},
    {"logf", reinterpret_cast<void*>(&logf)},
    {"log2", reinterpret_cast<void*>(static_cast<double(*)(double)>(&log2))},
    {"log2f", reinterpret_cast<void*>(&log2f)},
    {"log10", reinterpret_cast<void*>(static_cast<double(*)(double)>(&log10))},
    {"log10f", reinterpret_cast<void*>(&log10f)},
    {"log1p", reinterpret_cast<void*>(static_cast<double(*)(double)>(&log1p))},
    {"log1pf", reinterpret_cast<void*>(&log1pf)},
    {"pow", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&pow))},
    {"powf", reinterpret_cast<void*>(&powf)},
    {"sqrt", reinterpret_cast<void*>(static_cast<double(*)(double)>(&sqrt))},
    {"sqrtf", reinterpret_cast<void*>(&sqrtf)},
    {"cbrt", reinterpret_cast<void*>(static_cast<double(*)(double)>(&cbrt))},
    {"cbrtf", reinterpret_cast<void*>(&cbrtf)},
    {"hypot", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&hypot))},
    {"hypotf", reinterpret_cast<void*>(&hypotf)},
    {"ceil", reinterpret_cast<void*>(static_cast<double(*)(double)>(&ceil))},
    {"ceilf", reinterpret_cast<void*>(&ceilf)},
    {"floor", reinterpret_cast<void*>(static_cast<double(*)(double)>(&floor))},
    {"floorf", reinterpret_cast<void*>(&floorf)},
    {"trunc", reinterpret_cast<void*>(static_cast<double(*)(double)>(&trunc))},
    {"truncf", reinterpret_cast<void*>(&truncf)},
    {"round", reinterpret_cast<void*>(static_cast<double(*)(double)>(&round))},
    {"roundf", reinterpret_cast<void*>(&roundf)},
    {"rint", reinterpret_cast<void*>(static_cast<double(*)(double)>(&rint))},
    {"rintf", reinterpret_cast<void*>(&rintf)},
    {"nearbyint", reinterpret_cast<void*>(static_cast<double(*)(double)>(&nearbyint))},
    {"nearbyintf", reinterpret_cast<void*>(&nearbyintf)},
    {"fabs", reinterpret_cast<void*>(static_cast<double(*)(double)>(&fabs))},
    {"fabsf", reinterpret_cast<void*>(&fabsf)},
    {"fmod", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&fmod))},
    {"fmodf", reinterpret_cast<void*>(&fmodf)},
    {"remainder", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&remainder))},
    {"remainderf", reinterpret_cast<void*>(&remainderf)},
    {"fmin", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&fmin))},
    {"fminf", reinterpret_cast<void*>(&fminf)},
    {"fmax", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&fmax))},
    {"fmaxf", reinterpret_cast<void*>(&fmaxf)},
    {"fdim", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&fdim))},
    {"fdimf", reinterpret_cast<void*>(&fdimf)},
    {"fma", reinterpret_cast<void*>(static_cast<double(*)(double, double, double)>(&fma))},
    {"fmaf", reinterpret_cast<void*>(&fmaf)},
    {"ldexp", reinterpret_cast<void*>(static_cast<double(*)(double, int)>(&ldexp))},
    {"ldexpf", reinterpret_cast<void*>(&ldexpf)},
    {"frexp", reinterpret_cast<void*>(static_cast<double(*)(double, int*)>(&frexp))},
    {"frexpf", reinterpret_cast<void*>(&frexpf)},
    {"modf", reinterpret_cast<void*>(static_cast<double(*)(double, double*)>(&modf))},
    {"modff", reinterpret_cast<void*>(&modff)},
    {"scalbn", reinterpret_cast<void*>(static_cast<double(*)(double, int)>(&scalbn))},
    {"scalbnf", reinterpret_cast<void*>(&scalbnf)},
    {"copysign", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&copysign))},
    {"copysignf", reinterpret_cast<void*>(&copysignf)},
    {"nan", reinterpret_cast<void*>(&nan)},
    {"nanf", reinterpret_cast<void*>(&nanf)},
    {"sincos", reinterpret_cast<void*>(&bionic_sincos)},
    {"sincosf", reinterpret_cast<void*>(&bionic_sincosf)},
    {"__strlen_chk", reinterpret_cast<void*>(&bionic___strlen_chk)},
    {"__FD_SET_chk", reinterpret_cast<void*>(&bionic___FD_SET_chk)},
    {"__FD_ISSET_chk", reinterpret_cast<void*>(&bionic___FD_ISSET_chk)},
    {"sem_init", reinterpret_cast<void*>(&bionic_sem_init)},
    {"sem_destroy", reinterpret_cast<void*>(&bionic_sem_destroy)},
    {"sem_wait", reinterpret_cast<void*>(&bionic_sem_wait)},
    {"sem_trywait", reinterpret_cast<void*>(&bionic_sem_trywait)},
    {"sem_post", reinterpret_cast<void*>(&bionic_sem_post)},
    {"sem_getvalue", reinterpret_cast<void*>(&bionic_sem_getvalue)},
    {"sem_timedwait", reinterpret_cast<void*>(&bionic_sem_timedwait)},
    {"android_set_abort_message", reinterpret_cast<void*>(&bionic_android_set_abort_message)},
    {"__memcpy_chk", reinterpret_cast<void*>(&bionic___memcpy_chk)},
    {"__memmove_chk", reinterpret_cast<void*>(&bionic___memmove_chk)},
    {"__memset_chk", reinterpret_cast<void*>(&bionic___memset_chk)},
    {"__read_chk", reinterpret_cast<void*>(&bionic___read_chk)},
    {"__write_chk", reinterpret_cast<void*>(&bionic___write_chk)},
    // The _FORTIFY_SOURCE forms, which is what a release-built guest calls instead of
    // snprintf/sprintf. Same ABI problem as the plain versions, so the same trampoline
    // treatment. The v* forms take a guest va_list, which is a 32-byte composite passed by
    // reference where Apple's is a char* passed by value — the same split from the other
    // direction, handled by the shims above rather than by a trampoline.
#if defined(__aarch64__)
    {"__snprintf_chk", reinterpret_cast<void*>(&kudroid_snprintf_chk_trampoline)},
    {"__sprintf_chk", reinterpret_cast<void*>(&kudroid_sprintf_chk_trampoline)},
#else
    {"__snprintf_chk", reinterpret_cast<void*>(&bionic___snprintf_chk)},
    {"__sprintf_chk", reinterpret_cast<void*>(&bionic___sprintf_chk)},
#endif
    {"__vsnprintf_chk", reinterpret_cast<void*>(&bionic___vsnprintf_chk)},
    {"__vsprintf_chk", reinterpret_cast<void*>(&bionic___vsprintf_chk)},
    {"__vfprintf_chk", reinterpret_cast<void*>(&bionic___vfprintf_chk)},
    // The plain v* family. Previously absent from every table, so they fell through to
    // dlsym(RTLD_DEFAULT) and reached the HOST implementation directly — the worst case,
    // because the host reads a guest va_list as a stack cursor and prints plausible
    // rubbish rather than failing.
    {"vsnprintf", reinterpret_cast<void*>(&bionic_vsnprintf)},
    {"vsprintf", reinterpret_cast<void*>(&bionic_vsprintf)},
    {"vfprintf", reinterpret_cast<void*>(&bionic_vfprintf)},
    {"vprintf", reinterpret_cast<void*>(&bionic_vprintf)},
    {"vasprintf", reinterpret_cast<void*>(&bionic_vasprintf)},
    {"__vasprintf_chk", reinterpret_cast<void*>(&bionic___vasprintf_chk)},
    {"vdprintf", reinterpret_cast<void*>(&bionic_vdprintf)},
    {"__vdprintf_chk", reinterpret_cast<void*>(&bionic___vdprintf_chk)},
#if defined(__aarch64__)
    {"syslog", reinterpret_cast<void*>(&kudroid_syslog_trampoline)},
#else
    {"syslog", reinterpret_cast<void*>(&bionic_syslog)},
#endif
    {"vsyslog", reinterpret_cast<void*>(&bionic_vsyslog)},
    {"__vsyslog_chk", reinterpret_cast<void*>(&bionic___vsyslog_chk)},
    // The scanf family. These were absent too, and reaching the host implementation is
    // worse here than for printf: scanf WRITES through its varargs. Minecraft's UUID parse
    // passes eleven output pointers, and the host took five of them from stack words that
    // were never pointers — one held a jobject (reported later as clazz=0xf8ec9809), and
    // the next store went to address 0.
#if defined(__aarch64__)
    {"sscanf", reinterpret_cast<void*>(&kudroid_sscanf_trampoline)},
    {"__isoc99_sscanf", reinterpret_cast<void*>(&kudroid_isoc99_sscanf_trampoline)},
    {"fscanf", reinterpret_cast<void*>(&kudroid_fscanf_trampoline)},
    {"__isoc99_fscanf", reinterpret_cast<void*>(&kudroid_fscanf_trampoline)},
#else
    {"sscanf", reinterpret_cast<void*>(&bionic_sscanf)},
    {"__isoc99_sscanf", reinterpret_cast<void*>(&bionic_sscanf)},
    {"fscanf", reinterpret_cast<void*>(&bionic_fscanf)},
    {"__isoc99_fscanf", reinterpret_cast<void*>(&bionic_fscanf)},
#endif
    {"vsscanf", reinterpret_cast<void*>(&bionic_vsscanf)},
    {"__isoc99_vsscanf", reinterpret_cast<void*>(&bionic_vsscanf)},
    {"vfscanf", reinterpret_cast<void*>(&bionic_vfscanf)},
    {"__strncpy_chk", reinterpret_cast<void*>(&bionic___strncpy_chk)},
    {"__strcpy_chk", reinterpret_cast<void*>(&bionic___strcpy_chk)},
    {"__strcat_chk", reinterpret_cast<void*>(&bionic___strcat_chk)},
    {"__fdelt_chk", reinterpret_cast<void*>(&bionic___fdelt_chk)},
    {"__open_2", reinterpret_cast<void*>(&bionic___open_2)},
    {"__openat_2", reinterpret_cast<void*>(&bionic___openat_2)},
    {"__umask_chk", reinterpret_cast<void*>(&bionic___umask_chk)},
    {"__strrchr_chk", reinterpret_cast<void*>(&bionic___strrchr_chk)},
    {"sysinfo", reinterpret_cast<void*>(&bionic_sysinfo)},

    // pthread extensions
    {"pthread_condattr_setclock", reinterpret_cast<void*>(&bionic_pthread_condattr_setclock)},

    // Google internal
    {"__google_potentially_blocking_region_begin", reinterpret_cast<void*>(&bionic_google_potentially_blocking_region_begin)},
    {"__google_potentially_blocking_region_end", reinterpret_cast<void*>(&bionic_google_potentially_blocking_region_end)},

    // Additional Linux syscalls
    {"getcpu", reinterpret_cast<void*>(&bionic_getcpu)},
    {"sched_getaffinity", reinterpret_cast<void*>(&bionic_sched_getaffinity)},
    {"sched_setaffinity", reinterpret_cast<void*>(&bionic_sched_setaffinity)},
    // CPU_COUNT() expands to this. Missing here meant it bound to the universal dummy
    // and returned 0 for every mask, which is the shape of Unity's "Cores = 0".
    {"__sched_cpucount", reinterpret_cast<void*>(&bionic___sched_cpucount)},
    {"sched_getcpu", reinterpret_cast<void*>(&bionic_sched_getcpu)},
    {"get_nprocs", reinterpret_cast<void*>(&bionic_get_nprocs)},
    {"get_nprocs_conf", reinterpret_cast<void*>(&bionic_get_nprocs_conf)},
    {"get_phys_pages", reinterpret_cast<void*>(&bionic_get_phys_pages)},
    {"get_avphys_pages", reinterpret_cast<void*>(&bionic_get_avphys_pages)},
    {"inotify_init1", reinterpret_cast<void*>(&bionic_inotify_init1)},
    {"inotify_add_watch", reinterpret_cast<void*>(&bionic_inotify_add_watch)},
    {"inotify_rm_watch", reinterpret_cast<void*>(&bionic_inotify_rm_watch)},
    {"signalfd", reinterpret_cast<void*>(&bionic_signalfd)},
    {"eventfd2", reinterpret_cast<void*>(&bionic_eventfd2)},
    {"prlimit64", reinterpret_cast<void*>(&bionic_prlimit64)},
    {"statx", reinterpret_cast<void*>(&bionic_statx)},

    // JNI and JVM Runtime Bridge
    {"kudroid_jni_get_javavm", reinterpret_cast<void*>(&kudroid_jni_get_javavm)},
    {"JNI_GetCreatedJavaVMs", reinterpret_cast<void*>(&JNI_GetCreatedJavaVMs)},
    {"JNI_CreateJavaVM", reinterpret_cast<void*>(&JNI_CreateJavaVM)},

    // Android Runtime Permissions Bridge
    {"kudroid_check_permission", reinterpret_cast<void*>(&kudroid_check_permission)},
    {"kudroid_set_group_permission", reinterpret_cast<void*>(&kudroid_set_group_permission)},
    {"kudroid_is_group_granted", reinterpret_cast<void*>(&kudroid_is_group_granted)},
    {"kudroid_grant_all_permissions", reinterpret_cast<void*>(&kudroid_grant_all_permissions)},
    {"kudroid_get_app_permissions_json", reinterpret_cast<void*>(&kudroid_get_app_permissions_json)},
    {"kudroid_set_app_permissions_json", reinterpret_cast<void*>(&kudroid_set_app_permissions_json)},

    // Zstandard Compression Tracing Symbols
    {"ZSTD_trace_compress_begin", reinterpret_cast<void*>(&bionic_ZSTD_trace_compress_begin)},
    {"ZSTD_trace_compress_end", reinterpret_cast<void*>(&bionic_ZSTD_trace_compress_end)},
    {"ZSTD_trace_decompress_begin", reinterpret_cast<void*>(&bionic_ZSTD_trace_decompress_begin)},
    {"ZSTD_trace_decompress_end", reinterpret_cast<void*>(&bionic_ZSTD_trace_decompress_end)},

    // Android NDK Bitmap API
    {"AndroidBitmap_getInfo", reinterpret_cast<void*>(&bionic_AndroidBitmap_getInfo)},
    {"AndroidBitmap_lockPixels", reinterpret_cast<void*>(&bionic_AndroidBitmap_lockPixels)},
    {"AndroidBitmap_unlockPixels", reinterpret_cast<void*>(&bionic_AndroidBitmap_unlockPixels)},

    // Bionic Fortify / String / Assertion Shims
    {"__gnu_strerror_r", reinterpret_cast<void*>(&bionic___gnu_strerror_r)},
    {"__fwrite_chk", reinterpret_cast<void*>(&bionic___fwrite_chk)},
    {"__strchr_chk", reinterpret_cast<void*>(&bionic___strchr_chk)},
    {"omp_in_parallel", reinterpret_cast<void*>(&bionic_omp_in_parallel)},
    {"copy_file_range", reinterpret_cast<void*>(&bionic_copy_file_range)},
    {"splice", reinterpret_cast<void*>(&bionic_splice)},
    {"__assert2", reinterpret_cast<void*>(&bionic___assert2)},
#if defined(__aarch64__)
    {"__android_log_assert", reinterpret_cast<void*>(&kudroid_log_assert_trampoline)},
#else
    {"__android_log_assert", reinterpret_cast<void*>(&bionic___android_log_assert)},
#endif

    // KuDroid Screen Orientation, Haptic Vibrator & Sensor Bridge
    {"kudroid_vibrate", reinterpret_cast<void*>(&kudroid_vibrate)},
    {"kudroid_set_requested_orientation", reinterpret_cast<void*>(&kudroid_set_requested_orientation)},
    {"kudroid_get_requested_orientation", reinterpret_cast<void*>(&kudroid_get_requested_orientation)},
    {"kudroid_inject_sensor_event", reinterpret_cast<void*>(&kudroid_inject_sensor_event)},

    // Bionic malloc extensions (jemalloc/BoringSSL)
    {"malloc_usable_size", reinterpret_cast<void*>(&bionic_malloc_usable_size)},
    {"mallinfo", reinterpret_cast<void*>(&bionic_mallinfo)},
    {"sdallocx", reinterpret_cast<void*>(&bionic_sdallocx)},
    {"OPENSSL_memory_alloc", reinterpret_cast<void*>(&bionic_OPENSSL_memory_alloc)},
    {"OPENSSL_memory_free", reinterpret_cast<void*>(&bionic_OPENSSL_memory_free)},
    {"OPENSSL_memory_get_size", reinterpret_cast<void*>(&bionic_OPENSSL_memory_get_size)},

    // Linux-only syscalls emulated on iOS
    {"memfd_create", reinterpret_cast<void*>(&bionic_memfd_create)},

    // pthread / stdio extensions
    {"pthread_gettid_np", reinterpret_cast<void*>(&bionic_pthread_gettid_np)},
    {"__pthread_cleanup_push", reinterpret_cast<void*>(&bionic___pthread_cleanup_push)},
    {"__pthread_cleanup_pop", reinterpret_cast<void*>(&bionic___pthread_cleanup_pop)},
    {"__fgets_chk", reinterpret_cast<void*>(&bionic___fgets_chk)},
    {"freopen64", reinterpret_cast<void*>(&vfs_freopen)},

    // ── Added after Minecraft/libmaesdk null-dereference fixes ─────────────────────
    // strlcpy/strlcat: available natively in Darwin libc (BSD origin).
    {"strlcpy", reinterpret_cast<void*>(&::strlcpy)},
    {"strlcat", reinterpret_cast<void*>(&::strlcat)},
    // isatty/getpagesize/tkill: mapped to host libc equivalents.
    {"isatty", reinterpret_cast<void*>(&::isatty)},
    {"getpagesize", reinterpret_cast<void*>(&::getpagesize)},
    {"tkill", reinterpret_cast<void*>(&bionic_tkill)},
    // pthread_setname_np: 2-parameter Bionic overload wrapper.
    {"pthread_setname_np", reinterpret_cast<void*>(&bionic_pthread_setname_np2)},
    // setprogname/getprogname: Darwin native / Linux host fallback.
    {"setprogname", reinterpret_cast<void*>(&bionic_setprogname)},
    {"getprogname", reinterpret_cast<void*>(&bionic_getprogname)},
    // mallopt/malloc_info stubs for platform parity.
    {"mallopt", reinterpret_cast<void*>(&bionic_mallopt)},
    {"malloc_info", reinterpret_cast<void*>(&bionic_malloc_info)},
};

} // namespace

const SymbolEntry* get_syscall_symbols(size_t* count) {
    if (count) {
        *count = sizeof(kSyscallSymbols) / sizeof(SymbolEntry);
    }
    return kSyscallSymbols;
}

} // namespace kudroid
