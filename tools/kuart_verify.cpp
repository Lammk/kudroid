// Report every class, method and field a guest APK references that KuART cannot resolve.
//
// The point is to stop discovering missing framework API one device round-trip at a time.
// A DEX file's method_ids/field_ids tables list everything its code can possibly reference,
// so the whole list is available on a developer machine before any bytecode runs — and
// across a corpus of APKs, sorted by how many of them need each entry, it says what to
// implement next and in what order.
//
// Built as a binary against the real DexClassLinker rather than written as a script that
// parses DEX itself. A separate parser would drift from what KuART actually does, and the
// question being asked is precisely "what does KuART fail to resolve", not "what does some
// model of KuART fail to resolve".
//
// What this CANNOT see, and why it is not the whole answer:
//
//   1. Names resolved from native code. AGDK's initializeNativeCode calls
//      GetMethodID("getDeviceId", "()I") on MotionEvent; that string lives in the .so, not
//      in any DEX table. Twenty-five such methods were missing at once and none of them
//      appear here. Use --jni-scan on the .so for that half.
//   2. API that exists but is never initialised. ALooper_forThread returned null because
//      nothing called ALooper_prepare — no symbol is missing, so nothing static can find
//      it. That class of bug still needs a device.
//
// Usage:
//   kuart_verify <extracted-app-dir> [more dirs...]     one line per unresolved entry
//   kuart_verify --json <dir>                           machine-readable, for aggregation
//   kuart_verify --jni-scan <lib.so>                    candidate JNI names in a library
//
// The directory is one an APK has already been extracted into (classes*.dex at the top
// level), which is what APKExtractor produces.

#include "kudroid/KuArtRuntime.h"
#include "kudroid/kuart/DexClass.h"
#include "kudroid/kuart/DexClassLinker.h"

#include "dex/class_accessor-inl.h"
#include "dex/dex_file.h"
#include "dex/dex_file_loader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

// The live linker inside the runtime. Declared here rather than in KuArtRuntime.h because
// nothing in the app should reach inside the runtime this way; only this tool needs it, and
// it needs the real one so its answers match the device's.
extern "C" kudroid::kuart::DexClassLinker* kuart_debug_linker(void);

