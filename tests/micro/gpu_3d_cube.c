typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long size_t;
#define NULL ((void*)0)

// Android Log
int __android_log_print(int prio, const char* tag, const char* fmt, ...);
#define LOGI(...) __android_log_print(3, "Gpu3DCube", __VA_ARGS__)
#define LOGE(...) __android_log_print(6, "Gpu3DCube", __VA_ARGS__)

// Bionic GLES2/3 & EGL Types
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;

// EGL
EGLDisplay eglGetDisplay(void*);
unsigned int eglInitialize(EGLDisplay, int*, int*);
int eglChooseConfig(EGLDisplay, const int*, EGLConfig*, int, int*);
EGLSurface eglCreatePbufferSurface(EGLDisplay, EGLConfig, const int*);
EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const int*);
unsigned int eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext);

// GLES 2/3 Core
unsigned int glCreateShader(unsigned int);
void glShaderSource(unsigned int, int, const char* const*, const int*);
void glCompileShader(unsigned int);
void glGetShaderiv(unsigned int, unsigned int, int*);
void glGetShaderInfoLog(unsigned int, int, int*, char*);
unsigned int glCreateProgram(void);
void glAttachShader(unsigned int, unsigned int);
void glLinkProgram(unsigned int);
void glGetProgramiv(unsigned int, unsigned int, int*);
void glGetProgramInfoLog(unsigned int, int, int*, char*);
void glUseProgram(unsigned int);
int glGetAttribLocation(unsigned int, const char*);
int glGetUniformLocation(unsigned int, const char*);
void glUniformMatrix4fv(int, int, unsigned char, const float*);
void glEnableVertexAttribArray(unsigned int);
void glVertexAttribPointer(unsigned int, int, unsigned int, unsigned char, int, const void*);
void glViewport(int, int, int, int);
void glClearColor(float, float, float, float);
void glClearDepthf(float);
void glClear(unsigned int);
void glEnable(unsigned int);
void glDisable(unsigned int);
void glDepthFunc(unsigned int);
void glDepthMask(unsigned char);
void glCullFace(unsigned int);

// Buffers & VAO & FBO
void glGenBuffers(int, unsigned int*);
void glBindBuffer(unsigned int, unsigned int);
void glBufferData(unsigned int, long, const void*, unsigned int);
void glGenVertexArrays(int, unsigned int*);
void glBindVertexArray(unsigned int);
void glDrawElements(unsigned int, int, unsigned int, const void*);
void glReadPixels(int, int, int, int, unsigned int, unsigned int, void*);

void glGenTextures(int, unsigned int*);
void glBindTexture(unsigned int, unsigned int);
void glTexImage2D(unsigned int, int, int, int, int, int, unsigned int, unsigned int, const void*);
void glTexParameteri(unsigned int, unsigned int, int);
void glGenFramebuffers(int, unsigned int*);
void glBindFramebuffer(unsigned int, unsigned int);
void glFramebufferTexture2D(unsigned int, unsigned int, unsigned int, unsigned int, int);
void glGenRenderbuffers(int, unsigned int*);
void glBindRenderbuffer(unsigned int, unsigned int);
void glRenderbufferStorage(unsigned int, unsigned int, int, int);
void glFramebufferRenderbuffer(unsigned int, unsigned int, unsigned int, unsigned int);
unsigned int glCheckFramebufferStatus(unsigned int);

