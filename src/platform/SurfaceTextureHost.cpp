#include "kudroid/platform/SurfaceTextureHost.h"
#include "kudroid/platform/GraphicsShim.h"
#include "kudroid/platform/MediaShim.h"
#include "kudroid/Log.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#define HOST_GL(type, name) ([] { static auto f = reinterpret_cast<type>(kudroid::get_gl_func(name)); return f; }())
namespace kudroid::surface_texture {
namespace {
constexpr unsigned kExternal = 0x8d65, kTexture2D = 0x0de1;
constexpr const char* kSamplerPrefix = "kdExternalSampler_";
struct Frame { std::vector<uint8_t> pixels; int width = 0, height = 0; int64_t timestamp = 0; };
struct Texture {
    unsigned name = 0;
    bool attached = true;
    int64_t timestamp = 0;
    std::shared_ptr<Frame> pending;
    std::shared_ptr<Frame> current;
    void* owner = nullptr;
};
struct Context {
    std::unordered_map<int, unsigned> bindings;
    std::unordered_map<unsigned, std::vector<int>> samplers;
};
std::mutex mutex;
std::unordered_map<int, Texture> textures;
std::unordered_map<void*, Context> contexts;
std::atomic<bool> hasExternalShaders{false};
thread_local unsigned savedBinding = 0;
thread_local unsigned currentProgram = 0;
thread_local std::vector<int> currentSamplers;
std::atomic<uint64_t> revision{0};
thread_local uint64_t seenRevision = 0;

void* context() {
    using Fn = void* (*)();
    static auto f = reinterpret_cast<Fn>(get_egl_func("eglGetCurrentContext"));
    return f ? f() : nullptr;
}
int integer(unsigned pname) {
    int value = 0;
    using Fn = void (*)(unsigned, int*);
    if (auto f = HOST_GL(Fn, "glGetIntegerv")) f(pname, &value);
    return value;
}
void bind2d(unsigned name) {
    using Fn = void (*)(unsigned, unsigned);
    if (auto f = HOST_GL(Fn, "glBindTexture")) f(kTexture2D, name);
}
void active(int unit) {
    using Fn = void (*)(unsigned);
    if (auto f = HOST_GL(Fn, "glActiveTexture")) f(static_cast<unsigned>(unit));
}
std::vector<int> find_samplers(unsigned program) {
    using GetIv = void (*)(unsigned, unsigned, int*);
    using GetUniform = void (*)(unsigned, unsigned, int, int*, int*, unsigned*, char*);
    using Location = int (*)(unsigned, const char*);
    auto get = HOST_GL(GetIv, "glGetProgramiv");
    auto uniform = HOST_GL(GetUniform, "glGetActiveUniform");
    auto location = HOST_GL(Location, "glGetUniformLocation");
    std::vector<int> result;
    if (!get || !uniform || !location) return result;
    int count = 0, length = 0;
    get(program, 0x8b86, &count);
    get(program, 0x8b87, &length);
    if (length <= 0 || length > (1 << 20)) return result;
    std::vector<char> name(static_cast<size_t>(length));
    for (int i = 0; i < count; ++i) {
        int size = 0, written = 0;
        unsigned type = 0;
        uniform(program, static_cast<unsigned>(i), length, &written, &size, &type, name.data());
        if (type != 0x8b5e || std::strncmp(name.data(), kSamplerPrefix, std::strlen(kSamplerPrefix))) continue;
        std::string base(name.data());
        if (size > 1 && base.size() >= 3 && base.compare(base.size() - 3, 3, "[0]") == 0) base.resize(base.size() - 3);
        for (int j = 0; j < size; ++j) {
            const std::string element = size > 1 ? base + "[" + std::to_string(j) + "]" : base;
            const int slot = location(program, element.c_str());
            if (slot >= 0) result.push_back(slot);
        }
    }
    return result;
}
}

bool exists(int id) {
    std::lock_guard<std::mutex> lock(mutex);
    return textures.count(id) != 0;
}
bool post(int id, std::vector<uint8_t> pixels, int width, int height, int64_t timestamp) {
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return false;
    const size_t y = static_cast<size_t>(width) * height;
    const size_t chroma = static_cast<size_t>((width + 1) / 2) * ((height + 1) / 2);
    if (y > (64u << 20) || pixels.size() != y + chroma * 2) return false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = textures.find(id);
        if (it == textures.end()) return false;
        it->second.pending = std::make_shared<Frame>(Frame{std::move(pixels), width, height, timestamp});
    }
    kudroid_media_frame_post(id, timestamp);
    return true;
}

