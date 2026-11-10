static std::vector<std::string> parseStringPool(const std::vector<std::uint8_t>& data, std::size_t poolOffset) {
    std::vector<std::string> stringPool;
    if (poolOffset + 28 > data.size() || read16(data, poolOffset) != 0x0001) return stringPool;

    const std::uint32_t stringCount = read32(data, poolOffset + 8);
    const std::uint32_t flags = read32(data, poolOffset + 16);
    const std::uint32_t stringsStart = poolOffset + read32(data, poolOffset + 20);
    const bool isUtf8 = (flags & (1 << 8)) != 0;

    stringPool.reserve(stringCount);
    const std::size_t offsetsBase = poolOffset + 28;
    for (std::uint32_t i = 0; i < stringCount; ++i) {
        if (offsetsBase + i * 4 + 4 > data.size()) break;
        const std::uint32_t strOffset = stringsStart + read32(data, offsetsBase + i * 4);
        if (strOffset >= data.size()) { stringPool.push_back(""); continue; }

        if (isUtf8) {
            std::size_t cur = strOffset;
            if (cur >= data.size()) { stringPool.push_back(""); continue; }
            if (data[cur] & 0x80) cur += 2; else cur += 1;
            if (cur >= data.size()) { stringPool.push_back(""); continue; }
            std::size_t byteLen = 0;
            if (data[cur] & 0x80) {
                if (cur + 1 >= data.size()) { stringPool.push_back(""); continue; }
                byteLen = ((data[cur] & 0x7F) << 8) | data[cur + 1];
                cur += 2;
            } else {
                byteLen = data[cur];
                cur += 1;
            }
            if (cur + byteLen <= data.size()) {
                stringPool.emplace_back(reinterpret_cast<const char*>(data.data() + cur), byteLen);
            } else {
                stringPool.push_back("");
            }
        } else {
            std::size_t cur = strOffset;
            if (cur + 2 > data.size()) { stringPool.push_back(""); continue; }
            std::uint16_t charLen = data[cur] | (data[cur + 1] << 8);
            cur += 2;
            if (charLen & 0x8000) {
                if (cur + 2 > data.size()) { stringPool.push_back(""); continue; }
                charLen = ((charLen & 0x7FFF) << 16) | (data[cur] | (data[cur + 1] << 8));
                cur += 2;
            }
            std::string s;
            for (std::uint32_t c = 0; c < charLen && cur + 2 <= data.size(); ++c, cur += 2) {
                std::uint16_t ch = data[cur] | (data[cur + 1] << 8);
                if (ch < 0x80) {
                    s.push_back(static_cast<char>(ch));
                } else if (ch < 0x800) {
                    s.push_back(static_cast<char>(0xC0 | (ch >> 6)));
                    s.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
                } else {
                    s.push_back(static_cast<char>(0xE0 | (ch >> 12)));
                    s.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
                    s.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
                }
            }
            stringPool.push_back(s);
        }
    }
    return stringPool;
}

