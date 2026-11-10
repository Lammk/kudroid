// Result trace for mmap, deliberately separate from the request line: the request
// already appears in the KuDroidSyscall stream, but only the RESULT explains a
// missing allocation. A commit that failed, or landed somewhere other than the
// requested hint, is the difference between a working heap and a null structure.
namespace {
void log_mmap_result(void* result, size_t length, int prot, int flags, int fd,
                     off_t offset, const void* requested) {
    if (kudroid::guest_async_handler_active()) return;
    const int saved_errno = errno;
    if (result == MAP_FAILED) {
        static std::atomic<int> n{0};
        if (n.fetch_add(1, std::memory_order_relaxed) < 40) {
            std::fprintf(stderr,
                         "[KuDroidMmap] FAIL len=%zu prot=0x%x flags=0x%x fd=%d off=%lld "
                         "req=%p errno=%d (%s)\n",
                         length, prot, flags, fd, static_cast<long long>(offset), requested,
                         saved_errno, std::strerror(saved_errno));
        }
        if (length >= (4ULL << 30) && prot == PROT_NONE && (flags & 0x20) &&
            (flags & 3) == 2 && !(flags & (0x10 | 0x100000))) {
            static std::atomic<unsigned> probes{0};
            if (probes.fetch_add(1, std::memory_order_relaxed) < 2)
                kudroid_log_virtual_memory_state("reservation-failure");
        }
        errno = saved_errno;
        return;
    }
    // Success: surface what matters for placement — file-backed maps and the big
    // reservations/commits — without printing every small anonymous mapping.
    // The old single 120-line cap went silent five lines into a session (five
    // big reservations total in one crash log) and left the decisive question
    // "was this 512MB PROT_NONE region ever committed?" unanswerable. Big
    // mappings get their own, much higher budget: they are rare events and
    // each one is a heap-provenance fact.
    if (fd >= 0 || length >= (1u << 20) || requested != nullptr) {
        static std::atomic<int> n_small{0};
        static std::atomic<int> n_big{0};
        const bool big = length >= (1u << 20);
        auto& n = big ? n_big : n_small;
        const int cap = big ? 2000 : 120;
        if (n.fetch_add(1, std::memory_order_relaxed) < cap) {
            char current[4] = "???", maximum[4] = "???";
#if defined(__APPLE__)
            kudroid::QueryRegionProt(result, current, maximum);
#endif
            std::fprintf(stderr,
                         "[KuDroidMmap] ok len=%zu prot=0x%x flags=0x%x fd=%d off=%lld "
                         "req=%p -> %p cur=%s max=%s\n",
                         length, prot, flags, fd, static_cast<long long>(offset), requested,
                         result, current, maximum);
        }
    }
    errno = saved_errno;
}

#if defined(__APPLE__)
static bool host_range_query(uintptr_t start, size_t length, bool* out_mapped, int* out_prot,
                             int* out_maximum = nullptr) {
    if (start == 0 || length == 0 || start > UINTPTR_MAX - length) return false;
    uintptr_t curr = start;
    const uintptr_t target_end = start + length;
    int common_prot = -1;
    int maximum = VM_PROT_ALL;
    while (curr < target_end) {
        mach_vm_address_t region_addr = static_cast<mach_vm_address_t>(curr);
        mach_vm_size_t sz = 0;
        kudroid_vm_region_basic_info_64_t info{};
        mach_msg_type_number_t count = KUDROID_VM_REGION_BASIC_INFO_64_COUNT;
        mach_port_t obj = MACH_PORT_NULL;
        const kern_return_t kr = mach_vm_region(
            mach_task_self(), &region_addr, &sz, KUDROID_VM_REGION_BASIC_INFO_64_FLAVOR,
            reinterpret_cast<vm_region_info_t>(&info), &count, &obj);
        if (obj != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), obj);
        if (kr != KERN_SUCCESS || region_addr > curr) {
            if (out_mapped) *out_mapped = false;
            if (out_prot) *out_prot = 0;
            if (out_maximum) *out_maximum = 0;
            return true;
        }
        int p = 0;
        if (info.protection & VM_PROT_READ) p |= PROT_READ;
        if (info.protection & VM_PROT_WRITE) p |= PROT_WRITE;
        if (info.protection & VM_PROT_EXECUTE) p |= PROT_EXEC;
        if (common_prot == -1) common_prot = p;
        else if (common_prot != p) common_prot = -2;
        maximum &= info.max_protection;
        if (sz == 0 || region_addr + sz <= curr) return false;
        curr = static_cast<uintptr_t>(region_addr + sz);
    }
    if (out_mapped) *out_mapped = true;
    if (out_prot) *out_prot = common_prot;
    if (out_maximum) *out_maximum = maximum;
    return true;
}


#endif
}  // namespace

extern "C" int bionic_mprotect(void *addr, size_t len, int prot);

