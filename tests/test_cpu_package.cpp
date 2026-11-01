#include "kudroid/VFSPathRemapper.h"
#include "kudroid/platform/CpuInfo.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace {
int checks = 0, failures = 0;
void Check(bool ok, const char* message) {
    ++checks;
    std::printf("%s %s\n", ok ? "OK" : "FAIL", message);
    if (!ok) ++failures;
}
std::string Read(const std::string& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}

int main() {
    auto root = std::filesystem::temp_directory_path() / ("kudroid_cpu_package_" + std::to_string(getpid()));
    auto& remapper = kudroid::VFSPathRemapper::getInstance();
    remapper.setDocumentsDirectory(root.string());
    for (unsigned count : {1u, 6u, 8u}) {
        kudroid::CpuTopology cpu;
        cpu.total_cores = count;
        cpu.performance_cores = count;
        cpu.performance_max_khz = 2000000;
        kudroid::cpu_topology_override_for_test(cpu);
        Check(remapper.init_pseudo_files(), "pseudo files initialized");
        const std::string expected = count == 1 ? "0\n" : "0-" + std::to_string(count - 1) + "\n";
        const std::string base = remapper.androidRoot() + "/sys/devices/system/cpu/";
        Check(Read(base + "present") == expected, "present CPUs agree with topology");
        for (unsigned i = 0; i < count; ++i) {
            const std::string path = base + "cpu" + std::to_string(i) + "/topology/";
            Check(Read(path + "physical_package_id") == "0\n", "core belongs to represented package");
            Check(Read(path + "package_cpus_list") == expected, "package lists all represented CPUs");
            Check(Read(path + "core_siblings_list") == expected, "legacy siblings list agrees with package list");
        }
    }
    kudroid::cpu_topology_reset_for_test();
    std::filesystem::remove_all(root);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
