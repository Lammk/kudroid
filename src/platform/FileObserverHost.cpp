#include "kudroid/platform/FileObserverHost.h"

#include <cerrno>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#if defined(__APPLE__)
#include <sys/event.h>
#else
#include <poll.h>
#include <sys/inotify.h>
#endif

namespace kudroid {
namespace {
struct Watch {
    int fd = -1;
    uint32_t mask = 0;
    std::deque<FileWatchEvent> pending;
    std::mutex pollMutex;
#if defined(__APPLE__)
    int target = -1;
    bool directory = false;
#endif
    ~Watch() {
        if (fd >= 0) ::close(fd);
#if defined(__APPLE__)
        if (target >= 0) ::close(target);
#endif
    }
};
std::mutex watchesMutex;
std::unordered_map<uint64_t, std::shared_ptr<Watch>> watches;
uint64_t nextHandle = 1;
}

uint32_t supported_file_watch_events() {
#if defined(__APPLE__)
    // Vnode notifications do not expose access, open, or close events.
    return 0x002 | 0x004 | 0x400 | 0x800;
#else
    return 0xfff;
#endif
}

uint64_t start_file_watch(const std::string& path, uint32_t mask) {
    auto watch = std::make_shared<Watch>();
    watch->mask = mask & supported_file_watch_events();
    if (!watch->mask) return 0;
#if defined(__APPLE__)
    watch->target = ::open(path.c_str(), O_EVTONLY | O_CLOEXEC);
    if (watch->target < 0) return 0;
    struct stat st{};
    if (::fstat(watch->target, &st) != 0) return 0;
    watch->directory = S_ISDIR(st.st_mode);
    watch->fd = ::kqueue();
    if (watch->fd < 0) return 0;
    ::fcntl(watch->fd, F_SETFD, FD_CLOEXEC);
    uint32_t flags = NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB;
    if (!watch->directory) flags |= NOTE_WRITE | NOTE_EXTEND;
    struct kevent change{};
    EV_SET(&change, watch->target, EVFILT_VNODE, EV_ADD | EV_CLEAR, flags, 0, nullptr);
    if (::kevent(watch->fd, &change, 1, nullptr, 0, nullptr) < 0) return 0;
#else
    watch->fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (watch->fd < 0 || ::inotify_add_watch(watch->fd, path.c_str(), watch->mask) < 0) return 0;
#endif
    std::lock_guard<std::mutex> lock(watchesMutex);
    const uint64_t handle = nextHandle++;
    watches.emplace(handle, std::move(watch));
    return handle;
}

bool poll_file_watch(uint64_t handle, FileWatchEvent* event) {
    if (!event) return false;
    std::shared_ptr<Watch> watch;
    {
        std::lock_guard<std::mutex> lock(watchesMutex);
        auto it = watches.find(handle);
        if (it == watches.end()) return false;
        watch = it->second;
    }
    // Retain descriptors across a bounded poll while stop removes the handle.
    std::lock_guard<std::mutex> lock(watch->pollMutex);
    if (watch->pending.empty()) {
#if defined(__APPLE__)
        struct kevent result{};
        struct timespec timeout{0, 200000000};
        int count = ::kevent(watch->fd, nullptr, 0, &result, 1, &timeout);
        if (count <= 0 || (result.flags & EV_ERROR)) return false;
        uint32_t mask = 0;
        if (result.fflags & (NOTE_WRITE | NOTE_EXTEND)) mask |= 0x002;
        if (result.fflags & NOTE_ATTRIB) mask |= 0x004;
        if (result.fflags & NOTE_DELETE) mask |= 0x400;
        if (result.fflags & NOTE_RENAME) mask |= 0x800;
        mask &= watch->mask;
        if (mask) watch->pending.push_back({mask, {}});
#else
        struct pollfd ready{watch->fd, POLLIN, 0};
        if (::poll(&ready, 1, 200) <= 0 || !(ready.revents & POLLIN)) return false;
        alignas(inotify_event) char buffer[16384];
        const ssize_t count = ::read(watch->fd, buffer, sizeof(buffer));
        if (count <= 0) return false;
        size_t offset = 0;
        while (static_cast<size_t>(count) - offset >= sizeof(inotify_event)) {
            const auto* e = reinterpret_cast<const inotify_event*>(buffer + offset);
            const size_t remaining = static_cast<size_t>(count) - offset - sizeof(*e);
            if (e->len > remaining) break;
            const uint32_t mask = e->mask & watch->mask;
            if (mask) watch->pending.push_back({mask, e->len ? std::string(e->name, strnlen(e->name, e->len)) : std::string()});
            offset += sizeof(*e) + e->len;
        }
#endif
    }
    if (watch->pending.empty()) return false;
    *event = std::move(watch->pending.front());
    watch->pending.pop_front();
    return true;
}

void stop_file_watch(uint64_t handle) {
    std::lock_guard<std::mutex> lock(watchesMutex);
    watches.erase(handle);
}
}
