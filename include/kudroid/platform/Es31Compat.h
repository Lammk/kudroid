#pragma once

#include <string>
#include <string_view>

namespace kudroid {

// GLES 3.1 compatibility for a 3.0 host driver (ANGLE over Metal stops at 3.0).
//
// Some engines ship only "#version 310 es" shaders and pick them by the version the
// driver reports. For an opted-in package the reported version is raised to 3.1 and
// every 3.1 shader that is expressible in 3.0 is rewritten to "#version 300 es".
// Shaders that need real 3.1 features are never touched, so they fail exactly as
// they would without the shim instead of failing in a different way.

enum class ShaderDowngrade {
    NotApplicable,  // not a 3.1/3.2 shader; left as is
    Downgraded,     // rewritten to "#version 300 es"
    Unsupported,    // needs a real 3.1 feature; left as is
};

// Rewrites `source` in place when it is a 3.1/3.2 shader with nothing 3.0 lacks.
ShaderDowngrade gles31_downgrade_shader(std::string& source);

// First token that makes a shader impossible to express in 3.0, or nullptr.
const char* gles31_unsupported_feature(std::string_view source);

// "OpenGL ES 3.0 ..." -> "OpenGL ES 3.1 ...", "... GLSL ES 3.00 ..." -> "... 3.10 ...".
// `name` is GL_VERSION or GL_SHADING_LANGUAGE_VERSION; anything else, or a string that
// is not exactly 3.0, comes back unchanged. The result is stable for the process.
const char* gles31_advertise_version(const char* value, unsigned name);

// True when `pkg` is listed in `listText` (one package per line, '#' starts a comment).
bool gles31_package_listed(std::string_view listText, std::string_view pkg);

}  // namespace kudroid
