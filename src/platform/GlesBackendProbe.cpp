#include "kudroid/platform/GlesBackendProbe.h"
#include "kudroid/platform/GraphicsShim.h"
#include <GLES3/gl31.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <vulkan/vulkan.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

namespace kudroid {
std::string probe_vulkan_features(void* display) {
    const auto queryDisplay = reinterpret_cast<PFNEGLQUERYDISPLAYATTRIBEXTPROC>(
        get_egl_func("eglQueryDisplayAttribEXT"));
    const auto queryDevice = reinterpret_cast<PFNEGLQUERYDEVICEATTRIBEXTPROC>(
        get_egl_func("eglQueryDeviceAttribEXT"));
    EGLAttrib device = 0, physical = 0;
    if (!queryDisplay || !queryDevice || !queryDisplay(display, EGL_DEVICE_EXT, &device) ||
        !queryDevice(reinterpret_cast<EGLDeviceEXT>(device), EGL_VULKAN_PHYSICAL_DEVICE_ANGLE, &physical))
        return "native Vulkan device query unavailable";
    const auto getProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        get_vk_func("vkGetPhysicalDeviceProperties"));
    const auto getFeatures = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        get_vk_func("vkGetPhysicalDeviceFeatures2"));
    const auto enumerate = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        get_vk_func("vkEnumerateDeviceExtensionProperties"));
    if (!getProperties || !getFeatures || !enumerate) return "native Vulkan feature entry points unavailable";
    const auto handle = reinterpret_cast<VkPhysicalDevice>(physical);
    uint32_t count = 0;
    if (enumerate(handle, nullptr, &count, nullptr) != VK_SUCCESS) return "Vulkan extension count failed";
    std::vector<VkExtensionProperties> extensions(count);
    if (enumerate(handle, nullptr, &count, extensions.data()) != VK_SUCCESS)
        return "Vulkan extension enumeration failed";
    if (count > extensions.size()) return "Vulkan extension count changed during enumeration";
    const auto has = [&](const char* name) {
        for (uint32_t i = 0; i < count; ++i)
            if (std::strcmp(extensions[i].extensionName, name) == 0) return true;
        return false;
    };
    VkPhysicalDeviceProvokingVertexFeaturesEXT provoking{};
    provoking.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT;
    VkPhysicalDeviceTransformFeedbackFeaturesEXT feedback{};
    feedback.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    if (has(VK_EXT_PROVOKING_VERTEX_EXTENSION_NAME)) features.pNext = &provoking;
    if (has(VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME)) {
        feedback.pNext = features.pNext;
        features.pNext = &feedback;
    }
    getFeatures(handle, &features);
    VkPhysicalDeviceProperties properties{};
    getProperties(handle, &properties);
    std::ostringstream report;
    report << "device=" << properties.deviceName
           << " storageBuffers=" << properties.limits.maxPerStageDescriptorStorageBuffers
           << " vertexOffset=" << properties.limits.maxVertexInputAttributeOffset
           << " uniformBuffers=" << properties.limits.maxPerStageDescriptorUniformBuffers
           << " vertexOutputs=" << properties.limits.maxVertexOutputComponents
           << " standardSamples=" << properties.limits.standardSampleLocations
           << " independentBlend=" << features.features.independentBlend
           << " vertexStores=" << features.features.vertexPipelineStoresAndAtomics
           << " transformFeedback=" << feedback.transformFeedback
           << " provokingLast=" << provoking.provokingVertexLast
           << " geometry=" << features.features.geometryShader
           << " tessellation=" << features.features.tessellationShader
           << " portabilitySubset=" << has("VK_KHR_portability_subset");
    return report.str();
}

