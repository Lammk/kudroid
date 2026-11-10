bool Invoke_java_lang_System(Interpreter* interp, const char* name, const DexValue* args,
                             size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "currentTimeMillis") == 0) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
        *result = DexValue::Long(ms);
        return true;
    }
    if (std::strcmp(name, "nanoTime") == 0) {
        *result = DexValue::Long(guest_monotonic_ns());
        return true;
    }
    if (std::strcmp(name, "identityHashCode") == 0) {
        *result = DexValue::Int(static_cast<int32_t>(reinterpret_cast<uintptr_t>(args[0].l)));
        return true;
    }
    if (std::strcmp(name, "arraycopy") == 0) {
        DexObject* src_obj = args[0].l;
        int32_t src_pos = args[1].i;
        DexObject* dst_obj = args[2].l;
        int32_t dst_pos = args[3].i;
        int32_t length = args[4].i;

        if (src_obj == nullptr || dst_obj == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "arraycopy null array");
            return true;
        }
        if (!src_obj->clazz || !src_obj->clazz->is_array ||
            !dst_obj->clazz || !dst_obj->clazz->is_array) {
            interp->ThrowException("Ljava/lang/ArrayStoreException;", "arraycopy non-array argument");
            return true;
        }

        auto* src_arr = reinterpret_cast<DexArray*>(src_obj);
        auto* dst_arr = reinterpret_cast<DexArray*>(dst_obj);

        // Sum in 64 bits: src_pos + length wraps in int32 for a large length, which used
        // to pass the range test and then read past the end of the array.
        const int64_t src_end = static_cast<int64_t>(src_pos) + length;
        const int64_t dst_end = static_cast<int64_t>(dst_pos) + length;
        if (src_pos < 0 || dst_pos < 0 || length < 0 ||
            src_end > src_arr->length || dst_end > dst_arr->length) {
            interp->ThrowException("Ljava/lang/ArrayIndexOutOfBoundsException;", "arraycopy range invalid");
            return true;
        }

        uint32_t elem_size = interp->linker()->ElementSize(src_arr->clazz->component_type);
        uint8_t* src_data =
            reinterpret_cast<uint8_t*>(src_arr + 1) + static_cast<size_t>(src_pos) * elem_size;
        uint8_t* dst_data =
            reinterpret_cast<uint8_t*>(dst_arr + 1) + static_cast<size_t>(dst_pos) * elem_size;
        std::memmove(dst_data, src_data, static_cast<size_t>(length) * elem_size);
        return true;
    }
    if (std::strcmp(name, "getProperty") == 0 || std::strcmp(name, "getProperty0") == 0) {
        const std::string key = GetStringUtf8(args[0]);
        // Only the properties Android code actually reads; anything else is null
        // so callers fall back to their own defaults.
        const char* value = nullptr;
        if (key == "line.separator") {
            value = "\n";
        } else if (key == "file.separator") {
            value = "/";
        } else if (key == "path.separator") {
            value = ":";
        } else if (key == "java.io.tmpdir") {
            value = "/data/local/tmp";
        } else if (key == "user.dir" || key == "user.home") {
            value = "/";
        } else if (key == "java.vm.name") {
            // Report Dalvik; libraries branch on this for platform detection.
            value = "Dalvik";
        } else if (key == "java.vm.vendor") {
            value = "The Android Project";
        } else if (key == "java.vendor") {
            value = "The Android Project";
        } else if (key == "java.specification.name") {
            value = "Dalvik Core Library";
        } else if (key == "java.vm.specification.name") {
            value = "Dalvik Virtual Machine Specification";
        } else if (key == "java.vm.version") {
            value = "2.1.0";
        } else if (key == "java.version") {
            value = "0";
        } else if (key == "java.specification.version") {
            value = "0.9";
        } else if (key == "java.runtime.name") {
            value = "Android Runtime";
        } else if (key == "java.runtime.version") {
            value = "0.9";
        } else if (key == "os.name") {
            value = "Linux";
        } else if (key == "os.arch") {
            value = "aarch64";
        } else if (key == "os.version") {
            value = "5.15.0";
        } else if (key == "file.encoding") {
            value = "UTF-8";
        }
        result->l = value != nullptr ? interp->linker()->NewString(value) : nullptr;
        return true;
    }
    if (std::strcmp(name, "getenv") == 0) {
        const std::string key = GetStringUtf8(args[0]);
        const char* value = key.empty() ? nullptr : std::getenv(key.c_str());
        result->l = value != nullptr ? interp->linker()->NewString(value) : nullptr;
        return true;
    }
    if (std::strcmp(name, "exit") == 0) {
        // The guest shares the process with the iOS host, so exiting would kill
        // KuDroid itself. Ignore gracefully.
        std::fprintf(stderr, "[KuART][System] System.exit(%d) called -> ignored to prevent app shutdown\n", args[0].i);
        return true;
    }
    return false;
}

