#include "kudroid/platform/SurfaceTextureHost.h"
#include "kudroid/platform/MediaShim.h"
#include "kudroid/platform/ShimDefs.h"
#include "kudroid/Log.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
int activeUnit = 0x84c0, program = 5, contextId = 1, notices = 0, failures = 0;
int uploaded = 0, parameterCalls = 0;
std::unordered_map<unsigned, int> state = {{0x0cf5, 8}, {0x0cf2, 9}, {0x0cf3, 3},
    {0x0cf4, 7}, {0x806e, 5}, {0x806d, 2}, {0x88ef, 42}};
std::unordered_map<int, unsigned> bindings = {{0x84c0, 99}, {0x84c1, 77}};
std::vector<uint8_t> rgba;
void check(bool ok, const char* message) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", message); if (!ok) ++failures; }
void* current() { return reinterpret_cast<void*>(static_cast<uintptr_t>(contextId)); }
void get(unsigned pname, int* value) {
    if (pname == 0x84e0) *value = activeUnit;
    else if (pname == 0x8069) *value = bindings[activeUnit];
    else if (pname == 0x8b8d) *value = program;
    else if (pname == 0x8b4d) *value = 8;
    else *value = state[pname];
}
void bind(unsigned target, unsigned name) { assert(target == 0x0de1); bindings[activeUnit] = name; }
void active(unsigned unit) { activeUnit = static_cast<int>(unit); }
void store(unsigned pname, int value) { state[pname] = value; }
void buffer(unsigned target, unsigned value) { assert(target == 0x88ec); state[0x88ef] = static_cast<int>(value); }
void parameter(unsigned, unsigned, int) { ++parameterCalls; }
const char* strings(unsigned pname) { return pname == 0x1f02 ? "OpenGL ES 3.0" : ""; }
void upload(unsigned, int, int, int width, int height, int, unsigned, unsigned, const void* pixels) {
    assert(state[0x88ef] == 0 && state[0x0cf2] == 0 && state[0x0cf3] == 0 && state[0x0cf4] == 0);
    uploaded = static_cast<int>(bindings[activeUnit]);
    const auto* bytes = static_cast<const uint8_t*>(pixels);
    rgba.assign(bytes, bytes + width * height * 4);
}
void programIv(unsigned, unsigned pname, int* value) { *value = pname == 0x8b86 ? 1 : 64; }
void uniform(unsigned, unsigned, int, int* length, int* size, unsigned* type, char* name) {
    std::strcpy(name, "kdExternalSampler_video"); *length = static_cast<int>(std::strlen(name)); *size = 1; *type = 0x8b5e;
}
int location(unsigned, const char* name) { return std::strcmp(name, "kdExternalSampler_video") == 0 ? 7 : -1; }
void uniformIv(unsigned, int, int* unit) { *unit = 0; }
template <typename T> T media(const char* name) {
    size_t count = 0;
    const auto* symbols = kudroid::get_media_symbols(&count);
    for (size_t i = 0; i < count; ++i)
        if (std::strcmp(symbols[i].name, name) == 0) return reinterpret_cast<T>(symbols[i].address);
    std::abort();
}
}
namespace kudroid {
void* get_egl_func(const char* name) { return std::strcmp(name, "eglGetCurrentContext") == 0 ? reinterpret_cast<void*>(current) : nullptr; }
void* get_gl_func(const char* name) {
#define MAP(n, f) if (std::strcmp(name, n) == 0) return reinterpret_cast<void*>(f)
    MAP("glGetIntegerv", get); MAP("glBindTexture", bind); MAP("glActiveTexture", active);
    MAP("glPixelStorei", store); MAP("glBindBuffer", buffer); MAP("glTexImage2D", upload);
    MAP("glTexParameteri", parameter); MAP("glGetString", strings); MAP("glGetProgramiv", programIv);
    MAP("glGetActiveUniform", uniform); MAP("glGetUniformLocation", location); MAP("glGetUniformiv", uniformIv);
#undef MAP
    return nullptr;
}
namespace log { void write(Level, const char*, const char*, ...) {} }
}
extern "C" void kudroid_looper_wake_main() { ++notices; }
extern "C" int kudroid_android_log_message(int, const char*, const char*) { return 0; }
extern "C" int kudroid_native_window_surface_texture_id(void* window) { return window == reinterpret_cast<void*>(1) ? 1 : 0; }