std::string probe_gles31() {
#define LOAD_GL(name) \
    const auto name = reinterpret_cast<decltype(&::name)>(get_gl_func(#name)); \
    if (!name) return "missing " #name
    LOAD_GL(glGetString);
    int major = 0, minor = 0;
    const auto version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (!version || std::sscanf(version, "OpenGL ES %d.%d", &major, &minor) != 2)
        return "unrecognized GLES version";
    if (major < 3 || (major == 3 && minor < 1)) return std::string("GLES 3.1 unavailable: ") + version;

    LOAD_GL(glCreateShader); LOAD_GL(glShaderSource); LOAD_GL(glCompileShader);
    LOAD_GL(glGetShaderiv); LOAD_GL(glGetShaderInfoLog); LOAD_GL(glDeleteShader);
    LOAD_GL(glCreateProgram); LOAD_GL(glAttachShader); LOAD_GL(glLinkProgram);
    LOAD_GL(glGetProgramiv); LOAD_GL(glGetProgramInfoLog); LOAD_GL(glDeleteProgram);
    LOAD_GL(glUseProgram); LOAD_GL(glGenVertexArrays); LOAD_GL(glBindVertexArray);
    LOAD_GL(glDeleteVertexArrays); LOAD_GL(glViewport); LOAD_GL(glDrawArrays);
    LOAD_GL(glReadPixels); LOAD_GL(glGenBuffers); LOAD_GL(glBindBuffer);
    LOAD_GL(glBufferData); LOAD_GL(glBindBufferBase); LOAD_GL(glDispatchCompute);
    LOAD_GL(glMemoryBarrier); LOAD_GL(glMapBufferRange); LOAD_GL(glUnmapBuffer);
    LOAD_GL(glDeleteBuffers); LOAD_GL(glGetError);
    LOAD_GL(glFenceSync); LOAD_GL(glClientWaitSync); LOAD_GL(glDeleteSync);
#undef LOAD_GL

    std::array<GLuint, 3> shaders{};
    std::array<GLuint, 2> programs{};
    GLuint vao = 0, buffer = 0;
    const auto cleanup = [&] {
        glUseProgram(0);
        glBindVertexArray(0);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (buffer) glDeleteBuffers(1, &buffer);
        for (GLuint program : programs) if (program) glDeleteProgram(program);
        for (GLuint shader : shaders) if (shader) glDeleteShader(shader);
    };
    const auto fail = [&](const std::string& message) {
        cleanup();
        return message;
    };
    const auto waitForGpu = [&] {
        GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (!fence) return false;
        const GLenum status = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 5000000000ULL);
        glDeleteSync(fence);
        return status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED;
    };
    const char* sources[] = {
        "#version 310 es\n"
        "const vec2 points[3] = vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));\n"
        "void main(){gl_Position=vec4(points[gl_VertexID],0,1);}\n",
        "#version 310 es\nprecision highp float;\n"
        "layout(location=0) out vec4 color;\n"
        "void main(){color=vec4(0.25,0.5,0.75,1.0);}\n",
        "#version 310 es\nlayout(local_size_x=1) in;\n"
        "layout(std430,binding=0) buffer Result {uint value;};\n"
        "void main(){value=0x12345678u;}\n"
    };
    constexpr GLenum types[] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER, GL_COMPUTE_SHADER};
    for (size_t i = 0; i < shaders.size(); ++i) {
        shaders[i] = glCreateShader(types[i]);
        if (!shaders[i]) return fail("shader allocation failed");
        glShaderSource(shaders[i], 1, &sources[i], nullptr);
        glCompileShader(shaders[i]);
        GLint status = GL_FALSE;
        glGetShaderiv(shaders[i], GL_COMPILE_STATUS, &status);
        if (!status) {
            char message[1024]{};
            glGetShaderInfoLog(shaders[i], sizeof(message), nullptr, message);
            return fail("ESSL 310 compile failed: " + std::string(message));
        }
    }
    for (size_t i = 0; i < programs.size(); ++i) {
        programs[i] = glCreateProgram();
        if (!programs[i]) return fail("program allocation failed");
        if (i == 0) {
            glAttachShader(programs[i], shaders[0]);
            glAttachShader(programs[i], shaders[1]);
        } else glAttachShader(programs[i], shaders[2]);
        glLinkProgram(programs[i]);
        GLint status = GL_FALSE;
        glGetProgramiv(programs[i], GL_LINK_STATUS, &status);
        if (!status) {
            char message[1024]{};
            glGetProgramInfoLog(programs[i], sizeof(message), nullptr, message);
            return fail("ESSL 310 link failed: " + std::string(message));
        }
    }
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glUseProgram(programs[0]);
    glViewport(0, 0, 16, 16);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!waitForGpu()) return fail("draw GPU wait failed or timed out");
    std::array<GLubyte, 4> pixel{};
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    constexpr int expected[] = {64, 128, 191, 255};
    for (size_t i = 0; i < pixel.size(); ++i)
        if (std::abs(int(pixel[i]) - expected[i]) > 1) return fail("GLES 3.1 draw readback failed");

    glGenBuffers(1, &buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    GLuint initial = 0;
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(initial), &initial, GL_DYNAMIC_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);
    glUseProgram(programs[1]);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    if (!waitForGpu()) return fail("compute GPU wait failed or timed out");
    const auto mapped = static_cast<const GLuint*>(
        glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(initial), GL_MAP_READ_BIT));
    if (!mapped) return fail("compute buffer mapping failed");
    const bool correct = *mapped == 0x12345678u;
    const bool intact = glUnmapBuffer(GL_SHADER_STORAGE_BUFFER) == GL_TRUE;
    if (!correct || !intact) return fail("GLES 3.1 compute readback failed");
    const GLenum error = glGetError();
    cleanup();
    if (error != GL_NO_ERROR) return "GLES 3.1 probe GL error=" + std::to_string(error);
    return std::string("PASS ESSL 310 draw and compute readback; ") + version;
}
}
