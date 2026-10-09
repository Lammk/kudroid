#include "kudroid/ResourceTable.h"
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/platform/AssetShim.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>

#include <unistd.h>

using kudroid::kuart::DexArray;
using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexField;
using kudroid::kuart::DexJniEnv;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexObject;
using kudroid::kuart::DexString;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

namespace {

int g_checks = 0;
int g_failures = 0;
DexClassLinker* g_linker = nullptr;
Interpreter* g_interp = nullptr;

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
        g_interp->ClearPendingException();
        return false;
    }
    if (result != nullptr) *result = value;
    return true;
}

bool CallVirtualExpectException(DexObject* receiver, const char* name, const char* signature,
                                const std::vector<DexValue>& args) {
    if (receiver == nullptr || receiver->clazz == nullptr) return false;
    DexMethod* method = receiver->clazz->FindVirtualMethod(name, signature);
    if (method == nullptr) method = receiver->clazz->FindDirectMethod(name, signature);
    if (method == nullptr) return false;
    std::vector<DexValue> full{DexValue::Ref(receiver)};
    full.insert(full.end(), args.begin(), args.end());
    g_interp->ClearPendingException();
    g_interp->Execute(method, full.data(), full.size());
    const bool threw = g_interp->HasPendingException();
    g_interp->ClearPendingException();
    return threw;
}

void U8(std::vector<uint8_t>* out, uint8_t value) { out->push_back(value); }
void U16(std::vector<uint8_t>* out, uint16_t value) {
    U8(out, static_cast<uint8_t>(value));
    U8(out, static_cast<uint8_t>(value >> 8));
}
void U32(std::vector<uint8_t>* out, uint32_t value) {
    U16(out, static_cast<uint16_t>(value));
    U16(out, static_cast<uint16_t>(value >> 16));
}
void Patch16(std::vector<uint8_t>* out, size_t offset, uint16_t value) {
    (*out)[offset] = static_cast<uint8_t>(value);
    (*out)[offset + 1] = static_cast<uint8_t>(value >> 8);
}
void Patch32(std::vector<uint8_t>* out, size_t offset, uint32_t value) {
    Patch16(out, offset, static_cast<uint16_t>(value));
    Patch16(out, offset + 2, static_cast<uint16_t>(value >> 16));
}

std::vector<uint8_t> StringPool(const std::vector<std::string>& strings) {
    std::vector<uint8_t> out;
    U16(&out, 0x0001);  // RES_STRING_POOL_TYPE
    U16(&out, 28);
    U32(&out, 0);
    U32(&out, static_cast<uint32_t>(strings.size()));
    U32(&out, 0);      // styleCount
    U32(&out, 0x00000100);  // UTF-8 strings
    U32(&out, 0);      // stringsStart, patched below
    U32(&out, 0);      // stylesStart

    std::vector<uint32_t> offsets;
    std::vector<uint8_t> data;
    for (const std::string& value : strings) {
        offsets.push_back(static_cast<uint32_t>(data.size()));
        U8(&data, static_cast<uint8_t>(value.size()));  // UTF-16 length (fixture is ASCII)
        U8(&data, static_cast<uint8_t>(value.size()));  // UTF-8 byte length
        data.insert(data.end(), value.begin(), value.end());
        U8(&data, 0);
    }
    const uint32_t stringsStart = static_cast<uint32_t>(28 + strings.size() * 4);
    Patch32(&out, 20, stringsStart);
    for (uint32_t offset : offsets) U32(&out, offset);
    out.insert(out.end(), data.begin(), data.end());
    while ((out.size() & 3u) != 0) U8(&out, 0);
    Patch32(&out, 4, static_cast<uint32_t>(out.size()));
    return out;
}

