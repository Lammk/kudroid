#pragma once

#include <string>
#include <cstdint>

namespace kudroid {

// Parsing results AndroidManifest.xml (binary AXML).
struct ManifestInfo {
    std::string packageName;
    std::string versionName;
    std::string versionCode;
    std::string appLabel;
    std::string mainActivity; // activity has intent-filter MAIN+LAUNCHER
};

class APKExtractor {
public:
    // Extract a regular APK: lib/arm64-v8a/*.so, *.dex, assets/, AndroidManifest.xml
    // to targetDirectory (keep the directory tree intact).
    static bool extract_apk(const std::string& apkPath,
                            const std::string& targetDirectory);

    // Extract a split APK (part of bundle): like extract_apk but no
    // entry is required (split containing only res/ is normal) and ignored
    // AndroidManifest.xml (split's manifest is wrapper `<split ...>`, just
    // manifest of base).
    static bool extract_split(const std::string& apkPath,
                              const std::string& targetDirectory);

    // True if the file is a split-APK container (.xapk / .apks / .apkm): a ZIP has
    // entry .apk at the top-level (or under splits/).
    static bool is_bundle_container(const std::string& path);

    // Unpack the container: get the related APKs (base + arm64 + config not
    // ABI), include their lib/assets/dex/manifest in targetDirectory.
    static bool extract_bundle(const std::string& containerPath,
                               const std::string& targetDirectory);

    // Extract standard Android Application Package ID from APK/Bundle
    static std::string get_package_name(const std::string& apkPath);

    // Parse binary AndroidManifest.xml (AXML) extracted — returns package,
    // label, version and LAUNCHER activity (activity has intent-filter
    // MAIN+LAUNCHER, or activity-alias points to it).
    static ManifestInfo parse_manifest(const std::uint8_t* data, std::size_t size);

    // Parse AndroidManifest.xml as plain TEXT (APK repacked by apktool and
    // similarly often contains manifest text instead of binary AXML — AXML parser
    // return empty for these files). Heuristic scans <activity>/<activity- tags
    // alias> + intent-filter MAIN/LAUNCHER.
    static ManifestInfo parse_manifest_text(const char* data, std::size_t size);

    static const std::string& lastError();
};

} // namespace kudroid