// Memory mapping wrappers to strip Linux specific flags
extern "C" void* bionic_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    // ashmem fd: only mappable with granted prot.
    if (!ashmem_prot_allows(fd, prot)) {
        log_mmap_result(MAP_FAILED, length, prot, flags, fd, offset, addr);
        errno = EACCES;
        return MAP_FAILED;
    }
    // No diagnostics (allocation, log lock, stdio) while the world may be stopped:
    // the collector maps memory in that window.
    const bool quiet = kudroid::guest_async_handler_active();
    if (!quiet && (prot & PROT_EXEC)) {
        logAndroidMessage(4, "KuDroidSyscall", "bionic_mmap: allocating executable memory (JIT/library), length=" + std::to_string(length));
    }
    if (!quiet) logAndroidMessage(3, "KuDroidSyscall", "mmap(addr=" + (addr ? std::to_string((uintptr_t)addr) : std::string("NULL")) +
                      ", len=" + std::to_string(length) + ", prot=0x" +
                      [](int p){ char b[16]; snprintf(b, sizeof(b), "%x", p); return std::string(b); }(prot) +
                      ", flags=0x" +
                      [](int f){ char b[16]; snprintf(b, sizeof(b), "%x", f); return std::string(b); }(flags) +
                      ", fd=" + std::to_string(fd) + ")");

    // ashmem fake fd (iOS fallback): return granted region; no flag translation needed.
    if (void* region = bionic_ashmem_mmap_fd(fd, length)) {
        log_mmap_result(region, length, prot, flags, fd, offset, addr);
        logAndroidMessage(3, "KuDroidSyscall", "mmap -> ashmem region " +
                          std::to_string(reinterpret_cast<uintptr_t>(region)) + " (fake fd)");
        return region;
    }

