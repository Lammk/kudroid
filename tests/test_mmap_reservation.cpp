#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>

extern "C" void* bionic_mmap(void*, size_t, int, int, int, off_t);
extern "C" int bionic_mprotect(void*, size_t, int);
extern "C" int bionic_munmap(void*, size_t);

namespace {
int checks = 0, failures = 0;
void Check(bool ok, const char* message) {
    ++checks;
    std::printf("%s %s\n", ok ? "OK" : "FAIL", message);
    if (!ok) ++failures;
}
}

int main() {
    const size_t page = sysconf(_SC_PAGESIZE);
    auto* owner = static_cast<unsigned char*>(mmap(nullptr, page, PROT_READ | PROT_WRITE,
                                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    Check(owner != MAP_FAILED, "owner mapping allocated");
    if (owner == MAP_FAILED) return 1;
    owner[0] = 0x6a;
    constexpr size_t length = 8ull << 30;
    auto* reserved = static_cast<unsigned char*>(bionic_mmap(owner, length, PROT_NONE, 0x4022, -1, 0));
    Check(reserved != MAP_FAILED, "full large PROT_NONE reservation succeeds");
    if (reserved != MAP_FAILED) {
        Check(reserved != owner && owner[0] == 0x6a, "advisory hint preserves existing mapping");
        unsigned char resident = 0;
        Check(mincore(reserved, page, &resident) == 0, "first page is mapped");
        Check(mincore(reserved + length / 2, page, &resident) == 0, "middle page is mapped");
        Check(mincore(reserved + length - page, page, &resident) == 0, "last page is mapped");
        int rc = bionic_mprotect(reserved + length - page, page, PROT_READ | PROT_WRITE);
        Check(rc == 0, "last page can commit read/write data");
        if (rc == 0) {
            reserved[length - 1] = 0x57;
            Check(reserved[length - 1] == 0x57, "last byte of full reservation is writable");
            Check(bionic_mprotect(reserved + length - page, page, PROT_NONE) == 0,
                  "committed page returns to PROT_NONE");
        }
        Check(bionic_munmap(reserved, length) == 0, "full reservation releases");
        errno = 0;
        Check(mincore(reserved + length - page, page, &resident) != 0 && errno == ENOMEM,
              "released tail is unmapped");
    }
    errno = 0;
    void* exclusive = bionic_mmap(owner, page, PROT_NONE, 0x104022, -1, 0);
    Check(exclusive == MAP_FAILED && errno == EEXIST, "NOREPLACE cannot overwrite owner mapping");
    Check(owner[0] == 0x6a, "owner contents remain intact");
    if (exclusive != MAP_FAILED && exclusive != owner) bionic_munmap(exclusive, page);
    munmap(owner, page);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
