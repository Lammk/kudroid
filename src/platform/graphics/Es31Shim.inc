// GLES 3.1 compatibility on a 3.0 driver (see kudroid/platform/Es31Compat.h).
//
// Opt-in per package: the package is listed in <androidRoot>/es31_packages.txt.
// For a listed package the driver is reported as 3.1, 3.1 shaders that 3.0 can
// express are rewritten, and the 3.1-only queries answer from their 3.0 equivalents
// instead of raising an error that leaves the caller's output untouched.

static bool es31_compat_enabled() {
    const std::string pkg = VFSPathRemapper::getInstance(false).packageName();
    if (pkg.empty()) return false;
    static std::mutex mutex;
    static std::string decidedFor;
    static bool decision = false;
    std::lock_guard<std::mutex> lock(mutex);
    if (decidedFor != pkg) {
        decidedFor = pkg;
        std::ifstream in(VFSPathRemapper::getInstance(false).androidRoot() + "/es31_packages.txt");
        std::stringstream text;
        text << in.rdbuf();
        decision = kudroid::gles31_package_listed(text.str(), pkg);
        KLOG(kInfo, "KuDroidGPU", "ES 3.1 compat %s for %s", decision ? "enabled" : "off", pkg.c_str());
    }
    return decision;
}

static void es31_compat_shader(unsigned shader, std::string& source) {
    static std::atomic<unsigned> downgraded{0}, unsupported{0};
    switch (kudroid::gles31_downgrade_shader(source)) {
        case kudroid::ShaderDowngrade::Downgraded: {
            const unsigned n = downgraded.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n == 1 || n % 500 == 0)
                KLOG(kInfo, "KuDroidGPU", "ES 3.1 compat: %u shaders rewritten to 300 es", n);
            break;
        }
        case kudroid::ShaderDowngrade::Unsupported: {
            const unsigned n = unsupported.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n <= 8 || n % 500 == 0)
                KLOG(kInfo, "KuDroidGPU", "ES 3.1 compat: shader %u needs %s, left as is (%u so far)",
                     shader, kudroid::gles31_unsupported_feature(source), n);
            break;
        }
        case kudroid::ShaderDowngrade::NotApplicable:
            break;
    }
}

namespace {

constexpr unsigned kInterfaceUniform = 0x92E1;
constexpr unsigned kInterfaceUniformBlock = 0x92E2;
constexpr unsigned kInterfaceProgramInput = 0x92E3;

// The 3.0 queries the interface calls are answered from.
struct Es31Queries {
    void (*programiv)(unsigned, unsigned, int*);
    void (*activeUniform)(unsigned, unsigned, int, int*, int*, unsigned*, char*);
    void (*activeAttrib)(unsigned, unsigned, int, int*, int*, unsigned*, char*);
    void (*uniformsiv)(unsigned, int, const unsigned*, unsigned, int*);
    void (*blockiv)(unsigned, unsigned, unsigned, int*);
    void (*blockName)(unsigned, unsigned, int, int*, char*);
    unsigned (*blockIndex)(unsigned, const char*);
    void (*uniformIndices)(unsigned, int, const char* const*, unsigned*);
    int (*uniformLocation)(unsigned, const char*);
    int (*attribLocation)(unsigned, const char*);
    bool ok;
};

const Es31Queries& es31_queries() {
    static const Es31Queries table = [] {
        Es31Queries t{};
        t.programiv = reinterpret_cast<decltype(t.programiv)>(get_gl_func("glGetProgramiv"));
        t.activeUniform = reinterpret_cast<decltype(t.activeUniform)>(get_gl_func("glGetActiveUniform"));
        t.activeAttrib = reinterpret_cast<decltype(t.activeAttrib)>(get_gl_func("glGetActiveAttrib"));
        t.uniformsiv = reinterpret_cast<decltype(t.uniformsiv)>(get_gl_func("glGetActiveUniformsiv"));
        t.blockiv = reinterpret_cast<decltype(t.blockiv)>(get_gl_func("glGetActiveUniformBlockiv"));
        t.blockName = reinterpret_cast<decltype(t.blockName)>(get_gl_func("glGetActiveUniformBlockName"));
        t.blockIndex = reinterpret_cast<decltype(t.blockIndex)>(get_gl_func("glGetUniformBlockIndex"));
        t.uniformIndices = reinterpret_cast<decltype(t.uniformIndices)>(get_gl_func("glGetUniformIndices"));
        t.uniformLocation = reinterpret_cast<decltype(t.uniformLocation)>(get_gl_func("glGetUniformLocation"));
        t.attribLocation = reinterpret_cast<decltype(t.attribLocation)>(get_gl_func("glGetAttribLocation"));
        t.ok = t.programiv && t.activeUniform && t.activeAttrib && t.uniformsiv && t.blockiv &&
               t.blockName && t.blockIndex && t.uniformIndices && t.uniformLocation && t.attribLocation;
        return t;
    }();
    return table;
}

// The name of a uniform or vertex input by index, empty when the index is invalid.
std::string es31_resource_name(const Es31Queries& q, unsigned program, unsigned iface, unsigned index) {
    int maxLength = 0;
    q.programiv(program, iface == kInterfaceUniform ? 0x8B87 /* ACTIVE_UNIFORM_MAX_LENGTH */
                                                    : 0x8B8A /* ACTIVE_ATTRIBUTE_MAX_LENGTH */, &maxLength);
    if (maxLength <= 0 || maxLength > 4096) return {};
    std::vector<char> name(static_cast<size_t>(maxLength) + 1, '\0');
    int length = 0, size = 0;
    unsigned type = 0;
    if (iface == kInterfaceUniform) q.activeUniform(program, index, maxLength, &length, &size, &type, name.data());
    else q.activeAttrib(program, index, maxLength, &length, &size, &type, name.data());
    return std::string(name.data());
}

}  // namespace