// java.lang.Math
bool Invoke_java_lang_Math(Interpreter* /*interp*/, const char* name, const DexValue* args,
                           size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "sin") == 0) { *result = DexValue::Double(std::sin(args[0].d)); return true; }
    if (std::strcmp(name, "cos") == 0) { *result = DexValue::Double(std::cos(args[0].d)); return true; }
    if (std::strcmp(name, "tan") == 0) { *result = DexValue::Double(std::tan(args[0].d)); return true; }
    if (std::strcmp(name, "asin") == 0) { *result = DexValue::Double(std::asin(args[0].d)); return true; }
    if (std::strcmp(name, "acos") == 0) { *result = DexValue::Double(std::acos(args[0].d)); return true; }
    if (std::strcmp(name, "atan") == 0) { *result = DexValue::Double(std::atan(args[0].d)); return true; }
    if (std::strcmp(name, "atan2") == 0) { *result = DexValue::Double(std::atan2(args[0].d, args[1].d)); return true; }
    if (std::strcmp(name, "sinh") == 0) { *result = DexValue::Double(std::sinh(args[0].d)); return true; }
    if (std::strcmp(name, "cosh") == 0) { *result = DexValue::Double(std::cosh(args[0].d)); return true; }
    if (std::strcmp(name, "tanh") == 0) { *result = DexValue::Double(std::tanh(args[0].d)); return true; }
    if (std::strcmp(name, "exp") == 0) { *result = DexValue::Double(std::exp(args[0].d)); return true; }
    if (std::strcmp(name, "log") == 0) { *result = DexValue::Double(std::log(args[0].d)); return true; }
    if (std::strcmp(name, "log10") == 0) { *result = DexValue::Double(std::log10(args[0].d)); return true; }
    if (std::strcmp(name, "sqrt") == 0) { *result = DexValue::Double(std::sqrt(args[0].d)); return true; }
    if (std::strcmp(name, "cbrt") == 0) { *result = DexValue::Double(std::cbrt(args[0].d)); return true; }
    if (std::strcmp(name, "pow") == 0) { *result = DexValue::Double(std::pow(args[0].d, args[1].d)); return true; }
    if (std::strcmp(name, "ceil") == 0) { *result = DexValue::Double(std::ceil(args[0].d)); return true; }
    if (std::strcmp(name, "floor") == 0) { *result = DexValue::Double(std::floor(args[0].d)); return true; }
    if (std::strcmp(name, "rint") == 0) { *result = DexValue::Double(std::rint(args[0].d)); return true; }
    if (std::strcmp(name, "hypot") == 0) { *result = DexValue::Double(std::hypot(args[0].d, args[1].d)); return true; }
    if (std::strcmp(name, "IEEEremainder") == 0) { *result = DexValue::Double(std::remainder(args[0].d, args[1].d)); return true; }
    if (std::strcmp(name, "randomImpl") == 0) {
        static std::mt19937_64 rng(std::random_device{}());
        static std::uniform_real_distribution<double> dist(0.0, 1.0);
        *result = DexValue::Double(dist(rng));
        return true;
    }
    return false;
}

// java.lang.Float / Double bit casts

// Double.toString per the Java spec: shortest decimal that round-trips, always
// with a fractional part, scientific notation only outside [1e-3, 1e7).
std::string FormatJavaDouble(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    if (d == 0.0) return std::signbit(d) ? "-0.0" : "0.0";

    const double magnitude = std::fabs(d);
    const bool scientific = magnitude < 1e-3 || magnitude >= 1e7;

    // %.17g always round-trips; shorter precisions are tried first so 0.1 prints
    // as "0.1" rather than "0.10000000000000001".
    char buf[64];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buf, sizeof(buf), scientific ? "%.*E" : "%.*g", precision - 1, d);
        if (std::strtod(buf, nullptr) == d) break;
    }
    std::string out(buf);

    if (!scientific) {
        // Java always shows a decimal point: "1" must become "1.0".
        if (out.find('.') == std::string::npos && out.find('e') == std::string::npos &&
            out.find('E') == std::string::npos) {
            out += ".0";
        }
        return out;
    }

    // Reshape C's "1.5E-05" into Java's "1.5E-5": no leading zeros, mantissa
    // always has a fractional digit.
    const size_t e = out.find('E');
    if (e == std::string::npos) return out;
    std::string mantissa = out.substr(0, e);
    std::string exponent = out.substr(e + 1);
    if (mantissa.find('.') == std::string::npos) mantissa += ".0";

    bool negative_exp = false;
    if (!exponent.empty() && (exponent[0] == '+' || exponent[0] == '-')) {
        negative_exp = exponent[0] == '-';
        exponent.erase(0, 1);
    }
    while (exponent.size() > 1 && exponent[0] == '0') exponent.erase(0, 1);
    return mantissa + "E" + (negative_exp ? "-" : "") + exponent;
}

