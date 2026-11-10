ManifestInfo APKExtractor::parse_manifest(const std::uint8_t* data, std::size_t size) {
    if (!data || size == 0) return ManifestInfo{};
    return parseAxml(std::vector<std::uint8_t>(data, data + size));
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_manifest_text — AndroidManifest.xml plain TEXT. APK repack by
// apktool/mod tools (BANDISHARE etc.) usually contain manifest text instead
// binary AXML; parseAxml returns empty on them. Heuristic: scan each card
// <activity ...> / <activity-alias ...>, collect attribute name=, track
// The child intent-filter contains action MAIN + category LAUNCHER.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

std::string extractXmlAttr(const std::string& tag, const char* attrName) {
    // Find attrName="value" or attrName='value' in the tag string. Attr can
    // has namespace (android:name=) — accepts both forms: " name=" and ":name=".
    const std::string needle = std::string(attrName) + "=";
    std::size_t pos = 0;
    while ((pos = tag.find(needle, pos)) != std::string::npos) {
        // The character before attrName must be a space, '<' or ':' (namespace prefix).
        if (pos > 0) {
            const char prev = tag[pos - 1];
            if (!std::isspace(static_cast<unsigned char>(prev)) &&
                prev != '<' && prev != ':') {
                pos += needle.size();
                continue;
            }
        }
        pos += needle.size();
        if (pos >= tag.size()) break;
        const char quote = tag[pos];
        if (quote != '"' && quote != '\'') continue;
        const std::size_t end = tag.find(quote, pos + 1);
        if (end == std::string::npos) break;
        return tag.substr(pos + 1, end - pos - 1);
    }
    return "";
}

} // namespace

