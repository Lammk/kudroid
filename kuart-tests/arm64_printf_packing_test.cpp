#include "kudroid/abi/GuestVarargs.h"

namespace {
uint64_t bits(double value) {
    uint64_t result;
    __builtin_memcpy(&result, &value, sizeof(result));
    return result;
}
}

extern "C" int kudroid_test_printf_packing() {
    kudroid::GuestVarargs args{};
    uint64_t slots[128]{};
    size_t count = 0;
    int failures = 0;
    auto check = [&](bool result) { if (!result) ++failures; };
    args.gp[3] = 42;
    args.gp[4] = 3;
    args.gp[5] = 7;
    args.fp[0] = bits(0.125);
    check(kudroid::PackGuestPrintfArguments("%d %.*g %u", &args, 3, 0, slots, 128, &count));
    check(count == 4 && slots[0] == 42 && slots[1] == 3 && slots[2] == bits(0.125) && slots[3] == 7);
    args.gp[3] = 9;
    check(kudroid::PackGuestPrintfArguments("%2$.*1$g", &args, 3, 0, slots, 128, &count));
    check(count == 2 && slots[0] == 9 && slots[1] == bits(0.125));
    args.gp[4] = 0x12345678;
    check(kudroid::PackGuestPrintfArguments(L"%d %.2f %ls", &args, 3, 0, slots, 128, &count));
    check(count == 3 && slots[0] == 9 && slots[1] == bits(0.125) && slots[2] == 0x12345678);
    check(!kudroid::PackGuestPrintfArguments("%2$d", &args, 3, 0, slots, 128, &count));
    check(!kudroid::PackGuestPrintfArguments("%1$d %1$f", &args, 3, 0, slots, 128, &count));
    check(!kudroid::PackGuestPrintfArguments("%129$d", &args, 3, 0, slots, 128, &count));
    check(!kudroid::PackGuestPrintfArguments("%d", &args, 3, 0, slots, 0, &count));
    args.fp[0] = 0;
    args.fp[1] = 0x4005900000000000ULL;
    check(kudroid::PackGuestPrintfArguments("%Lf", &args, 3, 0, slots, 128, &count));
    check(count == 1 && slots[0] == bits(100.0));
    args.fp[0] = uint64_t{1} << 59;
    args.fp[1] = 0x3fff000000000000ULL;
    check(kudroid::PackGuestPrintfArguments("%Lg", &args, 3, 0, slots, 128, &count));
    check(slots[0] == bits(1.0));
    ++args.fp[0];
    check(kudroid::PackGuestPrintfArguments("%Lg", &args, 3, 0, slots, 128, &count));
    check(slots[0] == bits(1.0) + 1);
    args.fp[0] = 0;
    args.fp[1] = 0x8000000000000000ULL;
    check(kudroid::PackGuestPrintfArguments("%Lf", &args, 3, 0, slots, 128, &count));
    check(slots[0] == 0x8000000000000000ULL);
    args.fp[1] = uint64_t{16383 - 1074} << 48;
    check(kudroid::PackGuestPrintfArguments("%Lf", &args, 3, 0, slots, 128, &count));
    check(slots[0] == 1);
    args.fp[1] = 0x7fff000000000000ULL;
    check(kudroid::PackGuestPrintfArguments("%Lf", &args, 3, 0, slots, 128, &count));
    check(slots[0] == 0x7ff0000000000000ULL);
    alignas(16) const uint64_t overflow[] = {7, 0xdeadbeef, 0, 0x4005900000000000ULL, 9};
    args.overflow = overflow;
    check(kudroid::PackGuestPrintfArguments("%d %Lf %d", &args, 8, 8, slots, 128, &count));
    check(count == 3 && slots[0] == 7 && slots[1] == bits(100.0) && slots[2] == 9);
    return failures;
}