bool Invoke_java_lang_Float(Interpreter* /*interp*/, const char* name, const DexValue* args,
                            size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "floatToIntBits") == 0 || std::strcmp(name, "floatToRawIntBits") == 0) {
        int32_t bits = 0;
        float f = args[0].f;
        std::memcpy(&bits, &f, sizeof(float));
        *result = DexValue::Int(bits);
        return true;
    }
    if (std::strcmp(name, "intBitsToFloat") == 0) {
        float f = 0.0f;
        int32_t bits = args[0].i;
        std::memcpy(&f, &bits, sizeof(float));
        *result = DexValue::Float(f);
        return true;
    }
    return false;
}

bool Invoke_java_lang_Double(Interpreter* interp, const char* name, const DexValue* args,
                             size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "doubleToLongBits") == 0 || std::strcmp(name, "doubleToRawLongBits") == 0) {
        int64_t bits = 0;
        double d = args[0].d;
        std::memcpy(&bits, &d, sizeof(double));
        *result = DexValue::Long(bits);
        return true;
    }
    if (std::strcmp(name, "longBitsToDouble") == 0) {
        double d = 0.0;
        int64_t bits = args[0].j;
        std::memcpy(&d, &bits, sizeof(double));
        *result = DexValue::Double(d);
        return true;
    }
    if (std::strcmp(name, "toString") == 0) {
        result->l = interp->linker()->NewString(FormatJavaDouble(args[0].d).c_str());
        return true;
    }
    if (std::strcmp(name, "parseDouble") == 0) {
        const char* str = GetStringUtf8(args[0]);
        // Java throws instead of returning 0 for garbage, and accepts the
        // Infinity/NaN spellings that strtod does not.
        while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') ++str;
        std::string text(str);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                                 text.back() == '\n' || text.back() == '\r')) {
            text.pop_back();
        }
        if (!text.empty() && (text.back() == 'f' || text.back() == 'F' ||
                              text.back() == 'd' || text.back() == 'D')) {
            text.pop_back();
        }
        if (text == "NaN") {
            *result = DexValue::Double(std::nan(""));
            return true;
        }
        if (text == "Infinity" || text == "+Infinity") {
            *result = DexValue::Double(HUGE_VAL);
            return true;
        }
        if (text == "-Infinity") {
            *result = DexValue::Double(-HUGE_VAL);
            return true;
        }
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (text.empty() || end == text.c_str() || *end != '\0') {
            interp->ThrowException("Ljava/lang/NumberFormatException;",
                                   "For input string: \"" + text + "\"");
            return true;
        }
        *result = DexValue::Double(value);
        return true;
    }
    return false;
}

// java.lang.String

// Allocates a Java char[] holding `units`.
DexArray* NewCharArray(DexClassLinker* linker, const std::vector<uint16_t>& units) {
    DexClass* klass = linker->FindClass("[C");
    if (klass == nullptr) return nullptr;
    DexArray* arr = linker->AllocArray(klass, static_cast<int32_t>(units.size()));
    if (arr == nullptr) return nullptr;
    auto* data = reinterpret_cast<uint16_t*>(arr + 1);
    for (size_t i = 0; i < units.size(); ++i) data[i] = units[i];
    return arr;
}

DexArray* NewByteArray(DexClassLinker* linker, const char* bytes, size_t count) {
    DexClass* klass = linker->FindClass("[B");
    if (klass == nullptr) return nullptr;
    DexArray* arr = linker->AllocArray(klass, static_cast<int32_t>(count));
    if (arr == nullptr) return nullptr;
    std::memcpy(reinterpret_cast<uint8_t*>(arr + 1), bytes, count);
    return arr;
}