ManifestInfo APKExtractor::parse_manifest_text(const char* data, std::size_t size) {
    ManifestInfo info;
    if (!data || size == 0) return info;
    const std::string xml(data, size);

    // package="..." on the <manifest> tag.
    {
        const std::size_t mpos = xml.find("<manifest");
        if (mpos != std::string::npos) {
            const std::size_t close = xml.find('>', mpos);
            if (close != std::string::npos) {
                info.packageName = extractXmlAttr(xml.substr(mpos, close - mpos), "package");
            }
        }
    }

    static const char* kActionMain = "android.intent.action.MAIN";
    static const char* kCategoryLauncher = "android.intent.category.LAUNCHER";

    std::size_t cur = 0;
    std::string currentActivity;
    std::string aliasTarget;
    bool sawActionMain = false;
    bool sawCategoryLauncher = false;
    bool sawAnyIntentFilter = false;
    bool inIntentFilter = false;
    std::string currentExported;
    std::string currentTheme;
    ServiceEntry currentService;
    bool inService = false;
    bool serviceInFilter = false;
    bool servicePermissionSet = false;
    bool serviceExportedSet = false;
    auto commitService = [&]() {
        if (!serviceExportedSet) currentService.exported = !currentService.intentFilters.empty();
        if (!servicePermissionSet) currentService.permission = info.applicationPermission;
        if (!currentService.name.empty()) info.services.push_back(currentService);
        inService = serviceInFilter = false;
    };

    auto commit = [&](const std::string& name) {
        if (info.mainActivity.empty() && !name.empty() && sawActionMain && sawCategoryLauncher) {
            info.mainActivity = name;
        }
    };

    // Same as the AXML path: keep every declared activity so callers can walk real
    // manifest entries rather than guessing names.
    auto commitActivity = [&](const std::string& name, bool isAlias) {
        if (name.empty()) return;
        for (ActivityEntry& e : info.activities) {
            if (e.name != name) continue;
            e.isLauncher = e.isLauncher || (sawActionMain && sawCategoryLauncher);
            if (!isAlias && e.theme.empty()) e.theme = currentTheme;
            e.isExported = e.isExported || (currentExported == "true") || sawAnyIntentFilter;
            return;
        }
        ActivityEntry e;
        e.name = name;
        if (!isAlias) e.theme = currentTheme;
        e.isLauncher = sawActionMain && sawCategoryLauncher;
        e.isExported = currentExported.empty() ? sawAnyIntentFilter : (currentExported == "true");
        e.isAlias = isAlias;
        info.activities.push_back(e);
    };

    while ((cur = xml.find('<', cur)) != std::string::npos) {
        const std::size_t end = xml.find('>', cur);
        if (end == std::string::npos) break;
        const std::string tag = xml.substr(cur, end - cur + 1);
        const bool isClose = tag.size() > 1 && tag[1] == '/';
        const bool isSelfClose = tag.size() > 1 && tag[tag.size() - 2] == '/';

        // Card name: remove '<' or '</' and take the first token.
        std::string tagName;
        {
            std::size_t s = 1;
            while (s < tag.size() && (tag[s] == '/' || std::isspace(static_cast<unsigned char>(tag[s])))) ++s;
            std::size_t e = s;
            while (e < tag.size() && !std::isspace(static_cast<unsigned char>(tag[e])) && tag[e] != '/' && tag[e] != '>') ++e;
            tagName = tag.substr(s, e - s);
        }

        if (tagName == "service") {
            if (!isClose) {
                currentService = ServiceEntry{};
                currentService.name = extractXmlAttr(tag, "name");
                std::string enabled = extractXmlAttr(tag, "enabled");
                currentService.enabled = enabled != "false" && enabled != "0";
                std::string exported = extractXmlAttr(tag, "exported");
                serviceExportedSet = !exported.empty();
                currentService.exported = exported == "true" || exported == "1";
                currentService.permission = extractXmlAttr(tag, "permission");
                servicePermissionSet = tag.find("permission=") != std::string::npos;
                inService = true;
                if (isSelfClose) commitService();
            } else if (inService) commitService();
        } else if (tagName == "activity" || tagName == "activity-alias") {
            if (!isClose) {
                currentActivity = extractXmlAttr(tag, "name");
                aliasTarget = extractXmlAttr(tag, "targetActivity");
                currentExported = extractXmlAttr(tag, "exported");
                currentTheme = tagName == "activity" ? extractXmlAttr(tag, "theme") : "";
                sawActionMain = false;
                sawCategoryLauncher = false;
                sawAnyIntentFilter = false;
                inIntentFilter = false;
                if (isSelfClose) {
                    // <activity ... /> has no closing tag, so record it now.
                    commit(!aliasTarget.empty() ? aliasTarget : currentActivity);
                    commitActivity(!aliasTarget.empty() ? aliasTarget : currentActivity,
                                   tagName == "activity-alias");
                    currentActivity.clear();
                    aliasTarget.clear();
                    currentExported.clear();
                }
            } else {
                commit(currentActivity);
                commitActivity(!aliasTarget.empty() ? aliasTarget : currentActivity,
                               tagName == "activity-alias");
                currentActivity.clear();
                aliasTarget.clear();
                currentExported.clear();
                sawActionMain = false;
                sawCategoryLauncher = false;
                sawAnyIntentFilter = false;
                inIntentFilter = false;
            }
        } else if (tagName == "intent-filter") {
            if (inService) {
                serviceInFilter = !isClose && !isSelfClose;
                if (!isClose) {
                    currentService.intentFilters.emplace_back();
                    try { currentService.intentFilters.back().priority = std::stoi(extractXmlAttr(tag, "priority")); }
                    catch (...) {}
                }
            } else {
                inIntentFilter = !isClose && !isSelfClose;
                if (!isClose) sawAnyIntentFilter = true;
            }
        } else if (tagName == "data" && serviceInFilter && !isClose) {
            currentService.intentFilters.back().hasData = true;
        } else if (tagName == "meta-data" && inService && !isClose) {
            MetaDataEntry entry;
            entry.name = extractXmlAttr(tag, "name");
            entry.value = extractXmlAttr(tag, "value");
            entry.valueType = 3;
            std::string resource = extractXmlAttr(tag, "resource");
            entry.isResource = !resource.empty();
            if (entry.isResource) { entry.value = resource; entry.valueType = 1; }
            if (!entry.name.empty()) currentService.metaData.push_back(entry);
        } else if (tagName == "application") {
            if (!isClose) {
                std::string enabled = extractXmlAttr(tag, "enabled");
                info.applicationEnabled = enabled != "false" && enabled != "0";
                info.applicationPermission = extractXmlAttr(tag, "permission");
                info.applicationTheme = extractXmlAttr(tag, "theme");
                info.appLabel = extractXmlAttr(tag, "label");
                info.appIcon = extractXmlAttr(tag, "icon");
            }
            if (!isClose && info.appClass.empty()) {
                info.appClass = extractXmlAttr(tag, "name");
            }
            if (!isClose && info.appComponentFactory.empty()) {
                info.appComponentFactory = extractXmlAttr(tag, "appComponentFactory");
            }
        } else if (tagName == "action") {
            if (serviceInFilter && !isClose)
                currentService.intentFilters.back().actions.push_back(extractXmlAttr(tag, "name"));
            if (inIntentFilter && extractXmlAttr(tag, "name") == kActionMain) sawActionMain = true;
        } else if (tagName == "category") {
            if (serviceInFilter && !isClose)
                currentService.intentFilters.back().categories.push_back(extractXmlAttr(tag, "name"));
            if (inIntentFilter && extractXmlAttr(tag, "name") == kCategoryLauncher) sawCategoryLauncher = true;
        }

        cur = end + 1;
    }

    auto qualify = [&](std::string& name) {
        if (name.empty() || info.packageName.empty()) return;
        if (name.front() == '.') {
            name = info.packageName + name;
        } else if (name.find('.') == std::string::npos) {
            name = info.packageName + "." + name;
        }
    };
    qualify(info.mainActivity);
    qualify(info.appClass);
    qualify(info.appComponentFactory);
    for (ActivityEntry& e : info.activities) qualify(e.name);
    for (ServiceEntry& e : info.services) qualify(e.name);

    return info;
}

