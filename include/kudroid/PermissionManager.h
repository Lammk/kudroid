#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace kudroid {

enum PermissionStatus {
    PERMISSION_GRANTED = 0,
    PERMISSION_DENIED = -1
};

struct PermissionGroup {
    std::string key;          // e.g. "storage"
    std::string displayName;  // e.g. "Storage Access"
    std::string description;  // e.g. "Read and write device storage"
    std::vector<std::string> androidPermissions;
    bool defaultGranted;
};

class PermissionManager {
public:
    static PermissionManager& getInstance();

    void init(const std::string& documentsDir);

    // Check permissions
    int checkPermission(const std::string& packageName, const std::string& permissionName);
    
    // Grant/Revoke permissions by group (e.g. "storage", "internet", "camera")
    void setGroupPermission(const std::string& packageName, const std::string& groupKey, bool granted);
    bool isGroupGranted(const std::string& packageName, const std::string& groupKey);

    // Grant/Revoke specific permissions
    void setPermission(const std::string& packageName, const std::string& permissionName, bool granted);

    // Grant full permissions to the app
    void grantAll(const std::string& packageName);

    // Get the list of permission groups
    const std::vector<PermissionGroup>& getPermissionGroups() const;

    // JSON export cho Swift Shell
    std::string getAppPermissionsJson(const std::string& packageName);
    void setAppPermissionsFromJson(const std::string& packageName, const std::string& jsonStr);

private:
    PermissionManager();
    ~PermissionManager() = default;

    void loadConfigLocked();
    void saveConfigLocked();

    std::string configPath_;
    std::vector<PermissionGroup> groups_;
    // map: packageName -> map<permissionName, bool>
    std::unordered_map<std::string, std::unordered_map<std::string, bool>> appPermissions_;
    mutable std::mutex mutex_;
    bool initialized_{false};
};

} // namespace kudroid

// C Bridge APIs
#ifdef __cplusplus
extern "C" {
#endif

int kudroid_check_permission(const char* packageName, const char* permissionName);
void kudroid_set_group_permission(const char* packageName, const char* groupKey, int granted);
int kudroid_is_group_granted(const char* packageName, const char* groupKey);
void kudroid_grant_all_permissions(const char* packageName);
const char* kudroid_get_app_permissions_json(const char* packageName);
void kudroid_set_app_permissions_json(const char* packageName, const char* jsonStr);

#ifdef __cplusplus
}
#endif
