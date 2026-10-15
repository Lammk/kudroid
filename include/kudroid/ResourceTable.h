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

bool resource_get_identifier(const std::string& name, const std::string& type,
                             const std::string& package, uint32_t* id);
bool resource_get_string(uint32_t id, const std::string& locale, std::string* value);
bool resource_get_color(uint32_t id, const std::string& locale, uint32_t* value);
void reset_resource_table_cache();
bool resource_analyze_bytes(const uint8_t* bytes, size_t size,
                            ResourceTableAnalysis* analysis, std::string* error);

}  // namespace kudroid
