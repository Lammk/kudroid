typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned long size_t;
#define NULL ((void*)0)

// Bionic Android Log API
extern int __android_log_print(int priority, const char* tag, const char* fmt, ...);
#define LOG_TAG "TouchCube3D"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

// Bionic GLES2 / EGL types & function prototypes
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLNativeWindowType;

// Native Window & Input Queue
extern void* ANativeWindow_fromSurface(void* env, void* surface);
extern int ANativeWindow_getWidth(void* window);
extern int ANativeWindow_getHeight(void* window);

#define AINPUT_EVENT_TYPE_MOTION 2
#define AMOTION_EVENT_ACTION_DOWN 0
#define AMOTION_EVENT_ACTION_UP 1
#define AMOTION_EVENT_ACTION_MOVE 2
#define AMOTION_EVENT_ACTION_CANCEL 3

extern void* AInputQueue_create(void);
extern int32_t AInputQueue_getEvent(void* queue, void** outEvent);
extern int32_t AInputQueue_preDispatchEvent(void* queue, void* event);
extern int32_t AInputQueue_finishEvent(void* queue, void* event, int handled);
extern int32_t AInputEvent_getType(const void* event);
extern int32_t AMotionEvent_getAction(const void* event);
extern float AMotionEvent_getX(const void* event, size_t pointer_index);
extern float AMotionEvent_getY(const void* event, size_t pointer_index);
extern size_t AMotionEvent_getPointerCount(const void* event);
extern int usleep(unsigned int usec);

// EGL
extern EGLDisplay eglGetDisplay(void*);
extern unsigned int eglInitialize(EGLDisplay, int*, int*);
extern int eglChooseConfig(EGLDisplay, const int*, EGLConfig*, int, int*);
extern EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, EGLNativeWindowType, const int*);
extern EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const int*);
extern unsigned int eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
extern unsigned int eglSwapBuffers(EGLDisplay, EGLSurface);
extern unsigned int eglDestroySurface(EGLDisplay, EGLSurface);
extern unsigned int eglDestroyContext(EGLDisplay, EGLContext);
extern unsigned int eglTerminate(EGLDisplay);

// GLES2 Core
extern unsigned int glCreateShader(unsigned int);
extern void glShaderSource(unsigned int, int, const char* const*, const int*);
extern void glCompileShader(unsigned int);
extern void glGetShaderiv(unsigned int, unsigned int, int*);
extern unsigned int glCreateProgram(void);
extern void glAttachShader(unsigned int, unsigned int);
extern void glLinkProgram(unsigned int);
extern void glGetProgramiv(unsigned int, unsigned int, int*);
extern void glUseProgram(unsigned int);
extern int glGetAttribLocation(unsigned int, const char*);
extern int glGetUniformLocation(unsigned int, const char*);
extern void glUniform2f(int, float, float);
extern void glUniform1f(int, float);
extern void glEnableVertexAttribArray(unsigned int);
extern void glVertexAttribPointer(unsigned int, int, unsigned int, unsigned char, int, const void*);
extern void glViewport(int, int, int, int);
extern void glClearColor(float, float, float, float);
extern void glClearDepthf(float);
extern void glClear(unsigned int);
extern void glEnable(unsigned int);
extern void glDisable(unsigned int);
extern void glDepthFunc(unsigned int);
extern void glGenBuffers(int, unsigned int*);
extern void glBindBuffer(unsigned int, unsigned int);
extern void glBufferData(unsigned int, long, const void*, unsigned int);
extern void glDrawElements(unsigned int, int, unsigned int, const void*);

#define GL_DEPTH_TEST 0x0B71
#define GL_LEQUAL     0x0203
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_SHORT 0x1403

// Cube Vertex Structure: Position (3), Color (3)
typedef struct {
    float x, y, z;
    float r, g, b;
} Vertex3D;

