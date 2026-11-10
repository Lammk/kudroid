extern "C" void bionic_glMemoryBarrier(unsigned int barriers) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glMemoryBarrier");
    if (!f) { EGL_FORWARD_ERR("glMemoryBarrier", ""); return; }
    gpuLog("glMemoryBarrier(barriers=0x%x)", barriers);
    f(barriers);
}

extern "C" void bionic_glBindImageTexture(unsigned int unit, unsigned int texture, int level,
                                          unsigned int layered, int layer,
                                          unsigned int access, unsigned int format) {
    typedef void (*PFN)(unsigned int, unsigned int, int, unsigned char, int,
                        unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindImageTexture");
    if (!f) { EGL_FORWARD_ERR("glBindImageTexture", ""); return; }
    gpuLog("glBindImageTexture(unit=%u texture=%u level=%d layered=%u layer=%d)",
           unit, texture, level, layered, layer);
    f(unit, texture, level, static_cast<unsigned char>(layered), layer, access, format);
}

extern "C" void bionic_glFramebufferParameteri(unsigned int target, unsigned int pname, int param) {
    typedef void (*PFN)(unsigned int, unsigned int, int);
    auto f = (PFN)GL_FUNC_CACHED("glFramebufferParameteri");
    if (!f) { EGL_FORWARD_ERR("glFramebufferParameteri", ""); return; }
    gpuLog("glFramebufferParameteri(target=0x%x pname=0x%x param=%d)", target, pname, param);
    f(target, pname, param);
}

extern "C" unsigned int bionic_glCreateShader(unsigned int type) {
    typedef unsigned int (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glCreateShader");
    if (!f) { EGL_FORWARD_ERR("glCreateShader", ""); return 0; }
    gpuLog("glCreateShader(type=0x%x): calling ANGLE...", type);
    unsigned int s = f(type);
    gpuLog("glCreateShader -> %u", s);
    return s;
}

extern "C" void bionic_glShaderSource(unsigned int shader, int count, const char* const* string, const int* length) {
    g_shaderSources.fetch_add(1, std::memory_order_relaxed);
    typedef void (*PFN)(unsigned int, int, const char* const*, const int*);
    auto f = (PFN)GL_FUNC_CACHED("glShaderSource");
    if (!f) { EGL_FORWARD_ERR("glShaderSource", ""); return; }

    gpuLog("glShaderSource(shader=%u count=%d): marshalling guest memory to host buffer...", shader, count);
    if (!string || count <= 0) {
        f(shader, count, string, length);
        return;
    }

    // Safely copy the entire shader chain from the Guest memory (ELF/Stack) to the Host C++ heap
    std::vector<std::string> hostStrings;
    std::vector<const char*> hostPtrs;
    std::vector<int> hostLens;
    hostStrings.reserve(count);
    hostPtrs.reserve(count);
    hostLens.reserve(count);

    for (int i = 0; i < count; ++i) {
        if (string[i]) {
            int len = (length && length[i] >= 0) ? length[i] : static_cast<int>(strlen(string[i]));
            std::string src(string[i], len);
            hostStrings.push_back(std::move(src));
            hostLens.push_back(static_cast<int>(hostStrings.back().size()));
        } else {
            hostStrings.emplace_back("");
            hostLens.push_back(0);
        }
    }

    for (size_t i = 0; i < hostStrings.size(); ++i) {
        hostPtrs.push_back(hostStrings[i].c_str());
    }

    if (!hostStrings.empty()) {
        char snippet[64] = {0};
        strncpy(snippet, hostStrings[0].c_str(), 40);
        gpuLog("glShaderSource(shader=%u): marshalled len=%zu snippet='%s...'",
               shader, hostStrings[0].size(), snippet);
    }

    std::string combined;
    for (const auto& source : hostStrings) combined += source;
    if (es31_compat_enabled()) es31_compat_shader(shader, combined);
    const std::string translated = surface_texture::shader(std::move(combined));
    const char* source = translated.data();
    const int sourceLength = static_cast<int>(translated.size());
    f(shader, 1, &source, &sourceLength);
    gpuLog("glShaderSource(shader=%u) -> OK", shader);
}

static void log_gl_object_failure(unsigned int object, bool program) {
    static std::atomic<unsigned> budget{0};
    if (budget.fetch_add(1, std::memory_order_relaxed) >= 32) return;
    using GetIv = void (*)(unsigned int, unsigned int, int*);
    using GetLog = void (*)(unsigned int, int, int*, char*);
    auto get_iv = reinterpret_cast<GetIv>(program ? GL_FUNC_CACHED("glGetProgramiv")
                                                  : GL_FUNC_CACHED("glGetShaderiv"));
    auto get_log = reinterpret_cast<GetLog>(program ? GL_FUNC_CACHED("glGetProgramInfoLog")
                                                    : GL_FUNC_CACHED("glGetShaderInfoLog"));
    if (!get_iv || !get_log) return;
    int length = 0;
    get_iv(object, 0x8B84 /* GL_INFO_LOG_LENGTH */, &length);
    const int capacity = length > 1 ? (length < 4096 ? length : 4096) : 1;
    std::vector<char> info(static_cast<size_t>(capacity), '\0');
    int written = 0;
    get_log(object, capacity, &written, info.data());
    info.back() = '\0';
    ::kudroid::log::write(::kudroid::log::kError, "KuDroidGPU",
                          "GL %s %u failed: %s", program ? "program link" : "shader compilation",
                          object, info.data());
}

extern "C" void bionic_glCompileShader(unsigned int shader) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glCompileShader");
    if (!f) { EGL_FORWARD_ERR("glCompileShader", ""); return; }
    gpuLog("glCompileShader(shader=%u): calling ANGLE...", shader);
    f(shader);
    gpuLog("glCompileShader(shader=%u) submitted", shader);
}

extern "C" void bionic_glGetShaderiv(unsigned int shader, unsigned int pname, int* params) {
    typedef void (*PFN)(unsigned int, unsigned int, int*);
    auto f = (PFN)GL_FUNC_CACHED("glGetShaderiv");
    if (!f) { EGL_FORWARD_ERR("glGetShaderiv", ""); return; }
    f(shader, pname, params);
    if (params && pname == 0x8B81 /* GL_COMPILE_STATUS */ && !*params) {
        log_gl_object_failure(shader, false);
    }
    gpuLog("glGetShaderiv(shader=%u pname=0x%x param=%d)", shader, pname, params ? *params : 0);
}

extern "C" void bionic_glGetShaderInfoLog(unsigned int shader, int maxLength, int* length, char* infoLog) {
    typedef void (*PFN)(unsigned int, int, int*, char*);
    auto f = (PFN)GL_FUNC_CACHED("glGetShaderInfoLog");
    if (!f) { EGL_FORWARD_ERR("glGetShaderInfoLog", ""); return; }
    f(shader, maxLength, length, infoLog);
    gpuLog("glGetShaderInfoLog(shader=%u log='%s')", shader, infoLog ? infoLog : "");
}

extern "C" unsigned int bionic_glCreateProgram(void) {
    typedef unsigned int (*PFN)(void);
    auto f = (PFN)GL_FUNC_CACHED("glCreateProgram");
    if (!f) { EGL_FORWARD_ERR("glCreateProgram", ""); return 0; }
    gpuLog("glCreateProgram: calling ANGLE...");
    unsigned int p = f();
    gpuLog("glCreateProgram -> %u", p);
    return p;
}

extern "C" void bionic_glAttachShader(unsigned int program, unsigned int shader) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glAttachShader");
    if (!f) { EGL_FORWARD_ERR("glAttachShader", ""); return; }
    gpuLog("glAttachShader(prog=%u, shader=%u)", program, shader);
    f(program, shader);
}

