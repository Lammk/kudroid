namespace {

// "Lcom/foo/Bar;" -> "com.foo.Bar"; leaves anything else untouched.
std::string DescriptorToDotted(const char* descriptor) {
    if (descriptor == nullptr) return "?";
    std::string s(descriptor);
    if (s.size() > 2 && s.front() == 'L' && s.back() == ';') {
        s = s.substr(1, s.size() - 2);
    }
    std::replace(s.begin(), s.end(), '/', '.');
    return s;
}

// Render one frame like Throwable; method comes from the slot, not the frame.
std::string DescribeFrame(const Interpreter::StackSlot& slot) {
    const DexMethod* method = slot.method;
    if (method == nullptr) return "    at <null method>";

    std::string cls = method->declaring_class != nullptr
                          ? method->declaring_class->PrettyName()
                          : DescriptorToDotted(nullptr);
    const char* name = method->name != nullptr ? method->name : "?";

    const uint32_t dex_pc = slot.frame != nullptr ? slot.frame->dex_pc() : 0;

    std::string where;
    const char* source = nullptr;
    if (method->dex_file != nullptr && method->declaring_class != nullptr &&
        method->declaring_class->class_def != nullptr) {
        source = method->dex_file->GetSourceFile(*method->declaring_class->class_def);
    }

    uint32_t line = 0;
    bool have_line = false;
    if (method->dex_file != nullptr && method->code_item != nullptr) {
        art::CodeItemDebugInfoAccessor accessor(*method->dex_file, method->code_item,
                                                method->dex_method_index);
        have_line = accessor.GetLineNumForPc(dex_pc, &line);
    }

    if (source != nullptr && have_line) {
        where = std::string(source) + ":" + std::to_string(line);
    } else if (source != nullptr) {
        where = std::string(source) + ":pc=" + std::to_string(dex_pc);
    } else {
        where = "pc=" + std::to_string(dex_pc);
    }
    return "    at " + cls + "." + name + "(" + where + ")";
}

}  // namespace

Interpreter::ThreadState Interpreter::CaptureThreadState() {
    return ThreadState{depth_, call_stack_.size(), VmLockDepth()};
}

void Interpreter::RestoreThreadState(const ThreadState& state) {
    if (call_stack_.size() > state.frames) call_stack_.resize(state.frames);
    if (depth_ > state.depth) depth_ = state.depth;
    VmLockUnwindTo(state.vm_lock_depth);
}

std::string Interpreter::BuildStackTrace() const {
    std::string out;
    // Innermost frame first, matching the JVM.
    for (size_t i = call_stack_.size(); i-- > 0;) {
        out += DescribeFrame(call_stack_[i]);
        out += '\n';
    }
    return out;
}

std::string Interpreter::DescribePendingException() const {
    if (pending_exception_ == nullptr) return {};
    std::string out = pending_exception_->clazz != nullptr
                          ? pending_exception_->clazz->PrettyName()
                          : std::string("<unknown exception>");
    if (!last_error_.empty()) {
        out += " (";
        out += last_error_;
        out += ")";
    }
    if (!pending_exception_trace_.empty()) {
        out += '\n';
        out += pending_exception_trace_;
    }
    return out;
}

// Innermost Java frame's class; null when called from native with no frame.
DexClass* Interpreter::CallerClass() const {
    if (call_stack_.empty()) return nullptr;
    const DexMethod* method = call_stack_.back().method;
    return method != nullptr ? method->declaring_class : nullptr;
}

void Interpreter::ThrowException(const char* descriptor, const std::string& message) {
    // Old contract: a throw always overwrites (the JVM propagates the NEW
    // exception and suppresses the old). A preserve-first variant that kept
    // the old exception hung clinit chains at boot: callers rely on the slot
    // naming what just fired, and a cleanup throw must not be swallowed.
    last_error_ = std::string(descriptor) + ": " + message;
    pending_exception_trace_ = BuildStackTrace();

    // Record only; the owner logs caught vs uncaught later.
    std::fprintf(stderr, "[KuART][EXCEPTION] %s: %s\n", descriptor, message.c_str());
    if (std::strcmp(descriptor, "Ljava/lang/StackOverflowError;") == 0) {
        size_t end = 0;
        for (int line = 0; line < 24 && end < pending_exception_trace_.size(); ++line) {
            const size_t next = pending_exception_trace_.find('\n', end);
            end = next == std::string::npos ? pending_exception_trace_.size() : next + 1;
        }
        std::fprintf(stderr, "[KuART][STACK-OVERFLOW]\n%.*s", static_cast<int>(end),
                     pending_exception_trace_.c_str());
    }


    DexClass* klass = linker_ != nullptr ? linker_->FindClass(descriptor) : nullptr;
    if (klass != nullptr) {
        pending_exception_ = linker_->AllocObject(klass);
        if (pending_exception_ != nullptr) {
            InitNativeThrowable(pending_exception_, message.c_str());
        }
    }
    if (pending_exception_ == nullptr) {
        // No class exception in classpath (framework not loaded) -
        // You still have to stop the method, using an empty object as a flag.
        static DexObject placeholder;
        pending_exception_ = &placeholder;
    }
}

