#pragma once

#include <string>

namespace kudroid {

// Verify a pinned replacement before loading any of its code.
std::string selectConstrainedFlutterRuntime(const std::string& originalPath,
                                           const std::string& runtimeDirectory,
                                           std::string& reason);

std::string bundledFlutterRuntimeDirectory();

} // namespace kudroid