extern "C" void bionic_glLinkProgram(unsigned int program) {
    g_programLinks.fetch_add(1, std::memory_order_relaxed);
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glLinkProgram");
    if (!f) { EGL_FORWARD_ERR("glLinkProgram", ""); return; }
    surface_texture::program_changed(program);
    gpuLog("glLinkProgram(program=%u): calling ANGLE...", program);
#if defined(__APPLE__)
    void* pool = objc_autoreleasePoolPush();
    f(program);
    objc_autoreleasePoolPop(pool);
#else
    f(program);
#endif
    gpuLog("glLinkProgram(program=%u) submitted", program);
}

extern "C" void bionic_glGetProgramiv(unsigned int program, unsigned int pname, int* params) {
    typedef void (*PFN)(unsigned int, unsigned int, int*);
    auto f = (PFN)GL_FUNC_CACHED("glGetProgramiv");
    if (!f) { EGL_FORWARD_ERR("glGetProgramiv", ""); return; }
    f(program, pname, params);
    if (params && pname == 0x8B82 /* GL_LINK_STATUS */ && !*params) {
        log_gl_object_failure(program, true);
    }
    gpuLog("glGetProgramiv(prog=%u pname=0x%x param=%d)", program, pname, params ? *params : 0);
}

namespace {
struct MaterialAudit {
    unsigned epoch = ~0u;
    bool pending = false;
    void* context = nullptr;
    unsigned selected = 0;
    std::unordered_map<void*, std::unordered_set<unsigned>> programs;
};
thread_local MaterialAudit materialAudit;

void select_material_audit(unsigned program) {
    materialAudit.pending = false;
    if (!program || g_materialAuditCount.load(std::memory_order_relaxed) >= 96) return;
    const unsigned epoch = g_materialAuditEpoch.load(std::memory_order_relaxed);
    if (materialAudit.epoch != epoch) {
        materialAudit.programs.clear();
        materialAudit.epoch = epoch;
    }
    using Current = void* (*)();
    static auto current = reinterpret_cast<Current>(get_egl_func("eglGetCurrentContext"));
    if (current) {
        materialAudit.context = current();
        materialAudit.selected = program;
        materialAudit.pending = !materialAudit.programs[materialAudit.context].count(program);
    }
}

// Query each program once at its first draw, after render state has been applied.
void audit_material_draw() {
    if (!materialAudit.pending) return;
    materialAudit.pending = false;
    materialAudit.programs[materialAudit.context].insert(materialAudit.selected);
    if (g_materialAuditCount.fetch_add(1, std::memory_order_relaxed) >= 96) return;
    using String = const char* (*)(unsigned);
    auto strings = reinterpret_cast<String>(GL_FUNC_CACHED("glGetString"));
    const char* version = strings ? strings(0x1f02 /* GL_VERSION */) : nullptr;
    if (!version || !std::strstr(version, "OpenGL ES 3.")) return;
    using Integer = void (*)(unsigned, int*);
    using Program = void (*)(unsigned, unsigned, int*);
    using Uniform = void (*)(unsigned, unsigned, int, int*, int*, unsigned*, char*);
    using Attachment = void (*)(unsigned, unsigned, unsigned, int*);
    using Enabled = unsigned char (*)(unsigned);
    auto integer = reinterpret_cast<Integer>(GL_FUNC_CACHED("glGetIntegerv"));
    auto program = reinterpret_cast<Program>(GL_FUNC_CACHED("glGetProgramiv"));
    auto uniform = reinterpret_cast<Uniform>(GL_FUNC_CACHED("glGetActiveUniform"));
    auto attachment = reinterpret_cast<Attachment>(GL_FUNC_CACHED("glGetFramebufferAttachmentParameteriv"));
    auto enabled = reinterpret_cast<Enabled>(GL_FUNC_CACHED("glIsEnabled"));
    if (!integer || !program || !uniform || !attachment || !enabled) return;
    int id = 0, framebuffer = 0, count = 0, maximum = 0;
    integer(0x8b8d /* GL_CURRENT_PROGRAM */, &id);
    if (!id) return;
    integer(0x8ca6 /* GL_DRAW_FRAMEBUFFER_BINDING */, &framebuffer);
    program(id, 0x8b86 /* GL_ACTIVE_UNIFORMS */, &count);
    program(id, 0x8b87 /* GL_ACTIVE_UNIFORM_MAX_LENGTH */, &maximum);
    int mask[4] = {}, source = 0, destination = 0;
    integer(0x0c23 /* GL_COLOR_WRITEMASK */, mask);
    integer(0x80c9 /* GL_BLEND_SRC_RGB */, &source);
    integer(0x80c8 /* GL_BLEND_DST_RGB */, &destination);
    std::string names;
    if (maximum > 0 && maximum <= 65536) {
        std::vector<char> name(static_cast<size_t>(maximum), '\0');
        for (int i = 0; i < std::min(count, 16); ++i) {
            int length = 0, size = 0;
            unsigned type = 0;
            uniform(id, i, maximum, &length, &size, &type, name.data());
            name.back() = '\0';
            if (!names.empty()) names += ',';
            names.append(name.data(), std::min(std::strlen(name.data()), size_t{64}));
            if (names.size() >= 384) break;
        }
    }
    std::string targets;
    if (framebuffer) {
        int buffers = 0, colors = 0;
        integer(0x8824 /* GL_MAX_DRAW_BUFFERS */, &buffers);
        integer(0x8cdf /* GL_MAX_COLOR_ATTACHMENTS */, &colors);
        for (int i = 0; i < std::min(buffers, 4); ++i) {
            int buffer = 0, object = 0, type = 0, bits[4] = {};
            integer(0x8825u + i /* GL_DRAW_BUFFER0 */, &buffer);
            if (buffer < 0x8ce0 || buffer >= 0x8ce0 + colors) continue;
            attachment(0x8ca9 /* GL_DRAW_FRAMEBUFFER */, buffer, 0x8cd0, &object);
            if (!object) continue;
            attachment(0x8ca9, buffer, 0x8211 /* GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE */, &type);
            for (int c = 0; c < 4; ++c) attachment(0x8ca9, buffer, 0x8212u + c, &bits[c]);
            char value[112];
            std::snprintf(value, sizeof(value), " out%d(type=0x%x bits=%d/%d/%d/%d)",
                          i, type, bits[0], bits[1], bits[2], bits[3]);
            targets += value;
        }
    }
    KLOG(kInfo, "KuDroidGPU", "GL material program=%d blend=%u factors=0x%x/0x%x mask=%d%d%d%d fbo=%d uniforms=%d [%s]%s",
         id, static_cast<unsigned>(enabled(0x0be2 /* GL_BLEND */)),
         source, destination, mask[0], mask[1], mask[2], mask[3], framebuffer,
         count, names.c_str(), targets.c_str());
}
} // namespace