int main() {
    using namespace kudroid::surface_texture;
    const std::string source = "#version 300 es\n#extension GL_OES_EGL_image_external_essl3 : require\n"
        "// samplerExternalOES ignored;\nuniform samplerExternalOES video;\nvec4 f(vec2 uv) {return texture(video, uv);}";
    const auto translated = shader(source);
    check(translated.find("uniform sampler2D kdExternalSampler_video;") != std::string::npos &&
          translated.find("texture(kdExternalSampler_video, uv)") != std::string::npos,
          "external sampler declaration and uses remain linked");
    check(translated.find("#extension GL_OES") == std::string::npos &&
          translated.find("// samplerExternalOES ignored;") != std::string::npos,
          "only external directive is removed and comments survive");
    check(shader("uniform sampler2D image;") == "uniform sampler2D image;", "ordinary shaders remain byte-identical");
    const auto helper = shader("precision lowp samplerExternalOES; uniform samplerExternalOES video;"
        "vec4 sampleIt(samplerExternalOES inputTex, vec2 uv){ return texture(inputTex,uv); }");
    check(helper.find("precision lowp sampler2D;") != std::string::npos && helper.find("vec2 uv") != std::string::npos,
          "sampler precision and function parameters retain valid GLSL types");
    check(shader("precision lowp samplerExternalOES;") == "precision lowp sampler2D;",
          "external default precision is translated without a uniform declaration");
    const auto commented = shader("/*\n#extension GL_OES_EGL_image_external : require */\n"
        "# extension GL_OES_EGL_image_external : require\nuniform samplerExternalOES video;");
    check(commented.find("require */") != std::string::npos && commented.find("# extension") == std::string::npos,
          "directive parsing preserves block comments and accepts preprocessor whitespace");
    kudroid_surface_texture_create(1, 22);
    const auto before = state;
    check(post(1, {16, 16, 16, 16, 128, 128}, 2, 2, 100), "valid I420 frame accepted");
    check(post(1, {81, 81, 81, 81, 90, 240}, 2, 2, 200), "new frame replaces an unconsumed frame");
    check(kudroid_surface_texture_update(1) == 200 && uploaded == 22 && notices == 2,
          "update latches the latest frame with its media timestamp");
    check(rgba.size() == 16 && rgba[0] >= 253 && rgba[1] <= 1 && rgba[2] <= 1 && rgba[3] == 255,
          "I420 color conversion produces actual red pixels");
    check(state == before && bindings[0x84c0] == 99 && activeUnit == 0x84c0,
          "upload preserves PBO, unpack state, texture binding and active unit");
    check(kudroid_surface_texture_update(1) == 200, "empty update preserves the latched timestamp");
    check(!post(1, {0}, 2, 2, 300), "truncated image is rejected before upload");
    check(kudroid::surface_texture::bind(0x8d65, 22) && bindings[0x84c0] == 99, "external binding is independent of the 2D binding");
    int name = 0;
    check(binding(0x8d67, &name) && name == 22, "external binding query returns the logical texture");
    {
        use_program(5);
        DrawScope draw;
        check(bindings[0x84c0] == 22 && activeUnit == 0x84c0, "external sampler draws from the decoded texture");
    }
    check(bindings[0x84c0] == 99 && activeUnit == 0x84c0, "draw restores ordinary texture bindings");
    const unsigned deleted = 22;
    deleted_textures(1, &deleted);
    check(binding(0x8d67, &name) && name == 0, "deletion clears the logical external binding");
    kudroid_surface_texture_update(1);
    check(binding(0x8d67, &name) && name == 22, "empty update still binds its associated texture");
    contextId = 2;
    check(binding(0x8d67, &name) && name == 0, "independent contexts keep separate external bindings");
    check(kudroid_surface_texture_update(1) == -1, "update rejects the wrong GL context");
    contextId = 1;
    kudroid_surface_texture_detach(1);
    check(kudroid_surface_texture_update(1) == -1, "detached texture cannot consume a frame");
    kudroid_surface_texture_attach(1, 23);
    check(kudroid_surface_texture_update(1) == 200, "reattach preserves the latched timestamp");
    kudroid_surface_texture_release(1);
    check(!exists(1) && !post(1, {16,16,16,16,128,128}, 2, 2, 0), "release prevents future codec delivery");

    // One red 2x2 VP8 keyframe encoded from a synthetic color source.
    const uint8_t packet[] = {0x30,0x02,0x00,0x9d,0x01,0x2a,0x02,0x00,0x02,0x00,0x0b,0xc7,0x08,0x85,
        0x85,0x88,0x85,0x84,0x88,0x3f,0x82,0x02,0x74,0xf4,0x6f,0x9d,0x00,0xfe,0xd0,0xc9,0x23,0x0d,
        0x1a,0xfe,0x28,0x79,0xad,0xec,0x1d,0x30,0x00};
    auto formatNew = media<void* (*)()>("AMediaFormat_new");
    auto setString = media<void (*)(void*, const char*, const char*)>("AMediaFormat_setString");
    auto setInt = media<void (*)(void*, const char*, int32_t)>("AMediaFormat_setInt32");
    auto getInt = media<bool (*)(void*, const char*, int32_t*)>("AMediaFormat_getInt32");
    auto deleteFormat = media<int (*)(void*)>("AMediaFormat_delete");
    auto codecNew = media<void* (*)(const char*)>("AMediaCodec_createDecoderByType");
    auto configure = media<int (*)(void*, const void*, void*, void*, uint32_t)>("AMediaCodec_configure");
    auto start = media<int (*)(void*)>("AMediaCodec_start");
    auto stop = media<int (*)(void*)>("AMediaCodec_stop");
    auto destroy = media<int (*)(void*)>("AMediaCodec_delete");
    auto dequeueInput = media<ssize_t (*)(void*, int64_t)>("AMediaCodec_dequeueInputBuffer");
    auto input = media<uint8_t* (*)(void*, size_t, size_t*)>("AMediaCodec_getInputBuffer");
    auto queue = media<int (*)(void*, size_t, int64_t, size_t, uint64_t, uint32_t)>("AMediaCodec_queueInputBuffer");
    auto output = media<uint8_t* (*)(void*, size_t, size_t*)>("AMediaCodec_getOutputBuffer");
    struct Info { int32_t offset, size; int64_t pts; uint32_t flags; } info{};
    auto dequeueOutput = media<ssize_t (*)(void*, void*, int64_t)>("AMediaCodec_dequeueOutputBuffer");
    auto release = media<int (*)(void*, size_t, bool)>("AMediaCodec_releaseOutputBuffer");
    auto outputFormat = media<void* (*)(void*)>("AMediaCodec_getOutputFormat");
    for (bool surface : {false, true}) {
        kudroid_surface_texture_create(1, 24);
        void* format = formatNew();
        setString(format, "mime", "video/x-vnd.on2.vp8"); setInt(format, "width", 2); setInt(format, "height", 2);
        void* codec = codecNew("video/x-vnd.on2.vp8");
        check(configure(codec, format, surface ? reinterpret_cast<void*>(1) : nullptr, nullptr, 0) == 0 && start(codec) == 0,
              surface ? "NDK decoder configures a registered texture surface" : "NDK CPU decoder still configures");
        const auto index = dequeueInput(codec, 0);
        size_t capacity = 0;
        auto* bytes = input(codec, static_cast<size_t>(index), &capacity);
        if (!bytes || capacity < sizeof(packet)) { check(false, "decoder input buffer exists"); return 1; }
        std::memcpy(bytes, packet, sizeof(packet));
        check(queue(codec, static_cast<size_t>(index), 0, sizeof(packet), 1234, 0) == 0 &&
              dequeueOutput(codec, &info, 0) == -2, "real VP8 input announces a decoded output format");
        void* decodedFormat = outputFormat(codec);
        int32_t color = 0;
        check(getInt(decodedFormat, "color-format", &color) && color == (surface ? 0x7f000789 : 19),
              "output format follows Surface versus CPU mode");
        deleteFormat(decodedFormat);
        const auto outIndex = dequeueOutput(codec, &info, 0);
        check(outIndex >= 0 && info.pts == 1234 && info.size == 6, "decoded output retains real timestamp and frame size");
        bytes = output(codec, static_cast<size_t>(outIndex), &capacity);
        check(surface ? bytes == nullptr && capacity == 0 : bytes != nullptr && capacity == 6,
              "only CPU mode exposes decoded byte storage");
        const int beforeNotices = notices;
        check(release(codec, static_cast<size_t>(outIndex), true) == 0 && notices == beforeNotices + (surface ? 1 : 0),
              "render release delivers only Surface output");
        if (surface) check(kudroid_surface_texture_update(1) == 1234000 && uploaded == 24 && rgba[0] > 250,
                           "NDK compressed packet reaches actual GL pixels");
        const auto eos = dequeueInput(codec, 0);
        check(queue(codec, static_cast<size_t>(eos), 0, 0, 2000, 4) == 0, "empty EOS input is accepted");
        const auto eof = dequeueOutput(codec, &info, 0);
        check(eof >= 0 && info.size == 0 && info.flags == 4 &&
              release(codec, static_cast<size_t>(eof), true) == 0, "EOS remains ordered after the decoded frame");
        stop(codec); destroy(codec); deleteFormat(format); kudroid_surface_texture_release(1);
    }
    return failures ? 1 : 0;
}
