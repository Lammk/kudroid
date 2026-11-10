bool Invoke_android_graphics_Canvas(Interpreter* interp, const char* name, const DexValue* args,
                                    size_t num_args, DexValue* result) {
    if (std::strcmp(name, "native_filterColor") == 0) {
        result->i = static_cast<int32_t>(FilterPixelColor(args[0].i, args[1].i, args[2].i));
        return true;
    }
    if (std::strcmp(name, "native_blitBitmap") == 0) {
        if (num_args < 21) return false;
        auto* target = reinterpret_cast<DexArray*>(args[0].l);
        auto* source = reinterpret_cast<DexArray*>(args[3].l);
        const int tw = args[1].i, th = args[2].i, sw = args[4].i, sh = args[5].i;
        const int sx = args[6].i, sy = args[7].i, width = args[8].i, height = args[9].i;
        if (target == nullptr || source == nullptr || tw <= 0 || th <= 0 || sw <= 0 || sh <= 0
                || static_cast<int64_t>(tw) * th > target->length || static_cast<int64_t>(sw) * sh > source->length
                || sx < 0 || sy < 0 || width <= 0 || height <= 0
                || static_cast<int64_t>(sx) + width > sw || static_cast<int64_t>(sy) + height > sh) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;", "invalid bitmap region");
            return true;
        }
        const float dx = args[10].f, dy = args[11].f, dw = args[12].f, dh = args[13].f;
        if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dw) || !std::isfinite(dh)
                || dw == 0 || dh == 0) return true;
        const int left = static_cast<int>(std::clamp(std::ceil(std::min(dx, dx + dw)), 0.0f, static_cast<float>(tw)));
        const int top = static_cast<int>(std::clamp(std::ceil(std::min(dy, dy + dh)), 0.0f, static_cast<float>(th)));
        const int right = static_cast<int>(std::clamp(std::ceil(std::max(dx, dx + dw)), 0.0f, static_cast<float>(tw)));
        const int bottom = static_cast<int>(std::clamp(std::ceil(std::max(dy, dy + dh)), 0.0f, static_cast<float>(th)));
        const int x0 = std::max(left, args[14].i), y0 = std::max(top, args[15].i);
        const int x1 = std::min(right, args[16].i), y1 = std::min(bottom, args[17].i);
        if (x0 >= x1 || y0 >= y1) return true;
        const auto* pixels = reinterpret_cast<const uint32_t*>(source->Data());
        std::vector<uint32_t> snapshot;
        if (source == target) { snapshot.assign(pixels, pixels + source->length); pixels = snapshot.data(); }
        auto* output = reinterpret_cast<uint32_t*>(target->Data());
        for (int y = y0; y < y1; ++y) {
            const int py = sy + static_cast<int>(std::clamp((y + 0.5 - dy) / dh * height, 0.0, static_cast<double>(height - 1)));
            for (int x = x0; x < x1; ++x) {
                const int px = sx + static_cast<int>(std::clamp((x + 0.5 - dx) / dw * width, 0.0, static_cast<double>(width - 1)));
                const uint32_t pixel = ModulatePixelAlpha(FilterPixelColor(pixels[py * sw + px], args[19].i, args[20].i), args[18].i);
                const uint32_t sa = pixel >> 24;
                if (sa == 0) continue;
                uint32_t& dst = output[y * tw + x];
                if (sa == 255) { dst = pixel; continue; }
                const uint32_t da = dst >> 24, inverse = 255 - sa, denominator = sa * 255 + da * inverse;
                auto mix = [&](int shift) {
                    return (((pixel >> shift) & 255) * sa * 255 + ((dst >> shift) & 255) * da * inverse + denominator / 2) / denominator;
                };
                dst = ((denominator + 127) / 255) << 24 | mix(16) << 16 | mix(8) << 8 | mix(0);
            }
        }
        return true;
    }
    if (std::strcmp(name, "native_drawColor") == 0) {
        JavaCanvasRenderer::getInstance().drawColor(static_cast<uint32_t>(args[0].i));
        return true;
    }
    if (std::strcmp(name, "native_drawRect") == 0) {
        JavaCanvasRenderer::getInstance().drawRect(
            args[0].f, args[1].f, args[2].f, args[3].f,
            static_cast<uint32_t>(args[4].i));
        return true;
    }
    if (std::strcmp(name, "native_drawText") == 0) {
        const char* text = GetStringUtf8(args[0]);
        JavaCanvasRenderer::getInstance().drawText(
            text, args[1].f, args[2].f, static_cast<uint32_t>(args[3].i),
            args[4].f);
        return true;
    }
    if (std::strcmp(name, "native_drawBitmapText") == 0) {
        auto* pixels = reinterpret_cast<DexArray*>(args[0].l);
        const int width = args[1].i, height = args[2].i;
        if (pixels != nullptr && width > 0 && height > 0 &&
            static_cast<int64_t>(width) * height <= pixels->length) {
            JavaCanvasRenderer::drawBitmapText(reinterpret_cast<uint32_t*>(pixels->Data()),
                width, height, GetStringUtf8(args[3]), args[4].f, args[5].f,
                static_cast<uint32_t>(args[6].i), args[7].f, args[8].f, args[9].f,
                args[10].f, static_cast<uint32_t>(args[11].i));
        }
        return true;
    }
    if (std::strcmp(name, "native_drawBitmap") == 0) {
        auto* arr = reinterpret_cast<DexArray*>(args[0].l);
        if (num_args >= 5 && arr != nullptr && args[1].i > 0 && args[2].i > 0
                && static_cast<int64_t>(args[1].i) * args[2].i <= arr->length) {
            auto* pixels = reinterpret_cast<uint32_t*>(arr + 1);
            JavaCanvasRenderer::getInstance().drawBitmap(
                pixels, args[1].i, args[2].i, args[3].f, args[4].f,
                num_args >= 8 ? args[5].i : 255, num_args >= 8 ? args[6].i : 0, num_args >= 8 ? args[7].i : -1);
        }
        return true;
    }
    if (std::strcmp(name, "native_drawBitmapRegion") == 0) {
        auto* arr = reinterpret_cast<DexArray*>(args[0].l);
        if (num_args >= 11 && arr != nullptr && args[1].i > 0 && args[2].i > 0
                && static_cast<int64_t>(args[1].i) * args[2].i <= arr->length
                && args[3].i >= 0 && args[4].i >= 0 && args[5].i > 0 && args[6].i > 0
                && static_cast<int64_t>(args[3].i) + args[5].i <= args[1].i
                && static_cast<int64_t>(args[4].i) + args[6].i <= args[2].i) {
            auto* pixels = reinterpret_cast<uint32_t*>(arr + 1);
            JavaCanvasRenderer::getInstance().drawBitmapRegion(
                pixels, args[1].i, args[2].i, args[3].i, args[4].i,
                args[5].i, args[6].i, args[7].f, args[8].f, args[9].f, args[10].f,
                num_args >= 14 ? args[11].i : 255, num_args >= 14 ? args[12].i : 0, num_args >= 14 ? args[13].i : -1);
        }
        return true;
    }
    if (std::strcmp(name, "native_drawRoundRect") == 0) {
        JavaCanvasRenderer::getInstance().drawRoundRect(
            args[0].f, args[1].f, args[2].f, args[3].f,
            args[4].f, args[5].f,
            static_cast<uint32_t>(args[6].i),
            args[7].i, args[8].f);
        return true;
    }
    if (std::strcmp(name, "native_drawCircle") == 0) {
        JavaCanvasRenderer::getInstance().drawCircle(
            args[0].f, args[1].f, args[2].f,
            static_cast<uint32_t>(args[3].i),
            args[4].i, args[5].f);
        return true;
    }
    if (std::strcmp(name, "native_drawOval") == 0) {
        JavaCanvasRenderer::getInstance().drawOval(
            args[0].f, args[1].f, args[2].f, args[3].f,
            static_cast<uint32_t>(args[4].i),
            args[5].i, args[6].f);
        return true;
    }
    if (std::strcmp(name, "native_drawLine") == 0) {
        JavaCanvasRenderer::getInstance().drawLine(
            args[0].f, args[1].f, args[2].f, args[3].f,
            static_cast<uint32_t>(args[4].i), args[5].f);
        return true;
    }
    if (std::strcmp(name, "native_drawPath") == 0) {
        auto* verbsArr = reinterpret_cast<DexArray*>(args[0].l);
        int verbCount = args[1].i;
        auto* coordsArr = reinterpret_cast<DexArray*>(args[2].l);
        int coordCount = args[3].i;
        uint32_t color = static_cast<uint32_t>(args[4].i);
        int style = args[5].i;
        float strokeWidth = args[6].f;
        bool evenOdd = (args[7].i != 0);
        if (verbsArr && coordsArr && verbCount > 0 && coordCount >= 0) {
            const uint8_t* verbs = reinterpret_cast<const uint8_t*>(verbsArr->Data());
            const float* coords = reinterpret_cast<const float*>(coordsArr->Data());
            JavaCanvasRenderer::getInstance().drawPath(
                verbs, verbCount, coords, coordCount,
                color, style, strokeWidth, evenOdd);
        }
        return true;
    }
    if (std::strcmp(name, "native_save") == 0) {
        *result = DexValue::Int(JavaCanvasRenderer::getInstance().save());
        return true;
    }
    if (std::strcmp(name, "native_restore") == 0) {
        JavaCanvasRenderer::getInstance().restore();
        return true;
    }
    if (std::strcmp(name, "native_restoreToCount") == 0) {
        JavaCanvasRenderer::getInstance().restoreToCount(args[0].i);
        return true;
    }
    if (std::strcmp(name, "native_translate") == 0) {
        JavaCanvasRenderer::getInstance().translate(args[0].f, args[1].f);
        return true;
    }
    if (std::strcmp(name, "native_scale") == 0) {
        JavaCanvasRenderer::getInstance().scale(args[0].f, args[1].f);
        return true;
    }
    if (std::strcmp(name, "native_rotate") == 0) {
        JavaCanvasRenderer::getInstance().rotate(args[0].f);
        return true;
    }
    if (std::strcmp(name, "native_concat") == 0) {
        JavaCanvasRenderer::getInstance().concat(
            args[0].f, args[1].f, args[2].f, args[3].f, args[4].f, args[5].f);
        return true;
    }
    if (std::strcmp(name, "native_clipRect") == 0) {
        JavaCanvasRenderer::getInstance().clipRect(
            args[0].f, args[1].f, args[2].f, args[3].f);
        return true;
    }
    if (std::strcmp(name, "native_flush") == 0) {
        JavaCanvasRenderer::getInstance().flush();
        return true;
    }
    // Real surface size. Canvas used to hardcode 1080x1920, so on any other screen
    // the layout was computed for the wrong extent and drawing fell off the edge
    // (an 828x1792 device lost everything past ~77% of the width).
    if (std::strcmp(name, "native_getSurfaceWidth") == 0) {
        *result = DexValue::Int(JavaCanvasRenderer::getInstance().getWidth());
        return true;
    }
    if (std::strcmp(name, "native_getSurfaceHeight") == 0) {
        *result = DexValue::Int(JavaCanvasRenderer::getInstance().getHeight());
        return true;
    }
    if (std::strcmp(name, "native_isSurfaceReady") == 0) {
        *result = DexValue::Int(
            JavaCanvasRenderer::getInstance().hasBoundMetalSurface() ? 1 : 0);
        return true;
    }
    return false;
}