extern "C" void bionic_glUseProgram(unsigned int program) {
    g_programBinds.fetch_add(1, std::memory_order_relaxed);
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glUseProgram");
    if (!f) { EGL_FORWARD_ERR("glUseProgram", ""); return; }
    static std::mutex traceMutex;
    static std::unordered_map<uintptr_t, unsigned> traceCounts;
    static std::atomic<unsigned> traceCalls{0};
    const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    bool trace = false;
    if (traceCalls.load(std::memory_order_relaxed) < 128 &&
        traceCalls.fetch_add(1, std::memory_order_relaxed) < 128) {
        std::lock_guard<std::mutex> lock(traceMutex);
        auto it = traceCounts.find(caller);
        if (it != traceCounts.end()) {
            if (it->second < 4) { ++it->second; trace = true; }
        } else if (traceCounts.size() < 32) {
            traceCounts.emplace(caller, 1);
            trace = true;
        }
    }
    if (trace) KLOG(kInfo, "KuDroidGPU", "GL program binding: program=%u caller=%p",
                    program, reinterpret_cast<void*>(caller));
    gpuLog("glUseProgram(prog=%u)", program);
    f(program);
    surface_texture::use_program(program);
    select_material_audit(program);
}

extern "C" int bionic_glGetAttribLocation(unsigned int program, const char* name) {
    typedef int (*PFN)(unsigned int, const char*);
    auto f = (PFN)GL_FUNC_CACHED("glGetAttribLocation");
    if (!f) { EGL_FORWARD_ERR("glGetAttribLocation", ""); return -1; }
    int loc = f(program, name);
    gpuLog("glGetAttribLocation(prog=%u name='%s') -> %d", program, name ? name : "", loc);
    return loc;
}

extern "C" void bionic_glEnableVertexAttribArray(unsigned int index) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glEnableVertexAttribArray");
    if (!f) { EGL_FORWARD_ERR("glEnableVertexAttribArray", ""); return; }
    f(index);
}

extern "C" void bionic_glVertexAttribPointer(unsigned int index, int size, unsigned int type, unsigned int normalized, int stride, const void* pointer) {
    g_vertexSetups.fetch_add(1, std::memory_order_relaxed);
    typedef void (*PFN)(unsigned int, int, unsigned int, unsigned char, int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glVertexAttribPointer");
    if (!f) { EGL_FORWARD_ERR("glVertexAttribPointer", ""); return; }
    // Not logged: per-attribute per-draw state, sampled or not it was 2144 of
    // 3856 lines in one run's crash buffer and pushed real faults out of it.
    // Only KUDROID_GPU_LOG_ALL=1 (debug session) records it.
    if (gpu_log_all()) {
        gpuLog("glVertexAttribPointer(index=%u size=%d stride=%d ptr=%p)", index, size, stride, pointer);
    }
    f(index, size, type, static_cast<unsigned char>(normalized), stride, pointer);
}

extern "C" void bionic_glClearColor(float red, float green, float blue, float alpha) {
    typedef void (*PFN)(float, float, float, float);
    auto f = (PFN)GL_FUNC_CACHED("glClearColor");
    if (!f) { EGL_FORWARD_ERR("glClearColor", ""); return; }
    f(red, green, blue, alpha);
}

extern "C" void bionic_glClear(unsigned int mask) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glClear");
    if (!f) { EGL_FORWARD_ERR("glClear", ""); return; }
    g_clearCalls.fetch_add(1, std::memory_order_relaxed);
    f(mask);
}

extern "C" void bionic_glDrawArrays(unsigned int mode, int first, int count) {
    g_drawAttempts.fetch_add(1, std::memory_order_relaxed);
    surface_texture::DrawScope external;
    typedef void (*PFN)(unsigned int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glDrawArrays");
    if (!f) {
        static std::atomic<bool> reported{false};
        if (!reported.exchange(true, std::memory_order_relaxed))
            KLOG(kError, "KuDroidGPU", "GL draw backend unavailable: glDrawArrays");
        return;
    }
    g_drawCalls.fetch_add(1, std::memory_order_relaxed);
    audit_material_draw();
    gpuLog("glDrawArrays(mode=0x%x first=%d count=%d): calling ANGLE...", mode, first, count);
#if defined(__APPLE__)
    void* pool = objc_autoreleasePoolPush();
    f(mode, first, count);
    objc_autoreleasePoolPop(pool);
#else
    f(mode, first, count);
#endif
    gpuLog("glDrawArrays -> OK");
}

extern "C" void bionic_glViewport(int x, int y, int width, int height) {
    typedef void (*PFN)(int, int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glViewport");
    if (!f) { EGL_FORWARD_ERR("glViewport", ""); return; }
    gpuLog("glViewport(%d, %d, %d, %d)", x, y, width, height);
    f(x, y, width, height);
}

extern "C" void bionic_glGenBuffers(int n, unsigned int* buffers) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenBuffers");
    if (!f) { EGL_FORWARD_ERR("glGenBuffers", ""); return; }
    f(n, buffers);
    gpuLog("glGenBuffers(n=%d) -> buf=%u", n, (buffers && n > 0) ? buffers[0] : 0);
}

extern "C" void bionic_glBindBuffer(unsigned int target, unsigned int buffer) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindBuffer");
    if (!f) { EGL_FORWARD_ERR("glBindBuffer", ""); return; }
    f(target, buffer);
}

extern "C" void bionic_glBufferData(unsigned int target, long size, const void* data, unsigned int usage) {
    g_bufferUploads.fetch_add(1, std::memory_order_relaxed);
    typedef void (*PFN)(unsigned int, long, const void*, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBufferData");
    if (!f) { EGL_FORWARD_ERR("glBufferData", ""); return; }
    gpuLog("glBufferData(target=0x%x size=%ld usage=0x%x)", target, size, usage);
    trace_buffer_payload("glBufferData", target, 0, size, data, __builtin_return_address(0));
    f(target, size, data, usage);
}

// ── Textures ────────────────────────────────────────────────────────────────
extern "C" void bionic_glGenTextures(int n, unsigned int* textures) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenTextures");
    if (f) f(n, textures);
}

