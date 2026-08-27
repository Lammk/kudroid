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

    // Buffer của mỗi DEX phải sống suốt đời runtime: libdexfile chỉ giữ con trỏ
    // vào đó, không copy.
    std::vector<std::vector<uint8_t>> dex_buffers;

    // Descriptor mọi class có trong DEX của app (không gồm framework nhúng) —
    // giữ lại để kuart_list_app_classes không phải quét lại.
    std::vector<std::string> app_classes;

    bool ready = false;
    std::string last_error;
};

Runtime* g_rt = nullptr;
std::mutex g_mtx;
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

// "com.foo.Bar" hoặc "com/foo/Bar" → "Lcom/foo/Bar;"
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

// Class thuộc framework/SDK, không phải code của app. Dùng để lọc danh sách
// candidate Activity — cùng bộ tiền tố mà đường Avian cũ dùng.
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

// Gọi một static method của android/app/ActivityThread.
bool CallActivityThreadStatic(const char* name, const char* signature,
                              const DexValue* args, size_t num_args) {
    if (g_rt == nullptr || !g_rt->ready) return false;
    DexClass* at = g_rt->linker.FindClass("Landroid/app/ActivityThread;");
    if (at == nullptr) {
        SetError("không tìm thấy android/app/ActivityThread trong framework.dex");
        return false;
    }
    DexMethod* m = at->FindDirectMethod(name, signature);
    if (m == nullptr) {
        SetError(std::string("không tìm thấy ActivityThread.") + name + signature);
        return false;
    }
    g_rt->interpreter->ClearPendingException();
    g_rt->interpreter->Execute(m, args, num_args);
    if (g_rt->interpreter->HasPendingException()) {
        DexObject* ex = g_rt->interpreter->pending_exception();
        SetError(std::string("exception trong ActivityThread.") + name + ": " +
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

extern "C" int kuart_init(const char* app_dir) {
    std::lock_guard<std::mutex> lock(g_mtx);
    if (g_rt != nullptr && g_rt->ready) return 1;

    auto rt = std::make_unique<Runtime>();

    // framework.dex nhúng phải nạp TRƯỚC DEX của app: FindClass đi theo thứ tự
    // thêm, nên class của app tham chiếu android/* sẽ tìm thấy framework ngay.
    std::string error;
    if (!rt->linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size,
                               "framework.dex", &error)) {
        SetError("nạp framework.dex nhúng thất bại: " + error);
        return 0;
    }
    Log("framework.dex nhúng: %zu bytes", g_framework_dex_size);

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
        // classes.dex trước classes2.dex, classes3.dex... như Android thật.
        std::sort(dex_paths.begin(), dex_paths.end());

        if (dex_paths.empty()) {
            Log("WARNING: không có classes*.dex nào trong %s", app_dir);
        }
        for (const auto& p : dex_paths) {
            std::vector<uint8_t> bytes;
            if (!ReadFile(p, &bytes)) {
                Log("WARNING: đọc %s thất bại, bỏ qua", p.string().c_str());
                continue;
            }
            rt->dex_buffers.push_back(std::move(bytes));
            const std::vector<uint8_t>& buf = rt->dex_buffers.back();
            if (!rt->linker.AddDexFile(buf.data(), buf.size(), p.string(), &error)) {
                Log("WARNING: %s không nạp được (%s), bỏ qua", p.filename().string().c_str(),
                    error.c_str());
                rt->dex_buffers.pop_back();
                continue;
            }
            Log("nạp %s (%zu bytes)", p.filename().string().c_str(), buf.size());
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

    Log("KuART sẵn sàng: %zu DEX", g_rt->linker.NumDexFiles());
    return 1;
}

extern "C" void kuart_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_mtx);
    delete g_rt;
    g_rt = nullptr;
}

extern "C" int kuart_is_ready(void) { return (g_rt != nullptr && g_rt->ready) ? 1 : 0; }

extern "C" JavaVM* kuart_get_javavm(void) {
    if (g_rt == nullptr || g_rt->jni == nullptr) return nullptr;
    return g_rt->jni->vm();
}

extern "C" jint kuart_get_env(JavaVM* vm, void** env, jint version) {
    (void)version;
    if (g_rt == nullptr || g_rt->jni == nullptr || env == nullptr) return JNI_ERR;
    // Một JNIEnv duy nhất cho mọi thread: KuART chưa có thread Java thật, mọi
    // bytecode chạy trên thread gọi vào.
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

    // Quét lười: chỉ đi qua class_def của mọi DEX một lần rồi cache.
    if (g_rt->app_classes.empty()) {
        for (size_t i = 0; i < g_rt->linker.NumDexFiles(); ++i) {
            const art::DexFile* dex = g_rt->linker.DexFileAt(i);
            if (dex == nullptr) continue;
            // DEX đầu tiên là framework nhúng, bỏ qua.
            if (i == 0) continue;
            for (size_t c = 0; c < dex->NumClassDefs(); ++c) {
                const char* descriptor = dex->GetClassDescriptor(dex->GetClassDef(c));
                if (descriptor == nullptr) continue;
                const std::string d(descriptor);
                if (IsSystemOrSdkClass(d)) continue;
                // Inner class không bao giờ là launcher Activity.
                if (d.find('$') != std::string::npos) continue;
                g_rt->app_classes.push_back(d);
            }
        }
        Log("quét được %zu class của app", g_rt->app_classes.size());
    }

    size_t written = 0;
    for (const std::string& d : g_rt->app_classes) {
        if (written >= max_out) break;
        // Trả dạng "com/foo/Bar" (bỏ L và ;) cho khớp API cũ.
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

extern "C" int kuart_launch_activity(const char* activity_name,
                                     const char* const* extra_candidates, int extra_count) {
    if (g_rt == nullptr || !g_rt->ready) {
        SetError("kuart_launch_activity: runtime chưa init");
        return 0;
    }
    if (activity_name == nullptr || activity_name[0] == '\0') {
        SetError("kuart_launch_activity: tên activity rỗng");
        return 0;
    }

    DexClass* string_array = g_rt->linker.FindClass("[Ljava/lang/String;");
    if (string_array == nullptr) {
        SetError("không tạo được [Ljava/lang/String;");
        return 0;
    }

    const int total = 1 + (extra_candidates != nullptr ? extra_count : 0);
    auto* args_array = g_rt->linker.AllocArray(string_array, total);
    if (args_array == nullptr) {
        SetError("không cấp phát được mảng args");
        return 0;
    }
    args_array->Set<DexObject*>(0, g_rt->linker.NewString(activity_name));
    for (int i = 0; i < extra_count && extra_candidates != nullptr; ++i) {
        if (extra_candidates[i] == nullptr || extra_candidates[i][0] == '\0') continue;
        args_array->Set<DexObject*>(1 + i, g_rt->linker.NewString(extra_candidates[i]));
    }

    Log("gọi ActivityThread.main(\"%s\") + %d candidate dự phòng", activity_name,
        total - 1);
    const DexValue arg = DexValue::Ref(args_array);
    return CallActivityThreadStatic("main", "([Ljava/lang/String;)V", &arg, 1) ? 1 : 0;
}

extern "C" void kuart_send_lifecycle_event(int event_type) {
    const DexValue args[2] = {DexValue::Int(event_type), DexValue::Ref(nullptr)};
    CallActivityThreadStatic("postLifecycleEvent", "(ILjava/lang/String;)V", args, 2);
}

extern "C" void kuart_post_touch_event(int action, float x, float y) {
    const DexValue args[3] = {DexValue::Int(action), DexValue::Float(x), DexValue::Float(y)};
    CallActivityThreadStatic("postTouchEvent", "(IFF)V", args, 3);
}

extern "C" const char* kuart_last_error(void) {
    if (g_rt != nullptr && !g_rt->last_error.empty()) return g_rt->last_error.c_str();
    return g_last_error.c_str();
}

// ── điểm vào JNI chuẩn mà mã native của guest gọi ────────────────────────────
// Engine game (Unity, MCPE...) gọi thẳng JNI_GetCreatedJavaVMs từ static
// initializer để lấy VM. BionicShim map các symbol này vào bảng shim.

extern "C" jint JNI_GetCreatedJavaVMs(JavaVM** vm_buf, jsize buf_len, jsize* num_vms) {
    JavaVM* vm = kuart_get_javavm();
    if (num_vms != nullptr) *num_vms = vm != nullptr ? 1 : 0;
    if (vm_buf != nullptr && buf_len > 0 && vm != nullptr) vm_buf[0] = vm;
    return JNI_OK;
}

extern "C" jint JNI_CreateJavaVM(JavaVM** p_vm, void** p_env, void* vm_args) {
    (void)vm_args;
    // KuART chỉ có MỘT VM, tạo lúc kuart_init. Guest gọi CreateJavaVM thì trả
    // VM sẵn có thay vì dựng cái mới — dựng hai VM sẽ chia đôi class linker.
    if (!kuart_is_ready() && !kuart_init("")) return JNI_ERR;
    if (p_vm != nullptr) *p_vm = kuart_get_javavm();
    if (p_env != nullptr) kuart_get_env(kuart_get_javavm(), p_env, JNI_VERSION_1_6);
    return JNI_OK;
}

// Tên cũ mà bảng shim của BionicShim tham chiếu.
extern "C" void* kudroid_jni_get_javavm(void) { return kuart_get_javavm(); }

// Tên cũ mà vỏ Swift (kudroid_bridge.h) tham chiếu.
extern "C" void kudroid_send_lifecycle_event(int event_type) {
    kuart_send_lifecycle_event(event_type);
}
