#include "kudroid/platform/InputShim.h"
#include "kudroid/platform/NativeTouchGate.h"
#include <cstdint>
#include <cstdio>
#include <new>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <atomic>
#include <mutex>
#include <deque>
#include <vector>
#include <chrono>

// Declared in SyscallShim.cpp.
extern "C" int bionic_ALooper_addFd(void* looper, int fd, int ident, int events,
                                    void* callback, void* data);
extern "C" void bionic_ALooper_markInputPipe(void* looper, int fd);

// The MessageQueue wake slot lives in NativeLooper.cpp (see the note there for
// why it is not in this file). Touch injection only needs to wake the main
// queue; the Looper thread drains and builds the MotionEvent itself.
extern "C" void kudroid_looper_wake_main(void);

namespace kudroid {
namespace {

// ─────────────────────────────────────────────────────────────────────────────
// AInputEvent / AMotionEvent simulated structure
// ─────────────────────────────────────────────────────────────────────────────
struct PointerCoord {
    float x = 0.0f;
    float y = 0.0f;
    int32_t id = 0;
};

struct BionicInputEvent {
    int32_t type = 2;  // 2 = AINPUT_EVENT_TYPE_MOTION
    int32_t action = 0;
    float x = 0.0f;
    float y = 0.0f;
    int64_t eventTime = 0;
    int32_t pointerCount = 0;
    int32_t source = 0;
    int32_t flags = 0;
    // One entry per finger in this event, sized by the event itself. A fixed table has
    // to clamp, and a clamped event is one whose fingers are missing from what the app
    // reads: the digitiser reports them, the event denies them.
    std::vector<PointerCoord> pointers;
    uint64_t seq = 0;  // queue sequence; matches copies back to deque entries
};

// Tracks active fingers across events so getX/getY can query any finger by index.
struct PointerSlot {
    bool active = false;
    int32_t id = 0;
    float x = 0.0f;
    float y = 0.0f;
};
static std::vector<PointerSlot> g_activePointers;

// The table is the live finger set; nothing here bounds its size.
static PointerSlot* findPointerSlot(int32_t id) {
    for (auto& slot : g_activePointers) {
        if (slot.active && slot.id == id) return &slot;
    }
    return nullptr;
}

static int livePointerCount() {
    int n = 0;
    for (const auto& slot : g_activePointers) {
        if (slot.active) ++n;
    }
    return n;
}

// Slot a finger occupies in the event's pointer array, which is what the action's
// pointer index refers to.
static int pointerIndexInTable(int32_t id) {
    int index = 0;
    for (const auto& slot : g_activePointers) {
        if (!slot.active) continue;
        if (slot.id == id) return index;
        ++index;
    }
    return 0;
}

// Touch cost telemetry.
//
// Only two places can turn a touch into cost for this process: the host-side
// inject (this thread, i.e. UIKit's) and the Java hand-off (the guest UI thread,
// which is where an interpreted dispatch runs). The log said "touch is slow" for
// dozens of commits without a number behind it; this prints the two numbers, one
// line per second of touch activity, bounded so a session stays readable.
extern "C" const char* kudroid_trace_stamp(void);

namespace {
std::atomic<uint64_t> g_touchCount{0};
std::atomic<uint64_t> g_touchInjectNs{0};
std::atomic<uint64_t> g_touchInjectMaxNs{0};
std::atomic<uint64_t> g_touchForwardNs{0};
std::atomic<uint64_t> g_touchJavaForwarded{0};
std::atomic<long long> g_touchWindowNs{0};
std::atomic<int> g_touchReported{0};

inline uint64_t touch_now_ns() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// One call per injected event: inject_ns is the whole ingress, forward_ns the Java
// hand-off inside it (zero when this event took the native AInputQueue path).
void touch_telemetry(uint64_t inject_ns, uint64_t forward_ns) {
    const bool forwarded = forward_ns != 0;
    g_touchCount.fetch_add(1, std::memory_order_relaxed);
    g_touchInjectNs.fetch_add(inject_ns, std::memory_order_relaxed);
    if (forwarded) g_touchForwardNs.fetch_add(forward_ns, std::memory_order_relaxed);
    if (forwarded) g_touchJavaForwarded.fetch_add(1, std::memory_order_relaxed);
    uint64_t seen_max = g_touchInjectMaxNs.load(std::memory_order_relaxed);
    while (inject_ns > seen_max &&
           !g_touchInjectMaxNs.compare_exchange_weak(seen_max, inject_ns,
                                                     std::memory_order_relaxed)) {
    }
    const long long now = static_cast<long long>(touch_now_ns());
    long long window = g_touchWindowNs.load(std::memory_order_relaxed);
    if (window == 0) {
        g_touchWindowNs.store(now, std::memory_order_relaxed);
        return;
    }
    if (now - window < 1000000000LL) return;
    if (!g_touchWindowNs.compare_exchange_strong(window, now, std::memory_order_relaxed)) return;
    const uint64_t n = g_touchCount.exchange(0, std::memory_order_relaxed);
    const uint64_t inject = g_touchInjectNs.exchange(0, std::memory_order_relaxed);
    const uint64_t fwd = g_touchForwardNs.exchange(0, std::memory_order_relaxed);
    const uint64_t max_ns = g_touchInjectMaxNs.exchange(0, std::memory_order_relaxed);
    const uint64_t java_n = g_touchJavaForwarded.exchange(0, std::memory_order_relaxed);
    if (n == 0) return;
    if (g_touchReported.fetch_add(1, std::memory_order_relaxed) >= 60) return;
    std::fprintf(stderr,
                 "%s[KuDroidTouch] inject n=%llu java=%llu avg=%lluus max=%lluus "
                 "forward-avg=%lluus\n",
                 kudroid_trace_stamp(),
                 static_cast<unsigned long long>(n), static_cast<unsigned long long>(java_n),
                 static_cast<unsigned long long>(inject / (n * 1000)),
                 static_cast<unsigned long long>(max_ns / 1000),
                 static_cast<unsigned long long>(java_n ? (fwd / (java_n * 1000)) : 0));
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// AInputQueue — a mutex-protected FIFO of input events.
//
// Real Android apps call AInputQueue_getEvent() in a loop (usually from a
// thread attached to a Looper). We provide a proper FIFO so events are not
// lost, plus the standard AInputQueue_* API surface.
// ─────────────────────────────────────────────────────────────────────────────
struct BionicInputQueue {
    std::mutex mtx;
    std::deque<BionicInputEvent> events;
    // getEvent hands the guest a heap copy, never interior storage: any
    // push_back/tail-replace/pop reallocates the deque, dangling a handed-out
    // &front() and turning every getter into a UAF (plus finishEvent's
    // address compare then never matches, redelivering one stale event
    // forever). Copies are matched back by sequence number; at most one is
    // outstanding per the AInputQueue contract, and a replacement getEvent
    // deletes an abandoned copy so leaks cannot accumulate.
    BionicInputEvent* activeCopy = nullptr;
    uint64_t nextSeq = 1;
    std::atomic<int32_t> id; // looper ident; zero until a native looper attaches
    int wakePipe[2]; // pipe to wake the looper when events arrive
    bool pipeReady;

    BionicInputQueue() : id(0), pipeReady(false) {
        wakePipe[0] = -1;
        wakePipe[1] = -1;
    }
};

static BionicInputQueue g_inputQueue;

// Ensure the wake pipe exists (lazily created on first use).
// Internal locks: kudroid_inject_touch_event (Swift thread) and attachLooper
// (game threads) can run concurrently — without a lock, pipe() runs twice,
// leak a pair of fd.
static void ensure_wake_pipe(BionicInputQueue* q) {
    std::lock_guard<std::mutex> lock(q->mtx);
    if (q->pipeReady) return;
    if (::pipe(q->wakePipe) == 0) {
        // Set both read and write ends non-blocking.
        int flags0 = ::fcntl(q->wakePipe[0], F_GETFL, 0);
        ::fcntl(q->wakePipe[0], F_SETFL, flags0 | O_NONBLOCK);
        int flags1 = ::fcntl(q->wakePipe[1], F_GETFL, 0);
        ::fcntl(q->wakePipe[1], F_SETFL, flags1 | O_NONBLOCK);
        q->pipeReady = true;
    }
}

// Exported for kudroid_bridge: AInputQueue pointer used to pass in
// ANativeActivity's onInputQueueCreated callback.
extern "C" void* kudroid_get_input_queue(void) {
    return &g_inputQueue;
}

// Wake the looper only if a native looper is attached and polling the queue.
static void wakeInputLooper() {
    if (g_inputQueue.id.load() != 0) {
        ensure_wake_pipe(&g_inputQueue);
        if (g_inputQueue.pipeReady) {
            uint8_t byte = 1;
            ssize_t unused = ::write(g_inputQueue.wakePipe[1], &byte, 1);
            (void)unused;
        }
    }
}

// Build one motion event from the live finger table and enqueue it. The caller holds
// g_inputQueue.mtx, which is also what guards the table.
//
// The table — not the producer's count — sizes the event: on a release the producer
// reports the fingers still down, but Android's pointer array must still contain the
// finger that is lifting, because that array position is what POINTER_UP's index refers
// to. The producer's count stays a lower bound for DOWN/MOVE so an index can never
// exceed the array the app will read.
static void buildTouchEventLocked(int32_t baseAction, int32_t primaryId, int32_t producerCount,
                                  float primaryX, float primaryY, BionicInputEvent& ev) {
    ev = BionicInputEvent();
    ev.type = 2;  // AINPUT_EVENT_TYPE_MOTION
    ev.x = primaryX;
    ev.y = primaryY;
    ev.eventTime = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    ev.source = 0x0002;  // AINPUT_SOURCE_TOUCHSCREEN

    // How many fingers are down right now. This — not the producer's pointer id — decides
    // DOWN vs POINTER_DOWN and UP vs POINTER_UP: a finger lifting while another stays down
    // is a POINTER_UP, and calling it ACTION_UP ends the gesture in the app's eyes. The
    // lifting finger is still in the table here, which is what the count needs.
    const int live = livePointerCount();
    int count = live;
    if (count < 1) count = 1;
    if (producerCount > count &&
        (baseAction == 0 || baseAction == 2 || baseAction == 5)) {
        count = producerCount;
    }

    int actionPointerIndex = pointerIndexInTable(primaryId);
    ev.pointers.clear();
    ev.pointers.reserve(static_cast<size_t>(count));
    for (const auto& slot : g_activePointers) {
        if (!slot.active) continue;
        if (static_cast<int>(ev.pointers.size()) >= count) break;
        PointerCoord coord;
        coord.id = slot.id;
        coord.x = slot.x;
        coord.y = slot.y;
        ev.pointers.push_back(coord);
    }
    // A producer reporting more fingers than the table tracks (a gesture that began before
    // the first DOWN was seen) is padded with the primary position: the count is what the
    // app indexes by, and a short array is a corrupting read from guest code.
    while (static_cast<int>(ev.pointers.size()) < count) {
        PointerCoord coord;
        coord.id = static_cast<int32_t>(ev.pointers.size());
        coord.x = primaryX;
        coord.y = primaryY;
        ev.pointers.push_back(coord);
    }
    ev.pointerCount = static_cast<int32_t>(ev.pointers.size());
    if (actionPointerIndex >= ev.pointerCount) actionPointerIndex = 0;

    // Android's action for this sample. The index is the pointer's slot in the array
    // built above, which still contains the lifting finger for POINTER_UP.
    int32_t finalAction = baseAction;
    if (baseAction == 0) {
        finalAction = (live > 1) ? ((actionPointerIndex << 8) | 5) : 0;
    } else if (baseAction == 1) {
        finalAction = (live > 1) ? ((actionPointerIndex << 8) | 6) : 1;
    } else if (baseAction == 3) {
        finalAction = 3;  // CANCEL ends the gesture for every finger
    } else if (baseAction == 5) {
        finalAction = (actionPointerIndex << 8) | 5;
    } else if (baseAction == 6) {
        finalAction = (actionPointerIndex << 8) | 6;
    }
    ev.action = finalAction;

    // Coalesce MOVE floods in the NDK queue: replace the pending MOVE instead of
    // enqueuing another event per touch. Unity drains the queue every frame and
    // interpolates; intermediate positions are dead weight. DOWN/UP/CANCEL always
    // enqueue — ordering there is semantic.
    const bool is_move = (finalAction & 0xff) == 2;  // ACTION_MOVE
    bool replaced = false;
    if (is_move && !g_inputQueue.events.empty()) {
        auto& tail = g_inputQueue.events.back();
        if (tail.type == 2 && (tail.action & 0xff) == 2) {
            // The seq is the identity AInputQueue_finishEvent matches by. Keeping it
            // lets the guest pop this entry when it finishes the copy it holds;
            // overwriting it with the fresh event's zero left the entry queued, so
            // every coalesce handed the same MOVE out twice and the queue could not
            // report itself empty. Folding replaces the sample the guest may already
            // be holding, which is what coalescing means — the UP that ends a gesture
            // carries the finger table, so the final position is never lost.
            const uint64_t seq = tail.seq;
            tail = ev;
            tail.seq = seq;
            replaced = true;
        }
    }
    if (!replaced) {
        ev.seq = g_inputQueue.nextSeq++;  // under q->mtx here
        g_inputQueue.events.push_back(ev);
    }
}

#include "kudroid/KuArtRuntime.h"

// The gate exists to keep a drag from costing one VM-locked interpreted dispatch
// per 60-120 Hz MOVE (see NativeTouchGate.cpp). It must also only run while a
// runtime is live: kuart_post_touch_event queues regardless, but a queue pushed
// before kuart_launch_app resets it with accepting_=false — those events were
// silently dropped after the log said they were dispatched.
//
// Returns the nanoseconds spent in the hand-off, or 0 when this event did not take
// the Java path (not ready, native queue attached, or gated out).
static uint64_t forward_touch_to_java_activity(int action, const BionicInputEvent& ev) {
    if (kuart_is_ready() != 1) return 0;
    // When a native looper is attached, the NDK AInputQueue path already delivers this
    // event; forwarding it again would dispatch it to Java twice, and every MOVE would
    // take the VM lock against the render loop. The Java forward exists for titles that
    // never attach the native queue (the common Unity case).
    if (g_inputQueue.id.load() != 0) return 0;
    const bool is_move = (action & 0xff) == 2;  // ACTION_MOVE
    if (is_move && kudroid_touch_source_gate_allow_move() != 1) return 0;
    // Native enqueue, then wake the Looper's native wait. The UI/Looper thread
    // drains and builds the MotionEvent itself (see MessageQueue.nativeDrainInput),
    // so no second thread ever takes the VM lock for touch. The whole pointer table
    // travels with the event: the Java MotionEvent needs one entry per finger, and a
    // single (x, y) can only ever describe one of them.
    // Every finger this event carries, in array order — the order the action's pointer
    // index refers to. Sized from the event, so no finger is left behind.
    //
    // Reused per-thread buffers: this runs once per MOVE inside the UIKit callback,
    // and three fresh vectors per sample is heap traffic a drag does not need. The
    // callee (TouchQueue::push) copies out of them before returning.
    static thread_local std::vector<int32_t> ids;
    static thread_local std::vector<float> xs;
    static thread_local std::vector<float> ys;
    const size_t count = ev.pointers.size();
    ids.resize(count);
    xs.resize(count);
    ys.resize(count);
    for (size_t i = 0; i < count; ++i) {
        ids[i] = ev.pointers[i].id;
        xs[i] = ev.pointers[i].x;
        ys[i] = ev.pointers[i].y;
    }
    if (ids.empty()) return 0;
    const uint64_t forward_start = touch_now_ns();
    kuart_post_touch_event_ex(action, static_cast<int>(ids.size()), ids.data(), xs.data(),
                              ys.data());
    kudroid_looper_wake_main();
    // Never zero: a hand-off that finished inside the clock's resolution is still a
    // hand-off, and the counter is what says which path this build is on.
    const uint64_t spent = touch_now_ns() - forward_start;
    return spent == 0 ? 1 : spent;
}

// Single-position transport: one finger's new position per call; the rest of the table
// keeps its last known position. UIKit uses the batch entry below instead — this one stays
// for callers that drive a finger at a time.
extern "C" void kudroid_inject_touch_event_multi(float x, float y, int32_t action,
                                                 int32_t pointerId, int32_t pointerCount) {
    const uint64_t inject_start = touch_now_ns();
    const int32_t baseAction = action & 0xff;
    BionicInputEvent ev;
    {
        std::lock_guard<std::mutex> lock(g_inputQueue.mtx);

        // Update active pointer tracking table
        if (baseAction == 0 || baseAction == 5) {
            // DOWN or POINTER_DOWN: a finger that is already tracked only moves; a new
            // one extends the table, which has no fixed number of slots to run out of.
            PointerSlot* slot = findPointerSlot(pointerId);
            if (slot == nullptr) {
                PointerSlot fresh;
                fresh.active = true;
                fresh.id = pointerId;
                fresh.x = x;
                fresh.y = y;
                g_activePointers.push_back(fresh);
            } else {
                slot->x = x;
                slot->y = y;
            }
        } else if (baseAction == 2) {
            // MOVE: update position of moving pointer
            if (PointerSlot* slot = findPointerSlot(pointerId)) {
                slot->x = x;
                slot->y = y;
            }
        }

        // The lifting finger stays in the table while the event is built: POINTER_UP's
        // index refers to its slot in the array the app reads. It leaves below.
        buildTouchEventLocked(baseAction, pointerId, pointerCount > 0 ? pointerCount : 1, x, y,
                              ev);
        // Only the released finger leaves the table; the others stay down, which is what
        // lets a multi-finger drag survive one finger lifting.
        if (baseAction == 3 || (baseAction == 1 && livePointerCount() <= 1)) {
            g_activePointers.clear();
        } else if (baseAction == 1 || baseAction == 6) {
            if (PointerSlot* slot = findPointerSlot(pointerId)) slot->active = false;
        }
    }

    wakeInputLooper();

    // Forward touch events to Java Activity (e.g. Unity uGUI buttons like "Skip Tutorial").
    // Non-move events (DOWN, UP, CANCEL) are always forwarded to ensure UI clicks trigger.
    // MOVE events are rate-gated by NativeTouchGate to avoid stalling the render loop.
    const uint64_t forwarded_ns = forward_touch_to_java_activity(ev.action, ev);
    touch_telemetry(touch_now_ns() - inject_start, forwarded_ns);
}

// Batch transport: the caller (UIKit) hands over every live finger at once.
//
// This is the multi-finger form, and the reason it exists: with one position per call, the
// other fingers' samples can only arrive as their own events, so an N-finger drag costs N
// dispatches per frame — heavier than one finger, which is the opposite of what a drag with
// more fingers should cost. Here one callback is one event carrying every finger's own
// position. POINTER_UP's index refers to the lifting finger's slot in the array as sent, so
// the caller includes a lifting finger in the batch it sends for UP and drops it afterwards.
extern "C" void kudroid_inject_touch_batch(int32_t action, int32_t primaryId, int32_t count,
                                           const int32_t* ids, const float* xs, const float* ys) {
    const uint64_t inject_start = touch_now_ns();
    const int32_t baseAction = action & 0xff;
    BionicInputEvent ev;
    {
        std::lock_guard<std::mutex> lock(g_inputQueue.mtx);
        if (count < 1) count = 1;
        // The batch *is* the live finger set: replacing the table wholesale is what makes
        // each event self-consistent, with no drift from a mutation the event never saw.
        g_activePointers.clear();
        if (ids != nullptr && xs != nullptr && ys != nullptr) {
            g_activePointers.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i) {
                PointerSlot slot;
                slot.active = true;
                slot.id = ids[i];
                slot.x = xs[i];
                slot.y = ys[i];
                g_activePointers.push_back(slot);
            }
        }
        const float primaryX = xs != nullptr ? xs[0] : 0.0f;
        const float primaryY = ys != nullptr ? ys[0] : 0.0f;
        buildTouchEventLocked(baseAction, primaryId, count, primaryX, primaryY, ev);

        if (baseAction == 3) {
            g_activePointers.clear();
        } else if (baseAction == 1 || baseAction == 6) {
            if (PointerSlot* slot = findPointerSlot(primaryId)) slot->active = false;
        }
    }

    wakeInputLooper();
    const uint64_t forwarded_ns = forward_touch_to_java_activity(ev.action, ev);
    touch_telemetry(touch_now_ns() - inject_start, forwarded_ns);
}

extern "C" void kudroid_inject_touch_event(float x, float y, int32_t action) {
    kudroid_inject_touch_event_multi(x, y, action, 0, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// AInputQueue API (Bionic)
// ─────────────────────────────────────────────────────────────────────────────

extern "C" void* bionic_AInputQueue_create() {
    return &g_inputQueue;
}

extern "C" int32_t bionic_AInputQueue_getEvent(void* queue, void** outEvent) {
    if (!queue || !outEvent) return -1;
    BionicInputQueue* q = static_cast<BionicInputQueue*>(queue);
    std::lock_guard<std::mutex> lock(q->mtx);
    if (q->events.empty()) {
        *outEvent = nullptr;
        return 0; // No event available (WOULD_BLOCK semantics)
    }
    // Heap copy, never &front(): see the struct comment. An abandoned previous
    // copy is deleted here; its event stays queued until finished by seq.
    delete q->activeCopy;
    q->activeCopy = new (std::nothrow) BionicInputEvent(q->events.front());
    *outEvent = q->activeCopy;
    return 0;
}

extern "C" int32_t bionic_AInputQueue_preDispatchEvent(void* queue, void* event) {
    (void)queue;
    (void)event;
    return 0; // Not predispatching
}

extern "C" int32_t bionic_AInputQueue_finishEvent(void* queue, void* event, int handled) {
    (void)handled;
    if (!queue || !event) return -1;
    BionicInputQueue* q = static_cast<BionicInputQueue*>(queue);
    std::lock_guard<std::mutex> lock(q->mtx);
    auto* copy = static_cast<BionicInputEvent*>(event);
    // Pop only when the copy still names the queued front: a stale/double
    // finish, or a finish after the queue moved on, must not pop a live event.
    if (copy == q->activeCopy && !q->events.empty() &&
        q->events.front().seq == copy->seq) {
        q->events.pop_front();
    }
    if (copy == q->activeCopy) {
        delete q->activeCopy;
        q->activeCopy = nullptr;
    }
    return 0;
}

extern "C" void bionic_AInputQueue_attachLooper(void* queue, void* looper, int ident,
                                                void* callback, void* data) {
    if (!queue) return;
    BionicInputQueue* q = static_cast<BionicInputQueue*>(queue);
    q->id.store(ident);

    // Register the wake pipe with the looper so poll() wakes on input.
    ensure_wake_pipe(q);
    if (q->pipeReady && looper) {
        bionic_ALooper_addFd(looper, q->wakePipe[0], ident, 0x0001 /* ALOOPER_EVENT_INPUT */,
                             callback, data);
        // Mark this fd as wake pipe of AInputQueue to ALooper_pollAll drain
        // water every time it is readable — otherwise the pipe will always be ready -> busy loop.
        bionic_ALooper_markInputPipe(looper, q->wakePipe[0]);
    }
}

extern "C" void bionic_AInputQueue_detachLooper(void* queue) {
    (void)queue;
}

extern "C" int32_t bionic_AInputQueue_hasEvents(void* queue) {
    if (!queue) return 0;
    BionicInputQueue* q = static_cast<BionicInputQueue*>(queue);
    std::lock_guard<std::mutex> lock(q->mtx);
    return q->events.empty() ? 0 : 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// AInputEvent / AMotionEvent getters
// ─────────────────────────────────────────────────────────────────────────────

extern "C" int32_t bionic_AInputEvent_getType(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->type;
}

extern "C" int32_t bionic_AInputEvent_getSource(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->source;
}

extern "C" int32_t bionic_AInputEvent_getFlags(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->flags;
}

extern "C" int64_t bionic_AInputEvent_getEventTime(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->eventTime;
}

extern "C" int32_t bionic_AMotionEvent_getAction(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->action;
}

extern "C" float bionic_AMotionEvent_getX(const void* event, size_t pointer_index) {
    if (!event) return 0.0f;
    const auto* ev = static_cast<const BionicInputEvent*>(event);
    if (pointer_index < ev->pointers.size()) {
        return ev->pointers[pointer_index].x;
    }
    return ev->x;
}

extern "C" float bionic_AMotionEvent_getY(const void* event, size_t pointer_index) {
    if (!event) return 0.0f;
    const auto* ev = static_cast<const BionicInputEvent*>(event);
    if (pointer_index < ev->pointers.size()) {
        return ev->pointers[pointer_index].y;
    }
    return ev->y;
}

extern "C" float bionic_AMotionEvent_getXByIndex(const void* event, size_t pointer_index) {
    return bionic_AMotionEvent_getX(event, pointer_index);
}

extern "C" float bionic_AMotionEvent_getYByIndex(const void* event, size_t pointer_index) {
    return bionic_AMotionEvent_getY(event, pointer_index);
}

extern "C" float bionic_AMotionEvent_getPressure(const void* event, size_t pointer_index) {
    (void)event; (void)pointer_index;
    return 1.0f;
}

extern "C" float bionic_AMotionEvent_getPressureByIndex(const void* event, size_t pointer_index) {
    (void)event; (void)pointer_index;
    return 1.0f;
}

extern "C" size_t bionic_AMotionEvent_getPointerCount(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->pointerCount;
}

extern "C" int32_t bionic_AMotionEvent_getPointerId(const void* event, size_t pointer_index) {
    if (!event) return static_cast<int32_t>(pointer_index);
    const auto* ev = static_cast<const BionicInputEvent*>(event);
    if (pointer_index < ev->pointers.size()) {
        return ev->pointers[pointer_index].id;
    }
    return static_cast<int32_t>(pointer_index);
}

extern "C" float bionic_AMotionEvent_getRawX(const void* event, size_t pointer_index) {
    return bionic_AMotionEvent_getX(event, pointer_index);
}

extern "C" float bionic_AMotionEvent_getRawY(const void* event, size_t pointer_index) {
    return bionic_AMotionEvent_getY(event, pointer_index);
}

extern "C" int64_t bionic_AMotionEvent_getDownTime(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->eventTime;
}

extern "C" int64_t bionic_AMotionEvent_getEventTime(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->eventTime;
}

extern "C" size_t bionic_AMotionEvent_getHistorySize(const void* event) {
    (void)event; return 0;
}

extern "C" int64_t bionic_AMotionEvent_getHistoricalEventTime(const void* event, size_t history_index) {
    (void)history_index;
    return bionic_AMotionEvent_getEventTime(event);
}

extern "C" float bionic_AMotionEvent_getHistoricalX(const void* event, size_t pointer_index, size_t history_index) {
    (void)history_index;
    return bionic_AMotionEvent_getX(event, pointer_index);
}

extern "C" float bionic_AMotionEvent_getHistoricalY(const void* event, size_t pointer_index, size_t history_index) {
    (void)history_index;
    return bionic_AMotionEvent_getY(event, pointer_index);
}

extern "C" float bionic_AMotionEvent_getHistoricalPressure(const void* event, size_t pointer_index, size_t history_index) {
    (void)event; (void)pointer_index; (void)history_index;
    return 1.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// AKeyEvent shims
// ─────────────────────────────────────────────────────────────────────────────
extern "C" int32_t bionic_AKeyEvent_getAction(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->action;
}

extern "C" int32_t bionic_AKeyEvent_getKeyCode(const void* event) {
    (void)event; return 0;
}

extern "C" int32_t bionic_AKeyEvent_getScanCode(const void* event) {
    (void)event; return 0;
}

extern "C" int32_t bionic_AKeyEvent_getMetaState(const void* event) {
    (void)event; return 0;
}

extern "C" int32_t bionic_AKeyEvent_getRepeatCount(const void* event) {
    (void)event; return 0;
}

extern "C" int64_t bionic_AKeyEvent_getDownTime(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->eventTime;
}

extern "C" int64_t bionic_AKeyEvent_getEventTime(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->eventTime;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bionic ASensorManager shims (Real sensor bridge from iOS CoreMotion)
// ─────────────────────────────────────────────────────────────────────────────

struct ASensorVector {
    float x;
    float y;
    float z;
    int8_t status;
    uint8_t reserved[3];
};

struct ASensorEvent {
    int32_t version;
    int32_t sensor;
    int32_t type;
    int32_t reserved0;
    int64_t timestamp;
    union {
        float data[16];
        ASensorVector vector;
        ASensorVector acceleration;
    };
    int32_t reserved1[4];
};

struct DummySensor {
    int type;
    const char* name;
    const char* vendor;
};

static DummySensor g_sensorAccel = {1, "iOS Accelerometer", "Apple"};
static DummySensor g_sensorGyro = {4, "iOS Gyroscope", "Apple"};
static DummySensor g_sensorOrient = {3, "iOS Orientation Sensor", "Apple"};
static DummySensor* g_sensorList[3] = {&g_sensorAccel, &g_sensorGyro, &g_sensorOrient};

static std::mutex g_sensorMutex;
static std::vector<ASensorEvent> g_sensorEventQueue;

extern "C" void kudroid_inject_sensor_event(int sensorType, float x, float y, float z) {
    ASensorEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.version = sizeof(ASensorEvent);
    ev.sensor = sensorType;
    ev.type = sensorType;
    ev.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    ev.acceleration.x = x;
    ev.acceleration.y = y;
    ev.acceleration.z = z;
    ev.acceleration.status = 3; // SENSOR_STATUS_ACCURACY_HIGH

    {
        std::lock_guard<std::mutex> lock(g_sensorMutex);
        if (g_sensorEventQueue.size() < 64) {
            g_sensorEventQueue.push_back(ev);
        }
    }
}

extern "C" void* bionic_ASensorManager_getInstance() {
    static int dummyManager = 1;
    return &dummyManager;
}

extern "C" void* bionic_ASensorManager_createEventQueue(void* manager, void* looper, int ident, void* callback, void* data) {
    (void)manager; (void)looper; (void)ident; (void)callback; (void)data;
    static int dummyQueue = 1;
    return &dummyQueue;
}

extern "C" int bionic_ASensorManager_destroyEventQueue(void* manager, void* queue) {
    (void)manager; (void)queue; return 0;
}

extern "C" int bionic_ASensorManager_getSensorList(void* manager, void** list) {
    (void)manager;
    if (list) {
        *list = g_sensorList;
    }
    return 3;
}

extern "C" void* bionic_ASensorManager_getDefaultSensor(void* manager, int type) {
    (void)manager;
    if (type == 1) return &g_sensorAccel;
    if (type == 4) return &g_sensorGyro;
    if (type == 3) return &g_sensorOrient;
    // Unknown sensor: NULL, not the accelerometer. Returning a sensor that is
    // not there makes the guest configure and wait on data that never comes.
    return nullptr;
}

extern "C" int bionic_ASensorEventQueue_enableSensor(void* queue, void* sensor) { (void)queue; (void)sensor; return 0; }
extern "C" int bionic_ASensorEventQueue_disableSensor(void* queue, void* sensor) { (void)queue; (void)sensor; return 0; }
extern "C" int bionic_ASensorEventQueue_setEventRate(void* queue, void* sensor, int32_t usec) { (void)queue; (void)sensor; (void)usec; return 0; }
extern "C" int bionic_ASensorEventQueue_hasEvents(void* queue) {
    (void)queue;
    std::lock_guard<std::mutex> lock(g_sensorMutex);
    return g_sensorEventQueue.empty() ? 0 : 1;
}

extern "C" ssize_t bionic_ASensorEventQueue_getEvents(void* queue, void* events, size_t count) {
    (void)queue;
    if (!events || count == 0) return 0;
    // The guest declares its ASensorEvent layout by sizeof: a size mismatch
    // here is a buffer overflow on their side, so serve nothing instead.
    // NDK sizeof(ASensorEvent) is 104.
    if (count > 1024) count = 1024;
    std::lock_guard<std::mutex> lock(g_sensorMutex);
    size_t num = std::min(count, g_sensorEventQueue.size());
    if (num > 0) {
        ASensorEvent* dst = static_cast<ASensorEvent*>(events);
        for (size_t i = 0; i < num; ++i) {
            dst[i] = g_sensorEventQueue[i];
        }
        g_sensorEventQueue.erase(g_sensorEventQueue.begin(), g_sensorEventQueue.begin() + num);
    }
    return num;
}

extern "C" const char* bionic_ASensor_getName(void* sensor) {
    if (!sensor) return "Unknown";
    return static_cast<DummySensor*>(sensor)->name;
}
extern "C" const char* bionic_ASensor_getVendor(void* sensor) {
    if (!sensor) return "Apple";
    return static_cast<DummySensor*>(sensor)->vendor;
}
extern "C" int bionic_ASensor_getType(void* sensor) {
    if (!sensor) return 1;
    return static_cast<DummySensor*>(sensor)->type;
}
extern "C" float bionic_ASensor_getResolution(void* sensor) { (void)sensor; return 0.001f; }
extern "C" int bionic_ASensor_getMinDelay(void* sensor) { (void)sensor; return 10000; }

const SymbolEntry kInputSymbols[] = {
    // Sensor Symbols
    {"ASensorManager_getInstance", reinterpret_cast<void*>(&bionic_ASensorManager_getInstance)},
    {"ASensorManager_createEventQueue", reinterpret_cast<void*>(&bionic_ASensorManager_createEventQueue)},
    {"ASensorManager_destroyEventQueue", reinterpret_cast<void*>(&bionic_ASensorManager_destroyEventQueue)},
    {"ASensorManager_getSensorList", reinterpret_cast<void*>(&bionic_ASensorManager_getSensorList)},
    {"ASensorManager_getDefaultSensor", reinterpret_cast<void*>(&bionic_ASensorManager_getDefaultSensor)},
    {"ASensorEventQueue_enableSensor", reinterpret_cast<void*>(&bionic_ASensorEventQueue_enableSensor)},
    {"ASensorEventQueue_disableSensor", reinterpret_cast<void*>(&bionic_ASensorEventQueue_disableSensor)},
    {"ASensorEventQueue_setEventRate", reinterpret_cast<void*>(&bionic_ASensorEventQueue_setEventRate)},
    {"ASensorEventQueue_hasEvents", reinterpret_cast<void*>(&bionic_ASensorEventQueue_hasEvents)},
    {"ASensorEventQueue_getEvents", reinterpret_cast<void*>(&bionic_ASensorEventQueue_getEvents)},
    {"ASensor_getName", reinterpret_cast<void*>(&bionic_ASensor_getName)},
    {"ASensor_getVendor", reinterpret_cast<void*>(&bionic_ASensor_getVendor)},
    {"ASensor_getType", reinterpret_cast<void*>(&bionic_ASensor_getType)},
    {"ASensor_getResolution", reinterpret_cast<void*>(&bionic_ASensor_getResolution)},
    {"ASensor_getMinDelay", reinterpret_cast<void*>(&bionic_ASensor_getMinDelay)},

    // InputQueue Symbols
    {"AInputQueue_create", reinterpret_cast<void*>(&bionic_AInputQueue_create)},
    {"AInputQueue_getEvent", reinterpret_cast<void*>(&bionic_AInputQueue_getEvent)},
    {"AInputQueue_preDispatchEvent", reinterpret_cast<void*>(&bionic_AInputQueue_preDispatchEvent)},
    {"AInputQueue_finishEvent", reinterpret_cast<void*>(&bionic_AInputQueue_finishEvent)},
    {"AInputQueue_attachLooper", reinterpret_cast<void*>(&bionic_AInputQueue_attachLooper)},
    {"AInputQueue_detachLooper", reinterpret_cast<void*>(&bionic_AInputQueue_detachLooper)},
    {"AInputQueue_hasEvents", reinterpret_cast<void*>(&bionic_AInputQueue_hasEvents)},

    // Input/Motion Symbols
    {"AInputEvent_getType", reinterpret_cast<void*>(&bionic_AInputEvent_getType)},
    {"AInputEvent_getSource", reinterpret_cast<void*>(&bionic_AInputEvent_getSource)},
    {"AInputEvent_getFlags", reinterpret_cast<void*>(&bionic_AInputEvent_getFlags)},
    {"AInputEvent_getEventTime", reinterpret_cast<void*>(&bionic_AInputEvent_getEventTime)},
    {"AMotionEvent_getAction", reinterpret_cast<void*>(&bionic_AMotionEvent_getAction)},
    {"AMotionEvent_getX", reinterpret_cast<void*>(&bionic_AMotionEvent_getX)},
    {"AMotionEvent_getY", reinterpret_cast<void*>(&bionic_AMotionEvent_getY)},
    {"AMotionEvent_getPointerCount", reinterpret_cast<void*>(&bionic_AMotionEvent_getPointerCount)},
    {"AMotionEvent_getPointerId", reinterpret_cast<void*>(&bionic_AMotionEvent_getPointerId)},
    {"AMotionEvent_getXByIndex", reinterpret_cast<void*>(&bionic_AMotionEvent_getXByIndex)},
    {"AMotionEvent_getYByIndex", reinterpret_cast<void*>(&bionic_AMotionEvent_getYByIndex)},
    {"AMotionEvent_getPressure", reinterpret_cast<void*>(&bionic_AMotionEvent_getPressure)},
    {"AMotionEvent_getPressureByIndex", reinterpret_cast<void*>(&bionic_AMotionEvent_getPressureByIndex)},
    {"AMotionEvent_getRawX", reinterpret_cast<void*>(&bionic_AMotionEvent_getRawX)},
    {"AMotionEvent_getRawY", reinterpret_cast<void*>(&bionic_AMotionEvent_getRawY)},
    {"AMotionEvent_getDownTime", reinterpret_cast<void*>(&bionic_AMotionEvent_getDownTime)},
    {"AMotionEvent_getEventTime", reinterpret_cast<void*>(&bionic_AMotionEvent_getEventTime)},
    {"AMotionEvent_getHistorySize", reinterpret_cast<void*>(&bionic_AMotionEvent_getHistorySize)},
    {"AMotionEvent_getHistoricalEventTime", reinterpret_cast<void*>(&bionic_AMotionEvent_getHistoricalEventTime)},
    {"AMotionEvent_getHistoricalX", reinterpret_cast<void*>(&bionic_AMotionEvent_getHistoricalX)},
    {"AMotionEvent_getHistoricalY", reinterpret_cast<void*>(&bionic_AMotionEvent_getHistoricalY)},
    {"AMotionEvent_getHistoricalPressure", reinterpret_cast<void*>(&bionic_AMotionEvent_getHistoricalPressure)},

    // KeyEvent Symbols
    {"AKeyEvent_getAction", reinterpret_cast<void*>(&bionic_AKeyEvent_getAction)},
    {"AKeyEvent_getKeyCode", reinterpret_cast<void*>(&bionic_AKeyEvent_getKeyCode)},
    {"AKeyEvent_getScanCode", reinterpret_cast<void*>(&bionic_AKeyEvent_getScanCode)},
    {"AKeyEvent_getMetaState", reinterpret_cast<void*>(&bionic_AKeyEvent_getMetaState)},
    {"AKeyEvent_getRepeatCount", reinterpret_cast<void*>(&bionic_AKeyEvent_getRepeatCount)},
    {"AKeyEvent_getDownTime", reinterpret_cast<void*>(&bionic_AKeyEvent_getDownTime)},
    {"AKeyEvent_getEventTime", reinterpret_cast<void*>(&bionic_AKeyEvent_getEventTime)},
    {"kudroid_inject_touch_event", reinterpret_cast<void*>(&kudroid_inject_touch_event)},
    {"kudroid_inject_touch_batch", reinterpret_cast<void*>(&kudroid_inject_touch_batch)},
};

} // namespace

const SymbolEntry* get_input_symbols(size_t* count) {
    if (count) {
        *count = sizeof(kInputSymbols) / sizeof(SymbolEntry);
    }
    return kInputSymbols;
}

} // namespace kudroid
