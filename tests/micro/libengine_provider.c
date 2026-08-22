typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long size_t;
#define NULL ((void*)0)

// Android Log
int __android_log_print(int prio, const char* tag, const char* fmt, ...);
#define LOGI(...) __android_log_print(3, "EngineProvider", __VA_ARGS__)
#define LOGE(...) __android_log_print(6, "EngineProvider", __VA_ARGS__)

// GLES
unsigned int glCreateShader(unsigned int);
void glShaderSource(unsigned int, int, const char* const*, const int*);
void glCompileShader(unsigned int);
void glGetShaderiv(unsigned int, unsigned int, int*);
unsigned int glCreateProgram(void);
void glAttachShader(unsigned int, unsigned int);
void glLinkProgram(unsigned int);
void glUseProgram(unsigned int);
int glGetAttribLocation(unsigned int, const char*);
void glEnableVertexAttribArray(unsigned int);
void glVertexAttribPointer(unsigned int, int, unsigned int, unsigned char, int, const void*);
void glViewport(int, int, int, int);
void glClearColor(float, float, float, float);
void glClear(unsigned int);
void glDrawArrays(unsigned int, int, int);

// Exported function for consumer library
int engine_render_frame(void) {
    LOGI("🎮 [libengine_provider.so] Executing engine_render_frame()...");

    const char* vSrc =
        "#version 300 es\n"
        "layout(location = 0) in vec3 aPos;\n"
        "layout(location = 1) in vec3 aColor;\n"
        "out vec3 vColor;\n"
        "void main() {\n"
        "  gl_Position = vec4(aPos, 1.0);\n"
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

    glViewport(0, 0, 256, 256);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(0x4000);

    // Dữ liệu đỉnh: Tam giác RGB (Đỉnh 1: Đỏ, Đỉnh 2: Xanh lá, Đỉnh 3: Xanh dương)
    const float triVertices[] = {
         0.0f,  0.8f, 0.0f,   1.0f, 0.0f, 0.0f, // Top Red
        -0.8f, -0.8f, 0.0f,   0.0f, 1.0f, 0.0f, // Bottom-left Green
         0.8f, -0.8f, 0.0f,   0.0f, 0.0f, 1.0f  // Bottom-right Blue
    };

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, 0x1406, 0, 6 * sizeof(float), triVertices);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, 0x1406, 0, 6 * sizeof(float), &triVertices[3]);

    glDrawArrays(0x0004, 0, 3);
    LOGI("✔ [libengine_provider.so] glDrawArrays rendered Multi-Color RGB Triangle!");
    return 42; // Return confirmation code
}