#ifdef __APPLE__
    // Android/Linux and Darwin use different numeric values for mmap flags.
    // Translate them explicitly instead of forwarding the raw Linux bits.
    // Linux: MAP_SHARED=0x01 MAP_PRIVATE=0x02 MAP_FIXED=0x10 MAP_ANONYMOUS=0x20
    constexpr int LINUX_MAP_SHARED    = 0x01;
    constexpr int LINUX_MAP_PRIVATE   = 0x02;
    constexpr int LINUX_MAP_FIXED     = 0x10;
    constexpr int LINUX_MAP_ANONYMOUS = 0x20;
    constexpr int LINUX_MAP_SHARED_VALIDATE = 0x03;

    // Flags that only exist on Linux; forwarding them raw to Darwin causes
    // EINVAL (e.g. MAP_NORESERVE=0x4000 collides with a reserved Darwin bit)
    // or misparsing. Strip them before calling host mmap.
    constexpr int LINUX_ONLY_MAP_FLAGS =
        0x0100    | // MAP_GROWSDOWN
        0x0800    | // MAP_DENYWRITE
        0x1000    | // MAP_EXECUTABLE (collides with Darwin MAP_ANON bit)
        0x4000    | // MAP_NORESERVE
        0x8000    | // MAP_POPULATE
        0x10000   | // MAP_NONBLOCK
        0x20000   | // MAP_STACK
        0x40000   | // MAP_HUGETLB
        0x80000   | // MAP_SYNC
        0x100000  | // MAP_FIXED_NOREPLACE
        0x40000000; // MAP_UNINITIALIZED

    constexpr int LINUX_MAP_FIXED_NOREPLACE = 0x100000;
    const bool fixed_noreplace = (flags & LINUX_MAP_FIXED_NOREPLACE) != 0;

    // IMPORTANT: strip Linux-only flags from the input flags, not from darwin_flags
    // after mapping — both use bit 0x1000, so stripping after would drop MAP_ANON.
    const int clean_flags = flags & ~(LINUX_ONLY_MAP_FLAGS | 0xFC000000);

    int darwin_flags = 0;
    switch (clean_flags & LINUX_MAP_SHARED_VALIDATE) {
        case LINUX_MAP_SHARED:
        case LINUX_MAP_SHARED_VALIDATE: darwin_flags |= MAP_SHARED; break;
        case LINUX_MAP_PRIVATE: darwin_flags |= MAP_PRIVATE; break;
        default: errno = EINVAL; return MAP_FAILED;
    }
    if ((clean_flags & LINUX_MAP_FIXED) && !fixed_noreplace) darwin_flags |= MAP_FIXED;

    int darwin_fd = fd;
    if (clean_flags & LINUX_MAP_ANONYMOUS) {
        darwin_flags |= MAP_ANON;
        darwin_fd = -1;  // Darwin requires fd == -1 for anonymous mappings
        if (prot & PROT_EXEC) {
            darwin_flags |= MAP_JIT;
        }
    }

    void* const orig_addr = addr;
    const size_t orig_len = length;
    const long host_page = ::sysconf(_SC_PAGESIZE);
    const uintptr_t page_mask = (host_page > 0 ? static_cast<uintptr_t>(host_page) : 16384u) - 1;

    if (length == 0 || length > SIZE_MAX - page_mask ||
        ((darwin_flags & MAP_FIXED) &&
         reinterpret_cast<uintptr_t>(addr) > UINTPTR_MAX - length - page_mask)) {
        errno = length == 0 ? EINVAL : ENOMEM;
        return MAP_FAILED;
    }

    if ((darwin_flags & MAP_FIXED) &&
        kudroid::ExecMemory::GuestMemoryOverlaps(addr, length)) {
        void* write = kudroid::ExecMemory::GuestWritePointer(addr, length);
        if (!write || !(clean_flags & LINUX_MAP_ANONYMOUS) ||
            darwin_fd >= 0 || (darwin_flags & MAP_SHARED)) {
            errno = ENOTSUP;
            return MAP_FAILED;
        }
        // Preserve prepared backing and neighbouring guest bytes.
        if (bionic_mprotect(addr, length, prot) != 0) return MAP_FAILED;
        std::memset(write, 0, length);
        if (prot & PROT_EXEC) sys_icache_invalidate(addr, length);
        log_mmap_result(addr, length, prot, flags, fd, offset, addr);
        return addr;
    }

    if (darwin_fd < 0 && (clean_flags & LINUX_MAP_ANONYMOUS) &&
        (darwin_flags & MAP_PRIVATE) &&
        !(clean_flags & LINUX_MAP_FIXED) && !fixed_noreplace &&
        (prot == PROT_NONE || (prot & PROT_EXEC)) &&
        kudroid::ExecMemory::GuestReservationsAvailable()) {
        void* p = kudroid::ExecMemory::AllocateGuest(length, prot);
        if (p != MAP_FAILED || prot != PROT_NONE || errno != ENOMEM) {
            if (p != MAP_FAILED && bionic_mprotect(p, length, prot) != 0) p = MAP_FAILED;
            log_mmap_result(p, length, prot, flags, fd, offset, addr);
            return p;
        }
        // Pool capacity must not limit ordinary non-executable reservations.
    }

    // Reserve explicit maximum permissions before any guest code is stored.
    if (darwin_fd < 0 && prot == PROT_NONE &&
        (darwin_flags & MAP_PRIVATE) && (clean_flags & LINUX_MAP_ANONYMOUS) &&
        !(clean_flags & LINUX_MAP_FIXED) &&
        !fixed_noreplace && length > 0 && !quiet && !kudroid::ExecMemory::TxmPresent()) {
        mach_vm_address_t vm_addr = addr ? reinterpret_cast<mach_vm_address_t>(addr) : 0;
        const kern_return_t kr_map = mach_vm_map(
            mach_task_self(), &vm_addr, length, 0, VM_FLAGS_ANYWHERE,
            MACH_PORT_NULL, 0, FALSE, VM_PROT_NONE, VM_PROT_ALL, VM_INHERIT_DEFAULT);
        if (kr_map == KERN_SUCCESS) {
            void* p_vm = reinterpret_cast<void*>(static_cast<uintptr_t>(vm_addr));
            bool mapped = false;
            int current = 0, maximum = 0;
            if (host_range_query(vm_addr, length, &mapped, &current, &maximum) &&
                mapped && current == PROT_NONE && (maximum & VM_PROT_ALL) == VM_PROT_ALL) {
                if (!quiet) std::fprintf(stderr, "[KuDroidMmap] executable reservation source=mach\n");
                log_mmap_result(p_vm, length, prot, flags, fd, offset, addr);
                return p_vm;
            }
            mach_vm_deallocate(mach_task_self(), vm_addr, length);
        }

        void* p = ::mmap(addr, length, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
        if (p != MAP_FAILED) {
            bool mapped = false;
            int current = 0, maximum = 0;
            if (::mprotect(p, length, PROT_NONE) == 0 &&
                host_range_query(reinterpret_cast<uintptr_t>(p), length,
                                 &mapped, &current, &maximum) && mapped &&
                current == PROT_NONE && (maximum & VM_PROT_ALL) == VM_PROT_ALL) {
                if (!quiet) std::fprintf(stderr, "[KuDroidMmap] executable reservation source=map-jit\n");
                log_mmap_result(p, length, prot, flags, fd, offset, addr);
                return p;
            }
            ::munmap(p, length);
        }
        if (!quiet) std::fprintf(stderr, "[KuDroidMmap] executable reservation unavailable; reserving data pages\n");
    }
    mach_vm_address_t exclusive_base = 0;
    mach_vm_size_t exclusive_length = 0;
    if (fixed_noreplace) {
        const uintptr_t requested = reinterpret_cast<uintptr_t>(orig_addr);
        const size_t bias = requested & page_mask;
        if ((requested & 4095u) || orig_len == 0 || orig_len > SIZE_MAX - bias ||
            (darwin_fd >= 0 && (offset < 0 || (static_cast<uint64_t>(offset) & page_mask) != bias))) {
            errno = EINVAL;
            return MAP_FAILED;
        }
        exclusive_base = requested & ~page_mask;
        exclusive_length = orig_len + bias;
        // Claim the address atomically before replacing only our own reservation.
        const kern_return_t kr = mach_vm_allocate(mach_task_self(), &exclusive_base,
                                                  exclusive_length, VM_FLAGS_FIXED);
        if (kr != KERN_SUCCESS) {
            errno = kr == KERN_NO_SPACE ? EEXIST : kr == KERN_INVALID_ADDRESS ? EINVAL : ENOMEM;
            return MAP_FAILED;
        }
        addr = reinterpret_cast<void*>(exclusive_base);
        if (darwin_fd < 0) length = static_cast<size_t>(exclusive_length);
        darwin_flags |= MAP_FIXED;
    }
    auto validate_noreplace = [&](void* mapped) {
        if (mapped == MAP_FAILED && fixed_noreplace) {
            const int failure = errno;
            mach_vm_deallocate(mach_task_self(), exclusive_base, exclusive_length);
            errno = failure;
        }
        return mapped;
    };

    // Check if the host page range is already mapped. On Apple Silicon (16KB page),
    // calling host ::mmap(..., MAP_FIXED) on an already mapped page wipes the entire 16KB
    // page with zeros, destroying neighboring 4KB allocations in the same host page.
    if (darwin_fd < 0 && (darwin_flags & MAP_FIXED) && orig_addr != nullptr && !fixed_noreplace) {
        const uintptr_t uaddr = reinterpret_cast<uintptr_t>(orig_addr);
        const uintptr_t aligned_start = uaddr & ~page_mask;
        const uintptr_t aligned_end = (uaddr + orig_len + page_mask) & ~page_mask;
        const size_t aligned_len = static_cast<size_t>(aligned_end - aligned_start);

        bool is_mapped = false;
        int cur_prot = 0;
        if (host_range_query(aligned_start, aligned_len, &is_mapped, &cur_prot) && is_mapped) {
            if (bionic_mprotect(orig_addr, orig_len, PROT_READ | PROT_WRITE) != 0) return MAP_FAILED;
            std::memset(orig_addr, 0, orig_len);
            if (bionic_mprotect(orig_addr, orig_len, prot) != 0) return MAP_FAILED;
            log_mmap_result(orig_addr, orig_len, prot, flags, fd, offset, orig_addr);
            return const_cast<void*>(orig_addr);
        }
    }

    // Advisory hints never replace existing mappings, including PROT_NONE reservations.
    // Align unaligned MAP_FIXED anonymous mapping to host page size.
    if (darwin_fd < 0 && (darwin_flags & MAP_FIXED) && addr != nullptr) {
        const uintptr_t uaddr = reinterpret_cast<uintptr_t>(addr);
        if ((uaddr & page_mask) != 0) {
            const uintptr_t aligned_start = uaddr & ~page_mask;
            const uintptr_t aligned_end = (uaddr + length + page_mask) & ~page_mask;
            addr = reinterpret_cast<void*>(aligned_start);
            length = static_cast<size_t>(aligned_end - aligned_start);
        }
    }

    // File-backed mapping with a sub-page-start offset.
    //
    // Linux pages are 4 KiB and Darwin's are 16 KiB, but bionic code was compiled
    // against the Linux value and aligns offsets to it. A guest that mmaps a large
    // file at offset 0x401000 hands Darwin an offset that is not a multiple of the
    // host page size, and mmap answers EINVAL — which surfaces downstream as a read
    // that silently never happens. Align the offset DOWN to the host page and return
    // a pointer shifted by the difference, so the guest still reads from the address
    // it asked for. The tail may map a page past the requested range; that is the
    // same over-map a whole-page mapping does, and it is PROT_READ-tracked so it
    // cannot write.
    if (darwin_fd >= 0 && offset > 0) {
        const long host_page = ::sysconf(_SC_PAGESIZE);
        if (host_page > 0 && (offset % host_page) != 0) {
            const off_t aligned = offset - (offset % host_page);
            const off_t delta = offset - aligned;
            void* p = ::mmap(addr, static_cast<size_t>(delta) + length, prot, darwin_flags,
                             darwin_fd, aligned);
            p = validate_noreplace(p);
            log_mmap_result(p, length, prot, flags, fd, offset, addr);
            if (p == MAP_FAILED) {
                if (kudroid_fd_is_apk(darwin_fd)) {
                    static std::atomic<int> s_mmapFail{0};
                    if (s_mmapFail.load() < 15) {
                        ++s_mmapFail;
                        std::fprintf(stderr,
                                     "[KuDroidApk] mmap fail len=%zu offset=%lld aligned=%lld "
                                     "errno=%d\n",
                                     length, static_cast<long long>(offset),
                                     static_cast<long long>(aligned), errno);
                    }
                }
                return p;
            }
            if (kudroid_fd_is_apk(darwin_fd)) {
                static std::atomic<int> s_mmapOk{0};
                if (s_mmapOk.load() < 15) {
                    ++s_mmapOk;
                    std::fprintf(stderr,
                                 "[KuDroidApk] mmap ok len=%zu offset=%lld aligned=%lld "
                                 "delta=%lld total=%lluMB\n",
                                 length, static_cast<long long>(offset),
                                 static_cast<long long>(aligned), static_cast<long long>(delta),
                                 static_cast<unsigned long long>(
                                     (static_cast<uint64_t>(aligned) + length) / (1024ull * 1024ull)));
                }
            }
            return static_cast<char*>(p) + delta;
        }
    }
    if (darwin_fd >= 0 && offset == 0 && length > 0) {
        void* p = ::mmap(addr, length, prot, darwin_flags, darwin_fd, offset);
        p = validate_noreplace(p);
        log_mmap_result(p, length, prot, flags, fd, offset, addr);
        if (p != MAP_FAILED && kudroid_fd_is_apk(darwin_fd)) {
            static std::atomic<int> s_mmapZero{0};
            if (s_mmapZero.load() < 5) {
                ++s_mmapZero;
                std::fprintf(stderr, "[KuDroidApk] mmap ok len=%zu offset=0\n", length);
            }
        } else if (p == MAP_FAILED && kudroid_fd_is_apk(darwin_fd)) {
            static std::atomic<int> s_mmapZeroFail{0};
            if (s_mmapZeroFail.load() < 5) {
                ++s_mmapZeroFail;
                std::fprintf(stderr, "[KuDroidApk] mmap fail len=%zu offset=0 errno=%d\n",
                             length, errno);
            }
        }
        if (p != MAP_FAILED && (darwin_flags & MAP_FIXED) && orig_addr != nullptr) {
            return const_cast<void*>(orig_addr);
        }
        return p;
    }
    {
        void* p = ::mmap(addr, length, prot, darwin_flags, darwin_fd, offset);
        p = validate_noreplace(p);
        // A failing commit inside Unity's reserved heap is invisible today: the
        // caller only sees a null/short allocation later, which then surfaces as
        // a null structure (e.g. the pointer array a worker writes into). Log
        // the actual failure here so the allocator, not the crash, is the clue.
        log_mmap_result(p, length, prot, flags, fd, offset, addr);
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
        if (p != MAP_FAILED && (prot & PROT_EXEC)) {
            sys_icache_invalidate(p, length);
            if (auto fn = reinterpret_cast<void(*)(int)>(::dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_np"))) {
                fn(1);
                asm volatile("isb sy" ::: "memory");
            }
        }
#endif
        if (p != MAP_FAILED && (darwin_flags & MAP_FIXED) && orig_addr != nullptr) {
            return const_cast<void*>(orig_addr);
        }
        return p;
    }
#else
    void* p = ::mmap(addr, length, prot, flags, fd, offset);
    log_mmap_result(p, length, prot, flags, fd, offset, addr);
    return p;
#endif
}

extern "C" void* bionic_mmap64(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    return bionic_mmap(addr, length, prot, flags, fd, offset);
}

static inline void align_range_to_host_page(void* addr, size_t len, void*& out_addr, size_t& out_len) {
    const long host_page = ::sysconf(_SC_PAGESIZE);
    const uintptr_t page_mask = (host_page > 0 ? static_cast<uintptr_t>(host_page) : 4096u) - 1;
    const uintptr_t uaddr = reinterpret_cast<uintptr_t>(addr);
    const uintptr_t aligned_start = uaddr & ~page_mask;
    const uintptr_t aligned_end = (uaddr + len + page_mask) & ~page_mask;
    out_addr = reinterpret_cast<void*>(aligned_start);
    out_len = static_cast<size_t>(aligned_end - aligned_start);
}

// Whole host pages inside [addr, addr + len): the part of a guest range that can be given
// to an operation applying to host pages (munmap, advisory hints). The guest sees 4 KB
// pages; the host's are larger, so a range's end pages are shared with whatever lives
// beside it, and acting on them damages that neighbour.
extern "C" int kudroid_page_range_inward(uintptr_t addr, size_t len, size_t page,
                                         uintptr_t* out_start, uintptr_t* out_len) {
    if (page == 0 || (page & (page - 1)) != 0 || len == 0 || addr > UINTPTR_MAX - len) return 0;
    const uintptr_t mask = static_cast<uintptr_t>(page) - 1;
    const uintptr_t start = (addr + mask) & ~mask;
    const uintptr_t end = (addr + len) & ~mask;
    if (end <= start) return 0;
    if (out_start != nullptr) *out_start = start;
    if (out_len != nullptr) *out_len = end - start;
    return 1;
}

#if defined(__APPLE__)
// Protection of the host page holding `addr`, or -1 when it is not mapped.
static int host_page_protection(uintptr_t addr, int* maximum = nullptr) {
    vm_address_t region_addr = static_cast<vm_address_t>(addr);
    vm_size_t region_size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object_name = MACH_PORT_NULL;
    const kern_return_t result = vm_region_64(
        mach_task_self(), &region_addr, &region_size, VM_REGION_BASIC_INFO_64,
        reinterpret_cast<vm_region_info_t>(&info), &count, &object_name);
    if (object_name != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object_name);
    if (result != KERN_SUCCESS) {
        return -1;
    }
    if (static_cast<uintptr_t>(region_addr) > addr) return -1;
    if (maximum) *maximum = static_cast<int>(info.max_protection);
    return static_cast<int>(info.protection);
}

static bool host_range_has_protection(uintptr_t addr, size_t len, int prot) {
    const size_t page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    for (size_t offset = 0; offset < len; offset += page) {
        int maximum = 0;
        const int current = host_page_protection(addr + offset, &maximum);
        if (current < 0) { errno = ENOMEM; return false; }
        if ((current & prot) != prot || (maximum & prot) != prot ||
            (prot == PROT_NONE && current != PROT_NONE)) {
            errno = EACCES;
            return false;
        }
    }
    return true;
}

// mprotect on guest (4 KB) ranges over host (16 KB) pages. Whole pages inside the range take
// the new protection. An end page shared with a neighbour only takes it when that does not
// remove access: a guard or decommit of one guest page must not turn the live page next to it
// inaccessible. Granting access on a shared page is harmless, so it is applied.
static int mprotect_host_pages(uintptr_t addr, size_t len, int prot) {
    const uintptr_t host_page = static_cast<uintptr_t>(::sysconf(_SC_PAGESIZE));
    const uintptr_t mask = host_page - 1;
    const uintptr_t end = addr + len;
    const uintptr_t first = addr & ~mask;
    const uintptr_t last = (end - 1) & ~mask;
    for (uintptr_t page = first; page <= last; page += host_page) {
        const bool whole = page >= addr && page + host_page <= end;
        int effective_prot = prot;
        if (!whole) {
            const int current = host_page_protection(page);
            if (current >= 0) {
                effective_prot |= current;
                if (effective_prot == current) continue;
            }
        }
        // Coalesce whole pages into one call.
        uintptr_t run_end = page + host_page;
        if (whole) {
            while (run_end + host_page <= end && run_end <= last) run_end += host_page;
        }
        if (::mprotect(reinterpret_cast<void*>(page), run_end - page, effective_prot) != 0 ||
            !host_range_has_protection(page, run_end - page, effective_prot)) return -1;
        page = run_end - host_page;
    }
    return 0;
}
#endif

extern "C" int bionic_mprotect(void *addr, size_t len, int prot) {
    if (len == 0) return 0;
    const uintptr_t address = reinterpret_cast<uintptr_t>(addr);
    const size_t host_page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    if (!addr || len > SIZE_MAX - (host_page - 1) ||
        address > UINTPTR_MAX - len - (host_page - 1)) {
        errno = ENOMEM;
        return -1;
    }
    void* aligned_addr = nullptr;
    size_t aligned_len = 0;
    align_range_to_host_page(addr, len, aligned_addr, aligned_len);

#if defined(__APPLE__)
    int r = mprotect_host_pages(reinterpret_cast<uintptr_t>(addr), len, prot);
    if (r == 0 && (prot & PROT_EXEC)) {
        sys_icache_invalidate(aligned_addr, aligned_len);
    }
#else
    int r = ::mprotect(aligned_addr, aligned_len, prot);
    if (r == 0 && (prot & PROT_EXEC)) {
        __builtin___clear_cache(reinterpret_cast<char*>(aligned_addr),
                                reinterpret_cast<char*>(aligned_addr) + aligned_len);
    }
#endif
    const int saved_errno = errno;
    if (r == 0 && !(prot & PROT_WRITE)) {
        kudroid::GuestElfWritableAliases().RemoveOverlapping(address, len);
    }
    const bool quiet = kudroid::guest_async_handler_active();
    if (quiet) {
        // see bionic_mmap
    } else if (r != 0) {
        // Unity commits heap pages by flipping a PROT_NONE reservation to RW.
        // A failed mprotect leaves the allocation unusable with no other trace.
        static std::atomic<int> s_mprotFail{0};
        if (s_mprotFail.load() < 20) {
            ++s_mprotFail;
            std::fprintf(stderr,
                         "[KuDroidMmap] mprotect FAILED addr=%p len=%zu prot=0x%x errno=%d (%s)\n",
                         aligned_addr, aligned_len, prot, saved_errno, std::strerror(saved_errno));
        }
    } else if ((prot & PROT_WRITE) && aligned_len >= (1u << 20)) {
        // A successful big RW commit answers the other half of the same
        // question: if the guest flipped a reservation to RW, the pages are
        // valid and a later fault inside them is an allocator-walk bug, not a
        // missing commit. Only failures used to be logged, so this half of
        // the story was invisible. Cap: commits are batched, not per-page.
        static std::atomic<int> s_commit{0};
        if (s_commit.load(std::memory_order_relaxed) < 200) {
            s_commit.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr,
                         "[KuDroidMmap] mprotect RW addr=%p len=%zu prot=0x%x\n",
                         aligned_addr, aligned_len, prot);
        }
    }
    if (!quiet && (prot & PROT_EXEC)) {
        char current[4] = "???", maximum[4] = "???";
        kudroid::QueryRegionProt(addr, current, maximum);
        std::fprintf(stderr,
                     "[KuDroidMmap] mprotect EXEC addr=%p len=%zu prot=0x%x ret=%d errno=%d cur=%s max=%s\n",
                     addr, len, prot, r, r == 0 ? 0 : saved_errno, current, maximum);
    }
    errno = saved_errno;
    return r;
}

