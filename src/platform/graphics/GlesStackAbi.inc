// Guest code follows Linux AAPCS64: every stack-passed argument owns an 8-byte slot. Apple
// arm64 packs stack arguments at natural size, so a 4-byte stack argument followed by another
// 4-byte one would be read at the wrong offset. GL_STACK_PAD adds the missing 4 bytes after
// each 4-byte stack argument so these wrappers (and only them) read the guest layout.
#if defined(__APPLE__) && defined(__aarch64__)
#define GL_STACK_PAD , unsigned int /* unused slot half */
#else
#define GL_STACK_PAD
#endif

// ── GLES 3.0: 3D Textures & Immutable Storage & Samplers ────────────────────
extern "C" void bionic_glTexImage3D(unsigned int target, int level, int internalformat, int width, int height, int depth, int border, unsigned int format, unsigned int type, const void* pixels) {
    g_textureUploads.fetch_add(1, std::memory_order_relaxed);
    typedef void (*PFN)(unsigned int, int, int, int, int, int, int, unsigned int, unsigned int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glTexImage3D");
    if (f) f(target, level, internalformat, width, height, depth, border, format, type, pixels);
}
extern "C" void bionic_glTexSubImage3D(unsigned int target, int level, int xoffset, int yoffset, int zoffset, int width, int height, int depth, unsigned int format GL_STACK_PAD, unsigned int type GL_STACK_PAD, const void* pixels) {
    g_textureUploads.fetch_add(1, std::memory_order_relaxed);
    typedef void (*PFN)(unsigned int, int, int, int, int, int, int, int, unsigned int, unsigned int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glTexSubImage3D");
    if (f) f(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
}
extern "C" void bionic_glTexSubImage3DOES(unsigned int target, int level, int xoffset, int yoffset, int zoffset, int width, int height, int depth, unsigned int format GL_STACK_PAD, unsigned int type GL_STACK_PAD, const void* pixels) {
    g_textureUploads.fetch_add(1, std::memory_order_relaxed);
    typedef void (*PFN)(unsigned int, int, int, int, int, int, int, int, unsigned int, unsigned int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glTexSubImage3DOES");
    if (f) f(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
}
extern "C" void bionic_glCompressedTexSubImage3D(unsigned int target, int level, int xoffset, int yoffset, int zoffset, int width, int height, int depth, unsigned int format GL_STACK_PAD, int imageSize GL_STACK_PAD, const void* data) {
    g_textureUploads.fetch_add(1, std::memory_order_relaxed);
    typedef void (*PFN)(unsigned int, int, int, int, int, int, int, int, unsigned int, int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glCompressedTexSubImage3D");
    if (f) f(target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data);
}
extern "C" void bionic_glCompressedTexSubImage3DOES(unsigned int target, int level, int xoffset, int yoffset, int zoffset, int width, int height, int depth, unsigned int format GL_STACK_PAD, int imageSize GL_STACK_PAD, const void* data) {
    g_textureUploads.fetch_add(1, std::memory_order_relaxed);
    typedef void (*PFN)(unsigned int, int, int, int, int, int, int, int, unsigned int, int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glCompressedTexSubImage3DOES");
    if (f) f(target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data);
}
extern "C" void bionic_glTexStorage2D(unsigned int target, int levels, unsigned int internalformat, int width, int height) {
    typedef void (*PFN)(unsigned int, int, unsigned int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glTexStorage2D");
    if (f) f(target, levels, internalformat, width, height);
}
extern "C" void bionic_glCopyImageSubData(unsigned int srcName, unsigned int srcTarget, int srcLevel, int srcX, int srcY, int srcZ, unsigned int dstName, unsigned int dstTarget, int dstLevel GL_STACK_PAD, int dstX GL_STACK_PAD, int dstY GL_STACK_PAD, int dstZ GL_STACK_PAD, int width GL_STACK_PAD, int height GL_STACK_PAD, int depth) {
    using PFN = void (*)(unsigned int, unsigned int, int, int, int, int, unsigned int, unsigned int, int, int, int, int, int, int, int);
    auto f = reinterpret_cast<PFN>(GL_FUNC_CACHED("glCopyImageSubData"));
    if (f) f(srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName, dstTarget, dstLevel, dstX, dstY, dstZ, width, height, depth);
}
extern "C" void bionic_glCopyImageSubDataEXT(unsigned int srcName, unsigned int srcTarget, int srcLevel, int srcX, int srcY, int srcZ, unsigned int dstName, unsigned int dstTarget, int dstLevel GL_STACK_PAD, int dstX GL_STACK_PAD, int dstY GL_STACK_PAD, int dstZ GL_STACK_PAD, int width GL_STACK_PAD, int height GL_STACK_PAD, int depth) {
    using PFN = void (*)(unsigned int, unsigned int, int, int, int, int, unsigned int, unsigned int, int, int, int, int, int, int, int);
    auto f = reinterpret_cast<PFN>(GL_FUNC_CACHED("glCopyImageSubDataEXT"));
    if (f) f(srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName, dstTarget, dstLevel, dstX, dstY, dstZ, width, height, depth);
}
extern "C" void bionic_glCopyImageSubDataOES(unsigned int srcName, unsigned int srcTarget, int srcLevel, int srcX, int srcY, int srcZ, unsigned int dstName, unsigned int dstTarget, int dstLevel GL_STACK_PAD, int dstX GL_STACK_PAD, int dstY GL_STACK_PAD, int dstZ GL_STACK_PAD, int width GL_STACK_PAD, int height GL_STACK_PAD, int depth) {
    using PFN = void (*)(unsigned int, unsigned int, int, int, int, int, unsigned int, unsigned int, int, int, int, int, int, int, int);
    auto f = reinterpret_cast<PFN>(GL_FUNC_CACHED("glCopyImageSubDataOES"));
    if (f) f(srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName, dstTarget, dstLevel, dstX, dstY, dstZ, width, height, depth);
}
extern "C" void bionic_glTexStorage3D(unsigned int target, int levels, unsigned int internalformat, int width, int height, int depth) {
    typedef void (*PFN)(unsigned int, int, unsigned int, int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glTexStorage3D");
    if (f) f(target, levels, internalformat, width, height, depth);
}
extern "C" void bionic_glGenSamplers(int count, unsigned int* samplers) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenSamplers");
    if (f) f(count, samplers);
}
extern "C" void bionic_glBindSampler(unsigned int unit, unsigned int sampler) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindSampler");
    if (f) f(unit, sampler);
}
extern "C" void bionic_glDeleteSamplers(int count, const unsigned int* samplers) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteSamplers");
    if (f) f(count, samplers);
}
extern "C" void bionic_glSamplerParameteri(unsigned int sampler, unsigned int pname, int param) {
    typedef void (*PFN)(unsigned int, unsigned int, int);
    auto f = (PFN)GL_FUNC_CACHED("glSamplerParameteri");
    if (f) f(sampler, pname, param);
}
extern "C" void bionic_glSamplerParameterf(unsigned int sampler, unsigned int pname, float param) {
    typedef void (*PFN)(unsigned int, unsigned int, float);
    auto f = (PFN)GL_FUNC_CACHED("glSamplerParameterf");
    if (f) f(sampler, pname, param);
}

// ── GLES 3.0: Sync Objects & Fences ─────────────────────────────────────────
extern "C" void* bionic_glFenceSync(unsigned int condition, unsigned int flags) {
    typedef void* (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glFenceSync");
    if (f) return f(condition, flags);
    return nullptr;
}
extern "C" unsigned int bionic_glClientWaitSync(void* sync, unsigned int flags, uint64_t timeout) {
    typedef unsigned int (*PFN)(void*, unsigned int, uint64_t);
    auto f = (PFN)GL_FUNC_CACHED("glClientWaitSync");
    if (f) return f(sync, flags, timeout);
    return 0;
}
extern "C" void bionic_glWaitSync(void* sync, unsigned int flags, uint64_t timeout) {
    typedef void (*PFN)(void*, unsigned int, uint64_t);
    auto f = (PFN)GL_FUNC_CACHED("glWaitSync");
    if (f) f(sync, flags, timeout);
}
extern "C" void bionic_glDeleteSync(void* sync) {
    typedef void (*PFN)(void*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteSync");
    if (f) f(sync);
}
extern "C" unsigned char bionic_glIsSync(void* sync) {
    typedef unsigned char (*PFN)(void*);
    auto f = (PFN)GL_FUNC_CACHED("glIsSync");
    if (f) return f(sync);
    return 0;
}

// ── GLES 3.0: MRT & Blit Framebuffer ────────────────────────────────────────
extern "C" void bionic_glDrawBuffers(int n, const unsigned int* bufs) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDrawBuffers");
    if (f) f(n, bufs);
}
extern "C" void bionic_glReadBuffer(unsigned int src) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glReadBuffer");
    if (f) f(src);
}
extern "C" void bionic_glBlitFramebuffer(int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0, int dstX1, int dstY1, unsigned int mask GL_STACK_PAD, unsigned int filter) {
    typedef void (*PFN)(int, int, int, int, int, int, int, int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBlitFramebuffer");
    if (f) f(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}
extern "C" void bionic_glBlitFramebufferNV(int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0, int dstX1, int dstY1, unsigned int mask GL_STACK_PAD, unsigned int filter) {
    typedef void (*PFN)(int, int, int, int, int, int, int, int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBlitFramebufferNV");
    if (f) f(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}
extern "C" void bionic_glBlitFramebufferANGLE(int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0, int dstX1, int dstY1, unsigned int mask GL_STACK_PAD, unsigned int filter) {
    typedef void (*PFN)(int, int, int, int, int, int, int, int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBlitFramebufferANGLE");
    if (f) f(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}
extern "C" void bionic_glRenderbufferStorageMultisample(unsigned int target, int samples, unsigned int internalformat, int width, int height) {
    typedef void (*PFN)(unsigned int, int, unsigned int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glRenderbufferStorageMultisample");
    if (f) f(target, samples, internalformat, width, height);
}

// ── GLES 3.0/3.1: Uniform Buffer Objects (UBO) & SSBO ───────────────────────
extern "C" unsigned int bionic_glGetUniformBlockIndex(unsigned int program, const char* uniformBlockName) {
    typedef unsigned int (*PFN)(unsigned int, const char*);
    auto f = (PFN)GL_FUNC_CACHED("glGetUniformBlockIndex");
    if (f) return f(program, uniformBlockName);
    return 0xFFFFFFFF;
}
extern "C" void bionic_glUniformBlockBinding(unsigned int program, unsigned int uniformBlockIndex, unsigned int uniformBlockBinding) {
    typedef void (*PFN)(unsigned int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glUniformBlockBinding");
    if (f) f(program, uniformBlockIndex, uniformBlockBinding);
}
extern "C" void bionic_glBindBufferBase(unsigned int target, unsigned int index, unsigned int buffer) {
    typedef void (*PFN)(unsigned int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindBufferBase");
    if (f) f(target, index, buffer);
}
extern "C" void bionic_glBindBufferRange(unsigned int target, unsigned int index, unsigned int buffer, long offset, long size) {
    typedef void (*PFN)(unsigned int, unsigned int, unsigned int, long, long);
    auto f = (PFN)GL_FUNC_CACHED("glBindBufferRange");
    if (f) f(target, index, buffer, offset, size);
}

// ── GLES 3.1: Compute Shaders ───────────────────────────────────────────────
extern "C" void bionic_glDispatchCompute(unsigned int num_groups_x, unsigned int num_groups_y, unsigned int num_groups_z) {
    typedef void (*PFN)(unsigned int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glDispatchCompute");
    if (f) f(num_groups_x, num_groups_y, num_groups_z);
}
extern "C" void bionic_glDispatchComputeIndirect(long indirect) {
    typedef void (*PFN)(long);
    auto f = (PFN)GL_FUNC_CACHED("glDispatchComputeIndirect");
    if (f) f(indirect);
}

// ── GLES 3.0: Transform Feedback ────────────────────────────────────────────
extern "C" void bionic_glGenTransformFeedbacks(int n, unsigned int* ids) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenTransformFeedbacks");
    if (f) f(n, ids);
}
extern "C" void bionic_glBindTransformFeedback(unsigned int target, unsigned int id) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindTransformFeedback");
    if (f) f(target, id);
}
extern "C" void bionic_glDeleteTransformFeedbacks(int n, const unsigned int* ids) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteTransformFeedbacks");
    if (f) f(n, ids);
}
extern "C" void bionic_glBeginTransformFeedback(unsigned int primitiveMode) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBeginTransformFeedback");
    if (f) f(primitiveMode);
}
extern "C" void bionic_glEndTransformFeedback(void) {
    typedef void (*PFN)(void);
    auto f = (PFN)GL_FUNC_CACHED("glEndTransformFeedback");
    if (f) f();
}

// Receive guest GLboolean registers at full width before calling the host ABI.
#define GL_BOOLEAN_ENTRY(name, guest, host, arguments) \
    extern "C" void bionic_##name guest { \
        using Fn = void (*) host; \
        if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED(#name))) f arguments; \
    }
#include "../GraphicsBooleanEntries.inc"
#undef GL_BOOLEAN_ENTRY

