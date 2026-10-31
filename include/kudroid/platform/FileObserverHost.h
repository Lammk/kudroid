#pragma once

#include <cstdint>
#include <string>

namespace kudroid {
struct FileWatchEvent {
    uint32_t mask = 0;
    std::string name;
};
uint64_t start_file_watch(const std::string& path, uint32_t mask);
bool poll_file_watch(uint64_t handle, FileWatchEvent* event);
void stop_file_watch(uint64_t handle);
uint32_t supported_file_watch_events();
}