extern "C" int bionic_munmap(void *addr, size_t len) {
    if (!addr || len == 0) return 0;
#if defined(__APPLE__)
    if (kudroid::ExecMemory::GuestMemoryOverlaps(addr, len)) {
        void* write = kudroid::ExecMemory::GuestWritePointer(addr, len);
        if (!write) { errno = EINVAL; return -1; }
        if (bionic_mprotect(addr, len, PROT_NONE) != 0) return -1;
        std::memset(write, 0, len);
        return 0;
    }
#endif
    // Only whole host pages are returned. The partial pages at either end hold whatever
    // the guest placed beside this range; unmapping them, or hinting that their contents
    // are reclaimable, would zero or fault live neighbouring data.
    const long host_page = ::sysconf(_SC_PAGESIZE);
    uintptr_t start = 0, span = 0;
    if (kudroid_page_range_inward(reinterpret_cast<uintptr_t>(addr), len,
                                  host_page > 0 ? static_cast<size_t>(host_page) : 16384u,
                                  &start, &span)) {
        if (::munmap(reinterpret_cast<void*>(start), span) == 0) {
            kudroid::GuestElfWritableAliases().RemoveOverlapping(start, span);
        }
    }
    return 0;
}

#ifdef __APPLE__
static size_t prot_prefix_len(uintptr_t addr, size_t len, vm_prot_t required);

