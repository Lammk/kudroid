// test_kuart_network_bitmap.cpp - two things that stalled a real app, run through the real
// framework.dex:
//  * ConnectivityManager follows the host's network path (getAllNetworkInfo used to be
//    missing, so an app polling it threw and reported "no internet");
//  * Bitmap.copyPixelsToBuffer runs natively (a per-pixel Java loop made every rendered
//    text bitmap cost hundreds of milliseconds).
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/platform/NetworkState.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using kudroid::kuart::DexArray;
using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexJniEnv;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexObject;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

int g_checks = 0;
int g_failures = 0;
DexClassLinker* g_linker = nullptr;
Interpreter* g_interp = nullptr;

constexpr int kTypeMobile = 0;
constexpr int kTypeWifi = 1;

void Check(bool condition, const char* message) {
    ++g_checks;
    std::printf("%s %s\n", condition ? "  OK  " : "  FAIL", message);
    if (!condition) ++g_failures;
}

bool Run(DexMethod* method, std::vector<DexValue> args, DexValue* result, bool* threw) {
    if (method == nullptr) return false;
    g_interp->ClearPendingException();
    const DexValue value = g_interp->Execute(method, args.data(), args.size());
    const bool exception = g_interp->HasPendingException();
    if (exception) g_interp->ClearPendingException();
    if (threw != nullptr) *threw = exception;
    if (!exception && result != nullptr) *result = value;
    return !exception;
}

bool CallStatic(const char* descriptor, const char* name, const char* sig,
                std::vector<DexValue> args, DexValue* result, bool* threw = nullptr) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub || !g_interp->EnsureInitialized(klass)) return false;
    DexMethod* method = klass->FindDirectMethod(name, sig);
    if (method == nullptr) method = klass->FindVirtualMethod(name, sig);
    return Run(method, std::move(args), result, threw);
}

bool CallVirtual(DexObject* receiver, const char* name, const char* sig,
                 std::vector<DexValue> args, DexValue* result, bool* threw = nullptr) {
    if (receiver == nullptr) return false;
    DexMethod* method = receiver->clazz->FindVirtualMethod(name, sig);
    if (method == nullptr) method = receiver->clazz->FindDirectMethod(name, sig);
    args.insert(args.begin(), DexValue::Ref(receiver));
    return Run(method, std::move(args), result, threw);
}

DexObject* NewObject(const char* descriptor, const char* sig, std::vector<DexValue> args) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub || !g_interp->EnsureInitialized(klass)) return nullptr;
    DexObject* object = g_linker->AllocObject(klass);
    DexMethod* ctor = klass->FindDirectMethod("<init>", sig);
    if (object == nullptr || ctor == nullptr) return nullptr;
    args.insert(args.begin(), DexValue::Ref(object));
    return Run(ctor, args, nullptr, nullptr) ? object : nullptr;
}

DexArray* NewArray(const char* descriptor, int count) {
    DexClass* klass = g_linker->FindClass(descriptor);
    return klass != nullptr ? g_linker->AllocArray(klass, count) : nullptr;
}

int Int(DexObject* receiver, const char* name, const char* sig, std::vector<DexValue> args = {}) {
    DexValue v;
    return CallVirtual(receiver, name, sig, std::move(args), &v) ? v.i : -999;
}

// ── network ─────────────────────────────────────────────────────────────────

