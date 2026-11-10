const std::string& APKExtractor::lastError() { return gLastError; }

// Scan all existing .so and .dex files in targetDirectory before extraction.
static std::set<std::filesystem::path> scan_code_files(const std::string& targetDirectory) {
    std::set<std::filesystem::path> codeFiles;
    std::error_code ec;
    const std::filesystem::path root(targetDirectory);
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        return codeFiles;
    }
    for (const auto& it : std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (it.is_regular_file(ec)) {
            const std::string name = it.path().filename().string();
            if (endsWithCi(name, ".so") || endsWithCi(name, ".dex")) {
                codeFiles.insert(it.path().lexically_normal());
            }
        }
    }
    return codeFiles;
}

// Remove empty directories recursively (e.g. emptied lib/<abi> folders).
static void remove_empty_dirs(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) return;
    for (const auto& it : std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (it.is_directory(ec)) {
            remove_empty_dirs(it.path());
            std::filesystem::remove(it.path(), ec);
        }
    }
}

// When re-installing an APK or bundle over an existing app directory, delete any old .so
// or .dex files that are no longer part of the new APK release.
static void purge_orphaned_code_files(const std::string& targetDirectory,
                                      const std::set<std::filesystem::path>& oldCodeFiles,
                                      const std::set<std::filesystem::path>& newCodeFiles) {
    for (const auto& oldPath : oldCodeFiles) {
        if (newCodeFiles.find(oldPath) == newCodeFiles.end()) {
            std::error_code ec;
            std::filesystem::remove(oldPath, ec);
            if (!ec) {
                apkLog("  -> Removed orphaned code file: " + oldPath.string());
            }
        }
    }
    const auto libDir = std::filesystem::path(targetDirectory) / "lib";
    remove_empty_dirs(libDir);
}

