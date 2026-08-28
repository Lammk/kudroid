#include "kudroid/KuArtRuntime.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "dex/dex_file-inl.h"

#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexReflect.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/kuart/LibCore.h"

namespace {

using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexJniEnv;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexObject;
using kudroid::kuart::DexReflect;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

struct Runtime {
    DexClassLinker linker;
    std::unique_ptr<Interpreter> interpreter;
    std::unique_ptr<DexJniEnv> jni;
    std::unique_ptr<DexReflect> reflect;

    // The buffer of each DEX must live for the lifetime of the runtime: libdexfile only holds pointers
    // Go there, don't copy.
    std::vector<std::vector<uint8_t>> dex_buffers;

    // Descriptor for all classes in the app's DEX (excluding embedded frameworks) —
    // keep it so kuart_list_app_classes doesn't have to be rescanned.
    std::vector<std::string> app_classes;

    bool ready = false;
    std::string last_error;
};

Runtime* g_rt = nullptr;
std::mutex g_mtx;
std::string g_current_app_dir;
void (*g_log_cb)(const char*) = nullptr;
void* (*g_symbol_lookup)(const char*) = nullptr;
std::string g_last_error;

void Log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_log_cb != nullptr) {
        g_log_cb(buf);
    } else {
        std::fprintf(stderr, "[KuART] %s\n", buf);
    }
}

void SetError(const std::string& e) {
    g_last_error = e;
    if (g_rt != nullptr) g_rt->last_error = e;
    Log("ERROR: %s", e.c_str());
}

// "com.foo.Bar" or "com/foo/Bar" → "Lcom/foo/Bar;"
std::string ToDescriptor(const char* name) {
    if (name == nullptr || name[0] == '\0') return std::string();
    std::string s(name);
    for (char& c : s) {
        if (c == '.') c = '/';
    }
    if (s[0] == '[') return s;
    if (s.size() > 2 && s.front() == 'L' && s.back() == ';') return s;
    return "L" + s + ";";
}

bool ReadFile(const std::filesystem::path& path, std::vector<uint8_t>* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size <= 0) return false;
    f.seekg(0, std::ios::beg);
    out->resize(static_cast<size_t>(size));
    return static_cast<bool>(f.read(reinterpret_cast<char*>(out->data()), size));
}

// Class belongs to the framework/SDK, not the app's code. Used to filter the list
// candidate Activity — same prefix used by the old Avian line.
bool IsSystemOrSdkClass(const std::string& descriptor) {
    static const char* kPrefixes[] = {
        "Landroid/", "Landroidx/", "Ljava/", "Ljavax/", "Ljunit/", "Lkotlin/", "Lkotlinx/",
        "Lorg/", "Lcom/google/", "Lcom/android/", "Lcom/braze/", "Lcom/appboy/",
        "Lcom/facebook/", "Lcom/firebase/", "Lcom/crashlytics/", "Lcom/unity3d/",
        "Lcom/appsflyer/", "Lcom/adjust/", "Lcom/amplitude/", "Lcom/mixpanel/",
        "Lcom/microsoft/", "Lcom/playfab/", "Lcom/squareup/", "Lcom/bumptech/",
        "Ldagger/", "Lio/reactivex/", "Lretrofit2/", "Lokhttp3/", "Lokio/",
    };
    for (const char* p : kPrefixes) {
        if (descriptor.rfind(p, 0) == 0) return true;
    }
    return false;
}

// Call a static method of android/app/ActivityThread.
bool CallActivityThreadStatic(const char* name, const char* signature,
                              const DexValue* args, size_t num_args) {
    if (g_rt == nullptr || !g_rt->ready) return false;
    DexClass* at = g_rt->linker.FindClass("Landroid/app/ActivityThread;");
    if (at == nullptr) {
        SetError("android/app/ActivityThread not found in framework.dex");
        return false;
    }
    DexMethod* m = at->FindDirectMethod(name, signature);
    if (m == nullptr) {
        SetError(std::string("ActivityThread not found.") + name + signature);
        return false;
    }
    g_rt->interpreter->ClearPendingException();
    g_rt->interpreter->Execute(m, args, num_args);
    if (g_rt->interpreter->HasPendingException()) {
        DexObject* ex = g_rt->interpreter->pending_exception();
        SetError(std::string("exception in ActivityThread.") + name + ": " +
                 (ex != nullptr && ex->clazz != nullptr ? ex->clazz->PrettyName() : "?") + " (" +
                 g_rt->interpreter->last_error() + ")");
        g_rt->interpreter->ClearPendingException();
        return false;
    }
    return true;
}

}  // namespace

