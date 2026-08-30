#include "kudroid/abi/GuestVarargs.h"

// No libc and no <string>: this file is compiled twice — once into kudroid_core, and once
// freestanding for the arm64 qemu test that is the only thing able to prove it correct.
// The one exception is double formatting, which is done by hand below for the same reason.

namespace kudroid {
namespace {

// Somewhere to accumulate output that respects the caller's buffer but keeps counting past
// it, so the snprintf return contract can be honoured.
class Sink {
public:
    Sink(char* out, size_t size) : out_(out), size_(size) {}

    void Put(char c) {
        if (out_ != nullptr && size_ > 0 && written_ + 1 < size_) out_[written_] = c;
        ++written_;
    }

    void PutRange(const char* text, size_t length) {
        for (size_t i = 0; i < length; ++i) Put(text[i]);
    }

    void PutCString(const char* text) {
        while (*text != '\0') Put(*text++);
    }

    // The terminator is written at the clamped position, never past the buffer.
    void Terminate() {
        if (out_ == nullptr || size_ == 0) return;
        out_[written_ < size_ - 1 ? written_ : size_ - 1] = '\0';
    }

    size_t written() const { return written_; }

private:
    char* out_;
    size_t size_;
    size_t written_ = 0;
};

void PutUnsigned(Sink& sink, uint64_t value, unsigned base, bool upper = false) {
    char buffer[32];
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t length = 0;
    do {
        buffer[length++] = digits[value % base];
        value /= base;
    } while (value != 0 && length < sizeof(buffer));
    while (length > 0) sink.Put(buffer[--length]);
}

void PutSigned(Sink& sink, int64_t value) {
    if (value < 0) {
        sink.Put('-');
        // Negating INT64_MIN overflows; cast first, which gives the right magnitude in
        // two's complement.
        PutUnsigned(sink, ~static_cast<uint64_t>(value) + 1u, 10);
        return;
    }
    PutUnsigned(sink, static_cast<uint64_t>(value), 10);
}

// Fixed-point formatting for %f, by hand.
//
// Only %f is supported; %e and %g fall back to it. A game's log line wants "%.2f" and
// gets it, which is the case worth handling — and doing it here rather than calling the
// host's snprintf keeps this file free of libc so the freestanding test can build it.
void PutDouble(Sink& sink, double value, int precision) {
    if (precision < 0) precision = 6;
    if (precision > 17) precision = 17;

    // NaN and infinity have no digits to print, and the arithmetic below would loop.
    if (value != value) { sink.PutCString("nan"); return; }
    if (value > 1.7976931348623157e308) { sink.PutCString("inf"); return; }
    if (value < -1.7976931348623157e308) { sink.PutCString("-inf"); return; }

    if (value < 0.0) {
        sink.Put('-');
        value = -value;
    }

    // Round at the requested precision before splitting, so 2.45 at one decimal gives
    // "2.5" rather than "2.4" — truncating here would make every printed figure low.
    double scale = 1.0;
    for (int i = 0; i < precision; ++i) scale *= 10.0;
    // 0.5 in the last printed place.
    value += 0.5 / scale;

    const uint64_t whole = static_cast<uint64_t>(value);
    PutUnsigned(sink, whole, 10);
    if (precision == 0) return;

    sink.Put('.');
    double fraction = value - static_cast<double>(whole);
    for (int i = 0; i < precision; ++i) {
        fraction *= 10.0;
        int digit = static_cast<int>(fraction);
        if (digit < 0) digit = 0;
        if (digit > 9) digit = 9;
        sink.Put(static_cast<char>('0' + digit));
        fraction -= digit;
    }
}

// Draws arguments in the order a format string asks for them.
//
// Integer and floating-point arguments come from separate register files and fall back to
// one shared overflow area — that is what AAPCS64 specifies. The mistake to avoid is a
// single counter for both, which misformats any line mixing "%d" and "%f"; a game logging
// "frame %d at %f" hits it every frame.
class Cursor {
public:
    Cursor(const GuestVarargs* registers, unsigned firstGpIndex, unsigned firstFpIndex)
        : registers_(registers), gpIndex_(firstGpIndex), fpIndex_(firstFpIndex) {}

    uint64_t NextInt() {
        if (registers_ == nullptr) return 0;
        if (gpIndex_ < 8) return registers_->gp[gpIndex_++];
        return NextOverflow();
    }