// Zero private anonymous pages without replacing mappings or losing their capabilities.
// File-backed and shared ranges retain the host advisory operation.
static int linux_madv_dontneed(void* addr, size_t length) {
    const long host_page = ::sysconf(_SC_PAGESIZE);
    const uintptr_t page_mask = (host_page > 0 ? static_cast<uintptr_t>(host_page) : 16384u) - 1;
    const uintptr_t begin = reinterpret_cast<uintptr_t>(addr);
    const uintptr_t finish = begin + length;
    const uintptr_t inner_begin = (begin + page_mask) & ~page_mask;
    const uintptr_t inner_end = finish & ~page_mask;

    auto zero_edge = [](uintptr_t from, uintptr_t to) {
        if (to <= from) return;
        const size_t writable = prot_prefix_len(from, to - from, VM_PROT_READ | VM_PROT_WRITE);
        if (writable > 0) std::memset(reinterpret_cast<void*>(from), 0, writable);
    };
    if (inner_end <= inner_begin) {
        zero_edge(begin, finish);
        return 0;
    }
    zero_edge(begin, inner_begin);
    zero_edge(inner_end, finish);

    uintptr_t cur = inner_begin;
    while (cur < inner_end) {
        mach_vm_address_t region_addr = static_cast<mach_vm_address_t>(cur);
        mach_vm_size_t region_size = 0;
        kudroid_vm_region_basic_info_64_t basic{};
        mach_msg_type_number_t basic_count = KUDROID_VM_REGION_BASIC_INFO_64_COUNT;
        mach_port_t object = MACH_PORT_NULL;
        if (mach_vm_region(mach_task_self(), &region_addr, &region_size,
                           KUDROID_VM_REGION_BASIC_INFO_64_FLAVOR,
                           reinterpret_cast<vm_region_info_t>(&basic), &basic_count,
                           &object) != KERN_SUCCESS) {
            errno = ENOMEM;
            return -1;
        }
        if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
        if (region_addr > cur || region_addr >= inner_end) { errno = ENOMEM; return -1; }
        object = MACH_PORT_NULL;

        kudroid_vm_region_extended_info_t ext{};
        mach_msg_type_number_t ext_count = KUDROID_VM_REGION_EXTENDED_INFO_COUNT;
        mach_vm_address_t ext_addr = static_cast<mach_vm_address_t>(cur);
        mach_vm_size_t ext_size = 0;
        const bool have_ext =
            mach_vm_region(mach_task_self(), &ext_addr, &ext_size,
                           KUDROID_VM_REGION_EXTENDED_INFO_FLAVOR,
                           reinterpret_cast<vm_region_info_t>(&ext), &ext_count,
                           &object) == KERN_SUCCESS;
        if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
        if (!have_ext) { errno = ENOMEM; return -1; }

        uintptr_t seg_end = static_cast<uintptr_t>(region_addr + region_size);
        if (seg_end > inner_end) seg_end = inner_end;
        const bool anonymous_private = !basic.shared && have_ext && !ext.external_pager;
        if (anonymous_private && seg_end > cur) {
            const vm_prot_t writable = VM_PROT_READ | VM_PROT_WRITE;
            const bool change = (basic.protection & writable) != writable;
            if (change &&
                mach_vm_protect(mach_task_self(), cur, seg_end - cur, FALSE,
                                writable) != KERN_SUCCESS) {
                errno = EACCES;
                return -1;
            }
            bool mapped = false;
            int current = 0, maximum = 0;
            const bool can_zero = host_range_query(cur, seg_end - cur, &mapped,
                                                   &current, &maximum) && mapped &&
                                  (current & writable) == writable &&
                                  (maximum & basic.max_protection) == basic.max_protection;
            if (can_zero) std::memset(reinterpret_cast<void*>(cur), 0, seg_end - cur);
            const kern_return_t restored = change
                ? mach_vm_protect(mach_task_self(), cur, seg_end - cur, FALSE, basic.protection)
                : KERN_SUCCESS;
            const bool intact = restored == KERN_SUCCESS &&
                host_range_query(cur, seg_end - cur, &mapped, &current, &maximum) && mapped &&
                (current & VM_PROT_ALL) == (basic.protection & VM_PROT_ALL) &&
                (maximum & basic.max_protection) == basic.max_protection;
            if (!can_zero || !intact) {
                errno = EACCES;
                return -1;
            }
            if (basic.protection & VM_PROT_EXECUTE) {
                sys_icache_invalidate(reinterpret_cast<void*>(cur), seg_end - cur);
            }
        } else if (seg_end > cur) {
            if (::madvise(reinterpret_cast<void*>(cur), seg_end - cur, MADV_DONTNEED) != 0) return -1;
        }
        if (seg_end <= cur) { errno = ENOMEM; return -1; }
        cur = seg_end;
    }
    return 0;
}
#endif

