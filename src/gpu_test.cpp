#include "kudroid/elf_loader.hpp"
#include "kudroid/VFSPathRemapper.h"
#include <string>
#include <filesystem>
#include <vector>
#include <cstring>

extern "C" const char* kudroid_test_gpu(void) {
    std::string log;
    log += "[kudroid_gpu] Starting Native GPU (OpenGL/Vulkan) Test...\n";

    kudroid::LibraryManager manager;
    auto& remapper = kudroid::VFSPathRemapper::getInstance();
    std::filesystem::path libDir = std::filesystem::path(remapper.androidRoot()) / "system/lib64";
    
    // Ensure the directory exists
    std::error_code ec;
    std::filesystem::create_directories(libDir, ec);
    
    if (!std::filesystem::exists(libDir)) {
        log += "[kudroid_gpu] ERROR: " + libDir.string() + " does not exist and could not be created.\n";
        return strdup(log.c_str());
    }

    bool vulkanLoaded = false;
    std::string vulkanPath = (libDir / "libvulkan.so").string();
    if (std::filesystem::exists(vulkanPath)) {
        log += "[kudroid_gpu] Found libvulkan.so, attempting to load...\n";
        if (manager.loadRecursive(vulkanPath.c_str())) {
            log += "[kudroid_gpu] SUCCESS: Loaded libvulkan.so\n";
            vulkanLoaded = true;
            auto vkCreateInstance = manager.resolveAppSymbol("vkCreateInstance");
            if (vkCreateInstance) {
                log += "[kudroid_gpu] SUCCESS: Resolved vkCreateInstance at " + std::to_string(reinterpret_cast<uintptr_t>(vkCreateInstance)) + "\n";
            } else {
                log += "[kudroid_gpu] WARNING: vkCreateInstance not found in libvulkan.so!\n";
            }
        } else {
            log += "[kudroid_gpu] ERROR: Failed to load libvulkan.so\n";
        }
    }

    bool glesLoaded = false;
    std::string glesPath = (libDir / "libGLESv2.so").string();
    if (std::filesystem::exists(glesPath)) {
        log += "[kudroid_gpu] Found libGLESv2.so, attempting to load...\n";
        if (manager.loadRecursive(glesPath.c_str())) {
            log += "[kudroid_gpu] SUCCESS: Loaded libGLESv2.so\n";
            glesLoaded = true;
            auto glGetString = manager.resolveAppSymbol("glGetString");
            if (glGetString) {
                log += "[kudroid_gpu] SUCCESS: Resolved glGetString at " + std::to_string(reinterpret_cast<uintptr_t>(glGetString)) + "\n";
            } else {
                log += "[kudroid_gpu] WARNING: glGetString not found in libGLESv2.so!\n";
            }
        } else {
            log += "[kudroid_gpu] ERROR: Failed to load libGLESv2.so\n";
        }
    }
    
    std::string eglPath = (libDir / "libEGL.so").string();
    if (std::filesystem::exists(eglPath)) {
        log += "[kudroid_gpu] Found libEGL.so, attempting to load...\n";
        if (manager.loadRecursive(eglPath.c_str())) {
            log += "[kudroid_gpu] SUCCESS: Loaded libEGL.so\n";
            auto eglInitialize = manager.resolveAppSymbol("eglInitialize");
            if (eglInitialize) {
                log += "[kudroid_gpu] SUCCESS: Resolved eglInitialize at " + std::to_string(reinterpret_cast<uintptr_t>(eglInitialize)) + "\n";
            } else {
                log += "[kudroid_gpu] WARNING: eglInitialize not found in libEGL.so!\n";
            }
        } else {
            log += "[kudroid_gpu] ERROR: Failed to load libEGL.so\n";
        }
    }

    if (!vulkanLoaded && !glesLoaded) {
        log += "[kudroid_gpu] WARNING: No GPU libraries (libvulkan.so, libGLESv2.so) found in system/lib64.\n";
        log += "[kudroid_gpu] Please place the ANGLE or MoltenVK .so files into:\n";
        log += "[kudroid_gpu] " + libDir.string() + "\n";
    }

    log += "[kudroid_gpu] GPU Test Completed.\n";
    return strdup(log.c_str());
}