DexClass* Interpreter::ResolveClass(const DexMethod* context, uint32_t type_idx) {
    if (context == nullptr || context->dex_file == nullptr || linker_ == nullptr) return nullptr;
    // Raw dex indices: validate before use (release libdex skips DCHECKs, and
    // the uint16 truncation below would alias a large index onto an unrelated
    // type). Malformed dex resolves to null, which every caller already handles.
    if (type_idx >= context->dex_file->NumTypeIds()) return nullptr;
    const uint64_t generation = g_fastVirtualGeneration.load(std::memory_order_relaxed);
    if (t_fastTypeGeneration != generation) {
        std::fill_n(t_fastTypeCache, kFastTypeSlots, FastTypeSlot{});
        t_fastTypeGeneration = generation;
    }
    const size_t slot = ((reinterpret_cast<uintptr_t>(context->dex_file) >> 4)
            ^ (type_idx * 31u)) % kFastTypeSlots;
    const FastTypeSlot& cached = t_fastTypeCache[slot];
    if (cached.index == type_idx && cached.dex == context->dex_file
            && cached.linker == linker_) return cached.klass;
    const char* descriptor =
        context->dex_file->StringByTypeIdx(art::dex::TypeIndex(static_cast<uint16_t>(type_idx)));
    if (descriptor == nullptr) return nullptr;
    DexClass* klass = linker_->FindClass(descriptor);
    if (klass != nullptr) t_fastTypeCache[slot] = FastTypeSlot{linker_, context->dex_file, type_idx, klass};
    return klass;
}

DexField* Interpreter::ResolveField(const DexMethod* context, uint32_t field_idx,
                                    bool is_static) {
    if (context == nullptr || context->dex_file == nullptr) return nullptr;
    const art::DexFile& dex_file = *context->dex_file;
    if (field_idx >= dex_file.NumFieldIds()) return nullptr;
    // Keep resolved fields scoped to their DEX, executing class and runtime.
    const uint64_t generation = g_fastVirtualGeneration.load(std::memory_order_relaxed);
    if (t_fastFieldGeneration != generation) {
        std::fill_n(t_fastFieldCache, kFastFieldSets, FastFieldSet{});
        t_fastFieldGeneration = generation;
    }
    const size_t slot = ((reinterpret_cast<uintptr_t>(&dex_file) >> 3)
            ^ (reinterpret_cast<uintptr_t>(context->declaring_class) >> 3)
            ^ (field_idx * 31u) ^ static_cast<uint32_t>(is_static)) % kFastFieldSets;
    FastFieldSet& bucket = t_fastFieldCache[slot];
    for (const FastFieldSlot& cached : bucket.ways) {
        if (cached.index == field_idx && cached.dex == &dex_file
                && cached.owner == context->declaring_class && cached.linker == linker_
                && cached.is_static == is_static) return cached.field;
    }
    const art::dex::FieldId& field_id = dex_file.GetFieldId(field_idx);

    const char* class_descriptor = dex_file.GetFieldDeclaringClassDescriptor(field_id);
    // Prefer the class actually reached from the executing method. The global
    // descriptor map can contain a duplicate loaded by another thread; using it
    // would read a different class's static sentinel while the object was built
    // from this one.
    DexClass* klass = nullptr;
    if (context->declaring_class != nullptr) {
        for (DexClass* c = context->declaring_class; c != nullptr; c = c->superclass) {
            if (c->descriptor != nullptr && std::strcmp(c->descriptor, class_descriptor) == 0) {
                klass = c;
                break;
            }
        }
    }
    if (klass == nullptr) klass = linker_->FindClass(class_descriptor);
    if (klass == nullptr) return nullptr;

    const char* name = dex_file.GetFieldName(field_id);
    const char* type = dex_file.GetFieldTypeDescriptor(field_id);
    DexField* field = is_static ? klass->FindStaticField(name, type)
                                : klass->FindInstanceField(name, type);
    if (field == nullptr) {
        ReportMissingField(klass, name, type, is_static);
    } else {
        bucket.ways[bucket.next] = FastFieldSlot{linker_, &dex_file,
                context->declaring_class, field_idx, is_static, field};
        bucket.next = (bucket.next + 1) % kFastFieldWays;
    }
    return field;
}

