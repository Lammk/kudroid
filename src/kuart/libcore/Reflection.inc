// Turns the Object[] of a reflective call into the DexValue array the
// interpreter expects, unboxing each primitive parameter.
bool BuildInvokeArgs(Interpreter* interp, DexMethod* target, DexObject* receiver,
                     DexArray* boxed, std::vector<DexValue>* out) {
    const std::vector<std::string> params = SplitParams(target->signature);
    const int32_t given = boxed != nullptr ? boxed->length : 0;
    if (static_cast<size_t>(given) != params.size()) {
        interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                               "wrong argument count: expected " +
                                   std::to_string(params.size()) + ", got " +
                                   std::to_string(given));
        return false;
    }
    if (!target->IsStatic()) out->push_back(DexValue::Ref(receiver));

    auto** items = boxed != nullptr ? reinterpret_cast<DexObject**>(boxed + 1) : nullptr;
    for (size_t i = 0; i < params.size(); ++i) {
        DexValue v;
        if (!UnboxValue(interp, params[i].c_str(), items[i], &v)) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   "argument " + std::to_string(i) + " is not a " + params[i]);
            return false;
        }
        out->push_back(v);
    }
    return true;
}

bool Invoke_java_lang_reflect_Method(Interpreter* interp, const char* name,
                                     const DexValue* args, size_t num_args,
                                     DexValue* result) {
    DexClassLinker* linker = interp->linker();
    DexMethod* m = MethodFromObject(args[0].l);
    if (m == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;", "method handle null");
        return true;
    }

    if (std::strcmp(name, "getModifiers") == 0) {
        *result = DexValue::Int(static_cast<int32_t>(m->access_flags));
        return true;
    }
    // An interface method with a body. The three conditions are the platform's, and each
    // one is load-bearing: an abstract interface method has nothing to call, and a static
    // interface method is not inherited so it is not a default either.
    if (std::strcmp(name, "isDefault") == 0) {
        const bool in_interface =
            m->declaring_class != nullptr && m->declaring_class->IsInterface();
        const bool is_default = in_interface && !m->IsStatic() && !m->IsAbstract();
        *result = DexValue::Int(is_default ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isBridge") == 0) {
        *result = DexValue::Int((m->access_flags & art::kAccBridge) != 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isSynthetic") == 0) {
        *result = DexValue::Int((m->access_flags & art::kAccSynthetic) != 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isVarArgs") == 0) {
        *result = DexValue::Int((m->access_flags & art::kAccVarargs) != 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "getReturnType") == 0) {
        DexClass* k = linker->FindClass(ReturnDescriptor(m));
        result->l = k != nullptr ? linker->GetClassObject(k) : nullptr;
        return true;
    }
    if (std::strcmp(name, "getParameterTypes") == 0) {
        result->l = ClassArrayFromDescriptors(linker, SplitParams(m->signature));
        return true;
    }
    if (std::strcmp(name, "invoke") == 0) {
        DexObject* receiver = num_args > 1 ? args[1].l : nullptr;
        auto* boxed = num_args > 2 ? reinterpret_cast<DexArray*>(args[2].l) : nullptr;

        // Diagnostic: Unity's JNIBridge reflects every Java call from C# through
        // here, and when it throws a messageless NoSuchMethodError the only way
        // to know which method died is this line. The last REFLECT line before
        // the uncaught trace is the prime suspect. Grep REFLECT in stderr.log.
        {
            const char* owner = (m->declaring_class != nullptr &&
                                 m->declaring_class->descriptor != nullptr)
                                    ? m->declaring_class->descriptor
                                    : "?";
            std::fprintf(stderr, "[KuART][REFLECT] invoke %s.%s%s\n", owner,
                         m->name != nullptr ? m->name : "?",
                         m->signature != nullptr ? m->signature : "?");
        }

        if (!m->IsStatic() && receiver == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;",
                                   std::string("invoke ") + m->name + " on null");
            return true;
        }
        // Virtual dispatch: the Method handle may come from a superclass while
        // the receiver overrides it. Validate the receiver's class instead of only
        // null-checking it - a Method.invoke reachable from JNI can be handed a
        // native-supplied receiver (see DexClassLinker::ClassOfObject).
        DexMethod* target = m;
        if (!m->IsStatic() && m->vtable_index != DexMethod::kInvalidVTableIndex) {
            if (DexClass* receiver_class = interp->linker()->ClassOfObject(receiver)) {
                if (DexMethod* found = receiver_class->FindVirtualMethod(m->name, m->signature)) {
                    target = found;
                }
            }
        }
        if (target->IsStatic()) interp->EnsureInitialized(target->declaring_class);

        std::vector<DexValue> call_args;
        if (!BuildInvokeArgs(interp, target, receiver, boxed, &call_args)) return true;

        const DexValue ret = interp->Execute(target, call_args.data(), call_args.size());
        if (interp->HasPendingException()) {
            const char* owner = (target->declaring_class != nullptr &&
                                 target->declaring_class->descriptor != nullptr)
                                    ? target->declaring_class->descriptor
                                    : "?";
            std::fprintf(stderr, "[KuART][REFLECT] invoke FAILED %s.%s%s: %s\n",
                         owner, target->name != nullptr ? target->name : "?",
                         target->signature != nullptr ? target->signature : "?",
                         interp->last_error().c_str());
            return true;
        }

        const char* ret_desc = ReturnDescriptor(target);
        result->l = (ret_desc[0] == 'V') ? nullptr : BoxValue(interp, ret_desc, ret);
        return true;
    }
    return false;
}

bool Invoke_java_lang_reflect_Constructor(Interpreter* interp, const char* name,
                                          const DexValue* args, size_t num_args,
                                          DexValue* result) {
    DexClassLinker* linker = interp->linker();
    DexMethod* ctor = MethodFromObject(args[0].l);
    if (ctor == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;", "constructor handle null");
        return true;
    }

    if (std::strcmp(name, "getModifiers") == 0) {
        *result = DexValue::Int(static_cast<int32_t>(ctor->access_flags));
        return true;
    }
    if (std::strcmp(name, "getParameterTypes") == 0) {
        result->l = ClassArrayFromDescriptors(linker, SplitParams(ctor->signature));
        return true;
    }
    if (std::strcmp(name, "newInstance") == 0) {
        DexClass* klass = ctor->declaring_class;
        if (klass != nullptr && klass->is_stub) {
            // Stubs have no members; see DexClass::is_stub.
            interp->ThrowException("Ljava/lang/NoClassDefFoundError;", klass->PrettyName());
            return true;
        }
        if (klass == nullptr || klass->IsInterface() || klass->IsAbstract()) {
            interp->ThrowException("Ljava/lang/InstantiationException;",
                                   klass != nullptr ? klass->PrettyName() : "null");
            return true;
        }
        interp->EnsureInitialized(klass);
        DexObject* obj = linker->AllocObject(klass);
        if (obj == nullptr) {
            interp->ThrowException("Ljava/lang/OutOfMemoryError;", "newInstance");
            return true;
        }
        auto* boxed = num_args > 1 ? reinterpret_cast<DexArray*>(args[1].l) : nullptr;
        std::vector<DexValue> call_args;
        call_args.push_back(DexValue::Ref(obj));
        const std::vector<std::string> params = SplitParams(ctor->signature);
        const int32_t given = boxed != nullptr ? boxed->length : 0;
        if (static_cast<size_t>(given) != params.size()) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   "wrong argument count for <init>");
            return true;
        }
        auto** items = boxed != nullptr ? reinterpret_cast<DexObject**>(boxed + 1) : nullptr;
        for (size_t i = 0; i < params.size(); ++i) {
            DexValue v;
            if (!UnboxValue(interp, params[i].c_str(), items[i], &v)) {
                interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                       "argument " + std::to_string(i) + " is not a " + params[i]);
                return true;
            }
            call_args.push_back(v);
        }
        interp->Execute(ctor, call_args.data(), call_args.size());
        if (interp->HasPendingException()) return true;
        result->l = obj;
        return true;
    }
    return false;
}

