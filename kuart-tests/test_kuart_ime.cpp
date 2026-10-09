#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kudroid_bridge.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexJniEnv;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexObject;
using kudroid::kuart::DexString;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

int g_checks = 0;
int g_failures = 0;
int g_showCalls = 0;
int g_hideCalls = 0;
int g_lastShowFlags = 0;
DexClassLinker* g_linker = nullptr;
Interpreter* g_interp = nullptr;

void ShowCallback(int flags) {
    ++g_showCalls;
    g_lastShowFlags = flags;
}
void HideCallback() { ++g_hideCalls; }

void Check(bool condition, const char* message) {
    ++g_checks;
    std::printf("%s %s\n", condition ? "  OK  " : "  FAIL", message);
    if (!condition) ++g_failures;
}

DexValue Str(const char* value) {
    return DexValue::Ref(reinterpret_cast<DexObject*>(g_linker->NewString(value)));
}

const char* Utf8(const DexValue& value) {
    auto* string = reinterpret_cast<DexString*>(value.l);
    return string != nullptr && string->utf8 != nullptr ? string->utf8 : "(null)";
}

DexObject* NewObject(const char* descriptor, const char* ctorSignature,
                     const std::vector<DexValue>& args) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub || !g_interp->EnsureInitialized(klass)) return nullptr;
    DexObject* object = g_linker->AllocObject(klass);
    DexMethod* ctor = klass->FindDirectMethod("<init>", ctorSignature);
    if (object == nullptr || ctor == nullptr) return nullptr;
    std::vector<DexValue> full{DexValue::Ref(object)};
    full.insert(full.end(), args.begin(), args.end());
    g_interp->Execute(ctor, full.data(), full.size());
    if (g_interp->HasPendingException()) {
        g_interp->ClearPendingException();
        return nullptr;
    }
    return object;
}

bool CallStatic(const char* descriptor, const char* name, const char* signature,
                const std::vector<DexValue>& args, DexValue* result) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub || !g_interp->EnsureInitialized(klass)) return false;
    DexMethod* method = klass->FindDirectMethod(name, signature);
    if (method == nullptr) method = klass->FindVirtualMethod(name, signature);
    if (method == nullptr) return false;
    g_interp->ClearPendingException();
    const DexValue value = g_interp->Execute(method, args.data(), args.size());
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s.%s threw: %s\n", descriptor, name, g_interp->last_error().c_str());
        ++g_checks;
        ++g_failures;
        g_interp->ClearPendingException();
        return false;
    }
    if (result != nullptr) *result = value;
    return true;
}

bool CallVirtual(DexObject* receiver, const char* name, const char* signature,
                 const std::vector<DexValue>& args, DexValue* result) {
    if (receiver == nullptr || receiver->clazz == nullptr) return false;
    DexMethod* method = receiver->clazz->FindVirtualMethod(name, signature);
    if (method == nullptr) method = receiver->clazz->FindDirectMethod(name, signature);
    if (method == nullptr) return false;
    std::vector<DexValue> full{DexValue::Ref(receiver)};
    full.insert(full.end(), args.begin(), args.end());
    g_interp->ClearPendingException();
    const DexValue value = g_interp->Execute(method, full.data(), full.size());
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s threw: %s\n", name, g_interp->last_error().c_str());
        ++g_checks;
        ++g_failures;
        g_interp->ClearPendingException();
        return false;
    }
    if (result != nullptr) *result = value;
    return true;
}

}  // namespace