namespace {
bool inspectPreviewAPK(const std::string& path, const std::string& locale,
                       PackagePreview* preview, std::vector<std::string>* iconPaths,
                       std::uint32_t* iconId) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 22) return false;
    const auto length = file.tellg();
    std::vector<ZipEntryInfo> entries;
    if (!readZipEntries(file, length, entries)) return false;
    auto read = [&](const ZipEntryInfo& entry, uint32_t limit, std::vector<uint8_t>* bytes) {
        file.clear();
        return entry.uncompressedSize <= limit && entry.compressedSize <= limit &&
               extractZipEntryToMemory(file, entry, *bytes);
    };
    ManifestInfo manifest;
    std::vector<uint8_t> table;
    for (const auto& entry : entries) {
        if (entry.name == "AndroidManifest.xml") {
            std::vector<uint8_t> bytes;
            if (!read(entry, 16u * 1024 * 1024, &bytes)) return false;
            manifest = APKExtractor::parse_manifest(bytes.data(), bytes.size());
            if (manifest.packageName.empty()) manifest = APKExtractor::parse_manifest_text(
                    reinterpret_cast<const char*>(bytes.data()), bytes.size());
        } else if (entry.name == "resources.arsc") {
            if (!read(entry, 128u * 1024 * 1024, &table)) return false;
        }
    }
    if (manifest.packageName.empty()) return false;
    preview->packageName = manifest.packageName;
    std::vector<std::string> resolved;
    if (resource_resolve_strings(table, {manifest.appLabel}, locale, &resolved)) {
        preview->label = resolved[0];
    } else preview->label = manifest.appLabel;
    if (preview->label.empty() || preview->label.front() == '@')
        preview->label = prettifyAppName(manifest.packageName);

    std::vector<std::string> preferred;
    std::uint32_t id = resolveIconPaths(
        manifest.appIcon, table, entries,
        [&](const ZipEntryInfo& entry, std::vector<std::uint8_t>* bytes) {
            return read(entry, 16u * 1024 * 1024, bytes);
        }, &preferred);
    if (iconPaths != nullptr) *iconPaths = preferred;
    if (iconId != nullptr) *iconId = id;

    const IconPick pick = pickIcon(entries, preferred);
    if (pick.entry != nullptr &&
        (!read(*pick.entry, 16u * 1024 * 1024, &preview->icon) ||
         !isBitmapData(preview->icon))) {
        preview->icon.clear();
    }
    return true;
}
}