// Linux and Darwin number madvise advice differently from 5 up (Linux MADV_FREE is 8,
// Darwin's 8 is MADV_FREE_REUSE), so the value is translated, not forwarded.
// Pure hints with no Darwin counterpart succeed without doing anything.
extern "C" int bionic_madvise(void *addr, size_t length, int advice) {
    if (length == 0) return 0;
    if (!addr) { errno = ENOMEM; return -1; }
    const size_t page = kudroid::ExecMemory::PageSize();
    if (length > SIZE_MAX - (page - 1) ||
        reinterpret_cast<uintptr_t>(addr) > UINTPTR_MAX - length - (page - 1)) {
        errno = EINVAL;
        return -1;
    }
    constexpr int kLinuxMadvDontneed = 4;
    constexpr int kLinuxMadvFree = 8;
    constexpr int kLinuxMadvRemove = 9;
#ifdef __APPLE__
    if (kudroid::ExecMemory::GuestMemoryOverlaps(addr, length)) {
        void* write = kudroid::ExecMemory::GuestWritePointer(addr, length);
        if (!write) { errno = ENOMEM; return -1; }
        if (advice == kLinuxMadvDontneed) {
            std::memset(write, 0, length);
            return 0;
        }
        // Reclaim hints must retain debugger-prepared backing.
        if (advice == kLinuxMadvFree) return 0;
    }
    if (advice == kLinuxMadvDontneed) {
        return linux_madv_dontneed(addr, length);
    }
    int host_advice;
    if (advice >= 0 && advice <= 3) {
        host_advice = advice;  // NORMAL, RANDOM, SEQUENTIAL, WILLNEED agree
    } else if (advice == kLinuxMadvFree) {
        host_advice = MADV_FREE;
    } else if (advice == kLinuxMadvRemove) {
        errno = EINVAL;
        return -1;
    } else {
        return 0;
    }
    // Advice that lets the kernel reclaim (MADV_FREE) must only reach pages the range owns
    // outright; a partial end page also holds a neighbour's live bytes. The other hints are
    // harmless on a shared page and keep the outward rounding.
    if (host_advice == MADV_FREE) {
        const long host_page = ::sysconf(_SC_PAGESIZE);
        uintptr_t start = 0, span = 0;
        if (kudroid_page_range_inward(reinterpret_cast<uintptr_t>(addr), length,
                                      host_page > 0 ? static_cast<size_t>(host_page) : 16384u,
                                      &start, &span)) {
            ::madvise(reinterpret_cast<void*>(start), span, host_advice);
        }
        return 0;
    }
    void* aligned_addr = nullptr;
    size_t aligned_len = 0;
    align_range_to_host_page(addr, length, aligned_addr, aligned_len);
    ::madvise(aligned_addr, aligned_len, host_advice);
    return 0;
#else
    (void)kLinuxMadvFree;
    (void)kLinuxMadvRemove;
    void* aligned_addr = nullptr;
    size_t aligned_len = 0;
    align_range_to_host_page(addr, length, aligned_addr, aligned_len);
    return ::madvise(aligned_addr, aligned_len, advice);
#endif
}