namespace {

using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;

// What is wrong with one reference, in the terms that decide what to do about it.
enum class Missing {
    kClassAbsent,      // the declaring class does not exist at all
    kClassStubbed,     // the class was auto-stubbed: present, but empty
    kMethodAbsent,     // class is real, this method is not
    kFieldAbsent,      // class is real, this field is not
};

const char* MissingKindName(Missing kind) {
    switch (kind) {
        case Missing::kClassAbsent:  return "CLASS_ABSENT";
        case Missing::kClassStubbed: return "CLASS_STUBBED";
        case Missing::kMethodAbsent: return "METHOD_ABSENT";
        case Missing::kFieldAbsent:  return "FIELD_ABSENT";
    }
    return "?";
}

struct Finding {
    Missing kind;
    std::string owner;      // declaring class, dotted
    std::string member;     // method or field name, empty for a class-level finding
    std::string signature;  // method signature or field type descriptor
};

// Only the boot classpath is KuDroid's responsibility. An app's own classes are in its DEX
// and resolve on their own; a missing one means the APK was extracted incompletely, which
// is a different problem and would drown out the signal being looked for here.
//
// Kept in step with isBootClasspathDescriptor() in DexClassLinker.cpp: anything the linker
// will auto-stub is something KuDroid is expected to ship.
bool IsBootClasspath(const char* descriptor) {
    if (descriptor == nullptr || descriptor[0] != 'L') return false;
    static const char* kPrefixes[] = {
        "Landroid/", "Landroidx/", "Ljava/", "Ljavax/", "Ldalvik/", "Lsun/",
        "Llibcore/", "Lcom/android/", "Lorg/apache/harmony/", "Lorg/w3c/dom/",
        "Lorg/xml/sax/", "Lorg/xmlpull/", "Lorg/json/",
    };
    for (const char* prefix : kPrefixes) {
        if (std::strncmp(descriptor, prefix, std::strlen(prefix)) == 0) return true;
    }
    return false;
}

std::string Dotted(const char* descriptor) {
    if (descriptor == nullptr) return "?";
    std::string s(descriptor);
    if (s.size() > 2 && s.front() == 'L' && s.back() == ';') s = s.substr(1, s.size() - 2);
    std::replace(s.begin(), s.end(), '/', '.');
    return s;
}

// Resolve one method reference the way the interpreter would, and say what failed.
//
// Both the direct and the virtual table are consulted because a reference does not record
// which kind it is; the interpreter picks by opcode, and either hit means the method exists.
bool CheckMethod(DexClassLinker& linker, const art::DexFile& dex,
                 const art::dex::MethodId& id, Finding* out) {
    const char* owner = dex.GetMethodDeclaringClassDescriptor(id);
    if (!IsBootClasspath(owner)) return true;

    const char* name = dex.GetMethodName(id);
    const std::string signature = dex.GetMethodSignature(id).ToString();

    // FindClass synthesises a stub for any boot-classpath descriptor it does not have, so a
    // non-null return is not evidence the class exists. Reading is_stub is what separates
    // "present" from "invented so the app could keep running" — without it every lookup
    // here would appear to succeed and the tool would report nothing.
    DexClass* klass = linker.FindClass(owner);
    if (klass == nullptr) {
        *out = {Missing::kClassAbsent, Dotted(owner), "", ""};
        return false;
    }
    if (klass->is_stub) {
        *out = {Missing::kClassStubbed, Dotted(owner), "", ""};
        return false;
    }

    if (klass->FindDirectMethod(name, signature.c_str()) != nullptr) return true;
    if (klass->FindVirtualMethod(name, signature.c_str()) != nullptr) return true;

    *out = {Missing::kMethodAbsent, Dotted(owner), name != nullptr ? name : "?", signature};
    return false;
}

bool CheckField(DexClassLinker& linker, const art::DexFile& dex,
                const art::dex::FieldId& id, Finding* out) {
    const char* owner = dex.GetFieldDeclaringClassDescriptor(id);
    if (!IsBootClasspath(owner)) return true;

    const char* name = dex.GetFieldName(id);
    const char* type = dex.GetFieldTypeDescriptor(id);

    DexClass* klass = linker.FindClass(owner);
    if (klass == nullptr) {
        *out = {Missing::kClassAbsent, Dotted(owner), "", ""};
        return false;
    }
    if (klass->is_stub) {
        *out = {Missing::kClassStubbed, Dotted(owner), "", ""};
        return false;
    }

    if (klass->FindInstanceField(name, type) != nullptr) return true;
    if (klass->FindStaticField(name, type) != nullptr) return true;

    *out = {Missing::kFieldAbsent, Dotted(owner), name != nullptr ? name : "?",
            type != nullptr ? type : "?"};
    return false;
}

// One key per distinct finding, so the same missing method reported from twenty call sites
// counts once. Aggregating across APKs is the whole point: the number that matters is how
// many APPS need an entry, not how often one app mentions it.
std::string FindingKey(const Finding& f) {
    std::string key = MissingKindName(f.kind);
    key += ' ';
    key += f.owner;
    if (!f.member.empty()) {
        key += "->";
        key += f.member;
        key += f.signature;
    }
    return key;
}

struct AppResult {
    std::string name;
    size_t methodRefs = 0;
    size_t fieldRefs = 0;
    size_t methodRefsOk = 0;
    size_t fieldRefsOk = 0;
    std::map<std::string, Finding> findings;  // keyed to deduplicate within one app
};

bool VerifyApp(const std::filesystem::path& dir, AppResult* result) {
    result->name = dir.filename().string();

    // kuart_init loads the embedded framework.dex first, then every classes*.dex in the
    // directory — the same order and the same linker the runtime uses, which is what makes
    // the answer here match the answer on device.
    if (!kuart_init(dir.string().c_str())) {
        std::fprintf(stderr, "kuart_init failed for %s: %s\n", dir.string().c_str(),
                     kuart_last_error());
        return false;
    }

    DexClassLinker* linker = kuart_debug_linker();
    if (linker == nullptr) {
        std::fprintf(stderr, "no linker after kuart_init\n");
        return false;
    }

    // Skip index 0: that is the embedded framework, and checking KuDroid's own code against
    // itself would only report gaps between framework classes, not what the app needs.
    for (size_t i = 1; i < linker->NumDexFiles(); ++i) {
        const art::DexFile* dex = linker->DexFileAt(i);
        if (dex == nullptr) continue;

        for (size_t m = 0; m < dex->NumMethodIds(); ++m) {
            ++result->methodRefs;
            Finding finding;
            if (CheckMethod(*linker, *dex, dex->GetMethodId(m), &finding)) {
                ++result->methodRefsOk;
                continue;
            }
            result->findings.emplace(FindingKey(finding), finding);
        }
        for (size_t f = 0; f < dex->NumFieldIds(); ++f) {
            ++result->fieldRefs;
            Finding finding;
            if (CheckField(*linker, *dex, dex->GetFieldId(f), &finding)) {
                ++result->fieldRefsOk;
                continue;
            }
            result->findings.emplace(FindingKey(finding), finding);
        }
    }
    return true;
}

void PrintReport(const std::vector<AppResult>& results) {
    // How many apps need each finding. This ordering is the deliverable: it turns a few
    // thousand unresolved references into a list whose first entries unblock the most apps.
    std::map<std::string, size_t> appCount;
    std::map<std::string, Finding> byKey;
    for (const AppResult& app : results) {
        for (const auto& [key, finding] : app.findings) {
            ++appCount[key];
            byKey.emplace(key, finding);
        }
    }

    std::vector<std::pair<std::string, size_t>> ranked(appCount.begin(), appCount.end());
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;  // stable, so output can be diffed between runs
    });

    size_t totalRefs = 0, totalOk = 0;
    for (const AppResult& app : results) {
        totalRefs += app.methodRefs + app.fieldRefs;
        totalOk += app.methodRefsOk + app.fieldRefsOk;
    }

    std::printf("=== KuART static verification ===\n");
    for (const AppResult& app : results) {
        const size_t refs = app.methodRefs + app.fieldRefs;
        const size_t ok = app.methodRefsOk + app.fieldRefsOk;
        std::printf("  %-40s %6zu/%-6zu refs resolved (%.1f%%), %zu distinct gaps\n",
                    app.name.c_str(), ok, refs,
                    refs != 0 ? 100.0 * static_cast<double>(ok) / static_cast<double>(refs) : 100.0,
                    app.findings.size());
    }
    std::printf("\n  %zu apps, %zu/%zu references resolved (%.2f%%), %zu distinct gaps\n\n",
                results.size(), totalOk, totalRefs,
                totalRefs != 0 ? 100.0 * static_cast<double>(totalOk) / static_cast<double>(totalRefs) : 100.0,
                ranked.size());

    std::printf("--- gaps, most-needed first ---\n");
    for (const auto& [key, count] : ranked) {
        const Finding& f = byKey[key];
        std::printf("  [%zu app%s] %-14s %s", count, count == 1 ? "" : "s",
                    MissingKindName(f.kind), f.owner.c_str());
        if (!f.member.empty()) std::printf("->%s%s", f.member.c_str(), f.signature.c_str());
        std::printf("\n");
    }
}