std::vector<uint8_t> TypeChunk(uint8_t typeId, const std::vector<uint32_t>& keyIndices,
                               const std::vector<std::pair<uint8_t, uint32_t>>& values,
                               const std::string& language = "") {
    const uint32_t configSize = language.empty() ? 4 : 12;
    const uint16_t headerSize = static_cast<uint16_t>(20 + configSize);
    std::vector<uint8_t> out;
    U16(&out, 0x0201);  // RES_TABLE_TYPE_TYPE
    U16(&out, headerSize);
    U32(&out, 0);
    U8(&out, typeId);
    U8(&out, 0);  // flags
    U16(&out, 0);
    U32(&out, static_cast<uint32_t>(values.size()));
    U32(&out, static_cast<uint32_t>(headerSize + values.size() * 4));
    U32(&out, configSize);
    if (!language.empty()) {
        U16(&out, 0);  // mcc
        U16(&out, 0);  // mnc
        U8(&out, static_cast<uint8_t>(language[0]));
        U8(&out, static_cast<uint8_t>(language[1]));
        U8(&out, 0);   // country
        U8(&out, 0);
    }
    for (uint32_t i = 0; i < values.size(); ++i) U32(&out, i * 16);
    for (uint32_t i = 0; i < values.size(); ++i) {
        U16(&out, 8);  // ResTable_entry.size
        U16(&out, 0);  // flags
        U32(&out, keyIndices[i]);
        U16(&out, 8);  // Res_value.size
        U8(&out, 0);   // res0
        U8(&out, values[i].first);
        U32(&out, values[i].second);
    }
    Patch32(&out, 4, static_cast<uint32_t>(out.size()));
    return out;
}

std::vector<uint8_t> ResourceTableFixture() {
    const std::vector<uint8_t> globalStrings =
            StringPool({"Hello <KuDroid>", "Hello %s", "Hello from English"});
    const std::vector<uint8_t> typeStrings = StringPool({"string", "color"});
    const std::vector<uint8_t> keyStrings = StringPool(
            {"welcome", "welcome_alias", "accent", "welcome_format", "accent_rgb4"});
    const std::vector<uint8_t> strings = TypeChunk(
            1, {0, 1, 3}, {{0x03, 0}, {0x01, 0x7f010000}, {0x03, 1}});
    const std::vector<uint8_t> localizedStrings = TypeChunk(1, {0}, {{0x03, 2}}, "en");
    const std::vector<uint8_t> colors = TypeChunk(2, {2, 4},
                                                   {{0x1c, 0xff336699}, {0x1f, 0x00000f8a}});

    std::vector<uint8_t> package;
    U16(&package, 0x0200);  // RES_TABLE_PACKAGE_TYPE
    U16(&package, 288);
    U32(&package, 0);
    U32(&package, 0x7f);
    for (size_t i = 0; i < 128; ++i) {
        const uint16_t ch = i < 20 ? static_cast<uint8_t>("com.example.fixture"[i]) : 0;
        U16(&package, ch);
    }
    const uint32_t typeStringsOffset = 288;
    const uint32_t keyStringsOffset = typeStringsOffset + static_cast<uint32_t>(typeStrings.size());
    U32(&package, typeStringsOffset);
    U32(&package, 0);  // lastPublicType
    U32(&package, keyStringsOffset);
    U32(&package, 0);  // lastPublicKey
    U32(&package, 0);  // typeIdOffset
    package.insert(package.end(), typeStrings.begin(), typeStrings.end());
    package.insert(package.end(), keyStrings.begin(), keyStrings.end());
    package.insert(package.end(), strings.begin(), strings.end());
    package.insert(package.end(), localizedStrings.begin(), localizedStrings.end());
    package.insert(package.end(), colors.begin(), colors.end());
    Patch32(&package, 4, static_cast<uint32_t>(package.size()));

    std::vector<uint8_t> table;
    U16(&table, 0x0002);  // RES_TABLE_TYPE
    U16(&table, 12);
    U32(&table, 0);
    U32(&table, 1);  // packageCount
    table.insert(table.end(), globalStrings.begin(), globalStrings.end());
    table.insert(table.end(), package.begin(), package.end());
    Patch32(&table, 4, static_cast<uint32_t>(table.size()));
    return table;
}