// Report a missing field once; fields cannot be auto-stubbed like methods.
void Interpreter::ReportMissingField(const DexClass* klass, const char* name,
                                     const char* type, bool is_static) {
    if (klass == nullptr) return;
    const std::string detail = klass->PrettyName() + "." + (name != nullptr ? name : "?") +
                               " : " + (type != nullptr ? type : "?") +
                               (is_static ? " (static)" : "");

    static std::mutex s_mtx;
    static std::set<std::string> s_reported;
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        if (!s_reported.insert(detail).second) return;
    }

    // Distinguish the two cases, because the fix differs: a stub class needs the
    // whole class written, while a real class is merely incomplete.
    const char* kind = klass->is_stub ? "MISSING_FRAMEWORK_CLASS_FIELD"
                                      : "MISSING_FRAMEWORK_FIELD";
    std::fprintf(stderr, "[KuART][MISSING-FIELD] ⚠ %s: %s%s\n", kind, detail.c_str(),
                 klass->is_stub ? " (declaring class is an auto-stub)" : "");
    DexClassLinker::LogMissingMember(kind, detail);
}

// Describe a field reference so failures name the field.
std::string Interpreter::DescribeFieldRef(const DexMethod* context, uint32_t field_idx,
                                          const char* opcode) const {
    std::string out = opcode != nullptr ? opcode : "field";
    if (context == nullptr || context->dex_file == nullptr) return out;
    const art::DexFile& dex_file = *context->dex_file;
    const art::dex::FieldId& field_id = dex_file.GetFieldId(field_idx);
    const char* cls = dex_file.GetFieldDeclaringClassDescriptor(field_id);
    const char* name = dex_file.GetFieldName(field_id);
    const char* type = dex_file.GetFieldTypeDescriptor(field_id);

    out += " ";
    out += DescriptorToDotted(cls);
    out += ".";
    out += name != nullptr ? name : "?";
    out += " : ";
    out += type != nullptr ? type : "?";
    return out;
}

// Describe a method reference so failures name the method.
std::string Interpreter::DescribeMethodRef(const DexMethod* context,
                                          uint32_t method_idx) const {
    if (context == nullptr || context->dex_file == nullptr) {
        return "method index " + std::to_string(method_idx);
    }
    const art::DexFile& dex_file = *context->dex_file;
    const art::dex::MethodId& method_id = dex_file.GetMethodId(method_idx);
    const char* cls =
        dex_file.StringByTypeIdx(art::dex::TypeIndex(method_id.class_idx_.index_));
    const char* name = dex_file.GetMethodName(method_id);
    const std::string signature = dex_file.GetMethodSignature(method_id).ToString();

    std::string out = DescriptorToDotted(cls);
    out += ".";
    out += name != nullptr ? name : "?";
    out += signature;
    return out;
}

// Log unresolvable methods so stubs can be added.
void Interpreter::ReportUnresolvableMethod(const DexMethod* context, uint32_t method_idx) {
    if (context == nullptr || context->dex_file == nullptr) return;
    const art::DexFile& dex_file = *context->dex_file;
    const art::dex::MethodId& method_id = dex_file.GetMethodId(method_idx);
    const char* cls =
        dex_file.StringByTypeIdx(art::dex::TypeIndex(method_id.class_idx_.index_));
    const char* name = dex_file.GetMethodName(method_id);
    const std::string signature = dex_file.GetMethodSignature(method_id).ToString();

    // Note whether the declaring class itself is missing or stubbed.
    DexClass* klass = linker_ != nullptr ? linker_->FindClass(cls) : nullptr;
    std::string detail = std::string(cls != nullptr ? cls : "?") + "->" +
                         (name != nullptr ? name : "?") + signature;
    if (klass == nullptr) {
        detail += " (declaring class did not load)";
    } else if (klass->is_stub) {
        detail += " (declaring class is an auto-stub)";
    }

    static std::mutex s_mtx;
    static std::set<std::string> s_reported;
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        if (!s_reported.insert(detail).second) return;
    }
    std::fprintf(stderr, "[KuART][MISSING-METHOD] ⚠ UNRESOLVABLE: %s\n", detail.c_str());
    DexClassLinker::LogMissingMember("MISSING_FRAMEWORK_METHOD", detail);
}