bool Invoke_java_lang_String(Interpreter* interp, const DexMethod* method,
                             const char* name, const DexValue* args, size_t num_args,
                             DexValue* result) {
    const char* signature = method->signature != nullptr ? method->signature : "";
    DexClassLinker* linker = interp->linker();

    // Constructors: the object exists, this fills in its payload.
    if (std::strcmp(name, "initEmpty") == 0) {
        InitStringFrom(linker, args[0].l, std::string());
        return true;
    }
    if (std::strcmp(name, "initCopy") == 0) {
        InitStringFrom(linker, args[0].l, GetStringUtf8(args[1]));
        return true;
    }
    if (std::strcmp(name, "initChars") == 0) {
        auto* chars = reinterpret_cast<DexArray*>(args[1].l);
        const int32_t offset = args[2].i;
        const int32_t count = args[3].i;
        if (chars == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "char[] null");
            return true;
        }
        if (offset < 0 || count < 0 || offset > chars->length - count) {
            interp->ThrowException("Ljava/lang/StringIndexOutOfBoundsException;",
                                   "offset " + std::to_string(offset) + " count " +
                                       std::to_string(count));
            return true;
        }
        const auto* data = reinterpret_cast<const uint16_t*>(chars + 1);
        InitStringFrom(linker, args[0].l,
                       Utf16ToUtf8(data + offset, static_cast<size_t>(count)));
        return true;
    }
    if (std::strcmp(name, "initBytes") == 0) {
        auto* bytes = reinterpret_cast<DexArray*>(args[1].l);
        const int32_t offset = args[2].i;
        const int32_t count = args[3].i;
        if (bytes == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "byte[] null");
            return true;
        }
        if (offset < 0 || count < 0 || offset > bytes->length - count) {
            interp->ThrowException("Ljava/lang/StringIndexOutOfBoundsException;",
                                   "offset " + std::to_string(offset));
            return true;
        }
        // Bytes are taken as UTF-8 directly; KuART stores strings as UTF-8 so no
        // charset decoding step is needed for the only encoding Android uses.
        const auto* data = reinterpret_cast<const char*>(bytes + 1);
        InitStringFrom(linker, args[0].l,
                       std::string(data + offset, static_cast<size_t>(count)));
        return true;
    }

    DexString* self = AsString(args[0]);
    if (self == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;", "null string receiver");
        return true;
    }
    if (linker != nullptr) {
        DexClass* str_class = linker->FindClass("Ljava/lang/String;");
        if (str_class != nullptr && self->clazz != str_class) {
            interp->ThrowException("Ljava/lang/ClassCastException;",
                                   "receiver is not java.lang.String");
            return true;
        }
    }

    if (std::strcmp(name, "intern") == 0) {
        result->l = linker->InternString(std::string(self->utf8 ? self->utf8 : "", self->length));
        return true;
    }
    if (std::strcmp(name, "length") == 0) {
        *result = DexValue::Int(self->ascii ? static_cast<int32_t>(self->length)
                                           : static_cast<int32_t>(Utf16Length(
                                                 self->utf8, self->length)));
        return true;
    }

    Utf16View view(self);

    if (std::strcmp(name, "charAt") == 0) {
        const int32_t index = args[1].i;
        if (index < 0 || static_cast<uint32_t>(index) >= view.length()) {
            interp->ThrowException("Ljava/lang/StringIndexOutOfBoundsException;",
                                   "charAt " + std::to_string(index));
            return true;
        }
        *result = DexValue::Int(view.at(static_cast<uint32_t>(index)));
        return true;
    }
    if (std::strcmp(name, "indexOf") == 0 || std::strcmp(name, "lastIndexOf") == 0) {
        const bool last = name[0] == 'l';
        // Two overloads share the name: (int ch, int from) and (String, int from).
        // Only the signature can tell them apart - an int register holding 'l'
        // looks exactly like a reference.
        const bool by_string = std::strncmp(signature, "(Ljava/lang/String;", 19) == 0;
        const int32_t from = num_args > 2 ? args[2].i : 0;
        const int32_t len = static_cast<int32_t>(view.length());

        if (!by_string) {
            const auto needle = static_cast<uint16_t>(args[1].i);
            int32_t found = -1;
            if (last) {
                for (int32_t i = std::min(from, len - 1); i >= 0; --i) {
                    if (view.at(static_cast<uint32_t>(i)) == needle) {
                        found = i;
                        break;
                    }
                }
            } else {
                for (int32_t i = std::max(from, 0); i < len; ++i) {
                    if (view.at(static_cast<uint32_t>(i)) == needle) {
                        found = i;
                        break;
                    }
                }
            }
            *result = DexValue::Int(found);
            return true;
        }

        if (args[1].l == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "needle null");
            return true;
        }
        if (linker != nullptr) {
            DexClass* str_class = linker->FindClass("Ljava/lang/String;");
            if (str_class != nullptr && args[1].l->clazz != str_class) {
                interp->ThrowException("Ljava/lang/ClassCastException;",
                                       "needle is not java.lang.String");
                return true;
            }
        }
        Utf16View needle(AsString(args[1]));
        const int32_t nlen = static_cast<int32_t>(needle.length());
        int32_t found = -1;
        if (nlen == 0) {
            found = last ? std::min(from, len) : std::min(std::max(from, 0), len);
        } else if (last) {
            for (int32_t i = std::min(from, len - nlen); i >= 0; --i) {
                bool match = true;
                for (int32_t k = 0; k < nlen; ++k) {
                    if (view.at(static_cast<uint32_t>(i + k)) !=
                        needle.at(static_cast<uint32_t>(k))) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    found = i;
                    break;
                }
            }
        } else {
            for (int32_t i = std::max(from, 0); i + nlen <= len; ++i) {
                bool match = true;
                for (int32_t k = 0; k < nlen; ++k) {
                    if (view.at(static_cast<uint32_t>(i + k)) !=
                        needle.at(static_cast<uint32_t>(k))) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    found = i;
                    break;
                }
            }
        }
        *result = DexValue::Int(found);
        return true;
    }
    if (std::strcmp(name, "substring") == 0) {
        const int32_t begin = num_args > 1 ? args[1].i : 0;
        const int32_t len = static_cast<int32_t>(view.length());
        const int32_t end = num_args > 2 ? args[2].i : len;
        if (begin < 0 || end > len || begin > end) {
            interp->ThrowException("Ljava/lang/StringIndexOutOfBoundsException;",
                                   "substring(" + std::to_string(begin) + ", " +
                                       std::to_string(end) + ") len " +
                                       std::to_string(len));
            return true;
        }
        result->l = linker->NewString(
            view.SubstringUtf8(static_cast<uint32_t>(begin), static_cast<uint32_t>(end)));
        return true;
    }
    if (std::strcmp(name, "concat") == 0) {
        std::string out(self->utf8 ? self->utf8 : "", self->length);
        DexString* other = AsString(args[1]);
        if (!other) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "concat null");
            return true;
        }
        out.append(other->utf8, other->length);
        result->l = linker->NewString(out);
        return true;
    }
    if (std::strcmp(name, "replace") == 0) {
        const bool by_char = std::strncmp(signature, "(CC)", 4) == 0;
        if (by_char) {
            const auto from = static_cast<uint16_t>(args[1].i);
            const auto to = static_cast<uint16_t>(args[2].i);
            std::vector<uint16_t> units;
            units.reserve(view.length());
            for (uint32_t i = 0; i < view.length(); ++i) {
                const uint16_t c = view.at(i);
                units.push_back(c == from ? to : c);
            }
            result->l = linker->NewString(Utf16ToUtf8(units.data(), units.size()));
            return true;
        }
        const std::string haystack(self->utf8 ? self->utf8 : "", self->length);
        const std::string target = GetStringUtf8(args[1]);
        const std::string replacement = GetStringUtf8(args[2]);
        if (target.empty()) {
            result->l = linker->NewString(haystack);
            return true;
        }
        std::string out;
        size_t pos = 0;
        while (true) {
            const size_t hit = haystack.find(target, pos);
            if (hit == std::string::npos) {
                out.append(haystack, pos, std::string::npos);
                break;
            }
            out.append(haystack, pos, hit - pos);
            out += replacement;
            pos = hit + target.size();
        }
        result->l = linker->NewString(out);
        return true;
    }
    if (std::strcmp(name, "toLowerCase") == 0 || std::strcmp(name, "toUpperCase") == 0) {
        const bool upper = name[2] == 'U';
        std::string language;
        if (std::strcmp(signature, "(Ljava/util/Locale;)Ljava/lang/String;") == 0) {
            if (num_args < 2 || args[1].l == nullptr) {
                interp->ThrowException("Ljava/lang/NullPointerException;", "locale == null");
                return true;
            }
            auto* localeLanguage = reinterpret_cast<DexString*>(
                GetRefField(args[1].l, "language", "Ljava/lang/String;"));
            if (localeLanguage != nullptr && localeLanguage->utf8 != nullptr)
                language.assign(localeLanguage->utf8, localeLanguage->length);
        }
        std::vector<uint16_t> units;
        units.reserve(view.length());
        for (uint32_t i = 0; i < view.length(); ++i) units.push_back(view.at(i));
#if defined(__APPLE__)
        CFMutableStringRef mapped = CFStringCreateMutable(kCFAllocatorDefault, 0);
        CFStringAppendCharacters(mapped, units.data(), units.size());
        CFStringRef identifier = CFStringCreateWithCString(kCFAllocatorDefault,
            language.c_str(), kCFStringEncodingUTF8);
        CFLocaleRef locale = CFLocaleCreate(kCFAllocatorDefault, identifier);
        if (upper) CFStringUppercase(mapped, locale);
        else CFStringLowercase(mapped, locale);
        units.resize(CFStringGetLength(mapped));
        if (!units.empty()) CFStringGetCharacters(mapped,
            CFRangeMake(0, units.size()), units.data());
        CFRelease(locale);
        CFRelease(identifier);
        CFRelease(mapped);
#else
        // Non-Apple hosts retain the existing ASCII mapping.
        for (uint16_t& c : units) {
            if (upper && c >= 'a' && c <= 'z') c -= 32;
            else if (!upper && c >= 'A' && c <= 'Z') c += 32;
        }
#endif
        result->l = linker->NewString(Utf16ToUtf8(units.data(), units.size()));
        return true;
    }
    if (std::strcmp(name, "trim") == 0) {
        // Java trims every code unit <= ' ', not just the usual whitespace set.
        uint32_t begin = 0;
        uint32_t end = view.length();
        while (begin < end && view.at(begin) <= ' ') ++begin;
        while (end > begin && view.at(end - 1) <= ' ') --end;
        result->l = linker->NewString(view.SubstringUtf8(begin, end));
        return true;
    }
    if (std::strcmp(name, "equalsIgnoreCase") == 0) {
        DexString* other = AsString(args[1]);
        DexClass* str_class = linker != nullptr ? linker->FindClass("Ljava/lang/String;") : nullptr;
        if (other == nullptr || (str_class != nullptr && other->clazz != str_class)) {
            *result = DexValue::Int(0);
            return true;
        }
        Utf16View rhs(other);
        if (view.length() != rhs.length()) {
            *result = DexValue::Int(0);
            return true;
        }
        bool equal = true;
        for (uint32_t i = 0; i < view.length(); ++i) {
            uint16_t a = view.at(i);
            uint16_t b = rhs.at(i);
            if (a >= 'A' && a <= 'Z') a = static_cast<uint16_t>(a + 32);
            if (b >= 'A' && b <= 'Z') b = static_cast<uint16_t>(b + 32);
            if (a != b) {
                equal = false;
                break;
            }
        }
        *result = DexValue::Int(equal ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "compareTo") == 0) {
        DexString* other = AsString(args[1]);
        if (other == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "compareTo null");
            return true;
        }
        DexClass* str_class = linker != nullptr ? linker->FindClass("Ljava/lang/String;") : nullptr;
        if (str_class != nullptr && other->clazz != str_class) {
            interp->ThrowException("Ljava/lang/ClassCastException;", "compareTo non-string");
            return true;
        }
        Utf16View rhs(other);
        const uint32_t shared = std::min(view.length(), rhs.length());
        int32_t diff = 0;
        for (uint32_t i = 0; i < shared; ++i) {
            diff = static_cast<int32_t>(view.at(i)) - static_cast<int32_t>(rhs.at(i));
            if (diff != 0) break;
        }
        if (diff == 0) {
            diff = static_cast<int32_t>(view.length()) - static_cast<int32_t>(rhs.length());
        }
        *result = DexValue::Int(diff);
        return true;
    }
    if (std::strcmp(name, "getBytes") == 0) {
        if (self->ascii) {
            result->l = NewByteArray(linker, self->utf8 ? self->utf8 : "", self->length);
            return true;
        }
        std::string bytes;
        for (uint32_t i = 0; i < view.length(); ++i) {
            uint32_t cp = view.at(i);
            if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < view.length()
                && view.at(i + 1) >= 0xdc00 && view.at(i + 1) <= 0xdfff) {
                cp = 0x10000 + ((cp - 0xd800) << 10) + view.at(++i) - 0xdc00;
            } else if (cp >= 0xd800 && cp <= 0xdfff) {
                cp = '?';
            }
            AppendUtf8(&bytes, cp);
        }
        result->l = NewByteArray(linker, bytes.data(), bytes.size());
        return true;
    }
    if (std::strcmp(name, "toCharArray") == 0) {
        std::vector<uint16_t> units;
        units.reserve(view.length());
        for (uint32_t i = 0; i < view.length(); ++i) units.push_back(view.at(i));
        result->l = NewCharArray(linker, units);
        return true;
    }
    return false;
}

