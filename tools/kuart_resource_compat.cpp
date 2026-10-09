#include "kudroid/ResourceCompat.h"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    std::string output;
    std::string error;
    const int status = kudroid::resource_compat_cli(args, &output, &error);
    if (!output.empty()) std::fwrite(output.data(), 1, output.size(), stdout);
    if (!error.empty()) {
        std::fwrite(error.data(), 1, error.size(), stderr);
        if (error.back() != '\n') std::fputc('\n', stderr);
    }
    return status;
}