bool Invoke_android_graphics_BitmapFactory(Interpreter* interp, const char* name,
                                           const DexValue* args, size_t num_args,
                                           DexValue* result) {
    if (std::strcmp(name, "nativeDecodeByteArray") == 0) {
        if (num_args < 5) return false;
        auto* dataArr = reinterpret_cast<DexArray*>(args[0].l);
        int32_t offset = args[1].i;
        int32_t length = args[2].i;
        auto* outDimArr = reinterpret_cast<DexArray*>(args[3].l);
        bool justBounds = (args[4].i != 0);

        if (!dataArr || !outDimArr || offset < 0 || length <= 0 ||
            offset + length > dataArr->length || outDimArr->length < 2) {
            result->l = nullptr;
            return true;
        }

        int32_t* outDim = reinterpret_cast<int32_t*>(outDimArr->Data());
        outDim[0] = -1;
        outDim[1] = -1;

        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(dataArr->Data()) + offset;

#if defined(__APPLE__)
        CFDataRef cfData = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, bytes, length, kCFAllocatorNull);
        if (!cfData) {
            result->l = nullptr;
            return true;
        }
        CGImageSourceRef imageSource = CGImageSourceCreateWithData(cfData, nullptr);
        if (!imageSource) {
            CFRelease(cfData);
            result->l = nullptr;
            return true;
        }

        if (CGImageSourceGetCount(imageSource) == 0) {
            CFRelease(imageSource);
            CFRelease(cfData);
            result->l = nullptr;
            return true;
        }

        int width = 0;
        int height = 0;
        CFDictionaryRef props = CGImageSourceCopyPropertiesAtIndex(imageSource, 0, nullptr);
        if (props) {
            CFNumberRef cfWidth = (CFNumberRef)CFDictionaryGetValue(props, kCGImagePropertyPixelWidth);
            CFNumberRef cfHeight = (CFNumberRef)CFDictionaryGetValue(props, kCGImagePropertyPixelHeight);
            if (cfWidth) CFNumberGetValue(cfWidth, kCFNumberIntType, &width);
            if (cfHeight) CFNumberGetValue(cfHeight, kCFNumberIntType, &height);
            CFRelease(props);
        }

        CGImageRef image = nullptr;
        if (width <= 0 || height <= 0 || !justBounds) {
            image = CGImageSourceCreateImageAtIndex(imageSource, 0, nullptr);
            if (image) {
                width = static_cast<int>(CGImageGetWidth(image));
                height = static_cast<int>(CGImageGetHeight(image));
            }
        }

        if (width <= 0 || height <= 0) {
            if (image) CGImageRelease(image);
            CFRelease(imageSource);
            CFRelease(cfData);
            result->l = nullptr;
            return true;
        }

        outDim[0] = width;
        outDim[1] = height;

        if (justBounds) {
            if (image) CGImageRelease(image);
            CFRelease(imageSource);
            CFRelease(cfData);
            result->l = nullptr;
            return true;
        }

        if (!image) {
            image = CGImageSourceCreateImageAtIndex(imageSource, 0, nullptr);
        }
        if (!image) {
            CFRelease(imageSource);
            CFRelease(cfData);
            result->l = nullptr;
            return true;
        }

        int64_t totalPixels = static_cast<int64_t>(width) * height;
        if (totalPixels <= 0 || totalPixels > 64 * 1024 * 1024) {
            CGImageRelease(image);
            CFRelease(imageSource);
            CFRelease(cfData);
            result->l = nullptr;
            return true;
        }

        DexClass* intArrKlass = interp->linker()->FindClass("[I");
        if (!intArrKlass) {
            CGImageRelease(image);
            CFRelease(imageSource);
            CFRelease(cfData);
            result->l = nullptr;
            return true;
        }

        DexArray* pixelsArr = interp->linker()->AllocArray(intArrKlass, static_cast<int32_t>(totalPixels));
        if (!pixelsArr) {
            CGImageRelease(image);
            CFRelease(imageSource);
            CFRelease(cfData);
            interp->ThrowException("Ljava/lang/OutOfMemoryError;", "Failed to allocate bitmap pixels");
            result->l = nullptr;
            return true;
        }

        uint32_t* pixels = reinterpret_cast<uint32_t*>(pixelsArr->Data());
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(
            pixels, width, height, 8, width * 4, colorSpace,
            kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);

        if (ctx) {
            CGContextSetBlendMode(ctx, kCGBlendModeCopy);
            CGContextDrawImage(ctx, CGRectMake(0, 0, width, height), image);
            CGContextRelease(ctx);
        }
        CGColorSpaceRelease(colorSpace);
        CGImageRelease(image);
        CFRelease(imageSource);
        CFRelease(cfData);

        result->l = pixelsArr;
        return true;
#else
        (void)interp;
        if (length >= 24 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G') {
            int width = (bytes[16] << 24) | (bytes[17] << 16) | (bytes[18] << 8) | bytes[19];
            int height = (bytes[20] << 24) | (bytes[21] << 16) | (bytes[22] << 8) | bytes[23];
            outDim[0] = width;
            outDim[1] = height;
            if (justBounds) {
                result->l = nullptr;
                return true;
            }
            int64_t totalPixels = static_cast<int64_t>(width) * height;
            if (totalPixels > 0 && totalPixels < 64 * 1024 * 1024) {
                DexClass* intArrKlass = interp->linker()->FindClass("[I");
                if (intArrKlass) {
                    DexArray* pixelsArr = interp->linker()->AllocArray(intArrKlass, static_cast<int32_t>(totalPixels));
                    if (pixelsArr) {
                        result->l = pixelsArr;
                        return true;
                    }
                }
            }
        }
        result->l = nullptr;
        return true;
#endif
    }
    return false;
}