    double NextDouble() {
        uint64_t bits;
        if (registers_ == nullptr) {
            bits = 0;
        } else if (fpIndex_ < 8) {
            // A double is passed in d0-d7, which alias the low 64 bits of q0-q7.
            bits = registers_->fp[fpIndex_ * 2];
            ++fpIndex_;
        } else {
            bits = NextOverflow();
        }
        double value;
        __builtin_memcpy(&value, &bits, sizeof(value));
        return value;
    }

private:
    uint64_t NextOverflow() {
        // Bounded: a format string with more conversions than the call had arguments would
        // otherwise walk off the stack. Guest format strings are guest data and cannot be
        // assumed to match the call.
        const unsigned kMaxOverflowSlots = 32;
        if (registers_->overflow == nullptr || overflowIndex_ >= kMaxOverflowSlots) return 0;
        return registers_->overflow[overflowIndex_++];
    }

    const GuestVarargs* registers_;
    unsigned gpIndex_;
    unsigned fpIndex_;
    unsigned overflowIndex_ = 0;
};

} // namespace

size_t FormatGuestVarargs(char* out, size_t size, const char* format,
                          const GuestVarargs* registers, unsigned firstGpIndex,
                          unsigned firstFpIndex) {
    Sink sink(out, size);
    if (format == nullptr) {
        sink.Terminate();
        return 0;
    }
    Cursor args(registers, firstGpIndex, firstFpIndex);

    for (const char* cursor = format; *cursor != '\0'; ++cursor) {
        if (*cursor != '%') {
            sink.Put(*cursor);
            continue;
        }
        ++cursor;
        if (*cursor == '%') {
            sink.Put('%');
            continue;
        }
        // A trailing '%' is malformed, but must stop here rather than let the loop's
        // ++cursor step past the terminator and scan into whatever follows.
        if (*cursor == '\0') {
            sink.Put('%');
            break;
        }

        while (*cursor == '-' || *cursor == '+' || *cursor == ' ' ||
               *cursor == '#' || *cursor == '0' || *cursor == '\'') ++cursor;

        // Width. '*' takes an integer argument; skipping that consumption would shift
        // every later argument by one.
        if (*cursor == '*') {
            (void)args.NextInt();
            ++cursor;
        } else {
            while (*cursor >= '0' && *cursor <= '9') ++cursor;
        }

        int precision = -1;
        if (*cursor == '.') {
            ++cursor;
            if (*cursor == '*') {
                precision = static_cast<int>(static_cast<int64_t>(args.NextInt()));
                ++cursor;
            } else {
                precision = 0;
                while (*cursor >= '0' && *cursor <= '9') {
                    precision = precision * 10 + (*cursor - '0');
                    ++cursor;
                }
            }
        }

        // The length modifier decides how wide the argument is, so it has to be recorded
        // rather than skipped.
        //
        // AAPCS64 passes an `int` in a w-register — the low 32 bits of the x-register —
        // and leaves the upper half undefined. Reading all 64 bits turns -7 into
        // 4294967289: the sign is lost, and a plain "%d" prints a large positive number
        // for every negative value. Only `l`, `ll`, `z`, `j` and `t` denote a 64-bit
        // argument.
        bool isLong = false;
        for (;;) {
            if (*cursor == 'l') {
                isLong = true;   // 'll' is still 64-bit, so a second 'l' changes nothing
                ++cursor;
            } else if (*cursor == 'z' || *cursor == 'j' || *cursor == 't') {
                isLong = true;
                ++cursor;
            } else if (*cursor == 'h' || *cursor == 'L') {
                // 'h'/'hh' are promoted to int when passed variadically, so they are
                // 32-bit like the default; 'L' applies to long double, which is not
                // supported here and falls through to the double path.
                ++cursor;
            } else {
                break;
            }
        }

        // Narrow to 32 bits when the conversion is not long, preserving the sign for
        // signed conversions and clearing the garbage upper half for unsigned ones.
        const auto nextSigned = [&]() -> int64_t {
            const uint64_t raw = args.NextInt();
            if (isLong) return static_cast<int64_t>(raw);
            return static_cast<int32_t>(static_cast<uint32_t>(raw));
        };
        const auto nextUnsigned = [&]() -> uint64_t {
            const uint64_t raw = args.NextInt();
            if (isLong) return raw;
            return static_cast<uint32_t>(raw);
        };

        switch (*cursor) {
            case 'd':
            case 'i':
                PutSigned(sink, nextSigned());
                break;
            case 'u': PutUnsigned(sink, nextUnsigned(), 10); break;
            case 'o': PutUnsigned(sink, nextUnsigned(), 8); break;
            case 'x': PutUnsigned(sink, nextUnsigned(), 16); break;
            case 'X': PutUnsigned(sink, nextUnsigned(), 16, /*upper=*/true); break;
            case 'p':
                // A pointer is always 64-bit, whatever the modifier said.
                sink.PutCString("0x");
                PutUnsigned(sink, args.NextInt(), 16);
                break;
            case 'c':
                sink.Put(static_cast<char>(args.NextInt() & 0xFF));
                break;
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
                PutDouble(sink, args.NextDouble(), precision);
                break;
            case 's': {
                const char* text = reinterpret_cast<const char*>(args.NextInt());
                if (text == nullptr) {
                    // Guest code does pass null here, and a formatter that dereferences
                    // it takes the process down inside a log call — losing the very
                    // message that would have explained the state.
                    sink.PutCString("<null>");
                    break;
                }
                // Bounded so a bad pointer scans no further than a page rather than
                // walking into a guard page.
                size_t maximum = precision >= 0 ? static_cast<size_t>(precision) : 4096;
                size_t length = 0;
                while (length < maximum && text[length] != '\0') ++length;
                sink.PutRange(text, length);
                break;
            }
            case '\0':
                sink.Put('%');
                --cursor;
                break;
            default:
                sink.PutCString("<unsupported:%");
                sink.Put(*cursor);
                sink.Put('>');
                break;
        }
    }

    sink.Terminate();
    return sink.written();
}

// Rebuild a register capture from a guest va_list.
//
// The guest's va_start wrote the save areas; this copies them into the layout the
// formatter already reads, so there is one argument source rather than two.
//
// The offsets are negative byte counts from the top of each save area, so a guest that has
// consumed nothing has gr_offs = -64 (eight x-registers) and vr_offs = -128 (eight
// q-registers). Dividing by the register width converts that to the index the formatter
// wants.
bool GuestVarargsFromVaList(const void* guest_ap, GuestVarargs* out,
                            unsigned* firstGpIndex, unsigned* firstFpIndex) {
    if (guest_ap == nullptr || out == nullptr || firstGpIndex == nullptr ||
        firstFpIndex == nullptr) {
        return false;
    }

    const auto* ap = static_cast<const GuestVaList*>(guest_ap);

    // The sizes AAPCS64 fixes: eight 8-byte x-registers, eight 16-byte q-registers.
    const int32_t kGpSaveSize = 8 * 8;
    const int32_t kFpSaveSize = 8 * 16;

    // Reject anything va_start could not have produced. A guest passing a stale or
    // fabricated va_list would otherwise send the loop below reading from whatever
    // gr_top happened to hold — and a bad va_list is exactly what a shim receives when
    // the ABI is misunderstood at the other end, so this is the case to fail loudly on
    // rather than to guess through.
    if (ap->gr_offs > 0 || ap->gr_offs < -kGpSaveSize) return false;
    if (ap->vr_offs > 0 || ap->vr_offs < -kFpSaveSize) return false;
    // A register save area is only required when the list still has registers to read
    // from; once exhausted the guest may legitimately leave the pointer unset.
    if (ap->gr_offs != 0 && ap->gr_top == nullptr) return false;
    if (ap->vr_offs != 0 && ap->vr_top == nullptr) return false;

    // gr_top and vr_top point one PAST their save areas, so the base is top - size.
    const auto* gpBase = static_cast<const unsigned char*>(ap->gr_top);
    const auto* fpBase = static_cast<const unsigned char*>(ap->vr_top);

    for (int i = 0; i < 8; ++i) out->gp[i] = 0;
    for (int i = 0; i < 16; ++i) out->fp[i] = 0;

    if (gpBase != nullptr) {
        gpBase -= kGpSaveSize;
        __builtin_memcpy(out->gp, gpBase, kGpSaveSize);
    }
    if (fpBase != nullptr) {
        fpBase -= kFpSaveSize;
        __builtin_memcpy(out->fp, fpBase, kFpSaveSize);
    }

    out->overflow = static_cast<const uint64_t*>(ap->stack);
    out->reserved = 0;

    // -64 means nothing consumed -> index 0; -8 means seven consumed -> index 7.
    *firstGpIndex = static_cast<unsigned>((kGpSaveSize + ap->gr_offs) / 8);
    *firstFpIndex = static_cast<unsigned>((kFpSaveSize + ap->vr_offs) / 16);
    return true;
}

} // namespace kudroid

