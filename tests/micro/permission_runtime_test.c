typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned long size_t;
typedef long ssize_t;
#define NULL ((void*)0)

// Android Log API
extern int __android_log_print(int priority, const char* tag, const char* fmt, ...);
#define LOG_TAG "PermissionTest"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

// Permission C Bridge APIs
extern int kudroid_check_permission(const char* packageName, const char* permissionName);
extern void kudroid_set_group_permission(const char* packageName, const char* groupKey, int granted);
extern int kudroid_is_group_granted(const char* packageName, const char* groupKey);
extern void kudroid_grant_all_permissions(const char* packageName);
extern const char* kudroid_get_app_permissions_json(const char* packageName);
extern void kudroid_set_app_permissions_json(const char* packageName, const char* jsonStr);

// C runtime
extern size_t strlen(const char* s);
extern char* strstr(const char* haystack, const char* needle);
extern int strcmp(const char* s1, const char* s2);

static int g_passCount = 0;
static int g_failCount = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (cond) { \
            LOGI("  ✔ [PASS] %s", msg); \
            g_passCount++; \
        } else { \
            LOGE("  ❌ [FAIL] %s (Line %d)", msg, __LINE__); \
            g_failCount++; \
        } \
    } while(0)

int kudroid_test_main(void) {
    g_passCount = 0;
    g_failCount = 0;

    LOGI("=================================================");
    LOGI("🛡️ [KUDROID ANDROID RUNTIME PERMISSIONS TEST]");
    LOGI("=================================================");

    const char* zarchiverPkg = "ru.zdevs.zarchiver";
    const char* minecraftPkg = "com.mojang.minecraftpe";

    // ──────────────────────────────────────────────
    // 1. DEFAULT PERMISSIONS TEST
    // ──────────────────────────────────────────────
    LOGI("🧪 [TEST 1] Default Permissions (Storage & Internet)...");
    int readStorage = kudroid_check_permission(zarchiverPkg, "android.permission.READ_EXTERNAL_STORAGE");
    int writeStorage = kudroid_check_permission(zarchiverPkg, "android.permission.WRITE_EXTERNAL_STORAGE");
    int manageStorage = kudroid_check_permission(zarchiverPkg, "android.permission.MANAGE_EXTERNAL_STORAGE");
    int internet = kudroid_check_permission(zarchiverPkg, "android.permission.INTERNET");

    TEST_ASSERT(readStorage == 0, "Default READ_EXTERNAL_STORAGE granted (0)");
    TEST_ASSERT(writeStorage == 0, "Default WRITE_EXTERNAL_STORAGE granted (0)");
    TEST_ASSERT(manageStorage == 0, "Default MANAGE_EXTERNAL_STORAGE granted (0)");
    TEST_ASSERT(internet == 0, "Default INTERNET granted (0)");

    // ──────────────────────────────────────────────
    // 2. TOGGLE & REVOKE PERMISSIONS TEST
    // ──────────────────────────────────────────────
    LOGI("🧪 [TEST 2] Toggle Group Permission (Revoke Storage)...");
    kudroid_set_group_permission(zarchiverPkg, "storage", 0);
    
    int isStorageGranted = kudroid_is_group_granted(zarchiverPkg, "storage");
    int revokedRead = kudroid_check_permission(zarchiverPkg, "android.permission.READ_EXTERNAL_STORAGE");
    int revokedWrite = kudroid_check_permission(zarchiverPkg, "android.permission.WRITE_EXTERNAL_STORAGE");
    
    TEST_ASSERT(isStorageGranted == 0, "kudroid_is_group_granted returns 0 after revoke");
    TEST_ASSERT(revokedRead == -1, "READ_EXTERNAL_STORAGE returns -1 (PERMISSION_DENIED)");
    TEST_ASSERT(revokedWrite == -1, "WRITE_EXTERNAL_STORAGE returns -1 (PERMISSION_DENIED)");

    // ──────────────────────────────────────────────
    // 3. RE-GRANT STORAGE PERMISSION TEST
    // ──────────────────────────────────────────────
    LOGI("🧪 [TEST 3] Re-grant Storage Permission...");
    kudroid_set_group_permission(zarchiverPkg, "storage", 1);
    
    int reStorageGranted = kudroid_is_group_granted(zarchiverPkg, "storage");
    int reRead = kudroid_check_permission(zarchiverPkg, "android.permission.READ_EXTERNAL_STORAGE");
    int reWrite = kudroid_check_permission(zarchiverPkg, "android.permission.WRITE_EXTERNAL_STORAGE");
    
    TEST_ASSERT(reStorageGranted == 1, "kudroid_is_group_granted returns 1 after re-grant");
    TEST_ASSERT(reRead == 0, "READ_EXTERNAL_STORAGE restored to 0 (PERMISSION_GRANTED)");
    TEST_ASSERT(reWrite == 0, "WRITE_EXTERNAL_STORAGE restored to 0 (PERMISSION_GRANTED)");

    // ──────────────────────────────────────────────
    // 4. GRANT ALL PERMISSIONS TEST
    // ──────────────────────────────────────────────
    LOGI("🧪 [TEST 4] Grant All Permissions for Minecraft...");
    kudroid_grant_all_permissions(minecraftPkg);
    
    int mcStorage = kudroid_check_permission(minecraftPkg, "android.permission.WRITE_EXTERNAL_STORAGE");
    int mcNet = kudroid_check_permission(minecraftPkg, "android.permission.INTERNET");
    int mcMic = kudroid_check_permission(minecraftPkg, "android.permission.RECORD_AUDIO");
    int mcBt = kudroid_check_permission(minecraftPkg, "android.permission.BLUETOOTH_CONNECT");
    
    TEST_ASSERT(mcStorage == 0, "Minecraft Storage granted (0)");
    TEST_ASSERT(mcNet == 0, "Minecraft Internet granted (0)");
    TEST_ASSERT(mcMic == 0, "Minecraft Mic granted (0)");
    TEST_ASSERT(mcBt == 0, "Minecraft Bluetooth Gamepad granted (0)");

    // ──────────────────────────────────────────────
    // 5. JSON CONFIG EXPORT & SYNC TEST
    // ──────────────────────────────────────────────
    LOGI("🧪 [TEST 5] JSON Config Generation & Parsing...");
    const char* json = kudroid_get_app_permissions_json(zarchiverPkg);
    TEST_ASSERT(json != NULL && strlen(json) > 20, "JSON export returns non-empty string");
    TEST_ASSERT(strstr(json, "\"storage\"") != NULL, "JSON contains 'storage' group");
    TEST_ASSERT(strstr(json, "\"network\"") != NULL, "JSON contains 'network' group");
    TEST_ASSERT(strstr(json, "\"camera\"") != NULL, "JSON contains 'camera' group");

    LOGI("=================================================");
    LOGI("📊 PERMISSION AUDIT REPORT: %d PASSED, %d FAILED (TOTAL %d)",
         g_passCount, g_failCount, g_passCount + g_failCount);
    LOGI("=================================================");

    return (g_failCount == 0) ? 0 : 1;
}