extern "C" void bionic_glBindTexture(unsigned int target, unsigned int texture) {
    if (surface_texture::bind(target, texture)) return;
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindTexture");
    if (f) f(target, texture);
}
extern "C" void bionic_glDeleteTextures(int n, const unsigned int* textures) {
    surface_texture::deleted_textures(n, textures);
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteTextures");
    if (f) f(n, textures);
}
extern "C" void bionic_glTexImage2D(unsigned int target, int level, int internalformat, int width, int height, int border, unsigned int format, unsigned int type, const void* pixels) {
    g_textureUploads.fetch_add(1, std::memory_order_relaxed);
    typedef void (*PFN)(unsigned int, int, int, int, int, int, unsigned int, unsigned int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glTexImage2D");
    if (gpu_diag_enabled()) {
        gpu_diag_texture_upload("glTexImage2D", target, level, width, height, format, type, pixels);
    }
    if (f) f(target, level, internalformat, width, height, border, format, type, pixels);
}
extern "C" void bionic_glTexSubImage2D(unsigned int target, int level, int xoffset, int yoffset, int width, int height, unsigned int format, unsigned int type, const void* pixels) {
    g_textureUploads.fetch_add(1, std::memory_order_relaxed);
    typedef void (*PFN)(unsigned int, int, int, int, int, int, unsigned int, unsigned int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glTexSubImage2D");
    if (gpu_diag_enabled()) {
        gpu_diag_texture_upload("glTexSubImage2D", target, level, width, height, format, type, pixels);
    }
    if (f) f(target, level, xoffset, yoffset, width, height, format, type, pixels);
}
extern "C" void bionic_glTexParameteri(unsigned int target, unsigned int pname, int param) {
    typedef void (*PFN)(unsigned int, unsigned int, int);
    auto f = (PFN)GL_FUNC_CACHED("glTexParameteri");
    if (f) f(surface_texture::target(target), pname, param);
    surface_texture::restore_binding(target);
}
extern "C" void bionic_glTexParameterf(unsigned int target, unsigned int pname, float param) {
    typedef void (*PFN)(unsigned int, unsigned int, float);
    auto f = (PFN)GL_FUNC_CACHED("glTexParameterf");
    if (f) f(surface_texture::target(target), pname, param);
    surface_texture::restore_binding(target);
}
extern "C" void bionic_glTexParameteriv(unsigned int target, unsigned int pname, const int* params) {
    typedef void (*PFN)(unsigned int, unsigned int, const int*);
    auto f = (PFN)GL_FUNC_CACHED("glTexParameteriv");
    if (f) f(surface_texture::target(target), pname, params);
    surface_texture::restore_binding(target);
}
extern "C" void bionic_glTexParameterfv(unsigned int target, unsigned int pname, const float* params) {
    typedef void (*PFN)(unsigned int, unsigned int, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glTexParameterfv");
    if (f) f(surface_texture::target(target), pname, params);
    surface_texture::restore_binding(target);
}
extern "C" void bionic_glActiveTexture(unsigned int texture) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glActiveTexture");
    if (f) f(texture);
}
extern "C" void bionic_glGenerateMipmap(unsigned int target) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glGenerateMipmap");
    if (f) f(target);
}
extern "C" void bionic_glPixelStorei(unsigned int pname, int param) {
    typedef void (*PFN)(unsigned int, int);
    auto f = (PFN)GL_FUNC_CACHED("glPixelStorei");
    if (f) f(pname, param);
}
extern "C" void bionic_glCompressedTexImage2D(unsigned int target, int level, unsigned int internalformat, int width, int height, int border, int imageSize, const void* data) {
    g_textureUploads.fetch_add(1, std::memory_order_relaxed);
    typedef void (*PFN)(unsigned int, int, unsigned int, int, int, int, int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glCompressedTexImage2D");
    if (f) f(target, level, internalformat, width, height, border, imageSize, data);
}
extern "C" void bionic_glCompressedTexSubImage2D(unsigned int target, int level, int xoffset, int yoffset, int width, int height, unsigned int format, int imageSize, const void* data) {
    g_textureUploads.fetch_add(1, std::memory_order_relaxed);
    typedef void (*PFN)(unsigned int, int, int, int, int, int, unsigned int, int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glCompressedTexSubImage2D");
    if (f) f(target, level, xoffset, yoffset, width, height, format, imageSize, data);
}

// ── Buffers & Elements ──────────────────────────────────────────────────────
extern "C" void bionic_glDeleteBuffers(int n, const unsigned int* buffers) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteBuffers");
    if (f) f(n, buffers);
}
extern "C" void bionic_glBufferSubData(unsigned int target, long offset, long size, const void* data) {
    g_bufferUploads.fetch_add(1, std::memory_order_relaxed);
    typedef void (*PFN)(unsigned int, long, long, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glBufferSubData");
    if (f) {
        trace_buffer_payload("glBufferSubData", target, offset, size, data, __builtin_return_address(0));
        f(target, offset, size, data);
    }
}
extern "C" void bionic_glDrawElements(unsigned int mode, int count, unsigned int type, const void* indices) {
    g_drawAttempts.fetch_add(1, std::memory_order_relaxed);
    surface_texture::DrawScope external;
    typedef void (*PFN)(unsigned int, int, unsigned int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glDrawElements");
    if (!f) {
        static std::atomic<bool> reported{false};
        if (!reported.exchange(true, std::memory_order_relaxed))
            KLOG(kError, "KuDroidGPU", "GL draw backend unavailable: glDrawElements");
        return;
    }
    g_drawCalls.fetch_add(1, std::memory_order_relaxed);
    audit_material_draw();
#if defined(__APPLE__)
    void* pool = objc_autoreleasePoolPush();
    f(mode, count, type, indices);
    objc_autoreleasePoolPop(pool);
#else
    f(mode, count, type, indices);
#endif
}
extern "C" void bionic_glDisableVertexAttribArray(unsigned int index) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glDisableVertexAttribArray");
    if (f) f(index);
}

// ── Vertex Array Objects (VAO) ──────────────────────────────────────────────
extern "C" void bionic_glGenVertexArrays(int n, unsigned int* arrays) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenVertexArrays");
    if (f) f(n, arrays);
}
extern "C" void bionic_glGenVertexArraysOES(int n, unsigned int* arrays) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenVertexArraysOES");
    if (f) f(n, arrays);
}
extern "C" void bionic_glBindVertexArray(unsigned int array) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindVertexArray");
    if (f) f(array);
}
extern "C" void bionic_glBindVertexArrayOES(unsigned int array) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindVertexArrayOES");
    if (f) f(array);
}
extern "C" void bionic_glDeleteVertexArrays(int n, const unsigned int* arrays) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteVertexArrays");
    if (f) f(n, arrays);
}
extern "C" void bionic_glDeleteVertexArraysOES(int n, const unsigned int* arrays) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteVertexArraysOES");
    if (f) f(n, arrays);
}

