
#ifdef __APPLE__

static int create_loopback_udp() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(fd); return -1;
    }
    socklen_t len = sizeof(addr);
    getsockname(fd, (struct sockaddr*)&addr, &len);
    connect(fd, (struct sockaddr*)&addr, len);
    
    // Set non-blocking just in case
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

// Keep the guest clock for absolute timer deadlines, including disarmed timers.
static std::map<int, dispatch_source_t> g_timerfds;
static std::map<int, int> g_timerfd_clocks;
static std::mutex g_timerfds_mtx;
#endif

// Fake fds (emulated inotify/signalfd) — bionic_close must not leak them.
static std::set<int> g_fakefds;
static std::mutex g_fakefds_mtx;


extern "C" int bionic_eventfd(unsigned int initval, int flags) {
#ifdef __APPLE__
    (void)flags;
    int fd = create_loopback_udp();
    if (fd >= 0 && initval > 0) {
        uint64_t val = initval;
        write(fd, &val, sizeof(val));
    }
    if (fd >= 0) {
        // Emulated fd (loopback UDP) — register bionic_close bookkeeping y .
        std::lock_guard<std::mutex> lock(g_fakefds_mtx);
        g_fakefds.insert(fd);
    }
    return fd;
#else
    return ::eventfd(initval, flags);
#endif
}

extern "C" int bionic_timerfd_create(int clockid, int flags) {
#ifdef __APPLE__
    (void)flags;
    timespec now{};
    if (bionic_clock_gettime(clockid, &now) != 0) return -1;
    const int fd = create_loopback_udp();
    if (fd >= 0) {
        {
            std::lock_guard<std::mutex> lock(g_timerfds_mtx);
            g_timerfd_clocks.emplace(fd, clockid);
        }
        std::lock_guard<std::mutex> lock(g_fakefds_mtx);
        g_fakefds.insert(fd);
    }
    return fd;
#else
    return ::timerfd_create(clockid, flags);
#endif
}

struct bionic_itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};

extern "C" int bionic_timerfd_settime(int fd, int flags, const struct bionic_itimerspec *new_value, struct bionic_itimerspec *old_value) {
#ifdef __APPLE__
    (void)old_value;
    if (!new_value) { errno = EFAULT; return -1; }
    const auto valid_time = [](const timespec& value) {
        return value.tv_sec >= 0 && value.tv_nsec >= 0 && value.tv_nsec < 1000000000;
    };
    if (!valid_time(new_value->it_value) || !valid_time(new_value->it_interval)) {
        errno = EINVAL;
        return -1;
    }
    
    std::lock_guard<std::mutex> lock(g_timerfds_mtx);
    const auto clock = g_timerfd_clocks.find(fd);
    if (clock == g_timerfd_clocks.end()) {
        errno = fd < 0 || ::fcntl(fd, F_GETFD) < 0 ? EBADF : EINVAL;
        return -1;
    }
    timespec now{};
    if ((flags & 1) && bionic_clock_gettime(clock->second, &now) != 0) return -1;
    const int64_t start = kudroid::timer_deadline_delay_ns(new_value->it_value, now, flags & 1);
    const uint64_t interval_ns = static_cast<uint64_t>(
        kudroid::timer_deadline_delay_ns(new_value->it_interval, timespec{}, false));
    const uint64_t interval = interval_ns == 0 ? DISPATCH_TIME_FOREVER : interval_ns;
    const bool disarm = new_value->it_value.tv_sec == 0 && new_value->it_value.tv_nsec == 0;
    dispatch_source_t timer = disarm ? nullptr : dispatch_source_create(
        DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));
    if (!disarm && !timer) { errno = ENOMEM; return -1; }
    if (g_timerfds.count(fd)) {
        dispatch_source_cancel(g_timerfds[fd]);
        dispatch_release(g_timerfds[fd]);
        g_timerfds.erase(fd);
    }
    
    if (disarm) return 0;
    
    dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, start), interval, 1000000); // 1ms leeway
    dispatch_source_set_event_handler(timer, ^{
        std::lock_guard<std::mutex> lock(g_timerfds_mtx);
        const auto active = g_timerfds.find(fd);
        if (active == g_timerfds.end() || active->second != timer) return;
        uint64_t exp = 1;
        write(fd, &exp, sizeof(exp));
    });
    dispatch_resume(timer);
    g_timerfds[fd] = timer;
    
    return 0;