void test_network() {
    std::printf("[network] ConnectivityManager follows the host's path\n");
    DexObject* cm = NewObject("Landroid/net/ConnectivityManager;", "()V", {});
    DexValue v;

    Check(CallVirtual(cm, "getAllNetworkInfo", "()[Landroid/net/NetworkInfo;", {}, &v) &&
              v.l != nullptr && reinterpret_cast<DexArray*>(v.l)->length == 2,
          "getAllNetworkInfo exists and returns the supported types");

    // Default: a host that reports nothing is assumed to be online over Wi-Fi.
    CallVirtual(cm, "getActiveNetworkInfo", "()Landroid/net/NetworkInfo;", {}, &v);
    Check(v.l != nullptr && Int(v.l, "isConnected", "()Z") == 1 &&
              Int(v.l, "getType", "()I") == kTypeWifi,
          "before the host reports, the device is online over Wi-Fi");

    kudroid_set_network_state(0, kTypeWifi, 0, 0);
    CallVirtual(cm, "getActiveNetworkInfo", "()Landroid/net/NetworkInfo;", {}, &v);
    Check(v.l == nullptr, "offline: there is no active network info");
    CallVirtual(cm, "getActiveNetwork", "()Landroid/net/Network;", {}, &v);
    Check(v.l == nullptr, "offline: there is no active network");
    CallVirtual(cm, "getAllNetworkInfo", "()[Landroid/net/NetworkInfo;", {}, &v);
    DexArray* all = reinterpret_cast<DexArray*>(v.l);
    bool anyConnected = false;
    for (int i = 0; all != nullptr && i < all->length; ++i) {
        DexObject* info = all->Get<DexObject*>(i);
        if (info != nullptr && Int(info, "isConnected", "()Z") == 1) anyConnected = true;
    }
    Check(all != nullptr && !anyConnected, "offline: no listed network is connected");
    Check(kudroid_network_has_pending() == 1, "a changed state is announced to the main Looper");

    kudroid_set_network_state(1, kTypeMobile, 1, 0);
    CallVirtual(cm, "getActiveNetworkInfo", "()Landroid/net/NetworkInfo;", {}, &v);
    Check(v.l != nullptr && Int(v.l, "getType", "()I") == kTypeMobile &&
              Int(v.l, "isConnected", "()Z") == 1,
          "cellular: the active network is mobile");
    Check(Int(cm, "isActiveNetworkMetered", "()Z") == 1, "an expensive path is metered");

    DexValue net, caps;
    CallVirtual(cm, "getActiveNetwork", "()Landroid/net/Network;", {}, &net);
    CallVirtual(cm, "getNetworkCapabilities", "(Landroid/net/Network;)Landroid/net/NetworkCapabilities;",
                {net}, &caps);
    Check(caps.l != nullptr && Int(caps.l, "hasTransport", "(I)Z", {DexValue::Int(0)}) == 1 &&
              Int(caps.l, "hasTransport", "(I)Z", {DexValue::Int(1)}) == 0 &&
              Int(caps.l, "hasCapability", "(I)Z", {DexValue::Int(12)}) == 1 &&
              Int(caps.l, "hasCapability", "(I)Z", {DexValue::Int(11)}) == 0,
          "capabilities describe a metered cellular connection with internet");

    kudroid_set_network_state(1, kTypeWifi, 0, 0);
    CallVirtual(cm, "getActiveNetwork", "()Landroid/net/Network;", {}, &net);
    CallVirtual(cm, "getNetworkCapabilities", "(Landroid/net/Network;)Landroid/net/NetworkCapabilities;",
                {net}, &caps);
    Check(caps.l != nullptr && Int(caps.l, "hasCapability", "(I)Z", {DexValue::Int(11)}) == 1 &&
              Int(cm, "isActiveNetworkMetered", "()Z") == 0,
          "Wi-Fi is not metered");

    // Taking the change clears the announcement, and an unchanged report is not a change.
    CallStatic("Landroid/net/ConnectivityManager;", "dispatchNetworkChange", "()V", {}, nullptr);
    Check(kudroid_network_has_pending() == 0, "dispatching consumes the announcement");
    kudroid_set_network_state(1, kTypeWifi, 0, 0);
    Check(kudroid_network_has_pending() == 0, "reporting the same state again is not a change");
}

// ── bitmap ──────────────────────────────────────────────────────────────────

void SetPixel(DexObject* bitmap, int x, int y, uint32_t color) {
    CallVirtual(bitmap, "setPixel", "(III)V",
                {DexValue::Int(x), DexValue::Int(y), DexValue::Int(static_cast<int32_t>(color))}, nullptr);
}

DexObject* NewBitmap(int w, int h, int config) {
    DexValue v;
    return CallStatic("Landroid/graphics/Bitmap;", "createBitmap", "(III)Landroid/graphics/Bitmap;",
                      {DexValue::Int(w), DexValue::Int(h), DexValue::Int(config)}, &v)
               ? v.l
               : nullptr;
}

DexObject* Allocate(int n) {
    DexValue v;
    CallStatic("Ljava/nio/ByteBuffer;", "allocate", "(I)Ljava/nio/ByteBuffer;", {DexValue::Int(n)}, &v);
    return v.l;
}

