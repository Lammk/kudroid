#pragma once
namespace kudroid {
class ElfLoader;
class LibraryManager;
void prepareRuntimeCageReservations();
void logEarlyRuntimeCageReservations();
void releaseUnusedRuntimeCageReservations();
bool installRuntimeCageAdapter(LibraryManager& manager, ElfLoader& runtime);
bool configureRuntimeCageExecution(ElfLoader& runtime);
}
