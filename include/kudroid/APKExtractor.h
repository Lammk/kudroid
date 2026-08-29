#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace kudroid {

// One <meta-data android:name= android:value=> entry.
//
// Manifest meta-data is not decoration: it is how a component is told things the
// framework itself has to look up for it. The case that forced this to exist is
// AGDK's GameActivity, which reads
//
//   getPackageManager().getActivityInfo(getIntent().getComponent(), GET_META_DATA)
//       .metaData.getString("android.app.lib_name")
//
// to learn which .so to load. With no meta-data there is no library name, so the
// activity has no renderer and the surface stays blank — a failure that looks like a
// graphics problem and is not one.
struct MetaDataEntry {
    std::string name;
    std::string value;
};

// One <activity> / <activity-alias> declared in AndroidManifest.xml.
struct ActivityEntry {
    std::string name;          // fully qualified, leading '.' already expanded
    bool isLauncher = false;   // has intent-filter MAIN + LAUNCHER
    bool isExported = false;   // android:exported="true", or implied by a filter
    bool isAlias = false;      // came from <activity-alias>

    // <meta-data> nested in this <activity>. Per-activity rather than global
    // because that is what Android scopes it to, and two activities in one app can
    // legitimately carry different values for the same key.
    std::vector<MetaDataEntry> metaData;
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

    // <meta-data> directly under <application>. Android exposes these through
    // ApplicationInfo.metaData, and a component that cannot find a key on itself
    // falls back to looking here.
    std::vector<MetaDataEntry> applicationMetaData;

    // Every activity the manifest declares, in declaration order. This is the
    // authoritative list: Android launches what the manifest says, so KuDroid can
    // walk real entries instead of inventing names like "<pkg>.Main".
    std::vector<ActivityEntry> activities;

    // Meta-data of `activityName`, falling back to <application>'s.
    //
    // The fallback matches how apps are written rather than how Android stores it:
    // a library that documents a manifest key rarely says which element it belongs
    // under, so the same key turns up in either place across real APKs.
    std::vector<MetaDataEntry> metaDataFor(const std::string& activityName) const {
        for (const ActivityEntry& a : activities) {
            if (a.name == activityName && !a.metaData.empty()) return a.metaData;
        }
        return applicationMetaData;
    }

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
