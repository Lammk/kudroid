#pragma once

#include <string>

namespace kudroid {

class APKExtractor {
public:
    static bool extract_native_libs(const std::string& apkPath,
                                    const std::string& targetDirectory);
    static const std::string& lastError();
};

} // namespace kudroid