bool bind(unsigned target, unsigned texture) {
    if (target != kExternal) return false;
    const int unit = integer(0x84e0);
    const unsigned previous = static_cast<unsigned>(integer(0x8069));
    // Materialize the name without changing the independent 2D binding.
    using Param = void (*)(unsigned, unsigned, int);
    using IsTexture = unsigned char (*)(unsigned);
    auto isTexture = HOST_GL(IsTexture, "glIsTexture");
    const bool initialize = texture && (!isTexture || !isTexture(texture));
    bind2d(texture);
    if (initialize) if (auto f = HOST_GL(Param, "glTexParameteri")) {
        f(kTexture2D, 0x2801, 0x2601);
        f(kTexture2D, 0x2802, 0x812f);
        f(kTexture2D, 0x2803, 0x812f);
    }
    bind2d(previous);
    std::lock_guard<std::mutex> lock(mutex);
    contexts[context()].bindings[unit] = texture;
    return true;
}
void deleted_textures(int count, const unsigned* names) {
    if (!names || count <= 0) return;
    std::lock_guard<std::mutex> lock(mutex);
    auto& bindings = contexts[context()].bindings;
    for (auto& entry : bindings)
        for (int i = 0; i < count; ++i) if (entry.second == names[i]) entry.second = 0;
}
unsigned target(unsigned value) {
    if (value != kExternal) return value;
    savedBinding = static_cast<unsigned>(integer(0x8069));
    unsigned name = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        name = contexts[context()].bindings[integer(0x84e0)];
    }
    bind2d(name);
    return kTexture2D;
}
void restore_binding(unsigned value) { if (value == kExternal) bind2d(savedBinding); }
bool binding(unsigned pname, int* value) {
    if (pname != 0x8d67 || !value) return false;
    std::lock_guard<std::mutex> lock(mutex);
    *value = static_cast<int>(contexts[context()].bindings[integer(0x84e0)]);
    return true;
}

// Rename external uniforms so program binaries retain their sampler identity.
std::string shader(std::string source) {
    struct Token { size_t start, size; std::string text; };
    std::vector<Token> tokens;
    for (size_t i = 0; i < source.size();) {
        if (source.compare(i, 2, "//") == 0) { i = source.find('\n', i); if (i == std::string::npos) break; continue; }
        if (source.compare(i, 2, "/*") == 0) { const auto end = source.find("*/", i + 2); if (end == std::string::npos) break; i = end + 2; continue; }
        if (source[i] == '#') {
            size_t j = i + 1;
            auto word = [&]() {
                while (j < source.size() && (source[j] == ' ' || source[j] == '\t')) ++j;
                const size_t start = j;
                while (j < source.size() && (std::isalnum(static_cast<unsigned char>(source[j])) || source[j] == '_')) ++j;
                return source.substr(start, j - start);
            };
            if (word() == "extension") {
                const std::string name = word();
                if (name == "GL_OES_EGL_image_external" || name == "GL_OES_EGL_image_external_essl3") {
                    const size_t end = source.find('\n', i);
                    const size_t count = (end == std::string::npos ? source.size() : end) - i;
                    source.replace(i, count, count, ' ');
                    i += count;
                    continue;
                }
            }
        }
        if (std::isspace(static_cast<unsigned char>(source[i]))) { ++i; continue; }
        const size_t start = i++;
        if (std::isalnum(static_cast<unsigned char>(source[start])) || source[start] == '_')
            while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) ++i;
        tokens.push_back({start, i - start, source.substr(start, i - start)});
    }
    std::unordered_set<std::string> names;
    bool externalType = false;
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].text != "samplerExternalOES") continue;
        externalType = true;
        const auto& next = tokens[i + 1].text;
        if (next.empty() || (!std::isalpha(static_cast<unsigned char>(next[0])) && next[0] != '_')) continue;
        names.insert(next);
        bool uniform = false;
        for (size_t j = i; j > 0;) {
            const auto& previous = tokens[--j].text;
            if (previous == "uniform") { uniform = true; break; }
            if (previous == ";" || previous == "{" || previous == "(" || previous == "}") break;
        }
        if (uniform) for (size_t j = i + 2; j + 1 < tokens.size() && tokens[j].text != ";"; ++j)
            if (tokens[j].text == ",") names.insert(tokens[j + 1].text);
    }
    if (!externalType) return source;
    if (!names.empty()) hasExternalShaders.store(true, std::memory_order_release);
    std::string output;
    size_t copied = 0;
    for (const auto& token : tokens) {
        output.append(source, copied, token.start - copied);
        if (token.text == "samplerExternalOES") output += "sampler2D";
        else if (names.count(token.text)) output += std::string(kSamplerPrefix) + token.text;
        else output += token.text;
        copied = token.start + token.size;
    }
    output.append(source, copied, source.size() - copied);
    return output;
}
std::string uniform_name(const char* name) { return name ? std::string(kSamplerPrefix) + name : std::string(); }
void program_changed(unsigned program) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& entry : contexts) entry.second.samplers.erase(program);
    revision.fetch_add(1, std::memory_order_release);
}
void binary_loaded() { hasExternalShaders.store(true, std::memory_order_release); }
void use_program(unsigned) {
    currentSamplers.clear();
    seenRevision = revision.load(std::memory_order_acquire);
    if (!hasExternalShaders.load(std::memory_order_acquire) || !context()) return;
    currentProgram = static_cast<unsigned>(integer(0x8b8d));
    if (!currentProgram) return;
    std::lock_guard<std::mutex> lock(mutex);
    auto& state = contexts[context()];
    auto it = state.samplers.find(currentProgram);
    if (it == state.samplers.end()) it = state.samplers.emplace(currentProgram, find_samplers(currentProgram)).first;
    currentSamplers = it->second;
}
void context_changed() { use_program(0); }
void context_destroyed(void* value) {
    std::lock_guard<std::mutex> lock(mutex);
    contexts.erase(value);
    for (auto& entry : textures) if (entry.second.owner == value) {
        entry.second.attached = false;
        entry.second.owner = nullptr;
    }
    revision.fetch_add(1, std::memory_order_release);
}
void active_uniform(int capacity, int* length, unsigned* type, char* name) {
    if (!name || capacity <= 0 || !type || std::strncmp(name, kSamplerPrefix, std::strlen(kSamplerPrefix))) return;
    const size_t prefix = std::strlen(kSamplerPrefix), n = std::strlen(name);
    std::memmove(name, name + prefix, n - prefix + 1);
    if (length) *length = static_cast<int>(n - prefix);
    *type = 0x8d66;
}