void test_bitmap() {
    std::printf("[bitmap] pixel copies are native\n");
    // ARGB_8888 (config 0): premultiplied R, G, B, A bytes.
    DexObject* bmp = NewBitmap(3, 2, 0);
    SetPixel(bmp, 0, 0, 0xFFFF8000);  // opaque orange
    SetPixel(bmp, 1, 0, 0x80FFFFFF);  // half-transparent white
    SetPixel(bmp, 2, 0, 0x00123456);  // transparent
    SetPixel(bmp, 0, 1, 0xFF000001);
    DexObject* buf = Allocate(24);
    CallVirtual(bmp, "copyPixelsToBuffer", "(Ljava/nio/Buffer;)V", {DexValue::Ref(buf)}, nullptr);
    DexValue arr;
    CallVirtual(buf, "array", "()[B", {}, &arr);
    auto* bytes = reinterpret_cast<DexArray*>(arr.l);
    auto b = [&](int i) { return static_cast<uint8_t>(bytes->Get<int8_t>(i)); };
    Check(b(0) == 0xFF && b(1) == 0x80 && b(2) == 0x00 && b(3) == 0xFF,
          "an opaque pixel is stored as R, G, B, A");
    Check(b(4) == 0x80 && b(5) == 0x80 && b(6) == 0x80 && b(7) == 0x80,
          "colour channels are premultiplied by alpha");
    Check(b(8) == 0 && b(9) == 0 && b(10) == 0 && b(11) == 0, "a transparent pixel is all zero");
    Check(Int(buf, "position", "()I") == 24, "the buffer position advances by the byte count");

    // A buffer that is too small is refused, and nothing is written.
    DexObject* small = Allocate(8);
    bool threw = false;
    CallVirtual(bmp, "copyPixelsToBuffer", "(Ljava/nio/Buffer;)V", {DexValue::Ref(small)}, nullptr, &threw);
    Check(threw, "a buffer that is too small is rejected");

    // Copies start at the buffer's current position.
    DexObject* offset = Allocate(32);
    CallVirtual(offset, "position", "(I)Ljava/nio/Buffer;", {DexValue::Int(8)}, nullptr);
    CallVirtual(bmp, "copyPixelsToBuffer", "(Ljava/nio/Buffer;)V", {DexValue::Ref(offset)}, nullptr);
    CallVirtual(offset, "array", "()[B", {}, &arr);
    bytes = reinterpret_cast<DexArray*>(arr.l);
    Check(b(8) == 0xFF && b(9) == 0x80 && b(0) == 0, "a copy starts at the buffer's position");

    // Direct buffers take the same bytes.
    DexValue direct;
    CallStatic("Ljava/nio/ByteBuffer;", "allocateDirect", "(I)Ljava/nio/ByteBuffer;", {DexValue::Int(24)}, &direct);
    CallVirtual(bmp, "copyPixelsToBuffer", "(Ljava/nio/Buffer;)V", {direct}, nullptr);
    DexValue first, second;
    CallVirtual(direct.l, "get", "(I)B", {DexValue::Int(1)}, &first);
    CallVirtual(direct.l, "get", "(I)B", {DexValue::Int(4)}, &second);
    Check((first.i & 0xFF) == 0x80 && (second.i & 0xFF) == 0x80, "a direct buffer receives the same bytes");

    // ALPHA_8 (config 2 in createBitmap): one alpha byte per pixel.
    DexObject* alpha = NewBitmap(2, 1, 2);
    SetPixel(alpha, 0, 0, 0xC0102030);
    SetPixel(alpha, 1, 0, 0x10FFFFFF);
    DexObject* abuf = Allocate(2);
    CallVirtual(alpha, "copyPixelsToBuffer", "(Ljava/nio/Buffer;)V", {DexValue::Ref(abuf)}, nullptr);
    CallVirtual(abuf, "array", "()[B", {}, &arr);
    bytes = reinterpret_cast<DexArray*>(arr.l);
    Check(b(0) == 0xC0 && b(1) == 0x10, "ALPHA_8 stores one alpha byte per pixel");

    // RGB_565 (config 1): little-endian 5-6-5.
    DexObject* rgb = NewBitmap(1, 1, 1);
    SetPixel(rgb, 0, 0, 0xFFFF0000);
    DexObject* rbuf = Allocate(2);
    CallVirtual(rgb, "copyPixelsToBuffer", "(Ljava/nio/Buffer;)V", {DexValue::Ref(rbuf)}, nullptr);
    CallVirtual(rbuf, "array", "()[B", {}, &arr);
    bytes = reinterpret_cast<DexArray*>(arr.l);
    Check(b(0) == 0x00 && b(1) == 0xF8, "RGB_565 packs red into the top five bits");

    // A text-sized bitmap copies in a blink. With a per-pixel Java loop this took seconds.
    DexObject* big = NewBitmap(512, 96, 0);
    DexObject* bigBuf = Allocate(512 * 96 * 4);
    const auto t0 = std::chrono::steady_clock::now();
    CallVirtual(big, "copyPixelsToBuffer", "(Ljava/nio/Buffer;)V", {DexValue::Ref(bigBuf)}, nullptr);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();
    Check(ms < 100, "copying a 512x96 bitmap takes well under 100 ms");

    // Rectangle copies clip at the edges and move whole rows.
    DexObject* src = NewBitmap(4, 3, 0);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) SetPixel(src, x, y, 0xFF000000u | (y * 16 + x));
    }
    DexArray* out = NewArray("[I", 16);
    CallVirtual(src, "getPixels", "([IIIIIII)V",
                {DexValue::Ref(reinterpret_cast<DexObject*>(out)), DexValue::Int(0), DexValue::Int(4),
                 DexValue::Int(-1), DexValue::Int(1), DexValue::Int(3), DexValue::Int(4)}, nullptr);
    // Window x=-1..1, y=1..4: only x=0,1 and y=1,2 exist.
    Check(out->Get<int32_t>(0) == 0 && out->Get<int32_t>(1) == (0xFF000000u | 16) &&
              out->Get<int32_t>(2) == (0xFF000000u | 17) &&
              out->Get<int32_t>(4 + 1) == (0xFF000000u | 32) && out->Get<int32_t>(8 + 1) == 0,
          "getPixels clips to the bitmap and leaves the rest untouched");

    DexObject* dst = NewBitmap(4, 3, 0);
    DexArray* in = NewArray("[I", 4);
    for (int i = 0; i < 4; ++i) in->Set<int32_t>(i, 0xFF0000AA + i);
    CallVirtual(dst, "setPixels", "([IIIIIII)V",
                {DexValue::Ref(reinterpret_cast<DexObject*>(in)), DexValue::Int(0), DexValue::Int(2),
                 DexValue::Int(3), DexValue::Int(2), DexValue::Int(2), DexValue::Int(2)}, nullptr);
    DexValue p;
    CallVirtual(dst, "getPixel", "(II)I", {DexValue::Int(3), DexValue::Int(2)}, &p);
    DexValue outside;
    CallVirtual(dst, "getPixel", "(II)I", {DexValue::Int(0), DexValue::Int(2)}, &outside);
    Check(static_cast<uint32_t>(p.i) == 0xFF0000AA && outside.i == 0,
          "setPixels writes the visible part of the rectangle");

    // drawRect on a bitmap-backed canvas fills the clipped rectangle natively.
    DexObject* target = NewBitmap(4, 3, 0);
    DexObject* canvas = NewObject("Landroid/graphics/Canvas;", "(Landroid/graphics/Bitmap;)V",
                                  {DexValue::Ref(target)});
    DexObject* paint = NewObject("Landroid/graphics/Paint;", "()V", {});
    CallVirtual(paint, "setColor", "(I)V", {DexValue::Int(static_cast<int32_t>(0xFF00FF00))}, nullptr);
    CallVirtual(canvas, "drawRect", "(FFFFLandroid/graphics/Paint;)V",
                {DexValue::Float(-5), DexValue::Float(1), DexValue::Float(2), DexValue::Float(9),
                 DexValue::Ref(paint)}, nullptr);
    DexValue inside, beyond, above;
    CallVirtual(target, "getPixel", "(II)I", {DexValue::Int(1), DexValue::Int(2)}, &inside);
    CallVirtual(target, "getPixel", "(II)I", {DexValue::Int(2), DexValue::Int(2)}, &beyond);
    CallVirtual(target, "getPixel", "(II)I", {DexValue::Int(0), DexValue::Int(0)}, &above);
    Check(static_cast<uint32_t>(inside.i) == 0xFF00FF00 && beyond.i == 0 && above.i == 0,
          "drawRect on a bitmap canvas fills only the clipped rectangle");

    CallVirtual(dst, "eraseColor", "(I)V", {DexValue::Int(0x11223344)}, nullptr);
    CallVirtual(dst, "getPixel", "(II)I", {DexValue::Int(1), DexValue::Int(1)}, &p);
    Check(p.i == 0x11223344, "eraseColor fills every pixel");
}

}  // namespace

int main() {
    std::printf("=== KuART network state and bitmap copies ===\n");
    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex", &error)) {
        std::printf("  FAIL AddDexFile(framework.dex): %s\n", error.c_str());
        return 1;
    }
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    interp.set_instruction_limit(2000ull * 1000ull * 1000ull);
    g_linker = &linker;
    g_interp = &interp;

    test_network();
    test_bitmap();

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