#else
    return ::timerfd_settime(fd, flags, reinterpret_cast<const struct itimerspec*>(new_value), reinterpret_cast<struct itimerspec*>(old_value));
#endif
}

extern "C" int bionic_epoll_create(int size) {
#ifdef __APPLE__
    (void)size;
    return kqueue();
#else
    return ::epoll_create(size);
#endif
}
extern "C" int bionic_epoll_create1(int flags) { 
#ifdef __APPLE__
    (void)flags; return kqueue(); 
#else
    return ::epoll_create1(flags);
#endif
}

// close() wrapper — cleans up timerfd GCD timers when the fd is closed.
extern "C" int bionic_close(int fd) {
    fd_forget_apk(fd);
    if (net_fd(fd)) {
        net_fd_set(fd, false);
        if (net_trace_take()) std::fprintf(stderr, "[KuDroidNet] close fd=%d\n", fd);
    }
#ifdef __APPLE__
    {
        std::lock_guard<std::mutex> lock(g_timerfds_mtx);
        g_timerfd_clocks.erase(fd);
        auto it = g_timerfds.find(fd);
        if (it != g_timerfds.end()) {
            dispatch_source_cancel(it->second);
            dispatch_release(it->second);
            g_timerfds.erase(it);
        }
    }
#endif
    // Drop ashmem prot/region entries; fake fds also need munmap.
    {
        std::lock_guard<std::mutex> lock(g_ashmem_mtx);
        const bool is_fake = g_ashmem_region.find(fd) != g_ashmem_region.end();
        if (is_fake) {
            ashmem_forget(fd);
            return 0; // fake fd is not a real fd — do not close it.
        }
        g_ashmem_prot.erase(fd);
    }
    {
        // Fake inotify/signalfd fd — close the real socket.
        std::lock_guard<std::mutex> lock(g_fakefds_mtx);
        g_fakefds.erase(fd);
    }
    return ::close(fd);
}

