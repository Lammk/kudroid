#include "kudroid/PermissionManager.h"
#include "kudroid/Log.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstring>

namespace kudroid {

PermissionManager& PermissionManager::getInstance() {
    static PermissionManager instance;
    return instance;
}

PermissionManager::PermissionManager() {
    // Định nghĩa các nhóm quyền Android chuẩn
    groups_ = {
        {
            "storage",
            "Storage Access",
            "Read and write files, photos, downloads and SD card",
            {
                "android.permission.READ_EXTERNAL_STORAGE",
                "android.permission.WRITE_EXTERNAL_STORAGE",
                "android.permission.MANAGE_EXTERNAL_STORAGE",
                "android.permission.READ_MEDIA_IMAGES",
                "android.permission.READ_MEDIA_VIDEO",
                "android.permission.READ_MEDIA_AUDIO"
            },
            true // Default: Granted
        },
        {
            "network",
            "Internet & Network",
            "Access Wi-Fi and mobile network connections",
            {
                "android.permission.INTERNET",
                "android.permission.ACCESS_NETWORK_STATE",
                "android.permission.ACCESS_WIFI_STATE",
                "android.permission.CHANGE_NETWORK_STATE",
                "android.permission.CHANGE_WIFI_STATE"
            },
            true // Default: Granted
        },
        {
            "camera",
            "Camera",
            "Take pictures and record video",
            {
                "android.permission.CAMERA"
            },
            false // Default: Ask / Off
        },
        {
            "microphone",
            "Microphone & Audio",
            "Record audio and use voice chat",
            {
                "android.permission.RECORD_AUDIO",
                "android.permission.MODIFY_AUDIO_SETTINGS"
            },
            false
        },
        {
            "location",
            "Location",
            "Access device GPS and approximate location",
            {
                "android.permission.ACCESS_FINE_LOCATION",
                "android.permission.ACCESS_COARSE_LOCATION"
            },
            false
        },
        {
            "bluetooth",
            "Bluetooth & Gamepads",
            "Connect to wireless game controllers and devices",
            {
                "android.permission.BLUETOOTH",
                "android.permission.BLUETOOTH_ADMIN",
                "android.permission.BLUETOOTH_CONNECT",
                "android.permission.BLUETOOTH_SCAN"
            },
            true // Default: Granted for controllers
        }
    };
}

void PermissionManager::init(const std::string& documentsDir) {
    std::lock_guard<std::mutex> lock(mutex_);
    configPath_ = documentsDir + "/android_root/data/system/packages_permissions.json";
    
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(configPath_).parent_path(), ec);
    
    loadConfigLocked();
    initialized_ = true;
    KLOG(kInfo, "PermissionManager", "Initialized with config at %s", configPath_.c_str());
}

void PermissionManager::loadConfigLocked() {
    if (configPath_.empty() || !std::filesystem::exists(configPath_)) {
        return;
    }

    std::ifstream file(configPath_);
    if (!file.is_open()) return;

    std::string line;
    std::string currentPkg;
    
    // Simple parser for standard json key-value
    while (std::getline(file, line)) {
        size_t pkgPos = line.find("\"package\":");
        if (pkgPos != std::string::npos) {
            size_t q1 = line.find('"', pkgPos + 10);
            size_t q2 = line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                currentPkg = line.substr(q1 + 1, q2 - q1 - 1);
            }
        }
        
        if (!currentPkg.empty()) {
            for (const auto& group : groups_) {
                std::string keyPattern = "\"" + group.key + "\":";
                size_t keyPos = line.find(keyPattern);
                if (keyPos != std::string::npos) {
                    bool granted = (line.find("true", keyPos) != std::string::npos);
                    for (const auto& perm : group.androidPermissions) {
                        appPermissions_[currentPkg][perm] = granted;
                    }
                }
            }
        }
    }
}

void PermissionManager::saveConfigLocked() {
    if (configPath_.empty()) return;

    std::ofstream file(configPath_, std::ios::trunc);
    if (!file.is_open()) return;

    file << "{\n  \"apps\": [\n";
    bool firstApp = true;
    for (const auto& [pkg, perms] : appPermissions_) {
        if (!firstApp) file << ",\n";
        firstApp = false;
        file << "    {\n";
        file << "      \"package\": \"" << pkg << "\",\n";
        file << "      \"groups\": {\n";
        
        bool firstGroup = true;
        for (const auto& group : groups_) {
            if (!firstGroup) file << ",\n";
            firstGroup = false;
            
            // Check if primary perm is granted
            bool granted = group.defaultGranted;
            if (!group.androidPermissions.empty()) {
                auto it = perms.find(group.androidPermissions[0]);
                if (it != perms.end()) granted = it->second;
            }
            file << "        \"" << group.key << "\": " << (granted ? "true" : "false");
        }
        file << "\n      }\n    }";
    }
    file << "\n  ]\n}\n";
}