bool Invoke_android_app_ActivityThread(Interpreter* /*interp*/, const char* name,
                                         const DexValue* args, size_t num_args,
                                         DexValue* /*result*/) {
    if (std::strcmp(name, "nativeReportTouchTiming") == 0) {
        if (num_args < 3) return false;
        const int64_t now = guest_monotonic_ns();
        static std::atomic<int64_t> last{0};
        int64_t previous = last.load(std::memory_order_relaxed);
        if ((args[1].j >= 16 || args[2].j >= 16000000) && now - previous >= 1000000000ll
                && last.compare_exchange_strong(previous, now, std::memory_order_relaxed)) {
            std::fprintf(stderr, "[KuDroidTouch] response action=%d queue=%lldms dispatch=%lldus\n",
                         args[0].i, static_cast<long long>(args[1].j), static_cast<long long>(args[2].j / 1000));
        }
        return true;
    }
    if (std::strcmp(name, "kudroid_note_java_paused") == 0) {
        kudroid_note_java_paused();
        return true;
    }
    return false;
}

bool Invoke_android_app_Activity(Interpreter* /*interp*/, const char* name, const DexValue* args,
                                 size_t num_args, DexValue* /*result*/) {
    if (std::strcmp(name, "nativeReportFullyDrawn") == 0) {
        if (num_args < 1) return false;
        const std::string phase = std::string("fully-drawn ") + GetStringUtf8(args[0]);
        kudroid_boot_mark(phase.c_str());
        return true;
    }
    if (std::strcmp(name, "nativeReportFrameTimings") == 0) {
        if (num_args < 4) return false;
        const int64_t now = guest_monotonic_ns();
        static std::atomic<int64_t> last{0};
        int64_t previous = last.load(std::memory_order_relaxed);
        if ((args[0].j >= 16000000 || args[1].j + args[2].j + args[3].j >= 16000000)
                && now - previous >= 1000000000ll
                && last.compare_exchange_strong(previous, now, std::memory_order_relaxed)) {
            std::fprintf(stderr, "[KuDroidFrame] queue=%lldms layout=%lldus draw=%lldus submit=%lldus\n",
                         static_cast<long long>(args[0].j / 1000000), static_cast<long long>(args[1].j / 1000),
                         static_cast<long long>(args[2].j / 1000), static_cast<long long>(args[3].j / 1000));
        }
        return true;
    }
    if (std::strcmp(name, "setRequestedOrientation_native") == 0) {        kudroid_set_requested_orientation(args[0].i);
        return true;
    }
    if (std::strcmp(name, "nativeOpenUrl") == 0) {
        if (num_args < 1) return false;
        const std::string url = GetStringUtf8(args[0]);
        VmLockRelease unlocked;
        kudroid_open_url(url.c_str());
        return true;
    }
    if (std::strcmp(name, "nativeRequestPermissions") == 0) {
        // (activity, packageName, permissionsCsv, requestCode)
        if (num_args < 4) return false;
        DexObject* act = args[0].l;
        const std::string pkg = GetStringUtf8(args[1]);
        const std::string csv = GetStringUtf8(args[2]);
        const int reqCode = args[3].i;
        // The host may show UI and answer from another thread; the result comes back
        // through the Looper, so nothing here waits on it.
        VmLockRelease unlocked;
        kudroid_prompt_permission_request(pkg.c_str(), csv.c_str(), reqCode, act);
        return true;
    }
    return false;
}