bool Interpreter::CheckInstanceField(DexObject* obj, DexField* field, uint32_t width) {
    if (obj == nullptr || field == nullptr) return false;
    // A wild clazz pointer kills the process one LDRB later (observed: fault at
    // IsSubClassOf+0x4c reading obj->clazz->is_primitive, SEGV_ACCERR on a
    // 0x1004xxxxx pointer with no mapping). Report it and fail the access so the
    // interpreter unwinds instead of faulting in KuDroid's own text.
    const DexClass* clazz = obj->clazz;
    if (clazz != nullptr && linker_ != nullptr &&
        !linker_->IsRegisteredClass(reinterpret_cast<const DexClass*>(clazz))) {
        static std::mutex s_wild_mtx;
        static std::set<std::string> s_wild_reported;
        {
            std::lock_guard<std::mutex> lock(s_wild_mtx);
            const std::string key = "wild-clazz-0x" +
                std::to_string(reinterpret_cast<uintptr_t>(clazz));
            if (s_wild_reported.insert(key).second) {
                std::fprintf(stderr,
                             "[KuART][FIELD] wild clazz pointer %p on object %p "
                             "(not in live_classes_)\n",
                             reinterpret_cast<const void*>(clazz),
                             reinterpret_cast<const void*>(obj));
            }
        }
        ThrowException("Ljava/lang/NoSuchFieldError;", "object header corrupt");
        return false;
    }
    if (clazz == nullptr || field->declaring_class == nullptr ||
        !clazz->IsSubClassOf(field->declaring_class)) {
        ThrowException("Ljava/lang/NoSuchFieldError;", "field does not belong to receiver class");
        return false;
    }
    // object_size==0 means the layout was never computed (synthetic/test
    // classes): nothing to check against, so trust the offset. A computed
    // layout with an out-of-range offset is a hard error.
    if (clazz->object_size != 0) {
        const uint64_t end = static_cast<uint64_t>(field->offset_or_slot) + width;
        if (end > clazz->object_size) {
            ThrowException("Ljava/lang/NoSuchFieldError;", "field offset out of object");
            return false;
        }
    }
    return true;
}

