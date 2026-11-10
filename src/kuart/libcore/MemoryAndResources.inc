bool Invoke_java_lang_Runtime(Interpreter* interp, const char* name, const DexValue* args,
                              size_t num_args, DexValue* /*result*/) {
    const bool by_name = std::strcmp(name, "loadLibrary") == 0;
    if (!by_name && std::strcmp(name, "load") != 0) return false;

    const char* arg = num_args > 1 ? GetStringUtf8(args[1]) : "";
    if (arg == nullptr || arg[0] == '\0') {
        interp->ThrowException("Ljava/lang/UnsatisfiedLinkError;", "empty library name");
        return true;
    }
    // Copy the name: dlopen/JNI_OnLoad runs arbitrary native init that can block on I/O
    // or re-enter the interpreter, and the guest string must not be relied on across it.
    const std::string lib_name(arg);
    bool loaded = true;
    if (g_load_lib_cb != nullptr) {
        // Drop the VM lock: loading a library can take arbitrarily long and its
        // JNI_OnLoad may call back into Java (which needs to take the lock itself).
        VmLockRelease unlocked;
        loaded = g_load_lib_cb(lib_name.c_str());
    }
    if (!loaded) {
        if (!interp->HasPendingException()) {
            interp->ThrowException("Ljava/lang/UnsatisfiedLinkError;",
                                   std::string("Couldn't load ") + lib_name);
        }
        return true;
    }
    return true;
}

// dalvik.system.BaseDexClassLoader
bool Invoke_dalvik_system_BaseDexClassLoader(Interpreter* interp, const char* name,
                                            const DexValue* args, size_t num_args,
                                            DexValue* result) {
    if (std::strcmp(name, "findLibraryPath") != 0) return false;

    // Static method, so args[0] is the library name rather than a receiver.
    const char* lib = num_args > 0 ? GetStringUtf8(args[0]) : "";
    result->l = nullptr;
    if (lib == nullptr || lib[0] == '\0') return true;

    // Large buffer for long iOS container paths.
    char path[2048];
    if (kudroid_find_native_library(lib, path, sizeof(path)) != 0) {
        result->l = reinterpret_cast<DexObject*>(interp->linker()->NewString(path));
    }
    return true;
}

bool Invoke_dalvik_system_InMemoryDexClassLoader(Interpreter* interp, const char* name,
                                                const DexValue* args, size_t num_args,
                                                DexValue* result) {
    if (std::strcmp(name, "registerDexBuffers") != 0) return false;
    result->l = nullptr;
    auto* arrays = num_args > 0 ? reinterpret_cast<DexArray*>(args[0].l) : nullptr;
    if (arrays == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;", "null DEX buffers");
        return true;
    }
    try {
        std::vector<std::vector<uint8_t>> buffers;
        buffers.reserve(arrays->length);
        for (int32_t i = 0; i < arrays->length; ++i) {
            auto* bytes = reinterpret_cast<DexArray*>(arrays->Get<DexObject*>(i));
            if (bytes == nullptr) {
                interp->ThrowException("Ljava/lang/NullPointerException;", "null DEX buffer");
                return true;
            }
            buffers.emplace_back(bytes->Data(), bytes->Data() + bytes->length);
        }
        std::string error;
        if (!interp->linker()->AddOwnedDexFiles(std::move(buffers), &error)) {
            result->l = interp->linker()->NewString(error);
            if (result->l == nullptr)
                interp->ThrowException("Ljava/lang/OutOfMemoryError;", "DEX error allocation failed");
        }
    } catch (const std::bad_alloc&) {
        interp->ThrowException("Ljava/lang/OutOfMemoryError;", "DEX buffer allocation failed");
    }
    return true;
}

}  // namespace

