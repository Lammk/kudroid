#include "kudroid/platform/CameraShim.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

extern "C" void kudroid_looper_wake_main(void);

namespace {

constexpr int32_t kEventFrame = 1;
constexpr int32_t kEventPhoto = 2;
constexpr int32_t kEventError = 4;

std::mutex g_hostMtx;
KuDroidCameraHost g_host = {};
bool g_hasHost = false;

std::mutex g_dataMtx;
std::vector<uint8_t> g_frame;  // NV21
int g_frameWidth = 0;
int g_frameHeight = 0;
std::vector<uint8_t> g_photo;
int g_error = 0;
std::atomic<int32_t> g_pending{0};

// Copy the host table so a call never runs against a table being replaced.
bool host_snapshot(KuDroidCameraHost* out) {
    std::lock_guard<std::mutex> lock(g_hostMtx);
    if (!g_hasHost) return false;
    *out = g_host;
    return true;
}

void post(int32_t event) {
    g_pending.fetch_or(event);
    kudroid_looper_wake_main();
}

}  // namespace

extern "C" void kudroid_set_camera_host(const KuDroidCameraHost* host) {
    std::lock_guard<std::mutex> lock(g_hostMtx);
    if (host != nullptr) {
        g_host = *host;
        g_hasHost = true;
    } else {
        g_host = {};
        g_hasHost = false;
    }
}

extern "C" void kudroid_camera_push_frame(const uint8_t* yPlane, int yStride,
                                          const uint8_t* cbcrPlane, int cbcrStride, int width,
                                          int height, int rotate180) {
    if (yPlane == nullptr || cbcrPlane == nullptr || width <= 0 || height <= 0 ||
        (width & 1) != 0 || (height & 1) != 0 || yStride < width || cbcrStride < width) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_dataMtx);
        const size_t ySize = static_cast<size_t>(width) * height;
        g_frame.resize(ySize * 3 / 2);
        g_frameWidth = width;
        g_frameHeight = height;
        uint8_t* yOut = g_frame.data();
        uint8_t* vuOut = yOut + ySize;
        if (rotate180 == 0) {
            for (int r = 0; r < height; ++r) {
                std::memcpy(yOut + static_cast<size_t>(r) * width,
                            yPlane + static_cast<size_t>(r) * yStride, width);
            }
            // Cb/Cr pairs become Cr/Cb.
            for (int r = 0; r < height / 2; ++r) {
                const uint8_t* src = cbcrPlane + static_cast<size_t>(r) * cbcrStride;
                uint8_t* dst = vuOut + static_cast<size_t>(r) * width;
                for (int c = 0; c < width; c += 2) {
                    dst[c] = src[c + 1];
                    dst[c + 1] = src[c];
                }
            }
        } else {
            for (int r = 0; r < height; ++r) {
                const uint8_t* src = yPlane + static_cast<size_t>(r) * yStride;
                uint8_t* dst = yOut + static_cast<size_t>(height - 1 - r) * width;
                for (int c = 0; c < width; ++c) dst[width - 1 - c] = src[c];
            }
            for (int r = 0; r < height / 2; ++r) {
                const uint8_t* src = cbcrPlane + static_cast<size_t>(r) * cbcrStride;
                uint8_t* dst = vuOut + static_cast<size_t>(height / 2 - 1 - r) * width;
                for (int c = 0; c < width / 2; ++c) {
                    dst[width - 2 - 2 * c] = src[2 * c + 1];
                    dst[width - 1 - 2 * c] = src[2 * c];
                }
            }
        }
    }
    post(kEventFrame);
}

extern "C" void kudroid_camera_push_photo(const uint8_t* jpeg, int length) {
    {
        std::lock_guard<std::mutex> lock(g_dataMtx);
        if (jpeg != nullptr && length > 0) {
            g_photo.assign(jpeg, jpeg + length);
        } else {
            g_photo.clear();
        }
    }
    post(kEventPhoto);
}

extern "C" void kudroid_camera_report_error(int error) {
    {
        std::lock_guard<std::mutex> lock(g_dataMtx);
        g_error = error;
    }
    post(kEventError);
}