extern "C" size_t kudroid_format_guest_va_list(char* out, size_t size, const char* format,
                                               const void* guest_ap) {
    kudroid::GuestVarargs registers;
    unsigned firstGp = 0;
    unsigned firstFp = 0;
    if (!kudroid::GuestVarargsFromVaList(guest_ap, &registers, &firstGp, &firstFp)) {
        // A va_list that cannot be trusted must not be formatted from. Saying so is more
        // use than either a crash or plausible-looking rubbish, which is the failure mode
        // this whole file exists to remove.
        return kudroid::FormatGuestVarargs(out, size, "<bad va_list>", nullptr, 0, 0);
    }
    return kudroid::FormatGuestVarargs(out, size, format, &registers, firstGp, firstFp);
}

#if defined(__aarch64__)
// Handlers for the trampolines in bionic_log_trampoline.S.
//
// Kept here rather than in SyscallShim.cpp so they can be linked into the freestanding
// arm64 test alongside the trampoline and the formatter — the only place the ABI can
// actually be verified. Neither needs anything beyond FormatGuestVarargs.
//
// `frame` points at the register capture the trampoline wrote; see GuestVarargs.h.

// snprintf(buf, size, format, ...) as the guest called it.
//
// The return value is the length the formatted string WOULD have had, matching the real
// snprintf: callers size a second buffer from it, and returning the truncated length makes
// them allocate too little or loop.
extern "C" int kudroid_snprintf_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    char* buffer = reinterpret_cast<char*>(registers->gp[0]);
    const auto size = static_cast<size_t>(registers->gp[1]);
    const char* format = reinterpret_cast<const char*>(registers->gp[2]);
    // Varargs start at the fourth integer register: buf, size and format took three.
    return static_cast<int>(
        kudroid::FormatGuestVarargs(buffer, size, format, registers, /*firstGpIndex=*/3));
}