bool Invoke_java_lang_reflect_Field(Interpreter* interp, const char* name,
                                    const DexValue* args, size_t num_args,
                                    DexValue* result) {
    DexClassLinker* linker = interp->linker();
    DexField* f = FieldFromObject(args[0].l);
    if (f == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;", "field handle null");
        return true;
    }

    if (std::strcmp(name, "getModifiers") == 0) {
        *result = DexValue::Int(static_cast<int32_t>(f->access_flags));
        return true;
    }
    if (std::strcmp(name, "getType") == 0) {
        DexClass* k = linker->FindClass(f->type_descriptor);
        result->l = k != nullptr ? linker->GetClassObject(k) : nullptr;
        return true;
    }

    const bool is_get = std::strcmp(name, "get") == 0;
    const bool is_set = std::strcmp(name, "set") == 0;
    if (!is_get && !is_set) return false;

    DexObject* target = num_args > 1 ? args[1].l : nullptr;
    if (f->IsStatic()) {
        DexClass* owner = f->declaring_class;
        if (owner == nullptr || f->offset_or_slot >= owner->static_values.size()) {
            interp->ThrowException("Ljava/lang/NoSuchFieldError;", f->name);
            return true;
        }
        interp->EnsureInitialized(owner);
        DexValue& slot = owner->static_values[f->offset_or_slot];
        if (is_get) {
            result->l = BoxValue(interp, f->type_descriptor, slot);
        } else {
            DexValue v;
            if (!UnboxValue(interp, f->type_descriptor, num_args > 2 ? args[2].l : nullptr, &v)) {
                interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                       std::string("value is not a ") + f->type_descriptor);
                return true;
            }
            slot = v;
        }
        return true;
    }

    if (target == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;",
                               std::string("instance field ") + f->name + " on null");
        return true;
    }

    // Reads and writes go through the field's descriptor: a plain 8-byte copy
    // would read past the end of a byte/short field.
    const char kind = f->type_descriptor != nullptr ? f->type_descriptor[0] : 'L';
    const uint32_t off = f->offset_or_slot;
    if (is_get) {
        DexValue v;
        switch (kind) {
            case 'Z': v = DexValue::Int(target->GetField<uint8_t>(off) != 0 ? 1 : 0); break;
            case 'B': v = DexValue::Int(target->GetField<int8_t>(off)); break;
            case 'C': v = DexValue::Int(target->GetField<uint16_t>(off)); break;
            case 'S': v = DexValue::Int(target->GetField<int16_t>(off)); break;
            case 'I': v = DexValue::Int(target->GetField<int32_t>(off)); break;
            case 'J': v = DexValue::Long(target->GetField<int64_t>(off)); break;
            case 'F': v = DexValue::Float(target->GetField<float>(off)); break;
            case 'D': v = DexValue::Double(target->GetField<double>(off)); break;
            default:  v = DexValue::Ref(target->GetField<DexObject*>(off)); break;
        }
        result->l = BoxValue(interp, f->type_descriptor, v);
        return true;
    }

    DexValue v;
    if (!UnboxValue(interp, f->type_descriptor, num_args > 2 ? args[2].l : nullptr, &v)) {
        interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                               std::string("value is not a ") + f->type_descriptor);
        return true;
    }
    switch (kind) {
        case 'Z': target->SetField<uint8_t>(off, v.i != 0 ? 1 : 0); break;
        case 'B': target->SetField<int8_t>(off, static_cast<int8_t>(v.i)); break;
        case 'C': target->SetField<uint16_t>(off, static_cast<uint16_t>(v.i)); break;
        case 'S': target->SetField<int16_t>(off, static_cast<int16_t>(v.i)); break;
        case 'I': target->SetField<int32_t>(off, v.i); break;
        case 'J': target->SetField<int64_t>(off, v.j); break;
        case 'F': target->SetField<float>(off, v.f); break;
        case 'D': target->SetField<double>(off, v.d); break;
        default:  target->SetField<DexObject*>(off, v.l); break;
    }
    return true;
}