// java.lang.Thread

// Thread that is currently running bytecode. Set on entry to a Java thread body
// so currentThread() can return the right object without a thread-id map.
thread_local DexObject* t_current_thread = nullptr;

// The main thread has no java.lang.Thread object until something asks for one.
DexObject* MainThreadObject(Interpreter* interp) {
    DexClass* klass = interp->linker()->FindClass("Ljava/lang/Thread;");
    if (klass == nullptr || !interp->EnsureInitialized(klass)) return nullptr;
    DexField* field = klass->FindStaticField("mainThread", "Ljava/lang/Thread;");
    if (field == nullptr) return nullptr;
    DexValue& slot = klass->static_values[field->offset_or_slot];
    if (slot.l == nullptr) {
        slot.l = interp->linker()->AllocObject(klass);
        if (slot.l != nullptr) {
            SetRefField(slot.l, "name", "Ljava/lang/String;", interp->linker()->NewString("main"));
        }
    }
    return slot.l;
}

DexObject* CurrentThreadObject(Interpreter* interp) {
    return t_current_thread != nullptr ? t_current_thread : MainThreadObject(interp);
}

DexObject* ThreadParkLock(Interpreter* interp, DexObject* thread) {
    DexObject* lock = GetRefField(thread, "parkLock", "Ljava/lang/Object;");
    if (lock == nullptr) {
        DexClass* klass = interp->linker()->FindClass("Ljava/lang/Object;");
        lock = klass != nullptr ? interp->linker()->AllocObject(klass) : nullptr;
        if (lock == nullptr) {
            interp->ThrowException("Ljava/lang/OutOfMemoryError;", "thread park lock");
            return nullptr;
        }
        SetRefField(thread, "parkLock", "Ljava/lang/Object;", lock);
    }
    return lock;
}

