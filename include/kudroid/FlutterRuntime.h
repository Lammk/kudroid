#pragma once

#include <string>

namespace kudroid {

// Verify a pinned replacement before loading any of its code.
bool isConstrainedFlutterEligible(const std::string& originalPath,
                                 const std::string& candidatePath,
                                 std::string& reason);

std::string bundledFlutterRuntimePath();

} // namespace kudroid
