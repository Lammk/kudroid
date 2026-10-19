#pragma once

#include <cstdint>
#include <string>

namespace kudroid {

// Read-only table of the Android framework's own resources (package 0x01), generated from
// the AOSP framework values by scripts/gen_framework_resources.py. It answers references
// such as @android:color/white and Resources.getIdentifier(name, type, "android"): an
// application's resources.arsc never contains them, because framework-res is not shipped.
//
// Covered: default-configuration color/dimen/integer/bool/fraction scalars, a few public
// strings, and the name of every public resource. File-backed resources (drawables,
// layouts) and styles have a name and an id but no value here.

// package 0x01 with a non-zero type byte; 0x0100xxxx are internal bag keys, not resources.
inline bool framework_resource_is_id(uint32_t id) {
    return (id >> 24) == 0x01u && ((id >> 16) & 0xffu) != 0;
}

// Res_value type/data of a scalar entry.
bool framework_resource_scalar(uint32_t id, uint8_t* type, uint32_t* data);
// Default-locale text of a string entry.
bool framework_resource_string(uint32_t id, std::string* text);
// "color", "white" for 0x0106000b.
bool framework_resource_name(uint32_t id, std::string* type, std::string* entry);
// Inverse of framework_resource_name.
bool framework_resource_find(const std::string& type, const std::string& entry, uint32_t* id);

}  // namespace kudroid