extern "C" void bionic_glGetProgramInterfaceiv(unsigned program, unsigned iface, unsigned pname, int* params) {
    if (!es31_compat_enabled()) {
        using Fn = void (*)(unsigned, unsigned, unsigned, int*);
        if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glGetProgramInterfaceiv"))) f(program, iface, pname, params);
        return;
    }
    if (!params) return;
    *params = 0;
    const Es31Queries& q = es31_queries();
    if (!q.ok) return;
    const bool count = pname == 0x92F5;      // GL_ACTIVE_RESOURCES
    const bool maxName = pname == 0x92F6;    // GL_MAX_NAME_LENGTH
    if (iface == kInterfaceUniform) {
        if (count) q.programiv(program, 0x8B86, params);
        else if (maxName) q.programiv(program, 0x8B87, params);
    } else if (iface == kInterfaceUniformBlock) {
        int blocks = 0;
        q.programiv(program, 0x8A36, &blocks);  // GL_ACTIVE_UNIFORM_BLOCKS
        if (count) *params = blocks;
        else if (maxName) q.programiv(program, 0x8A35, params);
        else if (pname == 0x92F7) {             // GL_MAX_NUM_ACTIVE_VARIABLES
            for (int i = 0; i < blocks; ++i) {
                int members = 0;
                q.blockiv(program, static_cast<unsigned>(i), 0x8A42, &members);
                if (members > *params) *params = members;
            }
        }
    } else if (iface == kInterfaceProgramInput) {
        if (count) q.programiv(program, 0x8B89, params);
        else if (maxName) q.programiv(program, 0x8B8A, params);
    }
}

extern "C" unsigned bionic_glGetProgramResourceIndex(unsigned program, unsigned iface, const char* name) {
    if (!es31_compat_enabled()) {
        using Fn = unsigned (*)(unsigned, unsigned, const char*);
        auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glGetProgramResourceIndex"));
        return f ? f(program, iface, name) : 0xFFFFFFFFu;
    }
    const Es31Queries& q = es31_queries();
    if (!name || !q.ok) return 0xFFFFFFFFu;
    if (iface == kInterfaceUniform) {
        unsigned index = 0xFFFFFFFFu;
        q.uniformIndices(program, 1, &name, &index);
        return index;
    }
    if (iface == kInterfaceUniformBlock) return q.blockIndex(program, name);
    if (iface == kInterfaceProgramInput) {
        int count = 0;
        q.programiv(program, 0x8B89, &count);
        for (int i = 0; i < count; ++i)
            if (es31_resource_name(q, program, iface, static_cast<unsigned>(i)) == name) return static_cast<unsigned>(i);
    }
    return 0xFFFFFFFFu;
}

extern "C" int bionic_glGetProgramResourceLocation(unsigned program, unsigned iface, const char* name) {
    if (!es31_compat_enabled()) {
        using Fn = int (*)(unsigned, unsigned, const char*);
        auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glGetProgramResourceLocation"));
        return f ? f(program, iface, name) : -1;
    }
    const Es31Queries& q = es31_queries();
    if (!name || !q.ok) return -1;
    if (iface == kInterfaceUniform) return q.uniformLocation(program, name);
    if (iface == kInterfaceProgramInput) return q.attribLocation(program, name);
    return -1;
}

extern "C" void bionic_glGetProgramResourceName(unsigned program, unsigned iface, unsigned index, int bufSize,
                                                int* length, char* name) {
    if (!es31_compat_enabled()) {
        using Fn = void (*)(unsigned, unsigned, unsigned, int, int*, char*);
        if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glGetProgramResourceName")))
            f(program, iface, index, bufSize, length, name);
        return;
    }
    if (length) *length = 0;
    if (name && bufSize > 0) name[0] = '\0';
    const Es31Queries& q = es31_queries();
    if (!q.ok || bufSize <= 0 || !name) return;
    int written = 0;
    if (iface == kInterfaceUniformBlock) {
        q.blockName(program, index, bufSize, &written, name);
        if (length) *length = written;
    } else if (iface == kInterfaceUniform || iface == kInterfaceProgramInput) {
        const std::string text = es31_resource_name(q, program, iface, index);
        const int copied = std::min(static_cast<int>(text.size()), bufSize - 1);
        std::memcpy(name, text.data(), static_cast<size_t>(copied));
        name[copied] = '\0';
        if (length) *length = copied;
    }
}

