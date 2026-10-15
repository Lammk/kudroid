#include "kudroid/platform/AudioCapture.h"
#include "kudroid/kudroid_bridge.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <objc/message.h>
#include <objc/runtime.h>
#endif

// Microphone capture: a preallocated ring of 16-bit samples filled by the host's capture
// callback and drained by AudioRecord.read. The ring overwrites its oldest samples when
// the app falls behind, since live audio is only useful fresh.

namespace {

constexpr int32_t kErrorBadValue = -2;
constexpr int32_t kErrorInvalidOperation = -3;
constexpr int32_t kErrorDeadObject = -6;
constexpr int32_t kMinSampleRate = 4000;
constexpr int32_t kMaxSampleRate = 192000;
constexpr int kQueueBuffers = 4;
constexpr auto kStallLimit = std::chrono::milliseconds(2000);
constexpr auto kWaitSlice = std::chrono::milliseconds(50);

struct Recorder {
    int32_t sampleRate = 0;
    int32_t channels = 0;

    std::mutex mtx;
    std::condition_variable cv;
    std::vector<int16_t> ring;
    size_t readPos = 0;
    size_t count = 0;  // samples buffered
    bool recording = false;
    bool released = false;

#if defined(__APPLE__)
    AudioQueueRef queue = nullptr;
    AudioQueueBufferRef buffers[kQueueBuffers] = {};
    void* queueUser = nullptr;  // heap std::shared_ptr<Recorder>*
#endif
};

std::mutex g_recordersMtx;
std::unordered_map<int64_t, std::shared_ptr<Recorder>> g_recorders;
int64_t g_nextHandle = 1;
std::atomic<int32_t> g_openRecorders{0};

std::shared_ptr<Recorder> find_recorder(int64_t handle) {
    std::lock_guard<std::mutex> lock(g_recordersMtx);
    const auto it = g_recorders.find(handle);
    return it == g_recorders.end() ? nullptr : it->second;
}

bool valid_format(int32_t sampleRate, int32_t channels) {
    return sampleRate >= kMinSampleRate && sampleRate <= kMaxSampleRate && channels >= 1 &&
           channels <= 2;
}

// Append captured samples, overwriting the oldest when the ring is full.
void push_samples(Recorder* r, const int16_t* samples, size_t n) {
    std::lock_guard<std::mutex> lock(r->mtx);
    const size_t cap = r->ring.size();
    if (r->released || cap == 0 || !r->recording) return;
    if (n > cap) {
        samples += n - cap;
        n = cap;
    }
    const size_t overflow = r->count + n > cap ? r->count + n - cap : 0;
    r->readPos = (r->readPos + overflow) % cap;
    r->count -= overflow;
    size_t writePos = (r->readPos + r->count) % cap;
    const size_t first = std::min(n, cap - writePos);
    std::memcpy(&r->ring[writePos], samples, first * sizeof(int16_t));
    if (n > first) std::memcpy(&r->ring[0], samples + first, (n - first) * sizeof(int16_t));
    r->count += n;
    r->cv.notify_all();
}

#if defined(__APPLE__)
// Input needs a record-capable category; playback-only otherwise.
void apple_set_session_record(bool record) {
    Class cls = objc_getClass("AVAudioSession");
    if (!cls) return;
    id session = reinterpret_cast<id (*)(Class, SEL)>(objc_msgSend)(
        cls, sel_registerName("sharedInstance"));
    if (!session) return;
    Class strCls = objc_getClass("NSString");
    id category = reinterpret_cast<id (*)(Class, SEL, const char*)>(objc_msgSend)(
        strCls, sel_registerName("stringWithUTF8String:"),
        record ? "AVAudioSessionCategoryPlayAndRecord" : "AVAudioSessionCategoryPlayback");
    // MixWithOthers | AllowBluetooth | DefaultToSpeaker for record; MixWithOthers otherwise.
    const unsigned long options = record ? (0x1UL | 0x4UL | 0x8UL) : 0x1UL;
    reinterpret_cast<BOOL (*)(id, SEL, id, unsigned long, void*)>(objc_msgSend)(
        session, sel_registerName("setCategory:withOptions:error:"), category, options, nullptr);
    reinterpret_cast<BOOL (*)(id, SEL, BOOL, void*)>(objc_msgSend)(
        session, sel_registerName("setActive:error:"), YES, nullptr);
}

void input_callback(void* user, AudioQueueRef queue, AudioQueueBufferRef buffer,
                    const AudioTimeStamp*, UInt32 packets,
                    const AudioStreamPacketDescription*) {
    const auto rec = *static_cast<std::shared_ptr<Recorder>*>(user);
    if (!rec) return;
    push_samples(rec.get(), static_cast<const int16_t*>(buffer->mAudioData),
                 static_cast<size_t>(packets) * static_cast<size_t>(rec->channels));
    std::lock_guard<std::mutex> lock(rec->mtx);
    if (!rec->released && rec->recording) AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}

// Called with no recorder lock held.
bool open_queue(const std::shared_ptr<Recorder>& rec) {
    AudioStreamBasicDescription asbd = {};
    asbd.mSampleRate = rec->sampleRate;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    asbd.mBitsPerChannel = 16;
    asbd.mChannelsPerFrame = static_cast<UInt32>(rec->channels);
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = static_cast<UInt32>(rec->channels) * 2;
    asbd.mBytesPerPacket = asbd.mBytesPerFrame;
    auto* user = new std::shared_ptr<Recorder>(rec);
    AudioQueueRef queue = nullptr;
    OSStatus st = AudioQueueNewInput(&asbd, input_callback, user, nullptr, nullptr, 0, &queue);
    if (st != noErr || queue == nullptr) {
        std::fprintf(stderr, "[KuDroidAudio] input creation failed status=%d\n", static_cast<int>(st));
        delete user;
        return false;
    }
    const UInt32 bytes = static_cast<UInt32>(rec->sampleRate / 50) * asbd.mBytesPerFrame;
    AudioQueueBufferRef buffers[kQueueBuffers] = {};
    for (int i = 0; i < kQueueBuffers && st == noErr; ++i) {
        st = AudioQueueAllocateBuffer(queue, bytes, &buffers[i]);
        if (st == noErr) st = AudioQueueEnqueueBuffer(queue, buffers[i], 0, nullptr);
    }
    if (st != noErr) {
        std::fprintf(stderr, "[KuDroidAudio] input buffers failed status=%d\n", static_cast<int>(st));
        AudioQueueDispose(queue, true);
        delete user;
        return false;
    }
    std::lock_guard<std::mutex> lock(rec->mtx);
    rec->queue = queue;
    rec->queueUser = user;
    std::memcpy(rec->buffers, buffers, sizeof(buffers));
    return true;
}

void close_queue(const std::shared_ptr<Recorder>& rec) {
    AudioQueueRef queue = nullptr;
    void* user = nullptr;
    {
        std::lock_guard<std::mutex> lock(rec->mtx);
        queue = rec->queue;
        user = rec->queueUser;
        rec->queue = nullptr;
        rec->queueUser = nullptr;
    }
    if (queue != nullptr) AudioQueueDispose(queue, true);  // no callback runs after this
    delete static_cast<std::shared_ptr<Recorder>*>(user);
}
#endif

}  // namespace