// ── Framebuffers & Renderbuffers ────────────────────────────────────────────
extern "C" void bionic_glGenFramebuffers(int n, unsigned int* framebuffers) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenFramebuffers");
    if (f) f(n, framebuffers);
}
extern "C" void bionic_glBindFramebuffer(unsigned int target, unsigned int framebuffer) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindFramebuffer");
    if (f) f(target, framebuffer);
}
extern "C" void bionic_glDeleteFramebuffers(int n, const unsigned int* framebuffers) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteFramebuffers");
    if (f) f(n, framebuffers);
}
extern "C" void bionic_glFramebufferTexture2D(unsigned int target, unsigned int attachment, unsigned int textarget, unsigned int texture, int level) {
    typedef void (*PFN)(unsigned int, unsigned int, unsigned int, unsigned int, int);
    auto f = (PFN)GL_FUNC_CACHED("glFramebufferTexture2D");
    // External images use RGBA 2D storage in the host context.
    if (textarget == 0x8d65 /* GL_TEXTURE_EXTERNAL_OES */) textarget = 0x0de1;
    if (f) f(target, attachment, textarget, texture, level);
}
extern "C" unsigned int bionic_glCheckFramebufferStatus(unsigned int target) {
    typedef unsigned int (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glCheckFramebufferStatus");
    if (f) return f(target);
    return 0;
}
extern "C" void bionic_glGenRenderbuffers(int n, unsigned int* renderbuffers) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenRenderbuffers");
    if (f) f(n, renderbuffers);
}
extern "C" void bionic_glBindRenderbuffer(unsigned int target, unsigned int renderbuffer) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindRenderbuffer");
    if (f) f(target, renderbuffer);
}
extern "C" void bionic_glDeleteRenderbuffers(int n, const unsigned int* renderbuffers) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteRenderbuffers");
    if (f) f(n, renderbuffers);
}
extern "C" void bionic_glRenderbufferStorage(unsigned int target, unsigned int internalformat, int width, int height) {
    typedef void (*PFN)(unsigned int, unsigned int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glRenderbufferStorage");
    if (f) f(target, internalformat, width, height);
}
extern "C" void bionic_glFramebufferRenderbuffer(unsigned int target, unsigned int attachment, unsigned int renderbuffertarget, unsigned int renderbuffer) {
    typedef void (*PFN)(unsigned int, unsigned int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glFramebufferRenderbuffer");
    if (f) f(target, attachment, renderbuffertarget, renderbuffer);
}

// ── Uniforms ────────────────────────────────────────────────────────────────
extern "C" int bionic_glGetUniformLocation(unsigned int program, const char* name) {
    typedef int (*PFN)(unsigned int, const char*);
    auto f = (PFN)GL_FUNC_CACHED("glGetUniformLocation");
    if (f) {
        const int location = f(program, name);
        if (location >= 0 || !name) return location;
        return f(program, surface_texture::uniform_name(name).c_str());
    }
    return -1;
}
extern "C" void bionic_glUniform1f(int location, float v0) {
    typedef void (*PFN)(int, float);
    auto f = (PFN)GL_FUNC_CACHED("glUniform1f");
    if (f) f(location, v0);
}
extern "C" void bionic_glUniform2f(int location, float v0, float v1) {
    typedef void (*PFN)(int, float, float);
    auto f = (PFN)GL_FUNC_CACHED("glUniform2f");
    if (f) f(location, v0, v1);
}
extern "C" void bionic_glUniform3f(int location, float v0, float v1, float v2) {
    typedef void (*PFN)(int, float, float, float);
    auto f = (PFN)GL_FUNC_CACHED("glUniform3f");
    if (f) f(location, v0, v1, v2);
}
extern "C" void bionic_glUniform4f(int location, float v0, float v1, float v2, float v3) {
    typedef void (*PFN)(int, float, float, float, float);
    auto f = (PFN)GL_FUNC_CACHED("glUniform4f");
    if (f) f(location, v0, v1, v2, v3);
}
extern "C" void bionic_glUniform1i(int location, int v0) {
    typedef void (*PFN)(int, int);
    auto f = (PFN)GL_FUNC_CACHED("glUniform1i");
    if (f) f(location, v0);
}
extern "C" void bionic_glUniform2i(int location, int v0, int v1) {
    typedef void (*PFN)(int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glUniform2i");
    if (f) f(location, v0, v1);
}
extern "C" void bionic_glUniform3i(int location, int v0, int v1, int v2) {
    typedef void (*PFN)(int, int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glUniform3i");
    if (f) f(location, v0, v1, v2);
}
extern "C" void bionic_glUniform4i(int location, int v0, int v1, int v2, int v3) {
    typedef void (*PFN)(int, int, int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glUniform4i");
    if (f) f(location, v0, v1, v2, v3);
}
extern "C" void bionic_glUniform1fv(int location, int count, const float* value) {
    typedef void (*PFN)(int, int, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform1fv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniform2fv(int location, int count, const float* value) {
    typedef void (*PFN)(int, int, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform2fv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniform3fv(int location, int count, const float* value) {
    typedef void (*PFN)(int, int, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform3fv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniform4fv(int location, int count, const float* value) {
    typedef void (*PFN)(int, int, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform4fv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniform1iv(int location, int count, const int* value) {
    typedef void (*PFN)(int, int, const int*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform1iv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniform2iv(int location, int count, const int* value) {
    typedef void (*PFN)(int, int, const int*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform2iv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniform3iv(int location, int count, const int* value) {
    typedef void (*PFN)(int, int, const int*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform3iv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniform4iv(int location, int count, const int* value) {
    typedef void (*PFN)(int, int, const int*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform4iv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniformMatrix2fv(int location, int count, unsigned int transpose, const float* value) {
    typedef void (*PFN)(int, int, unsigned char, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glUniformMatrix2fv");
    if (f) f(location, count, static_cast<unsigned char>(transpose), value);
}
extern "C" void bionic_glUniformMatrix3fv(int location, int count, unsigned int transpose, const float* value) {
    typedef void (*PFN)(int, int, unsigned char, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glUniformMatrix3fv");
    if (f) f(location, count, static_cast<unsigned char>(transpose), value);
}
extern "C" void bionic_glUniformMatrix4fv(int location, int count, unsigned int transpose, const float* value) {
    typedef void (*PFN)(int, int, unsigned char, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glUniformMatrix4fv");
    if (f) f(location, count, static_cast<unsigned char>(transpose), value);
}

// ── Render States & Capabilities ────────────────────────────────────────────
extern "C" void bionic_glEnable(unsigned int cap) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glEnable");
    if (f) f(cap);
}
extern "C" void bionic_glDisable(unsigned int cap) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glDisable");
    if (f) f(cap);
}
extern "C" unsigned char bionic_glIsEnabled(unsigned int cap) {
    typedef unsigned char (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glIsEnabled");
    if (f) return f(cap);
    return 0;
}
extern "C" void bionic_glBlendFunc(unsigned int sfactor, unsigned int dfactor) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBlendFunc");
    if (f) f(sfactor, dfactor);
}
extern "C" void bionic_glBlendFuncSeparate(unsigned int srcRGB, unsigned int dstRGB, unsigned int srcAlpha, unsigned int dstAlpha) {
    typedef void (*PFN)(unsigned int, unsigned int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBlendFuncSeparate");
    if (f) f(srcRGB, dstRGB, srcAlpha, dstAlpha);
}
extern "C" void bionic_glBlendEquation(unsigned int mode) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBlendEquation");
    if (f) f(mode);
}
extern "C" void bionic_glBlendEquationSeparate(unsigned int modeRGB, unsigned int modeAlpha) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBlendEquationSeparate");
    if (f) f(modeRGB, modeAlpha);
}
extern "C" void bionic_glDepthFunc(unsigned int func) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glDepthFunc");
    if (f) f(func);
}
extern "C" void bionic_glDepthMask(unsigned int flag) {
    typedef void (*PFN)(unsigned char);
    auto f = (PFN)GL_FUNC_CACHED("glDepthMask");
    if (f) f(static_cast<unsigned char>(flag));
}
extern "C" void bionic_glCullFace(unsigned int mode) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glCullFace");
    if (f) f(mode);
}
extern "C" void bionic_glFrontFace(unsigned int mode) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glFrontFace");
    if (f) f(mode);
}
extern "C" void bionic_glScissor(int x, int y, int width, int height) {
    typedef void (*PFN)(int, int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glScissor");
    if (f) f(x, y, width, height);
}
extern "C" void bionic_glColorMask(unsigned int red, unsigned int green, unsigned int blue, unsigned int alpha) {
    typedef void (*PFN)(unsigned char, unsigned char, unsigned char, unsigned char);
    auto f = (PFN)GL_FUNC_CACHED("glColorMask");
    if (f) f(static_cast<unsigned char>(red), static_cast<unsigned char>(green),
             static_cast<unsigned char>(blue), static_cast<unsigned char>(alpha));
}
extern "C" void bionic_glFlush(void) {
    typedef void (*PFN)(void);
    auto f = (PFN)GL_FUNC_CACHED("glFlush");
    if (f) f();
}
extern "C" void bionic_glFinish(void) {
    typedef void (*PFN)(void);
    auto f = (PFN)GL_FUNC_CACHED("glFinish");
    if (f) f();
}
extern "C" void bionic_glGetIntegerv(unsigned int pname, int* params) {
    if (surface_texture::binding(pname, params)) return;
    typedef void (*PFN)(unsigned int, int*);
    auto f = (PFN)GL_FUNC_CACHED("glGetIntegerv");
    if (f && params && pname == 0x821D /* GL_NUM_EXTENSIONS */) {
        // Read into a local so a rejected query never walks the caller's stale value.
        int total = 0;
        f(pname, &total);
        typedef const char* (*Stringi)(unsigned int, unsigned int);
        auto stringi = (Stringi)GL_FUNC_CACHED("glGetStringi");
        int hidden = 0;
        for (int i = 0; stringi && i < total; ++i)
            hidden += gl_extension_hidden(stringi(0x1F03, static_cast<unsigned>(i)));
        if (total > 0) *params = total - hidden;
        return;
    }
    if (f) f(pname, params);
    if (f && params && pname == 0x821C /* GL_MINOR_VERSION */ && es31_compat_enabled() && *params == 0)
        *params = 1;
    if (f && params && pname == 0x87ff) {
        int count = 0;
        f(0x87fe, &count);
        for (int i = 0; i < count; ++i) params[i] ^= 0x4b440000;
    }
}

extern "C" void bionic_glGetActiveUniform(unsigned program, unsigned index, int capacity,
                                          int* length, int* size, unsigned* type, char* name) {
    using Get = void (*)(unsigned, unsigned, int, int*, int*, unsigned*, char*);
    using Iv = void (*)(unsigned, unsigned, int*);
    auto f = reinterpret_cast<Get>(GL_FUNC_CACHED("glGetActiveUniform"));
    auto iv = reinterpret_cast<Iv>(GL_FUNC_CACHED("glGetProgramiv"));
    if (!f || !iv) return;
    if (capacity < 0 || !size || !type || (capacity > 0 && !name)) {
        f(program, index, capacity, length, size, type, name);
        return;
    }
    int maximum = 0;
    iv(program, 0x8b87, &maximum);
    if (maximum <= 0 || maximum > (1 << 20)) { f(program, index, capacity, length, size, type, name); return; }
    std::vector<char> full(static_cast<size_t>(maximum), '\0');
    int written = 0;
    f(program, index, maximum, &written, size, type, full.data());
    surface_texture::active_uniform(maximum, &written, type, full.data());
    const int copied = capacity > 0 ? std::min(written, capacity - 1) : 0;
    if (capacity > 0) {
        std::memcpy(name, full.data(), static_cast<size_t>(copied));
        name[copied] = '\0';
    }
    if (length) *length = copied;
}

// Binary formats identify the shader translation ABI as well as the host driver.
extern "C" void bionic_glProgramBinary(unsigned program, unsigned format, const void* binary, int length) {
    using Fn = void (*)(unsigned, unsigned, const void*, int);
    surface_texture::program_changed(program);
    surface_texture::binary_loaded();
    if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glProgramBinary")))
        f(program, format ^ 0x4b440000u, binary, length);
}
extern "C" void bionic_glProgramBinaryOES(unsigned program, unsigned format, const void* binary, int length) {
    using Fn = void (*)(unsigned, unsigned, const void*, int);
    surface_texture::program_changed(program);
    surface_texture::binary_loaded();
    if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glProgramBinaryOES")))
        f(program, format ^ 0x4b440000u, binary, length);
}
extern "C" void bionic_glGetProgramBinary(unsigned program, int capacity, int* length, unsigned* format, void* binary) {
    using Fn = void (*)(unsigned, int, int*, unsigned*, void*);
    if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glGetProgramBinary"))) {
        unsigned native = 0;
        f(program, capacity, length, &native, binary);
        if (format) *format = native ^ 0x4b440000u;
    }
}
extern "C" void bionic_glGetProgramBinaryOES(unsigned program, int capacity, int* length, unsigned* format, void* binary) {
    using Fn = void (*)(unsigned, int, int*, unsigned*, void*);
    if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glGetProgramBinaryOES"))) {
        unsigned native = 0;
        f(program, capacity, length, &native, binary);
        if (format) *format = native ^ 0x4b440000u;
    }
}
extern "C" void bionic_glGetFloatv(unsigned int pname, float* params) {
    int texture = 0;
    if (params && surface_texture::binding(pname, &texture)) { *params = static_cast<float>(texture); return; }
    typedef void (*PFN)(unsigned int, float*);
    auto f = (PFN)GL_FUNC_CACHED("glGetFloatv");
    if (f) f(pname, params);
}
extern "C" void bionic_glGetBooleanv(unsigned int pname, unsigned char* params) {
    int texture = 0;
    if (params && surface_texture::binding(pname, &texture)) { *params = texture != 0; return; }
    typedef void (*PFN)(unsigned int, unsigned char*);
    auto f = (PFN)GL_FUNC_CACHED("glGetBooleanv");
    if (f) f(pname, params);
}
extern "C" const char* bionic_glGetString(unsigned int name) {
    typedef const char* (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glGetString");
    const char* s = f ? f(name) : nullptr;
    if (es31_compat_enabled() && (name == 0x1F02 || name == 0x8B8C)) return kudroid::gles31_advertise_version(s, name);
    return name == 0x1F03 /* GL_EXTENSIONS */ ? gl_context_extensions(s) : s;
}
// Indexed extension queries skip hidden entries so they match the filtered string.
extern "C" const char* bionic_glGetStringi(unsigned int name, unsigned int index) {
    typedef const char* (*PFN)(unsigned int, unsigned int);
    typedef void (*Count)(unsigned int, int*);
    auto f = (PFN)GL_FUNC_CACHED("glGetStringi");
    if (!f) return nullptr;
    if (name != 0x1F03 /* GL_EXTENSIONS */) return f(name, index);
    auto count = (Count)GL_FUNC_CACHED("glGetIntegerv");
    int total = 0;
    if (count) count(0x821D /* GL_NUM_EXTENSIONS */, &total);
    unsigned visible = 0;
    for (int i = 0; i < total; ++i) {
        const char* s = f(name, static_cast<unsigned>(i));
        if (gl_extension_hidden(s)) continue;
        if (visible++ == index) return s;
    }
    // Out of range: let the backend raise GL_INVALID_VALUE.
    return f(name, static_cast<unsigned>(total > 0 ? total : 0));
}
extern "C" unsigned int bionic_glGetError(void) {
    typedef unsigned int (*PFN)(void);
    auto f = (PFN)GL_FUNC_CACHED("glGetError");
    if (f) {
        const unsigned error = f();
        if (error) {
            // Keep later failures visible after capability probing.
            static std::mutex mutex;
            static std::unordered_map<uintptr_t, unsigned> counts;
            const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
            unsigned count = 0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (counts.size() < 64 || counts.count(caller)) count = ++counts[caller];
            }
            if (count && (count <= 4 || count % 120 == 0))
                KLOG(kError, "KuDroidGPU", "GL error reported to guest: 0x%x caller=%p occurrence=%u",
                     error, reinterpret_cast<void*>(caller), count);
        }
        return error;
    }
    return 0;
}
extern "C" void bionic_glReadPixels(int x, int y, int width, int height, unsigned int format, unsigned int type, void* pixels) {
    typedef void (*PFN)(int, int, int, int, unsigned int, unsigned int, void*);
    auto f = (PFN)GL_FUNC_CACHED("glReadPixels");
    if (f) f(x, y, width, height, format, type, pixels);
}
extern "C" void bionic_glDeleteShader(unsigned int shader) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteShader");
    if (f) f(shader);
}
extern "C" void bionic_glDeleteProgram(unsigned int program) {
    surface_texture::program_changed(program);
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteProgram");
    if (f) f(program);
}
extern "C" void bionic_glDetachShader(unsigned int program, unsigned int shader) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glDetachShader");
    if (f) f(program, shader);
}

// Preserve extension entry points: their validation differs from core GLES.
#define EXTERNAL_DRAW(name, parameters, arguments) \
    extern "C" void bionic_##name parameters { \
        g_drawAttempts.fetch_add(1, std::memory_order_relaxed); \
        surface_texture::DrawScope external; \
        using Fn = void (*) parameters; \
        auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED(#name)); \
        if (f) { g_drawCalls.fetch_add(1, std::memory_order_relaxed); audit_material_draw(); f arguments; } \
        else { \
            static std::atomic<bool> reported{false}; \
            if (!reported.exchange(true, std::memory_order_relaxed)) \
                KLOG(kError, "KuDroidGPU", "GL draw backend unavailable: %s", #name); \
        } \
    }
EXTERNAL_DRAW(glDrawArraysInstanced, (unsigned mode, int first, int count, int instances), (mode, first, count, instances))
EXTERNAL_DRAW(glDrawArraysInstancedANGLE, (unsigned mode, int first, int count, int instances), (mode, first, count, instances))
EXTERNAL_DRAW(glDrawArraysInstancedEXT, (unsigned mode, int first, int count, int instances), (mode, first, count, instances))
EXTERNAL_DRAW(glDrawElementsInstanced, (unsigned mode, int count, unsigned type, const void* indices, int instances), (mode, count, type, indices, instances))
EXTERNAL_DRAW(glDrawElementsInstancedANGLE, (unsigned mode, int count, unsigned type, const void* indices, int instances), (mode, count, type, indices, instances))
EXTERNAL_DRAW(glDrawElementsInstancedEXT, (unsigned mode, int count, unsigned type, const void* indices, int instances), (mode, count, type, indices, instances))
EXTERNAL_DRAW(glDrawElementsBaseVertex, (unsigned mode, int count, unsigned type, const void* indices, int base), (mode, count, type, indices, base))
EXTERNAL_DRAW(glDrawElementsBaseVertexOES, (unsigned mode, int count, unsigned type, const void* indices, int base), (mode, count, type, indices, base))
EXTERNAL_DRAW(glDrawElementsBaseVertexEXT, (unsigned mode, int count, unsigned type, const void* indices, int base), (mode, count, type, indices, base))
EXTERNAL_DRAW(glDrawRangeElements, (unsigned mode, unsigned start, unsigned end, int count, unsigned type, const void* indices), (mode, start, end, count, type, indices))
EXTERNAL_DRAW(glDrawRangeElementsBaseVertex, (unsigned mode, unsigned start, unsigned end, int count, unsigned type, const void* indices, int base), (mode, start, end, count, type, indices, base))
EXTERNAL_DRAW(glDrawRangeElementsBaseVertexOES, (unsigned mode, unsigned start, unsigned end, int count, unsigned type, const void* indices, int base), (mode, start, end, count, type, indices, base))
EXTERNAL_DRAW(glDrawRangeElementsBaseVertexEXT, (unsigned mode, unsigned start, unsigned end, int count, unsigned type, const void* indices, int base), (mode, start, end, count, type, indices, base))
EXTERNAL_DRAW(glDrawElementsInstancedBaseVertex, (unsigned mode, int count, unsigned type, const void* indices, int instances, int base), (mode, count, type, indices, instances, base))
EXTERNAL_DRAW(glDrawElementsInstancedBaseVertexOES, (unsigned mode, int count, unsigned type, const void* indices, int instances, int base), (mode, count, type, indices, instances, base))
EXTERNAL_DRAW(glDrawElementsInstancedBaseVertexEXT, (unsigned mode, int count, unsigned type, const void* indices, int instances, int base), (mode, count, type, indices, instances, base))
EXTERNAL_DRAW(glDrawElementsInstancedBaseInstanceEXT, (unsigned mode, int count, unsigned type, const void* indices, int instances, unsigned base), (mode, count, type, indices, instances, base))
EXTERNAL_DRAW(glDrawArraysInstancedBaseInstanceEXT, (unsigned mode, int first, int count, int instances, unsigned base), (mode, first, count, instances, base))
EXTERNAL_DRAW(glDrawArraysInstancedBaseInstanceANGLE, (unsigned mode, int first, int count, int instances, unsigned base), (mode, first, count, instances, base))
EXTERNAL_DRAW(glDrawElementsInstancedBaseVertexBaseInstanceEXT, (unsigned mode, int count, unsigned type, const void* indices, int instances, int baseVertex, unsigned baseInstance), (mode, count, type, indices, instances, baseVertex, baseInstance))
EXTERNAL_DRAW(glDrawElementsInstancedBaseVertexBaseInstanceANGLE, (unsigned mode, int count, unsigned type, const void* indices, int instances, int baseVertex, unsigned baseInstance), (mode, count, type, indices, instances, baseVertex, baseInstance))
EXTERNAL_DRAW(glDrawArraysIndirect, (unsigned mode, const void* indirect), (mode, indirect))
EXTERNAL_DRAW(glDrawElementsIndirect, (unsigned mode, unsigned type, const void* indirect), (mode, type, indirect))
EXTERNAL_DRAW(glMultiDrawArraysEXT, (unsigned mode, const int* first, const int* count, int draws), (mode, first, count, draws))
EXTERNAL_DRAW(glMultiDrawArraysANGLE, (unsigned mode, const int* first, const int* count, int draws), (mode, first, count, draws))
EXTERNAL_DRAW(glMultiDrawElementsEXT, (unsigned mode, const int* count, unsigned type, const void* const* indices, int draws), (mode, count, type, indices, draws))
EXTERNAL_DRAW(glMultiDrawElementsANGLE, (unsigned mode, const int* count, unsigned type, const void* const* indices, int draws), (mode, count, type, indices, draws))
EXTERNAL_DRAW(glMultiDrawElementsBaseVertexEXT, (unsigned mode, const int* count, unsigned type, const void* const* indices, int draws, const int* base), (mode, count, type, indices, draws, base))
EXTERNAL_DRAW(glMultiDrawArraysIndirectEXT, (unsigned mode, const void* indirect, int draws, int stride), (mode, indirect, draws, stride))
EXTERNAL_DRAW(glMultiDrawElementsIndirectEXT, (unsigned mode, unsigned type, const void* indirect, int draws, int stride), (mode, type, indirect, draws, stride))
EXTERNAL_DRAW(glMultiDrawArraysInstancedANGLE, (unsigned mode, const int* first, const int* count, const int* instances, int draws), (mode, first, count, instances, draws))
EXTERNAL_DRAW(glMultiDrawElementsInstancedANGLE, (unsigned mode, const int* count, unsigned type, const void* const* indices, const int* instances, int draws), (mode, count, type, indices, instances, draws))
EXTERNAL_DRAW(glMultiDrawArraysInstancedBaseInstanceANGLE, (unsigned mode, const int* first, const int* count, const int* instances, const unsigned* bases, int draws), (mode, first, count, instances, bases, draws))
EXTERNAL_DRAW(glMultiDrawElementsInstancedBaseVertexBaseInstanceANGLE, (unsigned mode, const int* count, unsigned type, const void* const* indices, const int* instances, const int* vertices, const unsigned* bases, int draws), (mode, count, type, indices, instances, vertices, bases, draws))
#undef EXTERNAL_DRAW

extern "C" void bionic_glGetTexParameteriv(unsigned target, unsigned pname, int* params) {
    using Fn = void (*)(unsigned, unsigned, int*);
    if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glGetTexParameteriv")))
        f(surface_texture::target(target), pname, params);
    surface_texture::restore_binding(target);
}
extern "C" void bionic_glGetTexParameterfv(unsigned target, unsigned pname, float* params) {
    using Fn = void (*)(unsigned, unsigned, float*);
    if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glGetTexParameterfv")))
        f(surface_texture::target(target), pname, params);
    surface_texture::restore_binding(target);
}
extern "C" void bionic_glVertexAttribDivisor(unsigned int index, unsigned int divisor) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glVertexAttribDivisor");
    if (!f) f = (PFN)GL_FUNC_CACHED("glVertexAttribDivisorANGLE");
    if (f) f(index, divisor);
}

// ── GLES 3.0: Buffer Mapping ────────────────────────────────────────────────
extern "C" void* bionic_glMapBufferRange(unsigned int target, long offset, long length, unsigned int access) {
    typedef void* (*PFN)(unsigned int, long, long, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glMapBufferRange");
    void* result = f ? f(target, offset, length, access) : nullptr;
    if (result && (access & 0x0002 /* GL_MAP_WRITE_BIT */))
        g_bufferMapWrites.fetch_add(1, std::memory_order_relaxed);
    static std::atomic<unsigned> calls{0};
    if (calls.fetch_add(1, std::memory_order_relaxed) < 64)
        KLOG(kInfo, "KuDroidGPU", "GL buffer map: target=0x%x offset=%ld length=%ld access=0x%x result=%p caller=%p",
             target, offset, length, access, result, __builtin_return_address(0));
    return result;
}

extern "C" void bionic_glPushDebugGroup(unsigned source, unsigned id, int length, const char* message) {
    using Fn = void (*)(unsigned, unsigned, int, const char*);
    auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glPushDebugGroup"));
    if (!f) return;
    static std::atomic<unsigned> calls{0}, logged{0};
    trace_gl_label("glPushDebugGroup", source, id, length, message, calls, logged);
    f(source, id, length, message);
}

extern "C" void bionic_glObjectLabel(unsigned identifier, unsigned name, int length, const char* label) {
    using Fn = void (*)(unsigned, unsigned, int, const char*);
    auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glObjectLabel"));
    if (!f) return;
    static std::atomic<unsigned> calls{0}, logged{0};
    trace_gl_label("glObjectLabel", identifier, name, length, label, calls, logged);
    f(identifier, name, length, label);
}
extern "C" unsigned char bionic_glUnmapBuffer(unsigned int target) {
    typedef unsigned char (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glUnmapBuffer");
    if (f) return f(target);
    return 0;
}
extern "C" void bionic_glFlushMappedBufferRange(unsigned int target, long offset, long length) {
    typedef void (*PFN)(unsigned int, long, long);
    auto f = (PFN)GL_FUNC_CACHED("glFlushMappedBufferRange");
    if (f) f(target, offset, length);
}

