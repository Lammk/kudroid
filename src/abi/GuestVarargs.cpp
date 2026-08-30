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

// ─────────────────────────────────────────────────────────────────────────────
// Scanning, which is the same ABI problem with the consequences reversed.
//
// A guest's sscanf passes OUTPUT POINTERS as its varargs: the first six in x2-x7 and the
// rest in the overflow area. Apple's callee reads every one of them from the stack, so a
// forwarded call takes whatever is there and WRITES through it. printf formatted from the
// wrong place and produced rubbish; scanf stores to the wrong place and corrupts memory.
//
// On device Minecraft parses a UUID with
//   "%8x-%4hx-%4hx-%2hhx%2hhx-%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx"
// — eleven output pointers. The host wrote to five words of stack that were never
// pointers, one of which held a jobject: the next JNI call reported clazz=0xf8ec9809, a
// value no heap pointer can be. Then it stored to 0 and took SIGSEGV.
//
// The width and length modifiers are not decoration here. "%2hhx" must store exactly ONE
// byte; storing four silently overwrites the two fields after it, which is worse than the
// crash because nothing reports it.

// Where characters come from. A function pointer rather than two copies of the scanner,
// because fscanf reads a FILE* and this file cannot include <cstdio> — it is compiled
// freestanding for the arm64 test.
struct ScanSource {
    // Next character, or -1 at end of input. Must not consume.
    int (*peek)(void* state);
    // Consume the character last peeked.
    void (*advance)(void* state);
    void* state;
    // Characters consumed so far, for %n and for the "matched nothing" check.
    size_t consumed;
};

int SourcePeek(ScanSource& src) { return src.peek(src.state); }
void SourceAdvance(ScanSource& src) {
    src.advance(src.state);
    ++src.consumed;
}

bool IsSpace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}
bool IsDigit(int c) { return c >= '0' && c <= '9'; }

