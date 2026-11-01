#include "kudroid/AlignedVmReservations.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <sys/mman.h>
#include <thread>
#include <vector>

namespace {
int checks = 0, failures = 0;
void check(bool value, const char* name) {
    ++checks;
    failures += !value;
    std::printf("%s %s\n", value ? "PASS" : "FAIL", name);
}
}

int main() {
    kudroid::AlignedVmReservations owner;
    const auto page = owner.pageSize();
    const auto length = page * 8, alignment = page * 16;
    check(page != 0, "native page granularity exists");
    check(!owner.reserve(0, alignment), "zero reservation is rejected");
    check(!owner.reserve(page + 1, alignment), "partial native page is rejected");
    check(!owner.reserve(length, page * 3), "non-power-of-two alignment is rejected");
    check(!owner.reserve(SIZE_MAX, alignment), "overflowing request is rejected");
    check(owner.prepare(length, alignment), "early exact aligned span is prepared");
    check(owner.prepare(length, alignment), "identical preparation is idempotent");
    check(!owner.prepare(page, alignment), "prepared ownership cannot be silently replaced");
    void* unrelated = owner.reserve(page, page);
    void* address = owner.reserve(length, alignment);
    check(address && unrelated && address != unrelated, "different request does not consume the prepared span");
    check(address && !(reinterpret_cast<uintptr_t>(address) & (alignment - 1)), "retained span has requested alignment");
    if (!address || !unrelated) return 1;
    auto* head = static_cast<volatile unsigned char*>(address);
    auto* tail = head + length - page;
    check(owner.protect(address, page, PROT_READ | PROT_WRITE), "first native page commits");
    check(owner.protect(const_cast<unsigned char*>(tail), page, PROT_READ | PROT_WRITE), "last native page commits");
    *head = 0x35;
    *tail = 0x57;
    check(*head == 0x35 && *tail == 0x57, "both ends of the full retained span are writable");
    check(!owner.release(static_cast<char*>(address) + page, page), "interior unmap is rejected");
    check(*head == 0x35 && *tail == 0x57, "failed interior unmap preserves live pages");
    check(!owner.protect(static_cast<char*>(address) + 1, page, PROT_READ), "partial-page protection is rejected");
    check(!owner.protect(address, page, 8), "unknown permissions are rejected");
    check(!owner.protect(reinterpret_cast<void*>(1), page, PROT_READ), "unowned range is rejected");
    check(owner.discard(address, page), "owned pages accept the platform reclaim hint");
    check(owner.protect(address, page, PROT_NONE), "access is revoked without releasing ownership");
    check(owner.protect(address, page, PROT_READ | PROT_WRITE), "revoked page recommits");
    *head = 0x62;
    check(*head == 0x62, "recommitted page remains writable");
    check(owner.release(const_cast<unsigned char*>(tail), page), "tail release shrinks the owned span");
    check(!owner.protect(const_cast<unsigned char*>(tail), page, PROT_READ), "released tail cannot be protected through stale ownership");
    check(owner.release(address, length - page), "remaining full reservation releases");
    check(!owner.release(address, length - page), "double release is rejected");
    check(owner.release(unrelated, page), "independent reservation releases");

    std::atomic<bool> parallelOk{true};
    std::vector<std::thread> workers;
    for (int thread = 0; thread < 4; ++thread) workers.emplace_back([&] {
        for (int iteration = 0; iteration < 32; ++iteration) {
            void* span = owner.reserve(page * 2, page * 4);
            if (!span || !owner.protect(span, page * 2, PROT_READ | PROT_WRITE)) { parallelOk = false; return; }
            auto* bytes = static_cast<volatile unsigned char*>(span);
            bytes[0] = 0x13;
            bytes[page * 2 - 1] = 0x31;
            if (bytes[0] != 0x13 || bytes[page * 2 - 1] != 0x31 || !owner.release(span, page * 2)) {
                parallelOk = false;
                return;
            }
        }
    });
    for (auto& worker : workers) worker.join();
    check(parallelOk, "concurrent reservations preserve separate span ownership");
    void* retained = nullptr;
    {
        kudroid::AlignedVmReservations scoped;
        retained = scoped.reserve(page, page);
        check(retained != nullptr, "scoped owner reserves memory");
    }
#if defined(__linux__)
    unsigned char residency = 0;
    errno = 0;
    check(::mincore(retained, page, &residency) == -1 && errno == ENOMEM, "owner destruction releases retained mappings");
#endif
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
