#include "kudroid/platform/Es31Compat.h"

#include <cstddef>
#include <mutex>
#include <set>
#include <string_view>

namespace kudroid {
namespace {

constexpr unsigned kGlVersion = 0x1F02;
constexpr unsigned kGlslVersion = 0x8B8C;

bool is_ident(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool has_word(std::string_view text, std::string_view word) {
    for (size_t at = text.find(word); at != std::string_view::npos; at = text.find(word, at + 1)) {
        const bool leftOk = at == 0 || !is_ident(text[at - 1]);
        const size_t end = at + word.size();
        const bool rightOk = end >= text.size() || !is_ident(text[end]);
        if (leftOk && rightOk) return true;
    }
    return false;
}

bool has_text(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

// Position just past the leading "#version 3x0 es" token, or npos when the shader
// does not start with one. Leading blank space is allowed.
size_t version_token_end(std::string_view source, size_t* begin) {
    size_t at = 0;
    while (at < source.size() && (source[at] == ' ' || source[at] == '\t' ||
                                  source[at] == '\r' || source[at] == '\n')) {
        ++at;
    }
    for (std::string_view tag : {std::string_view("#version 310 es"),
                                 std::string_view("#version 320 es")}) {
        if (source.compare(at, tag.size(), tag) == 0) {
            const size_t end = at + tag.size();
            if (end < source.size() && is_ident(source[end])) return std::string_view::npos;
            *begin = at;
            return end;
        }
    }
    return std::string_view::npos;
}

const char* intern(std::string value) {
    static std::mutex mutex;
    static std::set<std::string> strings;
    std::lock_guard<std::mutex> lock(mutex);
    return strings.insert(std::move(value)).first->c_str();
}

}  // namespace

const char* gles31_unsupported_feature(std::string_view source) {
    // "buffer" is reserved in 3.0 GLSL, so any use is a storage block.
    if (has_word(source, "buffer")) return "buffer";
    if (has_text(source, "local_size_")) return "compute";
    for (const char* image : {"image2D", "image3D", "imageCube", "image2DArray", "imageBuffer"}) {
        if (has_text(source, image)) return "image";
    }
    if (has_text(source, "atomic_uint") || has_text(source, "atomicCounter") ||
        has_text(source, "atomicAdd") || has_text(source, "atomicMax") ||
        has_text(source, "atomicMin") || has_text(source, "atomicExchange")) {
        return "atomic";
    }
    if (has_word(source, "textureGather") || has_word(source, "textureGatherOffset") ||
        has_word(source, "textureGatherOffsets")) {
        return "textureGather";
    }
    for (const char* type : {"samplerCubeArray", "samplerBuffer", "sampler2DMS",
                             "sampler2DMSArray", "samplerCubeArrayShadow"}) {
        if (has_word(source, type)) return "sampler";
    }
    return nullptr;
}

ShaderDowngrade gles31_downgrade_shader(std::string& source) {
    size_t begin = 0;
    const size_t end = version_token_end(source, &begin);
    if (end == std::string_view::npos) return ShaderDowngrade::NotApplicable;
    if (gles31_unsupported_feature(source)) return ShaderDowngrade::Unsupported;
    source.replace(begin, end - begin, "#version 300 es");
    return ShaderDowngrade::Downgraded;
}

const char* gles31_advertise_version(const char* value, unsigned name) {
    if (!value || (name != kGlVersion && name != kGlslVersion)) return value;
    std::string text(value);
    if (name == kGlVersion) {
        constexpr std::string_view from = "OpenGL ES 3.0";
        if (text.compare(0, from.size(), from) != 0) return value;
        if (text.size() > from.size() && is_ident(text[from.size()])) return value;
        text[from.size() - 1] = '1';
    } else {
        constexpr std::string_view from = "OpenGL ES GLSL ES 3.00";
        if (text.compare(0, from.size(), from) != 0) return value;
        if (text.size() > from.size() && is_ident(text[from.size()])) return value;
        text[from.size() - 2] = '1';
    }
    return intern(std::move(text));
}

bool gles31_package_listed(std::string_view listText, std::string_view pkg) {
    if (pkg.empty()) return false;
    size_t start = 0;
    while (start <= listText.size()) {
        size_t end = listText.find('\n', start);
        if (end == std::string_view::npos) end = listText.size();
        std::string_view line = listText.substr(start, end - start);
        const size_t hash = line.find('#');
        if (hash != std::string_view::npos) line = line.substr(0, hash);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t' || line.front() == '\r'))
            line.remove_prefix(1);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
            line.remove_suffix(1);
        if (line == pkg) return true;
        if (end >= listText.size()) break;
        start = end + 1;
    }
    return false;
}

}  // namespace kudroid