int HexValue(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// How wide the destination is. This IS the correctness question for scanning: the value
// parsed is 64-bit internally and must be narrowed to exactly what the caller declared.
enum class ScanWidth { kChar, kShort, kInt, kLong, kLongLong, kSizeT, kIntMax, kPtrDiff };

void StoreInteger(void* dest, uint64_t value, ScanWidth width, bool is_signed) {
    if (dest == nullptr) return;
    switch (width) {
        case ScanWidth::kChar: {
            // "%hhx" — one byte. Writing more corrupts whatever follows, which for the
            // UUID parse is the next two bytes of the same struct.
            const uint8_t narrow = static_cast<uint8_t>(value);
            __builtin_memcpy(dest, &narrow, sizeof(narrow));
            break;
        }
        case ScanWidth::kShort: {
            const uint16_t narrow = static_cast<uint16_t>(value);
            __builtin_memcpy(dest, &narrow, sizeof(narrow));
            break;
        }
        case ScanWidth::kInt: {
            if (is_signed) {
                const int32_t narrow = static_cast<int32_t>(value);
                __builtin_memcpy(dest, &narrow, sizeof(narrow));
            } else {
                const uint32_t narrow = static_cast<uint32_t>(value);
                __builtin_memcpy(dest, &narrow, sizeof(narrow));
            }
            break;
        }
        default:
            // long, long long, size_t, intmax_t and ptrdiff_t are all 64-bit on arm64.
            __builtin_memcpy(dest, &value, sizeof(value));
            break;
    }
}

// Parse an integer in `base`, at most `max_width` characters. base 0 detects 0x/0 prefixes,
// as strtol does. Returns false when no digits were available.
bool ScanInteger(ScanSource& src, unsigned base, int max_width, bool is_signed,
                 uint64_t* out) {
    int budget = max_width > 0 ? max_width : -1;
    const auto take = [&]() { SourceAdvance(src); if (budget > 0) --budget; };

    bool negative = false;
    int c = SourcePeek(src);
    if (budget != 0 && (c == '+' || c == '-')) {
        negative = (c == '-');
        take();
        c = SourcePeek(src);
    }

    // A "0x" prefix is only a prefix when a hex digit follows; "0xz" is the value 0 with
    // "xz" left unread. Consuming the x regardless would swallow input the next conversion
    // needs.
    if (budget != 0 && (base == 0 || base == 16) && c == '0') {
        take();
        c = SourcePeek(src);
        if (budget != 0 && (c == 'x' || c == 'X')) {
            // Peeking two ahead is not possible through this interface, so commit to the
            // prefix and treat a following non-digit as a value of 0 — which is what the
            // digits-seen flag below produces.
            take();
            base = 16;
            c = SourcePeek(src);
        } else {
            if (base == 0) base = 8;
            // The leading zero already counts as a digit, so "0" alone parses as 0.
            uint64_t value = 0;
            bool any = true;
            while (budget != 0 && HexValue(c) >= 0 &&
                   static_cast<unsigned>(HexValue(c)) < base) {
                value = value * base + static_cast<unsigned>(HexValue(c));
                take();
                c = SourcePeek(src);
            }
            (void)any;
            *out = negative ? ~value + 1u : value;
            return true;
        }
    }
    if (base == 0) base = 10;

    uint64_t value = 0;
    bool any_digits = false;
    for (;;) {
        if (budget == 0) break;
        c = SourcePeek(src);
        const int digit = HexValue(c);
        if (digit < 0 || static_cast<unsigned>(digit) >= base) break;
        value = value * base + static_cast<unsigned>(digit);
        any_digits = true;
        take();
    }
    if (!any_digits) return false;
    (void)is_signed;
    *out = negative ? ~value + 1u : value;
    return true;
}

// Parse a decimal floating-point number. Exponents are supported; hex floats are not, and
// no guest format string seen so far asks for one.
bool ScanFloat(ScanSource& src, int max_width, double* out) {
    int budget = max_width > 0 ? max_width : -1;
    const auto take = [&]() { SourceAdvance(src); if (budget > 0) --budget; };

    bool negative = false;
    int c = SourcePeek(src);
    if (budget != 0 && (c == '+' || c == '-')) {
        negative = (c == '-');
        take();
    }

    double value = 0.0;
    bool any_digits = false;
    while (budget != 0 && IsDigit(SourcePeek(src))) {
        value = value * 10.0 + static_cast<double>(SourcePeek(src) - '0');
        any_digits = true;
        take();
    }
    if (budget != 0 && SourcePeek(src) == '.') {
        take();
        double scale = 0.1;
        while (budget != 0 && IsDigit(SourcePeek(src))) {
            value += static_cast<double>(SourcePeek(src) - '0') * scale;
            scale *= 0.1;
            any_digits = true;
            take();
        }
    }
    if (!any_digits) return false;

    if (budget != 0 && (SourcePeek(src) == 'e' || SourcePeek(src) == 'E')) {
        take();
        bool exp_negative = false;
        if (budget != 0 && (SourcePeek(src) == '+' || SourcePeek(src) == '-')) {
            exp_negative = (SourcePeek(src) == '-');
            take();
        }
        int exponent = 0;
        bool exp_digits = false;
        while (budget != 0 && IsDigit(SourcePeek(src))) {
            if (exponent < 10000) exponent = exponent * 10 + (SourcePeek(src) - '0');
            exp_digits = true;
            take();
        }
        if (exp_digits) {
            for (int i = 0; i < exponent; ++i) {
                if (exp_negative) value *= 0.1; else value *= 10.0;
            }
        }
    }

    *out = negative ? -value : value;
    return true;
}

// The %[...] set. Returns the position just past the closing bracket, or nullptr when the
// set is unterminated — a malformed guest format must stop the scan, not run off the end.
const char* ParseScanSet(const char* p, bool* table, bool* negated) {
    for (int i = 0; i < 256; ++i) table[i] = false;
    *negated = false;
    if (*p == '^') { *negated = true; ++p; }
    // A ']' first is a literal member, per the standard.
    bool first = true;
    while (*p != '\0' && (*p != ']' || first)) {
        if (p[0] == '-' && p[1] != ']' && p[1] != '\0' && !first) {
            // A range: the previous character was already added, so fill up to p[1].
            const unsigned char lo = static_cast<unsigned char>(p[-1]);
            const unsigned char hi = static_cast<unsigned char>(p[1]);
            if (lo <= hi) {
                for (unsigned c = lo; c <= hi; ++c) table[c] = true;
            }
            p += 2;
            first = false;
            continue;
        }
        table[static_cast<unsigned char>(*p)] = true;
        ++p;
        first = false;
    }
    if (*p != ']') return nullptr;
    return p + 1;
}

} // namespace