// Generic body for extract_apk / extract_split.
static bool extract_apk_impl(const std::string& apkPath, const std::string& targetDirectory,
                             bool requireEntries, bool extractManifest,
                             std::set<std::filesystem::path>* outExtractedCodeFiles = nullptr) {
    gLastError.clear();
    std::ifstream apk(apkPath, std::ios::binary | std::ios::ate);
    if (!apk) { gLastError = "Cannot open APK: " + apkPath; apkLog(gLastError); return false; }
    const std::streamsize fileSize = apk.tellg();
    if (fileSize < 22) { gLastError = "APK is too small to be a ZIP archive"; apkLog(gLastError); return false; }

    std::vector<ZipEntryInfo> entries;
    if (!readZipEntries(apk, fileSize, entries)) {
        apkLog("readZipEntries failed: " + gLastError);
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(targetDirectory, error);
    if (error) { gLastError = "Cannot create target directory: " + error.message(); apkLog(gLastError); return false; }

    bool found = false;
    int bestIconScore = -1;
    std::vector<std::uint8_t> bestIconData;
    ManifestInfo manifestInfo;
    std::string arscAppName;
    std::vector<std::uint8_t> arscData;

    // Which ABIs the APK ships, so an unsupported one can be named.
    //
    // Only arm64-v8a is extracted, because the ELF loader relocates AArch64 only
    // (R_AARCH64_*). Without this bookkeeping an armeabi-v7a-only APK extracted zero
    // .so files and the failure surfaced much later as "Library directory does not
    // exist" — which reads as a KuDroid bug rather than as an APK this build cannot
    // run.
    std::set<std::string> abisSeen;
    bool extractedArm64Lib = false;

    for (const auto& entryInfo : entries) {
        const std::string& entry = entryInfo.name;
        const int iconScore = iconPriority(entry);
        bool shouldExtract = false;
        const bool isNativeLib = entry.rfind("lib/", 0) == 0 && entry.size() >= 3 &&
                                 entry.compare(entry.size() - 3, 3, ".so") == 0;
        if (isNativeLib) {
            // lib/<abi>/foo.so — record the ABI whether or not it is usable.
            const std::size_t abiEnd = entry.find('/', 4);
            if (abiEnd != std::string::npos) abisSeen.insert(entry.substr(4, abiEnd - 4));
            if (entry.rfind("lib/" KUDROID_DEVICE_ABI "/", 0) == 0) {
                shouldExtract = true;
                extractedArm64Lib = true;
            }
        }
        else if (entry.size() >= 4 && entry.compare(entry.size() - 4, 4, ".dex") == 0) shouldExtract = true;
        else if (entry == "AndroidManifest.xml" || entry == "resources.arsc") shouldExtract = true;
        else if (iconScore > bestIconScore) shouldExtract = true;

        if (!shouldExtract) {
            continue;
        }
        if (entry.empty() || entry.back() == '/') {
            continue; // skip folder
        }

        found = true;
        if (entry == "AndroidManifest.xml") {
            std::vector<std::uint8_t> output;
            if (extractZipEntryToMemory(apk, entryInfo, output)) {
                manifestInfo = parseAxml(output);
            }
        } else if (entry == "resources.arsc") {
            extractZipEntryToMemory(apk, entryInfo, arscData);
        }

        if (iconScore > bestIconScore) {
            bestIconScore = iconScore;
            extractZipEntryToMemory(apk, entryInfo, bestIconData);
        }

        // Do not write resources.arsc or icons to disk at this step
        if (entry == "resources.arsc" || (iconScore > 0 && entry != "AndroidManifest.xml" &&
            entry.rfind("lib/", 0) != 0 && !endsWithCi(entry, ".dex"))) {
            continue;
        }

        apkLog("Extracting: " + entry);

        // Save .dex files into oat/ subdirectory
        const auto destination = endsWithCi(entry, ".dex")
            ? std::filesystem::path(targetDirectory) / "oat" / std::filesystem::path(entry).filename()
            : std::filesystem::path(targetDirectory) / entry;
        if (!zip_dest_within(targetDirectory, destination)) {
            gLastError = "Refusing to extract outside target: " + entry;
            apkLog(gLastError);
            return false;
        }
        std::filesystem::create_directories(destination.parent_path(), error);

        if (!extractZipEntryToFile(apk, entryInfo, destination.string())) {
            gLastError = "Cannot extract entry: " + entry;
            apkLog(gLastError);
            return false;
        }
        ::chmod(destination.c_str(), 0755);
        apkLog("  -> Saved to " + destination.string() + " (" + std::to_string(entryInfo.uncompressedSize) + " bytes)");
        if (outExtractedCodeFiles != nullptr &&
            (endsWithCi(entry, ".so") || endsWithCi(entry, ".dex"))) {
            outExtractedCodeFiles->insert(destination.lexically_normal());
        }
    }

    if (!found) {
        if (!requireEntries) return true; // split contains only res/ — nothing to get, no error
        gLastError = "No expected entries found in APK"; apkLog(gLastError); return false;
    }

    // An APK with native code for other ABIs and none for ours cannot run here, and
    // saying so now is the whole point. The alternative — silently extracting nothing
    // — sends the loader down the Java-only path, where the first missing symbol looks
    // like an unrelated defect.
    if (!abisSeen.empty() && !extractedArm64Lib) {
        std::string list;
        for (const std::string& abi : abisSeen) {
            if (!list.empty()) list += ", ";
            list += abi;
        }
        apkLog("  !! APK ships native libraries for [" + list + "] but not " +
               KUDROID_DEVICE_ABI + ". KuDroid can only load " + KUDROID_DEVICE_ABI +
               " (the ELF loader relocates AArch64 only), so this APK's native code"
               " will be missing.");
    }

    // Symlink lib/arm64 to arm64-v8a for AOSP convention.
    {
        const auto libArm64V8a = std::filesystem::path(targetDirectory) / "lib/arm64-v8a";
        const auto libArm64 = std::filesystem::path(targetDirectory) / "lib/arm64";
        std::error_code linkEc;
        if (std::filesystem::exists(libArm64V8a, linkEc) && !std::filesystem::exists(libArm64, linkEc)) {
            std::filesystem::create_directory_symlink("arm64-v8a", libArm64, linkEc);
            if (!linkEc) {
                apkLog("  -> Created lib/arm64 symlink to arm64-v8a");
            }
        }
    }

    // Save app_icon.png if found
    // The manifest names the icon resource; res/ file names can be rewritten by
    // resource shrinking, so resolve the reference instead of guessing.
    if (extractManifest && !arscData.empty() && !manifestInfo.appIcon.empty()) {
        std::vector<std::string> preferred;
        resolveIconPaths(manifestInfo.appIcon, arscData, entries,
                         [&](const ZipEntryInfo& entry, std::vector<std::uint8_t>* out) {
                             return extractZipEntryToMemory(apk, entry, *out);
                         }, &preferred);
        const IconPick pick = pickIcon(entries, preferred);
        if (pick.entry != nullptr) {
            std::vector<std::uint8_t> data;
            if (extractZipEntryToMemory(apk, *pick.entry, data) && isBitmapData(data)) {
                bestIconData = std::move(data);
            }
        }
    }

    if (!bestIconData.empty()) {
        const auto iconDest = std::filesystem::path(targetDirectory) / "app_icon.png";
        std::ofstream iconFile(iconDest, std::ios::binary);
        if (iconFile) {
            iconFile.write(reinterpret_cast<const char*>(bestIconData.data()), bestIconData.size());
            iconFile.close();
            apkLog("  -> Saved app icon to " + iconDest.string());
        }
    }

    // Keep the APK itself alongside the extracted tree, as base.apk.
    //
    // Extracting entries as loose files is not enough for every app. A Unity game
    // opens its own APK as a ZIP and reads assets/bin/Data/* out of the central
    // directory — it never looks at loose files — so it needs the archive present at
    // the path getPackageCodePath() reports, which is /data/app/<pkg>/base.apk.
    // Without it Unity logs "ApkAddCentralDirectory : Unable to open" and starts with
    // no scenes, no textures and no audio: a black screen and silence, with no
    // failure anywhere near the cause.
    //
    // Only written for a base APK (extractManifest), not for config splits: they
    // would each overwrite it, and the base is the one an app expects to find.
    // Copying rather than moving keeps the caller's file intact — kudroid_install_apk
    // deletes its own temp copy, and the KDB install path streams from elsewhere.
    if (extractManifest) {
        const auto apkDest = std::filesystem::path(targetDirectory) / "base.apk";
        std::error_code copyError;
        // Compare canonical paths: re-installing from a path that already IS the
        // destination would otherwise truncate the source to nothing.
        const bool sameFile = std::filesystem::exists(apkDest, copyError) &&
                              std::filesystem::equivalent(apkPath, apkDest, copyError);
        if (!sameFile) {
            copyError.clear();
            std::filesystem::copy_file(apkPath, apkDest,
                                       std::filesystem::copy_options::overwrite_existing,
                                       copyError);
            if (copyError) {
                // Not fatal on its own — an app that only reads loose files still
                // runs — but say so, because the apps that need it fail in a way
                // that points nowhere near here.
                apkLog("  !! Could not place base.apk at " + apkDest.string() + ": " +
                       copyError.message() +
                       " (apps that read their own APK as a ZIP will find no assets)");
            } else {
                ::chmod(apkDest.c_str(), 0644);
                apkLog("  -> Kept APK as " + apkDest.string());
            }
        }
    }

    if (!arscData.empty()) {
        std::uint32_t targetResId = 0;
        if (!manifestInfo.appLabel.empty() && manifestInfo.appLabel.front() == '@') {
            const auto hexPos = manifestInfo.appLabel.find("0x");
            if (hexPos != std::string::npos) {
                try {
                    targetResId = static_cast<std::uint32_t>(std::stoul(manifestInfo.appLabel.substr(hexPos), nullptr, 16));
                } catch (...) {}
            }
        }
        arscAppName = parseArscAppName(arscData, targetResId);
        arscData.clear();
        arscData.shrink_to_fit();
    }

    // Save app_info.json (metadata: version, label, package)
    if (extractManifest) {
        const auto infoDest = std::filesystem::path(targetDirectory) / "app_info.json";
        std::string appDirName = std::filesystem::path(targetDirectory).filename().string();
        
        std::string label = manifestInfo.appLabel;
        if (label.empty() || label.front() == '@' || (label.find('.') != std::string::npos && label.find(' ') == std::string::npos)) {
            // The manifest's label is a resource reference or missing. Prefer the
            // resolved app_name resource, then fall back to deriving a name from the
            // package. A special case for one app used to sit between the two.
            if (!arscAppName.empty()) {
                label = arscAppName;
            } else if (!manifestInfo.packageName.empty()) {
                label = prettifyAppName(manifestInfo.packageName);
            } else {
                label = prettifyAppName(appDirName);
            }
        }
        
        std::string version = manifestInfo.versionName.empty() ? "1.0.0" : manifestInfo.versionName;
        std::ofstream infoFile(infoDest);
        if (infoFile) {
            infoFile << "{\n";
            infoFile << "  \"name\": \"" << appDirName << "\",\n";
            infoFile << "  \"label\": \"" << label << "\",\n";
            infoFile << "  \"version\": \"" << version << "\",\n";
            infoFile << "  \"package\": \"" << manifestInfo.packageName << "\",\n";
            infoFile << "  \"main_activity\": \"" << manifestInfo.mainActivity << "\"\n";
            infoFile << "}\n";
            infoFile.close();
            apkLog("  -> Saved app info (v" + version + ", " + label + ") to " + infoDest.string());
        }
    }

    return true;
}

bool APKExtractor::extract_apk(const std::string& apkPath, const std::string& targetDirectory) {
    const auto oldCodeFiles = scan_code_files(targetDirectory);
    std::set<std::filesystem::path> newCodeFiles;
    const bool ok = extract_apk_impl(apkPath, targetDirectory, /*requireEntries=*/true, /*extractManifest=*/true, &newCodeFiles);
    if (ok) {
        purge_orphaned_code_files(targetDirectory, oldCodeFiles, newCodeFiles);
    }
    return ok;
}

bool APKExtractor::extract_split(const std::string& apkPath, const std::string& targetDirectory) {
    return extract_apk_impl(apkPath, targetDirectory, /*requireEntries=*/false, /*extractManifest=*/false, nullptr);
}

bool APKExtractor::is_bundle_container(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamsize size = f.tellg();
    if (size < 22) return false;
    std::vector<ZipEntryInfo> entries;
    if (!readZipEntries(f, size, entries)) return false;
    for (const auto& e : entries) {
        if (!endsWithCi(e.name, ".apk")) continue;
        // Only considered a container if the .apk is at the top-level or below splits/ —
        // Avoid mistakenly normal APK containing .apk file in assets/.
        if (e.name.find('/') == std::string::npos || e.name.rfind("splits/", 0) == 0) {
            return true;
        }
    }
    return false;
}

bool APKExtractor::extract_bundle(const std::string& containerPath, const std::string& targetDirectory,
                                  const std::string& expansionDirectory) {
    gLastError.clear();
    std::ifstream f(containerPath, std::ios::binary);
    if (!f) { gLastError = "Cannot open bundle container: " + containerPath; apkLog(gLastError); return false; }
    f.seekg(0, std::ios::end);
    const std::streamsize size = f.tellg();

    std::vector<ZipEntryInfo> allEntries;
    if (!readZipEntries(f, size, allEntries)) return false;

    // Select related APKs: the base plus every non-ABI config split, and the ABI
    // split for this device.
    //
    // Other-ABI splits are skipped to avoid pointless I/O — a 32-bit split cannot be
    // loaded (the ELF loader relocates AArch64 only). Skipping is only safe when
    // SOMETHING is left, which is the bug this used to have: an armeabi-only bundle
    // filtered down to zero APKs and reported "no usable APK splits", naming neither
    // the ABI it found nor the one it wanted.
    std::vector<ZipEntryInfo> apks;
    std::set<std::string> skippedAbiSplits;
    for (const auto& e : allEntries) {
        if (!endsWithCi(e.name, ".apk")) continue;
        const std::string lower = toLower(e.name);
        const char* otherAbi = nullptr;
        if (lower.find("armeabi") != std::string::npos) otherAbi = "armeabi-v7a";
        else if (lower.find("x86_64") != std::string::npos) otherAbi = "x86_64";
        else if (lower.find("x86") != std::string::npos) otherAbi = "x86";
        else if (lower.find("mips") != std::string::npos) otherAbi = "mips";
        if (otherAbi != nullptr) {
            skippedAbiSplits.insert(otherAbi);
            continue;
        }
        apks.push_back(e);
    }
    if (apks.empty()) {
        if (!skippedAbiSplits.empty()) {
            std::string list;
            for (const std::string& abi : skippedAbiSplits) {
                if (!list.empty()) list += ", ";
                list += abi;
            }
            gLastError = "Bundle only contains splits for [" + list + "]; KuDroid needs " +
                         KUDROID_DEVICE_ABI + " (the ELF loader relocates AArch64 only)";
        } else {
            gLastError = "Bundle container has no usable APK splits";
        }
        apkLog(gLastError);
        return false;
    }

    // Process the base finally so that the actual (base's) AndroidManifest.xml overwrites the manifest
    // wrapper of the split.
    auto isBaseApk = [](const ZipEntryInfo& e) {
        const std::string lower = toLower(e.name);
        return lower.find("base") != std::string::npos || lower.find("config") == std::string::npos;
    };
    std::stable_sort(apks.begin(), apks.end(), [&](const ZipEntryInfo& a, const ZipEntryInfo& b) {
        return !isBaseApk(a) && isBaseApk(b); // non-base first, base last
    });

    const auto splitDir = std::filesystem::path(targetDirectory) / "__bundle_splits__";
    std::error_code error;
    std::filesystem::create_directories(splitDir, error);
    if (error) { gLastError = "Cannot create split temp dir: " + error.message(); return false; }

    const auto oldCodeFiles = scan_code_files(targetDirectory);
    std::set<std::filesystem::path> newCodeFiles;

    bool ok = true;
    std::size_t index = 0;
    for (const auto& e : apks) {
        const std::string safeName = "split_" + std::to_string(index++) + ".apk";
        const std::string tmpApk = (splitDir / safeName).string();
        if (!extractZipEntryToFile(f, e, tmpApk)) {
            gLastError = "Cannot extract split from container: " + e.name;
            apkLog(gLastError);
            ok = false;
            break;
        }
        apkLog("Processing split: " + e.name);
        const bool splitOk = isBaseApk(e)
            ? extract_apk_impl(tmpApk, targetDirectory, /*requireEntries=*/true, /*extractManifest=*/true, &newCodeFiles)
            : extract_apk_impl(tmpApk, targetDirectory, /*requireEntries=*/false, /*extractManifest=*/false, &newCodeFiles);
        if (!splitOk) {
            gLastError = "Split failed (" + e.name + "): " + lastError();
            apkLog(gLastError);
            ok = false;
            break;
        }
    }

    // ── OBB expansion files ──
    // .xapk (APKPure) usually contains "Android/obb/<pkg>/<file>.obb". The game reads them
    // qua /sdcard/Android/obb/<pkg>/<file> — VFSPathRemapper map /sdcard/ →
    // <androidRoot>/sdcard/. So extract to <androidRoot>/sdcard/Android/obb/.
    // targetDirectory = <androidRoot>/data/app/<appName> → androidRoot 3 levels above.
    const auto androidRoot = std::filesystem::path(targetDirectory).parent_path().parent_path().parent_path();
    const std::string appName = std::filesystem::path(targetDirectory).filename().string();
    const auto obbRoot = expansionDirectory.empty()
        ? androidRoot / "sdcard/Android/obb" : std::filesystem::path(expansionDirectory);

    for (const auto& e : allEntries) {
        if (!endsWithCi(e.name, ".obb")) continue;
        // Get the package from the entry path (".../Android/obb/<pkg>/file.obb" or
        // ".../obb/<pkg>/file.obb" — search anywhere in the path for some tools
        // zip with prefix directory); If not, fallback to app name.
        std::string pkg = appName;
        std::string rel = e.name;
        auto pos = rel.find("Android/obb/");
        if (pos != std::string::npos) {
            rel = rel.substr(pos + 12); // cut to "<pkg>/file.obb"
        } else {
            pos = rel.find("obb/");
            if (pos != std::string::npos) rel = rel.substr(pos + 4);
        }
        const auto slash = rel.find('/');
        if (slash != std::string::npos) {
            pkg = rel.substr(0, slash);
        }
        // Package names are [A-Za-z0-9._]+ — anything else is a traversal attempt.
        if (pkg.empty() || pkg == "." || pkg == ".." ||
            pkg.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._") !=
                std::string::npos) {
            gLastError = "Refusing suspicious OBB package: " + pkg;
            apkLog(gLastError);
            ok = false;
            break;
        }
        const auto obbDir = obbRoot / pkg;
        std::error_code obbError;
        std::filesystem::create_directories(obbDir, obbError);
        if (obbError) {
            gLastError = "Cannot create OBB dir: " + obbError.message();
            apkLog(gLastError);
            ok = false;
            break;
        }
        const auto obbDest = obbDir / std::filesystem::path(e.name).filename();
        if (!extractZipEntryToFile(f, e, obbDest.string())) {
            gLastError = "Cannot extract OBB from container: " + e.name;
            apkLog(gLastError);
            ok = false;
            break;
        }
        apkLog("OBB extracted -> " + obbDest.string());
    }

    // A base APK can carry no raster icon when the density splits own it. Fill
    // app_icon.png from the merged set while the temporary splits still exist.
    if (ok) {
        const auto iconDest = std::filesystem::path(targetDirectory) / "app_icon.png";
        std::error_code iconEc;
        if (!std::filesystem::exists(iconDest, iconEc)) {
            const std::string savedError = gLastError;
            PackagePreview preview;
            if (preview_package(containerPath, splitDir.string(), "", &preview) &&
                isBitmapData(preview.icon)) {
                std::ofstream iconFile(iconDest, std::ios::binary);
                if (iconFile) {
                    iconFile.write(reinterpret_cast<const char*>(preview.icon.data()),
                                   static_cast<std::streamsize>(preview.icon.size()));
                    if (iconFile) apkLog("  -> Saved app icon from bundle splits");
                }
            }
            gLastError = savedError;
        }
    }

    // Clean up temp (even if there are errors).
    std::filesystem::remove_all(splitDir, error);
    if (!ok) return false;
    purge_orphaned_code_files(targetDirectory, oldCodeFiles, newCodeFiles);
    apkLog("Bundle merged into " + targetDirectory);
    return true;
}