extern "C" int bionic_epoll_ctl(int epfd, int op, int fd, void *event_ptr) {
#ifdef __APPLE__
    if (epfd < 0 || fd < 0) return -1;
    struct android_epoll_event* event = static_cast<struct android_epoll_event*>(event_ptr);
    
    if (op == EPOLL_CTL_DEL) {
        struct kevent changes[2];
        EV_SET(&changes[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        EV_SET(&changes[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        // Execute individually to safely ignore ENOENT (filter not found)
        kevent(epfd, &changes[0], 1, NULL, 0, NULL);
        kevent(epfd, &changes[1], 1, NULL, 0, NULL);
        return 0;
    }

    uint16_t base_flags = EV_ADD;
    uint32_t ep_events = event->events;
    void* udata = reinterpret_cast<void*>(event->data);
    
    // Android defines: EPOLLET = (1U << 31), EPOLLONESHOT = (1U << 30)
    if (ep_events & (1U << 31)) base_flags |= EV_CLEAR;
    if (ep_events & (1U << 30)) base_flags |= EV_ONESHOT;

    if (op == EPOLL_CTL_MOD) {
        // Blindly delete both first
        struct kevent del_changes[2];
        EV_SET(&del_changes[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        EV_SET(&del_changes[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        kevent(epfd, &del_changes[0], 1, NULL, 0, NULL);
        kevent(epfd, &del_changes[1], 1, NULL, 0, NULL);
    }
    
    struct kevent changes[2];
    int num_changes = 0;
    
    if (ep_events & EPOLLIN) {
        EV_SET(&changes[num_changes++], fd, EVFILT_READ, base_flags, 0, 0, udata);
    }
    if (ep_events & EPOLLOUT) {
        EV_SET(&changes[num_changes++], fd, EVFILT_WRITE, base_flags, 0, 0, udata);
    }
    
    if (num_changes > 0) {
        if (kevent(epfd, changes, num_changes, NULL, 0, NULL) == -1) return -1;
    }
    return 0;
#else
    return ::epoll_ctl(epfd, op, fd, reinterpret_cast<struct epoll_event*>(event_ptr));
#endif
}

extern "C" int bionic_epoll_wait(int epfd, void *events_ptr, int maxevents, int timeout) {
#ifdef __APPLE__
    if (epfd < 0 || maxevents <= 0) return -1;
    struct android_epoll_event* events = static_cast<struct android_epoll_event*>(events_ptr);
    
    // We fetch maxevents * 2 because read and write events for the same FD are separated in kqueue.
    // Use a bounded stack buffer for small requests, heap for large ones.
    const int kStackEvents = 64;
    struct kevent stackBuf[kStackEvents * 2];
    std::vector<struct kevent> heapBuf;
    struct kevent* evlist = stackBuf;
    if (maxevents > kStackEvents) {
        heapBuf.resize(static_cast<size_t>(maxevents) * 2);
        evlist = heapBuf.data();
    }
    
    struct timespec ts;
    struct timespec* ts_ptr = NULL;
    if (timeout >= 0) {
        ts.tv_sec = timeout / 1000;
        ts.tv_nsec = (timeout % 1000) * 1000000;
        ts_ptr = &ts;
    }
    
    // timeout < 0 means "wait indefinitely", which is the case worth naming: a guest
    // event loop whose fd never becomes ready sits here for the life of the process.
    // A bounded wait is left untracked, because a report for something that will end
    // on its own is noise.
    int n;
    if (timeout < 0) {
        const BlockingWaitScope tracked(WaitKind::kEpoll,
                                        reinterpret_cast<const void*>(
                                            static_cast<uintptr_t>(epfd)),
                                        guest_return_address(6));
        n = kevent(epfd, NULL, 0, evlist, maxevents * 2, ts_ptr);
    } else {
        n = kevent(epfd, NULL, 0, evlist, maxevents * 2, ts_ptr);
    }
    int unique_events = 0;
    
    if (n > 0) {
        // Coalesce by fd, not udata; one fd may report both read+write.
        std::vector<uint64_t> order;
        std::unordered_map<uint64_t, std::pair<uint32_t, uint64_t>> coalesced;
        for (int i = 0; i < n; i++) {
            const uint64_t fd = static_cast<uint64_t>(evlist[i].ident);
            uint32_t flags = 0;
            if (evlist[i].filter == EVFILT_READ) flags |= EPOLLIN;
            else if (evlist[i].filter == EVFILT_WRITE) flags |= EPOLLOUT;
            if (evlist[i].flags & EV_ERROR) flags |= EPOLLERR;
            if (evlist[i].flags & EV_EOF) flags |= EPOLLHUP;
            auto& entry = coalesced[fd];
            if (std::find(order.begin(), order.end(), fd) == order.end()) {
                order.push_back(fd); // keep first-seen order
            }
            entry.first |= flags;
            entry.second = reinterpret_cast<uint64_t>(evlist[i].udata);
        }
        
        for (const uint64_t fd : order) {
            if (unique_events >= maxevents) break;
            events[unique_events].events = coalesced[fd].first;
            events[unique_events].data = coalesced[fd].second;
            unique_events++;
        }
    }
    return n >= 0 ? unique_events : -1;
#else
    if (timeout < 0) {
        const BlockingWaitScope tracked(WaitKind::kEpoll,
                                        reinterpret_cast<const void*>(
                                            static_cast<uintptr_t>(epfd)),
                                        guest_return_address(6));
        return ::epoll_wait(epfd, reinterpret_cast<struct epoll_event*>(events_ptr), maxevents, timeout);
    }
    return ::epoll_wait(epfd, reinterpret_cast<struct epoll_event*>(events_ptr), maxevents, timeout);
#endif
}

// The signal mask is not applied; the wait itself is epoll_wait.
extern "C" int bionic_epoll_pwait(int epfd, void* events_ptr, int maxevents, int timeout,
                                  const void* sigmask) {
    (void)sigmask;
    return bionic_epoll_wait(epfd, events_ptr, maxevents, timeout);
}

extern "C" int bionic_pthread_condattr_init(void* attr) {
    if (!attr) return EINVAL;
    const uint32_t word = 0;
    std::memcpy(attr, &word, sizeof(word));
    return 0;
}
extern "C" int bionic_pthread_condattr_destroy(void* attr) { (void)attr; return 0; }