bool ThreadIsInterrupted(DexObject* thread) {
    if (thread == nullptr || thread->clazz == nullptr) return false;
    DexField* field = thread->clazz->FindInstanceField("interrupted", "Z");
    return field != nullptr && thread->GetField<uint8_t>(field->offset_or_slot) != 0;
}

bool ClearThreadInterrupted(DexObject* thread) {
    if (!ThreadIsInterrupted(thread)) return false;
    DexField* field = thread->clazz->FindInstanceField("interrupted", "Z");
    thread->SetField<uint8_t>(field->offset_or_slot, 0);
    return true;
}

void UnparkThread(Interpreter* interp, DexObject* thread) {
    if (thread == nullptr) return;
    DexObject* lock = ThreadParkLock(interp, thread);
    if (lock == nullptr) return;
    Monitor::Enter(lock);
    SetLongField(thread, "parkPermit", 1);
    Monitor::Notify(lock, true);
    Monitor::Exit(lock);
}

void ParkThread(Interpreter* interp, bool absolute, int64_t time,
                DexObject* blocker, bool publish_blocker) {
    if (!absolute && time < 0) return;
    DexObject* thread = CurrentThreadObject(interp);
    if (thread == nullptr) return;
    DexObject* lock = ThreadParkLock(interp, thread);
    if (lock == nullptr) return;
    Monitor::Enter(lock);
    // Use a private monitor so parking retains every caller-owned monitor.
    struct MonitorExit {
        DexObject* lock;
        ~MonitorExit() { Monitor::Exit(lock); }
    } release{lock};
    if (GetLongField(thread, "parkPermit") != 0) {
        SetLongField(thread, "parkPermit", 0);
        return;
    }
    if (ThreadIsInterrupted(thread)) return;
    int64_t millis = time / 1000000;
    int32_t nanos = static_cast<int32_t>(time % 1000000);
    if (absolute) {
        const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (time <= now) return;
        millis = time - now;
        nanos = 0;
    }
    if (publish_blocker) SetRefField(thread, "parkBlocker", "Ljava/lang/Object;", blocker);
    // Record the first blocking stack on each worker for startup diagnostics.
    static thread_local bool traced = false;
    if (!traced) {
        traced = true;
        const std::string stack = interp->BuildStackTrace();
        std::fprintf(stderr, "[KuART][PARK] first wait absolute=%d time=%lld\n%s",
                     absolute ? 1 : 0, static_cast<long long>(time), stack.c_str());
    }
    Monitor::Wait(lock, millis, nanos);
    SetLongField(thread, "parkPermit", 0);
    if (publish_blocker) SetRefField(thread, "parkBlocker", "Ljava/lang/Object;", nullptr);
}