DrawScope::DrawScope() {
    if (seenRevision != revision.load(std::memory_order_acquire)) use_program(0);
    if (currentSamplers.empty()) return;
    std::unordered_map<int, unsigned> bindings;
    {
        std::lock_guard<std::mutex> lock(mutex);
        bindings = contexts[context()].bindings;
    }
    active_ = integer(0x84e0);
    using Get = void (*)(unsigned, int, int*);
    auto get = HOST_GL(Get, "glGetUniformiv");
    if (!get) return;
    std::unordered_set<int> seen;
    const int units = integer(0x8b4d);
    for (int location : currentSamplers) {
        int unit = -1;
        get(currentProgram, location, &unit);
        if (unit < 0 || unit >= units || !seen.insert(unit).second) continue;
        active(0x84c0 + unit);
        saved_.emplace_back(0x84c0 + unit, static_cast<unsigned>(integer(0x8069)));
        bind2d(bindings[0x84c0 + unit]);
    }
    active(active_);
}
DrawScope::~DrawScope() {
    for (auto entry : saved_) { active(entry.first); bind2d(entry.second); }
    if (!saved_.empty()) active(active_);
}
}

extern "C" void kudroid_surface_texture_create(int id, unsigned texture) {
    std::lock_guard<std::mutex> lock(kudroid::surface_texture::mutex);
    kudroid::surface_texture::textures[id].name = texture;
    static std::atomic<int> logs{0};
    if (logs.fetch_add(1) < 16) KLOG(kudroid::kInfo, "KuDroidMedia", "SurfaceTexture %d: created texture=%u", id, texture);
}
extern "C" void kudroid_surface_texture_release(int id) {
    std::lock_guard<std::mutex> lock(kudroid::surface_texture::mutex);
    kudroid::surface_texture::textures.erase(id);
}
extern "C" void kudroid_surface_texture_attach(int id, unsigned texture) {
    std::lock_guard<std::mutex> lock(kudroid::surface_texture::mutex);
    auto it = kudroid::surface_texture::textures.find(id);
    if (it != kudroid::surface_texture::textures.end()) {
        it->second.name = texture;
        it->second.attached = true;
        it->second.owner = nullptr;
        if (!it->second.pending) it->second.pending = it->second.current;
    }
}
extern "C" void kudroid_surface_texture_detach(int id) {
    std::lock_guard<std::mutex> lock(kudroid::surface_texture::mutex);
    auto it = kudroid::surface_texture::textures.find(id);
    if (it != kudroid::surface_texture::textures.end()) it->second.attached = false;
}
extern "C" int64_t kudroid_surface_texture_update(int id) {
    using namespace kudroid::surface_texture;
    std::shared_ptr<Frame> frame;
    std::shared_ptr<Frame> currentFrame;
    unsigned texture;
    int64_t timestamp;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = textures.find(id);
        void* current = context();
        if (it == textures.end() || !it->second.attached || !current ||
            (it->second.owner && it->second.owner != current)) return -1;
        it->second.owner = current;
        texture = it->second.name;
        timestamp = it->second.timestamp;
        frame = std::move(it->second.pending);
        currentFrame = it->second.current;
    }
    using IsTexture = unsigned char (*)(unsigned);
    auto isTexture = HOST_GL(IsTexture, "glIsTexture");
    if (!frame && isTexture && !isTexture(texture)) frame = std::move(currentFrame);
    if (!frame) {
        bind(kExternal, texture);
        return timestamp;
    }
    const size_t ySize = static_cast<size_t>(frame->width) * frame->height;
    const size_t cw = (frame->width + 1) / 2, ch = (frame->height + 1) / 2;
    const uint8_t* u = frame->pixels.data() + ySize;
    const uint8_t* v = u + cw * ch;
    std::vector<uint8_t> rgba(ySize * 4);
    auto clip = [](int x) { return static_cast<uint8_t>(std::clamp(x, 0, 255)); };
    for (int y = 0; y < frame->height; ++y) for (int x = 0; x < frame->width; ++x) {
        const size_t i = static_cast<size_t>(y) * frame->width + x;
        const size_t c = static_cast<size_t>(y / 2) * cw + x / 2;
        const int luma = 298 * (static_cast<int>(frame->pixels[i]) - 16);
        const int blue = static_cast<int>(u[c]) - 128, red = static_cast<int>(v[c]) - 128;
        rgba[i * 4] = clip((luma + 409 * red + 128) >> 8);
        rgba[i * 4 + 1] = clip((luma - 100 * blue - 208 * red + 128) >> 8);
        rgba[i * 4 + 2] = clip((luma + 516 * blue + 128) >> 8);
        rgba[i * 4 + 3] = 255;
    }
    using Store = void (*)(unsigned, int);
    using Buffer = void (*)(unsigned, unsigned);
    using Upload = void (*)(unsigned, int, int, int, int, int, unsigned, unsigned, const void*);
    auto store = HOST_GL(Store, "glPixelStorei");
    auto buffer = HOST_GL(Buffer, "glBindBuffer");
    auto upload = HOST_GL(Upload, "glTexImage2D");
    if (!store || !upload) return -1;
    bind(kExternal, texture);
    const unsigned previous = static_cast<unsigned>(integer(0x8069));
    const int alignment = integer(0x0cf5);
    using GetString = const char* (*)(unsigned);
    auto strings = HOST_GL(GetString, "glGetString");
    const char* version = strings ? strings(0x1f02) : nullptr;
    const bool es3 = version && std::strstr(version, "OpenGL ES 3.");
    const unsigned unpack[] = {0x0cf2, 0x0cf3, 0x0cf4, 0x806e, 0x806d};
    int saved[5] = {}, pbo = 0;
    if (es3) {
        pbo = integer(0x88ef);
        if (buffer) buffer(0x88ec, 0);
        for (size_t i = 0; i < 5; ++i) { saved[i] = integer(unpack[i]); store(unpack[i], 0); }
    }
    store(0x0cf5, 1);
    bind2d(texture);
    upload(kTexture2D, 0, 0x1908, frame->width, frame->height, 0, 0x1908, 0x1401, rgba.data());
    bind2d(previous);
    store(0x0cf5, alignment);
    if (es3) {
        for (size_t i = 0; i < 5; ++i) store(unpack[i], saved[i]);
        if (buffer) buffer(0x88ec, static_cast<unsigned>(pbo));
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = textures.find(id);
        if (it != textures.end()) {
            it->second.timestamp = frame->timestamp;
            it->second.current = frame;
        }
    }
    static std::atomic<int> logs{0};
    if (logs.fetch_add(1) < 4) KLOG(kudroid::kInfo, "KuDroidMedia", "SurfaceTexture %d: uploaded %dx%d texture=%u pts=%lldns", id, frame->width, frame->height, texture, static_cast<long long>(frame->timestamp));
    return frame->timestamp;
}