// Allocate an n-dimensional array. Recursive: the outer array holds references to
// arrays one dimension smaller, which is how the JVM represents T[a][b].
DexArray* AllocMultiArray(Interpreter* interp, const std::string& component_descriptor,
                          const int32_t* dims, size_t ndims) {
    DexClassLinker* linker = interp->linker();
    if (ndims == 0) return nullptr;

    // Descriptor for this level: one '[' per remaining dimension.
    std::string desc;
    desc.reserve(ndims + component_descriptor.size());
    for (size_t i = 0; i < ndims; ++i) desc += '[';
    desc += component_descriptor;

    DexClass* array_class = linker->FindClass(desc.c_str());
    if (array_class == nullptr) return nullptr;

    DexArray* arr = linker->AllocArray(array_class, dims[0]);
    if (arr == nullptr || ndims == 1) return arr;

    for (int32_t i = 0; i < dims[0]; ++i) {
        DexArray* sub = AllocMultiArray(interp, component_descriptor, dims + 1, ndims - 1);
        if (sub == nullptr) return nullptr;
        arr->Set<DexObject*>(i, sub);
    }
    return arr;
}

bool Invoke_java_lang_reflect_Array(Interpreter* interp, const DexMethod* method,
                                    const char* name, const DexValue* args,
                                    size_t num_args, DexValue* result) {
    DexClassLinker* linker = interp->linker();

    if (std::strcmp(name, "newInstance") == 0) {
        DexClass* component = ClassOf(interp, args[0].l);
        if (component == nullptr || component->descriptor == nullptr) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;", "null component type");
            return true;
        }

        // Two overloads share this name. DEX has no multianewarray opcode, so d8
        // compiles `new T[a][b]` into newInstance(Class, int[]) - dispatch on the
        // declared signature rather than guessing from the argument value, which
        // would confuse an int[] handle with an int.
        const bool multi = method != nullptr && method->signature != nullptr &&
                           std::strstr(method->signature, "[I)") != nullptr;

        if (multi) {
            auto* dims_arr = reinterpret_cast<DexArray*>(num_args > 1 ? args[1].l : nullptr);
            if (dims_arr == nullptr || dims_arr->length <= 0) {
                interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                       "empty dimensions array");
                return true;
            }
            if (dims_arr->length > 255) {
                interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                       "too many dimensions");
                return true;
            }
            std::vector<int32_t> dims(static_cast<size_t>(dims_arr->length));
            for (int32_t i = 0; i < dims_arr->length; ++i) {
                dims[static_cast<size_t>(i)] = dims_arr->Get<int32_t>(i);
                if (dims[static_cast<size_t>(i)] < 0) {
                    interp->ThrowException("Ljava/lang/NegativeArraySizeException;",
                                           std::to_string(dims[static_cast<size_t>(i)]));
                    return true;
                }
            }
            result->l = AllocMultiArray(interp, component->descriptor, dims.data(),
                                        dims.size());
            if (result->l == nullptr) {
                interp->ThrowException("Ljava/lang/OutOfMemoryError;",
                                       "Array.newInstance multi-dimensional");
            }
            return true;
        }

        const int32_t length = num_args > 1 ? args[1].i : 0;
        if (length < 0) {
            interp->ThrowException("Ljava/lang/NegativeArraySizeException;",
                                   std::to_string(length));
            return true;
        }
        DexClass* array_class = linker->FindClass((std::string("[") + component->descriptor).c_str());
        result->l = array_class != nullptr ? linker->AllocArray(array_class, length) : nullptr;
        return true;
    }

    auto* arr = reinterpret_cast<DexArray*>(args[0].l);
    if (arr == nullptr || arr->clazz == nullptr || !arr->clazz->is_array) {
        interp->ThrowException("Ljava/lang/IllegalArgumentException;", "not an array");
        return true;
    }

    if (std::strcmp(name, "getLength") == 0) {
        *result = DexValue::Int(arr->length);
        return true;
    }

    const bool is_get = std::strcmp(name, "get") == 0;
    const bool is_set = std::strcmp(name, "set") == 0;
    if (!is_get && !is_set) return false;

    const int32_t index = num_args > 1 ? args[1].i : 0;
    if (index < 0 || index >= arr->length) {
        interp->ThrowException("Ljava/lang/ArrayIndexOutOfBoundsException;",
                               std::to_string(index));
        return true;
    }
    DexClass* component = arr->clazz->component_type;
    const char* desc = component != nullptr ? component->descriptor : "Ljava/lang/Object;";
    const char kind = desc != nullptr ? desc[0] : 'L';
    auto* base = reinterpret_cast<uint8_t*>(arr + 1);

    if (is_get) {
        DexValue v;
        switch (kind) {
            case 'Z': case 'B': v = DexValue::Int(reinterpret_cast<int8_t*>(base)[index]); break;
            case 'C': v = DexValue::Int(reinterpret_cast<uint16_t*>(base)[index]); break;
            case 'S': v = DexValue::Int(reinterpret_cast<int16_t*>(base)[index]); break;
            case 'I': v = DexValue::Int(reinterpret_cast<int32_t*>(base)[index]); break;
            case 'J': v = DexValue::Long(reinterpret_cast<int64_t*>(base)[index]); break;
            case 'F': v = DexValue::Float(reinterpret_cast<float*>(base)[index]); break;
            case 'D': v = DexValue::Double(reinterpret_cast<double*>(base)[index]); break;
            default:  v = DexValue::Ref(reinterpret_cast<DexObject**>(base)[index]); break;
        }
        result->l = BoxValue(interp, desc, v);
        return true;
    }

    DexValue v;
    if (!UnboxValue(interp, desc, num_args > 2 ? args[2].l : nullptr, &v)) {
        interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                               std::string("value is not a ") + desc);
        return true;
    }
    switch (kind) {
        case 'Z': case 'B': reinterpret_cast<int8_t*>(base)[index] = static_cast<int8_t>(v.i); break;
        case 'C': reinterpret_cast<uint16_t*>(base)[index] = static_cast<uint16_t>(v.i); break;
        case 'S': reinterpret_cast<int16_t*>(base)[index] = static_cast<int16_t>(v.i); break;
        case 'I': reinterpret_cast<int32_t*>(base)[index] = v.i; break;
        case 'J': reinterpret_cast<int64_t*>(base)[index] = v.j; break;
        case 'F': reinterpret_cast<float*>(base)[index] = v.f; break;
        case 'D': reinterpret_cast<double*>(base)[index] = v.d; break;
        default:  reinterpret_cast<DexObject**>(base)[index] = v.l; break;
    }
    return true;
}