int ScanGuestVarargsFrom(int (*peek)(void*), void (*advance)(void*), void* state,
                         const char* format, const GuestVarargs* registers,
                         unsigned firstGpIndex, unsigned firstFpIndex) {
    if (peek == nullptr || advance == nullptr || format == nullptr) return -1;

    ScanSource src{peek, advance, state, 0};
    Cursor args(registers, firstGpIndex, firstFpIndex);
    int assigned = 0;
    bool input_ended_early = false;

    for (const char* p = format; *p != '\0'; ++p) {
        if (IsSpace(*p)) {
            // Whitespace in the format matches any amount of whitespace, including none.
            while (IsSpace(SourcePeek(src))) SourceAdvance(src);
            continue;
        }
        if (*p != '%') {
            // A literal must match exactly. This is what makes the '-' separators in the
            // UUID format do their job: a mismatch stops the scan.
            const int c = SourcePeek(src);
            if (c < 0) { input_ended_early = true; break; }
            if (c != static_cast<unsigned char>(*p)) break;
            SourceAdvance(src);
            continue;
        }

        ++p;
        if (*p == '%') {
            while (IsSpace(SourcePeek(src))) SourceAdvance(src);
            if (SourcePeek(src) != '%') break;
            SourceAdvance(src);
            continue;
        }
        if (*p == '\0') break;

        // '*' suppresses assignment: the value is parsed and thrown away, and NO pointer
        // is consumed. Treating it as a normal conversion would shift every later output
        // pointer by one — and write through the wrong one.
        bool suppress = false;
        if (*p == '*') { suppress = true; ++p; }

        int max_width = 0;
        while (IsDigit(*p)) {
            max_width = max_width * 10 + (*p - '0');
            ++p;
        }

        ScanWidth width = ScanWidth::kInt;
        for (;;) {
            if (p[0] == 'h' && p[1] == 'h') { width = ScanWidth::kChar; p += 2; }
            else if (p[0] == 'h') { width = ScanWidth::kShort; ++p; }
            else if (p[0] == 'l' && p[1] == 'l') { width = ScanWidth::kLongLong; p += 2; }
            else if (p[0] == 'l') { width = ScanWidth::kLong; ++p; }
            else if (p[0] == 'j') { width = ScanWidth::kIntMax; ++p; }
            else if (p[0] == 'z') { width = ScanWidth::kSizeT; ++p; }
            else if (p[0] == 't') { width = ScanWidth::kPtrDiff; ++p; }
            else if (p[0] == 'L' || p[0] == 'q') { width = ScanWidth::kLongLong; ++p; }
            else break;
        }

        const char conversion = *p;

        // Every conversion but %c, %s, %[ and %n skips leading whitespace first.
        if (conversion != 'c' && conversion != '[' && conversion != 'n') {
            while (IsSpace(SourcePeek(src))) SourceAdvance(src);
        }

        switch (conversion) {
            case 'd':
            case 'i':
            case 'u':
            case 'o':
            case 'x':
            case 'X':
            case 'p': {
                unsigned base = 10;
                if (conversion == 'o') base = 8;
                else if (conversion == 'x' || conversion == 'X' || conversion == 'p') base = 16;
                else if (conversion == 'i') base = 0;
                const bool is_signed = (conversion == 'd' || conversion == 'i');

                if (SourcePeek(src) < 0) { input_ended_early = true; break; }
                uint64_t value = 0;
                if (!ScanInteger(src, base, max_width, is_signed, &value)) {
                    return assigned;  // matching failure
                }
                if (!suppress) {
                    void* dest = reinterpret_cast<void*>(args.NextInt());
                    // %p is always a full pointer, whatever a length modifier claimed.
                    StoreInteger(dest, value,
                                 conversion == 'p' ? ScanWidth::kLong : width, is_signed);
                    ++assigned;
                }
                break;
            }
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
            case 'a': {
                if (SourcePeek(src) < 0) { input_ended_early = true; break; }
                double value = 0.0;
                if (!ScanFloat(src, max_width, &value)) return assigned;
                if (!suppress) {
                    void* dest = reinterpret_cast<void*>(args.NextInt());
                    if (dest != nullptr) {
                        // No length modifier means float, not double — storing eight bytes
                        // into a float would overwrite the next member.
                        if (width == ScanWidth::kInt) {
                            const float narrow = static_cast<float>(value);
                            __builtin_memcpy(dest, &narrow, sizeof(narrow));
                        } else {
                            __builtin_memcpy(dest, &value, sizeof(value));
                        }
                    }
                    ++assigned;
                }
                break;
            }
            case 'c': {
                const int count = max_width > 0 ? max_width : 1;
                char* dest = suppress ? nullptr
                                      : reinterpret_cast<char*>(args.NextInt());
                int written = 0;
                for (int i = 0; i < count; ++i) {
                    const int c = SourcePeek(src);
                    if (c < 0) { input_ended_early = true; break; }
                    if (dest != nullptr) dest[written] = static_cast<char>(c);
                    ++written;
                    SourceAdvance(src);
                }
                if (written != count) return assigned;
                // %c does NOT terminate: it is a character array, not a string.
                if (!suppress) ++assigned;
                break;
            }
            case 's': {
                char* dest = suppress ? nullptr
                                      : reinterpret_cast<char*>(args.NextInt());
                size_t written = 0;
                for (;;) {
                    const int c = SourcePeek(src);
                    if (c < 0 || IsSpace(c)) break;
                    if (max_width > 0 && written >= static_cast<size_t>(max_width)) break;
                    if (dest != nullptr) dest[written] = static_cast<char>(c);
                    ++written;
                    SourceAdvance(src);
                }
                if (written == 0) {
                    if (dest != nullptr) dest[0] = '\0';
                    return assigned;
                }
                if (dest != nullptr) dest[written] = '\0';
                if (!suppress) ++assigned;
                break;
            }
            case '[': {
                bool table[256];
                bool negated = false;
                const char* after = ParseScanSet(p + 1, table, &negated);
                if (after == nullptr) return assigned;  // unterminated set
                p = after - 1;                          // loop's ++p lands past ']'

                char* dest = suppress ? nullptr
                                      : reinterpret_cast<char*>(args.NextInt());
                size_t written = 0;
                for (;;) {
                    const int c = SourcePeek(src);
                    if (c < 0) break;
                    const bool in_set = table[static_cast<unsigned char>(c)];
                    if (in_set == negated) break;
                    if (max_width > 0 && written >= static_cast<size_t>(max_width)) break;
                    if (dest != nullptr) dest[written] = static_cast<char>(c);
                    ++written;
                    SourceAdvance(src);
                }
                if (written == 0) {
                    if (dest != nullptr) dest[0] = '\0';
                    return assigned;
                }
                if (dest != nullptr) dest[written] = '\0';
                if (!suppress) ++assigned;
                break;
            }
            case 'n': {
                // %n reports progress and does NOT count as an assignment.
                if (!suppress) {
                    void* dest = reinterpret_cast<void*>(args.NextInt());
                    StoreInteger(dest, static_cast<uint64_t>(src.consumed), width, true);
                }
                break;
            }
            default:
                // An unknown conversion cannot be skipped safely: the number of output
                // pointers it would have consumed is unknown, so continuing would write
                // through the wrong one. Stop instead.
                return assigned;
        }

        if (input_ended_early) break;
    }

    // EOF before any assignment is -1, not 0. Callers distinguish "no input" from "input
    // that did not match", and a game deciding whether to retry reads that difference.
    if (assigned == 0 && input_ended_early) return -1;
    return assigned;
}

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