std::vector<uint8_t> StoredZipEntry(const std::string& name, const std::vector<uint8_t>& payload) {
    const uint32_t crc = static_cast<uint32_t>(crc32(0L, payload.data(),
                                                     static_cast<uInt>(payload.size())));
    const uint32_t size = static_cast<uint32_t>(payload.size());
    std::vector<uint8_t> zip;
    U32(&zip, 0x04034b50); U16(&zip, 20); U16(&zip, 0); U16(&zip, 0);
    U16(&zip, 0); U16(&zip, 0); U32(&zip, crc); U32(&zip, size); U32(&zip, size);
    U16(&zip, static_cast<uint16_t>(name.size())); U16(&zip, 0);
    zip.insert(zip.end(), name.begin(), name.end());
    zip.insert(zip.end(), payload.begin(), payload.end());
    const uint32_t centralOffset = static_cast<uint32_t>(zip.size());
    U32(&zip, 0x02014b50); U16(&zip, 20); U16(&zip, 20); U16(&zip, 0); U16(&zip, 0);
    U16(&zip, 0); U16(&zip, 0); U32(&zip, crc); U32(&zip, size); U32(&zip, size);
    U16(&zip, static_cast<uint16_t>(name.size())); U16(&zip, 0); U16(&zip, 0);
    U16(&zip, 0); U16(&zip, 0); U32(&zip, 0); U32(&zip, 0);
    zip.insert(zip.end(), name.begin(), name.end());
    const uint32_t centralSize = static_cast<uint32_t>(zip.size()) - centralOffset;
    U32(&zip, 0x06054b50); U16(&zip, 0); U16(&zip, 0); U16(&zip, 1); U16(&zip, 1);
    U32(&zip, centralSize); U32(&zip, centralOffset); U16(&zip, 0);
    return zip;
}

}  // namespace

