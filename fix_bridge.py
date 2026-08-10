with open("src/kudroid_bridge.cpp", "r") as f:
    content = f.read()

target = """extern "C" char* kudroid_test_jvm(void) {
    g_jvm_test_log.clear();
    g_jvm_test_log += "[kudroid_core] ===== JVM Integration Test =====\\n\\n";

    // Add timestamp
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    g_jvm_test_log += "[kudroid_core] Timestamp: " + oss.str() + "\\n\\n";

    g_jvm_test_log += "[kudroid_core] JIT: Enabled\\n\\n";

    g_jvm_test_log += "[kudroid_core] Phase: init JVM via JNI Bridge\\n\\n";
    // Setup log callback to capture JNI detailed logs
    kudroid_jni_set_log_callback([](const char* msg) {
        g_jvm_test_log += std::string(msg) + "\\n\\n";
    });

    kudroid_jni_init_jvm("", "");"""

replacement = """extern "C" char* kudroid_test_jvm(const char* rt_jar_path) {
    g_jvm_test_log.clear();
    g_jvm_test_log += "[kudroid_core] ===== JVM Integration Test =====\\n\\n";

    // Add timestamp
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    g_jvm_test_log += "[kudroid_core] Timestamp: " + oss.str() + "\\n\\n";

    g_jvm_test_log += "[kudroid_core] JIT: Enabled\\n\\n";

    g_jvm_test_log += "[kudroid_core] Phase: init JVM via JNI Bridge\\n\\n";
    // Setup log callback to capture JNI detailed logs
    kudroid_jni_set_log_callback([](const char* msg) {
        g_jvm_test_log += std::string(msg) + "\\n\\n";
    });

    kudroid_jni_init_jvm(rt_jar_path ? rt_jar_path : "", "");"""

if target in content:
    with open("src/kudroid_bridge.cpp", "w") as f:
        f.write(content.replace(target, replacement))
    print("Success")
else:
    print("Target not found")
