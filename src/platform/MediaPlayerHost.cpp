#include "kudroid/platform/MediaPlayerHost.h"
#include "kudroid/VFSPathRemapper.h"

#include <deque>
#include <mutex>
#include <string>

extern "C" void kudroid_looper_wake_main(void);

namespace {
std::mutex hostMutex;
KuDroidMediaPlayerHost host{};
std::mutex eventMutex;
struct Event { int64_t handle; int32_t what; int32_t extra; };
std::deque<Event> events;

KuDroidMediaPlayerHost snapshot() {
    std::lock_guard<std::mutex> lock(hostMutex);
    return host;
}
}

extern "C" void kudroid_set_media_player_host(const KuDroidMediaPlayerHost* value) {
    std::lock_guard<std::mutex> lock(hostMutex);
    host = value ? *value : KuDroidMediaPlayerHost{};
}

extern "C" int64_t kudroid_media_player_create() {
    auto value = snapshot();
    return value.create ? value.create() : 0;
}

extern "C" int32_t kudroid_media_player_source(int64_t handle, const char* path,
                                               int32_t fd, int64_t offset, int64_t length) {
    auto value = snapshot();
    if (!value.source) return -1;
    std::string mapped;
    if (path) {
        mapped = path;
        if (mapped.find("://") == std::string::npos || mapped.rfind("file://", 0) == 0 ||
            mapped.rfind("jar:", 0) == 0) {
            if (mapped.rfind("file://", 0) == 0) mapped.erase(0, 7);
            mapped = kudroid::VFSPathRemapper::getInstance().remap(mapped.c_str());
        }
    }
    return value.source(handle, path ? mapped.c_str() : nullptr, fd, offset, length);
}

extern "C" int64_t kudroid_media_player_command(int64_t handle, int32_t command,
                                                double a, double b) {
    auto value = snapshot();
    return value.command ? value.command(handle, command, a, b) : -1;
}

extern "C" void kudroid_media_player_post(int64_t handle, int32_t event, int32_t extra) {
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        events.push_back({handle, event, extra});
    }
    kudroid_looper_wake_main();
}

extern "C" int kudroid_media_player_take(int64_t* handle, int32_t* event, int32_t* extra) {
    std::lock_guard<std::mutex> lock(eventMutex);
    if (events.empty()) return 0;
    const auto value = events.front();
    events.pop_front();
    *handle = value.handle;
    *event = value.what;
    *extra = value.extra;
    return 1;
}

extern "C" void kudroid_media_player_reset() {
    auto value = snapshot();
    if (value.reset_all) value.reset_all();
    std::lock_guard<std::mutex> lock(eventMutex);
    events.clear();
}
