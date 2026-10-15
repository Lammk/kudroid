#include "kudroid/debug/FrameWalk.h"

#include <pthread.h>

namespace kudroid {

StackBounds query_thread_stack_bounds() {
    StackBounds b;
#if defined(__APPLE__)
    // Darwin answers with the HIGHEST address and the size; the stack grows down from
    // there. Getting this backwards describes a region entirely above the real stack,
    // which is its own bug — see bionic_pthread_getattr_np, where exactly that happened.
    void* const top = ::pthread_get_stackaddr_np(::pthread_self());
    const std::size_t size = ::pthread_get_stacksize_np(::pthread_self());
    if (top != nullptr && size != 0) {
        b.high = reinterpret_cast<uintptr_t>(top);
        b.low = b.high - size;
        b.valid = true;
    }
#else
    pthread_attr_t attr;
    if (::pthread_getattr_np(::pthread_self(), &attr) == 0) {
        void* base = nullptr;
        std::size_t size = 0;
        if (::pthread_attr_getstack(&attr, &base, &size) == 0 && base != nullptr &&
            size != 0) {
            // pthread_attr_getstack reports the LOW address, unlike Darwin's query.
            b.low = reinterpret_cast<uintptr_t>(base);
            b.high = b.low + size;
            b.valid = true;
        }
        ::pthread_attr_destroy(&attr);
    }
#endif
    return b;
}

const StackBounds& cached_thread_stack_bounds() {
    static thread_local StackBounds bounds = query_thread_stack_bounds();
    return bounds;
}

FrameWalker::FrameWalker(uintptr_t start_fp, const StackBounds& bounds)
    : bounds_(bounds), fp_(start_fp) {
    // No bounds means no walk. Falling back to a plausibility test is what let
    // 0x100000000018 through and turned the guard diagnostic into a SIGSEGV on the
    // guest's main thread.
    valid_ = bounds_.valid && frame_is_readable(start_fp);
}

bool FrameWalker::frame_is_readable(uintptr_t candidate) const {
    if (!bounds_.valid) return false;
    // AAPCS64 requires 16-byte stack alignment, but a frame RECORD only needs to be
    // 8-aligned to be read safely, and requiring 16 would reject valid intermediate
    // frames on x86-64. Alignment is checked because an unaligned load is the other way
    // this faults — see the SIGBUS/BUS_ADRALN round.
    if ((candidate & 0x7) != 0) return false;
    if (candidate < bounds_.low) return false;
    // Both words of the frame record must fit: [fp] and [fp+8].
    if (candidate + 2 * sizeof(void*) > bounds_.high) return false;
    return true;
}

bool FrameWalker::return_address(uintptr_t* out) const {
    if (!valid_ || out == nullptr) return false;
    const uintptr_t ret = *reinterpret_cast<const uintptr_t*>(fp_ + sizeof(void*));
    if (ret == 0) return false;  // outermost frame
    *out = ret;
    return true;
}

bool FrameWalker::saved_fp(uintptr_t* out) const {
    if (!valid_ || out == nullptr) return false;
    *out = *reinterpret_cast<const uintptr_t*>(fp_);
    return true;
}

bool FrameWalker::slot(std::size_t byte_offset, uintptr_t* out) const {
    if (!valid_ || out == nullptr) return false;
    const uintptr_t addr = fp_ + byte_offset;
    // The whole word, not just its first byte: a slot that straddles the top of the
    // stack must be refused, and checking only the start address would allow it.
    if (addr < bounds_.low || addr + sizeof(uintptr_t) > bounds_.high) return false;
    if ((addr & 0x7) != 0) return false;
    *out = *reinterpret_cast<const uintptr_t*>(addr);
    return true;
}

bool FrameWalker::next() {
    if (!valid_) return false;
    const uintptr_t candidate = *reinterpret_cast<const uintptr_t*>(fp_);
    // Strictly increasing. This is the check that matters: a garbage slot read where a
    // saved fp would be — which is what happens when the compiler omits the frame
    // pointer at -O3 — is almost never a higher address inside the same stack, so the
    // walk stops instead of following it. It also makes a cycle impossible rather than
    // merely bounded by an iteration count.
    if (candidate <= fp_) {
        valid_ = false;
        return false;
    }
    if (!frame_is_readable(candidate)) {
        valid_ = false;
        return false;
    }
    fp_ = candidate;
    ++depth_;
    return true;
}

}  // namespace kudroid
