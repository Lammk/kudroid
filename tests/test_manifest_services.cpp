#include "kudroid/APKExtractor.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

static int checks, failures;
static void check(bool ok, const char* name) {
    ++checks;
    if (!ok) ++failures;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

static void verify(const kudroid::ManifestInfo& manifest) {
    check(manifest.packageName == "test.services", "package identity");
    check(manifest.mainActivity == "test.services.Main" && manifest.activities.size() == 1,
          "service filters preserve activity resolution");
    check(!manifest.applicationEnabled && manifest.applicationPermission == "test.permission",
          "application enabled state and permission");
    check(manifest.services.size() == 3, "all declared services retained");
    if (manifest.services.size() != 3) return;
    const auto& worker = manifest.services[0];
    check(worker.name == "test.services.Worker" && worker.enabled && !worker.exported,
          "qualified name and explicit exported state");
    check(worker.permission == "test.permission", "service inherits application permission");
    check(worker.metaData.size() == 1 && worker.metaData[0].name == "worker.key"
          && worker.metaData[0].value == "worker.value" && manifest.applicationMetaData.empty(),
          "service metadata stays scoped to the component");
    check(worker.intentFilters.size() == 2, "distinct filters retained");
    if (worker.intentFilters.size() == 2) {
        const auto& first = worker.intentFilters[0];
        check(first.actions == std::vector<std::string>{"test.services.BIND"}
              && first.categories == std::vector<std::string>{"test.category"}
              && first.priority == 7 && !first.hasData, "action and category filter");
        const auto& second = worker.intentFilters[1];
        check(second.actions == std::vector<std::string>{"test.services.DATA"}
              && second.categories.empty() && second.priority == -3 && second.hasData,
              "data constraints are retained separately");
    }
    const auto& other = manifest.services[1];
    check(other.name == "test.services.Other" && !other.enabled && other.exported
          && other.permission.empty(), "disabled service and cleared inherited permission");
    const auto& explicitService = manifest.services[2];
    check(explicitService.name == "test.services.Explicit" && !explicitService.exported
          && explicitService.intentFilters.empty(), "self-closing service defaults");
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::ifstream binaryFile(argv[1], std::ios::binary), textFile(argv[2], std::ios::binary);
    std::vector<uint8_t> binary((std::istreambuf_iterator<char>(binaryFile)), {});
    std::string text((std::istreambuf_iterator<char>(textFile)), {});
    if (binary.empty() || text.empty()) return 2;
    verify(kudroid::APKExtractor::parse_manifest(binary.data(), binary.size()));
    verify(kudroid::APKExtractor::parse_manifest_text(text.data(), text.size()));
    std::printf("Manifest services: %d checks, %d failures\n", checks, failures);
    return failures || checks != 24 ? 1 : 0;
}