bool APKExtractor::preview_package(const std::string& path, const std::string& workDirectory,
                                  const std::string& locale, PackagePreview* preview) {
    gLastError.clear();
    if (!preview) return false;
    *preview = PackagePreview{};
    std::vector<std::string> iconPaths;
    std::uint32_t iconId = 0;
    if (!is_bundle_container(path)) {
        if (inspectPreviewAPK(path, locale, preview, &iconPaths, &iconId)) return true;
        gLastError = "Could not read application metadata";
        return false;
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { gLastError = "Could not open package container"; return false; }
    const auto size = file.tellg();
    std::vector<ZipEntryInfo> entries;
    if (!readZipEntries(file, size, entries)) return false;
    std::vector<ZipEntryInfo> candidates;
    for (const auto& entry : entries) {
        if (endsWithCi(entry.name, ".apk") && (entry.name.find('/') == std::string::npos ||
                entry.name.rfind("splits/", 0) == 0)) candidates.push_back(entry);
    }
    auto rank = [](const ZipEntryInfo& entry) {
        const std::string name = toLower(std::filesystem::path(entry.name).filename().string());
        if (name == "base.apk" || name == "base-master.apk") return 3;
        if (name.find("base") != std::string::npos) return 2;
        return name.find("config") == std::string::npos && name.find("split") == std::string::npos ? 1 : 0;
    };
    std::stable_sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) { return rank(a) > rank(b); });
    const auto temporary = std::filesystem::path(workDirectory) / "preview-base.apk";
    struct Remove { std::filesystem::path path; ~Remove() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{temporary};
    bool found = false;
    uint64_t remaining = 4ull * 1024 * 1024 * 1024;
    uint32_t baseOffset = UINT32_MAX;
    size_t attempts = 0;
    for (const auto& entry : candidates) {
        if (++attempts > 32) break;
        if (entry.uncompressedSize == 0 || entry.uncompressedSize > remaining ||
            entry.uncompressedSize > 2ull * 1024 * 1024 * 1024) continue;
        remaining -= entry.uncompressedSize;
        if (!extractZipEntryToFile(file, entry, temporary.string(), entry.uncompressedSize)) continue;
        if (inspectPreviewAPK(temporary.string(), locale, preview, &iconPaths, &iconId)) {
            found = true; baseOffset = entry.localOffset; break;
        }
    }
    if (!found) { gLastError = "Could not locate a base APK with application metadata"; return false; }
    // Density splits may contain the raster icon referenced by the base table.
    // Resolve the same resource id against each split's own table, because a
    // density split maps that id to a different file path.
    if (preview->icon.empty() && iconId != 0) {
        attempts = 0;
        int bestScore = -1;
        for (const auto& entry : candidates) {
            if (++attempts > 32) break;
            if (entry.localOffset == baseOffset || entry.uncompressedSize == 0 ||
                entry.uncompressedSize > remaining ||
                entry.uncompressedSize > 2ull * 1024 * 1024 * 1024) continue;
            const std::string lower = toLower(entry.name);
            if (lower.find("armeabi") != std::string::npos ||
                lower.find("x86") != std::string::npos ||
                lower.find("mips") != std::string::npos) continue;
            remaining -= entry.uncompressedSize;
            if (!extractZipEntryToFile(file, entry, temporary.string(), entry.uncompressedSize)) continue;
            std::ifstream split(temporary, std::ios::binary | std::ios::ate);
            if (!split) continue;
            const auto splitSize = split.tellg();
            std::vector<ZipEntryInfo> items;
            if (!readZipEntries(split, splitSize, items)) continue;

            std::vector<std::string> preferred = iconPaths;
            for (const auto& item : items) {
                if (item.name != "resources.arsc" || item.uncompressedSize > 128u * 1024 * 1024 ||
                    item.compressedSize > 128u * 1024 * 1024) continue;
                std::vector<std::uint8_t> splitTable;
                if (extractZipEntryToMemory(split, item, splitTable)) {
                    std::vector<std::string> paths;
                    if (resource_id_file_paths(splitTable, iconId, &paths)) {
                        for (const auto& path : paths) appendUnique(&preferred, path);
                    }
                }
                break;
            }

            const IconPick pick = pickIcon(items, preferred);
            if (pick.entry == nullptr || pick.score <= bestScore ||
                pick.entry->uncompressedSize > 16u * 1024 * 1024 ||
                pick.entry->compressedSize > 16u * 1024 * 1024) continue;
            std::vector<std::uint8_t> bytes;
            if (!extractZipEntryToMemory(split, *pick.entry, bytes) || !isBitmapData(bytes)) continue;
            bestScore = pick.score;
            preview->icon = std::move(bytes);
        }
    }
    return true;
}