DexMethod* Interpreter::ResolveMethod(const DexMethod* context, uint32_t method_idx,
                                      bool is_static, bool exact_owner) {
    if (context == nullptr || context->dex_file == nullptr) return nullptr;
    const art::DexFile& dex_file = *context->dex_file;
    if (method_idx >= dex_file.NumMethodIds()) return nullptr;
    auto trace = [&](DexMethod* method, const char* source, const DexClass* owner = nullptr) {
        const auto& id = dex_file.GetMethodId(method_idx);
        char line[384];
        std::snprintf(line, sizeof(line),
            "source=%s runtime=%p context=%p dex=%p index=%u target=%p lookup_class=%p ref=%s->%s",
            source, static_cast<void*>(linker_), static_cast<const void*>(context),
            static_cast<const void*>(&dex_file), method_idx, static_cast<void*>(method),
            static_cast<const void*>(owner), dex_file.StringByTypeIdx(id.class_idx_),
            dex_file.GetMethodName(id));
        RecordMethodResolution((uintptr_t)pthread_self(), line);
        return method;
    };
    const uint64_t generation = g_fastVirtualGeneration.load(std::memory_order_relaxed);
    if (t_fastMethodGeneration != generation) {
        std::fill_n(t_fastMethodCache, kFastMethodSlots, FastMethodSlot{});
        t_fastMethodGeneration = generation;
    }
    const size_t slot = ((reinterpret_cast<uintptr_t>(&dex_file) >> 3)
            ^ (reinterpret_cast<uintptr_t>(context->declaring_class) >> 3)
            ^ (method_idx * 31u) ^ (static_cast<uint32_t>(is_static) << 1)
            ^ static_cast<uint32_t>(exact_owner)) % kFastMethodSlots;
    const FastMethodSlot& cached = t_fastMethodCache[slot];
    if (cached.linker == linker_ && cached.dex == &dex_file && cached.owner == context->declaring_class
            && cached.index == method_idx && cached.is_static == is_static
            && cached.exact_owner == exact_owner) return trace(cached.method, "fast-cache");
    auto remember = [&](DexMethod* method, const char* source, const DexClass* owner = nullptr) {
        if (method != nullptr) t_fastMethodCache[slot] = FastMethodSlot{
                linker_, &dex_file, context->declaring_class, method_idx, is_static, exact_owner, method};
        return trace(method, source, owner);
    };
    if (!exact_owner) {
        if (DexMethod* cached =
                linker_->FindResolvedMethod(context->dex_file, method_idx, is_static)) {
            return remember(cached, "shared-cache");
        }
    }
    const art::dex::MethodId& method_id = dex_file.GetMethodId(method_idx);

    const char* class_descriptor =
        dex_file.StringByTypeIdx(art::dex::TypeIndex(method_id.class_idx_.index_));
    DexClass* klass = nullptr;
    if (exact_owner && context->declaring_class != nullptr) {
        for (DexClass* c = context->declaring_class; c != nullptr; c = c->superclass) {
            if (c->descriptor != nullptr && std::strcmp(c->descriptor, class_descriptor) == 0) {
                klass = c;
                break;
            }
        }
    }
    if (klass == nullptr) klass = linker_->FindClass(class_descriptor);
    if (klass == nullptr) return nullptr;

    const char* name = dex_file.GetMethodName(method_id);
    const std::string signature = dex_file.GetMethodSignature(method_id).ToString();

    if (DexMethod* m = klass->FindDirectMethod(name, signature.c_str())) {
        if (!exact_owner) linker_->CacheResolvedMethod(context->dex_file, method_idx, is_static, m);
        return remember(m, "direct-lookup", klass);
    }
    if (DexMethod* m = klass->FindVirtualMethod(name, signature.c_str())) {
        if (!exact_owner) linker_->CacheResolvedMethod(context->dex_file, method_idx, is_static, m);
        return remember(m, "virtual-lookup", klass);
    }

    auto isBootClasspathDescriptor = [](const char* desc) -> bool {
        if (!desc || desc[0] != 'L') return false;
        return (std::strncmp(desc, "Landroid/", 9) == 0 ||
                std::strncmp(desc, "Landroidx/", 10) == 0 ||
                std::strncmp(desc, "Ljava/", 6) == 0 ||
                std::strncmp(desc, "Ljavax/", 7) == 0 ||
                std::strncmp(desc, "Ldalvik/", 8) == 0 ||
                std::strncmp(desc, "Lsun/", 5) == 0 ||
                std::strncmp(desc, "Llibcore/", 9) == 0 ||
                std::strncmp(desc, "Lcom/android/", 13) == 0 ||
                std::strncmp(desc, "Lorg/apache/harmony/", 20) == 0 ||
                std::strncmp(desc, "Lorg/w3c/dom/", 13) == 0 ||
                std::strncmp(desc, "Lorg/xml/sax/", 13) == 0 ||
                std::strncmp(desc, "Lorg/xmlpull/", 13) == 0 ||
                std::strncmp(desc, "Lorg/json/", 10) == 0);
    };

    // An app class inherits from the framework: a method the framework lacks is looked up
    // through the app subclass (invoke-virtual names the receiver's static type), so the
    // owner in the reference is an app class even though the gap is in the framework.
    auto inheritsFromFramework = [&]() -> bool {
        for (const DexClass* c = klass->superclass; c != nullptr; c = c->superclass) {
            const char* d = c->descriptor;
            if (d == nullptr || std::strcmp(d, "Ljava/lang/Object;") == 0) continue;
            if (std::strncmp(d, "Landroid/", 9) == 0 || std::strncmp(d, "Ljava/", 6) == 0 ||
                std::strncmp(d, "Ljavax/", 7) == 0 || std::strncmp(d, "Ldalvik/", 8) == 0 ||
                std::strncmp(d, "Lcom/android/", 13) == 0) {
                return true;
            }
        }
        return false;
    };

    if (klass->dex_file == nullptr || isBootClasspathDescriptor(class_descriptor) ||
        inheritsFromFramework()) {
        std::lock_guard<std::mutex> lock(g_stubMethodMtx);

        std::string methodKey = std::string(class_descriptor) + "->" + name + signature;
        if (g_loggedMissingMethods.insert(methodKey).second) {
            std::fprintf(stderr, "[KuART][MISSING-METHOD] Auto-stubbing missing framework method: %s (dex index %u)\n",
                         methodKey.c_str(), method_idx);
            DexClassLinker::LogMissingMember("MISSING_FRAMEWORK_METHOD", methodKey);
        }

        auto key = std::make_tuple(klass, std::string(name), signature);
        auto it = g_stubMethods.find(key);
        if (it != g_stubMethods.end()) {
            DexMethod* cached = it->second.get();
            if (!exact_owner) {
                linker_->CacheResolvedMethod(context->dex_file, method_idx, is_static, cached);
            }
            return trace(cached, "stub-cache", klass);
        }

        auto stubM = std::make_unique<DexMethod>();
        stubM->name = name;
        // Signature must be recorded; null matches any overload by name.
        stubM->signature = linker_->heap().InternString(signature.c_str());
        stubM->declaring_class = klass;
        stubM->access_flags = art::kAccPublic |
                              (is_static ? art::kAccStatic : 0);
        stubM->code_item = nullptr;
        DexMethod* res = stubM.get();
        g_stubMethods.emplace(std::move(key), std::move(stubM));
        if (!exact_owner) {
            linker_->CacheResolvedMethod(context->dex_file, method_idx, is_static, res);
        }
        return trace(res, "new-stub", klass);
    }
    return nullptr;
}

