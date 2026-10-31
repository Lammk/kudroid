#include "kudroid/platform/SystemFonts.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unistd.h>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace kudroid {
namespace {

bool installMissing(const std::filesystem::path& source, const std::filesystem::path& target,
                    std::string& error) {
    std::error_code ec;
    if (std::filesystem::exists(target, ec)) return true;
    if (!ec) std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) { error = ec.message(); return false; }
    std::string temporary = target.string() + ".XXXXXX";
    const int fd = ::mkstemp(temporary.data());
    if (fd < 0) { error = std::strerror(errno); return false; }
    ::close(fd);
    std::filesystem::copy_file(source, temporary, std::filesystem::copy_options::overwrite_existing, ec);
    bool ok = !ec;
    if (ec) error = ec.message();
    // Publish complete files without replacing an existing configuration.
    if (ok && ::link(temporary.c_str(), target.c_str()) != 0 && errno != EEXIST) {
        error = std::strerror(errno);
        ok = false;
    }
    ::unlink(temporary.c_str());
    return ok;
}

} // namespace

std::string bundledSystemFontsPath() {
#if defined(__APPLE__)
    CFURLRef url = CFBundleCopyResourceURL(CFBundleGetMainBundle(), CFSTR("system"), nullptr, nullptr);
    if (!url) return {};
    std::array<UInt8, 4096> path {};
    const bool valid = CFURLGetFileSystemRepresentation(url, true, path.data(), path.size());
    CFRelease(url);
    if (valid) return reinterpret_cast<const char*>(path.data());
#endif
    return {};
}

bool provisionSystemFonts(const std::string& androidRoot, const std::string& source,
                          std::string& error) {
    error.clear();
    const auto target = std::filesystem::path(androidRoot) / "system";
    // The font must exist before its configuration becomes visible to native readers.
    for (const char* relative : {"fonts/NotoSans.ttf", "NOTO_LICENSE.txt", "etc/fonts.xml"}) {
        if (!installMissing(std::filesystem::path(source) / relative, target / relative, error)) {
            error = std::string(relative) + ": " + error;
            return false;
        }
    }
    return true;
}

} // namespace kudroid
