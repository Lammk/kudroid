// Integration test: run the real framework.dex inside KuART and drive the Java that
// was just added — java.util.regex, java.text.SimpleDateFormat, java.util.Calendar.
//
// Compiling is not evidence. These classes are large, mutually dependent, and reach
// deep into the interpreter (filled-new-array in enum $values(), string natives,
// ListResourceBundle lookups, exception tables). Executing them here is what proves
// KuART can actually run them on device.
//
// Methods are invoked directly through the linker rather than from synthesised
// bytecode: the point is to exercise the framework code, not the call sequence.
#include "kudroid/framework_dex_bytes.h"
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

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexField;
using kudroid::kuart::DexJniEnv;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexObject;
using kudroid::kuart::DexString;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

DexClassLinker* g_linker = nullptr;
Interpreter* g_interp = nullptr;

DexValue Str(const char* s) {
    return DexValue::Ref(reinterpret_cast<DexObject*>(g_linker->NewString(s)));
}

const char* Utf8Of(const DexValue& v) {
    auto* s = reinterpret_cast<DexString*>(v.l);
    return (s != nullptr && s->utf8 != nullptr) ? s->utf8 : "(null)";
}

// Call a static method, reporting any pending exception as a failure.
bool CallStatic(const char* descriptor, const char* name, const char* sig,
                std::vector<DexValue> args, DexValue* out, const char* what) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub) {
        std::printf("  FAIL %s: class %s not in framework.dex\n", what, descriptor);
        ++g_failures;
        return false;
    }
    g_interp->ClearPendingException();
    if (!g_interp->EnsureInitialized(klass)) {
        std::printf("  FAIL %s: <clinit> of %s failed: %s\n", what, descriptor,
                    g_interp->last_error().c_str());
        ++g_failures;
        g_interp->ClearPendingException();
        return false;
    }
    DexMethod* m = klass->FindDirectMethod(name, sig);
    if (m == nullptr) m = klass->FindVirtualMethod(name, sig);
    if (m == nullptr) {
        std::printf("  FAIL %s: no method %s%s\n", what, name, sig);
        ++g_failures;
        return false;
    }
    const DexValue r = g_interp->Execute(m, args.data(), args.size());
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s threw: %s\n", what, g_interp->last_error().c_str());
        ++g_failures;
        g_interp->ClearPendingException();
        return false;
    }
    if (out != nullptr) *out = r;
    return true;
}

// Call an instance method on `receiver`, dispatching against its real class.
bool CallVirtual(DexObject* receiver, const char* name, const char* sig,
                 std::vector<DexValue> args, DexValue* out, const char* what) {
    if (receiver == nullptr || receiver->clazz == nullptr) {
        std::printf("  FAIL %s: null receiver\n", what);
        ++g_failures;
        return false;
    }
    DexMethod* m = receiver->clazz->FindVirtualMethod(name, sig);
    if (m == nullptr) m = receiver->clazz->FindDirectMethod(name, sig);
    if (m == nullptr) {
        std::printf("  FAIL %s: no method %s%s on %s\n", what, name, sig,
                    receiver->clazz->PrettyName().c_str());
        ++g_failures;
        return false;
    }
    std::vector<DexValue> full;
    full.push_back(DexValue::Ref(receiver));
    for (const DexValue& v : args) full.push_back(v);

    g_interp->ClearPendingException();
    const DexValue r = g_interp->Execute(m, full.data(), full.size());
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s threw: %s\n", what, g_interp->last_error().c_str());
        ++g_failures;
        g_interp->ClearPendingException();
        return false;
    }
    if (out != nullptr) *out = r;
    return true;
}

// new <descriptor>(args) using the given constructor signature.
DexObject* NewObject(const char* descriptor, const char* ctorSig,
                     std::vector<DexValue> args, const char* what) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub) {
        std::printf("  FAIL %s: class %s not in framework.dex\n", what, descriptor);
        ++g_failures;
        return nullptr;
    }
    g_interp->ClearPendingException();
    if (!g_interp->EnsureInitialized(klass)) {
        std::printf("  FAIL %s: <clinit> failed: %s\n", what,
                    g_interp->last_error().c_str());
        ++g_failures;
        g_interp->ClearPendingException();
        return nullptr;
    }
    DexObject* obj = g_linker->AllocObject(klass);
    if (obj == nullptr) {
        std::printf("  FAIL %s: AllocObject failed\n", what);
        ++g_failures;
        return nullptr;
    }
    DexMethod* ctor = klass->FindDirectMethod("<init>", ctorSig);
    if (ctor == nullptr) {
        std::printf("  FAIL %s: no <init>%s\n", what, ctorSig);
        ++g_failures;
        return nullptr;
    }
    std::vector<DexValue> full;
    full.push_back(DexValue::Ref(obj));
    for (const DexValue& v : args) full.push_back(v);

    g_interp->Execute(ctor, full.data(), full.size());
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s ctor threw: %s\n", what, g_interp->last_error().c_str());
        ++g_failures;
        g_interp->ClearPendingException();
        return nullptr;
    }
    return obj;
}

}  // namespace