// java.lang.reflect.Proxy

// newProxyInstance synthesizes a bodyless class; interpreter forwards to handler.
bool Invoke_java_lang_reflect_Proxy(Interpreter* interp, const char* name,
                                    const DexValue* args, size_t num_args,
                                    DexValue* result) {
    DexClassLinker* linker = interp->linker();

    const auto collect_interfaces = [&](DexObject* array_obj,
                                        std::vector<DexClass*>* out) -> bool {
        auto* arr = reinterpret_cast<DexArray*>(array_obj);
        if (arr == nullptr) return true;  // no interfaces is legal, if useless
        auto** items = reinterpret_cast<DexObject**>(arr + 1);
        for (int32_t i = 0; i < arr->length; ++i) {
            DexClass* iface = ClassOf(interp, items[i]);
            if (iface == nullptr) {
                interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                       "interface " + std::to_string(i) +
                                           " is not a class");
                return false;
            }
            out->push_back(iface);
        }
        return true;
    };

    if (std::strcmp(name, "newProxyInstance") == 0) {
        // (ClassLoader, Class[], InvocationHandler) - static, so args[0] is the loader.
        DexObject* handler = num_args > 2 ? args[2].l : nullptr;
        if (handler == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;",
                                   "InvocationHandler is null");
            return true;
        }
        std::vector<DexClass*> interfaces;
        if (!collect_interfaces(num_args > 1 ? args[1].l : nullptr, &interfaces)) return true;

        DexClass* proxy_class = linker->GetOrCreateProxyClass(interfaces);
        if (proxy_class == nullptr) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   "cannot create proxy class: " + linker->last_error());
            return true;
        }
        DexObject* proxy = linker->AllocObject(proxy_class);
        if (proxy == nullptr) {
            interp->ThrowException("Ljava/lang/OutOfMemoryError;", "proxy allocation failed");
            return true;
        }
        // `h` is declared by java.lang.reflect.Proxy, which this class extends.
        SetRefField(proxy, "h", "Ljava/lang/reflect/InvocationHandler;", handler);
        // TEMP DIAGNOSTIC (ULTRAKILL dead Runnable proxies): Unity's native bridge
        // keeps the C++ peer for `ptr` and the proxy's interfaces must match what the
        // peer was built for. Log both so a stranded peer can be correlated by ptr.
        // Remove once the failing peer is identified.
        if (DexClass* handler_class = linker->ClassOfObject(handler)) {
            if (handler_class->descriptor != nullptr &&
                std::strstr(handler_class->descriptor, "bitter/jnibridge") != nullptr) {
                std::string names;
                for (size_t i = 0; i < interfaces.size(); ++i) {
                    if (i > 0) names += ",";
                    names += interfaces[i] != nullptr ? interfaces[i]->PrettyName() : "?";
                }
                std::string ptrs;
                for (DexClass* k = handler_class; k != nullptr; k = k->superclass) {
                    for (DexField& f : k->instance_fields) {
                        if (f.type_descriptor != nullptr &&
                            std::strcmp(f.type_descriptor, "J") == 0) {
                            int64_t v =
                                handler->GetField<int64_t>(f.offset_or_slot);
                            char field[48];
                            std::snprintf(field, sizeof(field), " %s=0x%llx",
                                          f.name != nullptr ? f.name : "?",
                                          static_cast<unsigned long long>(v));
                            ptrs += field;
                        }
                    }
                }
                std::fprintf(stderr, "[KuART][PROXY] new jnibridge proxy ifaces=[%s]%s\n",
                             names.c_str(), ptrs.c_str());
            }
        }
        result->l = proxy;
        return true;
    }

    if (std::strcmp(name, "getProxyClass") == 0) {
        std::vector<DexClass*> interfaces;
        if (!collect_interfaces(num_args > 1 ? args[1].l : nullptr, &interfaces)) return true;
        DexClass* proxy_class = linker->GetOrCreateProxyClass(interfaces);
        result->l = proxy_class != nullptr ? linker->GetClassObject(proxy_class) : nullptr;
        return true;
    }

    if (std::strcmp(name, "isProxyClass") == 0) {
        DexClass* k = ClassOf(interp, num_args > 0 ? args[0].l : nullptr);
        *result = DexValue::Int(k != nullptr && k->is_proxy ? 1 : 0);
        return true;
    }

    if (std::strcmp(name, "getInvocationHandler") == 0) {
        DexObject* proxy = num_args > 0 ? args[0].l : nullptr;
        DexClass* k = linker->ClassOfObject(proxy);
        if (k == nullptr || !k->is_proxy) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   "not a proxy instance");
            return true;
        }
        result->l = GetRefField(proxy, "h", "Ljava/lang/reflect/InvocationHandler;");
        return true;
    }

    return false;
}

