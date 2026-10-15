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

    // EditText end to end: focus, keyboard text, Return, cursor, and routing.
    kudroid_set_soft_input_callbacks(ShowCallback, HideCallback);
    g_showCalls = 0;
    g_hideCalls = 0;
    auto textOf = [](DexObject* editText) -> std::string {
        DexValue editable, text;
        if (!CallVirtual(editText, "getText", "()Landroid/text/Editable;", {}, &editable) ||
            editable.l == nullptr) {
            return "(null)";
        }
        CallVirtual(editable.l, "toString", "()Ljava/lang/String;", {}, &text);
        return Utf8(text);
    };
    auto intOf = [](DexObject* object, const char* name) -> int {
        DexValue v;
        CallVirtual(object, name, "()I", {}, &v);
        return v.i;
    };
    auto type = [](const char* text) {
        CallStatic("Landroid/view/inputmethod/InputMethodManager;", "deliverText",
                   "(Ljava/lang/String;)V", {Str(text)}, nullptr);
    };
    DexObject* edit = NewObject("Landroid/widget/EditText;", "(Landroid/content/Context;)V",
                                {DexValue::Ref(context)});
    DexObject* other = NewObject("Landroid/widget/EditText;", "(Landroid/content/Context;)V",
                                 {DexValue::Ref(context)});
    Check(edit != nullptr && other != nullptr, "EditText instances are available");

    DexValue textEditor;
    CallVirtual(edit, "onCheckIsTextEditor", "()Z", {}, &textEditor);
    Check(textEditor.i == 1, "an EditText is a text editor");

    DexValue touched;
    DexValue down, up;
    CallStatic("Landroid/view/MotionEvent;", "obtain", "(IFFJ)Landroid/view/MotionEvent;",
               {DexValue::Int(0), DexValue::Float(5), DexValue::Float(5), DexValue::Long(1)}, &down);
    CallStatic("Landroid/view/MotionEvent;", "obtain", "(IFFJ)Landroid/view/MotionEvent;",
               {DexValue::Int(1), DexValue::Float(5), DexValue::Float(5), DexValue::Long(2)}, &up);
    CallVirtual(edit, "onTouchEvent", "(Landroid/view/MotionEvent;)Z", {down}, &touched);
    Check(touched.i == 1 && g_showCalls == 0, "touch DOWN is claimed and does not show the keyboard yet");
    CallVirtual(edit, "onTouchEvent", "(Landroid/view/MotionEvent;)Z", {up}, &touched);
    DexValue focused, accepting2;
    CallVirtual(edit, "isFocused", "()Z", {}, &focused);
    CallVirtual(manager, "isAcceptingText", "()Z", {}, &accepting2);
    Check(touched.i == 1 && focused.i == 1 && g_showCalls == 1 && accepting2.i == 1,
          "a tap focuses the EditText, raises the keyboard, and attaches its connection");

    type("ab");
    Check(textOf(edit) == "ab" && intOf(edit, "getSelectionStart") == 2,
          "keyboard text lands in the EditText and the cursor follows it");
    CallStatic("Landroid/view/inputmethod/InputMethodManager;", "deliverDeleteBackward", "()V", {},
               nullptr);
    Check(textOf(edit) == "a", "backspace removes the character before the cursor");
    type("\n");
    Check(textOf(edit) == "a\n", "Return in a multi-line field inserts a line break");

    CallVirtual(edit, "setSingleLine", "(Z)V", {DexValue::Int(1)}, nullptr);
    const int hidesBefore = g_hideCalls;
    type("\n");
    Check(textOf(edit) == "a\n" && g_hideCalls == hidesBefore + 1,
          "Return in a single-line field is the Done action: no text, keyboard dismissed");

    CallVirtual(edit, "setText", "(Ljava/lang/CharSequence;)V", {Str("xyz")}, nullptr);
    Check(textOf(edit) == "xyz" && intOf(edit, "getSelectionStart") == 3,
          "setText replaces the content and puts the cursor after it");
    type("1");
    CallVirtual(edit, "append", "(Ljava/lang/CharSequence;)V", {Str("!")}, nullptr);
    Check(textOf(edit) == "xyz1!", "append adds at the end and typing continues after setText");

    DexValue focusedOther;
    CallVirtual(other, "requestFocus", "()Z", {}, nullptr);
    CallVirtual(edit, "isFocused", "()Z", {}, &focused);
    CallVirtual(other, "isFocused", "()Z", {}, &focusedOther);
    Check(focused.i == 0 && focusedOther.i == 1, "focus is exclusive between EditTexts");
    type("Q");
    Check(textOf(other) == "Q" && textOf(edit) == "xyz1!",
          "keyboard text goes to the focused EditText only");

    CallVirtual(other, "setInputType", "(I)V", {DexValue::Int(0x81)}, nullptr);
    CallVirtual(other, "setImeOptions", "(I)V", {DexValue::Int(3)}, nullptr);
    CallVirtual(manager, "restartInput", "(Landroid/view/View;)V", {DexValue::Ref(other)}, nullptr);
    Check(kudroid_get_soft_input_type() == 0x81 &&
              (kudroid_get_soft_input_ime_options() & 0xff) == 3,
          "the focused field's input type and action reach the host for the keyboard layout");
    CallStatic("Landroid/view/inputmethod/InputMethodManager;", "resetForRelaunch", "()V", {}, nullptr);
    kudroid_set_soft_input_callbacks(nullptr, nullptr);

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
