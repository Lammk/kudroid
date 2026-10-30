#pragma once

#include <cstdint>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace kudroid {

struct ResourceValueTypeCount {
    uint8_t data_type = 0;
    std::string name;
    uint64_t count = 0;
    bool supported = false;
};

struct ResourceQualifierCount {
    std::string name;
    std::string value;
    uint64_t count = 0;
    bool supported = false;
};

struct ResourceTableAnalysis {
    std::vector<std::string> packages;
    std::map<std::string, uint64_t> resource_type_counts;
    std::vector<ResourceValueTypeCount> value_types;
    std::vector<ResourceQualifierCount> qualifiers;
    uint64_t resource_ids = 0;
    uint64_t simple_entries = 0;
    uint64_t complex_entries = 0;
    uint64_t complex_map_values = 0;
    uint64_t malformed_entries = 0;
    uint64_t skipped_entries = 0;
};

struct ResourceScalar {
    uint8_t type = 0;
    uint32_t data = 0;
    uint32_t resource_id = 0;
    std::string text;
};
struct ResourceStyleConfig {
    std::string locale;
    int ui_mode = 0;
    int sdk = 0;
    int width_dp = 0;
    int height_dp = 0;
    int orientation = 0;
    int smallest_width_dp = 0;
    int density = 0;
    int screen_layout = 0;
    int color_mode = 0;
};
bool resource_get_style(uint32_t id, const ResourceStyleConfig& config,
                        std::map<uint32_t, ResourceScalar>* attributes);
bool resource_get_value(uint32_t id, const std::string& locale, bool resolve,
                        ResourceScalar* value);
bool resource_get_value(uint32_t id, const ResourceStyleConfig& config, bool resolve,
                        ResourceScalar* value);
bool resource_get_pool_string(uint32_t index, std::string* value);
bool resource_resolve_strings(const std::vector<uint8_t>& bytes,
                              const std::vector<std::string>& references,
                              const std::string& locale, std::vector<std::string>* values);

// Every file path a reference resolves to across all configurations. Resource
// shrinking can rename res/ files, so the manifest's icon reference is the only
// reliable way to find the right bitmap.
bool resource_resolve_file_paths(const std::vector<uint8_t>& bytes,
                                 const std::string& reference,
                                 std::vector<std::string>* paths,
                                 uint32_t* resource_id);
bool resource_id_file_paths(const std::vector<uint8_t>& bytes, uint32_t resource_id,
                            std::vector<std::string>* paths);

bool resource_get_identifier(const std::string& name, const std::string& type,
                             const std::string& package, uint32_t* id);
// package, type and entry name of a resource id; framework ids report package "android".
bool resource_get_name(uint32_t id, std::string* package, std::string* type,
                       std::string* entry);
bool resource_get_string(uint32_t id, const std::string& locale, std::string* value);
bool resource_get_color(uint32_t id, const std::string& locale, uint32_t* value);
void reset_resource_table_cache();
bool resource_analyze_bytes(const uint8_t* bytes, size_t size,
                            ResourceTableAnalysis* analysis, std::string* error);

}  // namespace kudroid