bool Invoke_java_util_concurrent_locks_LockSupport(Interpreter* interp, const char* name,
                                                   const DexValue* args, DexValue* result) {
    if (std::strcmp(name, "nativePark") == 0) {
        ParkThread(interp, args[2].i != 0, args[3].j, args[0].l, args[1].i != 0);
        return true;
    }
    if (std::strcmp(name, "unpark") == 0) {
        UnparkThread(interp, args[0].l);
        return true;
    }
    if (std::strcmp(name, "getBlocker") == 0) {
        if (args[0].l == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "thread == null");
        } else {
            result->l = GetRefField(args[0].l, "parkBlocker", "Ljava/lang/Object;");
        }
        return true;
    }
    if (std::strcmp(name, "setCurrentBlocker") == 0) {
        SetRefField(CurrentThreadObject(interp), "parkBlocker", "Ljava/lang/Object;", args[0].l);
        return true;
    }
    return false;
}

// Throwable stack traces. nativeFillInStackTrace runs inside Throwable.fillInStackTrace,
// so the capture skips the exception's own constructor frames.
bool Invoke_java_lang_Throwable(Interpreter* interp, const char* name, const DexValue* args,
                                DexValue* result) {
    if (std::strcmp(name, "nativeFillInStackTrace") == 0) {
        result->l = interp->CaptureBacktrace(true);
        return true;
    }
    if (std::strcmp(name, "nativeGetStackTrace") == 0) {
        result->l = interp->MaterializeBacktrace(reinterpret_cast<DexArray*>(args[0].l));
        return true;
    }
    return false;
}