static const Vertex3D kCubeVertices[24] = {
    // Front Face (Red / Orange)
    { -0.45f, -0.45f,  0.45f,  1.0f, 0.2f, 0.1f },
    {  0.45f, -0.45f,  0.45f,  1.0f, 0.5f, 0.0f },
    {  0.45f,  0.45f,  0.45f,  1.0f, 0.8f, 0.2f },
    { -0.45f,  0.45f,  0.45f,  1.0f, 0.3f, 0.4f },
    // Back Face (Deep Blue / Cyan)
    { -0.45f, -0.45f, -0.45f,  0.0f, 0.3f, 1.0f },
    { -0.45f,  0.45f, -0.45f,  0.1f, 0.7f, 1.0f },
    {  0.45f,  0.45f, -0.45f,  0.0f, 1.0f, 0.8f },
    {  0.45f, -0.45f, -0.45f,  0.2f, 0.1f, 0.9f },
    // Top Face (Neon Green)
    { -0.45f,  0.45f, -0.45f,  0.1f, 1.0f, 0.2f },
    { -0.45f,  0.45f,  0.45f,  0.6f, 1.0f, 0.0f },
    {  0.45f,  0.45f,  0.45f,  0.0f, 0.9f, 0.4f },
    {  0.45f,  0.45f, -0.45f,  0.3f, 1.0f, 0.1f },
    // Bottom Face (Gold / Yellow)
    { -0.45f, -0.45f, -0.45f,  0.9f, 0.8f, 0.0f },
    {  0.45f, -0.45f, -0.45f,  1.0f, 0.9f, 0.1f },
    {  0.45f, -0.45f,  0.45f,  0.8f, 0.6f, 0.0f },
    { -0.45f, -0.45f,  0.45f,  1.0f, 1.0f, 0.2f },
    // Right Face (Magenta / Purple)
    {  0.45f, -0.45f, -0.45f,  0.9f, 0.0f, 0.9f },
    {  0.45f,  0.45f, -0.45f,  1.0f, 0.2f, 0.7f },
    {  0.45f,  0.45f,  0.45f,  0.5f, 0.0f, 1.0f },
    {  0.45f, -0.45f,  0.45f,  0.8f, 0.1f, 0.6f },
    // Left Face (Teal / Emerald)
    { -0.45f, -0.45f, -0.45f,  0.0f, 0.8f, 0.8f },
    { -0.45f, -0.45f,  0.45f,  0.1f, 0.9f, 0.7f },
    { -0.45f,  0.45f,  0.45f,  0.2f, 1.0f, 0.5f },
    { -0.45f,  0.45f, -0.45f,  0.0f, 0.6f, 0.6f }
};

static const uint16_t kCubeIndices[36] = {
    0, 1, 2,  2, 3, 0,
    4, 5, 6,  6, 7, 4,
    8, 9, 10, 10, 11, 8,
    12, 13, 14, 14, 15, 12,
    16, 17, 18, 18, 19, 16,
    20, 21, 22, 22, 23, 20
};

static const char* kVertexShader =
    "attribute vec3 aPosition;\n"
    "attribute vec3 aColor;\n"
    "varying vec3 vColor;\n"
    "uniform vec2 uRotation;\n"
    "uniform float uAspect;\n"
    "void main() {\n"
    "    vColor = aColor;\n"
    "    float cx = cos(uRotation.x);\n"
    "    float sx = sin(uRotation.x);\n"
    "    float cy = cos(uRotation.y);\n"
    "    float sy = sin(uRotation.y);\n"
    "    // Rotate around Y-axis\n"
    "    vec3 p1 = vec3(\n"
    "        aPosition.x * cy + aPosition.z * sy,\n"
    "        aPosition.y,\n"
    "        -aPosition.x * sy + aPosition.z * cy\n"
    "    );\n"
    "    // Rotate around X-axis\n"
    "    vec3 p2 = vec3(\n"
    "        p1.x,\n"
    "        p1.y * cx - p1.z * sx,\n"
    "        p1.y * sx + p1.z * cx\n"
    "    );\n"
    "    // Perspective projection centered\n"
    "    float z = p2.z + 2.0;\n"
    "    gl_Position = vec4(p2.x, p2.y * uAspect, p2.z * 0.4, z);\n"
    "}\n";