int PermissionManager::checkPermission(const std::string& packageName, const std::string& permissionName) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Tìm trong cấu hình của app
    auto appIt = appPermissions_.find(packageName);
    if (appIt != appPermissions_.end()) {
        auto permIt = appIt->second.find(permissionName);
        if (permIt != appIt->second.end()) {
            return permIt->second ? PERMISSION_GRANTED : PERMISSION_DENIED;
        }
    }

    // Nếu chưa cấu hình, kiểm tra mặc định của nhóm quyền
    for (const auto& group : groups_) {
        for (const auto& perm : group.androidPermissions) {
            if (perm == permissionName) {
                return group.defaultGranted ? PERMISSION_GRANTED : PERMISSION_DENIED;
            }
        }
    }

    // Mặc định cho phép các quyền thông thường
    return PERMISSION_GRANTED;
}

void PermissionManager::setGroupPermission(const std::string& packageName, const std::string& groupKey, bool granted) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& group : groups_) {
        if (group.key == groupKey) {
            for (const auto& perm : group.androidPermissions) {
                appPermissions_[packageName][perm] = granted;
            }
            break;
        }
    }
    saveConfigLocked();
}

bool PermissionManager::isGroupGranted(const std::string& packageName, const std::string& groupKey) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& group : groups_) {
        if (group.key == groupKey && !group.androidPermissions.empty()) {
            auto appIt = appPermissions_.find(packageName);
            if (appIt != appPermissions_.end()) {
                auto permIt = appIt->second.find(group.androidPermissions[0]);
                if (permIt != appIt->second.end()) {
                    return permIt->second;
                }
            }
            return group.defaultGranted;
        }
    }
    return true;
}

void PermissionManager::setPermission(const std::string& packageName, const std::string& permissionName, bool granted) {
    std::lock_guard<std::mutex> lock(mutex_);
    appPermissions_[packageName][permissionName] = granted;
    saveConfigLocked();
}

void PermissionManager::grantAll(const std::string& packageName) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& group : groups_) {
        for (const auto& perm : group.androidPermissions) {
            appPermissions_[packageName][perm] = true;
        }
    }
    saveConfigLocked();
}

const std::vector<PermissionGroup>& PermissionManager::getPermissionGroups() const {
    return groups_;
}

std::string PermissionManager::getAppPermissionsJson(const std::string& packageName) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"package\": \"" << packageName << "\",\n";
    ss << "  \"groups\": [\n";
    
    bool first = true;
    for (const auto& group : groups_) {
        if (!first) ss << ",\n";
        first = false;
        
        bool granted = group.defaultGranted;
        auto appIt = appPermissions_.find(packageName);
        if (appIt != appPermissions_.end() && !group.androidPermissions.empty()) {
            auto permIt = appIt->second.find(group.androidPermissions[0]);
            if (permIt != appIt->second.end()) {
                granted = permIt->second;
            }
        }

        ss << "    {\n";
        ss << "      \"key\": \"" << group.key << "\",\n";
        ss << "      \"displayName\": \"" << group.displayName << "\",\n";
        ss << "      \"description\": \"" << group.description << "\",\n";
        ss << "      \"granted\": " << (granted ? "true" : "false") << "\n";
        ss << "    }";
    }
    ss << "\n  ]\n}";
    return ss.str();
}

void PermissionManager::setAppPermissionsFromJson(const std::string& packageName, const std::string& jsonStr) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& group : groups_) {
        std::string pattern = "\"" + group.key + "\":";
        size_t pos = jsonStr.find(pattern);
        if (pos != std::string::npos) {
            bool granted = (jsonStr.find("true", pos) != std::string::npos);
            for (const auto& perm : group.androidPermissions) {
                appPermissions_[packageName][perm] = granted;
            }
        }
    }
    saveConfigLocked();
}

} // namespace kudroid

// C Bridge APIs
extern "C" {

int kudroid_check_permission(const char* packageName, const char* permissionName) {
    if (!packageName || !permissionName) return kudroid::PERMISSION_GRANTED;
    return kudroid::PermissionManager::getInstance().checkPermission(packageName, permissionName);
}

void kudroid_set_group_permission(const char* packageName, const char* groupKey, int granted) {
    if (!packageName || !groupKey) return;
    kudroid::PermissionManager::getInstance().setGroupPermission(packageName, groupKey, granted != 0);
}

int kudroid_is_group_granted(const char* packageName, const char* groupKey) {
    if (!packageName || !groupKey) return 1;
    return kudroid::PermissionManager::getInstance().isGroupGranted(packageName, groupKey) ? 1 : 0;
}

void kudroid_grant_all_permissions(const char* packageName) {
    if (!packageName) return;
    kudroid::PermissionManager::getInstance().grantAll(packageName);
}

const char* kudroid_get_app_permissions_json(const char* packageName) {
    if (!packageName) return "{}";
    static std::string s_lastJson;
    s_lastJson = kudroid::PermissionManager::getInstance().getAppPermissionsJson(packageName);
    return s_lastJson.c_str();
}

void kudroid_set_app_permissions_json(const char* packageName, const char* jsonStr) {
    if (!packageName || !jsonStr) return;
    kudroid::PermissionManager::getInstance().setAppPermissionsFromJson(packageName, jsonStr);
}

}