bool Invoke_java_lang_Thread(Interpreter* interp, const char* name, const DexValue* args,
                             size_t num_args, DexValue* result) {
    if (std::strcmp(name, "currentThread") == 0) {
        result->l = t_current_thread != nullptr ? t_current_thread : MainThreadObject(interp);
        return true;
    }
    if (std::strcmp(name, "sleep") == 0) {
        const int64_t ms = args[0].j;
        const int32_t nanos = num_args > 1 ? args[1].i : 0;
        if (ms < 0 || nanos < 0 || nanos > 999999) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;", "invalid sleep timeout");
            return true;
        }
        DexObject* thread = CurrentThreadObject(interp);
        if (ClearThreadInterrupted(thread)) {
            interp->ThrowException("Ljava/lang/InterruptedException;", "sleep interrupted");
            return true;
        }
        if (ms > 0 || nanos > 0) {
            DexObject* lock = ThreadParkLock(interp, thread);
            if (lock == nullptr) return true;
            Monitor::Enter(lock);
            const auto start = std::chrono::steady_clock::now();
            int64_t remaining_ms = ms;
            int32_t remaining_ns = nanos;
            do {
                Monitor::Wait(lock, remaining_ms, remaining_ns, thread);
                if (ThreadIsInterrupted(thread)) break;
                const int64_t elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start).count();
                const int64_t elapsed_ms = elapsed / 1000000;
                const int32_t elapsed_ns = static_cast<int32_t>(elapsed % 1000000);
                if (elapsed_ms > ms || (elapsed_ms == ms && elapsed_ns >= nanos)) break;
                remaining_ms = ms - elapsed_ms;
                remaining_ns = nanos - elapsed_ns;
                if (remaining_ns < 0) { --remaining_ms; remaining_ns += 1000000; }
            } while (true);
            Monitor::Exit(lock);
            if (ClearThreadInterrupted(thread)) {
                interp->ThrowException("Ljava/lang/InterruptedException;", "sleep interrupted");
            }
        }
        return true;
    }
    if (std::strcmp(name, "yield") == 0) {
        VmLockRelease unlocked;
        std::this_thread::yield();
        return true;
    }
    if (std::strcmp(name, "nativeInterrupt") == 0) {
        Monitor::Interrupt(args[0].l);
        DexObject* lock = GetRefField(args[0].l, "parkLock", "Ljava/lang/Object;");
        if (lock != nullptr) {
            Monitor::Enter(lock);
            Monitor::Notify(lock, true);
            Monitor::Exit(lock);
        }
        return true;
    }
    if (std::strcmp(name, "nativeStart") == 0) {
        DexObject* self = args[0].l;
        if (self == nullptr || self->clazz == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "thread null");
            return true;
        }
        DexMethod* body = self->clazz->FindDirectMethod("runFromNative", "()V");
        if (body == nullptr) body = self->clazz->FindVirtualMethod("runFromNative", "()V");
        if (body == nullptr) {
            interp->ThrowException("Ljava/lang/NoSuchMethodError;", "runFromNative");
            return true;
        }

        // Read on the starting thread, which holds the VM lock.
        const int32_t priority = GetIntField(self, "priority", 5);

        // A Java thread runs interpreter frames and whatever guest code its natives
        // call (an audio mixer's DSP graph, for one) on one host stack. The host
        // default of 512KB overflows there and the guard page raises a fault inside
        // the guest. Match the stack guest pthreads get.
        struct ThreadStart {
            Interpreter* interp;
            DexObject* self;
            DexMethod* body;
            int32_t priority;
        };
        constexpr size_t kJavaThreadStackSize = 16u * 1024u * 1024u;
        auto* start = new ThreadStart{interp, self, body, priority};
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, kJavaThreadStackSize);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_t worker;
        const int rc = pthread_create(
            &worker, &attr,
            [](void* raw) -> void* {
                std::unique_ptr<ThreadStart> t(static_cast<ThreadStart*>(raw));
                ApplyHostThreadPriority(t->priority);
                t_current_thread = t->self;
                DexValue arg = DexValue::Ref(t->self);
                // Execute takes the VM lock itself at depth 0.
                t->interp->Execute(t->body, &arg, 1);
                t->interp->ClearPendingException();
                return nullptr;
            },
            start);
        pthread_attr_destroy(&attr);
        if (rc != 0) {
            delete start;
            interp->ThrowException("Ljava/lang/OutOfMemoryError;", "pthread_create failed");
            return true;
        }
        SetLongField(self, "nativePeer",
                     static_cast<int64_t>(reinterpret_cast<uintptr_t>(worker)));
        return true;
    }
    return false;
}

// java.io.File

// The path lives in the `path` field written by File's Java constructor.
std::string GetFilePath(const DexValue& file_obj) {
    if (file_obj.l == nullptr) return "";
    DexClass* klass = file_obj.l->clazz;
    if (!klass) return "";
    DexField* f_path = klass->FindInstanceField("path", "Ljava/lang/String;");
    if (!f_path) f_path = klass->FindInstanceField("mPath", "Ljava/lang/String;");
    if (!f_path) return "";
    DexObject* path_str_obj = file_obj.l->GetField<DexObject*>(f_path->offset_or_slot);
    if (!path_str_obj) return "";
    auto* str = reinterpret_cast<DexString*>(path_str_obj);
    return str->utf8 ? str->utf8 : "";
}

// Remap Java paths like native code so both see the same files.
std::string RemapJavaPath(const std::string& path) {
    if (path.empty()) return path;
    return VFSPathRemapper::getInstance().remap(path.c_str());
}