// MethodHandle support for unreflectSpecial default-method calls on proxies.

// Wrap DexMethod as a Direct MethodHandle with its DEX type.
DexObject* NewMethodHandle(Interpreter* interp, DexMethod* m, DexObject* receiver,
                           bool special) {
    DexClassLinker* linker = interp->linker();
    if (linker == nullptr || m == nullptr) return nullptr;

    DexClass* klass = linker->FindClass("Ljava/lang/invoke/MethodHandle$Direct;");
    if (klass == nullptr) {
        interp->ThrowException("Ljava/lang/InternalError;",
                               "java.lang.invoke.MethodHandle$Direct is missing from the framework");
        return nullptr;
    }
    interp->EnsureInitialized(klass);
    DexObject* handle = linker->AllocObject(klass);
    if (handle == nullptr) {
        interp->ThrowException("Ljava/lang/OutOfMemoryError;", "MethodHandle");
        return nullptr;
    }

    SetLongField(handle, "artMethod", static_cast<int64_t>(reinterpret_cast<uintptr_t>(m)));
    SetRefField(handle, "receiver", "Ljava/lang/Object;", receiver);

    // Set all handle fields explicitly; do not rely on zeroed payload.
    if (DexField* f_special = klass->FindInstanceField("special", "Z")) {
        handle->SetField<uint8_t>(f_special->offset_or_slot, special ? 1 : 0);
    }
    if (DexField* f_static = klass->FindInstanceField("isStatic", "Z")) {
        handle->SetField<uint8_t>(f_static->offset_or_slot, m->IsStatic() ? 1 : 0);
    }

    // type() is built by calling MethodType.fromMethodDescriptorString on the method's own
    // signature. Going through the Java factory rather than allocating a MethodType here
    // keeps one implementation of descriptor parsing: a second one in C++ would drift.
    DexClass* mt = linker->FindClass("Ljava/lang/invoke/MethodType;");
    if (mt != nullptr && m->signature != nullptr) {
        interp->EnsureInitialized(mt);
        DexMethod* from_desc = mt->FindDirectMethod(
            "fromMethodDescriptorString",
            "(Ljava/lang/String;Ljava/lang/ClassLoader;)Ljava/lang/invoke/MethodType;");
        if (from_desc != nullptr) {
            DexValue call_args[2];
            call_args[0] = DexValue::Ref(
                reinterpret_cast<DexObject*>(linker->NewString(m->signature)));
            call_args[1] = DexValue::Ref(nullptr);
            const DexValue type_obj = interp->Execute(from_desc, call_args, 2);
            if (interp->HasPendingException()) {
                // A signature this runtime cannot fully describe is not a reason to refuse
                // the handle: the method still resolves and still runs. Drop the type and
                // carry on, so type() reports null rather than the call failing.
                interp->ClearPendingException();
            } else {
                DexMethod* set_type = klass->FindVirtualMethod(
                    "setType", "(Ljava/lang/invoke/MethodType;)V");
                if (set_type != nullptr) {
                    DexValue set_args[2];
                    set_args[0] = DexValue::Ref(handle);
                    set_args[1] = type_obj;
                    interp->Execute(set_type, set_args, 2);
                    if (interp->HasPendingException()) interp->ClearPendingException();
                }
            }
        }
    }
    return handle;
}