int main() {
    std::printf("=== KuART framework integration: regex + java.text ===\n");

    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex",
                           &error)) {
        std::printf("  FAIL AddDexFile(framework.dex): %s\n=== FAILED ===\n",
                    error.c_str());
        return 1;
    }
    std::printf("framework.dex: %zu bytes\n", g_framework_dex_size);

    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    g_linker = &linker;
    g_interp = &interp;

    // The interpreter's default budget is generous, but regex compilation is the most
    // instruction-hungry Java KuDroid runs; a low limit here would look like a hang.
    interp.set_instruction_limit(2000ull * 1000ull * 1000ull);

    // ── the classes are present and real, not auto-stubs ──
    {
        const char* wanted[] = {
            "Ljava/util/regex/Pattern;", "Ljava/util/regex/Matcher;",
            "Ljava/text/SimpleDateFormat;", "Ljava/text/DateFormat;",
            "Ljava/text/NumberFormat;", "Ljava/util/Calendar;",
            "Ljava/util/GregorianCalendar;", "Ljava/util/TimeZone;",
            "Ljava/util/ListResourceBundle;",
        };
        for (const char* d : wanted) {
            DexClass* k = linker.FindClass(d);
            Check(k != nullptr && !k->is_stub,
                  std::string("present and not a stub: ") + d);
        }
    }

    // ── java.util.regex ──
    // Pattern.matches(regex, input) compiles a pattern and runs the matcher, so a
    // single call already covers the Lexer, the AbstractCharClass tables and the
    // node graph.
    {
        DexValue r;
        if (CallStatic("Ljava/util/regex/Pattern;", "matches",
                       "(Ljava/lang/String;Ljava/lang/CharSequence;)Z",
                       {Str("[0-9]+"), Str("12345")}, &r, "Pattern.matches digits")) {
            Check(r.i == 1, "Pattern.matches(\"[0-9]+\", \"12345\") is true");
        }
        if (CallStatic("Ljava/util/regex/Pattern;", "matches",
                       "(Ljava/lang/String;Ljava/lang/CharSequence;)Z",
                       {Str("[0-9]+"), Str("12a45")}, &r,
                       "Pattern.matches non-digits")) {
            Check(r.i == 0, "Pattern.matches(\"[0-9]+\", \"12a45\") is false");
        }
        // Anchors, alternation and quantifiers in one pattern.
        if (CallStatic("Ljava/util/regex/Pattern;", "matches",
                       "(Ljava/lang/String;Ljava/lang/CharSequence;)Z",
                       {Str("^(foo|bar)-\\d{2,3}$"), Str("bar-123")}, &r,
                       "Pattern.matches alternation")) {
            Check(r.i == 1, "Pattern.matches(\"^(foo|bar)-\\\\d{2,3}$\", \"bar-123\")");
        }
        // A predefined character class, which goes through ListResourceBundle.
        if (CallStatic("Ljava/util/regex/Pattern;", "matches",
                       "(Ljava/lang/String;Ljava/lang/CharSequence;)Z",
                       {Str("\\p{Alpha}+"), Str("abcXYZ")}, &r,
                       "Pattern.matches \\p{Alpha}")) {
            Check(r.i == 1, "Pattern.matches(\"\\\\p{Alpha}+\", \"abcXYZ\")");
        }
    }

    // String.matches / replaceAll / split now route to the engine. Before this they
    // faked it: matches() compared for equality and replaceAll() did a literal
    // replace, returning wrong answers with no error.
    {
        DexValue r;
        DexObject* s = reinterpret_cast<DexObject*>(linker.NewString("a1b22c333"));
        if (CallVirtual(s, "matches", "(Ljava/lang/String;)Z",
                        {Str("([a-z]\\d+)+")}, &r, "String.matches")) {
            Check(r.i == 1, "\"a1b22c333\".matches(\"([a-z]\\\\d+)+\") is true");
        }
        // The old implementation returned true here because it compared for equality.
        if (CallVirtual(s, "matches", "(Ljava/lang/String;)Z",
                        {Str("a1b22c333")}, &r, "String.matches literal-as-regex")) {
            Check(r.i == 1, "a literal pattern still matches itself");
        }
        if (CallVirtual(s, "matches", "(Ljava/lang/String;)Z",
                        {Str("\\d+")}, &r, "String.matches negative")) {
            Check(r.i == 0, "\"a1b22c333\".matches(\"\\\\d+\") is false");
        }

        if (CallVirtual(s, "replaceAll", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                        {Str("\\d+"), Str("#")}, &r, "String.replaceAll")) {
            Check(std::strcmp(Utf8Of(r), "a#b#c#") == 0,
                  std::string("replaceAll(\"\\\\d+\", \"#\") == \"a#b#c#\", got \"") +
                      Utf8Of(r) + "\"");
        }
        if (CallVirtual(s, "replaceFirst", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                        {Str("\\d+"), Str("#")}, &r, "String.replaceFirst")) {
            Check(std::strcmp(Utf8Of(r), "a#b22c333") == 0,
                  std::string("replaceFirst == \"a#b22c333\", got \"") + Utf8Of(r) + "\"");
        }
    }

    // split() keeps a fast path for a plain one-character separator and uses the
    // engine otherwise; both need checking.
    {
        DexValue r;
        DexObject* csv = reinterpret_cast<DexObject*>(linker.NewString("a,b,c"));
        if (CallVirtual(csv, "split", "(Ljava/lang/String;)[Ljava/lang/String;",
                        {Str(",")}, &r, "String.split literal")) {
            auto* arr = static_cast<kudroid::kuart::DexArray*>(r.l);
            Check(arr != nullptr && arr->length == 3, "split(\",\") gives 3 parts");
            if (arr != nullptr && arr->length == 3) {
                Check(std::strcmp(reinterpret_cast<DexString*>(
                                      arr->Get<DexObject*>(1))->utf8, "b") == 0,
                      "split(\",\")[1] == \"b\"");
            }
        }
        DexObject* spaced = reinterpret_cast<DexObject*>(linker.NewString("a1b22c"));
        if (CallVirtual(spaced, "split", "(Ljava/lang/String;)[Ljava/lang/String;",
                        {Str("\\d+")}, &r, "String.split regex")) {
            auto* arr = static_cast<kudroid::kuart::DexArray*>(r.l);
            Check(arr != nullptr && arr->length == 3,
                  "split(\"\\\\d+\") gives 3 parts (regex path)");
        }
    }

    // ── java.util.Calendar / GregorianCalendar ──
    // A fixed instant, formatted in UTC so the host's zone cannot change the answer.
    // 1234567890000 ms = 2009-02-13 23:31:30 UTC.
    {
        DexValue utcZone;
        if (!CallStatic("Ljava/util/TimeZone;", "getTimeZone",
                        "(Ljava/lang/String;)Ljava/util/TimeZone;", {Str("UTC")},
                        &utcZone, "TimeZone.getTimeZone(UTC)")) {
            utcZone = DexValue();
        }

        DexObject* cal = NewObject("Ljava/util/GregorianCalendar;",
                                   "(Ljava/util/TimeZone;)V", {utcZone},
                                   "new GregorianCalendar(UTC)");
        if (cal != nullptr) {
            CallVirtual(cal, "setTimeInMillis", "(J)V",
                        {DexValue::Long(1234567890000LL)}, nullptr,
                        "Calendar.setTimeInMillis");

            DexValue y, mo, d, h, mi, sec, dow;
            const bool ok =
                CallVirtual(cal, "get", "(I)I", {DexValue::Int(1)}, &y, "get(YEAR)") &&
                CallVirtual(cal, "get", "(I)I", {DexValue::Int(2)}, &mo, "get(MONTH)") &&
                CallVirtual(cal, "get", "(I)I", {DexValue::Int(5)}, &d, "get(DAY)") &&
                CallVirtual(cal, "get", "(I)I", {DexValue::Int(11)}, &h, "get(HOUR_OF_DAY)") &&
                CallVirtual(cal, "get", "(I)I", {DexValue::Int(12)}, &mi, "get(MINUTE)") &&
                CallVirtual(cal, "get", "(I)I", {DexValue::Int(13)}, &sec, "get(SECOND)") &&
                CallVirtual(cal, "get", "(I)I", {DexValue::Int(7)}, &dow, "get(DAY_OF_WEEK)");
            if (ok) {
                Check(y.i == 2009, std::string("YEAR == 2009, got ") + std::to_string(y.i));
                Check(mo.i == 1, std::string("MONTH == 1 (February), got ") +
                                     std::to_string(mo.i));
                Check(d.i == 13, std::string("DAY_OF_MONTH == 13, got ") +
                                     std::to_string(d.i));
                Check(h.i == 23, std::string("HOUR_OF_DAY == 23, got ") +
                                     std::to_string(h.i));
                Check(mi.i == 31, std::string("MINUTE == 31, got ") + std::to_string(mi.i));
                Check(sec.i == 30, std::string("SECOND == 30, got ") + std::to_string(sec.i));
                // 2009-02-13 was a Friday; Calendar.FRIDAY == 6.
                Check(dow.i == 6, std::string("DAY_OF_WEEK == FRIDAY(6), got ") +
                                      std::to_string(dow.i));
            }

            // add() must carry across a month boundary, and clamp the day when the
            // target month is shorter.
            DexObject* cal2 = NewObject("Ljava/util/GregorianCalendar;",
                                        "(III)V",
                                        {DexValue::Int(2020), DexValue::Int(0),
                                         DexValue::Int(31)},
                                        "new GregorianCalendar(2020, JAN, 31)");
            if (cal2 != nullptr) {
                CallVirtual(cal2, "add", "(II)V",
                            {DexValue::Int(2), DexValue::Int(1)}, nullptr,
                            "Calendar.add(MONTH, 1)");
                DexValue y2, m2, d2;
                if (CallVirtual(cal2, "get", "(I)I", {DexValue::Int(1)}, &y2, "get(YEAR)") &&
                    CallVirtual(cal2, "get", "(I)I", {DexValue::Int(2)}, &m2, "get(MONTH)") &&
                    CallVirtual(cal2, "get", "(I)I", {DexValue::Int(5)}, &d2, "get(DAY)")) {
                    Check(y2.i == 2020 && m2.i == 1 && d2.i == 29,
                          std::string("Jan 31 + 1 month == Feb 29 2020 (leap), got ") +
                              std::to_string(y2.i) + "-" + std::to_string(m2.i + 1) + "-" +
                              std::to_string(d2.i));
                }
            }
        }
    }

    // ── java.text.SimpleDateFormat ──
    {
        DexObject* fmt = NewObject("Ljava/text/SimpleDateFormat;",
                                   "(Ljava/lang/String;)V",
                                   {Str("yyyy-MM-dd HH:mm:ss")},
                                   "new SimpleDateFormat(\"yyyy-MM-dd HH:mm:ss\")");
        if (fmt != nullptr) {
            DexValue utcZone;
            if (CallStatic("Ljava/util/TimeZone;", "getTimeZone",
                           "(Ljava/lang/String;)Ljava/util/TimeZone;", {Str("UTC")},
                           &utcZone, "TimeZone.getTimeZone(UTC)")) {
                CallVirtual(fmt, "setTimeZone", "(Ljava/util/TimeZone;)V", {utcZone},
                            nullptr, "DateFormat.setTimeZone(UTC)");
            }

            DexObject* date = NewObject("Ljava/util/Date;", "(J)V",
                                        {DexValue::Long(1234567890000LL)},
                                        "new Date(1234567890000)");
            if (date != nullptr) {
                DexValue out;
                if (CallVirtual(fmt, "format", "(Ljava/util/Date;)Ljava/lang/String;",
                                {DexValue::Ref(date)}, &out, "SimpleDateFormat.format")) {
                    Check(std::strcmp(Utf8Of(out), "2009-02-13 23:31:30") == 0,
                          std::string("format == \"2009-02-13 23:31:30\", got \"") +
                              Utf8Of(out) + "\"");
                }
            }

            // Round-trip: parse the text back and confirm the instant survives.
            DexValue parsed;
            if (CallVirtual(fmt, "parse", "(Ljava/lang/String;)Ljava/util/Date;",
                            {Str("2009-02-13 23:31:30")}, &parsed,
                            "SimpleDateFormat.parse")) {
                if (parsed.l != nullptr) {
                    DexValue millis;
                    if (CallVirtual(parsed.l, "getTime", "()J", {}, &millis,
                                    "Date.getTime")) {
                        Check(millis.j == 1234567890000LL,
                              std::string("parse round-trips to 1234567890000, got ") +
                                  std::to_string(millis.j));
                    }
                }
            }
        }

        // Named month and weekday come from DateFormatSymbols.
        DexObject* fmt2 = NewObject("Ljava/text/SimpleDateFormat;",
                                    "(Ljava/lang/String;)V", {Str("EEE, d MMM yyyy")},
                                    "new SimpleDateFormat(\"EEE, d MMM yyyy\")");
        if (fmt2 != nullptr) {
            DexValue utcZone;
            if (CallStatic("Ljava/util/TimeZone;", "getTimeZone",
                           "(Ljava/lang/String;)Ljava/util/TimeZone;", {Str("UTC")},
                           &utcZone, "TimeZone.getTimeZone(UTC)")) {
                CallVirtual(fmt2, "setTimeZone", "(Ljava/util/TimeZone;)V", {utcZone},
                            nullptr, "setTimeZone");
            }
            DexObject* date = NewObject("Ljava/util/Date;", "(J)V",
                                        {DexValue::Long(1234567890000LL)},
                                        "new Date");
            if (date != nullptr) {
                DexValue out;
                if (CallVirtual(fmt2, "format", "(Ljava/util/Date;)Ljava/lang/String;",
                                {DexValue::Ref(date)}, &out, "format with names")) {
                    Check(std::strcmp(Utf8Of(out), "Fri, 13 Feb 2009") == 0,
                          std::string("format == \"Fri, 13 Feb 2009\", got \"") +
                              Utf8Of(out) + "\"");
                }
            }
        }
    }

    // ── java.text.NumberFormat ──
    // SimpleDateFormat relies on this for zero-padded fields, so a regression here
    // shows up as malformed dates.
    {
        DexValue nf;
        if (CallStatic("Ljava/text/NumberFormat;", "getIntegerInstance",
                       "()Ljava/text/NumberFormat;", {}, &nf,
                       "NumberFormat.getIntegerInstance")) {
            if (nf.l != nullptr) {
                DexValue out;
                if (CallVirtual(nf.l, "format", "(J)Ljava/lang/String;",
                                {DexValue::Long(1234567)}, &out, "NumberFormat.format")) {
                    Check(std::strcmp(Utf8Of(out), "1,234,567") == 0,
                          std::string("format(1234567) == \"1,234,567\", got \"") +
                              Utf8Of(out) + "\"");
                }
            }
        }
    }

    // ── app bootstrap: AppComponentFactory + Application + package-derived paths ──
    //
    // Android runs the declared factory and the Application before any component, so
    // their static initialisers happen before all app code. KuDroid used to skip
    // both, which left every app whose setup lives there running with empty state
    // and failing far away from the cause. None of this is app-specific: the names
    // come from the manifest and the paths are derived from the package.
    {
        DexClass* factory = linker.FindClass("Landroid/app/AppComponentFactory;");
        Check(factory != nullptr && !factory->is_stub,
              "android.app.AppComponentFactory present and not a stub");

        // The factory instantiates components by name. An app subclass overrides
        // these and calls through super, so the base implementation has to work.
        if (factory != nullptr && !factory->is_stub) {
            Check(factory->FindVirtualMethod(
                      "instantiateApplication",
                      "(Ljava/lang/ClassLoader;Ljava/lang/String;)Landroid/app/Application;") != nullptr,
                  "AppComponentFactory.instantiateApplication exists");
            Check(factory->FindVirtualMethod(
                      "instantiateActivity",
                      "(Ljava/lang/ClassLoader;Ljava/lang/String;Landroid/content/Intent;)"
                      "Landroid/app/Activity;") != nullptr,
                  "AppComponentFactory.instantiateActivity exists");

            DexObject* f = NewObject("Landroid/app/AppComponentFactory;", "()V", {},
                                     "new AppComponentFactory");
            if (f != nullptr) {
                DexValue app;
                if (CallVirtual(f, "instantiateApplication",
                                "(Ljava/lang/ClassLoader;Ljava/lang/String;)"
                                "Landroid/app/Application;",
                                {DexValue::Ref(nullptr), Str("android.app.Application")},
                                &app, "instantiateApplication(Application)")) {
                    Check(app.l != nullptr && app.l->clazz != nullptr &&
                              app.l->clazz->PrettyName() == "android.app.Application",
                          "factory built an android.app.Application");
                }
            }
        }

        // ActivityThread must expose the statics native code reaches the Context
        // through: currentActivityThread().getApplication() is the standard JNI way
        // for an SDK that never sees an Activity to obtain one.
        DexClass* at = linker.FindClass("Landroid/app/ActivityThread;");
        Check(at != nullptr && !at->is_stub, "android.app.ActivityThread present");
        if (at != nullptr && !at->is_stub) {
            Check(at->FindDirectMethod("currentActivityThread",
                                       "()Landroid/app/ActivityThread;") != nullptr,
                  "ActivityThread.currentActivityThread() exists");
            Check(at->FindDirectMethod("currentApplication",
                                       "()Landroid/app/Application;") != nullptr,
                  "ActivityThread.currentApplication() exists");
            Check(at->FindVirtualMethod("getApplication",
                                        "()Landroid/app/Application;") != nullptr,
                  "ActivityThread.getApplication() exists");
        }

        // Paths must follow the manifest's package. ContextWrapper falls back to a
        // fixed "com.kudroid.app" when it has no base context, so anything comparing
        // getPackageName() with its own identity — license checks, provider
        // authorities, crash reporters — used to see the wrong app.
        DexObject* ctx = NewObject("Landroid/app/ApplicationContext;",
                                   "(Ljava/lang/String;)V", {Str("com.example.probe")},
                                   "new ApplicationContext(pkg)");
        if (ctx != nullptr) {
            DexValue pkg;
            if (CallVirtual(ctx, "getPackageName", "()Ljava/lang/String;", {}, &pkg,
                            "getPackageName")) {
                Check(std::strcmp(Utf8Of(pkg), "com.example.probe") == 0,
                      std::string("getPackageName() == manifest package, got \"") +
                          Utf8Of(pkg) + "\"");
            }
            DexValue dir;
            if (CallVirtual(ctx, "getFilesDir", "()Ljava/io/File;", {}, &dir,
                            "getFilesDir") &&
                dir.l != nullptr) {
                DexValue path;
                if (CallVirtual(dir.l, "getPath", "()Ljava/lang/String;", {}, &path,
                                "File.getPath")) {
                    Check(std::strcmp(Utf8Of(path),
                                      "/data/data/com.example.probe/files") == 0,
                          std::string("getFilesDir() derived from the package, got \"") +
                              Utf8Of(path) + "\"");
                }
            }
        }
    }

    // ── values libraries branch on ──
    //
    // These three used to hold placeholders that read as "not Android", and every
    // library that checks them took its desktop-JVM path. It is not a cosmetic
    // difference: okhttp's platform detection reads all three, and getting them wrong
    // left its Platform class permanently in error, which kills every HTTP call for
    // the rest of the process.
    {
        DexValue vmName;
        if (CallStatic("Ljava/lang/System;", "getProperty",
                       "(Ljava/lang/String;)Ljava/lang/String;", {Str("java.vm.name")},
                       &vmName, "System.getProperty(java.vm.name)")) {
            // Libraries compare this against "Dalvik" to decide they are on Android.
            Check(std::strcmp(Utf8Of(vmName), "Dalvik") == 0,
                  std::string("java.vm.name == \"Dalvik\", got \"") + Utf8Of(vmName) + "\"");
        }

        // Class.getClassLoader() must return a usable loader. The common obfuscator
        // pattern is Foo.class.getClassLoader().loadClass(name); on null that is an
        // NPE inside a <clinit>, which poisons the class for the whole process.
        DexClass* stringClass = linker.FindClass("Ljava/lang/String;");
        DexClass* classClass = linker.FindClass("Ljava/lang/Class;");
        if (stringClass != nullptr && classClass != nullptr) {
            DexObject* classObj =
                reinterpret_cast<DexObject*>(linker.GetClassObject(stringClass));
            DexValue loader;
            if (CallVirtual(classObj, "getClassLoader", "()Ljava/lang/ClassLoader;", {},
                            &loader, "Class.getClassLoader")) {
                Check(loader.l != nullptr, "getClassLoader() is not null");
                if (loader.l != nullptr) {
                    // And it has to actually resolve: a loader that cannot load is
                    // the same failure one call later.
                    DexValue loaded;
                    if (CallVirtual(loader.l, "loadClass",
                                    "(Ljava/lang/String;)Ljava/lang/Class;",
                                    {Str("java.lang.Integer")}, &loaded,
                                    "ClassLoader.loadClass")) {
                        Check(loaded.l != nullptr,
                              "loadClass(\"java.lang.Integer\") resolves");
                    }
                }
            }
        }

        // Security.getProviders() is indexed without a bounds check by real
        // libraries — okhttp writes getProviders()[0] verbatim — so an empty list is
        // an ArrayIndexOutOfBoundsException in their class initialiser.
        DexValue providers;
        if (CallStatic("Ljava/security/Security;", "getProviders",
                       "()[Ljava/security/Provider;", {}, &providers,
                       "Security.getProviders")) {
            auto* arr = reinterpret_cast<kudroid::kuart::DexArray*>(providers.l);
            Check(arr != nullptr && arr->length > 0,
                  std::string("getProviders() is non-empty, length ") +
                      std::to_string(arr != nullptr ? arr->length : -1));
            if (arr != nullptr && arr->length > 0) {
                DexObject* p = arr->Get<DexObject*>(0);
                DexValue name;
                if (p != nullptr && CallVirtual(p, "getName", "()Ljava/lang/String;", {},
                                                &name, "Provider.getName")) {
                    Check(std::strcmp(Utf8Of(name), "AndroidOpenSSL") == 0,
                          std::string("providers[0] is the Android default, got \"") +
                              Utf8Of(name) + "\"");
                }
            }
        }
    }

    // ── framework classes whose absence stopped a real launch ──
    {
        // ActivityManager.MemoryInfo: reading available memory during startup is
        // routine, and the nested class was missing because ActivityManager itself
        // was a generated stub.
        DexObject* mi = NewObject("Landroid/app/ActivityManager$MemoryInfo;", "()V", {},
                                  "new ActivityManager.MemoryInfo");
        if (mi != nullptr) {
            DexClass* k = mi->clazz;
            const DexField* avail =
                k != nullptr ? k->FindInstanceField("availMem", "J") : nullptr;
            Check(avail != nullptr, "MemoryInfo.availMem field present");
            if (avail != nullptr) {
                // Zero would tell an app the device is out of memory, and apps
                // degrade or refuse to run on that.
                Check(mi->GetField<int64_t>(avail->offset_or_slot) > 0,
                      "MemoryInfo.availMem is populated, not zero");
            }
        }
        DexClass* am = linker.FindClass("Landroid/app/ActivityManager;");
        Check(am != nullptr && !am->is_stub, "ActivityManager is real, not a stub");
        if (am != nullptr && !am->is_stub) {
            Check(am->FindVirtualMethod("getMemoryInfo",
                                        "(Landroid/app/ActivityManager$MemoryInfo;)V") != nullptr,
                  "ActivityManager.getMemoryInfo exists");
            Check(am->FindVirtualMethod("getMemoryClass", "()I") != nullptr,
                  "ActivityManager.getMemoryClass exists");
        }

        // Messenger(Handler) is the only form apps use; the stub had just a no-arg
        // constructor, so the call resolved to an auto-stub that did nothing and the
        // object looked valid while being unusable.
        DexClass* messenger = linker.FindClass("Landroid/os/Messenger;");
        Check(messenger != nullptr && !messenger->is_stub, "Messenger is real, not a stub");
        if (messenger != nullptr && !messenger->is_stub) {
            Check(messenger->FindDirectMethod("<init>", "(Landroid/os/Handler;)V") != nullptr,
                  "Messenger(Handler) constructor exists");
        }

        // RemoteException has to be a Throwable: as a bare stub it could not be
        // thrown or declared, so every signature mentioning it was unusable.
        DexClass* remote = linker.FindClass("Landroid/os/RemoteException;");
        DexClass* throwable = linker.FindClass("Ljava/lang/Throwable;");
        Check(remote != nullptr && throwable != nullptr && !remote->is_stub &&
                  remote->IsSubClassOf(throwable),
              "RemoteException is a Throwable");
    }

    // ── memory figures reach Java from the host ──
    //
    // Apps size caches, texture atlases and world chunks from these. Constants are
    // worse than they look: above what the device can give and the process is killed
    // mid-load, below it and the app runs degraded on hardware that could do better.
    {
        // MemoryInfo populates itself at construction because apps commonly build
        // one and read the fields without calling getMemoryInfo() first.
        DexObject* mi = NewObject("Landroid/app/ActivityManager$MemoryInfo;", "()V", {},
                                  "new ActivityManager.MemoryInfo");
        if (mi != nullptr && mi->clazz != nullptr) {
            const DexField* total = mi->clazz->FindInstanceField("totalMem", "J");
            const DexField* avail = mi->clazz->FindInstanceField("availMem", "J");
            if (total != nullptr && avail != nullptr) {
                const int64_t totalMem = mi->GetField<int64_t>(total->offset_or_slot);
                const int64_t availMem = mi->GetField<int64_t>(avail->offset_or_slot);
                std::printf("  device totalMem = %lld MiB, availMem = %lld MiB\n",
                            static_cast<long long>(totalMem >> 20),
                            static_cast<long long>(availMem >> 20));
                // Reading zero tells an app the device is out of memory.
                Check(totalMem > 256LL * 1024 * 1024,
                      "MemoryInfo.totalMem is the real device size");
                Check(availMem > 0 && availMem <= totalMem,
                      "MemoryInfo.availMem is populated and within total");
            }
        }

        // getMemoryClass() is the per-app heap budget apps divide to size caches.
        DexObject* am = NewObject("Landroid/app/ActivityManager;", "()V", {},
                                  "new ActivityManager");
        if (am != nullptr) {
            DexValue cls;
            if (CallVirtual(am, "getMemoryClass", "()I", {}, &cls, "getMemoryClass")) {
                std::printf("  memory class = %d MiB\n", cls.i);
                Check(cls.i >= 32 && cls.i <= 512,
                      std::string("getMemoryClass() within Android's range, got ") +
                          std::to_string(cls.i));
            }
            DexValue large;
            if (CallVirtual(am, "getLargeMemoryClass", "()I", {}, &large,
                            "getLargeMemoryClass")) {
                // An app opting into largeHeap must not end up with less.
                Check(large.i >= cls.i, "largeMemoryClass >= memoryClass");
            }
        }

        // Runtime's heap figures back the same budget: KuART has no separate managed
        // heap, so the process budget IS the heap budget.
        DexClass* runtime = linker.FindClass("Ljava/lang/Runtime;");
        if (runtime != nullptr && !runtime->is_stub) {
            DexValue rt;
            if (CallStatic("Ljava/lang/Runtime;", "getRuntime", "()Ljava/lang/Runtime;",
                           {}, &rt, "Runtime.getRuntime") && rt.l != nullptr) {
                DexValue maxMem;
                DexValue freeMem;
                DexValue totalHeap;
                const bool gotMax =
                    CallVirtual(rt.l, "maxMemory", "()J", {}, &maxMem, "Runtime.maxMemory");
                const bool gotFree =
                    CallVirtual(rt.l, "freeMemory", "()J", {}, &freeMem, "Runtime.freeMemory");
                const bool gotTotal = CallVirtual(rt.l, "totalMemory", "()J", {},
                                                  &totalHeap, "Runtime.totalMemory");
                if (gotMax && gotFree && gotTotal) {
                    std::printf("  Runtime max = %lld MiB, total = %lld MiB, free = %lld MiB\n",
                                static_cast<long long>(maxMem.j >> 20),
                                static_cast<long long>(totalHeap.j >> 20),
                                static_cast<long long>(freeMem.j >> 20));
                    Check(maxMem.j > 0, "Runtime.maxMemory() is non-zero");
                    Check(freeMem.j > 0, "Runtime.freeMemory() is non-zero");
                    // Apps compute headroom as max - total and compare it against an
                    // allocation, so these must not contradict each other.
                    Check(totalHeap.j <= maxMem.j, "totalMemory() <= maxMemory()");
                }
                DexValue cores;
                if (CallVirtual(rt.l, "availableProcessors", "()I", {}, &cores,
                                "Runtime.availableProcessors")) {
                    // Thread-pool sizes come from this; a fixed 4 either idles cores
                    // or oversubscribes a smaller device.
                    Check(cores.i > 0, std::string("availableProcessors() > 0, got ") +
                                           std::to_string(cores.i));
                }
            }
        }

        // Debug.MemoryInfo is polled to decide when to drop caches; a constant makes
        // the app believe its footprint never changes and release nothing.
        DexObject* dbg = NewObject("Landroid/os/Debug$MemoryInfo;", "()V", {},
                                   "new Debug.MemoryInfo");
        if (dbg != nullptr) {
            DexValue pss;
            if (CallVirtual(dbg, "getTotalPss", "()I", {}, &pss, "Debug getTotalPss")) {
                Check(pss.i > 0, std::string("Debug.MemoryInfo total PSS is real, got ") +
                                     std::to_string(pss.i) + " kB");
            }
        }
    }

    // ── theme and styled attributes ──
    //
    // Resources$Theme did not exist, so Activity.getTheme() was auto-stubbed to null
    // and anything chaining off it died. That stopped a real launch in onCreate:
    // androidx.core.splashscreen calls getTheme().resolveAttribute(...) from
    // install(), which apps invoke as their first onCreate statement, and every
    // AppCompat activity calls getTheme().obtainStyledAttributes(...) while
    // inflating.
    {
        DexClass* theme = linker.FindClass("Landroid/content/res/Resources$Theme;");
        Check(theme != nullptr && !theme->is_stub,
              "Resources$Theme is real, not a stub");
        DexClass* typedArray = linker.FindClass("Landroid/content/res/TypedArray;");
        Check(typedArray != nullptr && !typedArray->is_stub,
              "TypedArray is real, not a stub");

        // A Context must hand out a theme, never null: callers chain without a null
        // check because on Android the theme always exists.
        DexObject* ctx = NewObject("Landroid/app/ApplicationContext;",
                                   "(Ljava/lang/String;)V", {Str("com.example.theme")},
                                   "new ApplicationContext for theme");
        if (ctx != nullptr) {
            DexValue t;
            if (CallVirtual(ctx, "getTheme", "()Landroid/content/res/Resources$Theme;", {},
                            &t, "Context.getTheme")) {
                Check(t.l != nullptr, "Context.getTheme() is not null");

                if (t.l != nullptr) {
                    // resolveAttribute must report failure rather than throw: KuDroid
                    // resolves no attributes, and "not found" is a state callers
                    // already handle because Android produces it for any attribute
                    // absent from the current theme.
                    DexObject* tv = NewObject("Landroid/util/TypedValue;", "()V", {},
                                              "new TypedValue");
                    if (tv != nullptr) {
                        DexValue resolved;
                        if (CallVirtual(t.l, "resolveAttribute",
                                        "(ILandroid/util/TypedValue;Z)Z",
                                        {DexValue::Int(0x7f040001), DexValue::Ref(tv),
                                         DexValue::Int(1)},
                                        &resolved, "Theme.resolveAttribute")) {
                            Check(resolved.i == 0,
                                  "resolveAttribute() reports not-found instead of throwing");
                        }
                    }

                    // obtainStyledAttributes must return a usable TypedArray; the
                    // inflation idiom is obtain -> read -> recycle with no null check.
                    DexClass* intArray = linker.FindClass("[I");
                    if (intArray != nullptr) {
                        auto* attrs = linker.AllocArray(intArray, 3);
                        DexValue ta;
                        if (attrs != nullptr &&
                            CallVirtual(t.l, "obtainStyledAttributes",
                                        "([I)Landroid/content/res/TypedArray;",
                                        {DexValue::Ref(attrs)}, &ta,
                                        "Theme.obtainStyledAttributes")) {
                            Check(ta.l != nullptr, "obtainStyledAttributes() is not null");
                            if (ta.l != nullptr) {
                                DexValue len;
                                if (CallVirtual(ta.l, "length", "()I", {}, &len,
                                                "TypedArray.length")) {
                                    Check(len.i == 3,
                                          std::string("TypedArray.length() is the attr count, got ") +
                                              std::to_string(len.i));
                                }
                                // Missing attributes must return the caller's own
                                // default; those are its considered fallbacks, and
                                // substituting zero would silently change behaviour.
                                DexValue got;
                                if (CallVirtual(ta.l, "getInt", "(II)I",
                                                {DexValue::Int(0), DexValue::Int(4242)},
                                                &got, "TypedArray.getInt")) {
                                    Check(got.i == 4242,
                                          "getInt() falls back to the caller's default");
                                }
                                DexValue has;
                                if (CallVirtual(ta.l, "hasValue", "(I)Z",
                                                {DexValue::Int(0)}, &has,
                                                "TypedArray.hasValue")) {
                                    Check(has.i == 0, "hasValue() is false when nothing resolved");
                                }
                                // recycle() is called in a finally block by every
                                // obtain site.
                                CallVirtual(ta.l, "recycle", "()V", {}, nullptr,
                                            "TypedArray.recycle");
                            }
                        }
                    }
                }
            }
        }

        // CopyOnWriteArraySet backs listener registries that are iterated while
        // callbacks register more; a missing class surfaces inside an unrelated
        // callback.
        DexObject* set = NewObject("Ljava/util/concurrent/CopyOnWriteArraySet;", "()V", {},
                                   "new CopyOnWriteArraySet");
        if (set != nullptr) {
            DexValue added;
            if (CallVirtual(set, "add", "(Ljava/lang/Object;)Z",
                            {Str("listener")}, &added, "CopyOnWriteArraySet.add")) {
                Check(added.i != 0, "add() accepts a new element");
            }
            // Set semantics: the same element must not be admitted twice.
            DexValue again;
            if (CallVirtual(set, "add", "(Ljava/lang/Object;)Z", {Str("listener")}, &again,
                            "CopyOnWriteArraySet.add duplicate")) {
                Check(again.i == 0, "add() rejects a duplicate");
            }
            DexValue size;
            if (CallVirtual(set, "size", "()I", {}, &size, "CopyOnWriteArraySet.size")) {
                Check(size.i == 1, std::string("size() is 1 after a duplicate add, got ") +
                                       std::to_string(size.i));
            }
        }
    }

    // ── fields the app writes directly ──
    //
    // A class that is present but missing a field is a worse failure than a missing
    // class: `new X()` succeeds, then the first field access throws NoSuchFieldError
    // and there is no auto-stub fallback, because object layout is fixed by
    // LinkClass. EditorInfo was the case that stopped a launch — GameActivity
    // constructs one in onCreate and writes inputType, so createSurfaceView() never
    // finished and no surface existed to draw on.
    {
        struct FieldCase {
            const char* descriptor;
            const char* ctor_sig;
            const char* field;
            const char* type;
            const char* why;
        };
        // Every one of these is written directly by app or library code, so the name
        // and descriptor have to match AOSP exactly.
        const FieldCase cases[] = {
            {"Landroid/view/inputmethod/EditorInfo;", "()V", "inputType", "I",
             "GameActivity.getImeEditorInfo writes it during onCreate"},
            {"Landroid/view/inputmethod/EditorInfo;", "()V", "imeOptions", "I", nullptr},
            {"Landroid/view/inputmethod/EditorInfo;", "()V", "actionId", "I", nullptr},
            {"Landroid/view/inputmethod/EditorInfo;", "()V", "initialSelStart", "I", nullptr},
            {"Landroid/graphics/Paint$FontMetricsInt;", "()V", "ascent", "I",
             "text layout places baselines from these"},
            {"Landroid/graphics/Paint$FontMetricsInt;", "()V", "descent", "I", nullptr},
            {"Landroid/graphics/BitmapFactory$Options;", "()V", "inJustDecodeBounds", "Z",
             "measuring an image before allocating for it"},
            {"Landroid/graphics/BitmapFactory$Options;", "()V", "outWidth", "I", nullptr},
            {"Landroid/graphics/BitmapFactory$Options;", "()V", "inSampleSize", "I", nullptr},
            {"Landroid/content/pm/ActivityInfo;", "()V", "exported", "Z",
             "read off a resolved component"},
            {"Landroid/content/pm/ApplicationInfo;", "()V", "minSdkVersion", "I", nullptr},
        };
        for (const FieldCase& c : cases) {
            DexClass* k = linker.FindClass(c.descriptor);
            if (k == nullptr || k->is_stub) {
                Check(false, std::string("class present: ") + c.descriptor);
                continue;
            }
            const bool has = k->FindInstanceField(c.field, c.type) != nullptr;
            Check(has, std::string(c.descriptor) + "." + c.field + " exists" +
                           (c.why != nullptr ? std::string(" (") + c.why + ")" : ""));
        }

        // Writing and reading a field back proves the layout works, not just that the
        // name resolves.
        DexObject* ei = NewObject("Landroid/view/inputmethod/EditorInfo;", "()V", {},
                                  "new EditorInfo");
        if (ei != nullptr && ei->clazz != nullptr) {
            const DexField* f = ei->clazz->FindInstanceField("inputType", "I");
            if (f != nullptr) {
                ei->SetField<int32_t>(f->offset_or_slot, 0x21);
                Check(ei->GetField<int32_t>(f->offset_or_slot) == 0x21,
                      "EditorInfo.inputType round-trips a written value");
            }
        }

        // Layout sentinels: nearly every programmatic addView() sizes a child with
        // these, and they were absent.
        DexClass* lp = linker.FindClass("Landroid/view/ViewGroup$LayoutParams;");
        Check(lp != nullptr && !lp->is_stub, "ViewGroup$LayoutParams is real");
        if (lp != nullptr && !lp->is_stub) {
            const DexField* match = lp->FindStaticField("MATCH_PARENT", "I");
            const DexField* wrap = lp->FindStaticField("WRAP_CONTENT", "I");
            Check(match != nullptr && wrap != nullptr,
                  "MATCH_PARENT and WRAP_CONTENT exist");
            if (match != nullptr && wrap != nullptr && interp.EnsureInitialized(lp)) {
                // The values are compared against directly by app code, so they
                // cannot be renumbered.
                Check(lp->static_values[match->offset_or_slot].i == -1,
                      "MATCH_PARENT is -1, as app code expects");
                Check(lp->static_values[wrap->offset_or_slot].i == -2,
                      "WRAP_CONTENT is -2, as app code expects");
            }
        }

        // WindowManager.LayoutParams: a fullscreen game configures its window through
        // these, and a missing one aborted setup partway.
        DexClass* wlp = linker.FindClass("Landroid/view/WindowManager$LayoutParams;");
        if (wlp != nullptr && !wlp->is_stub) {
            const char* names[] = {"format", "softInputMode", "layoutInDisplayCutoutMode",
                                   "windowAnimations", "packageName"};
            const char* types[] = {"I", "I", "I", "I", "Ljava/lang/String;"};
            for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
                Check(wlp->FindInstanceField(names[i], types[i]) != nullptr,
                      std::string("WindowManager$LayoutParams.") + names[i] + " exists");
            }
        }

        // Enum-like constants used as values; an empty stub made every reference throw.
        DexClass* cfg = linker.FindClass("Landroid/graphics/Bitmap$Config;");
        if (cfg != nullptr && !cfg->is_stub) {
            Check(cfg->FindStaticField("ARGB_8888", "Landroid/graphics/Bitmap$Config;") != nullptr,
                  "Bitmap.Config.ARGB_8888 exists");
        }
        DexClass* fmt = linker.FindClass("Landroid/graphics/Bitmap$CompressFormat;");
        if (fmt != nullptr && !fmt->is_stub) {
            Check(fmt->FindStaticField("PNG", "Landroid/graphics/Bitmap$CompressFormat;") != nullptr,
                  "Bitmap.CompressFormat.PNG exists");
        }

        // okhttp's Android detection looks this up by name; the failed lookup left its
        // Platform class in error, which kills every later HTTP call.
        DexClass* ossl = linker.FindClass("Lcom/android/org/conscrypt/OpenSSLSocketImpl;");
        Check(ossl != nullptr && !ossl->is_stub,
              "com.android.org.conscrypt.OpenSSLSocketImpl is findable");

        // Methods that were being auto-stubbed, i.e. silently doing nothing.
        DexClass* logCls = linker.FindClass("Landroid/util/Log;");
        if (logCls != nullptr) {
            Check(logCls->FindDirectMethod("isLoggable", "(Ljava/lang/String;I)Z") != nullptr,
                  "Log.isLoggable exists (libraries gate work on it)");
        }
        DexClass* classCls = linker.FindClass("Ljava/lang/Class;");
        if (classCls != nullptr) {
            Check(classCls->FindVirtualMethod("getPackage", "()Ljava/lang/Package;") != nullptr,
                  "Class.getPackage exists");
        }
        DexClass* logger = linker.FindClass("Ljava/util/logging/Logger;");
        if (logger != nullptr) {
            Check(logger->FindVirtualMethod("setUseParentHandlers", "(Z)V") != nullptr,
                  "Logger.setUseParentHandlers exists");
        }
    }

    // ── getSystemService actually returns the managers ──
    //
    // A manager class that exists under framework/ but is not reachable through
    // getSystemService is, from the app's side, identical to one that was never
    // written — and the failure is quiet, because the method just answers null.
    // Minecraft's onCreate did getSystemService(INPUT_METHOD_SERVICE), got null and
    // threw RuntimeException("Can't get IMM"); the whole Activity launch failed while
    // InputMethodManager.java had been present the entire time.
    //
    // Checking the class list is not enough for the same reason. Each name has to be
    // asked for, through a real Context, and the answer has to be an instance of the
    // right type.
    {
        DexObject* ctx = NewObject("Landroid/app/ApplicationContext;",
                                   "(Ljava/lang/String;)V", {Str("com.example.services")},
                                   "new ApplicationContext for getSystemService");
        struct ServiceCase {
            const char* name;        // the string apps pass
            const char* constant;    // Context.<CONSTANT> that must hold it
            const char* expect;      // descriptor the result must be an instance of
            const char* why;
        };
        // INPUT_METHOD_SERVICE and ACTIVITY_SERVICE are first: both had their class
        // shipped with no mapping, and the first one stopped the game.
        const ServiceCase cases[] = {
            {"input_method", "INPUT_METHOD_SERVICE",
             "Landroid/view/inputmethod/InputMethodManager;",
             "onCreate throws \"Can't get IMM\" on null"},
            {"activity", "ACTIVITY_SERVICE", "Landroid/app/ActivityManager;",
             "getMemoryInfo callers"},
            {"window", "WINDOW_SERVICE", "Landroid/view/WindowManager;", nullptr},
            {"layout_inflater", "LAYOUT_INFLATER_SERVICE",
             "Landroid/view/LayoutInflater;", nullptr},
            {"sensor", "SENSOR_SERVICE", "Landroid/hardware/SensorManager;", nullptr},
            {"audio", "AUDIO_SERVICE", "Landroid/media/AudioManager;", nullptr},
            {"vibrator", "VIBRATOR_SERVICE", "Landroid/os/Vibrator;", nullptr},
            {"connectivity", "CONNECTIVITY_SERVICE",
             "Landroid/net/ConnectivityManager;", nullptr},
            {"wifi", "WIFI_SERVICE", "Landroid/net/wifi/WifiManager;", nullptr},
            {"phone", "TELEPHONY_SERVICE", "Landroid/telephony/TelephonyManager;", nullptr},
            {"clipboard", "CLIPBOARD_SERVICE", "Landroid/content/ClipboardManager;", nullptr},
            {"notification", "NOTIFICATION_SERVICE",
             "Landroid/app/NotificationManager;", nullptr},
            {"alarm", "ALARM_SERVICE", "Landroid/app/AlarmManager;", nullptr},
            {"power", "POWER_SERVICE", "Landroid/os/PowerManager;", nullptr},
            {"keyguard", "KEYGUARD_SERVICE", "Landroid/app/KeyguardManager;", nullptr},
            {"accessibility", "ACCESSIBILITY_SERVICE",
             "Landroid/view/accessibility/AccessibilityManager;", nullptr},
            {"account", "ACCOUNT_SERVICE", "Landroid/accounts/AccountManager;", nullptr},
            {"appops", "APP_OPS_SERVICE", "Landroid/app/AppOpsManager;", nullptr},
            {"bluetooth", "BLUETOOTH_SERVICE", "Landroid/bluetooth/BluetoothManager;", nullptr},
            {"display", "DISPLAY_SERVICE", "Landroid/hardware/display/DisplayManager;", nullptr},
            {"fingerprint", "FINGERPRINT_SERVICE",
             "Landroid/hardware/fingerprint/FingerprintManager;", nullptr},
            {"input", "INPUT_SERVICE", "Landroid/hardware/input/InputManager;", nullptr},
            {"jobscheduler", "JOB_SCHEDULER_SERVICE", "Landroid/app/job/JobScheduler;", nullptr},
            {"location", "LOCATION_SERVICE", "Landroid/location/LocationManager;", nullptr},
            {"shortcut", "SHORTCUT_SERVICE", "Landroid/content/pm/ShortcutManager;", nullptr},
        };

        DexClass* ctxCls = linker.FindClass("Landroid/content/Context;");
        for (const ServiceCase& c : cases) {
            // The constant has to exist and hold the name apps actually pass: code
            // reads Context.INPUT_METHOD_SERVICE rather than the literal, so a
            // constant with the wrong value fails in a way the literal would not show.
            if (ctxCls != nullptr && interp.EnsureInitialized(ctxCls)) {
                const DexField* f = ctxCls->FindStaticField(c.constant, "Ljava/lang/String;");
                Check(f != nullptr, std::string("Context.") + c.constant + " exists");
                if (f != nullptr) {
                    const DexValue v = ctxCls->static_values[f->offset_or_slot];
                    auto* s = reinterpret_cast<DexString*>(v.l);
                    Check(s != nullptr && s->utf8 != nullptr &&
                              std::strcmp(s->utf8, c.name) == 0,
                          std::string("Context.") + c.constant + " == \"" + c.name + "\"");
                }
            }

            if (ctx == nullptr) continue;
            DexValue svc;
            if (!CallVirtual(ctx, "getSystemService",
                             "(Ljava/lang/String;)Ljava/lang/Object;", {Str(c.name)},
                             &svc, "getSystemService")) {
                continue;
            }
            const std::string what = std::string("getSystemService(\"") + c.name +
                                     "\") -> non-null" +
                                     (c.why != nullptr ? std::string(" (") + c.why + ")" : "");
            Check(svc.l != nullptr, what);
            if (svc.l == nullptr) continue;

            // And it has to be the right type: returning some other manager would
            // pass a null check and then fail on the first method call.
            DexClass* expect = linker.FindClass(c.expect);
            DexClass* got = linker.ClassOfObject(svc.l);
            Check(expect != nullptr && got != nullptr && got->IsSubClassOf(expect),
                  std::string("  ...and it is a ") + c.expect + ", got " +
                      (got != nullptr ? got->PrettyName() : "null"));
        }

        // The IME manager is a process singleton in AOSP and apps compare instances:
        // they cache what getSystemService returned and expect a later call to reach
        // the same object, so soft-keyboard state stays consistent.
        if (ctx != nullptr) {
            DexValue a, b;
            if (CallVirtual(ctx, "getSystemService",
                            "(Ljava/lang/String;)Ljava/lang/Object;", {Str("input_method")},
                            &a, "getSystemService(input_method) #1") &&
                CallVirtual(ctx, "getSystemService",
                            "(Ljava/lang/String;)Ljava/lang/Object;", {Str("input_method")},
                            &b, "getSystemService(input_method) #2")) {
                Check(a.l != nullptr && a.l == b.l,
                      "InputMethodManager is the same instance every time");
            }
        }

        // An unknown name still returns null — that is Android's contract and apps
        // are written to cope — but it must not throw, or a defensive
        // getSystemService for an optional service would kill the caller.
        if (ctx != nullptr) {
            DexValue none;
            const bool ok = CallVirtual(ctx, "getSystemService",
                                        "(Ljava/lang/String;)Ljava/lang/Object;",
                                        {Str("kudroid_no_such_service")}, &none,
                                        "getSystemService(unknown)");
            Check(ok && none.l == nullptr,
                  "an unknown service name returns null without throwing");
        }

        // The methods Minecraft calls on the IME right after obtaining it. Each one
        // must answer optimistically: a false from isActive() or showSoftInput()
        // makes an app disable its own text entry.
        {
            DexObject* imm = nullptr;
            if (ctx != nullptr) {
                DexValue v;
                if (CallVirtual(ctx, "getSystemService",
                                "(Ljava/lang/String;)Ljava/lang/Object;",
                                {Str("input_method")}, &v, "IMM for method probe")) {
                    imm = v.l;
                }
            }
            if (imm != nullptr) {
                DexValue r;
                if (CallVirtual(imm, "isAcceptingText", "()Z", {}, &r, "isAcceptingText")) {
                    Check(r.i != 0, "IMM.isAcceptingText() is true");
                }
                if (CallVirtual(imm, "isActive", "()Z", {}, &r, "isActive")) {
                    Check(r.i != 0, "IMM.isActive() is true");
                }
                if (CallVirtual(imm, "showSoftInput", "(Landroid/view/View;I)Z",
                                {DexValue::Ref(nullptr), DexValue::Int(0)}, &r,
                                "showSoftInput")) {
                    Check(r.i != 0, "IMM.showSoftInput() reports success");
                }
                if (CallVirtual(imm, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z",
                                {DexValue::Ref(nullptr), DexValue::Int(0)}, &r,
                                "hideSoftInputFromWindow")) {
                    Check(r.i != 0, "IMM.hideSoftInputFromWindow() reports success");
                }
            }
        }
    }

    // ── BaseInputConnection edits a real buffer ──
    //
    // Apps subclass it and override only what they need, inheriting the rest, so the
    // inherited methods have to be correct rather than stubs. It used to be an
    // auto-generated stub with a no-arg constructor only, which meant the app's
    // super(view, fullEditor) resolved to an auto-stub and the connection was never
    // attached to its view.
    {
        DexClass* bic = linker.FindClass("Landroid/view/inputmethod/BaseInputConnection;");
        Check(bic != nullptr && !bic->is_stub, "BaseInputConnection is real, not a stub");
        if (bic != nullptr && !bic->is_stub) {
            Check(bic->FindDirectMethod("<init>", "(Landroid/view/View;Z)V") != nullptr,
                  "BaseInputConnection(View, boolean) exists (apps call it via super)");

            DexClass* ic = linker.FindClass("Landroid/view/inputmethod/InputConnection;");
            // InputConnection must be an interface: d8 compiles a call through an
            // InputConnection-typed variable to invoke-interface, and app classes
            // declare `implements InputConnection`.
            Check(ic != nullptr && ic->IsInterface(), "InputConnection is an interface");
            Check(bic->IsSubClassOf(ic), "BaseInputConnection implements InputConnection");
        }

        DexObject* conn = NewObject("Landroid/view/inputmethod/BaseInputConnection;",
                                    "(Landroid/view/View;Z)V",
                                    {DexValue::Ref(nullptr), DexValue::Int(1)},
                                    "new BaseInputConnection(null, true)");
        if (conn != nullptr) {
            DexValue r;
            // commitText then read it back: this is the round trip an IME performs,
            // and a stubbed getTextBeforeCursor returning null would make the IME
            // believe the field is empty and re-send everything.
            if (CallVirtual(conn, "commitText", "(Ljava/lang/CharSequence;I)Z",
                            {Str("hello"), DexValue::Int(1)}, &r, "commitText")) {
                Check(r.i != 0, "commitText returns true");
            }
            if (CallVirtual(conn, "getTextBeforeCursor", "(II)Ljava/lang/CharSequence;",
                            {DexValue::Int(5), DexValue::Int(0)}, &r,
                            "getTextBeforeCursor")) {
                DexValue str;
                if (r.l != nullptr &&
                    CallVirtual(r.l, "toString", "()Ljava/lang/String;", {}, &str,
                                "toString of text before cursor")) {
                    Check(std::strcmp(Utf8Of(str), "hello") == 0,
                          std::string("getTextBeforeCursor returns what was committed,"
                                      " got \"") + Utf8Of(str) + "\"");
                }
            }
            // deleteSurroundingText has to move the cursor as well as the text; getting
            // the order wrong (deleting before the selection first) corrupts offsets.
            if (CallVirtual(conn, "deleteSurroundingText", "(II)Z",
                            {DexValue::Int(2), DexValue::Int(0)}, &r,
                            "deleteSurroundingText")) {
                Check(r.i != 0, "deleteSurroundingText returns true");
            }
            if (CallVirtual(conn, "getTextBeforeCursor", "(II)Ljava/lang/CharSequence;",
                            {DexValue::Int(5), DexValue::Int(0)}, &r,
                            "getTextBeforeCursor after delete")) {
                DexValue str;
                if (r.l != nullptr &&
                    CallVirtual(r.l, "toString", "()Ljava/lang/String;", {}, &str,
                                "toString after delete")) {
                    Check(std::strcmp(Utf8Of(str), "hel") == 0,
                          std::string("deleting 2 before the cursor leaves \"hel\","
                                      " got \"") + Utf8Of(str) + "\"");
                }
            }
            // Nothing is selected, so this must be null rather than "" — an IME uses
            // the distinction to decide whether a commit replaces or inserts.
            if (CallVirtual(conn, "getSelectedText", "(I)Ljava/lang/CharSequence;",
                            {DexValue::Int(0)}, &r, "getSelectedText")) {
                Check(r.l == nullptr, "getSelectedText is null when nothing is selected");
            }
        }
    }

    // ── the soft-keyboard round trip ──
    //
    // KuDroid has no keyboard; the host's is used, so the guest side is two halves
    // that have to meet: showSoftInput reaches the host, and text typed there comes
    // back to whichever InputConnection is registered. Neither half is observable
    // from the other, so both are driven here.
    {
        DexClass* immCls = linker.FindClass("Landroid/view/inputmethod/InputMethodManager;");
        Check(immCls != nullptr && !immCls->is_stub, "InputMethodManager is real");

        // The host bridge is a native method. If it were auto-stubbed instead, every
        // showSoftInput would silently do nothing while still reporting success.
        if (immCls != nullptr) {
            const char* natives[] = {"showSoftInputNative", "hideSoftInputNative",
                                     "isSoftInputVisibleNative"};
            const char* sigs[] = {"(I)Z", "()Z", "()Z"};
            for (size_t i = 0; i < sizeof(natives) / sizeof(natives[0]); ++i) {
                DexMethod* m = immCls->FindDirectMethod(natives[i], sigs[i]);
                Check(m != nullptr && m->IsNative(),
                      std::string("InputMethodManager.") + natives[i] +
                          " is declared native (host bridge)");
            }
        }

        // A View that accepts text: onCreateInputConnection returning null is right
        // for a plain View, and is what showSoftInput uses to decide where text goes.
        DexClass* viewCls = linker.FindClass("Landroid/view/View;");
        if (viewCls != nullptr) {
            Check(viewCls->FindVirtualMethod(
                      "onCreateInputConnection",
                      "(Landroid/view/inputmethod/EditorInfo;)"
                      "Landroid/view/inputmethod/InputConnection;") != nullptr,
                  "View.onCreateInputConnection exists (IME attachment point)");
            // androidx's WindowCompat reads these off the decor view during onCreate;
            // their absence is what stopped Minecraft with a NullPointerException.
            Check(viewCls->FindVirtualMethod("getSystemUiVisibility", "()I") != nullptr,
                  "View.getSystemUiVisibility exists");
            Check(viewCls->FindVirtualMethod("setSystemUiVisibility", "(I)V") != nullptr,
                  "View.setSystemUiVisibility exists");
        }

        // Flags must round-trip. An auto-stubbed getter returns 0 and the setter
        // discards, so an app that reads back what it set sees the wrong value — worse
        // than a missing method, because nothing reports it.
        DexObject* view = NewObject("Landroid/view/View;", "(Landroid/content/Context;)V",
                                    {DexValue::Ref(nullptr)}, "new View(null)");
        if (view != nullptr) {
            DexValue r;
            if (CallVirtual(view, "setSystemUiVisibility", "(I)V",
                            {DexValue::Int(0x00000504)}, &r, "setSystemUiVisibility") &&
                CallVirtual(view, "getSystemUiVisibility", "()I", {}, &r,
                            "getSystemUiVisibility")) {
                Check(r.i == 0x00000504,
                      std::string("system UI flags round-trip, got ") +
                          std::to_string(r.i) + " (expected 1284)");
            }
            // A plain View accepts no text.
            if (CallVirtual(view, "onCheckIsTextEditor", "()Z", {}, &r,
                            "onCheckIsTextEditor")) {
                Check(r.i == 0, "a plain View is not a text editor");
            }
        }

        // Text delivery: register a connection, hand text to the static entry point
        // ActivityThread uses, and read it back out of the connection. This is the
        // whole host->guest path minus the host.
        DexObject* conn2 = NewObject("Landroid/view/inputmethod/BaseInputConnection;",
                                     "(Landroid/view/View;Z)V",
                                     {DexValue::Ref(nullptr), DexValue::Int(1)},
                                     "connection for delivery");
        if (immCls != nullptr && conn2 != nullptr && interp.EnsureInitialized(immCls)) {
            DexValue ignored;
            if (CallStatic("Landroid/view/inputmethod/InputMethodManager;",
                           "setCurrentInputConnection",
                           "(Landroid/view/View;Landroid/view/inputmethod/InputConnection;)V",
                           {DexValue::Ref(nullptr), DexValue::Ref(conn2)}, &ignored,
                           "setCurrentInputConnection")) {
                if (CallStatic("Landroid/view/inputmethod/InputMethodManager;",
                               "deliverText", "(Ljava/lang/String;)V", {Str("kudroid")},
                               &ignored, "deliverText")) {
                    DexValue r;
                    if (CallVirtual(conn2, "getTextBeforeCursor",
                                    "(II)Ljava/lang/CharSequence;",
                                    {DexValue::Int(16), DexValue::Int(0)}, &r,
                                    "read delivered text")) {
                        DexValue str;
                        if (r.l != nullptr &&
                            CallVirtual(r.l, "toString", "()Ljava/lang/String;", {}, &str,
                                        "toString of delivered text")) {
                            Check(std::strcmp(Utf8Of(str), "kudroid") == 0,
                                  std::string("host keystrokes reach the registered"
                                              " connection, got \"") + Utf8Of(str) + "\"");
                        }
                    }
                }
                // Backspace takes the other native entry point and must reach the same
                // connection.
                if (CallStatic("Landroid/view/inputmethod/InputMethodManager;",
                               "deliverDeleteBackward", "()V", {}, &ignored,
                               "deliverDeleteBackward")) {
                    DexValue r;
                    if (CallVirtual(conn2, "getTextBeforeCursor",
                                    "(II)Ljava/lang/CharSequence;",
                                    {DexValue::Int(16), DexValue::Int(0)}, &r,
                                    "read after backspace")) {
                        DexValue str;
                        if (r.l != nullptr &&
                            CallVirtual(r.l, "toString", "()Ljava/lang/String;", {}, &str,
                                        "toString after backspace")) {
                            Check(std::strcmp(Utf8Of(str), "kudroi") == 0,
                                  std::string("backspace deletes one char, got \"") +
                                      Utf8Of(str) + "\"");
                        }
                    }
                }
            }

            // Nothing focused: text must be dropped, not applied to a stale
            // connection, and must not throw — the host keeps sending keystrokes
            // whether or not the guest has an editor.
            if (CallStatic("Landroid/view/inputmethod/InputMethodManager;",
                           "setCurrentInputConnection",
                           "(Landroid/view/View;Landroid/view/inputmethod/InputConnection;)V",
                           {DexValue::Ref(nullptr), DexValue::Ref(nullptr)}, &ignored,
                           "clear the current connection")) {
                const bool ok = CallStatic("Landroid/view/inputmethod/InputMethodManager;",
                                           "deliverText", "(Ljava/lang/String;)V",
                                           {Str("dropped")}, &ignored,
                                           "deliverText with nothing focused");
                Check(ok, "text with no focused connection is dropped without throwing");
            }
        }
    }

    // ── the decor view exists before anyone sets content ──
    //
    // WindowCompat.setDecorFitsSystemWindows chains
    // window.getDecorView().getSystemUiVisibility() during onCreate, on essentially
    // every modern app. getDecorView answered null on a Window nobody had called
    // setContentView on, so Minecraft's onCreate died inside
    // GameActivity.createSurfaceView with a NullPointerException.
    {
        DexObject* activity = NewObject("Landroid/app/Activity;", "()V", {},
                                       "new Activity");
        if (activity != nullptr) {
            DexValue w1, w2;
            if (CallVirtual(activity, "getWindow", "()Landroid/view/Window;", {}, &w1,
                            "getWindow #1") &&
                CallVirtual(activity, "getWindow", "()Landroid/view/Window;", {}, &w2,
                            "getWindow #2")) {
                // One Window per Activity. A fresh instance per call loses everything
                // set through it, including the decor view that setContentView records.
                Check(w1.l != nullptr && w1.l == w2.l,
                      "Activity.getWindow() returns the same Window every time");

                if (w1.l != nullptr) {
                    DexValue decor;
                    if (CallVirtual(w1.l, "getDecorView", "()Landroid/view/View;", {},
                                    &decor, "getDecorView before setContentView")) {
                        Check(decor.l != nullptr,
                              "getDecorView() is non-null before any setContentView"
                              " (the WindowCompat chain)");
                        // And it must be usable, not merely non-null: the next call in
                        // that chain is getSystemUiVisibility on it.
                        if (decor.l != nullptr) {
                            DexValue vis;
                            const bool ok = CallVirtual(decor.l, "getSystemUiVisibility",
                                                        "()I", {}, &vis,
                                                        "decorView.getSystemUiVisibility");
                            Check(ok, "the full WindowCompat chain runs without throwing");
                        }
                    }
                }
            }
        }
    }

    // ── InputFilter.LengthFilter ──
    //
    // androidx's gametextinput caps the length of its editor with
    // `new InputFilter.LengthFilter(n)`, three frames inside
    // GameActivity.createSurfaceView. InputFilter was an empty interface with no
    // nested classes, so that resolved to an auto-stub and threw
    // NoClassDefFoundError — Minecraft's onCreate died there.
    {
        DexClass* filter = linker.FindClass("Landroid/text/InputFilter;");
        Check(filter != nullptr && filter->IsInterface(), "InputFilter is an interface");

        DexClass* len = linker.FindClass("Landroid/text/InputFilter$LengthFilter;");
        Check(len != nullptr && !len->is_stub, "InputFilter$LengthFilter is real");
        if (len != nullptr && !len->is_stub) {
            Check(len->FindDirectMethod("<init>", "(I)V") != nullptr,
                  "LengthFilter(int) exists (how androidx constructs it)");
            Check(len->IsSubClassOf(filter), "LengthFilter implements InputFilter");
        }

        // The arithmetic is the part that matters: room left is the cap minus what
        // stays in the buffer, and what stays excludes the range being replaced.
        // Ignoring the replaced range rejects valid edits near the limit, because
        // overwriting a selection does not add to the length.
        DexObject* lf = NewObject("Landroid/text/InputFilter$LengthFilter;", "(I)V",
                                  {DexValue::Int(5)}, "new LengthFilter(5)");
        DexObject* dest = NewObject("Landroid/text/SpannableStringBuilder;",
                                    "(Ljava/lang/CharSequence;)V", {Str("abc")},
                                    "dest buffer \"abc\"");
        if (lf != nullptr && dest != nullptr) {
            const char* kSig =
                "(Ljava/lang/CharSequence;IILandroid/text/Spanned;II)"
                "Ljava/lang/CharSequence;";
            DexValue r;

            // "abc" + "xy" = 5, exactly the cap: accepted unchanged, so null.
            if (CallVirtual(lf, "filter", kSig,
                            {Str("xy"), DexValue::Int(0), DexValue::Int(2),
                             DexValue::Ref(dest), DexValue::Int(3), DexValue::Int(3)},
                            &r, "filter: exact fit")) {
                Check(r.l == nullptr, "an insertion that exactly fits is accepted (null)");
            }

            // "abc" + "xyz" = 6 > 5: truncated to what fits.
            if (CallVirtual(lf, "filter", kSig,
                            {Str("xyz"), DexValue::Int(0), DexValue::Int(3),
                             DexValue::Ref(dest), DexValue::Int(3), DexValue::Int(3)},
                            &r, "filter: partial fit")) {
                DexValue str;
                if (r.l != nullptr &&
                    CallVirtual(r.l, "toString", "()Ljava/lang/String;", {}, &str,
                                "toString of truncated text")) {
                    Check(std::strcmp(Utf8Of(str), "xy") == 0,
                          std::string("an over-long insertion is truncated to \"xy\","
                                      " got \"") + Utf8Of(str) + "\"");
                }
            }

            // Replacing all of "abc" leaves the whole cap free, so 5 chars fit even
            // though the buffer was already 3 long. This is the case that breaks if
            // the replaced range is ignored.
            if (CallVirtual(lf, "filter", kSig,
                            {Str("vwxyz"), DexValue::Int(0), DexValue::Int(5),
                             DexValue::Ref(dest), DexValue::Int(0), DexValue::Int(3)},
                            &r, "filter: replacing the whole buffer")) {
                Check(r.l == nullptr,
                      "replacing the selection frees its length (not counted twice)");
            }
        }
    }

    // ── manifest meta-data reaches the app ──
    //
    // The whole point of the AXML meta-data parser is that a component can read its
    // own manifest entry. AGDK's GameActivity does exactly this inside onCreate:
    //
    //   getPackageManager().getActivityInfo(getIntent().getComponent(), GET_META_DATA)
    //       .metaData.getString("android.app.lib_name")
    //
    // to learn which .so holds its renderer. Every link in that chain used to be
    // broken: getIntent() returned a fresh empty Intent, getComponent() returned a
    // String instead of a ComponentName, and getActivityInfo did not exist so it was
    // auto-stubbed to null — the NullPointerException at GameActivity.java:317.
    {
        DexClass* spm = linker.FindClass("Landroid/content/pm/SystemPackageManager;");
        Check(spm != nullptr && !spm->is_stub, "SystemPackageManager is real");

        DexClass* pm = linker.FindClass("Landroid/content/pm/PackageManager;");
        if (pm != nullptr && interp.EnsureInitialized(pm)) {
            const DexField* f = pm->FindStaticField("GET_META_DATA", "I");
            Check(f != nullptr, "PackageManager.GET_META_DATA exists");
            if (f != nullptr) {
                Check(pm->static_values[f->offset_or_slot].i == 128,
                      "GET_META_DATA is 128, as app code passes it");
            }
        }

        // Register what a manifest would have declared, the way the native launcher
        // does before starting the app.
        if (spm != nullptr && interp.EnsureInitialized(spm)) {
            DexClass* strArr = linker.FindClass("[Ljava/lang/String;");
            const auto makeArray = [&](std::vector<const char*> items) -> DexValue {
                auto* arr = linker.AllocArray(strArr, static_cast<int32_t>(items.size()));
                for (size_t i = 0; i < items.size(); ++i) {
                    arr->Set<DexObject*>(static_cast<int32_t>(i),
                                         reinterpret_cast<DexObject*>(
                                             linker.NewString(items[i])));
                }
                return DexValue::Ref(reinterpret_cast<DexObject*>(arr));
            };

            DexValue ignored;
            CallStatic("Landroid/content/pm/SystemPackageManager;", "registerPackage",
                       "(Ljava/lang/String;[Ljava/lang/String;)V",
                       {Str("com.example.probe"),
                        makeArray({"com.example.probe.MainActivity"})},
                       &ignored, "registerPackage");

            // Activity-scoped, which is where AGDK looks.
            CallStatic("Landroid/content/pm/SystemPackageManager;",
                       "registerComponentMetaData",
                       "(Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)V",
                       {Str("com.example.probe.MainActivity"),
                        makeArray({"android.app.lib_name"}),
                        makeArray({"libprobe"})},
                       &ignored, "registerComponentMetaData(activity)");

            // Application-scoped, the fallback for a component that declared none.
            CallStatic("Landroid/content/pm/SystemPackageManager;",
                       "registerComponentMetaData",
                       "(Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)V",
                       {Str(""), makeArray({"com.example.APP_KEY"}),
                        makeArray({"app-value"})},
                       &ignored, "registerComponentMetaData(application)");
        }

        // Now walk the chain an app walks, object by object.
        DexObject* mgr = NewObject("Landroid/content/pm/SystemPackageManager;", "()V", {},
                                   "new SystemPackageManager");
        DexObject* component = NewObject("Landroid/content/ComponentName;",
                                         "(Ljava/lang/String;Ljava/lang/String;)V",
                                         {Str("com.example.probe"),
                                          Str("com.example.probe.MainActivity")},
                                         "new ComponentName");
        if (mgr != nullptr && component != nullptr) {
            DexValue ai;
            if (CallVirtual(mgr, "getActivityInfo",
                            "(Landroid/content/ComponentName;I)Landroid/content/pm/ActivityInfo;",
                            {DexValue::Ref(component), DexValue::Int(128)}, &ai,
                            "getActivityInfo")) {
                Check(ai.l != nullptr, "getActivityInfo returns an ActivityInfo");
                if (ai.l != nullptr) {
                    // metaData must be a real Bundle. It being null is what threw:
                    // `ai.metaData.getString(...)` is an iget on null.
                    DexClass* aiCls = linker.ClassOfObject(ai.l);
                    const DexField* fMeta =
                        aiCls != nullptr
                            ? aiCls->FindInstanceField("metaData", "Landroid/os/Bundle;")
                            : nullptr;
                    Check(fMeta != nullptr, "ActivityInfo.metaData field exists");
                    if (fMeta != nullptr) {
                        DexObject* bundle = ai.l->GetField<DexObject*>(fMeta->offset_or_slot);
                        Check(bundle != nullptr, "ActivityInfo.metaData is not null");
                        if (bundle != nullptr) {
                            DexValue lib;
                            if (CallVirtual(bundle, "getString",
                                            "(Ljava/lang/String;)Ljava/lang/String;",
                                            {Str("android.app.lib_name")}, &lib,
                                            "metaData.getString(lib_name)")) {
                                Check(lib.l != nullptr &&
                                          std::strcmp(Utf8Of(lib), "libprobe") == 0,
                                      std::string("the AGDK chain yields the library"
                                                  " name, got \"") + Utf8Of(lib) + "\"");
                            }
                        }
                    }
                }
            }

            // An unknown component must throw NameNotFoundException rather than hand
            // back a blank ActivityInfo: a wrong component name should surface here,
            // not later when something reads a field off the blank.
            DexObject* unknown = NewObject("Landroid/content/ComponentName;",
                                           "(Ljava/lang/String;Ljava/lang/String;)V",
                                           {Str("com.example.probe"), Str("")},
                                           "ComponentName with empty class");
            if (unknown != nullptr) {
                interp.ClearPendingException();
                DexMethod* m = mgr->clazz->FindVirtualMethod(
                    "getActivityInfo",
                    "(Landroid/content/ComponentName;I)Landroid/content/pm/ActivityInfo;");
                if (m != nullptr) {
                    std::vector<DexValue> args = {DexValue::Ref(mgr),
                                                  DexValue::Ref(unknown),
                                                  DexValue::Int(128)};
                    interp.Execute(m, args.data(), args.size());
                    Check(interp.HasPendingException(),
                          "a component with no class name throws NameNotFoundException");
                    interp.ClearPendingException();
                }
            }
        }

        // The other half of the chain: an Activity's own Intent has to name it.
        DexObject* activity = NewObject("Landroid/app/Activity;", "()V", {},
                                        "new Activity for getIntent");
        if (activity != nullptr) {
            DexValue intent;
            if (CallVirtual(activity, "getIntent", "()Landroid/content/Intent;", {},
                            &intent, "getIntent")) {
                Check(intent.l != nullptr, "getIntent() is not null");
                if (intent.l != nullptr) {
                    DexValue again;
                    if (CallVirtual(activity, "getIntent", "()Landroid/content/Intent;",
                                    {}, &again, "getIntent #2")) {
                        // A fresh Intent per call is what made the component always
                        // null; apps also compare the instance across calls.
                        Check(again.l == intent.l,
                              "getIntent() returns the same Intent every time");
                    }
                    DexValue comp;
                    if (CallVirtual(intent.l, "getComponent",
                                    "()Landroid/content/ComponentName;", {}, &comp,
                                    "Intent.getComponent")) {
                        Check(comp.l != nullptr,
                              "getIntent().getComponent() is a ComponentName, not null");
                        if (comp.l != nullptr) {
                            DexValue cls;
                            if (CallVirtual(comp.l, "getClassName",
                                            "()Ljava/lang/String;", {}, &cls,
                                            "ComponentName.getClassName")) {
                                Check(cls.l != nullptr &&
                                          std::strcmp(Utf8Of(cls),
                                                      "android.app.Activity") == 0,
                                      std::string("the component names the Activity's"
                                                  " own class, got \"") +
                                          Utf8Of(cls) + "\"");
                            }
                        }
                    }
                }
            }
        }
    }

    // ── the stubs GameActivity called on the way through onCreate ──
    //
    // Each was being auto-stubbed, which means it silently did nothing. Individually
    // small; collectively they are the difference between an activity that configures
    // its window and one that half-configures it.
    {
        DexObject* view = NewObject("Landroid/view/View;", "(Landroid/content/Context;)V",
                                    {DexValue::Ref(nullptr)}, "new View for stubs");
        if (view != nullptr) {
            DexValue r;
            // An unkeyed tag has to round-trip: getTag() returned a hard null before,
            // so an app keeping its state there lost it.
            if (CallVirtual(view, "setTag", "(Ljava/lang/Object;)V",
                            {Str("payload")}, &r, "setTag(Object)") &&
                CallVirtual(view, "getTag", "()Ljava/lang/Object;", {}, &r, "getTag")) {
                Check(r.l != nullptr && std::strcmp(Utf8Of(r), "payload") == 0,
                      "View tag round-trips");
            }
            // The keyed form is what libraries use to stash per-view state without
            // subclassing; androidx.core keeps insets bookkeeping this way.
            if (CallVirtual(view, "setTag", "(ILjava/lang/Object;)V",
                            {DexValue::Int(0x7f0a0001), Str("keyed")}, &r,
                            "setTag(int, Object)") &&
                CallVirtual(view, "getTag", "(I)Ljava/lang/Object;",
                            {DexValue::Int(0x7f0a0001)}, &r, "getTag(int)")) {
                Check(r.l != nullptr && std::strcmp(Utf8Of(r), "keyed") == 0,
                      "keyed View tag round-trips");
            }
            if (CallVirtual(view, "getTag", "(I)Ljava/lang/Object;",
                            {DexValue::Int(0x7f0a0002)}, &r, "getTag(unset key)")) {
                Check(r.l == nullptr, "an unset keyed tag is null");
            }
        }

        // generateViewId must stay below 0x7f000000, where aapt allocates resource
        // ids, or a generated id could collide with a real R.id constant.
        DexValue id1, id2;
        if (CallStatic("Landroid/view/View;", "generateViewId", "()I", {}, &id1,
                       "generateViewId #1") &&
            CallStatic("Landroid/view/View;", "generateViewId", "()I", {}, &id2,
                       "generateViewId #2")) {
            Check(id1.i != id2.i, "generateViewId returns distinct ids");
            Check(id1.i > 0 && id1.i < 0x00FFFFFF && id2.i < 0x00FFFFFF,
                  "generated ids stay below the aapt resource-id range");
        }

        // Window format and soft-input mode: KuDroid cannot act on either (the surface
        // format is fixed by the host, keyboard geometry is the host's business) but
        // both must survive a round trip, because apps read them back to decide
        // whether they already configured the window.
        DexObject* win = NewObject("Landroid/view/Window;", "(Landroid/content/Context;)V",
                                   {DexValue::Ref(nullptr)}, "new Window");
        if (win != nullptr) {
            DexValue r;
            if (CallVirtual(win, "setFormat", "(I)V", {DexValue::Int(1)}, &r,
                            "setFormat") &&
                CallVirtual(win, "getFormat", "()I", {}, &r, "getFormat")) {
                Check(r.i == 1, "Window format round-trips");
            }
            if (CallVirtual(win, "setSoftInputMode", "(I)V", {DexValue::Int(0x10)}, &r,
                            "setSoftInputMode") &&
                CallVirtual(win, "getSoftInputMode", "()I", {}, &r,
                            "getSoftInputMode")) {
                Check(r.i == 0x10, "Window soft-input mode round-trips");
            }
        }

        // Editable.setFilters is on the interface because libraries call it on an
        // Editable they were handed, not on a concrete type. Auto-stubbing it dropped
        // the cap, so a length-limited field accepted unlimited text.
        DexClass* editable = linker.FindClass("Landroid/text/Editable;");
        if (editable != nullptr) {
            Check(editable->FindVirtualMethod("setFilters",
                                              "([Landroid/text/InputFilter;)V") != nullptr,
                  "Editable.setFilters is declared on the interface");
        }
        DexObject* buf = NewObject("Landroid/text/SpannableStringBuilder;",
                                   "(Ljava/lang/CharSequence;)V", {Str("")},
                                   "buffer for setFilters");
        DexObject* lf2 = NewObject("Landroid/text/InputFilter$LengthFilter;", "(I)V",
                                   {DexValue::Int(4)}, "LengthFilter(4)");
        if (buf != nullptr && lf2 != nullptr) {
            DexClass* filterArrCls = linker.FindClass("[Landroid/text/InputFilter;");
            if (filterArrCls != nullptr) {
                auto* arr = linker.AllocArray(filterArrCls, 1);
                arr->Set<DexObject*>(0, lf2);
                DexValue r;
                if (CallVirtual(buf, "setFilters", "([Landroid/text/InputFilter;)V",
                                {DexValue::Ref(reinterpret_cast<DexObject*>(arr))}, &r,
                                "setFilters")) {
                    // The filter has to actually run on an insertion, not merely be
                    // stored: appending past the cap is the case a dropped filter
                    // lets through.
                    if (CallVirtual(buf, "append",
                                    "(Ljava/lang/CharSequence;)Landroid/text/Editable;",
                                    {Str("abcdefgh")}, &r, "append past the cap")) {
                        DexValue str;
                        if (CallVirtual(buf, "toString", "()Ljava/lang/String;", {},
                                        &str, "toString after filtered append")) {
                            Check(std::strcmp(Utf8Of(str), "abcd") == 0,
                                  std::string("an installed LengthFilter truncates an"
                                              " append, got \"") + Utf8Of(str) + "\"");
                        }
                    }
                }
            }
        }
    }

    std::printf("  executed %llu instructions\n",
                static_cast<unsigned long long>(interp.instructions_executed()));

    if (g_failures == 0) {
        std::printf("=== KuART framework integration test PASSED ===\n");
        return 0;
    }
    std::printf("=== KuART framework integration test FAILED (%d errors) ===\n",
                g_failures);
    return 1;
}
