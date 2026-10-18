#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {
void kudroid_surface_texture_create(int id, unsigned texture);
void kudroid_surface_texture_release(int id);
void kudroid_surface_texture_attach(int id, unsigned texture);
void kudroid_surface_texture_detach(int id);
int64_t kudroid_surface_texture_update(int id);
}

namespace kudroid::surface_texture {
bool exists(int id);
bool post(int id, std::vector<uint8_t> pixels, int width, int height, int64_t timestamp);
bool bind(unsigned target, unsigned texture);
void deleted_textures(int count, const unsigned* names);
unsigned target(unsigned target);
void restore_binding(unsigned target);
bool binding(unsigned pname, int* value);
std::string shader(std::string source);
std::string uniform_name(const char* name);
void program_changed(unsigned program);
void use_program(unsigned program);
void binary_loaded();
void context_changed();
void context_destroyed(void* context);
void active_uniform(int capacity, int* length, unsigned* type, char* name);

class DrawScope {
    int active_ = 0;
    std::vector<std::pair<int, unsigned>> saved_;
public:
    DrawScope();
    ~DrawScope();
};
}