extern "C" int32_t bionic_kudroid_audiorecord_min_buffer_size(int32_t sampleRateInHz,
                                                              int32_t channelCount) {
    if (!valid_format(sampleRateInHz, channelCount)) return kErrorBadValue;
    // 40 ms of audio.
    return (sampleRateInHz / 25) * channelCount * 2;
}

extern "C" int64_t bionic_kudroid_audiorecord_create(int32_t sampleRateInHz, int32_t channelCount,
                                                     int32_t bufferSizeInBytes) {
    if (!valid_format(sampleRateInHz, channelCount)) return 0;
    if (kudroid_get_host_authorization(0) != 1) {
        std::fprintf(stderr, "[KuDroidAudio] microphone is not authorized by the host\n");
        return 0;
    }
    auto rec = std::make_shared<Recorder>();
    rec->sampleRate = sampleRateInHz;
    rec->channels = channelCount;
    // At least half a second so a slow reader does not lose audio between reads.
    const size_t requested = bufferSizeInBytes > 0 ? static_cast<size_t>(bufferSizeInBytes) / 2 : 0;
    const size_t floorSamples = static_cast<size_t>(sampleRateInHz / 2) * channelCount;
    rec->ring.assign(std::max(requested * 2, floorSamples), 0);
    int64_t handle;
    {
        std::lock_guard<std::mutex> lock(g_recordersMtx);
        handle = g_nextHandle++;
        g_recorders[handle] = rec;
    }
    if (g_openRecorders.fetch_add(1) == 0) {
#if defined(__APPLE__)
        apple_set_session_record(true);
#endif
    }
    return handle;
}