// Read the DEX signature from a MethodType via Java.
std::string DescriptorFromMethodType(Interpreter* interp, DexObject* type_obj) {
    if (type_obj == nullptr) return std::string();
    DexClassLinker* linker = interp->linker();
    DexClass* klass = linker != nullptr ? linker->ClassOfObject(type_obj) : nullptr;
    if (klass == nullptr) return std::string();
    DexMethod* to_desc =
        klass->FindVirtualMethod("toMethodDescriptorString", "()Ljava/lang/String;");
    if (to_desc == nullptr) return std::string();
    DexValue arg = DexValue::Ref(type_obj);
    const DexValue str = interp->Execute(to_desc, &arg, 1);
    if (interp->HasPendingException()) {
        interp->ClearPendingException();
        return std::string();
    }
    const char* utf8 = GetStringUtf8(str);
    return utf8 != nullptr ? std::string(utf8) : std::string();
}

// Build the lookup signature; constructors force void return.
std::string LookupSignature(Interpreter* interp, DexObject* type_obj, bool force_void_return) {
    std::string desc = DescriptorFromMethodType(interp, type_obj);
    if (desc.empty()) return desc;
    if (!force_void_return) return desc;
    const std::size_t close = desc.find(')');
    if (close == std::string::npos) return desc;
    return desc.substr(0, close + 1) + "V";
}