extern "C" void bionic_glGetProgramResourceiv(unsigned program, unsigned iface, unsigned index, int propCount,
                                              const unsigned* props, int bufSize, int* length, int* params) {
    if (!es31_compat_enabled()) {
        using Fn = void (*)(unsigned, unsigned, unsigned, int, const unsigned*, int, int*, int*);
        if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glGetProgramResourceiv")))
            f(program, iface, index, propCount, props, bufSize, length, params);
        return;
    }
    if (length) *length = 0;
    const Es31Queries& q = es31_queries();
    if (!q.ok || !props || !params || propCount <= 0 || bufSize <= 0) return;

    int written = 0;
    std::string name;
    int arraySize = 0;
    unsigned type = 0;
    if (iface == kInterfaceProgramInput) {
        // Vertex inputs only answer through the attribute query.
        int nameLength = 0;
        std::vector<char> buffer(256, '\0');
        q.activeAttrib(program, index, static_cast<int>(buffer.size()), &nameLength, &arraySize, &type, buffer.data());
        name.assign(buffer.data());
    }
    for (int i = 0; i < propCount && written < bufSize; ++i) {
        const unsigned prop = props[i];
        int value = 0;
        if (iface == kInterfaceUniform) {
            unsigned mapped = 0;
            switch (prop) {
                case 0x92FA: mapped = 0x8A37; break;  // TYPE
                case 0x92FB: mapped = 0x8A38; break;  // ARRAY_SIZE
                case 0x92F9: mapped = 0x8A39; break;  // NAME_LENGTH
                case 0x92FD: mapped = 0x8A3A; break;  // BLOCK_INDEX
                case 0x92FC: mapped = 0x8A3B; break;  // OFFSET
                case 0x92FE: mapped = 0x8A3C; break;  // ARRAY_STRIDE
                case 0x92FF: mapped = 0x8A3D; break;  // MATRIX_STRIDE
                case 0x9300: mapped = 0x8A3E; break;  // IS_ROW_MAJOR
                default: break;
            }
            if (mapped) q.uniformsiv(program, 1, &index, mapped, &value);
            else if (prop == 0x930E) {                // LOCATION
                const std::string text = es31_resource_name(q, program, iface, index);
                value = text.empty() ? -1 : q.uniformLocation(program, text.c_str());
            } else if (prop == 0x9306 || prop == 0x930A) value = 1;  // referenced by vertex / fragment
            else if (prop == 0x9301) value = -1;      // ATOMIC_COUNTER_BUFFER_INDEX
        } else if (iface == kInterfaceUniformBlock) {
            unsigned mapped = 0;
            switch (prop) {
                case 0x9302: mapped = 0x8A3F; break;  // BUFFER_BINDING
                case 0x9303: mapped = 0x8A40; break;  // BUFFER_DATA_SIZE
                case 0x92F9: mapped = 0x8A41; break;  // NAME_LENGTH
                case 0x9304: mapped = 0x8A42; break;  // NUM_ACTIVE_VARIABLES
                case 0x9306: mapped = 0x8A44; break;  // REFERENCED_BY_VERTEX_SHADER
                case 0x930A: mapped = 0x8A46; break;  // REFERENCED_BY_FRAGMENT_SHADER
                default: break;
            }
            if (prop == 0x9305) {                     // ACTIVE_VARIABLES: one entry per member
                int members = 0;
                q.blockiv(program, index, 0x8A42, &members);
                if (members > 0 && members <= 4096) {
                    std::vector<int> indices(static_cast<size_t>(members), 0);
                    q.blockiv(program, index, 0x8A43, indices.data());
                    for (int m = 0; m < members && written < bufSize; ++m) params[written++] = indices[m];
                }
                continue;
            }
            if (mapped) q.blockiv(program, index, mapped, &value);
        } else if (iface == kInterfaceProgramInput) {
            if (prop == 0x92FA) value = static_cast<int>(type);
            else if (prop == 0x92FB) value = arraySize;
            else if (prop == 0x92F9) value = name.empty() ? 0 : static_cast<int>(name.size()) + 1;
            else if (prop == 0x930E) value = name.empty() ? -1 : q.attribLocation(program, name.c_str());
            else if (prop == 0x9306) value = 1;
        }
        params[written++] = value;
    }
    if (length) *length = written;
}

// Indexed 3.1 limits (compute work-group sizes and counts) are errors on a 3.0
// context and leave the caller's storage untouched; they read as zero instead.
extern "C" void bionic_glGetIntegeri_v(unsigned target, unsigned index, int* data) {
    using Fn = void (*)(unsigned, unsigned, int*);
    auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glGetIntegeri_v"));
    if (data && es31_compat_enabled()) *data = 0;
    if (f) f(target, index, data);
}