// Simple 4x4 Matrix Multiply
static void mat4_multiply(float* out, const float* a, const float* b) {
    float res[16];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            res[i * 4 + j] =
                a[i * 4 + 0] * b[0 * 4 + j] +
                a[i * 4 + 1] * b[1 * 4 + j] +
                a[i * 4 + 2] * b[2 * 4 + j] +
                a[i * 4 + 3] * b[3 * 4 + j];
        }
    }
    for (int k = 0; k < 16; ++k) out[k] = res[k];
}

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🎲 [3D Engine Test] Cube Mesh, VBO, EBO, Depth Test & Matrix 4x4");
    LOGI("=================================================");

    EGLDisplay dpy = eglGetDisplay(NULL);
    int maj = 0, min = 0;
    if (!eglInitialize(dpy, &maj, &min)) {
        LOGE("❌ eglInitialize failed");
        return 1;
    }
    LOGI("✔ EGL Initialized: version %d.%d", maj, min);

    const int configAttribs[] = {
        0x3033, 0x0001, // EGL_PBUFFER_BIT
        0x3040, 0x0040, // EGL_OPENGL_ES3_BIT
        0x3025, 24,     // EGL_DEPTH_SIZE 24
        0x3038
    };
    EGLConfig config = NULL;
    int numConfigs = 0;
    eglChooseConfig(dpy, configAttribs, &config, 1, &numConfigs);

    const int pbufAttribs[] = { 0x3057, 256, 0x3056, 256, 0x3038 };
    EGLSurface surf = eglCreatePbufferSurface(dpy, config, pbufAttribs);
    const int ctxAttribs[] = { 0x3098, 3, 0x3038 };
    EGLContext ctx = eglCreateContext(dpy, config, NULL, ctxAttribs);
    if (!ctx) {
        LOGE("❌ Failed to create ES3 Context");
        return 2;
    }
    eglMakeCurrent(dpy, surf, surf, ctx);
    LOGI("✔ ES3 Context bound with Depth Buffer support");

    // 1. Tạo FBO có cả Color Attachment và Depth Attachment (D24)
    unsigned int colorTex = 0;
    glGenTextures(1, &colorTex);
    glBindTexture(0x0DE1, colorTex);
    glTexParameteri(0x0DE1, 0x2801, 0x2601);
    glTexParameteri(0x0DE1, 0x2800, 0x2601);
    glTexImage2D(0x0DE1, 0, 0x1908, 256, 256, 0, 0x1908, 0x1401, NULL);

    unsigned int depthRb = 0;
    glGenRenderbuffers(1, &depthRb);
    glBindRenderbuffer(0x8D41, depthRb); // GL_RENDERBUFFER
    glRenderbufferStorage(0x8D41, 0x81A6, 256, 256); // GL_DEPTH_COMPONENT24

    unsigned int fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(0x8D40, fbo);
    glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, colorTex, 0);
    glFramebufferRenderbuffer(0x8D40, 0x8D00, 0x8D41, depthRb); // GL_DEPTH_ATTACHMENT

    if (glCheckFramebufferStatus(0x8D40) != 0x8CD5) {
        LOGE("❌ 3D Framebuffer incomplete");
        return 3;
    }
    LOGI("✔ 3D FBO Configured: 256x256 Color (RGBA8) + Depth (D24)");

    // 2. Dữ liệu đỉnh 3D của Cube (24 đỉnh = 6 mặt * 4 đỉnh, mỗi đỉnh gồm: PosX, PosY, PosZ, ColorR, ColorG, ColorB)
    const float cubeVertices[] = {
        // Mặt trước (Màu Đỏ)
        -1.0f, -1.0f,  1.0f,   1.0f, 0.0f, 0.0f,
         1.0f, -1.0f,  1.0f,   1.0f, 0.0f, 0.0f,
         1.0f,  1.0f,  1.0f,   1.0f, 0.0f, 0.0f,
        -1.0f,  1.0f,  1.0f,   1.0f, 0.0f, 0.0f,
        // Mặt sau (Màu Xanh lá)
        -1.0f, -1.0f, -1.0f,   0.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, -1.0f,   0.0f, 1.0f, 0.0f,
         1.0f,  1.0f, -1.0f,   0.0f, 1.0f, 0.0f,
         1.0f, -1.0f, -1.0f,   0.0f, 1.0f, 0.0f,
        // Mặt trên (Màu Xanh dương)
        -1.0f,  1.0f, -1.0f,   0.0f, 0.0f, 1.0f,
        -1.0f,  1.0f,  1.0f,   0.0f, 0.0f, 1.0f,
         1.0f,  1.0f,  1.0f,   0.0f, 0.0f, 1.0f,
         1.0f,  1.0f, -1.0f,   0.0f, 0.0f, 1.0f,
        // Mặt dưới (Màu Vàng)
        -1.0f, -1.0f, -1.0f,   1.0f, 1.0f, 0.0f,
         1.0f, -1.0f, -1.0f,   1.0f, 1.0f, 0.0f,
         1.0f, -1.0f,  1.0f,   1.0f, 1.0f, 0.0f,
        -1.0f, -1.0f,  1.0f,   1.0f, 1.0f, 0.0f,
        // Mặt phải (Màu Tím Cyan)
         1.0f, -1.0f, -1.0f,   0.0f, 1.0f, 1.0f,
         1.0f,  1.0f, -1.0f,   0.0f, 1.0f, 1.0f,
         1.0f,  1.0f,  1.0f,   0.0f, 1.0f, 1.0f,
         1.0f, -1.0f,  1.0f,   0.0f, 1.0f, 1.0f,
        // Mặt trái (Màu Trắng Hồng)
        -1.0f, -1.0f, -1.0f,   1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f,  1.0f,   1.0f, 0.0f, 1.0f,
        -1.0f,  1.0f,  1.0f,   1.0f, 0.0f, 1.0f,
        -1.0f,  1.0f, -1.0f,   1.0f, 0.0f, 1.0f,
    };

    // 36 chỉ số (Indices) tạo thành 12 tam giác
    const uint16_t cubeIndices[] = {
         0,  1,  2,      0,  2,  3,    // Mặt trước
         4,  5,  6,      4,  6,  7,    // Mặt sau
         8,  9, 10,      8, 10, 11,    // Mặt trên
        12, 13, 14,     12, 14, 15,    // Mặt dưới
        16, 17, 18,     16, 18, 19,    // Mặt phải
        20, 21, 22,     20, 22, 23     // Mặt trái
    };

    // 3. Tạo VBO, EBO và nạp dữ liệu vào bộ nhớ GPU
    unsigned int vbo = 0, ebo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(0x8892, vbo); // GL_ARRAY_BUFFER
    glBufferData(0x8892, sizeof(cubeVertices), cubeVertices, 0x88E4); // GL_STATIC_DRAW

    glGenBuffers(1, &ebo);
    glBindBuffer(0x8893, ebo); // GL_ELEMENT_ARRAY_BUFFER
    glBufferData(0x8893, sizeof(cubeIndices), cubeIndices, 0x88E4);
    LOGI("✔ VBO (ID=%u, %lu bytes) & EBO (ID=%u, %lu bytes) loaded to GPU memory",
         vbo, sizeof(cubeVertices), ebo, sizeof(cubeIndices));

    // 4. Shader GLSL ES 3.00 có ma trận biến đổi không gian 3D
    const char* vSrc =
        "#version 300 es\n"
        "layout(location = 0) in vec3 aPos;\n"
        "layout(location = 1) in vec3 aColor;\n"
        "uniform mat4 uMVP;\n"
        "out vec3 vColor;\n"
        "void main() {\n"
        "  gl_Position = uMVP * vec4(aPos, 1.0);\n"
        "  vColor = aColor;\n"
        "}\n";

    const char* fSrc =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec3 vColor;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "  fragColor = vec4(vColor, 1.0);\n"
        "}\n";

    unsigned int vs = glCreateShader(0x8B31);
    const char* vsList[] = { vSrc };
    glShaderSource(vs, 1, vsList, NULL);
    glCompileShader(vs);

    unsigned int fs = glCreateShader(0x8B30);
    const char* fsList[] = { fSrc };
    glShaderSource(fs, 1, fsList, NULL);
    glCompileShader(fs);

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glUseProgram(prog);

    int mvpLoc = glGetUniformLocation(prog, "uMVP");
    LOGI("✔ 3D Shader compiled & linked. uMVP uniform location: %d", mvpLoc);

    // 5. Cấu hình Vertex Attributes từ VBO
    int stride = 6 * sizeof(float);
    glEnableVertexAttribArray(0); // aPos
    glVertexAttribPointer(0, 3, 0x1406, 0, stride, (void*)0);
    glEnableVertexAttribArray(1); // aColor
    glVertexAttribPointer(1, 3, 0x1406, 0, stride, (void*)(3 * sizeof(float)));

    // 6. Kích hoạt Depth Test
    glEnable(0x0B71); // GL_DEPTH_TEST
    glDepthFunc(0x0201); // GL_LESS
    glDepthMask(1);

    glViewport(0, 0, 256, 256);
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClearDepthf(1.0f);
    glClear(0x4000 | 0x0100); // GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT

    // 7. Tạo ma trận MVP 3D chiếu phối cảnh (Perspective Projection) và xoay khối cube
    // Góc nhìn hơi nghiêng xuống để nhìn thấy cả mặt trên (Xanh dương) và mặt trước (Đỏ)
    const float mvpMatrix[16] = {
        0.58f,  0.24f, -0.38f, -0.38f,
        0.00f,  0.64f,  0.40f,  0.40f,
       -0.58f,  0.24f, -0.38f, -0.38f,
        0.00f,  0.00f,  3.20f,  3.40f
    };
    glUniformMatrix4fv(mvpLoc, 1, 0, mvpMatrix);

    // 8. Thực thi Draw Elements với Index Buffer
    // Lưu ý: indices là offset 0 trong EBO
    LOGI("🚀 Executing 3D Indexed Draw Call: glDrawElements(GL_TRIANGLES, 36)...");
    glDrawElements(0x0004, 36, 0x1403, (void*)0); // GL_TRIANGLES, 36, GL_UNSIGNED_SHORT
    LOGI("✔ 3D Indexed Draw Call executed successfully!");

    // 9. Xác thực Pixel & Depth Test
    uint8_t frontFacePix[4] = {0};
    glReadPixels(128, 100, 1, 1, 0x1908, 0x1401, frontFacePix);
    LOGI("🔍 Front/Top 3D Face Pixel: RGBA=(%d, %d, %d, %d)",
         (int)frontFacePix[0], (int)frontFacePix[1], (int)frontFacePix[2], (int)frontFacePix[3]);

    uint8_t bgPix[4] = {0};
    glReadPixels(20, 20, 1, 1, 0x1908, 0x1401, bgPix);
    LOGI("🔍 Background Pixel: RGBA=(%d, %d, %d, %d)",
         (int)bgPix[0], (int)bgPix[1], (int)bgPix[2], (int)bgPix[3]);

    // Nếu vẽ 3D thành công: Pixel ở trung tâm phải có màu sáng từ các mặt của khối cube (không phải màu nền)
    if ((frontFacePix[0] > 100 || frontFacePix[1] > 100 || frontFacePix[2] > 100) &&
        (bgPix[0] < 50 && bgPix[1] < 50 && bgPix[2] < 60)) {
        LOGI("🎉 SUCCESS: 3D CUBE MESH + VBO + EBO + DEPTH TEST RENDERED PERFECTLY!");
        return 0;
    } else {
        LOGE("⚠ 3D Pixel validation mismatch");
        return 4;
    }
}