bool Invoke_java_lang_invoke_MethodHandle(Interpreter* interp, const char* name,
                                          const DexValue* args, size_t num_args,
                                          DexValue* result) {
    if (std::strcmp(name, "invokeWithArguments") != 0) return false;

    DexObject* handle = num_args > 0 ? args[0].l : nullptr;
    if (handle == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;", "null method handle");
        return true;
    }
    DexMethod* target = reinterpret_cast<DexMethod*>(
        static_cast<uintptr_t>(GetLongField(handle, "artMethod")));
    if (target == nullptr) {
        interp->ThrowException("Ljava/lang/IllegalStateException;",
                               "method handle was never resolved");
        return true;
    }

    DexClass* handle_class = interp->linker()->ClassOfObject(handle);
    bool special = false;
    if (handle_class != nullptr) {
        if (DexField* f = handle_class->FindInstanceField("special", "Z")) {
            special = handle->GetField<uint8_t>(f->offset_or_slot) != 0;
        }
    }
    DexObject* bound = GetRefField(handle, "receiver", "Ljava/lang/Object;");
    auto* boxed = num_args > 1 ? reinterpret_cast<DexArray*>(args[1].l) : nullptr;

    // The receiver comes from bindTo when the handle is bound, otherwise from the first
    // element of the argument array - the platform's rule, and the reason the two cases
    // cannot share a single argument-count check.
    DexObject* receiver = bound;
    int32_t first_arg = 0;
    if (!target->IsStatic() && receiver == nullptr) {
        if (boxed == nullptr || boxed->length < 1) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   std::string("no receiver for ") +
                                       (target->name != nullptr ? target->name : "?"));
            return true;
        }
        receiver = reinterpret_cast<DexObject**>(boxed + 1)[0];
        first_arg = 1;
    }
    if (!target->IsStatic() && receiver == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;",
                               std::string("invoke ") +
                                   (target->name != nullptr ? target->name : "?") + " on null");
        return true;
    }

    // Virtual dispatch UNLESS the handle is special. This branch is the entire point of
    // unreflectSpecial: for a default method invoked on a Proxy, re-resolving against the
    // receiver's class would land back on the proxy's bodyless method and be forwarded to
    // the InvocationHandler that is asking for this call - unbounded recursion.
    DexMethod* resolved = target;
    if (!special && !target->IsStatic()) {
        if (DexClass* receiver_class = interp->linker()->ClassOfObject(receiver)) {
            if (DexMethod* found =
                    receiver_class->FindVirtualMethod(target->name, target->signature)) {
                resolved = found;
            }
        }
    }
    if (resolved->IsStatic()) interp->EnsureInitialized(resolved->declaring_class);

    // Unbox into the declared parameter types. The argument array may carry a leading
    // receiver, which BuildInvokeArgs does not expect, so the tail is copied out first.
    const std::vector<std::string> params = SplitParams(resolved->signature);
    const int32_t given = boxed != nullptr ? boxed->length - first_arg : 0;
    if (static_cast<size_t>(given < 0 ? 0 : given) != params.size()) {
        interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                               "wrong argument count for " +
                                   std::string(resolved->name != nullptr ? resolved->name : "?") +
                                   ": expected " + std::to_string(params.size()) + ", got " +
                                   std::to_string(given < 0 ? 0 : given));
        return true;
    }

    std::vector<DexValue> call_args;
    call_args.reserve(params.size() + 1);
    if (!resolved->IsStatic()) call_args.push_back(DexValue::Ref(receiver));
    auto** items = boxed != nullptr ? reinterpret_cast<DexObject**>(boxed + 1) : nullptr;
    for (size_t i = 0; i < params.size(); ++i) {
        DexValue v;
        if (!UnboxValue(interp, params[i].c_str(), items[first_arg + i], &v)) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   "argument " + std::to_string(i) + " is not a " + params[i]);
            return true;
        }
        call_args.push_back(v);
    }

    // A special handle on a method with no body is a genuine error rather than something
    // to forward: the caller asked for THIS method non-virtually and it has no code.
    // Saying so names the method, whereas letting Execute() fall through to proxy dispatch
    // would silently re-enter the handler.
    if (special && resolved->code_item == nullptr && !resolved->IsNative()) {
        interp->ThrowException("Ljava/lang/AbstractMethodError;",
                               (resolved->declaring_class != nullptr
                                    ? resolved->declaring_class->PrettyName() + "."
                                    : std::string()) +
                                   (resolved->name != nullptr ? resolved->name : "?") +
                                   " has no body to invoke directly");
        return true;
    }

    const DexValue ret = interp->Execute(resolved, call_args.data(), call_args.size());
    if (interp->HasPendingException()) return true;

    const char* ret_desc = ReturnDescriptor(resolved);
    result->l = (ret_desc[0] == 'V') ? nullptr : BoxValue(interp, ret_desc, ret);
    return true;
}