// Forward proxy calls to the InvocationHandler; reachable from both invoke sites.
bool Interpreter::InvokeProxyMethod(DexObject* proxy, DexMethod* method,
                                    const DexValue* args, size_t num_args,
                                    DexValue* out) {
    if (linker_ == nullptr || proxy == nullptr || method == nullptr) return false;

    DexObject* handler = LibCoreGetRefField(proxy, "h",
                                           "Ljava/lang/reflect/InvocationHandler;");
    if (handler == nullptr) {
        ThrowException("Ljava/lang/NullPointerException;",
                       "proxy has no InvocationHandler");
        return true;
    }

    DexClass* handler_class = linker_->ClassOfObject(handler);
    if (handler_class == nullptr) {
        ThrowException("Ljava/lang/IllegalStateException;",
                       "proxy handler is not a usable object");
        return true;
    }
    DexMethod* invoke = handler_class->FindVirtualMethod(
        "invoke",
        "(Ljava/lang/Object;Ljava/lang/reflect/Method;[Ljava/lang/Object;)Ljava/lang/Object;");
    if (invoke == nullptr) {
        ThrowException("Ljava/lang/AbstractMethodError;",
                       handler_class->PrettyName() + " does not implement InvocationHandler.invoke");
        return true;
    }

    // A real java.lang.reflect.Method for the interface method being proxied. Passing
    // null here would be simpler but breaks the common handler shape, which switches
    // on method.getName() and inspects getParameterTypes().
    DexObject* method_object = LibCoreNewMethodObject(linker_, method);

    // Arguments arrive with the receiver at index 0 for an instance method; the
    // handler expects only the declared parameters, each boxed.
    const size_t first = method->IsStatic() ? 0 : 1;
    std::vector<DexObject*> boxed;
    const std::vector<std::string> params = LibCoreSplitParams(method->signature);
    boxed.reserve(params.size());
    size_t arg_index = first;
    for (const std::string& param : params) {
        if (arg_index >= num_args) break;
        boxed.push_back(LibCoreBoxValue(this, param.c_str(), args[arg_index]));
        // A long or double occupies two DexValue slots in the incoming array.
        arg_index += (param == "J" || param == "D") ? 2 : 1;
    }

    DexArray* arg_array = LibCoreNewRefArray(linker_, "[Ljava/lang/Object;", boxed);

    DexValue call_args[4];
    call_args[0] = DexValue::Ref(handler);
    call_args[1] = DexValue::Ref(proxy);
    call_args[2] = DexValue::Ref(method_object);
    call_args[3] = DexValue::Ref(reinterpret_cast<DexObject*>(arg_array));

    const DexValue boxed_result = Execute(invoke, call_args, 4);
    if (HasPendingException()) return true;

    // Unbox to return type; null for primitives yields zero, not failure.
    const char* ret = method->signature != nullptr ? std::strchr(method->signature, ')') : nullptr;
    if (ret != nullptr) ++ret;
    if (ret == nullptr || ret[0] == 'V') {
        *out = DexValue();
        return true;
    }
    if (ret[0] == 'L' || ret[0] == '[') {
        *out = DexValue::Ref(boxed_result.l);
        return true;
    }
    DexValue unboxed;
    if (!LibCoreUnboxValue(this, ret, boxed_result.l, &unboxed)) {
        unboxed = DexValue();
    }
    *out = unboxed;
    return true;
}

