// libcore.io.KuZlib: zlib streams and checksums behind java.util.zip.
//
// A stream is an opaque handle. Input and output buffers are passed on every call and the
// consumed/produced counts come back through a two-element int array, so no buffer outlives a
// call and nothing here keeps pointers into the Java heap.
#include <zlib.h>

#include <cstdint>
#include <cstring>

#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexValue.h"
#include "kudroid/kuart/LibCore.h"

namespace kudroid {
namespace kuart {

namespace {

DexArray* Bytes(const DexValue& v) { return reinterpret_cast<DexArray*>(v.l); }

bool RangeOk(const DexArray* a, int off, int len) {
    return a != nullptr && off >= 0 && len >= 0 && off <= a->length && len <= a->length - off;
}

// Maps a zlib return code to what the Java side acts on:
// 0 progress, 1 end of stream, 2 needs a dictionary, negative for an error.
int Status(int rc) {
    switch (rc) {
        case Z_OK: return 0;
        case Z_BUF_ERROR: return 0;  // nothing to do with the buffers given
        case Z_STREAM_END: return 1;
        case Z_NEED_DICT: return 2;
        case Z_MEM_ERROR: return -4;
        default: return -3;  // data or stream error
    }
}

int64_t ToHandle(void* p) { return static_cast<int64_t>(reinterpret_cast<intptr_t>(p)); }
z_stream* Stream(const DexValue& v) { return reinterpret_cast<z_stream*>(v.j); }

}  // namespace

bool Invoke_libcore_io_KuZlib(Interpreter* /*interp*/, const char* name, const DexValue* args,
                              size_t num_args, DexValue* result) {
    const auto Is = [&](const char* n) { return std::strcmp(name, n) == 0; };

    if (Is("inflateInit") && num_args >= 1) {
        auto* z = new z_stream();
        std::memset(z, 0, sizeof(*z));
        const int rc = inflateInit2(z, args[0].i ? -15 : 15);
        if (rc != Z_OK) {
            delete z;
            *result = DexValue::Long(0);
        } else {
            *result = DexValue::Long(ToHandle(z));
        }
        return true;
    }
    if (Is("deflateInit") && num_args >= 3) {
        auto* z = new z_stream();
        std::memset(z, 0, sizeof(*z));
        const int rc = deflateInit2(z, args[0].i, Z_DEFLATED, args[1].i ? -15 : 15, 8, args[2].i);
        if (rc != Z_OK) {
            delete z;
            *result = DexValue::Long(0);
        } else {
            *result = DexValue::Long(ToHandle(z));
        }
        return true;
    }
    if ((Is("inflate") || Is("deflate")) && num_args >= 9) {
        // (handle, in, inOff, inLen, out, outOff, outLen, flush-or-unused, counts)
        z_stream* z = Stream(args[0]);
        DexArray* in = Bytes(args[1]);
        DexArray* out = Bytes(args[4]);
        DexArray* counts = Bytes(args[8]);
        const int inOff = args[2].i, inLen = args[3].i, outOff = args[5].i, outLen = args[6].i;
        if (z == nullptr || !RangeOk(out, outOff, outLen) || counts == nullptr || counts->length < 2 ||
            (inLen > 0 && !RangeOk(in, inOff, inLen))) {
            *result = DexValue::Int(-3);
            return true;
        }
        z->next_in = inLen > 0 ? in->Data() + inOff : nullptr;
        z->avail_in = static_cast<uInt>(inLen);
        z->next_out = out->Data() + outOff;
        z->avail_out = static_cast<uInt>(outLen);
        const int rc = Is("inflate") ? inflate(z, Z_NO_FLUSH) : deflate(z, args[7].i);
        counts->Set<int32_t>(0, inLen - static_cast<int>(z->avail_in));
        counts->Set<int32_t>(1, outLen - static_cast<int>(z->avail_out));
        *result = DexValue::Int(Status(rc));
        return true;
    }
    if (Is("inflateReset") && num_args >= 1) {
        z_stream* z = Stream(args[0]);
        *result = DexValue::Int(z != nullptr && inflateReset(z) == Z_OK ? 0 : -3);
        return true;
    }
    if (Is("deflateReset") && num_args >= 1) {
        z_stream* z = Stream(args[0]);
        *result = DexValue::Int(z != nullptr && deflateReset(z) == Z_OK ? 0 : -3);
        return true;
    }
    if (Is("inflateEnd") && num_args >= 1) {
        if (z_stream* z = Stream(args[0])) {
            inflateEnd(z);
            delete z;
        }
        return true;
    }
    if (Is("deflateEnd") && num_args >= 1) {
        if (z_stream* z = Stream(args[0])) {
            deflateEnd(z);
            delete z;
        }
        return true;
    }
    if ((Is("crc32") || Is("adler32")) && num_args >= 4) {
        DexArray* buf = Bytes(args[1]);
        const int off = args[2].i, len = args[3].i;
        if (!RangeOk(buf, off, len)) {
            *result = DexValue::Int(args[0].i);
            return true;
        }
        const uLong v = Is("crc32")
                            ? crc32(static_cast<uLong>(static_cast<uint32_t>(args[0].i)), buf->Data() + off, static_cast<uInt>(len))
                            : adler32(static_cast<uLong>(static_cast<uint32_t>(args[0].i)), buf->Data() + off, static_cast<uInt>(len));
        *result = DexValue::Int(static_cast<int32_t>(static_cast<uint32_t>(v)));
        return true;
    }
    return false;
}

}  // namespace kuart
}  // namespace kudroid
