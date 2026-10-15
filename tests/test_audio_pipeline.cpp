#include "../src/platform/AudioShim.cpp"

#include <future>

static int failures = 0;
static void check(bool ok, const char* message) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", message);
    if (!ok) ++failures;
}

static void complete_buffer(void*, void* context) {
    static_cast<std::atomic<int>*>(context)->fetch_add(1);
}

static void destroy_from_callback(void* player, void* context) {
    kudroid::bionic_slObjectDestroy(player);
    static_cast<std::atomic<int>*>(context)->store(1);
}

int main(int argc, char** argv) {
    using namespace kudroid;
    const int encoding = argc > 1 ? std::atoi(argv[1]) : 2;
    const auto track = bionic_kudroid_audiotrack_create(12000, 1, encoding, 16384);
    auto p = audiotrack_find(track);
    p->workerRunning = true;
    const size_t bpf = player_bytes_per_frame(p.get());
    std::vector<float> samples(10000, 0.25f);
    if (encoding != 2) {
        bionic_kudroid_audiotrack_write(track, samples.data(), 256 * bpf);
        check(p->ringCount == 256, "low-rate PCM retains its source frame count");
        bionic_kudroid_audiotrack_flush(track);
    }
    const int bytes = 10000 * bpf;
    const int written = bionic_kudroid_audiotrack_write(track, samples.data(), bytes);
    check(written == static_cast<int>(kRingBufferFrames * bpf),
          "stopped write reports only frames accepted by the ring");
    check(p->ringCount <= kRingBufferFrames, "ring count never exceeds capacity");
    check(p->framesWritten == p->ringCount, "write counter counts accepted source frames");
    const auto count = p->ringCount;
    const int retry = bionic_kudroid_audiotrack_write(track, samples.data(), 32 * bpf);
    check(retry == 0 && p->ringCount == count, "full stopped ring preserves queued PCM");
    bionic_kudroid_audiotrack_play(track);
    check(bionic_kudroid_audiotrack_write_mode(track, samples.data(), 32 * bpf, false) == 0,
          "nonblocking write returns immediately when playing ring is full");
    auto writer = std::async(std::launch::async, [&] {
        return bionic_kudroid_audiotrack_write(track, samples.data(), 32 * bpf);
    });
    check(writer.wait_for(std::chrono::milliseconds(80)) == std::future_status::timeout,
          "blocking write waits beyond 50ms without discarding audio");
    check(bionic_kudroid_audiotrack_write_mode(track, samples.data(), 32 * bpf, false) == 0,
          "nonblocking writer does not wait behind a blocking writer");
    // A playing blocking writer is held to the short fill level, so the ring has to drain
    // to within 32 frames of it before the writer has room.
    const size_t fillLimit = std::max<size_t>(
        static_cast<size_t>(12000 * kBlockingFillSeconds), 2 * kAudioQueueFramesPerBuffer);
    {
        std::lock_guard<std::mutex> lock(p->mtx);
        player_read_pcm(p.get(), nullptr, 64);
    }
    p->cv.notify_all();
    check(writer.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout,
          "playing blocking write stays held while the ring is above the fill level");
    {
        std::lock_guard<std::mutex> lock(p->mtx);
        player_read_pcm(p.get(), nullptr, p->ringCount - (fillLimit - 32));
    }
    p->cv.notify_all();
    check(writer.get() == static_cast<int>(32 * bpf), "writer resumes after space becomes available");
    check(p->framesWritten == kRingBufferFrames + 32, "chunked write counts each frame once");
    {
        // Leave exactly 32 frames of room under the fill level.
        std::lock_guard<std::mutex> lock(p->mtx);
        player_read_pcm(p.get(), nullptr, 32);
    }
    p->cv.notify_all();
    writer = std::async(std::launch::async, [&] {
        return bionic_kudroid_audiotrack_write(track, samples.data(), 128 * bpf);
    });
    check(writer.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout,
          "second writer waits for full transfer");
    bionic_kudroid_audiotrack_release(track);
    check(writer.get() == static_cast<int>(32 * bpf), "release returns already accepted bytes and wakes writer");

    auto queued = std::make_shared<AudioPlayer>();
    queued->iface = queued.get();
    queued->workerRunning = true;
    queued->playState = SL_PLAYSTATE_PLAYING;
    check(player_write_pcm(queued.get(), samples.data(), 2048 * 4, false, true) == 8192,
          "buffer queue accepts a whole submitted buffer");
    AudioRead first, last;
    {
        std::lock_guard<std::mutex> lock(queued->mtx);
        first = player_read_pcm(queued.get(), nullptr, 1024);
        check(first.callbacks == 0 && queued->pendingCount == 1,
              "partial consumption keeps buffer pending without callback");
        last = player_read_pcm(queued.get(), nullptr, 1024);
        check(last.callbacks == 1 && queued->pendingCount == 0 && queued->queueIndex == 1,
              "submitted buffer completes once at its actual boundary");
        auto empty = player_read_pcm(queued.get(), nullptr, 1024);
        check(empty.callbacks == 0 && empty.frames == 0, "underrun produces no false completion");
    }

    SLDataFormat_PCM format{3, 2, 48000000, 16, 16, 3, 2};
    SLDataSource source{nullptr, &format};
    SLObjectItf player;
    check(bionic_slEngineCreateAudioPlayer(nullptr, &player, &source, nullptr, 0, nullptr, nullptr) == 0,
          "buffer-queue player is created");
    std::atomic<int> callbacks{0};
    bionic_slAndroidSimpleBufferQueueRegisterCallback(player, reinterpret_cast<void*>(&complete_buffer), &callbacks);
    bionic_slAndroidSimpleBufferQueueEnqueue(player, samples.data(), 8192);
    bionic_slPlaySetPlayState(player, 3);
    for (int i = 0; i < 100 && callbacks.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    check(callbacks == 1, "playback invokes one callback for a 2048-frame buffer");
    bionic_slObjectDestroy(player);
    bionic_slEngineCreateAudioPlayer(nullptr, &player, &source, nullptr, 0, nullptr, nullptr);
    callbacks = 0;
    bionic_slAndroidSimpleBufferQueueRegisterCallback(player, reinterpret_cast<void*>(&destroy_from_callback), &callbacks);
    bionic_slAndroidSimpleBufferQueueEnqueue(player, samples.data(), 2048);
    bionic_slPlaySetPlayState(player, 3);
    for (int i = 0; i < 100 && callbacks.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(callbacks == 1, "player destruction from its callback completes without deadlock");
    return failures ? 1 : 0;
}