extern "C" void kuart_set_log_callback(void (*cb)(const char*)) { g_log_cb = cb; }

extern "C" void kuart_set_symbol_lookup(void* (*fn)(const char*)) {
    g_symbol_lookup = fn;
    std::lock_guard<std::mutex> lock(g_mtx);
    if (g_rt != nullptr && g_rt->jni != nullptr) g_rt->jni->set_symbol_lookup(fn);
}

extern "C" void kuart_set_load_library_callback(int (*cb)(const char*)) {
    kudroid::kuart::LibCoreSetLoadLibraryCallback(cb);
}

extern "C" void kuart_set_missing_class_log_path(const char* path) {
    kudroid::kuart::DexClassLinker::SetMissingClassLogPath(path);
}

extern "C" int kuart_init(const char* app_dir) {
    std::lock_guard<std::mutex> lock(g_mtx);
    const std::string requested_dir = (app_dir != nullptr) ? app_dir : "";
    if (g_rt != nullptr && g_rt->ready) {
        if (requested_dir.empty() || requested_dir == g_current_app_dir) {
            return 1;
        }
        delete g_rt;
        g_rt = nullptr;
        g_current_app_dir.clear();
    }

    auto rt = std::make_unique<Runtime>();

    // Record unresolvable framework classes next to the app's other logs, not in
    // the process CWD (which on iOS is not writable, and in host tests is
    // wherever the binary was started from).
    if (app_dir != nullptr && app_dir[0] != '\0') {
        const std::string log_path = (std::filesystem::path(app_dir) / "classes.log").string();
        kudroid::kuart::DexClassLinker::SetMissingClassLogPath(log_path.c_str());
    }

    // Embedded framework.dex must be loaded BEFORE the app's DEX: FindClass comes in order
    // Additionally, the class of the app referencing android/* will find the framework immediately.
    std::string error;
    if (!rt->linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size,
                               "framework.dex", &error)) {
        SetError("Failed to load embedded framework.dex: " + error);
        return 0;
    }
    Log("embedded framework.dex: %zu bytes", g_framework_dex_size);

    if (app_dir != nullptr && app_dir[0] != '\0') {
        std::error_code ec;
        std::vector<std::filesystem::path> dex_paths;
        for (const auto& entry : std::filesystem::directory_iterator(app_dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            const std::string name = entry.path().filename().string();
            if (name.size() > 4 && name.rfind("classes", 0) == 0 &&
                name.compare(name.size() - 4, 4, ".dex") == 0) {
                dex_paths.push_back(entry.path());
            }
        }
        // classes.dex before classes2.dex, classes3.dex... like real Android.
        std::sort(dex_paths.begin(), dex_paths.end());

        if (dex_paths.empty()) {
            Log("WARNING: no classes*.dex in %s", app_dir);
        }
        for (const auto& p : dex_paths) {
            std::vector<uint8_t> bytes;
            if (!ReadFile(p, &bytes)) {
                Log("WARNING: reading %s failed, skipped", p.string().c_str());
                continue;
            }
            rt->dex_buffers.push_back(std::move(bytes));
            const std::vector<uint8_t>& buf = rt->dex_buffers.back();
            if (!rt->linker.AddDexFile(buf.data(), buf.size(), p.string(), &error)) {
                Log("WARNING: %s failed to load (%s), skipped", p.filename().string().c_str(),
                    error.c_str());
                rt->dex_buffers.pop_back();
                continue;
            }
            Log("load %s (%zu bytes)", p.filename().string().c_str(), buf.size());
        }
    }

    rt->interpreter = std::make_unique<Interpreter>(&rt->linker);
    rt->jni = std::make_unique<DexJniEnv>(&rt->linker, rt->interpreter.get());
    rt->interpreter->set_jni_env(rt->jni.get());
    rt->reflect = std::make_unique<DexReflect>(&rt->linker, rt->interpreter.get(),
                                               rt->jni.get());
    if (g_symbol_lookup != nullptr) rt->jni->set_symbol_lookup(g_symbol_lookup);

    rt->ready = true;
    g_rt = rt.release();
    g_current_app_dir = requested_dir;

    Log("KuART ready: %zu DEX", g_rt->linker.NumDexFiles());
    return 1;
}

extern "C" void kuart_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_mtx);
    delete g_rt;
    g_rt = nullptr;
    g_current_app_dir.clear();
}

extern "C" int kuart_is_ready(void) { return (g_rt != nullptr && g_rt->ready) ? 1 : 0; }

