// test_kuart_openurl.cpp - startActivity(ACTION_VIEW + URL) reaches the host's URL callback
// through the real framework.dex, and only for URL intents that are safe to hand out.
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
DexClassLinker* g_linker = nullptr;
Interpreter* g_interp = nullptr;
std::string g_openedUrl;
int g_openCount = 0;

void Check(bool condition, const char* message) {
    ++g_checks;
    std::printf("%s %s\n", condition ? "  OK  " : "  FAIL", message);
    if (!condition) ++g_failures;
}

void OnOpenUrl(const char* url) {
    g_openedUrl = url != nullptr ? url : "";
    ++g_openCount;
}

bool Run(DexMethod* method, std::vector<DexValue> args, DexValue* result) {
    if (method == nullptr) return false;
    g_interp->ClearPendingException();
    const DexValue value = g_interp->Execute(method, args.data(), args.size());
    const bool exception = g_interp->HasPendingException();
    if (exception) g_interp->ClearPendingException();
    if (!exception && result != nullptr) *result = value;
    return !exception;
}

bool CallStatic(const char* descriptor, const char* name, const char* sig,
                std::vector<DexValue> args, DexValue* result) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub || !g_interp->EnsureInitialized(klass)) return false;
    DexMethod* method = klass->FindDirectMethod(name, sig);
    if (method == nullptr) method = klass->FindVirtualMethod(name, sig);
    return Run(method, std::move(args), result);
}

DexObject* NewObject(const char* descriptor, const char* sig, std::vector<DexValue> args) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub || !g_interp->EnsureInitialized(klass)) return nullptr;
    DexObject* object = g_linker->AllocObject(klass);
    DexMethod* ctor = klass->FindDirectMethod("<init>", sig);
    if (object == nullptr || ctor == nullptr) return nullptr;
    args.insert(args.begin(), DexValue::Ref(object));
    return Run(ctor, args, nullptr) ? object : nullptr;
}

DexObject* ViewIntent(const char* action, const char* url) {
    DexValue uri;
    if (!CallStatic("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                    {DexValue::Ref(g_linker->NewString(url))}, &uri)) {
        return nullptr;
    }
    return NewObject("Landroid/content/Intent;", "(Ljava/lang/String;Landroid/net/Uri;)V",
                     {DexValue::Ref(g_linker->NewString(action)), uri});
}

// externalUrl() result, or "<null>".
std::string ExternalUrl(DexObject* intent) {
    DexValue v;
    if (!CallStatic("Landroid/app/ActivityThread;", "externalUrl",
                    "(Landroid/content/Intent;)Ljava/lang/String;", {DexValue::Ref(intent)}, &v)) {
        return "<error>";
    }
    return v.l == nullptr ? "<null>" : static_cast<DexString*>(v.l)->utf8;
}

void test_urls() {
    std::printf("[openurl] URL intents map to host URLs\n");
    Check(ExternalUrl(ViewIntent("android.intent.action.VIEW", "https://www.youtube.com/user/x")) ==
              "https://www.youtube.com/user/x",
          "https link passes through");
    Check(ExternalUrl(ViewIntent("android.intent.action.VIEW", "mailto:a@b.c")) == "mailto:a@b.c",
          "mailto passes through");
    Check(ExternalUrl(ViewIntent("android.intent.action.DIAL", "tel:123")) == "tel:123",
          "tel via DIAL passes through");
    Check(ExternalUrl(ViewIntent("android.intent.action.VIEW", "market://details?id=com.a.b")) ==
              "https://play.google.com/store/apps/details?id=com.a.b",
          "market:// becomes a Play web URL");
    Check(ExternalUrl(ViewIntent("android.intent.action.VIEW", "vnd.youtube:abc")) ==
              "https://www.youtube.com/watch?v=abc",
          "vnd.youtube becomes a watch URL");
    Check(ExternalUrl(ViewIntent("android.intent.action.VIEW", "file:///data/x")) == "<null>",
          "file: is never forwarded");
    Check(ExternalUrl(ViewIntent("android.intent.action.VIEW", "content://a/b")) == "<null>",
          "content: is never forwarded");
    Check(ExternalUrl(ViewIntent("android.intent.action.SEND", "https://x.y")) == "<null>",
          "other actions are not URL opens");
}

void test_open() {
    std::printf("[openurl] openExternal reaches the host callback\n");
    kudroid_set_open_url_callback(OnOpenUrl);
    DexValue v;
    DexObject* view = ViewIntent("android.intent.action.VIEW", "https://example.com/a?b=1");
    g_openCount = 0;
    Check(CallStatic("Landroid/app/ActivityThread;", "openExternal",
                     "(Landroid/content/Intent;)Z", {DexValue::Ref(view)}, &v) &&
              v.i == 1,
          "openExternal reports handled");
    Check(g_openCount == 1 && g_openedUrl == "https://example.com/a?b=1",
          "host callback received the URL once");

    DexObject* file = ViewIntent("android.intent.action.VIEW", "file:///etc/passwd");
    g_openCount = 0;
    Check(CallStatic("Landroid/app/ActivityThread;", "openExternal",
                     "(Landroid/content/Intent;)Z", {DexValue::Ref(file)}, &v) &&
              v.i == 0 && g_openCount == 0,
          "a local-file intent is refused and never reaches the host");
    kudroid_set_open_url_callback(nullptr);
}

}  // namespace

int main() {
    std::printf("=== KuART open url ===\n");
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

    test_urls();
    test_open();

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