// ── scanning from a string ───────────────────────────────────────────────────
//
// The character source for sscanf/vsscanf. fscanf's source needs <cstdio> and so lives in
// SyscallShim.cpp; the scanner itself is shared.
namespace {
struct StringScanState {
    const char* p;
};
int StringPeek(void* state) {
    auto* s = static_cast<StringScanState*>(state);
    if (s->p == nullptr || *s->p == '\0') return -1;
    return static_cast<unsigned char>(*s->p);
}
void StringAdvance(void* state) {
    auto* s = static_cast<StringScanState*>(state);
    if (s->p != nullptr && *s->p != '\0') ++s->p;
}
}  // namespace

// sscanf(str, format, ...): the output pointers start at the third integer register.
extern "C" int kudroid_sscanf_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    const char* input = reinterpret_cast<const char*>(registers->gp[0]);
    const char* format = reinterpret_cast<const char*>(registers->gp[1]);
    if (input == nullptr || format == nullptr) return -1;
    StringScanState state{input};
    return kudroid::ScanGuestVarargsFrom(&StringPeek, &StringAdvance, &state, format,
                                        registers, /*firstGpIndex=*/2, /*firstFpIndex=*/0);
}

// __isoc99_sscanf has the same signature; glibc-built guests call it instead.
extern "C" int kudroid_isoc99_sscanf_from_registers(const uint64_t* frame) {
    return kudroid_sscanf_from_registers(frame);
}
#endif

// Scanning a string from a guest va_list, for vsscanf.
//
// Not inside the __aarch64__ guard: the declaration is unconditional so the shim table can
// name it on every host, and on a non-arm64 host the guest IS the host, so the plain
// forwarding in SyscallShim.cpp is used instead and this is never called.
extern "C" int kudroid_scan_guest_va_list(const char* input, const char* format,
                                          const void* guest_ap) {
    if (input == nullptr || format == nullptr) return -1;
    kudroid::GuestVarargs registers;
    unsigned firstGp = 0;
    unsigned firstFp = 0;
    if (!kudroid::GuestVarargsFromVaList(guest_ap, &registers, &firstGp, &firstFp)) {
        // A va_list that cannot be trusted must not be scanned through: every conversion
        // would WRITE to an address taken from it.
        return -1;
    }
    struct State { const char* p; } state{input};
    const auto peek = [](void* s) -> int {
        auto* st = static_cast<State*>(s);
        if (st->p == nullptr || *st->p == '\0') return -1;
        return static_cast<unsigned char>(*st->p);
    };
    const auto advance = [](void* s) {
        auto* st = static_cast<State*>(s);
        if (st->p != nullptr && *st->p != '\0') ++st->p;
    };
    return kudroid::ScanGuestVarargsFrom(peek, advance, &state, format, &registers, firstGp,
                                        firstFp);
}
