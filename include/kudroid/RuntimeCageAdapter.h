#pragma once
namespace kudroid {
class ElfLoader;
class LibraryManager;
void prepareRuntimeCageReservations();
void logEarlyRuntimeCageReservations();
bool installRuntimeCageAdapter(LibraryManager& manager, ElfLoader& runtime);
}