extern "C" JavaVM* kuart_get_javavm(void) {
    if (g_rt == nullptr || g_rt->jni == nullptr) return nullptr;
    return g_rt->jni->vm();
}

extern "C" jint kuart_get_env(JavaVM* vm, void** env, jint version) {
    (void)version;
    if (g_rt == nullptr || g_rt->jni == nullptr || env == nullptr) return JNI_ERR;
    // A single JNIEnv for every thread: KuART doesn't have a real, universal Java thread yet
    // bytecode runs on the calling thread.
    (void)vm;
    *env = g_rt->jni->env();
    return JNI_OK;
}

extern "C" size_t kuart_num_dex_files(void) {
    return g_rt != nullptr ? g_rt->linker.NumDexFiles() : 0;
}

extern "C" size_t kuart_num_loaded_classes(void) {
    return g_rt != nullptr ? g_rt->linker.NumLoadedClasses() : 0;
}

extern "C" int kuart_class_extends_activity(const char* class_name) {
    if (g_rt == nullptr || !g_rt->ready || class_name == nullptr) return 0;
    DexClass* activity = g_rt->linker.FindClass("Landroid/app/Activity;");
    if (activity == nullptr) return 0;
    const std::string descriptor = ToDescriptor(class_name);
    DexClass* klass = g_rt->linker.FindClass(descriptor.c_str());
    if (klass == nullptr) return 0;
    return klass->IsSubClassOf(activity) ? 1 : 0;
}

extern "C" size_t kuart_list_app_classes(char** out, size_t max_out) {
    if (g_rt == nullptr || !g_rt->ready || out == nullptr || max_out == 0) return 0;

    // Lazy scanning: only goes through the class_def of every DEX once and then caches.
    if (g_rt->app_classes.empty()) {
        for (size_t i = 0; i < g_rt->linker.NumDexFiles(); ++i) {
            const art::DexFile* dex = g_rt->linker.DexFileAt(i);
            if (dex == nullptr) continue;
            // The first DEX is an embedded, bypass framework.
            if (i == 0) continue;
            for (size_t c = 0; c < dex->NumClassDefs(); ++c) {
                const char* descriptor = dex->GetClassDescriptor(dex->GetClassDef(c));
                if (descriptor == nullptr) continue;
                const std::string d(descriptor);
                if (IsSystemOrSdkClass(d)) continue;
                // Inner class is never an Activity launcher.
                if (d.find('$') != std::string::npos) continue;
                g_rt->app_classes.push_back(d);
            }
        }
        Log("scanned %zu classes of app", g_rt->app_classes.size());
    }

    size_t written = 0;
    for (const std::string& d : g_rt->app_classes) {
        if (written >= max_out) break;
        // Returns the form "com/foo/Bar" (omit the L and ;) to match the old API.
        const std::string name = (d.size() > 2 && d.front() == 'L' && d.back() == ';')
                                     ? d.substr(1, d.size() - 2)
                                     : d;
        char* copy = static_cast<char*>(std::malloc(name.size() + 1));
        if (copy == nullptr) break;
        std::memcpy(copy, name.c_str(), name.size() + 1);
        out[written++] = copy;
    }
    return written;
}

extern "C" void kuart_free_class_list(char** list, size_t count) {
    if (list == nullptr) return;
    for (size_t i = 0; i < count; ++i) std::free(list[i]);
}

extern "C" int kuart_launch_app(const char* package_name, const char* component_factory,
                                const char* app_class, const char* activity_name,
                                const char* const* extra_candidates, int extra_count) {
    if (g_rt == nullptr || !g_rt->ready) {
        SetError("kuart_launch_app: runtime not init yet");
        return 0;
    }
    if (activity_name == nullptr || activity_name[0] == '\0') {
        SetError("kuart_launch_app: empty activity name");
        return 0;
    }

    DexClass* string_array = g_rt->linker.FindClass("[Ljava/lang/String;");
    if (string_array == nullptr) {
        SetError("failed to create [Ljava/lang/String;");
        return 0;
    }

    // Layout must match ActivityThread.main: package, factory, app class, then the
    // activity candidates. Absent entries are the empty string rather than null so
    // the Java side never has to null-check before calling isEmpty().
    const int kHeader = 3;
    const int total = kHeader + 1 + (extra_candidates != nullptr ? extra_count : 0);
    auto* args_array = g_rt->linker.AllocArray(string_array, total);
    if (args_array == nullptr) {
        SetError("args array could not be allocated");
        return 0;
    }
    const auto put = [&](int index, const char* value) {
        args_array->Set<DexObject*>(index,
                                    g_rt->linker.NewString(value != nullptr ? value : ""));
    };
    put(0, package_name);
    put(1, component_factory);
    put(2, app_class);
    put(kHeader, activity_name);
    for (int i = 0; i < extra_count && extra_candidates != nullptr; ++i) {
        if (extra_candidates[i] == nullptr || extra_candidates[i][0] == '\0') continue;
        put(kHeader + 1 + i, extra_candidates[i]);
    }

    Log("call ActivityThread.main(pkg=\"%s\" factory=\"%s\" app=\"%s\" activity=\"%s\") + %d backup candidate",
        package_name != nullptr ? package_name : "",
        component_factory != nullptr ? component_factory : "",
        app_class != nullptr ? app_class : "", activity_name,
        total - kHeader - 1);
    const DexValue arg = DexValue::Ref(args_array);
    return CallActivityThreadStatic("main", "([Ljava/lang/String;)V", &arg, 1) ? 1 : 0;
}

