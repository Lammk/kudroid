#include "kudroid/FrameworkResources.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <unordered_map>

namespace kudroid {
namespace {

#include "FrameworkResourcesData.inc"

template <typename Entry, size_t N>
const Entry* FindById(const Entry (&table)[N], uint32_t id) {
    const Entry* end = table + N;
    const Entry* it = std::lower_bound(
        table, end, id, [](const Entry& entry, uint32_t value) { return entry.id < value; });
    return it != end && it->id == id ? it : nullptr;
}

const char* TypeName(uint32_t id) {
    const uint32_t type = (id >> 16) & 0xffu;
    constexpr size_t kTypes = sizeof(kFrameworkTypeNames) / sizeof(kFrameworkTypeNames[0]);
    return type < kTypes ? kFrameworkTypeNames[type] : nullptr;
}

// "type/entry" -> id, built once on first lookup.
const std::unordered_map<std::string, uint32_t>& NameIndex() {
    static const std::unordered_map<std::string, uint32_t> index = [] {
        std::unordered_map<std::string, uint32_t> built;
        built.reserve(sizeof(kFrameworkNames) / sizeof(kFrameworkNames[0]) * 2);
        for (const FrameworkNameEntry& entry : kFrameworkNames) {
            const char* type = TypeName(entry.id);
            if (type == nullptr) continue;
            built.emplace(std::string(type) + "/" + (kFrameworkText + entry.offset), entry.id);
        }
        return built;
    }();
    return index;
}

}  // namespace

bool framework_resource_scalar(uint32_t id, uint8_t* type, uint32_t* data) {
    if (!framework_resource_is_id(id)) return false;
    const FrameworkScalarEntry* entry = FindById(kFrameworkScalars, id);
    if (entry == nullptr) return false;
    if (type != nullptr) *type = entry->type;
    if (data != nullptr) *data = entry->data;
    return true;
}

bool framework_resource_string(uint32_t id, std::string* text) {
    if (!framework_resource_is_id(id)) return false;
    const FrameworkStringEntry* entry = FindById(kFrameworkStrings, id);
    if (entry == nullptr) return false;
    if (text != nullptr) *text = kFrameworkText + entry->offset;
    return true;
}

bool framework_resource_name(uint32_t id, std::string* type, std::string* entry) {
    if (!framework_resource_is_id(id)) return false;
    const FrameworkNameEntry* found = FindById(kFrameworkNames, id);
    const char* type_name = TypeName(id);
    if (found == nullptr || type_name == nullptr) return false;
    if (type != nullptr) *type = type_name;
    if (entry != nullptr) *entry = kFrameworkText + found->offset;
    return true;
}

bool framework_resource_find(const std::string& type, const std::string& entry, uint32_t* id) {
    const auto& index = NameIndex();
    const auto found = index.find(type + "/" + entry);
    if (found == index.end()) return false;
    if (id != nullptr) *id = found->second;
    return true;
}

}  // namespace kudroid
