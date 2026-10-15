// Text-manifest path: apktool-repacked APKs ship AndroidManifest.xml as plain XML,
// so parse_manifest_text must record the same activity list as the AXML path.
#include "kudroid/APKExtractor.h"
#include <cstdio>
#include <cstring>
#include <string>

using kudroid::APKExtractor;
using kudroid::ManifestInfo;

int main() {
    const char* xml =
        "<?xml version='1.0' encoding='utf-8'?>\n"
        "<manifest xmlns:android='http://schemas.android.com/apk/res/android'\n"
        "    package='com.example.app'>\n"
        "  <application android:name='.MyApp' android:label='Demo'\n"
        "      android:appComponentFactory='androidx.core.app.CoreComponentFactory'>\n"
        "    <activity android:name='.SplashActivity' android:exported='false'/>\n"
        "    <activity android:name='com.example.app.RealMain'>\n"
        "      <intent-filter>\n"
        "        <action android:name='android.intent.action.MAIN'/>\n"
        "        <category android:name='android.intent.category.LAUNCHER'/>\n"
        "      </intent-filter>\n"
        "    </activity>\n"
        "    <activity android:name='SettingsActivity'/>\n"
        "  </application>\n"
        "</manifest>\n";

    const ManifestInfo info = APKExtractor::parse_manifest_text(xml, std::strlen(xml));
    int failures = 0;
    auto check = [&](bool ok, const char* what, const std::string& got) {
        std::printf("%s: %s = %s\n", ok ? "OK" : "FAIL", what, got.c_str());
        if (!ok) ++failures;
    };

    check(info.packageName == "com.example.app", "packageName", info.packageName);
    check(info.mainActivity == "com.example.app.RealMain", "mainActivity", info.mainActivity);
    // android:name on <application> is expanded from ".MyApp".
    check(info.appClass == "com.example.app.MyApp", "appClass", info.appClass);
    // android:appComponentFactory runs before any component, so it has to survive
    // parsing; an app whose factory never initialises finds its own statics empty.
    check(info.appComponentFactory == "androidx.core.app.CoreComponentFactory",
          "appComponentFactory", info.appComponentFactory);
    check(info.activities.size() == 3, "activities", std::to_string(info.activities.size()));

    const auto order = info.launchOrder();
    check(order.size() == 3, "launchOrder size", std::to_string(order.size()));
    if (order.size() == 3) {
        check(order[0] == "com.example.app.RealMain", "launcher first", order[0]);
        // A bare "SettingsActivity" is relative to the package and must be qualified,
        // otherwise Class.forName can never resolve it.
        bool sawSettings = false;
        for (const auto& s : order) if (s == "com.example.app.SettingsActivity") sawSettings = true;
        check(sawSettings, "bare name qualified with package", order[2]);
    }

    if (failures == 0) { std::printf("=== text manifest PASSED ===\n"); return 0; }
    std::printf("=== text manifest FAILED (%d) ===\n", failures);
    return 1;
}
