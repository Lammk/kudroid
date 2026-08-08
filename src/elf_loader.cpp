#include "kudroid/elf_loader.hpp"

#include <utility>

namespace kudroid {

ElfLoader::ElfLoader(std::string path)
    : path_(std::move(path)) {}

ElfLoader::~ElfLoader() = default;

ElfLoader::ElfLoader(ElfLoader&&) noexcept = default;
ElfLoader& ElfLoader::operator=(ElfLoader&&) noexcept = default;

bool ElfLoader::parse() {
    // TODO(Phase1): Open file, validate ELF64 magic, read program headers.
    return false;
}

bool ElfLoader::map() {
    // TODO(Phase1): mmap PT_LOAD segments with correct permissions.
    return false;
}

bool ElfLoader::relocate() {
    // TODO(Phase1): Process R_AARCH64_* relocations and bind symbols.
    return false;
}

} // namespace kudroid