bool Interpreter::InvokeMethod(DexFrame* frame, const art::Instruction* inst, bool is_range,
                              Instruction::Code opcode) {
    const uint32_t method_idx = is_range ? inst->VRegB_3rc() : inst->VRegB_35c();
    const bool is_static = opcode == Instruction::INVOKE_STATIC ||
                           opcode == Instruction::INVOKE_STATIC_RANGE;
    const bool exact_owner = opcode == Instruction::INVOKE_DIRECT ||
                             opcode == Instruction::INVOKE_DIRECT_RANGE ||
                             opcode == Instruction::INVOKE_SUPER ||
                             opcode == Instruction::INVOKE_SUPER_RANGE;
    DexMethod* target = ResolveMethod(frame->method(), method_idx, is_static, exact_owner);
    if (target == nullptr) {
        // Name the method; a raw DEX index alone cannot be acted on.
        ReportUnresolvableMethod(frame->method(), method_idx);
        ThrowException("Ljava/lang/NoSuchMethodError;",
                       DescribeMethodRef(frame->method(), method_idx));
        return false;
    }

    // The receiver convention follows the opcode, not the target's flags. Running a
    // static call through an instance method silently shifts every argument by one
    // (the first argument becomes `this`), so reject the mismatch instead.
    if (target->IsStatic() != is_static) {
        std::string detail = is_static ? "invoke-static " : "invoke-instance ";
        if (target->declaring_class != nullptr) {
            detail += target->declaring_class->PrettyName();
            detail += ".";
        }
        detail += target->name != nullptr ? target->name : "?";
        if (target->signature != nullptr) detail += target->signature;
        detail += is_static ? " resolved to an instance method" : " resolved to a static method";
        ThrowException("Ljava/lang/NoSuchMethodError;", detail);
        return false;
    }

    // Collect register containing parameters. The /range form uses a continuous range, the normal form
    // Use a list of up to 5 discrete registers.
    uint32_t arg_regs[art::Instruction::kMaxVarArgRegs];
    uint32_t arg_count;
    uint32_t first_reg = 0;
    if (is_range) {
        arg_count = inst->VRegA_3rc();
        first_reg = inst->VRegC_3rc();
    } else {
        arg_count = inst->GetVarArgs(arg_regs);
    }

    // Static call triggers class initialization.
    if (is_static) {
        EnsureInitialized(target->declaring_class);
        if (HasPendingException()) return false;
    }

    // Wide values use two slots but one register; skip the second slot.
    const char* shorty = nullptr;
    if (target->dex_file != nullptr) {
        shorty = target->dex_file->GetMethodShorty(
            target->dex_file->GetMethodId(target->dex_method_index));
    }

    // Invocations overwhelmingly have a handful of arguments; keep the array on the
    // stack and fall back to the heap only for wide signatures.
    DexValue inline_args[8];
    std::vector<DexValue> heap_args;
    DexValue* args = inline_args;
    size_t argc = 0;
    if (arg_count > 8) {
        heap_args.resize(arg_count);
        args = heap_args.data();
    }

    uint32_t slot = 0;
    const auto reg_at = [&](uint32_t i) {
        return is_range ? first_reg + i : arg_regs[i];
    };

    DexObject* receiver = nullptr;
    if (!is_static) {
        if (arg_count == 0) {
            ThrowException("Ljava/lang/VerifyError;", "invoke instance missing receiver");
            return false;
        }
        receiver = frame->GetRef(reg_at(0));
        if (receiver == nullptr) {
            ThrowException("Ljava/lang/NullPointerException;",
                           std::string("call ") + target->name + " on null");
            return false;
        }
        args[argc++] = DexValue::Ref(receiver);
        slot = 1;
    }

    if (shorty != nullptr) {
        for (const char* p = shorty + 1; *p != '\0' && slot < arg_count; ++p) {
            args[argc++] = frame->Get(reg_at(slot));
            slot += (*p == 'J' || *p == 'D') ? 2 : 1;
        }
    }

    // Dispatch virtually on the validated receiver class.
    if (opcode == Instruction::INVOKE_VIRTUAL ||
        opcode == Instruction::INVOKE_VIRTUAL_RANGE ||
        opcode == Instruction::INVOKE_INTERFACE ||
        opcode == Instruction::INVOKE_INTERFACE_RANGE) {
        DexClass* receiver_class =
            linker_ != nullptr ? linker_->ClassOfObject(receiver) : nullptr;
        if (receiver_class != nullptr) {
            DexMethod* resolved = FastVirtualLookup(inst, receiver_class);
            if (resolved == nullptr) {
                resolved = receiver_class->FindVirtualMethod(target->name, target->signature);
                FastVirtualStore(inst, receiver_class, resolved);
            }
            if (resolved != nullptr) {
                target = resolved;
            } else if (target->declaring_class != nullptr &&
                       receiver_class->IsSubClassOf(target->declaring_class)) {
                // Target is declared on an implemented interface or base class; keep target.
            } else if (!receiver_class->is_proxy) {
                std::string detail = "invoke ";
                if (target->declaring_class != nullptr) {
                    detail += target->declaring_class->PrettyName();
                    detail += ".";
                }
                detail += target->name != nullptr ? target->name : "?";
                if (target->signature != nullptr) detail += target->signature;
                detail += " — receiver class ";
                detail += receiver_class->descriptor != nullptr ? receiver_class->descriptor : "?";
                detail += " does not implement method";
                ThrowException("Ljava/lang/IncompatibleClassChangeError;", detail);
                return false;
            }
        } else if (receiver != nullptr) {
            // Name both the method and the bad receiver for diagnosis.
            std::string detail = "invoke ";
            if (target->declaring_class != nullptr) {
                detail += target->declaring_class->PrettyName();
                detail += ".";
            }
            detail += target->name != nullptr ? target->name : "?";
            if (target->signature != nullptr) detail += target->signature;
            detail += " — ";
            detail += linker_->DescribeBadReceiver(receiver);
            ThrowException("Ljava/lang/IllegalStateException;", detail);
            return false;
        }
    }

    if (target->IsNative()) {
        // Libcore natives run directly in C++, bypassing the JNI ABI.
        DexValue lib_result;
        if (LibCoreInvoke(this, target, args, argc, &lib_result)) {
            if (HasPendingException()) return false;
            frame->set_result(lib_result);
            return true;
        }
        if (jni_env_ == nullptr || !jni_env_->LinkNativeMethod(target)) {
            ThrowException("Ljava/lang/UnsatisfiedLinkError;",
                           std::string("unbound native method: ") + target->name);
            return false;
        }
        const DexValue native_result = jni_env_->CallNative(target, args, argc);
        if (HasPendingException()) return false;
        frame->set_result(native_result);
        return true;
    }

    const DexValue result = Execute(target, args, argc);
    if (HasPendingException()) return false;
    frame->set_result(result);
    return true;
}

