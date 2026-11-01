#include "kudroid/FlutterRuntime.h"
#include "kudroid/elf_loader.hpp"
#include "kudroid/BionicShim.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
std::vector<std::string> mappedPaths;
int initializations = 0;
bool failMapping = false;
int symbolToken = 0;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

// Model mapping without executing ARM64 code on the host.
namespace kudroid {
ElfLoader::ElfLoader(std::string path) : path_(std::move(path)) {}
ElfLoader::~ElfLoader() = default;
bool ElfLoader::parse() { return std::filesystem::exists(path_); }
bool ElfLoader::map() {
    if (failMapping) return false;
    mappedPaths.push_back(path_);
    return true;
}
bool ElfLoader::relocate() { return true; }
void ElfLoader::registerEhFrame() {}
void ElfLoader::releaseFileMapping() {}
void ElfLoader::executeInit() { ++initializations; }
const char* ElfLoader::lastError() const { return "fixture missing"; }
void* ElfLoader::getSymbolAddress(const char*) { return &symbolToken; }
void* resolve_bionic_symbol(const char*, bool) { return nullptr; }
bool is_universal_dummy(const void*) { return false; }
}
extern "C" int kudroid_android_log_message(int, const char*, const char*) { return 0; }

int main(int argc, char** argv) {
    try {
        require(argc >= 5, "expected mode, original, candidate and expected result");
        const std::string mode = argv[1], original = argv[2], candidate = argv[3];
        const std::string runtime = std::filesystem::path(candidate).parent_path().parent_path().string();
        const bool expected = std::string(argv[4]) == "1";
        std::string reason;
        if (mode == "selector") {
            const std::string selected = kudroid::selectConstrainedFlutterRuntime(original, runtime, reason);
            std::cout << reason << '\n';
            require(!selected.empty() == expected, "unexpected selector result");
            if (expected) require(selected == candidate, "wrong catalog candidate");
        } else if (mode == "map-failure") {
            require(argc > 5, "expected another original engine path");
            ::setenv("KUDROID_FLUTTER_RUNTIME_DIRECTORY", runtime.c_str(), 1);
            kudroid::LibraryManager manager;
            failMapping = true;
            require(!manager.loadRecursive(original), "mapping failure was ignored");
            require(manager.libraries().empty() && initializations == 0, "failed loader remained visible");
            failMapping = false;
            require(manager.loadRecursive(argv[5]), "failed-load aliases prevented a fresh selection");
            require(manager.libraries().size() == 1 && initializations == 1, "failed selection initialized code");
        } else if (mode == "loader") {
            ::setenv("KUDROID_FLUTTER_RUNTIME_DIRECTORY", runtime.c_str(), 1);
            kudroid::LibraryManager manager;
            require(manager.loadRecursive(original), "initial load failed");
            const auto entries = manager.libraries();
            const std::string originalKey = std::filesystem::weakly_canonical(original).string();
            require(entries.size() == 1 && entries[0].first == originalKey, "original identity lost");
            require(mappedPaths.size() == 1 && mappedPaths[0] == (expected ? candidate : originalKey),
                    "wrong physical engine selected");
            require(manager.resolveSymbolInLib(originalKey, "JNI_OnLoad") == &symbolToken,
                    "absolute-path JNI lookup failed");
            require(manager.resolveSymbolInLib(original, "JNI_OnLoad") == &symbolToken,
                    "requested-path JNI lookup failed");
            require(manager.resolveSymbolInLib("libflutter.so", "JNI_OnLoad") == &symbolToken,
                    "soname JNI lookup failed");
            require(manager.loadRecursive(original), "repeated absolute load failed");
            if (expected) {
                require(manager.loadRecursive("libflutter.so"), "soname alias load failed");
                require(manager.loadRecursive(candidate), "physical alias load failed");
                if (argc > 5) require(!manager.loadRecursive(argv[5]), "resident engine switched applications");
            }
            require(manager.libraries().size() == 1 && mappedPaths.size() == 1 && initializations == 1,
                    "alias caused duplicate ownership or initialization");
        } else {
            throw std::runtime_error("unknown test mode");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