std::string APKExtractor::get_package_name(const std::string& apkPath) {
    std::ifstream f(apkPath, std::ios::binary);
    if (f) {
        f.seekg(0, std::ios::end);
        const auto size = f.tellg();
        f.seekg(0, std::ios::beg);
        if (size >= 22) {
            std::vector<ZipEntryInfo> allEntries;
            if (readZipEntries(f, size, allEntries)) {
                for (const auto& e : allEntries) {
                    if (e.name == "AndroidManifest.xml") {
                        f.seekg(e.localOffset);
                        char lhdr[30];
                        if (readExact(f, lhdr, 30) && read32b(lhdr, 0) == 0x04034b50) {
                            const std::uint16_t nameLen = read16b(lhdr, 26);
                            const std::uint16_t extraLen = read16b(lhdr, 28);
                            f.seekg(e.localOffset + 30 + nameLen + extraLen);
                            std::vector<std::uint8_t> comp(e.compressedSize);
                            if (readExact(f, comp.data(), e.compressedSize)) {
                                std::vector<std::uint8_t> uncomp;
                                if (e.compression == 0) {
                                    uncomp = std::move(comp);
                                } else if (e.compression == 8) {
                                    uncomp.resize(e.compressedSize * 4 + 4096);
                                    z_stream stream = {};
                                    stream.next_in = comp.data();
                                    stream.avail_in = static_cast<uInt>(comp.size());
                                    stream.next_out = uncomp.data();
                                    stream.avail_out = static_cast<uInt>(uncomp.size());
                                    if (inflateInit2(&stream, -MAX_WBITS) == Z_OK) {
                                        inflate(&stream, Z_FINISH);
                                        uncomp.resize(stream.total_out);
                                        inflateEnd(&stream);
                                    }
                                }
                                if (!uncomp.empty()) {
                                    ManifestInfo info = parseAxml(uncomp);
                                    if (!info.packageName.empty()) {
                                        return info.packageName;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Remove dotted numeric versions while preserving package underscores.
    std::string stem = std::filesystem::path(apkPath).stem().string();
    const auto idx = stem.rfind('_');
    if (idx != std::string::npos) {
        const std::string part = stem.substr(0, idx), version = stem.substr(idx + 1);
        if (part.find('.') != std::string::npos && version.find('.') != std::string::npos &&
            version.find_first_not_of("0123456789.") == std::string::npos) return part;
    }
    return stem;
}

} // namespace kudroid
