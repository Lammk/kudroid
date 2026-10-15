#pragma once

#include <string>
#include <vector>

namespace kudroid {

// args excludes argv[0]. Returns 0 for successful scans/help, 1 when one or more
// inputs cannot be read or parsed, and 2 for invalid arguments or API misuse.
int resource_compat_cli(const std::vector<std::string>& args,
                        std::string* output, std::string* error);

}  // namespace kudroid