extern "C" void kudroid_camera_reset(void) {
    bionic_kudroid_camera_set_preview(0, 0);
    bionic_kudroid_camera_close();
    std::lock_guard<std::mutex> lock(g_dataMtx);
    g_frame.clear();
    g_frameWidth = g_frameHeight = 0;
    g_photo.clear();
    g_error = 0;
}

extern "C" int kudroid_camera_has_pending(void) { return g_pending.load() != 0 ? 1 : 0; }

extern "C" int32_t bionic_kudroid_camera_count(void) {
    KuDroidCameraHost host;
    if (!host_snapshot(&host) || host.count == nullptr) return 0;
    return host.count();
}

extern "C" int32_t bionic_kudroid_camera_info(int32_t cameraId) {
    KuDroidCameraHost host;
    if (!host_snapshot(&host) || host.info == nullptr || host.count == nullptr) return -1;
    if (cameraId < 0 || cameraId >= host.count()) return -1;
    int facing = 0;
    int orientation = 0;
    if (host.info(cameraId, &facing, &orientation) != 0) return -1;
    return (facing & 0xFF) | ((orientation & 0x1FF) << 8);
}

extern "C" int32_t bionic_kudroid_camera_open(int32_t cameraId) {
    KuDroidCameraHost host;
    if (!host_snapshot(&host) || host.open == nullptr) return -1;
    {
        std::lock_guard<std::mutex> lock(g_dataMtx);
        g_frame.clear();
        g_frameWidth = g_frameHeight = 0;
        g_photo.clear();
        g_error = 0;
    }
    g_pending.store(0);
    return host.open(cameraId);
}

extern "C" void bionic_kudroid_camera_close(void) {
    KuDroidCameraHost host;
    if (host_snapshot(&host) && host.close != nullptr) host.close();
    g_pending.store(0);
}

extern "C" int32_t bionic_kudroid_camera_start(int32_t width, int32_t height) {
    KuDroidCameraHost host;
    if (!host_snapshot(&host) || host.start == nullptr) return -1;
    return host.start(width, height);
}

extern "C" void bionic_kudroid_camera_stop(void) {
    KuDroidCameraHost host;
    if (host_snapshot(&host) && host.stop != nullptr) host.stop();
}

extern "C" int32_t bionic_kudroid_camera_take_picture(void) {
    KuDroidCameraHost host;
    if (!host_snapshot(&host) || host.take_picture == nullptr) return -1;
    return host.take_picture();
}

extern "C" void bionic_kudroid_camera_set_preview(int32_t visible, int32_t displayRotation) {
    KuDroidCameraHost host;
    if (host_snapshot(&host) && host.set_preview != nullptr) host.set_preview(visible, displayRotation);
}

extern "C" int32_t bionic_kudroid_camera_events(void) { return g_pending.exchange(0); }

extern "C" int32_t bionic_kudroid_camera_frame_size(void) {
    std::lock_guard<std::mutex> lock(g_dataMtx);
    return (g_frameWidth << 16) | (g_frameHeight & 0xFFFF);
}

extern "C" int32_t bionic_kudroid_camera_copy_frame(uint8_t* dst, int32_t capacity) {
    std::lock_guard<std::mutex> lock(g_dataMtx);
    if (dst == nullptr || g_frame.empty() || capacity < static_cast<int32_t>(g_frame.size())) return 0;
    std::memcpy(dst, g_frame.data(), g_frame.size());
    return static_cast<int32_t>(g_frame.size());
}

extern "C" int32_t bionic_kudroid_camera_photo_size(void) {
    std::lock_guard<std::mutex> lock(g_dataMtx);
    return static_cast<int32_t>(g_photo.size());
}

extern "C" int32_t bionic_kudroid_camera_copy_photo(uint8_t* dst, int32_t capacity) {
    std::lock_guard<std::mutex> lock(g_dataMtx);
    if (dst == nullptr || g_photo.empty() || capacity < static_cast<int32_t>(g_photo.size())) return 0;
    std::memcpy(dst, g_photo.data(), g_photo.size());
    return static_cast<int32_t>(g_photo.size());
}

extern "C" int32_t bionic_kudroid_camera_take_error(void) {
    std::lock_guard<std::mutex> lock(g_dataMtx);
    const int error = g_error;
    g_error = 0;
    return error;
}