void LibCoreSetLoadLibraryCallback(LoadLibraryCallback cb) {
    g_load_lib_cb = cb;
}
namespace {

// Unsafe offsets are measured from the object base, while DexObject fields are
// stored after the object header. Arrays keep their elements inline after the
// DexArray header. Resolve both layouts so desugared JDK collections can use
// Unsafe without silently losing writes.
bool UnsafeAddress(DexObject* obj, uint64_t offset, size_t width, uint8_t** addr) {
    if (obj == nullptr || obj->clazz == nullptr || addr == nullptr) return false;
    auto* base = reinterpret_cast<uint8_t*>(obj);
    if (obj->clazz->is_array) {
        auto* arr = reinterpret_cast<DexArray*>(obj);
        const uint32_t elem = DexClassLinker::ElementSize(arr->clazz->component_type);
        if (elem == 0 || offset < sizeof(DexArray)) return false;
        const uint64_t rel = offset - sizeof(DexArray);
        const uint64_t total = static_cast<uint64_t>(arr->length) * elem;
        if (rel > total || width > total - rel) return false;
        *addr = base + offset;
        return true;
    }
    if (offset < sizeof(DexObject)) return false;
    const uint64_t end = sizeof(DexObject) + static_cast<uint64_t>(obj->clazz->object_size);
    if (offset > end || width > end - offset) return false;
    *addr = base + offset;
    return true;
}

}  // namespace