// filled-new-array writes frame result, like an invoke, for move-result.
bool Interpreter::FilledNewArray(DexFrame* frame, const art::Instruction* inst, bool is_range) {
    const DexMethod* method = frame->method();

    // VRegB is the ARRAY type ("[I"), not the element type - take component_type
    // from the resolved class rather than slicing the descriptor, so that
    // multi-dimensional arrays ("[[I") stay correct.
    const uint32_t type_idx = is_range ? inst->VRegB_3rc() : inst->VRegB_35c();
    DexClass* array_class = ResolveClass(method, type_idx);
    if (array_class == nullptr || !array_class->is_array) {
        ThrowException("Ljava/lang/ClassNotFoundException;", "filled-new-array type");
        return false;
    }

    uint32_t arg_regs[art::Instruction::kMaxVarArgRegs];
    uint32_t count;
    uint32_t first_reg = 0;
    if (is_range) {
        count = inst->VRegA_3rc();
        first_reg = inst->VRegC_3rc();
    } else {
        count = inst->GetVarArgs(arg_regs);
    }

    DexArray* arr = linker_->AllocArray(array_class, static_cast<int32_t>(count));
    if (arr == nullptr) {
        ThrowException("Ljava/lang/OutOfMemoryError;", "filled-new-array");
        return false;
    }

    // Element width follows component_type, matching DexClassLinker::ElementSize
    // used by AllocArray - writing a wider type here would run past the allocation.
    const DexClass* component = array_class->component_type;
    const char kind = (component != nullptr && component->is_primitive &&
                       component->descriptor != nullptr)
                          ? component->descriptor[0]
                          : 'L';

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t vreg = is_range ? first_reg + i : arg_regs[i];
        const int32_t index = static_cast<int32_t>(i);
        switch (kind) {
            case 'Z':
            case 'B':
                arr->Set<int8_t>(index, static_cast<int8_t>(frame->GetInt(vreg)));
                break;
            case 'C':
                arr->Set<uint16_t>(index, static_cast<uint16_t>(frame->GetInt(vreg)));
                break;
            case 'S':
                arr->Set<int16_t>(index, static_cast<int16_t>(frame->GetInt(vreg)));
                break;
            case 'J':
            case 'D':
                // Wide elements: each argument register holds the whole 64-bit
                // value (KuART's one-slot-per-vreg convention), so no pairing.
                arr->Set<int64_t>(index, frame->GetLong(vreg));
                break;
            case 'I':
            case 'F':
                arr->Set<int32_t>(index, frame->GetInt(vreg));
                break;
            default:
                arr->Set<DexObject*>(index, frame->GetRef(vreg));
                break;
        }
    }

    frame->set_result(DexValue::Ref(arr));
    return true;
}