// sprintf(buf, format, ...). The guest promises the buffer is large enough, exactly as it
// does on Android; nothing here can check that. The cap only limits how far a runaway
// format string can go.
extern "C" int kudroid_sprintf_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    char* buffer = reinterpret_cast<char*>(registers->gp[0]);
    const char* format = reinterpret_cast<const char*>(registers->gp[1]);
    return static_cast<int>(
        kudroid::FormatGuestVarargs(buffer, 0x10000, format, registers, /*firstGpIndex=*/2));
}

// __snprintf_chk(buf, maxlen, flag, slen, format, ...) — the _FORTIFY_SOURCE form, which
// is what a release-built guest actually calls for snprintf. Same ABI problem, one more
// fixed argument: varargs start at the sixth integer register.
extern "C" int kudroid_snprintf_chk_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    char* buffer = reinterpret_cast<char*>(registers->gp[0]);
    const auto maxlen = static_cast<size_t>(registers->gp[1]);
    // gp[2] is the fortify flag and gp[3] the compiler's idea of the buffer size; the
    // former is advisory and the latter is not more trustworthy than maxlen, so both are
    // ignored, as bionic's own implementation does once the check passes.
    const char* format = reinterpret_cast<const char*>(registers->gp[4]);
    return static_cast<int>(
        kudroid::FormatGuestVarargs(buffer, maxlen, format, registers, /*firstGpIndex=*/5));
}

// __sprintf_chk(buf, flag, slen, format, ...). `slen` is the destination size the compiler
// inferred, so unlike plain sprintf there IS a bound to respect here.
extern "C" int kudroid_sprintf_chk_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    char* buffer = reinterpret_cast<char*>(registers->gp[0]);
    const auto slen = static_cast<size_t>(registers->gp[2]);
    const char* format = reinterpret_cast<const char*>(registers->gp[3]);
    return static_cast<int>(
        kudroid::FormatGuestVarargs(buffer, slen != 0 ? slen : 0x10000, format, registers,
                                    /*firstGpIndex=*/4));
}
#endif
