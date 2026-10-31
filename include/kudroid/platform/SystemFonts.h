#pragma once

#include <string>

namespace kudroid {

std::string bundledSystemFontsPath();
bool provisionSystemFonts(const std::string& androidRoot, const std::string& source,
                          std::string& error);

} // namespace kudroid