extern "C" int32_t bionic_kudroid_audiorecord_start(int64_t handle) {
    const auto rec = find_recorder(handle);
    if (!rec) return kErrorInvalidOperation;
    {
        std::lock_guard<std::mutex> lock(rec->mtx);
        if (rec->released) return kErrorInvalidOperation;
        if (rec->recording) return 0;
    }
#if defined(__APPLE__)
    // A fresh queue per start: a stopped input queue is not reliably restartable.
    if (!open_queue(rec)) return kErrorDeadObject;
#endif
    {
        std::lock_guard<std::mutex> lock(rec->mtx);
        rec->recording = true;
        rec->readPos = 0;
        rec->count = 0;
    }
#if defined(__APPLE__)
    const OSStatus st = AudioQueueStart(rec->queue, nullptr);
    if (st != noErr) {
        std::fprintf(stderr, "[KuDroidAudio] input start failed status=%d\n", static_cast<int>(st));
        {
            std::lock_guard<std::mutex> lock(rec->mtx);
            rec->recording = false;
        }
        close_queue(rec);
        return kErrorDeadObject;
    }
#endif
    return 0;
}

extern "C" int32_t bionic_kudroid_audiorecord_stop(int64_t handle) {
    const auto rec = find_recorder(handle);
    if (!rec) return kErrorInvalidOperation;
    {
        std::lock_guard<std::mutex> lock(rec->mtx);
        if (!rec->recording) return 0;
        rec->recording = false;
    }
    rec->cv.notify_all();
#if defined(__APPLE__)
    close_queue(rec);
#endif
    return 0;
}

extern "C" int32_t bionic_kudroid_audiorecord_read(int64_t handle, void* data,
                                                   int32_t sizeInBytes, bool blocking) {
    const auto rec = find_recorder(handle);
    if (!rec || data == nullptr) return kErrorInvalidOperation;
    if (sizeInBytes < 0) return kErrorBadValue;
    const size_t frameBytes = static_cast<size_t>(rec->channels) * 2;
    const size_t want = static_cast<size_t>(sizeInBytes) / frameBytes * frameBytes;
    auto* out = static_cast<uint8_t*>(data);
    size_t copied = 0;
    bool stalled = false;

    std::unique_lock<std::mutex> lock(rec->mtx);
    auto lastData = std::chrono::steady_clock::now();
    while (copied < want) {
        if (rec->count > 0) {
            const size_t cap = rec->ring.size();
            const size_t take = std::min((want - copied) / sizeof(int16_t), rec->count);
            const size_t first = std::min(take, cap - rec->readPos);
            std::memcpy(out + copied, &rec->ring[rec->readPos], first * sizeof(int16_t));
            if (take > first) {
                std::memcpy(out + copied + first * sizeof(int16_t), &rec->ring[0],
                            (take - first) * sizeof(int16_t));
            }
            rec->readPos = (rec->readPos + take) % cap;
            rec->count -= take;
            copied += take * sizeof(int16_t);
            lastData = std::chrono::steady_clock::now();
            continue;
        }
        if (!blocking || !rec->recording || rec->released) break;
        rec->cv.wait_for(lock, kWaitSlice);
        if (std::chrono::steady_clock::now() - lastData > kStallLimit) {
            stalled = true;
            break;
        }
    }
    if (copied > 0) return static_cast<int32_t>(copied);
    if (rec->released) return kErrorInvalidOperation;
    if (!rec->recording) return kErrorInvalidOperation;
    return stalled ? kErrorDeadObject : 0;
}

extern "C" int32_t bionic_kudroid_audiorecord_release(int64_t handle) {
    std::shared_ptr<Recorder> rec;
    {
        std::lock_guard<std::mutex> lock(g_recordersMtx);
        const auto it = g_recorders.find(handle);
        if (it == g_recorders.end()) return 0;
        rec = it->second;
        g_recorders.erase(it);
    }
    {
        std::lock_guard<std::mutex> lock(rec->mtx);
        rec->recording = false;
        rec->released = true;
    }
    rec->cv.notify_all();
#if defined(__APPLE__)
    close_queue(rec);
#endif
    if (g_openRecorders.fetch_sub(1) == 1) {
#if defined(__APPLE__)
        apple_set_session_record(false);
#endif
    }
    return 0;
}

extern "C" int32_t kudroid_audio_capture_active(void) { return g_openRecorders.load(); }

extern "C" void kudroid_audio_capture_release_all(void) {
    std::vector<int64_t> handles;
    {
        std::lock_guard<std::mutex> lock(g_recordersMtx);
        for (const auto& entry : g_recorders) handles.push_back(entry.first);
    }
    for (const int64_t handle : handles) bionic_kudroid_audiorecord_release(handle);
}

extern "C" void kudroid_audio_capture_test_feed(const int16_t* samples, int32_t frames) {
    if (samples == nullptr || frames <= 0) return;
    std::vector<std::shared_ptr<Recorder>> all;
    {
        std::lock_guard<std::mutex> lock(g_recordersMtx);
        for (const auto& entry : g_recorders) all.push_back(entry.second);
    }
    for (const auto& rec : all) {
        push_samples(rec.get(), samples, static_cast<size_t>(frames) * rec->channels);
    }
}