static const char* kFragmentShader =
    "precision mediump float;\n"
    "varying vec3 vColor;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(vColor, 1.0);\n"
    "}\n";

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🎮 [INTERACTIVE 3D OPENGL CUBE WITH LIVE TOUCH]");
    LOGI("=================================================");

    void* win = ANativeWindow_fromSurface(NULL, NULL);
    int width = ANativeWindow_getWidth(win);
    int height = ANativeWindow_getHeight(win);
    LOGI("✔ Native Screen Resolution: %dx%d Retina", width, height);

    EGLDisplay dpy = eglGetDisplay(NULL);
    int maj = 0, min = 0;
    eglInitialize(dpy, &maj, &min);

    const int configAttribs[] = {
        0x3033, 0x0004, // EGL_WINDOW_BIT
        0x3040, 0x0004, // EGL_OPENGL_ES2_BIT
        0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8,
        0x3025, 24,     // EGL_DEPTH_SIZE 24-bit
        0x3038
    };
    EGLConfig config = NULL;
    int numConfigs = 0;
    eglChooseConfig(dpy, configAttribs, &config, 1, &numConfigs);

    EGLSurface winSurf = eglCreateWindowSurface(dpy, config, win, NULL);
    const int ctxAttribs[] = { 0x3098, 2, 0x3038 };
    EGLContext ctx = eglCreateContext(dpy, config, NULL, ctxAttribs);
    eglMakeCurrent(dpy, winSurf, winSurf, ctx);

    // Compile Shaders
    unsigned int vs = glCreateShader(0x8B31);
    glShaderSource(vs, 1, &kVertexShader, NULL);
    glCompileShader(vs);

    unsigned int fs = glCreateShader(0x8B30);
    glShaderSource(fs, 1, &kFragmentShader, NULL);
    glCompileShader(fs);

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glUseProgram(prog);

    int posLoc = glGetAttribLocation(prog, "aPosition");
    int colLoc = glGetAttribLocation(prog, "aColor");
    int rotLoc = glGetUniformLocation(prog, "uRotation");
    int aspectLoc = glGetUniformLocation(prog, "uAspect");

    // Correct aspect ratio for portrait screen: Width / Height
    float aspect = (float)width / (float)(height > 0 ? height : 1);
    glUniform1f(aspectLoc, aspect);

    // Setup Buffers
    unsigned int vbo, ebo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVertices), kCubeVertices, GL_STATIC_DRAW);

    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCubeIndices), kCubeIndices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 3, GL_FLOAT, 0, sizeof(Vertex3D), (void*)0);

    glEnableVertexAttribArray(colLoc);
    glVertexAttribPointer(colLoc, 3, GL_FLOAT, 0, sizeof(Vertex3D), (void*)(3 * sizeof(float)));

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glViewport(0, 0, width, height);

    void* inputQueue = AInputQueue_create();
    LOGI("=================================================");
    LOGI("🌟 3D CUBE IS CENTERED & LIVE ON SCREEN! (60 FPS)");
    LOGI("👉 SWIPE YOUR FINGER ON SCREEN TO ROTATE 3D CUBE!");
    LOGI("=================================================");

    float rotX = 0.5f;
    float rotY = 0.5f;
    float lastTouchX = 0.0f;
    float lastTouchY = 0.0f;
    int isTouching = 0;
    uint32_t totalTouchEvents = 0;

    // 25 seconds loop @ ~60 FPS (1500 frames)
    for (int frame = 0; frame < 1500; ++frame) {
        // 1. Process Multi-Touch Input
        if (inputQueue) {
            void* event = NULL;
            while (AInputQueue_getEvent(inputQueue, &event) == 0 && event != NULL) {
                if (AInputQueue_preDispatchEvent(inputQueue, event)) continue;

                if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
                    int32_t action = AMotionEvent_getAction(event);
                    float curX = AMotionEvent_getX(event, 0);
                    float curY = AMotionEvent_getY(event, 0);

                    if (action == AMOTION_EVENT_ACTION_DOWN) {
                        isTouching = 1;
                        lastTouchX = curX;
                        lastTouchY = curY;
                        totalTouchEvents++;
                    } else if (action == AMOTION_EVENT_ACTION_MOVE && isTouching) {
                        float dx = curX - lastTouchX;
                        float dy = curY - lastTouchY;
                        rotY += dx * 0.010f;
                        rotX += dy * 0.010f;
                        lastTouchX = curX;
                        lastTouchY = curY;
                        totalTouchEvents++;
                    } else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
                        isTouching = 0;
                    }
                }
                AInputQueue_finishEvent(inputQueue, event, 1);
                event = NULL;
            }
        }

        // Auto spin if not touched
        if (!isTouching) {
            rotY += 0.020f;
            rotX += 0.012f;
        }

        // 2. Render 3D Cube
        glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        glClearDepthf(1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUniform2f(rotLoc, rotX, rotY);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, (void*)0);

        eglSwapBuffers(dpy, winSurf);
        usleep(16666); // ~60 FPS
    }

    LOGI("=================================================");
    LOGI("🎉 3D TOUCH CUBE TEST COMPLETED!");
    LOGI("📊 Total Interactive Touch Gestures Processed: %u", totalTouchEvents);
    LOGI("=================================================");

    eglMakeCurrent(dpy, NULL, NULL, NULL);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, winSurf);
    eglTerminate(dpy);
    return 0;
}