int main() {
    const auto appDir = std::filesystem::temp_directory_path() / "kuart-resource-table-test";
    std::filesystem::remove_all(appDir);
    std::filesystem::create_directories(appDir / "assets");
    const std::vector<uint8_t> fixture = ResourceTableFixture();
    {
        std::ofstream out(appDir / "resources.arsc", std::ios::binary);
        out.write(reinterpret_cast<const char*>(fixture.data()),
                  static_cast<std::streamsize>(fixture.size()));
    }
    const std::string assetsDir = (appDir / "assets").string();
    kudroid_set_assets_dir(assetsDir.c_str());

    uint32_t id = 0;
    Check(kudroid::resource_get_identifier("welcome", "string", "com.example.fixture", &id) &&
              id == 0x7f010000,
          "lookup resource identifier by package, type, and name");
    std::string value;
    Check(kudroid::resource_get_string(id, "", &value) && value == "Hello <KuDroid>",
          "resolve the default UTF-8 string value");
    Check(kudroid::resource_get_string(id, "en_US", &value) &&
              value == "Hello from English",
          "select a language-qualified resource when it matches");
    Check(kudroid::resource_get_string(id, "fr_CA", &value) && value == "Hello <KuDroid>",
          "fall back to the unqualified resource when locale does not match");
    uint32_t aliasId = 0;
    Check(kudroid::resource_get_identifier("welcome_alias", "string", "com.example.fixture",
                                           &aliasId) &&
              kudroid::resource_get_string(aliasId, "", &value) &&
              value == "Hello <KuDroid>",
          "follow a string resource reference");
    uint32_t colorId = 0;
    uint32_t color = 0;
    Check(kudroid::resource_get_identifier("accent", "color", "com.example.fixture", &colorId) &&
              kudroid::resource_get_color(colorId, "en_US", &color) && color == 0xff336699,
          "resolve an ARGB color value");
    uint32_t rgb4Id = 0;
    Check(kudroid::resource_get_identifier("accent_rgb4", "color", "com.example.fixture",
                                            &rgb4Id) &&
              kudroid::resource_get_color(rgb4Id, "", &color) && color == 0xffff88aa,
          "expand a packed RGB4 color into ARGB");
    Check(!kudroid::resource_get_identifier("missing", "string", "com.example.fixture", &id),
          "return false for a missing resource name");
    Check(!kudroid::resource_get_string(0x7f7f0000, "en_US", &value),
          "return false for a missing resource ID");

    const auto apkAppDir = std::filesystem::temp_directory_path() / "kuart-resource-apk-test";
    std::filesystem::remove_all(apkAppDir);
    std::filesystem::create_directories(apkAppDir / "assets");
    const std::vector<uint8_t> zip = StoredZipEntry("resources.arsc", fixture);
    {
        std::ofstream apk(apkAppDir / "base.apk", std::ios::binary);
        apk.write(reinterpret_cast<const char*>(zip.data()),
                  static_cast<std::streamsize>(zip.size()));
    }
    const std::string apkAssetsDir = (apkAppDir / "assets").string();
    kudroid_set_assets_dir(apkAssetsDir.c_str());
    char* resolvedPath = nullptr;
    int64_t resolvedOffset = 0, resolvedLength = 0;
    const int resolveResult = kudroid_package_resolve_bytes(
            "resources.arsc", &resolvedPath, &resolvedOffset, &resolvedLength);
    Check(resolveResult == 1 && resolvedPath != nullptr &&
              std::filesystem::path(resolvedPath) == (apkAppDir / "base.apk") &&
              resolvedOffset > 0 && resolvedLength == static_cast<int64_t>(fixture.size()),
          "resolve an exact stored root entry from base.apk");
    if (resolvedPath != nullptr) {
        std::ifstream resolved(resolvedPath, std::ios::binary);
        resolved.seekg(resolvedOffset, std::ios::beg);
        std::vector<uint8_t> bytes(static_cast<size_t>(resolvedLength));
        resolved.read(reinterpret_cast<char*>(bytes.data()), resolvedLength);
        Check(bytes == fixture, "root APK entry bytes are bounded to resources.arsc");
        std::free(resolvedPath);
    }
    uint32_t apkId = 0;
    Check(kudroid::resource_get_identifier("welcome", "string", "com.example.fixture", &apkId) &&
              kudroid::resource_get_string(apkId, "", &value) && value == "Hello <KuDroid>",
          "load resources.arsc from the APK root when no loose table exists");
    kudroid_set_assets_dir(assetsDir.c_str());
    std::filesystem::remove_all(apkAppDir);

    std::printf("-- real framework Resources API --\n");
    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex", &error)) {
        std::printf("  FAIL AddDexFile(framework.dex): %s\n", error.c_str());
        ++g_checks;
        ++g_failures;
    } else {
        Interpreter interp(&linker);
        DexJniEnv jni(&linker, &interp);
        interp.set_jni_env(&jni);
        interp.set_instruction_limit(2000ull * 1000ull * 1000ull);
        g_linker = &linker;
        g_interp = &interp;

        DexObject* context = NewObject("Landroid/app/ApplicationContext;", "(Ljava/lang/String;)V",
                                       {Str("com.example.fixture")});
        DexValue resourcesValue;
        const bool gotResources = CallVirtual(context, "getResources",
                "()Landroid/content/res/Resources;", {}, &resourcesValue);
        DexObject* resources = gotResources ? resourcesValue.l : nullptr;
        DexValue contextAssets, resourceAssets;
        const bool sameAssets = CallVirtual(context, "getAssets",
                "()Landroid/content/res/AssetManager;", {}, &contextAssets) &&
                CallVirtual(resources, "getAssets", "()Landroid/content/res/AssetManager;",
                            {}, &resourceAssets) && contextAssets.l == resourceAssets.l;
        Check(sameAssets, "ApplicationContext Resources shares its AssetManager instance");

        DexValue javaId;
        const bool idOk = CallVirtual(resources, "getIdentifier",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
                {Str("welcome"), Str("string"), Str("com.example.fixture")}, &javaId);
        Check(idOk && javaId.i == 0x7f010000, "Java getIdentifier returns the compiled resource ID");
        DexValue javaString;
        const bool stringOk = CallVirtual(resources, "getString", "(I)Ljava/lang/String;",
                                           {DexValue::Int(0x7f010000)}, &javaString);
        Check(stringOk && std::strcmp(Utf8(javaString), "Hello from English") == 0,
              "Java getString selects the resource matching Configuration.locale");
        DexValue javaText;
        const bool textOk = CallVirtual(resources, "getText", "(I)Ljava/lang/CharSequence;",
                                        {DexValue::Int(0x7f010000)}, &javaText);
        Check(textOk && std::strcmp(Utf8(javaText), "Hello from English") == 0,
              "Java getText returns the resolved resource");
        DexValue textDefault;
        const bool defaultTextOk = CallVirtual(resources, "getText",
                "(ILjava/lang/CharSequence;)Ljava/lang/CharSequence;",
                {DexValue::Int(0x7f7f0000), Str("fallback")}, &textDefault);
        Check(defaultTextOk && std::strcmp(Utf8(textDefault), "fallback") == 0,
              "Java getText returns its default for a missing resource");
        DexValue javaColor;
        const bool colorOk = CallVirtual(resources, "getColor", "(I)I",
                                         {DexValue::Int(static_cast<int32_t>(colorId))}, &javaColor);
        Check(colorOk && static_cast<uint32_t>(javaColor.i) == 0xff336699,
              "Java getColor returns the ARGB resource");
        DexValue missingId;
        const bool missingIdOk = CallVirtual(resources, "getIdentifier",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
                {Str("absent"), Str("string"), Str("com.example.fixture")}, &missingId);
        Check(missingIdOk && missingId.i == 0, "Java getIdentifier returns zero for a miss");
        Check(CallVirtualExpectException(resources, "getString", "(I)Ljava/lang/String;",
                                         {DexValue::Int(0x7f7f0000)}),
              "Java getString throws NotFoundException for a missing ID");
        Check(CallVirtualExpectException(resources, "getColor", "(I)I",
                                         {DexValue::Int(0x7f7f0000)}),
              "Java getColor throws NotFoundException for a missing ID");

        DexValue formattedId;
        const bool formatIdOk = CallVirtual(resources, "getIdentifier",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
                {Str("welcome_format"), Str("string"), Str("com.example.fixture")},
                &formattedId);
        DexClass* objectArrayClass = linker.FindClass("[Ljava/lang/Object;");
        DexArray* formatArgs = objectArrayClass != nullptr
                ? linker.AllocArray(objectArrayClass, 1) : nullptr;
        if (formatArgs != nullptr) formatArgs->Set<DexObject*>(
                0, reinterpret_cast<DexObject*>(linker.NewString("KuDroid")));
        DexValue formatted;
        const bool formatOk = formatIdOk && formatArgs != nullptr &&
                CallVirtual(resources, "getString", "(I[Ljava/lang/Object;)Ljava/lang/String;",
                            {formattedId, DexValue::Ref(reinterpret_cast<DexObject*>(formatArgs))},
                            &formatted);
        Check(formatOk && std::strcmp(Utf8(formatted), "Hello KuDroid") == 0,
              "formatted getString uses the resource locale and arguments");

        DexObject* metrics = NewObject("Landroid/util/DisplayMetrics;", "()V", {});
        DexObject* display = NewObject("Landroid/view/Display;", "()V", {});
        const bool metricsOk = CallVirtual(display, "getMetrics",
                "(Landroid/util/DisplayMetrics;)V", {DexValue::Ref(metrics)}, nullptr);
        DexField* densityField = metrics != nullptr
                ? metrics->clazz->FindInstanceField("density", "F") : nullptr;
        DexField* dpiField = metrics != nullptr
                ? metrics->clazz->FindInstanceField("densityDpi", "I") : nullptr;
        const float density = densityField != nullptr ? metrics->GetField<float>(densityField->offset_or_slot) : 0;
        const int32_t densityDpi = dpiField != nullptr ? metrics->GetField<int32_t>(dpiField->offset_or_slot) : 0;
        Check(metricsOk && density > 0.0f && densityDpi > 0,
              "DisplayMetrics reports a positive host density and densityDpi");
    }

    std::filesystem::remove_all(appDir);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