extern "C" int kuart_launch_activity(const char* activity_name,
                                     const char* const* extra_candidates, int extra_count) {
    return kuart_launch_app(nullptr, nullptr, nullptr, activity_name, extra_candidates,
                            extra_count);
}

extern "C" void kuart_send_lifecycle_event(int event_type) {
    const DexValue args[2] = {DexValue::Int(event_type), DexValue::Ref(nullptr)};
    CallActivityThreadStatic("postLifecycleEvent", "(ILjava/lang/String;)V", args, 2);
}

extern "C" void kuart_post_touch_event(int action, float x, float y) {
    const DexValue args[3] = {DexValue::Int(action), DexValue::Float(x), DexValue::Float(y)};
    CallActivityThreadStatic("postTouchEvent", "(IFF)V", args, 3);
}

extern "C" int kuart_take_pending_exception(const char** out) {
    // Per-thread: the exception itself is per-thread, so the buffer backing the
    // returned string has to be too, otherwise two threads reporting at once would
    // read each other's text.
    static thread_local std::string s_description;
    if (out != nullptr) *out = "";
    if (g_rt == nullptr || !g_rt->ready || g_rt->interpreter == nullptr) return 0;
    if (!g_rt->interpreter->HasPendingException()) return 0;

    s_description = g_rt->interpreter->DescribePendingException();
    g_rt->interpreter->ClearPendingException();
    // The JNI env caches its own copy of the pointer, so clearing only the
    // interpreter would leave ExceptionCheck() returning true forever.
    if (g_rt->jni != nullptr) g_rt->jni->ClearException();
    if (out != nullptr) *out = s_description.c_str();
    return 1;
}

extern "C" const char* kuart_last_error(void) {
    if (g_rt != nullptr && !g_rt->last_error.empty()) return g_rt->last_error.c_str();
    return g_last_error.c_str();
}

// ── standard JNI entry point that the guest's native code calls ────────────────────────────
// Game engines (Unity, MCPE...) call JNI_GetCreatedJavaVMs directly from static
// initializer to get the VM. BionicShim maps these symbols to the shim table.

extern "C" jint JNI_GetCreatedJavaVMs(JavaVM** vm_buf, jsize buf_len, jsize* num_vms) {
    JavaVM* vm = kuart_get_javavm();
    if (num_vms != nullptr) *num_vms = vm != nullptr ? 1 : 0;
    if (vm_buf != nullptr && buf_len > 0 && vm != nullptr) vm_buf[0] = vm;
    return JNI_OK;
}

extern "C" jint JNI_CreateJavaVM(JavaVM** p_vm, void** p_env, void* vm_args) {
    (void)vm_args;
    // KuART has only ONE VM, created at kuart_init. Guest calls CreateJavaVM and returns
    // Existing VM instead of building a new one — building two VMs will split the linker class in half.
    if (!kuart_is_ready() && !kuart_init("")) return JNI_ERR;
    if (p_vm != nullptr) *p_vm = kuart_get_javavm();
    if (p_env != nullptr) kuart_get_env(kuart_get_javavm(), p_env, JNI_VERSION_1_6);
    return JNI_OK;
}

// The old name that BionicShim's shim table references.
extern "C" void* kudroid_jni_get_javavm(void) { return kuart_get_javavm(); }

// The old name that the Swift shell (kudroid_bridge.h) references.
extern "C" void kudroid_send_lifecycle_event(int event_type) {
    kuart_send_lifecycle_event(event_type);
}