void PrintJson(const std::vector<AppResult>& results) {
    const auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        return out;
    };
    std::printf("{\n  \"apps\": [\n");
    for (size_t i = 0; i < results.size(); ++i) {
        const AppResult& app = results[i];
        std::printf("    {\"name\": \"%s\", \"refs\": %zu, \"resolved\": %zu, \"gaps\": [\n",
                    escape(app.name).c_str(), app.methodRefs + app.fieldRefs,
                    app.methodRefsOk + app.fieldRefsOk);
        size_t n = 0;
        for (const auto& [key, f] : app.findings) {
            std::printf("      {\"kind\": \"%s\", \"owner\": \"%s\", \"member\": \"%s\", \"signature\": \"%s\"}%s\n",
                        MissingKindName(f.kind), escape(f.owner).c_str(),
                        escape(f.member).c_str(), escape(f.signature).c_str(),
                        ++n == app.findings.size() ? "" : ",");
        }
        std::printf("    ]}%s\n", i + 1 == results.size() ? "" : ",");
    }
    std::printf("  ]\n}\n");
}

// Names in a .so that look like JNI lookups, for the half no DEX table can show.
//
// GetMethodID("getDeviceId", "()I") leaves two strings next to each other in .rodata, and a
// signature is recognisable on sight: "()I", "(II)F", "(Landroid/view/MotionEvent;)V". What
// is NOT recoverable is which class each one belongs to — that is a register holding a
// jclass at run time, not anything in the file.
//
// So this reports candidates, not conclusions: a signature-shaped string plus the
// identifiers near it. It is a list to read, and it is how the twenty-five MotionEvent and
// KeyEvent methods would have been found in one pass instead of one device run.
void ScanJniNames(const std::filesystem::path& soPath) {
    std::ifstream file(soPath, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "cannot open %s\n", soPath.string().c_str());
        return;
    }
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    const auto printable = [](unsigned char c) {
        return c >= 0x20 && c < 0x7f;
    };

    // Collect NUL-terminated printable runs, keeping their offsets so adjacency can be
    // used: a JNI call site almost always stores the name immediately before the signature.
    struct Str { size_t offset; std::string text; };
    std::vector<Str> strings;
    size_t i = 0;
    while (i < data.size()) {
        if (!printable(static_cast<unsigned char>(data[i]))) { ++i; continue; }
        const size_t start = i;
        while (i < data.size() && printable(static_cast<unsigned char>(data[i]))) ++i;
        if (i < data.size() && data[i] == '\0' && i - start >= 2 && i - start < 256) {
            strings.push_back({start, data.substr(start, i - start)});
        }
        ++i;
    }

    // A JNI signature: "(" args ")" return, where every character is one of the type
    // letters. Checked rather than pattern-matched loosely, because ordinary text with
    // parentheses is common in a binary and would bury the result.
    const auto isSignature = [](const std::string& s) {
        if (s.size() < 3 || s[0] != '(') return false;
        const size_t close = s.find(')');
        if (close == std::string::npos || close + 1 >= s.size()) return false;
        for (size_t k = 1; k < s.size(); ++k) {
            const char c = s[k];
            if (c == ')' || c == '[' || c == ';' || c == '/' || c == '$' || c == '_') continue;
            if (std::strchr("ZBCSIJFDVL", c) != nullptr) continue;
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) continue;
            return false;
        }
        return true;
    };
    const auto isIdentifier = [](const std::string& s) {
        if (s.empty()) return false;
        if (!((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z') ||
              s[0] == '_' || s[0] == '<')) {
            return false;
        }
        for (char c : s) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '_' || c == '$' || c == '<' || c == '>') {
                continue;
            }
            return false;
        }
        return true;
    };

    std::printf("=== JNI name candidates in %s ===\n", soPath.filename().string().c_str());
    std::printf("Read these as three lists, not as resolved calls: which class owns which\n"
                "method is a register holding a jclass at run time, not anything in the file.\n\n");

    std::set<std::string> classNames;
    for (const Str& s : strings) {
        // FindClass takes a slashed internal name, which no compiler emits by accident.
        if (s.text.find('/') != std::string::npos && s.text.find(' ') == std::string::npos &&
            s.text.find('.') == std::string::npos && s.text.size() > 5 &&
            (s.text.rfind("android/", 0) == 0 || s.text.rfind("androidx/", 0) == 0 ||
             s.text.rfind("java/", 0) == 0 || s.text.rfind("com/", 0) == 0)) {
            classNames.insert(s.text);
        }
    }
    std::printf("--- classes looked up by name (%zu) ---\n", classNames.size());
    for (const std::string& c : classNames) std::printf("  %s\n", c.c_str());

    // Names and signatures are listed separately rather than paired.
    //
    // Pairing by adjacency looks obvious and is wrong: the linker pools identical strings,
    // so a signature shared by two methods ("()I" for both getDeviceId and getUnicodeChar)
    // is emitted once and lands nowhere near either name. Tried on a library with six known
    // call sites it produced "getDeviceId(II)F" — a pair that does not exist. Reporting the
    // two lists is less satisfying and does not invent facts.
    std::set<std::string> signatures;
    std::set<std::string> identifiers;
    for (const Str& s : strings) {
        if (isSignature(s.text)) {
            signatures.insert(s.text);
        } else if (isIdentifier(s.text) && s.text.size() >= 3) {
            identifiers.insert(s.text);
        }
    }

    std::printf("\n--- JNI signatures present (%zu) ---\n", signatures.size());
    for (const std::string& s : signatures) std::printf("  %s\n", s.c_str());

    std::printf("\n--- identifiers that could be method names (%zu) ---\n", identifiers.size());
    for (const std::string& s : identifiers) std::printf("  %s\n", s.c_str());

    if (signatures.empty()) {
        std::printf("\n(no JNI signatures found: the library may build them at run time,\n"
                    " or resolve natives through RegisterNatives instead)\n");
    }
}

