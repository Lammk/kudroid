#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace kudroid {

/// Convert a DEX file into a JAR containing class stubs.
///
/// This parses the DEX structure (classes, methods, fields) and produces a
/// JAR with one .class file per class. Each class has:
///   - a default constructor
///   - all methods declared in the DEX, with correct signatures but EMPTY
///     bodies (returning default values)
///
/// This is enough for apps to load Java at startup without crashing (the
/// classes and methods resolve), while the actual method logic is left empty
/// until real bytecode translation is implemented (driven by GitHub issues).
///
/// The output is a JAR (ZIP) that can be fed to the Avian JVM as a classpath.
class DexToJar {
public:
    /// Parsed class data.
    struct ClassInfo {
        std::string name;          // JVM internal name, e.g. "com/foo/Bar"
        std::string superName;     // JVM internal name of superclass
        uint32_t accessFlags;
        std::vector<std::string> interfaces; // JVM internal names
        // Methods: {name, descriptor}
        std::vector<std::pair<std::string, std::string>> methods;
        // Fields: {name, descriptor}
        std::vector<std::pair<std::string, std::string>> fields;
    };

    /// Convert a DEX file to a JAR. Returns true on success.
    /// On success, `outJar` is filled with the JAR bytes.
    static bool convert(const std::string& dexPath, std::vector<uint8_t>& outJar,
                        std::string* error = nullptr);

    /// Convert DEX bytes to a JAR. Returns true on success.
    static bool convertBytes(const std::vector<uint8_t>& dexBytes,
                             std::vector<uint8_t>& outJar,
                             std::string* error = nullptr);

private:
    // Internal helpers (implemented in DexToJar.cpp)
    static bool parseDex(const std::vector<uint8_t>& dex, std::vector<ClassInfo>& classes,
                         std::string* error);
    static bool buildJar(const std::vector<ClassInfo>& classes,
                         std::vector<uint8_t>& outJar, std::string* error);
};

} // namespace kudroid