bool Invoke_java_lang_invoke_MethodHandles(Interpreter* interp, const char* name,
                                           const DexValue* args, size_t num_args,
                                           DexValue* result) {
    if (std::strcmp(name, "lookup") != 0) return false;

    DexClassLinker* linker = interp->linker();
    DexClass* lookup_class = linker->FindClass("Ljava/lang/invoke/MethodHandles$Lookup;");
    if (lookup_class == nullptr) {
        interp->ThrowException("Ljava/lang/InternalError;",
                               "MethodHandles$Lookup is missing from the framework");
        return true;
    }
    interp->EnsureInitialized(lookup_class);

    // The lookup class is the CALLER's, which the interpreter knows. Reporting a
    // placeholder would break a caller that passes the Lookup to in() or compares
    // lookupClass() against its own - and that comparison is how libraries decide whether
    // they need the reflective Lookup constructor at all.
    DexClass* caller = interp->CallerClass();
    DexObject* caller_class_obj =
        caller != nullptr ? reinterpret_cast<DexObject*>(linker->GetClassObject(caller))
                          : nullptr;

    DexObject* lookup = linker->AllocObject(lookup_class);
    if (lookup == nullptr) {
        interp->ThrowException("Ljava/lang/OutOfMemoryError;", "Lookup");
        return true;
    }
    DexMethod* ctor = lookup_class->FindDirectMethod("<init>", "(Ljava/lang/Class;I)V");
    if (ctor == nullptr) {
        interp->ThrowException("Ljava/lang/InternalError;",
                               "MethodHandles$Lookup(Class,int) is missing");
        return true;
    }
    // PUBLIC|PRIVATE|PROTECTED|PACKAGE: nothing in KuART enforces access, so a narrower
    // mode would only make callers take a fallback path for a restriction that is not real.
    DexValue call_args[3];
    call_args[0] = DexValue::Ref(lookup);
    call_args[1] = DexValue::Ref(caller_class_obj);
    call_args[2] = DexValue::Int(0x0001 | 0x0002 | 0x0004 | 0x0008);
    interp->Execute(ctor, call_args, 3);
    if (interp->HasPendingException()) return true;
    result->l = lookup;
    return true;
}

bool Invoke_java_lang_invoke_Lookup(Interpreter* interp, const char* name,
                                    const DexValue* args, size_t num_args,
                                    DexValue* result) {
    // unreflect / unreflectSpecial / unreflectConstructor: the Method or Constructor object
    // already carries the resolved DexMethod, so there is nothing to search for.
    const bool is_unreflect = std::strcmp(name, "unreflect") == 0;
    const bool is_unreflect_special = std::strcmp(name, "unreflectSpecial") == 0;
    const bool is_unreflect_ctor = std::strcmp(name, "unreflectConstructor") == 0;
    if (is_unreflect || is_unreflect_special || is_unreflect_ctor) {
        DexObject* member = num_args > 1 ? args[1].l : nullptr;
        if (member == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;",
                                   std::string(name) + " with null member");
            return true;
        }
        DexMethod* m = MethodFromObject(member);
        if (m == nullptr) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   std::string(name) + " argument is not a resolved member");
            return true;
        }
        // A constructor is always invoked non-virtually, and so is unreflectSpecial's
        // result; unreflect() keeps virtual dispatch.
        DexObject* handle =
            NewMethodHandle(interp, m, nullptr, is_unreflect_special || is_unreflect_ctor);
        if (handle == nullptr) return true;
        result->l = handle;
        return true;
    }

    const bool is_find_virtual = std::strcmp(name, "findVirtual") == 0;
    const bool is_find_static = std::strcmp(name, "findStatic") == 0;
    const bool is_find_special = std::strcmp(name, "findSpecial") == 0;
    const bool is_find_ctor = std::strcmp(name, "findConstructor") == 0;
    if (!is_find_virtual && !is_find_static && !is_find_special && !is_find_ctor) {
        return false;
    }

    DexClass* refc = ClassOf(interp, num_args > 1 ? args[1].l : nullptr);
    if (refc == nullptr) {
        interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                               std::string(name) + " with no target class");
        return true;
    }

    std::string member_name;
    DexObject* type_obj = nullptr;
    if (is_find_ctor) {
        member_name = "<init>";
        type_obj = num_args > 2 ? args[2].l : nullptr;
    } else {
        const char* n = num_args > 2 ? GetStringUtf8(args[2]) : nullptr;
        if (n == nullptr) {
            interp->ThrowException("Ljava/lang/NoSuchMethodException;",
                                   refc->PrettyName() + ".<null name>");
            return true;
        }
        member_name = n;
        type_obj = num_args > 3 ? args[3].l : nullptr;
    }

    const std::string signature = LookupSignature(interp, type_obj, is_find_ctor);
    if (signature.empty()) {
        interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                               std::string(name) + " with no usable MethodType");
        return true;
    }

    interp->EnsureInitialized(refc);
    DexMethod* m = nullptr;
    if (is_find_static || is_find_ctor) {
        m = refc->FindDirectMethod(member_name.c_str(), signature.c_str());
    } else {
        m = refc->FindVirtualMethod(member_name.c_str(), signature.c_str());
        // A private or final instance method lives among the direct methods, and both are
        // legitimate findVirtual/findSpecial targets.
        if (m == nullptr) m = refc->FindDirectMethod(member_name.c_str(), signature.c_str());
    }
    if (m == nullptr) {
        // Name the class, member and signature: "no such method" without them cannot be
        // acted on, and the signature is what is usually wrong.
        interp->ThrowException("Ljava/lang/NoSuchMethodException;",
                               refc->PrettyName() + "." + member_name + signature);
        return true;
    }

    DexObject* handle =
        NewMethodHandle(interp, m, nullptr, is_find_special || is_find_ctor);
    if (handle == nullptr) return true;
    result->l = handle;
    return true;
}