void Usage() {
    std::fprintf(stderr,
        "usage: kuart_verify [--json] <extracted-app-dir> [more dirs...]\n"
        "       kuart_verify --jni-scan <library.so>\n"
        "\n"
        "Reports boot-classpath classes, methods and fields a guest APK references that\n"
        "KuART cannot resolve. Across several apps, gaps are ranked by how many need them.\n"
        "\n"
        "--jni-scan lists names a native library looks up by string, which no DEX table\n"
        "records; that is where AGDK's MotionEvent/KeyEvent requirements come from.\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        Usage();
        return 2;
    }

    bool json = false;
    std::vector<std::filesystem::path> dirs;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--json") {
            json = true;
        } else if (arg == "--jni-scan") {
            if (i + 1 >= argc) { Usage(); return 2; }
            ScanJniNames(argv[i + 1]);
            return 0;
        } else if (arg == "-h" || arg == "--help") {
            Usage();
            return 0;
        } else {
            dirs.emplace_back(arg);
        }
    }
    if (dirs.empty()) {
        Usage();
        return 2;
    }

    // Nothing should reach the missing-class log: this tool reports, it does not want the
    // side effect of recording gaps as though an app had hit them at run time.
    kuart_set_missing_class_log_path("");

    std::vector<AppResult> results;
    for (const std::filesystem::path& dir : dirs) {
        if (!std::filesystem::is_directory(dir)) {
            std::fprintf(stderr, "not a directory: %s\n", dir.string().c_str());
            continue;
        }
        AppResult result;
        if (VerifyApp(dir, &result)) results.push_back(std::move(result));
    }
    if (results.empty()) return 1;

    if (json) {
        PrintJson(results);
    } else {
        PrintReport(results);
    }
    return 0;
}
