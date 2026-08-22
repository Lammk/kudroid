typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
#define NULL ((void*)0)

// Android Log
int __android_log_print(int prio, const char* tag, const char* fmt, ...);
#define LOGI(...) __android_log_print(3, "GpuGLInfo", __VA_ARGS__)
#define LOGE(...) __android_log_print(6, "GpuGLInfo", __VA_ARGS__)

// EGL
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;

EGLDisplay eglGetDisplay(void*);
unsigned int eglInitialize(EGLDisplay, int*, int*);
int eglChooseConfig(EGLDisplay, const int*, EGLConfig*, int, int*);
EGLSurface eglCreatePbufferSurface(EGLDisplay, EGLConfig, const int*);
EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const int*);
unsigned int eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext);

// GLES
const char* glGetString(unsigned int);
const char* glGetStringi(unsigned int, unsigned int);
void glGetIntegerv(unsigned int, int*);

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("📱 [Apple Silicon GPU & OpenGL ES Hardware Info]");
    LOGI("=================================================");

    EGLDisplay dpy = eglGetDisplay(NULL);
    int maj = 0, min = 0;
    if (!eglInitialize(dpy, &maj, &min)) {
        LOGE("❌ eglInitialize failed");
        return 1;
    }

    const int configAttribs[] = {
        0x3033, 0x0001, // EGL_PBUFFER_BIT
        0x3040, 0x0040, // EGL_OPENGL_ES3_BIT
        0x3038
    };
    EGLConfig config = NULL;
    int numConfigs = 0;
    eglChooseConfig(dpy, configAttribs, &config, 1, &numConfigs);

    const int pbufAttribs[] = { 0x3057, 16, 0x3056, 16, 0x3038 };
    EGLSurface surf = eglCreatePbufferSurface(dpy, config, pbufAttribs);
    const int ctxAttribs[] = { 0x3098, 3, 0x3038 };
    EGLContext ctx = eglCreateContext(dpy, config, NULL, ctxAttribs);
    eglMakeCurrent(dpy, surf, surf, ctx);

    // 1. Truy vấn các chuỗi định danh GPU
    const char* vendor   = glGetString(0x1F00); // GL_VENDOR
    const char* renderer = glGetString(0x1F01); // GL_RENDERER
    const char* version  = glGetString(0x1F02); // GL_VERSION
    const char* glslVer  = glGetString(0x8B8C); // GL_SHADING_LANGUAGE_VERSION

    LOGI("🌟 [GPU Hardware Identification]");
    LOGI("  🏛 Vendor                  : %s", vendor ? vendor : "N/A");
    LOGI("  🚀 Renderer (GPU Chipset)  : %s", renderer ? renderer : "N/A");
    LOGI("  ⚙️ GLES Version            : %s", version ? version : "N/A");
    LOGI("  📜 GLSL Shading Language   : %s", glslVer ? glslVer : "N/A");

    // 2. Truy vấn giới hạn phần cứng đồ họa (Hardware Limits)
    int maxTexSize = 0;
    glGetIntegerv(0x0D33, &maxTexSize); // GL_MAX_TEXTURE_SIZE

    int max3DTexSize = 0;
    glGetIntegerv(0x8073, &max3DTexSize); // GL_MAX_3D_TEXTURE_SIZE

    int maxCubeMapSize = 0;
    glGetIntegerv(0x851C, &maxCubeMapSize); // GL_MAX_CUBE_MAP_TEXTURE_SIZE

    int maxRenderbufferSize = 0;
    glGetIntegerv(0x84E8, &maxRenderbufferSize); // GL_MAX_RENDERBUFFER_SIZE

    int maxVertexAttribs = 0;
    glGetIntegerv(0x8869, &maxVertexAttribs); // GL_MAX_VERTEX_ATTRIBS

    int maxVUniforms = 0, maxFUniforms = 0;
    glGetIntegerv(0x8DFB, &maxVUniforms); // GL_MAX_VERTEX_UNIFORM_VECTORS
    glGetIntegerv(0x8DFD, &maxFUniforms); // GL_MAX_FRAGMENT_UNIFORM_VECTORS

    int maxColorAttachments = 0;
    glGetIntegerv(0x8CDF, &maxColorAttachments); // GL_MAX_COLOR_ATTACHMENTS

    int maxDrawBuffers = 0;
    glGetIntegerv(0x8824, &maxDrawBuffers); // GL_MAX_DRAW_BUFFERS

    int maxSamples = 0;
    glGetIntegerv(0x8D57, &maxSamples); // GL_MAX_SAMPLES (MSAA)

    LOGI("📊 [Apple Silicon Hardware Capabilities]");
    LOGI("  🖼 Max 2D Texture Size     : %d x %d", maxTexSize, maxTexSize);
    LOGI("  📦 Max 3D Texture Size     : %d x %d x %d", max3DTexSize, max3DTexSize, max3DTexSize);
    LOGI("  🎲 Max Cube Map Size       : %d x %d", maxCubeMapSize, maxCubeMapSize);
    LOGI("  🎯 Max FBO Renderbuffer    : %d x %d", maxRenderbufferSize, maxRenderbufferSize);
    LOGI("  📌 Max Vertex Attributes   : %d", maxVertexAttribs);
    LOGI("  🔢 Max Vertex Uniforms     : %d vectors", maxVUniforms);
    LOGI("  🔢 Max Fragment Uniforms   : %d vectors", maxFUniforms);
    LOGI("  🎨 Max MRT Attachments     : %d", maxColorAttachments);
    LOGI("  🖌 Max Simultaneous Draws  : %d buffers", maxDrawBuffers);
    LOGI("  ✨ Max Hardware MSAA       : %dx", maxSamples);

    // 3. Đếm và in các extension GLES 3.0
    int numExts = 0;
    glGetIntegerv(0x821D, &numExts); // GL_NUM_EXTENSIONS
    LOGI("🧩 [Supported GLES Extensions: Total %d]", numExts);
    for (int i = 0; i < numExts && i < 15; ++i) {
        const char* ext = glGetStringi(0x1F03, i); // GL_EXTENSIONS
        if (ext) {
            LOGI("   - [%d] %s", i, ext);
        }
    }
    if (numExts > 15) {
        LOGI("   ... and %d more extensions", numExts - 15);
    }

    LOGI("=================================================");
    LOGI("🎉 OPENGL ES GPU QUERY COMPLETED SUCCESSFULLY!");
    LOGI("=================================================");
    return 0;
}