int main() {
    std::printf("=== KuART InputMethodManager and UIKit bridge ===\n");
    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex", &error)) {
        std::printf("  FAIL AddDexFile(framework.dex): %s\n", error.c_str());
        return 1;
    }
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    interp.set_instruction_limit(2000ull * 1000ull * 1000ull);
    g_linker = &linker;
    g_interp = &interp;

    kudroid_set_soft_input_callbacks(ShowCallback, HideCallback);
    DexValue managerValue;
    const bool managerOk = CallStatic("Landroid/view/inputmethod/InputMethodManager;", "getInstance",
            "()Landroid/view/inputmethod/InputMethodManager;", {}, &managerValue);
    DexObject* manager = managerOk ? managerValue.l : nullptr;
    DexObject* context = NewObject("Landroid/app/ApplicationContext;", "(Ljava/lang/String;)V",
                                   {Str("com.example.ime")});
    DexObject* view = NewObject("Landroid/view/View;", "(Landroid/content/Context;)V",
                                {DexValue::Ref(context)});
    DexObject* connection = NewObject("Landroid/view/inputmethod/BaseInputConnection;",
            "(Landroid/view/View;Z)V", {DexValue::Ref(view), DexValue::Int(1)});
    Check(manager != nullptr && view != nullptr && connection != nullptr,
          "manager, view, and test InputConnection are available");

    DexValue shown;
    const bool showOk = CallVirtual(manager, "showSoftInput", "(Landroid/view/View;I)Z",
            {DexValue::Ref(view), DexValue::Int(0x12)}, &shown);
    Check(showOk && shown.i == 1 && g_showCalls == 1 && g_lastShowFlags == 0x12,
          "show request reaches the registered host callback with flags");

    CallStatic("Landroid/view/inputmethod/InputMethodManager;", "setCurrentInputConnection",
            "(Landroid/view/View;Landroid/view/inputmethod/InputConnection;)V",
            {DexValue::Ref(view), DexValue::Ref(connection)}, nullptr);
    DexValue activeView, active, accepting, current;
    CallVirtual(manager, "isActive", "(Landroid/view/View;)Z", {DexValue::Ref(view)}, &activeView);
    CallVirtual(manager, "isActive", "()Z", {}, &active);
    CallVirtual(manager, "isAcceptingText", "()Z", {}, &accepting);
    CallStatic("Landroid/view/inputmethod/InputMethodManager;", "getCurrentInputConnection",
            "()Landroid/view/inputmethod/InputConnection;", {}, &current);
    Check(activeView.i == 1 && active.i == 1 && accepting.i == 1 && current.l == connection,
          "served view and active InputConnection state are reported accurately");

    kudroid_set_soft_input_visible(1);
    DexValue visible;
    CallVirtual(manager, "isSoftInputVisible", "()Z", {}, &visible);
    Check(visible.i == 1, "host visibility is reflected by InputMethodManager");
    DexValue hidden;
    const bool hideOk = CallVirtual(manager, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z",
            {DexValue::Ref(nullptr), DexValue::Int(0)}, &hidden);
    Check(hideOk && hidden.i == 1 && g_hideCalls == 1,
          "hide request reaches the registered host callback");

    kudroid_set_soft_input_visible(1);
    CallStatic("Landroid/view/inputmethod/InputMethodManager;", "resetForRelaunch", "()V", {}, nullptr);
    CallVirtual(manager, "isActive", "()Z", {}, &active);
    CallVirtual(manager, "isAcceptingText", "()Z", {}, &accepting);
    CallStatic("Landroid/view/inputmethod/InputMethodManager;", "getCurrentInputConnection",
            "()Landroid/view/inputmethod/InputConnection;", {}, &current);
    Check(g_hideCalls == 2 && active.i == 0 && accepting.i == 0 && current.l == nullptr,
          "relaunch hides the keyboard and clears served view and connection state");

    kudroid_set_soft_input_callbacks(nullptr, nullptr);
    CallVirtual(manager, "showSoftInput", "(Landroid/view/View;I)Z",
            {DexValue::Ref(view), DexValue::Int(0)}, &shown);
    CallVirtual(manager, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z",
            {DexValue::Ref(nullptr), DexValue::Int(0)}, &hidden);
    Check(shown.i == 0 && hidden.i == 0,
          "show and hide report false when the host has no registered callback");
    CallStatic("Landroid/view/inputmethod/InputMethodManager;", "resetForRelaunch", "()V", {}, nullptr);

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