static ManifestInfo parseAxml(const std::vector<std::uint8_t>& data) {
    ManifestInfo info;
    // Diagnostics: The actual manifest of some APK repacks has been parsed and is empty
    // no clue — log enough for remote debugging via logs/.
    if (data.size() < 32 || read32(data, 0) != 0x00080003) {
        char magic[128];
        std::snprintf(magic, sizeof(magic),
                      "[kudroid_axml] reject: size=%zu magic=0x%02x%02x%02x%02x (expect 0x00080003)",
                      data.size(),
                      data.size() > 3 ? data[3] : 0, data.size() > 2 ? data[2] : 0,
                      data.size() > 1 ? data[1] : 0, data.size() > 0 ? data[0] : 0);
        apkLog(magic);
        return info;
    }

    const std::size_t poolOffset = 8;
    std::vector<std::string> stringPool = parseStringPool(data, poolOffset);
    if (stringPool.empty()) {
        apkLog("[kudroid_axml] string pool EMPTY (type=" +
               std::to_string(read16(data, poolOffset)) + ")");
        return info;
    }

    std::size_t cur = poolOffset + read32(data, poolOffset + 4);
    if (cur + 8 <= data.size() && read32(data, cur) == 0x00080180) {
        cur += read32(data, cur + 4);
    }

    // State finds LAUNCHER activity: manifest contains MANY <activity>, only
    // activity (or activity-alias) has <intent-filter> with
    // <action android:name="android.intent.action.MAIN"> +
    // <category android:name="android.intent.category.LAUNCHER"> is
    // entry point that the Android launcher opens. Get the first activity as before
    // This is WRONG (e.g. Minecraft: the first activity is not a launcher).
    // IMPORTANT: state must be OUTSIDE the while loop — AXML splits into multiple
    // chunk (one chunk per tag), declared in loop will reset each chunk.
    static const char* kActionMain = "android.intent.action.MAIN";
    static const char* kCategoryLauncher = "android.intent.category.LAUNCHER";
    std::string currentActivity;   // name of the <activity>/<activity-alias> being browsed
    std::string aliasTarget;       // targetActivity of <activity-alias>
    std::string currentOrientationStr;
    std::string currentTheme;
    int currentOrientation = -1;
    bool inIntentFilter = false;
    bool sawActionMain = false;
    bool sawCategoryLauncher = false;
    bool sawAnyIntentFilter = false;
    bool currentExported = false;
    bool currentExportedSet = false;
    std::string firstActivityName; // fallback when there is no intent-filter

    // <meta-data> state.
    //
    // Nesting is what makes this need state at all: the element appears inside
    // <activity>, inside <application>, and inside <service>/<provider>/<receiver>,
    // and the same key means different things depending on which. Tracking depth
    // rather than a single "in activity" flag is necessary because an <activity>
    // contains <intent-filter> too, so the parser is often two levels down.
    int elementDepth = 0;
    int applicationDepth = -1;   // depth of the open <application>, -1 when outside
    int activityDepth = -1;      // depth of the open <activity>/<activity-alias>
    std::vector<MetaDataEntry> currentActivityMeta;
    int serviceDepth = -1;
    int serviceFilterDepth = -1;
    ServiceEntry currentService;
    bool serviceExportedSet = false;
    bool servicePermissionSet = false;

    // Meta-data attributes arrive in any order and android:value may be absent when
    // the manifest uses android:resource instead, so accumulate then commit.
    std::string metaName;
    std::string metaValue;
    std::string metaResource;
    uint8_t metaValueType = 0;
    uint8_t metaResourceType = 0;
    bool sawMetaValue = false;

    // Record every activity, not just the launcher. Callers can then try real
    // manifest entries in order instead of guessing names from the package.
    auto commitActivity = [&](const std::string& name, bool isAlias) {
        if (name.empty()) return;
        for (ActivityEntry& e : info.activities) {
            if (e.name != name) continue;
            // Same activity seen again (multiple intent-filter blocks): merge.
            e.isLauncher = e.isLauncher || (sawActionMain && sawCategoryLauncher);
            if (!isAlias && e.theme.empty()) e.theme = currentTheme;
            e.isExported = e.isExported || currentExported || sawAnyIntentFilter;
            if (e.screenOrientation == -1 && currentOrientation != -1) {
                e.screenOrientation = currentOrientation;
                e.screenOrientationStr = currentOrientationStr;
            }
            for (const MetaDataEntry& m : currentActivityMeta) {
                bool dup = false;
                for (const MetaDataEntry& have : e.metaData) {
                    if (have.name == m.name) { dup = true; break; }
                }
                if (!dup) e.metaData.push_back(m);
            }
            return;
        }
        ActivityEntry e;
        e.name = name;
        if (!isAlias) e.theme = currentTheme;
        e.isLauncher = sawActionMain && sawCategoryLauncher;
        // An activity with an intent-filter is implicitly exported unless it says
        // otherwise, which is how the launcher can start it.
        e.isExported = currentExportedSet ? currentExported : sawAnyIntentFilter;
        e.isAlias = isAlias;
        e.screenOrientationStr = currentOrientationStr;
        e.screenOrientation = currentOrientation;
        e.metaData = currentActivityMeta;
        info.activities.push_back(e);
    };

    auto commitLauncher = [&](const std::string& name) {
        if (info.mainActivity.empty() && !name.empty() &&
            sawActionMain && sawCategoryLauncher) {
            info.mainActivity = name;
        }
    };

    while (cur + 8 <= data.size()) {
        const std::uint32_t chunkType = read32(data, cur);
        const std::uint32_t chunkSize = read32(data, cur + 4);
        if (chunkSize < 8 || cur + chunkSize > data.size()) break;

        if (chunkType == 0x00100102) { // RES_XML_START_ELEMENT_TYPE
            if (cur + 36 <= data.size()) {
                const std::uint32_t nameIdx = read32(data, cur + 20);
                const std::string tagName = (nameIdx < stringPool.size()) ? stringPool[nameIdx] : "";
                // Spec ResXMLTree_attrExt: attributeStart is the offset FROM START
                // struct attrExt (chunk+16), not from the beginning of the chunk. aapt writes
                // attrStart=20 → attrs starts at chunk+36. Old code used
                // cur+attrStart → reads 16 bytes off every real manifest
                // (synthetic test matches the old code so the password is fake).
                const std::uint16_t attrStart = read16(data, cur + 24);
                const std::uint16_t attrSize = read16(data, cur + 26);
                const std::uint16_t attrCount = read16(data, cur + 28);
                const std::size_t attrsBase = cur + 16 + attrStart;

                const bool isActivityTag =
                    tagName == "activity" || tagName == "activity-alias";

                // Depth is tracked for every element, including the ones this parser
                // otherwise ignores, because <meta-data> has to be attributed to
                // whichever element encloses it. AXML emits an END_ELEMENT even for a
                // self-closing tag, so the counts stay balanced.
                ++elementDepth;

                if (isActivityTag) {
                    // Start a new activity — reset the state of the previous activity.
                    currentActivity.clear();
                    aliasTarget.clear();
                    currentOrientationStr.clear();
                    currentTheme.clear();
                    currentOrientation = -1;
                    sawActionMain = false;
                    sawCategoryLauncher = false;
                    sawAnyIntentFilter = false;
                    currentExported = false;
                    currentExportedSet = false;
                    inIntentFilter = false;
                    currentActivityMeta.clear();
                    activityDepth = elementDepth;
                } else if (tagName == "service" && elementDepth == applicationDepth + 1) {
                    serviceDepth = elementDepth;
                    currentService = ServiceEntry{};
                    serviceExportedSet = servicePermissionSet = false;
                } else if (tagName == "application") {
                    applicationDepth = elementDepth;
                } else if (tagName == "intent-filter") {
                    if (serviceDepth >= 0) {
                        serviceFilterDepth = elementDepth;
                        currentService.intentFilters.emplace_back();
                    } else {
                        inIntentFilter = true;
                        sawAnyIntentFilter = true;
                    }
                } else if (tagName == "data" && serviceFilterDepth >= 0) {
                    currentService.intentFilters.back().hasData = true;
                } else if (tagName == "meta-data") {
                    metaName.clear();
                    metaValue.clear();
                    metaResource.clear();
                    metaValueType = metaResourceType = 0;
                    sawMetaValue = false;
                }

                std::size_t attrCur = attrsBase;
                // attrSize below a full attribute entry means the typed-value
                // reads below would run on misaligned/short data: stop, don't
                // parse garbage as manifest fields.
                if (attrSize < 20) break;
                for (std::uint16_t a = 0; a < attrCount && attrCur + attrSize <= cur + chunkSize; ++a, attrCur += attrSize) {
                    const std::uint32_t attrNameIdx = read32(data, attrCur + 4);
                    const std::uint32_t attrRawValIdx = read32(data, attrCur + 8);
                    const std::string attrName = (attrNameIdx < stringPool.size()) ? stringPool[attrNameIdx] : "";
                    std::string attrVal = (attrRawValIdx < stringPool.size()) ? stringPool[attrRawValIdx] : "";

                    // If rawValue = -1 (0xFFFFFFFF), read from Res_value (typedValue data)
                    // Also record the type, because <meta-data> distinguishes a value
                    // that is genuinely absent from one that is the string "0" —
                    // android:value="false" arrives as TYPE_INT_BOOLEAN, not a string.
                    std::uint8_t attrType = 0;
                    if (attrCur + 20 <= cur + chunkSize) attrType = data[attrCur + 15];
                    if (attrVal.empty() && attrCur + 20 <= cur + chunkSize) {
                        const std::uint32_t dataVal = read32(data, attrCur + 16);
                        if (attrType == 0x03 /* TYPE_STRING */ && dataVal < stringPool.size()) {
                            attrVal = stringPool[dataVal];
                        } else if (attrType >= 0x10 && attrType <= 0x1f && attrType != 0x12) {
                            attrVal = std::to_string(dataVal);
                        } else if (attrType == 0x12 /* TYPE_INT_BOOLEAN */) {
                            // aapt writes 0xFFFFFFFF for true and 0 for false.
                            attrVal = dataVal != 0 ? "true" : "false";
                        } else if (attrType == 0x04 /* TYPE_FLOAT */) {
                            float f = 0.0f;
                            std::memcpy(&f, &dataVal, sizeof(f));
                            attrVal = std::to_string(f);
                        } else if (attrType == 0x01 /* TYPE_REFERENCE */) {
                            // android:value="@string/x" or android:resource="@raw/y".
                            // The id cannot be resolved without parsing resources.arsc,
                            // so record it in the form aapt would print. A caller that
                            // needs the text sees something identifiable rather than an
                            // empty string that reads as "the key is absent".
                            char ref[32];
                            std::snprintf(ref, sizeof(ref), "@0x%08x", dataVal);
                            attrVal = ref;
                        }
                    }

                    if (tagName == "manifest") {
                        if (attrName == "package" && info.packageName.empty()) info.packageName = attrVal;
                        if (attrName == "versionName" && info.versionName.empty()) info.versionName = attrVal;
                        if (attrName == "versionCode" && info.versionCode.empty()) info.versionCode = attrVal;
                    } else if (tagName == "application") {
                        if (attrName == "enabled") info.applicationEnabled = attrVal != "false" && attrVal != "0";
                        if (attrName == "permission") info.applicationPermission = attrVal;
                        if (attrName == "theme") info.applicationTheme = attrVal;
                        if (attrName == "icon") info.appIcon = attrVal;
                        if (attrName == "label" && info.appLabel.empty() && !attrVal.empty()) info.appLabel = attrVal;
                        // android:name on <application> is the custom Application
                        // subclass; Android instantiates it before any Activity.
                        if (attrName == "name" && info.appClass.empty() && !attrVal.empty()) info.appClass = attrVal;
                        if (attrName == "appComponentFactory" && info.appComponentFactory.empty() &&
                            !attrVal.empty()) {
                            info.appComponentFactory = attrVal;
                        }
                    } else if (tagName == "service" && serviceDepth >= 0) {
                        if (attrName == "name") currentService.name = attrVal;
                        if (attrName == "enabled") currentService.enabled = attrVal != "false" && attrVal != "0";
                        if (attrName == "permission") {
                            servicePermissionSet = true;
                            currentService.permission = attrVal;
                        }
                        if (attrName == "exported") {
                            serviceExportedSet = true;
                            currentService.exported = attrVal == "true" || attrVal == "1";
                        }
                    } else if (tagName == "intent-filter" && serviceFilterDepth >= 0) {
                        if (attrName == "priority") {
                            if (attrType >= 0x10 && attrType <= 0x1f) {
                                currentService.intentFilters.back().priority =
                                    static_cast<int32_t>(read32(data, attrCur + 16));
                            } else {
                                try { currentService.intentFilters.back().priority = std::stoi(attrVal); }
                                catch (...) {}
                            }
                        }
                    } else if (tagName == "meta-data") {
                        if (attrName == "name") {
                            metaName = attrVal;
                        } else if (attrName == "value") {
                            metaValue = attrVal;
                            metaValueType = attrType;
                            sawMetaValue = true;
                        } else if (attrName == "resource") {
                            metaResource = attrVal;
                            metaResourceType = attrType;
                        }
                    } else if (isActivityTag) {
                        if (attrName == "name" && currentActivity.empty() && !attrVal.empty()) {
                            currentActivity = attrVal;
                            if (firstActivityName.empty()) firstActivityName = attrVal;
                        } else if (tagName == "activity-alias" && attrName == "targetActivity" && !attrVal.empty()) {
                            aliasTarget = attrVal;
                        } else if (attrName == "exported") {
                            currentExportedSet = true;
                            // Boolean attributes arrive as "true"/"false" or as an int 0/1.
                            currentExported = (attrVal == "true" || attrVal == "1");
                        } else if (attrName == "theme" && tagName == "activity") {
                            currentTheme = attrVal;
                        } else if (attrName == "screenOrientation") {
                            currentOrientationStr = attrVal;
                            auto parseOrientationString = [](const std::string& str) -> int {
                                if (str.empty() || str == "unspecified" || str == "-1") return -1;
                                if (str == "landscape" || str == "0") return 0;
                                if (str == "portrait" || str == "1") return 1;
                                if (str == "user" || str == "2") return 2;
                                if (str == "behind" || str == "3") return 3;
                                if (str == "sensor" || str == "4") return 4;
                                if (str == "nosensor" || str == "5") return 5;
                                if (str == "sensorLandscape" || str == "6") return 6;
                                if (str == "sensorPortrait" || str == "7") return 7;
                                if (str == "reverseLandscape" || str == "8") return 8;
                                if (str == "reversePortrait" || str == "9") return 9;
                                if (str == "fullSensor" || str == "10") return 10;
                                if (str == "userLandscape" || str == "11") return 11;
                                if (str == "userPortrait" || str == "12") return 12;
                                if (str == "fullUser" || str == "13") return 13;
                                if (str == "locked" || str == "14") return 14;
                                try { return std::stoi(str); } catch (...) { return -1; }
                            };
                            currentOrientation = parseOrientationString(attrVal);
                        }
                    } else if (tagName == "action") {
                        if (serviceFilterDepth >= 0 && attrName == "name")
                            currentService.intentFilters.back().actions.push_back(attrVal);
                        if (inIntentFilter && attrName == "name" && attrVal == kActionMain) {
                            sawActionMain = true;
                        }
                    } else if (tagName == "category") {
                        if (serviceFilterDepth >= 0 && attrName == "name")
                            currentService.intentFilters.back().categories.push_back(attrVal);
                        if (inIntentFilter && attrName == "name" && attrVal == kCategoryLauncher) {
                            sawCategoryLauncher = true;
                        }
                    }
                }

                // Activity-alias points to the real activity via targetActivity —
                // Android launcher opens TARGET, not alias.
                if (tagName == "activity-alias") {
                    commitLauncher(!aliasTarget.empty() ? aliasTarget : currentActivity);
                }
                // Self-closing <activity .../> never produces an end element, so
                // record it here too; commitActivity dedupes.
                if (isActivityTag && !currentActivity.empty()) {
                    commitActivity(!aliasTarget.empty() ? aliasTarget : currentActivity,
                                   tagName == "activity-alias");
                }
            }
        } else if (chunkType == 0x00100103) { // RES_XML_END_ELEMENT_TYPE
            if (cur + 24 <= data.size()) {
                const std::uint32_t nameIdx = read32(data, cur + 20);
                const std::string tagName = (nameIdx < stringPool.size()) ? stringPool[nameIdx] : "";

                if (tagName == "meta-data") {
                    // Attribute it to the innermost open element. An <activity> wins
                    // over <application> because Android scopes it that way: two
                    // activities can carry different values for the same key, and
                    // reading the wrong one is worse than reading none.
                    if (!metaName.empty() && (sawMetaValue || !metaResource.empty())) {
                        MetaDataEntry entry;
                        entry.name = metaName;
                        entry.isResource = metaResourceType == 0x01 && !metaResource.empty()
                                           && metaResource != "@0x00000000";
                        entry.value = entry.isResource ? metaResource : metaValue;
                        entry.valueType = entry.isResource ? metaResourceType : metaValueType;
                        if (serviceDepth >= 0) {
                            currentService.metaData.push_back(entry);
                        } else if (activityDepth >= 0) {
                            bool dup = false;
                            for (const MetaDataEntry& m : currentActivityMeta) {
                                if (m.name == entry.name) { dup = true; break; }
                            }
                            if (!dup) currentActivityMeta.push_back(entry);
                        } else if (applicationDepth >= 0) {
                            bool dup = false;
                            for (const MetaDataEntry& m : info.applicationMetaData) {
                                if (m.name == entry.name) { dup = true; break; }
                            }
                            if (!dup) info.applicationMetaData.push_back(entry);
                        }
                    }
                    metaName.clear();
                    metaValue.clear();
                    sawMetaValue = false;
                } else if (tagName == "intent-filter") {
                    inIntentFilter = false;
                    serviceFilterDepth = -1;
                } else if (tagName == "service" && serviceDepth >= 0) {
                    if (!serviceExportedSet) currentService.exported = !currentService.intentFilters.empty();
                    if (!servicePermissionSet) currentService.permission = info.applicationPermission;
                    if (!currentService.name.empty()) info.services.push_back(currentService);
                    serviceDepth = serviceFilterDepth = -1;
                } else if (tagName == "activity" || tagName == "activity-alias") {
                    // </activity>: if intent-filter MAIN+LAUNCHER appears between
                    // Apparently this is the launcher activity.
                    commitLauncher(currentActivity);
                    commitActivity(!aliasTarget.empty() ? aliasTarget : currentActivity,
                                   tagName == "activity-alias");
                    currentActivity.clear();
                    aliasTarget.clear();
                    sawActionMain = false;
                    sawCategoryLauncher = false;
                    sawAnyIntentFilter = false;
                    currentExported = false;
                    currentExportedSet = false;
                    inIntentFilter = false;
                    currentActivityMeta.clear();
                    activityDepth = -1;
                } else if (tagName == "application") {
                    applicationDepth = -1;
                }

                if (elementDepth > 0) --elementDepth;
            }
        }
        cur += chunkSize;
    }

    // Fallback: no intent-filter found (non-standard/obfuscated manifest)
    // → use the first activity as the old behavior, better than empty.
    if (info.mainActivity.empty() && !firstActivityName.empty()) {
        info.mainActivity = firstActivityName;
    }

    // Manifests may write ".MainActivity" or even a bare "MainActivity" relative to
    // the package; expand both so callers always hold a resolvable class name.
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

static std::string parseArscAppName(const std::vector<std::uint8_t>& data, std::uint32_t targetResId = 0) {
    if (data.size() < 12 || read16(data, 0) != 0x0002) return "";

    const std::size_t poolOffset = read16(data, 2);
    if (poolOffset + 28 > data.size() || read16(data, poolOffset) != 0x0001) return "";

    std::vector<std::string> globalStrings = parseStringPool(data, poolOffset);
    if (globalStrings.empty()) return "";

    std::size_t cur = poolOffset + read32(data, poolOffset + 4);
    std::string bestName;
    std::string fallbackName;

    const std::uint8_t targetTypeId = (targetResId >> 16) & 0xFF;
    const std::uint16_t targetEntryId = targetResId & 0xFFFF;

    while (cur + 8 <= data.size()) {
        const std::uint16_t chunkType = read16(data, cur);
        const std::uint32_t chunkSize = read32(data, cur + 4);
        if (chunkSize < 8 || cur + chunkSize > data.size()) break;

        if (chunkType == 0x0200 && cur + 284 <= data.size()) { // RES_TABLE_PACKAGE_TYPE
            const std::uint32_t keyStringsRel = read32(data, cur + 276);
            if (keyStringsRel < chunkSize && cur + keyStringsRel + 28 <= data.size()) {
                const std::vector<std::string> keyStrings = parseStringPool(data, cur + keyStringsRel);
                std::unordered_set<std::uint32_t> targetKeyIndices;
                for (std::size_t k = 0; k < keyStrings.size(); ++k) {
                    const std::string lower = toLower(keyStrings[k]);
                    if (lower == "app_name" || lower == "app_label" || lower == "application_name" ||
                        lower == "title_activity_main" || lower == "activity_main_title" ||
                        lower == "game_name") {
                        targetKeyIndices.insert(static_cast<std::uint32_t>(k));
                    }
                }

                const std::uint16_t pkgHdrSize = read16(data, cur + 2);
                std::size_t subCur = cur + (pkgHdrSize >= 284 ? pkgHdrSize : 288);
                const std::size_t pkgEnd = cur + chunkSize;

                while (subCur + 8 <= pkgEnd && subCur + 8 <= data.size()) {
                    const std::uint16_t subType = read16(data, subCur);
                    const std::uint16_t subHdrSize = read16(data, subCur + 2);
                    const std::uint32_t subSize = read32(data, subCur + 4);
                    if (subSize < 8 || subCur + subSize > pkgEnd || subCur + subSize > data.size()) break;

                    if (subType == 0x0201 && subCur + 20 <= pkgEnd) { // RES_TABLE_TYPE_TYPE
                        const std::uint8_t typeId = data[subCur + 8];
                        const std::uint32_t entryCount = read32(data, subCur + 12);
                        const std::uint32_t entriesStart = read32(data, subCur + 16);

                        bool isDefaultLocale = true;
                        if (subHdrSize >= 32 && subCur + 32 <= pkgEnd) {
                            const char lang0 = static_cast<char>(data[subCur + 28]);
                            const char lang1 = static_cast<char>(data[subCur + 29]);
                            if ((lang0 != 0 || lang1 != 0) && (lang0 != 'e' || lang1 != 'n')) {
                                isDefaultLocale = false;
                            }
                        }

                        // 1. Direct match by target resource ID if provided
                        if (targetResId != 0 && typeId == targetTypeId && targetEntryId < entryCount) {
                            const std::size_t offPos = subCur + subHdrSize + static_cast<std::size_t>(targetEntryId) * 4;
                            if (offPos + 4 <= subCur + subSize) {
                                const std::uint32_t entryOff = read32(data, offPos);
                                if (entryOff != 0xFFFFFFFF) {
                                    const std::size_t entryPtr = subCur + entriesStart + entryOff;
                                    if (entryPtr + 8 <= subCur + subSize) {
                                        const std::uint16_t entrySize = read16(data, entryPtr);
                                        const std::uint16_t flags = read16(data, entryPtr + 2);
                                        if (!(flags & 0x0001 /* FLAG_COMPLEX */)) {
                                            const std::size_t valOffset = entryPtr + entrySize;
                                            if (valOffset + 8 <= subCur + subSize) {
                                                const std::uint8_t dataType = data[valOffset + 3];
                                                const std::uint32_t dataVal = read32(data, valOffset + 4);
                                                if (dataType == 0x03 /* TYPE_STRING */ && dataVal < globalStrings.size()) {
                                                    const std::string& found = globalStrings[dataVal];
                                                    if (!found.empty() && found.rfind("http", 0) != 0 && found.find('/') == std::string::npos) {
                                                        if (isDefaultLocale) return found;
                                                        if (bestName.empty()) bestName = found;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // 2. Scan entries matching known app name keys
                        if (!targetKeyIndices.empty() && (targetResId == 0 || bestName.empty())) {
                            for (std::uint32_t e = 0; e < entryCount; ++e) {
                                const std::size_t offPos = subCur + subHdrSize + static_cast<std::size_t>(e) * 4;
                                if (offPos + 4 > subCur + subSize) break;
                                const std::uint32_t entryOff = read32(data, offPos);
                                if (entryOff == 0xFFFFFFFF) continue;

                                const std::size_t entryPtr = subCur + entriesStart + entryOff;
                                if (entryPtr + 8 > subCur + subSize) continue;

                                const std::uint16_t entrySize = read16(data, entryPtr);
                                const std::uint16_t flags = read16(data, entryPtr + 2);
                                const std::uint32_t keyIndex = read32(data, entryPtr + 4);

                                if (targetKeyIndices.count(keyIndex) && !(flags & 0x0001 /* FLAG_COMPLEX */)) {
                                    const std::size_t valOffset = entryPtr + entrySize;
                                    if (valOffset + 8 <= subCur + subSize) {
                                        const std::uint8_t dataType = data[valOffset + 3];
                                        const std::uint32_t dataVal = read32(data, valOffset + 4);
                                        if (dataType == 0x03 /* TYPE_STRING */ && dataVal < globalStrings.size()) {
                                            const std::string& found = globalStrings[dataVal];
                                            if (!found.empty() && found.rfind("http", 0) != 0 && found.find('/') == std::string::npos) {
                                                if (isDefaultLocale) {
                                                    if (bestName.empty()) bestName = found;
                                                } else {
                                                    if (fallbackName.empty()) fallbackName = found;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    subCur += (subSize >= 8 ? subSize : 8);
                }
            }
        }
        cur += chunkSize;
    }

    return !bestName.empty() ? bestName : fallbackName;
}
} // namespace

static std::string prettifyAppName(const std::string& raw) {
    if (raw.empty()) return "Android App";
    std::string s = raw;

    // 1. Separate package name if there is one (e.g. "com.example.app.google")
    if (s.find('.') != std::string::npos) {
        std::vector<std::string> parts;
        std::stringstream ss(s);
        std::string seg;
        while (std::getline(ss, seg, '.')) {
            if (!seg.empty()) parts.push_back(seg);
        }
        if (!parts.empty()) {
            std::size_t chosenIdx = parts.size() - 1;
            static const std::unordered_set<std::string> flavorSuffixes = {
                "google", "android", "store", "play", "release", "debug",
                "beta", "alpha", "taptap", "bilibili", "amazon", "dist"
            };
            if (parts.size() > 1 && flavorSuffixes.count(toLower(parts.back()))) {
                chosenIdx = parts.size() - 2;
            }
            s = parts[chosenIdx];
        }
    }

    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".apk") == 0) {
        s = s.substr(0, s.size() - 4);
    }

    // The cleanup below (strip repacker tags and version numbers, then title-case)
    // derives a readable name from any package or file name. A table mapping specific
    // apps to their branded spelling used to sit here — "minecraft"/"mojang" to
    // "Minecraft", plus Discord, ULTRAKILL and Rolling Sky — which only ever helped
    // four apps and mislabelled anything whose name happened to contain one of those
    // words.

    // 2. Separate common junk prefixes/suffixes in mod/port APK file names
    for (char& c : s) {
        if (c == '-' || c == '_') c = ' ';
    }

    std::stringstream ss(s);
    std::string word;
    std::vector<std::string> validWords;
    while (ss >> word) {
        std::string wLower = toLower(word);
        if (wLower == "apk" || wLower == "arm64" || wLower == "arm64v8a" || wLower == "v8a" ||
            wLower == "vulkan" || wLower == "gles" || wLower == "mod" || wLower == "signed" ||
            wLower == "release" || wLower == "debug" || wLower == "beta" || wLower == "alpha" ||
            wLower == "jakitomzed" || wLower == "bandishare" || wLower == "apkpure" ||
            wLower == "moddroid" || wLower == "an1" || wLower == "apkmirror") {
            continue;
        }
        // Ignore pure numeric version e.g. "5.5.8" or "v202"
        bool isVer = true;
        for (char c : word) {
            if (!std::isdigit(c) && c != '.' && c != 'v' && c != 'V') { isVer = false; break; }
        }
        if (!isVer) {
            validWords.push_back(word);
        }
    }

    if (!validWords.empty()) {
        s = "";
        for (std::size_t i = 0; i < validWords.size(); ++i) {
            if (i > 0) s += " ";
            s += validWords[i];
        }
    }

    std::string result;
    bool newWord = true;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == ' ') {
            if (!result.empty() && result.back() != ' ') result += ' ';
            newWord = true;
        } else {
            if (i > 0 && std::islower(s[i - 1]) && std::isupper(c) && !result.empty() && result.back() != ' ') {
                result += ' ';
                newWord = false;
            }
            if (newWord) {
                result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                newWord = false;
            } else {
                result += c;
            }
        }
    }

    return result.empty() ? raw : result;
}