bool Invoke_sun_misc_Unsafe(Interpreter* interp, const char* name, const DexValue* args,
                            size_t num_args, DexValue* result) {
    if (std::strcmp(name, "objectFieldOffset") == 0) {
        DexField* field = num_args > 1 && args[1].l != nullptr ? FieldFromObject(args[1].l)
                                                              : nullptr;
        if (field == nullptr) {
            *result = DexValue::Long(0);
            return true;
        }
        const uint64_t offset = field->IsStatic()
                                    ? field->offset_or_slot
                                    : sizeof(DexObject) + static_cast<uint64_t>(field->offset_or_slot);
        *result = DexValue::Long(static_cast<int64_t>(offset));
        return true;
    }
    if (std::strcmp(name, "arrayBaseOffset") == 0) {
        *result = DexValue::Int(static_cast<int32_t>(sizeof(DexArray)));
        return true;
    }
    if (std::strcmp(name, "arrayIndexScale") == 0) {
        DexClass* klass = num_args > 1 ? ClassOf(interp, args[1].l) : nullptr;
        uint32_t scale = 1;
        if (klass != nullptr && klass->is_array && klass->component_type != nullptr) {
            scale = DexClassLinker::ElementSize(klass->component_type);
        }
        *result = DexValue::Int(static_cast<int32_t>(scale > 0 ? scale : 1));
        return true;
    }
    if (std::strcmp(name, "compareAndSwapInt") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        int32_t expected = num_args > 3 ? args[3].i : 0;
        int32_t new_val = num_args > 4 ? args[4].i : 0;
        uint8_t* addr = nullptr;
        if (!UnsafeAddress(obj, offset, sizeof(int32_t), &addr)) {
            *result = DexValue::Int(0);
            return true;
        }
        auto* target = reinterpret_cast<int32_t*>(addr);
        bool success = __atomic_compare_exchange_n(target, &expected, new_val, false,
                                                   __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        *result = DexValue::Int(success ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "compareAndSwapLong") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        int64_t expected = num_args > 3 ? args[3].j : 0;
        int64_t new_val = num_args > 4 ? args[4].j : 0;
        uint8_t* addr = nullptr;
        if (!UnsafeAddress(obj, offset, sizeof(int64_t), &addr)) {
            *result = DexValue::Int(0);
            return true;
        }
        auto* target = reinterpret_cast<int64_t*>(addr);
        bool success = __atomic_compare_exchange_n(target, &expected, new_val, false,
                                                   __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        *result = DexValue::Int(success ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "compareAndSwapObject") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        DexObject* expected = num_args > 3 ? args[3].l : nullptr;
        DexObject* new_val = num_args > 4 ? args[4].l : nullptr;
        uint8_t* addr = nullptr;
        if (!UnsafeAddress(obj, offset, sizeof(DexObject*), &addr)) {
            *result = DexValue::Int(0);
            return true;
        }
        auto* target = reinterpret_cast<DexObject**>(addr);
        bool success = __atomic_compare_exchange_n(target, &expected, new_val, false,
                                                   __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        *result = DexValue::Int(success ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "getInt") == 0 || std::strcmp(name, "getIntVolatile") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        uint8_t* addr = nullptr;
        int32_t val = 0;
        if (UnsafeAddress(obj, offset, sizeof(val), &addr)) {
            std::memcpy(&val, addr, sizeof(val));
        }
        *result = DexValue::Int(val);
        return true;
    }
    if (std::strcmp(name, "putInt") == 0 || std::strcmp(name, "putIntVolatile") == 0 ||
        std::strcmp(name, "putOrderedInt") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        int32_t val = num_args > 3 ? args[3].i : 0;
        uint8_t* addr = nullptr;
        if (UnsafeAddress(obj, offset, sizeof(val), &addr)) {
            std::memcpy(addr, &val, sizeof(val));
        }
        return true;
    }
    if (std::strcmp(name, "getLong") == 0 || std::strcmp(name, "getLongVolatile") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        uint8_t* addr = nullptr;
        int64_t val = 0;
        if (UnsafeAddress(obj, offset, sizeof(val), &addr)) {
            std::memcpy(&val, addr, sizeof(val));
        }
        *result = DexValue::Long(val);
        return true;
    }
    if (std::strcmp(name, "putLong") == 0 || std::strcmp(name, "putLongVolatile") == 0 ||
        std::strcmp(name, "putOrderedLong") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        int64_t val = num_args > 3 ? args[3].j : 0;
        uint8_t* addr = nullptr;
        if (UnsafeAddress(obj, offset, sizeof(val), &addr)) {
            std::memcpy(addr, &val, sizeof(val));
        }
        return true;
    }
    if (std::strcmp(name, "getObject") == 0 || std::strcmp(name, "getObjectVolatile") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        uint8_t* addr = nullptr;
        DexObject* val = nullptr;
        if (UnsafeAddress(obj, offset, sizeof(DexObject*), &addr)) {
            std::memcpy(&val, addr, sizeof(val));
        }
        *result = DexValue::Ref(val);
        return true;
    }
    if (std::strcmp(name, "putObject") == 0 || std::strcmp(name, "putObjectVolatile") == 0 ||
        std::strcmp(name, "putOrderedObject") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        DexObject* val = num_args > 3 ? args[3].l : nullptr;
        uint8_t* addr = nullptr;
        if (UnsafeAddress(obj, offset, sizeof(val), &addr)) {
            std::memcpy(addr, &val, sizeof(val));
        }
        return true;
    }
    if (std::strcmp(name, "park") == 0) {
        ParkThread(interp, args[1].i != 0, args[2].j, nullptr, false);
        return true;
    }
    if (std::strcmp(name, "unpark") == 0) {
        UnparkThread(interp, args[1].l);
        return true;
    }
    return false;
}

// TimeZone default from the host UTC offset; no zoneinfo on device.
bool Invoke_java_util_TimeZone(Interpreter* interp, const char* name,
                               const DexValue* /*args*/, size_t /*num_args*/,
                               DexValue* result) {
    if (std::strcmp(name, "native_getDefaultRawOffset") == 0) {
        const std::time_t now = std::time(nullptr);
        std::tm local_tm{};
        std::tm utc_tm{};
#if defined(_WIN32)
        localtime_s(&local_tm, &now);
        gmtime_s(&utc_tm, &now);
#else
        localtime_r(&now, &local_tm);
        gmtime_r(&now, &utc_tm);
#endif
        // Diff via mktime so rollover is handled.
        local_tm.tm_isdst = 0;
        utc_tm.tm_isdst = 0;
        const double diff = std::difftime(std::mktime(&local_tm), std::mktime(&utc_tm));
        *result = DexValue::Int(static_cast<int32_t>(diff * 1000.0));
        return true;
    }
    if (std::strcmp(name, "native_getDefaultId") == 0) {
        const std::time_t now = std::time(nullptr);
        std::tm local_tm{};
#if defined(_WIN32)
        localtime_s(&local_tm, &now);
#else
        localtime_r(&now, &local_tm);
#endif
        char zone[64] = {0};
        if (std::strftime(zone, sizeof(zone), "%Z", &local_tm) == 0 || zone[0] == '\0') {
            std::snprintf(zone, sizeof(zone), "UTC");
        }
        // NewString copies into the KuART heap, which owns the result.
        result->l = (interp != nullptr && interp->linker() != nullptr)
                        ? reinterpret_cast<DexObject*>(interp->linker()->NewString(zone))
                        : nullptr;
        return true;
    }
    return false;
}

bool Invoke_java_nio_DirectByteBuffer(Interpreter* interp, const char* name, const DexValue* args,
                                          size_t num_args, DexValue* result) {
    if (std::strcmp(name, "nAllocate") == 0) {
        int32_t capacity = num_args > 0 ? args[0].i : 0;
        if (capacity < 0) capacity = 0;
        // Zero-filled like a fresh direct buffer; at least 1 byte so the
        // address is always usable (allocateDirect(0) edge case).
        const size_t size = static_cast<size_t>(capacity > 0 ? capacity : 1);
        void* mem = std::calloc(1, size);
        if (mem == nullptr) {
            if (interp != nullptr) {
                interp->ThrowException("Ljava/lang/OutOfMemoryError;", "DirectByteBuffer malloc failed");
            }
            *result = DexValue::Long(0);
            return true;
        }
        *result = DexValue::Long(static_cast<int64_t>(reinterpret_cast<uintptr_t>(mem)));
        return true;
    }
    if (std::strcmp(name, "nGet") == 0) {
        auto* mem = reinterpret_cast<const uint8_t*>(
            static_cast<uintptr_t>(num_args > 0 ? args[0].j : 0));
        const int32_t index = num_args > 1 ? args[1].i : 0;
        // Bounds are enforced on the Java side; a null address here means the
        // allocation failed and threw, so just avoid faulting the host.
        *result = DexValue::Int(mem != nullptr ? static_cast<int8_t>(mem[index]) : 0);
        return true;
    }
    if (std::strcmp(name, "nPut") == 0) {
        auto* mem = reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(num_args > 0 ? args[0].j : 0));
        const int32_t index = num_args > 1 ? args[1].i : 0;
        if (mem != nullptr) mem[index] = static_cast<uint8_t>(num_args > 2 ? args[2].i : 0);
        return true;
    }
    if (std::strcmp(name, "nGetArray") == 0 || std::strcmp(name, "nPutArray") == 0) {
        auto* mem = reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(num_args > 0 ? args[0].j : 0));
        const int32_t index = num_args > 1 ? args[1].i : 0;
        auto* arr = num_args > 2 ? reinterpret_cast<DexArray*>(args[2].l) : nullptr;
        const int32_t offset = num_args > 3 ? args[3].i : 0;
        const int32_t length = num_args > 4 ? args[4].i : 0;
        if (mem != nullptr && arr != nullptr && offset >= 0 && length >= 0 && index >= 0 &&
            static_cast<int64_t>(offset) + length <= arr->length) {
            if (std::strcmp(name, "nGetArray") == 0) {
                std::memcpy(arr->Data() + offset, mem + index, static_cast<size_t>(length));
            } else {
                std::memcpy(mem + index, arr->Data() + offset, static_cast<size_t>(length));
            }
        }
        return true;
    }
    return false;
}

extern "C" const char* kudroid_get_assets_dir(void);
// Resolve an asset the way the AAssetManager shim does: loose file, archive entry
// extracted to cache (deflated) or mmap-backed from base.apk (stored). Returns the
// backing file plus its in-file offset and length. Defined in AssetShim.cpp.
extern "C" int kudroid_asset_resolve_bytes(const char* filename, char** outPath,
                                           int64_t* outStart, int64_t* outLength);
extern "C" int kudroid_package_resolve_bytes(const char* entry, char** outPath,
                                               int64_t* outOffset, int64_t* outLength);

bool Invoke_android_provider_Settings(Interpreter* interp, const char* name,
                                      const DexValue* /*args*/, size_t /*num_args*/,
                                      DexValue* result) {
    if (std::strcmp(name, "nativeGetAndroidId") == 0) {
        const std::string id =
            VFSPathRemapper::getInstance().android_id();
        result->l = (interp != nullptr && interp->linker() != nullptr)
                        ? reinterpret_cast<DexObject*>(interp->linker()->NewString(id.c_str()))
                        : nullptr;
        return true;
    }
    return false;
}

bool Invoke_android_content_res_AssetManager(Interpreter* interp, const char* name,
                                             const DexValue* args, size_t num_args,
                                             DexValue* result) {
    if (std::strcmp(name, "nativeGetAssetsDir") == 0) {
        const std::string dir = kudroid::kudroid_get_assets_dir_cpp();
        result->l = (interp != nullptr && interp->linker() != nullptr)
                        ? reinterpret_cast<DexObject*>(interp->linker()->NewString(dir.c_str()))
                        : nullptr;
        return true;
    }
    // Return the backing path, offset and length for an asset or package entry.
    if (std::strcmp(name, "nativeResolveAsset") == 0 ||
        std::strcmp(name, "nativeResolvePackageResource") == 0) {
        if (interp == nullptr || interp->linker() == nullptr || num_args < 1 ||
            args[0].l == nullptr) {
            result->l = nullptr;
            return true;
        }
        auto* str = reinterpret_cast<DexString*>(args[0].l);
        const char* filename = str->utf8 ? str->utf8 : "";
        char* path = nullptr;
        int64_t start = 0, length = 0;
        const int rc = std::strcmp(name, "nativeResolvePackageResource") == 0
                ? kudroid_package_resolve_bytes(filename, &path, &start, &length)
                : kudroid_asset_resolve_bytes(filename, &path, &start, &length);
        if (rc <= 0 || path == nullptr) {
            result->l = nullptr;
            return true;
        }
        DexClassLinker* linker = interp->linker();
        DexObject* path_str = reinterpret_cast<DexObject*>(linker->NewString(path));
        std::free(path);
        // Longs are boxed because the result array carries (path, start, length) and
        // Java generic arrays cannot hold primitives.
        DexObject* start_box = BoxValue(interp, "J", DexValue::Long(start));
        DexObject* length_box = BoxValue(interp, "J", DexValue::Long(length));
        if (path_str == nullptr || start_box == nullptr || length_box == nullptr) {
            result->l = nullptr;
            return true;
        }
        DexArray* arr = NewRefArray(linker, "[Ljava/lang/Object;",
                                    {path_str, start_box, length_box});
        result->l = arr ? reinterpret_cast<DexObject*>(arr) : nullptr;
        return true;
    }
    return false;
}

bool Invoke_VectorDrawable(Interpreter* interp, const char* name, const DexValue* args,
                           size_t num_args, DexValue* result) {
    if (std::strcmp(name, "nativeRasterize") != 0 || num_args < 7) return false;
    result->l = nullptr;
    auto* paths = reinterpret_cast<DexArray*>(args[0].l);
    auto* commands = reinterpret_cast<DexArray*>(args[1].l);
    auto* colors = reinterpret_cast<DexArray*>(args[2].l);
    const int width = args[3].i, height = args[4].i;
    const int64_t count = static_cast<int64_t>(width) * height;
    if (paths == nullptr || commands == nullptr || colors == nullptr ||
        width <= 0 || height <= 0 || count > 16 * 1024 * 1024 ||
        paths->length <= 0 || paths->length > 65536 ||
        static_cast<int64_t>(paths->length) * 16 != commands->length ||
        static_cast<int64_t>(paths->length) * 2 != colors->length) return true;
    std::vector<std::string> text;
    text.reserve(paths->length);
    for (int32_t i = 0; i < paths->length; ++i) {
        DexValue value; value.l = paths->Get<DexObject*>(i);
        text.emplace_back(GetStringUtf8(value));
    }
    DexClass* klass = interp->linker()->FindClass("[I");
    DexArray* output = klass ? interp->linker()->AllocArray(klass, static_cast<int32_t>(count)) : nullptr;
    if (!output) { interp->ThrowException("Ljava/lang/OutOfMemoryError;", "vector bitmap"); return true; }
    if (kudroid::rasterize_vector(text, reinterpret_cast<const float*>(commands->Data()),
            reinterpret_cast<const uint32_t*>(colors->Data()), width, height,
            args[5].f, args[6].f, reinterpret_cast<uint32_t*>(output->Data()))) result->l = output;
    return true;
}

kudroid::ResourceStyleConfig ReadResourceConfig(const std::string& locale, DexObject* config, int sdk) {
    kudroid::ResourceStyleConfig result;
    result.locale = locale;
    result.sdk = sdk;
    result.ui_mode = GetIntField(config, "uiMode", 0);
    result.width_dp = GetIntField(config, "screenWidthDp", 0);
    result.height_dp = GetIntField(config, "screenHeightDp", 0);
    result.smallest_width_dp = GetIntField(config, "smallestScreenWidthDp", 0);
    result.orientation = GetIntField(config, "orientation", 0);
    result.density = GetIntField(config, "densityDpi", 0);
    result.screen_layout = GetIntField(config, "screenLayout", 0);
    result.color_mode = GetIntField(config, "colorMode", 0);
    return result;
}

bool Invoke_android_content_res_Resources(Interpreter* interp, const char* name,
                                          const DexValue* args, size_t num_args,
                                          DexValue* result) {
    if (std::strcmp(name, "nativeGetDarkMode") == 0) {
        *result = DexValue::Int(kudroid_get_dark_mode());
        return true;
    }
    if (std::strcmp(name, "nativeReadResource") == 0 && num_args >= 1) {
        result->l = nullptr;
        char* path = nullptr; int64_t offset = 0, length = 0;
        if (kudroid_package_resolve_bytes(GetStringUtf8(args[0]), &path, &offset, &length) <= 0 ||
            path == nullptr || offset < 0 || length <= 0 || length > 16 * 1024 * 1024) {
            std::free(path); return true;
        }
        std::ifstream stream(path, std::ios::binary); std::free(path);
        if (!stream || !stream.seekg(offset, std::ios::beg)) return true;
        std::vector<char> bytes(static_cast<size_t>(length));
        if (!stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return true;
        result->l = NewByteArray(interp->linker(), bytes.data(), bytes.size());
        return true;
    }
    if (std::strcmp(name, "nativeGetStyle") == 0 && num_args >= 4) {
        const auto config = ReadResourceConfig(GetStringUtf8(args[1]), args[2].l, args[3].i);
        std::map<uint32_t, kudroid::ResourceScalar> attributes;
        if (!kudroid::resource_get_style(static_cast<uint32_t>(args[0].i), config, &attributes)) {
            result->l = nullptr;
            return true;
        }
        DexClass* klass = interp->linker()->FindClass("[I");
        DexArray* array = klass != nullptr ? interp->linker()->AllocArray(
                klass, static_cast<int32_t>(attributes.size() * 3)) : nullptr;
        if (array == nullptr) {
            interp->ThrowException("Ljava/lang/OutOfMemoryError;", "style attributes");
            return true;
        }
        int32_t index = 0;
        for (const auto& [id, value] : attributes) {
            array->Set<int32_t>(index++, static_cast<int32_t>(id));
            array->Set<int32_t>(index++, value.type);
            array->Set<int32_t>(index++, static_cast<int32_t>(value.data));
        }
        result->l = array;
        return true;
    }
    if (std::strcmp(name, "nativeGetPoolString") == 0 && num_args >= 1) {
        std::string text;
        result->l = kudroid::resource_get_pool_string(static_cast<uint32_t>(args[0].i), &text)
                ? interp->linker()->NewString(text.c_str()) : nullptr;
        return true;
    }
    if (std::strcmp(name, "nativeGetValue") == 0 && num_args >= 6) {
        kudroid::ResourceScalar value;
        DexObject* output = args[2].l;
        const auto config = ReadResourceConfig(GetStringUtf8(args[1]), args[4].l, args[5].i);
        const bool found = output != nullptr && kudroid::resource_get_value(
                static_cast<uint32_t>(args[0].i), config, args[3].i != 0, &value);
        if (found) {
            auto set = [output](const char* field, uint32_t data) {
                if (DexField* f = output->clazz->FindInstanceField(field, "I"))
                    output->SetField<int32_t>(f->offset_or_slot, static_cast<int32_t>(data));
            };
            set("type", value.type); set("data", value.data); set("resourceId", value.resource_id);
            set("assetCookie", 1);
            SetRefField(output, "string", "Ljava/lang/CharSequence;", value.type == 3
                    ? interp->linker()->NewString(value.text.c_str()) : nullptr);
        }
        *result = DexValue::Int(found ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "nativeGetIdentifier") == 0) {
        if (num_args < 3) { *result = DexValue::Int(0); return true; }
        uint32_t id = 0;
        const bool found = kudroid::resource_get_identifier(
                GetStringUtf8(args[0]), GetStringUtf8(args[1]), GetStringUtf8(args[2]), &id);
        *result = DexValue::Int(found ? static_cast<int32_t>(id) : 0);
        return true;
    }
    if (std::strcmp(name, "nativeGetResourceName") == 0) {
        // "package:type/entry", or null for an id no table knows.
        std::string package, type, entry;
        if (interp == nullptr || interp->linker() == nullptr || num_args < 1 ||
            !kudroid::resource_get_name(static_cast<uint32_t>(args[0].i), &package, &type,
                                        &entry)) {
            result->l = nullptr;
            return true;
        }
        const std::string full = package + ":" + type + "/" + entry;
        result->l = reinterpret_cast<DexObject*>(interp->linker()->NewString(full.c_str()));
        return true;
    }
    if (std::strcmp(name, "nativeGetString") == 0) {
        if (interp == nullptr || interp->linker() == nullptr || num_args < 2) {
            result->l = nullptr;
            return true;
        }
        std::string value;
        const uint32_t id = static_cast<uint32_t>(args[0].i);
        if (!kudroid::resource_get_string(id, GetStringUtf8(args[1]), &value)) {
            result->l = nullptr;
            return true;
        }
        result->l = reinterpret_cast<DexObject*>(interp->linker()->NewString(value.c_str()));
        return true;
    }
    if (std::strcmp(name, "nativeGetColor") == 0) {
        if (num_args < 2) {
            if (interp != nullptr) interp->ThrowException(
                    "Landroid/content/res/Resources$NotFoundException;", "color resource not found");
            *result = DexValue::Int(0);
            return true;
        }
        uint32_t color = 0;
        if (!kudroid::resource_get_color(static_cast<uint32_t>(args[0].i),
                                         GetStringUtf8(args[1]), &color)) {
            if (interp != nullptr) interp->ThrowException(
                    "Landroid/content/res/Resources$NotFoundException;",
                    "color resource not found or is not a color");
            *result = DexValue::Int(0);
            return true;
        }
        *result = DexValue::Int(static_cast<int32_t>(color));
        return true;
    }
    return false;
}

bool Invoke_android_view_Display(const char* name, DexValue* result) {
    if (std::strcmp(name, "nativeGetDensity") != 0) return false;
    const float density = g_metalLayerDensity;
    result->f = std::isfinite(density) && density > 0.0f ? density : 1.0f;
    return true;
}

// android.os.MessageQueue native surface. The queue blocks on a native wait
// slot (AOSP epoll stand-in) instead of Object.wait(), so a message enqueue and
// a touch injection can both wake the Looper thread without an interpreted Java
// notify. nativeDrainInput builds pending touch on that same thread.
bool Invoke_android_os_MessageQueue(Interpreter* /*interp*/, const char* name,
                                    const DexValue* args, size_t num_args,
                                    DexValue* result) {
    if (std::strcmp(name, "nativeInit") == 0) {
        result->j = kudroid_looper_init();
        return true;
    }
    if (std::strcmp(name, "nativeDestroy") == 0) {
        if (num_args >= 1) kudroid_looper_destroy(args[0].j);
        return true;
    }
    if (std::strcmp(name, "nativePollOnce") == 0) {
        if (num_args >= 2) {
            // Release the VM lock for the wait, exactly as Monitor::Wait does.
            // Otherwise an idle Looper would hold the global lock while it
            // sleeps and no other thread (render, workers) could run.
            kudroid::kuart::VmLockRelease unlocked;
            kudroid_looper_poll(args[0].j, args[1].j);
        }
        return true;
    }
    if (std::strcmp(name, "nativeWake") == 0) {
        if (num_args >= 1) kudroid_looper_wake(args[0].j);
        return true;
    }
    if (std::strcmp(name, "nativeSetMainQueue") == 0) {
        if (num_args >= 1) kudroid_looper_set_main(args[0].j);
        return true;
    }
    if (std::strcmp(name, "nativeDrainInput") == 0) {
        // Only the main queue owns touch; worker loopers must not steal it.
        if (num_args >= 1 && kudroid_looper_is_main(args[0].j)) {
            kuart_dark_mode_drain_pending();
            kuart_surface_drain_pending();
            kuart_camera_drain_pending();
            kuart_network_drain_pending();
            kuart_media_drain_pending();
            kuart_host_events_drain();
            kuart_touch_drain_pending();
        }
        return true;
    }
    return false;
}