bool Invoke_android_media_AudioRecord(Interpreter* /*interp*/, const char* name,
                                      const DexValue* args, size_t num_args,
                                      DexValue* result) {
    if (std::strcmp(name, "nativeGetMinBufferSize") == 0) {
        if (num_args < 2) return false;
        *result = DexValue::Int(bionic_kudroid_audiorecord_min_buffer_size(args[0].i, args[1].i));
        return true;
    }
    if (std::strcmp(name, "nativeCreate") == 0) {
        if (num_args < 3) return false;
        VmLockRelease unlocked;
        *result = DexValue::Long(bionic_kudroid_audiorecord_create(args[0].i, args[1].i, args[2].i));
        return true;
    }
    if (num_args < 1) return false;
    const int64_t rec = args[0].j;
    if (std::strcmp(name, "nativeStart") == 0) {
        VmLockRelease unlocked;
        *result = DexValue::Int(bionic_kudroid_audiorecord_start(rec));
        return true;
    }
    if (std::strcmp(name, "nativeStop") == 0) {
        VmLockRelease unlocked;
        *result = DexValue::Int(bionic_kudroid_audiorecord_stop(rec));
        return true;
    }
    if (std::strcmp(name, "nativeRelease") == 0) {
        VmLockRelease unlocked;
        *result = DexValue::Int(bionic_kudroid_audiorecord_release(rec));
        return true;
    }
    const bool isBytes = std::strcmp(name, "nativeRead") == 0;
    if (isBytes || std::strcmp(name, "nativeReadShorts") == 0) {
        // (rec, array, offset, size, blocking); offset and size count array elements.
        if (num_args < 5) return false;
        auto* data = reinterpret_cast<DexArray*>(args[1].l);
        const int32_t offset = args[2].i;
        const int32_t count = args[3].i;
        if (data == nullptr) {
            *result = DexValue::Int(-3);  // ERROR_INVALID_OPERATION
            return true;
        }
        const size_t elem = isBytes ? 1 : sizeof(int16_t);
        if (offset < 0 || count < 0 || offset > data->length || count > data->length - offset ||
            static_cast<size_t>(count) > static_cast<size_t>(INT32_MAX) / elem) {
            *result = DexValue::Int(-2);  // ERROR_BAD_VALUE
            return true;
        }
        std::vector<uint8_t> pcm(static_cast<size_t>(count) * elem);
        int32_t got;
        {
            VmLockRelease unlocked;
            got = bionic_kudroid_audiorecord_read(rec, pcm.data(),
                                                  static_cast<int32_t>(pcm.size()), args[4].i != 0);
        }
        if (got > 0) {
            std::memcpy(data->Data() + static_cast<size_t>(offset) * elem, pcm.data(),
                        static_cast<size_t>(got));
        }
        *result = DexValue::Int(got > 0 ? got / static_cast<int32_t>(elem) : got);
        return true;
    }
    return false;
}

