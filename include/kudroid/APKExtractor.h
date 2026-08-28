#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace kudroid {

// One <activity> / <activity-alias> declared in AndroidManifest.xml.
struct ActivityEntry {
    std::string name;          // fully qualified, leading '.' already expanded
    bool isLauncher = false;   // has intent-filter MAIN + LAUNCHER
    bool isExported = false;   // android:exported="true", or implied by a filter
    bool isAlias = false;      // came from <activity-alias>
};

// Parsing results AndroidManifest.xml (binary AXML).
struct ManifestInfo {
    std::string packageName;
    std::string versionName;
    std::string versionCode;
    std::string appLabel;
    std::string mainActivity; // activity has intent-filter MAIN+LAUNCHER
    std::string appClass;     // android:name on <application>, if any

    // android:appComponentFactory on <application>. Android instantiates this
    // class before any component and routes every Application/Activity/Service/
    // Provider/Receiver instantiation through it. Its <clinit> is therefore the
    // first guest code that runs, and build tools (AGP resource shrinking,
    // string-pool obfuscators, DI frameworks) rely on that ordering: an app whose
    // factory never initialises can find its own static state empty.
    std::string appComponentFactory;

    // Every activity the manifest declares, in declaration order. This is the
    // authoritative list: Android launches what the manifest says, so KuDroid can
    // walk real entries instead of inventing names like "<pkg>.Main".
    std::vector<ActivityEntry> activities;

    // Launcher activities first, then the rest, all manifest-declared.
    std::vector<std::string> launchOrder() const {
        std::vector<std::string> out;
        for (const ActivityEntry& a : activities) {
            if (a.isLauncher && !a.name.empty()) out.push_back(a.name);
        }
        for (const ActivityEntry& a : activities) {
            if (a.isLauncher || a.name.empty()) continue;
            bool dup = false;
            for (const std::string& s : out) {
                if (s == a.name) { dup = true; break; }
            }
            if (!dup) out.push_back(a.name);
        }
        return out;
    }
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