bool Invoke_android_hardware_Camera(Interpreter* /*interp*/, const char* name,
                                    const DexValue* args, size_t num_args, DexValue* result) {
    if (std::strcmp(name, "nativeCount") == 0) {
        *result = DexValue::Int(bionic_kudroid_camera_count());
        return true;
    }
    if (std::strcmp(name, "nativeInfo") == 0) {
        if (num_args < 1) return false;
        *result = DexValue::Int(bionic_kudroid_camera_info(args[0].i));
        return true;
    }
    // Session control crosses into the host, which may take a while to answer.
    if (std::strcmp(name, "nativeOpen") == 0) {
        if (num_args < 1) return false;
        VmLockRelease unlocked;
        *result = DexValue::Int(bionic_kudroid_camera_open(args[0].i));
        return true;
    }
    if (std::strcmp(name, "nativeClose") == 0) {
        VmLockRelease unlocked;
        bionic_kudroid_camera_close();
        return true;
    }
    if (std::strcmp(name, "nativeStart") == 0) {
        if (num_args < 2) return false;
        VmLockRelease unlocked;
        *result = DexValue::Int(bionic_kudroid_camera_start(args[0].i, args[1].i));
        return true;
    }
    if (std::strcmp(name, "nativeStop") == 0) {
        VmLockRelease unlocked;
        bionic_kudroid_camera_stop();
        return true;
    }
    if (std::strcmp(name, "nativeTakePicture") == 0) {
        VmLockRelease unlocked;
        *result = DexValue::Int(bionic_kudroid_camera_take_picture());
        return true;
    }
    if (std::strcmp(name, "nativeSetPreviewDisplay") == 0) {
        if (num_args < 2) return false;
        VmLockRelease unlocked;
        bionic_kudroid_camera_set_preview(args[0].i, args[1].i);
        return true;
    }
    if (std::strcmp(name, "nativeEvents") == 0) {
        *result = DexValue::Int(bionic_kudroid_camera_events());
        return true;
    }
    if (std::strcmp(name, "nativeFrameSize") == 0) {
        *result = DexValue::Int(bionic_kudroid_camera_frame_size());
        return true;
    }
    if (std::strcmp(name, "nativePhotoSize") == 0) {
        *result = DexValue::Int(bionic_kudroid_camera_photo_size());
        return true;
    }
    if (std::strcmp(name, "nativeTakeError") == 0) {
        *result = DexValue::Int(bionic_kudroid_camera_take_error());
        return true;
    }
    const bool isFrame = std::strcmp(name, "nativeCopyFrame") == 0;
    if (isFrame || std::strcmp(name, "nativeCopyPhoto") == 0) {
        if (num_args < 1) return false;
        auto* dst = reinterpret_cast<DexArray*>(args[0].l);
        if (dst == nullptr) {
            *result = DexValue::Int(0);
            return true;
        }
        *result = DexValue::Int(isFrame ? bionic_kudroid_camera_copy_frame(dst->Data(), dst->length)
                                        : bionic_kudroid_camera_copy_photo(dst->Data(), dst->length));
        return true;
    }
    return false;
}

// Bitmap pixel work too heavy for interpreted loops. Pixels are ARGB ints; the byte layouts
// below are Bitmap.Config's (ARGB_8888 is premultiplied R, G, B, A in memory).
