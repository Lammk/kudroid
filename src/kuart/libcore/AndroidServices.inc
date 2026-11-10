bool Invoke_android_graphics_Bitmap(Interpreter* interp, const char* name, const DexValue* args,
                                    size_t num_args, DexValue* /*result*/) {
    if (std::strcmp(name, "nativeFill") == 0) {
        if (num_args < 2) return false;
        auto* pixels = reinterpret_cast<DexArray*>(args[0].l);
        if (pixels == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "pixels");
            return true;
        }
        auto* data = reinterpret_cast<int32_t*>(pixels->Data());
        std::fill(data, data + pixels->length, args[1].i);
        return true;
    }
    if (std::strcmp(name, "nativeFillRect") == 0) {
        // (int[] pixels, int stride, int left, int top, int right, int bottom, int color);
        // the Java caller has already clipped the rectangle to the bitmap.
        if (num_args < 7) return false;
        auto* pixels = reinterpret_cast<DexArray*>(args[0].l);
        if (pixels == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "pixels");
            return true;
        }
        const int32_t stride = args[1].i, left = args[2].i, top = args[3].i;
        const int32_t right = args[4].i, bottom = args[5].i;
        if (stride <= 0 || left < 0 || top < 0 || left >= right || top >= bottom || right > stride ||
            static_cast<int64_t>(bottom) * stride > pixels->length) {
            interp->ThrowException("Ljava/lang/ArrayIndexOutOfBoundsException;", "fillRect range");
            return true;
        }
        auto* data = reinterpret_cast<int32_t*>(pixels->Data());
        const uint32_t color = static_cast<uint32_t>(args[6].i);
        const uint32_t sa = color >> 24;
        if (sa == 0) return true;
        for (int32_t y = top; y < bottom; ++y) {
            int32_t* row = data + static_cast<int64_t>(y) * stride;
            if (sa == 255) {
                std::fill(row + left, row + right, args[6].i);
                continue;
            }
            // Source-over for straight (non-premultiplied) ARGB.
            const uint32_t inverse = 255 - sa;
            for (int32_t x = left; x < right; ++x) {
                const uint32_t dst = static_cast<uint32_t>(row[x]);
                const uint32_t da = dst >> 24;
                const uint32_t denominator = sa * 255 + da * inverse;
                const uint32_t a = (denominator + 127) / 255;
                auto mix = [&](int shift) {
                    return (((color >> shift) & 255) * sa * 255 + ((dst >> shift) & 255) * da * inverse +
                            denominator / 2) / denominator;
                };
                row[x] = static_cast<int32_t>(a << 24 | mix(16) << 16 | mix(8) << 8 | mix(0));
            }
        }
        return true;
    }
    if (std::strcmp(name, "nativeReadPixels") == 0) {
        if (num_args < 4) return false;
        auto* dst = reinterpret_cast<DexArray*>(args[0].l);
        auto* src = reinterpret_cast<DexArray*>(args[2].l);
        const int config = args[1].i;
        const int32_t offset = args[3].i;
        if (src == nullptr || dst == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "bitmap copy");
            return true;
        }
        const size_t bpp = config == 0 ? 4 : config == 1 ? 1 : 2;
        if (config < 0 || config > 3 || offset < 0 || offset > src->length ||
            static_cast<size_t>(dst->length) * bpp > static_cast<size_t>(src->length - offset)) {
            interp->ThrowException("Ljava/lang/ArrayIndexOutOfBoundsException;", "bitmap copy range");
            return true;
        }
        const uint8_t* in = src->Data() + offset;
        auto* out = reinterpret_cast<uint32_t*>(dst->Data());
        for (int32_t i = 0; i < dst->length; ++i, in += bpp) {
            uint32_t a, r, g, b;
            if (config == 1) {
                out[i] = static_cast<uint32_t>(in[0]) << 24;
                continue;
            }
            if (config == 2) {
                const uint32_t v = in[0] | (static_cast<uint32_t>(in[1]) << 8);
                r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31;
                out[i] = 0xff000000u | ((r << 3 | r >> 2) << 16) |
                         ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
                continue;
            }
            if (config == 3) {
                const uint32_t v = in[0] | (static_cast<uint32_t>(in[1]) << 8);
                a = (v >> 12) * 17; r = ((v >> 8) & 15) * 17;
                g = ((v >> 4) & 15) * 17; b = (v & 15) * 17;
            } else {
                r = in[0]; g = in[1]; b = in[2]; a = in[3];
            }
            // The renderer stores straight ARGB; imported bytes are premultiplied.
            auto unpremultiply = [a](uint32_t c) {
                return a == 0 ? 0u : std::min(255u, (c * 255 + a / 2) / a);
            };
            out[i] = (a << 24) | (unpremultiply(r) << 16) |
                     (unpremultiply(g) << 8) | unpremultiply(b);
        }
        return true;
    }
    if (std::strcmp(name, "nativeCopyPixels") == 0) {
        // (int[] src, int config, byte[] dst, int dstOffset); config 0 ARGB_8888,
        // 1 ALPHA_8, 2 RGB_565, 3 ARGB_4444.
        if (num_args < 4) return false;
        auto* src = reinterpret_cast<DexArray*>(args[0].l);
        auto* dst = reinterpret_cast<DexArray*>(args[2].l);
        const int config = args[1].i;
        const int32_t offset = args[3].i;
        if (src == nullptr || dst == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "bitmap copy");
            return true;
        }
        const size_t bpp = config == 0 ? 4 : config == 1 ? 1 : 2;
        if (config < 0 || config > 3 || offset < 0 || offset > dst->length ||
            static_cast<size_t>(src->length) * bpp > static_cast<size_t>(dst->length - offset)) {
            interp->ThrowException("Ljava/lang/ArrayIndexOutOfBoundsException;", "bitmap copy range");
            return true;
        }
        const auto* in = reinterpret_cast<const uint32_t*>(src->Data());
        uint8_t* out = dst->Data() + offset;
        for (int32_t i = 0; i < src->length; ++i) {
            const uint32_t p = in[i];
            const uint32_t a = p >> 24;
            const uint32_t r = (((p >> 16) & 255) * a + 127) / 255;
            const uint32_t g = (((p >> 8) & 255) * a + 127) / 255;
            const uint32_t b = ((p & 255) * a + 127) / 255;
            switch (config) {
                case 0:
                    out[0] = static_cast<uint8_t>(r);
                    out[1] = static_cast<uint8_t>(g);
                    out[2] = static_cast<uint8_t>(b);
                    out[3] = static_cast<uint8_t>(a);
                    break;
                case 1:
                    out[0] = static_cast<uint8_t>(a);
                    break;
                case 2: {
                    const uint32_t v = (((p >> 19) & 31) << 11) | (((p >> 10) & 63) << 5) |
                                       ((p >> 3) & 31);
                    out[0] = static_cast<uint8_t>(v);
                    out[1] = static_cast<uint8_t>(v >> 8);
                    break;
                }
                default: {
                    const uint32_t v = ((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) |
                                       (b >> 4);
                    out[0] = static_cast<uint8_t>(v);
                    out[1] = static_cast<uint8_t>(v >> 8);
                    break;
                }
            }
            out += bpp;
        }
        return true;
    }
    return false;
}

bool Invoke_android_net_ConnectivityManager(Interpreter* /*interp*/, const char* name,
                                            const DexValue* /*args*/, size_t /*num_args*/,
                                            DexValue* result) {
    if (std::strcmp(name, "nativeState") == 0) {
        *result = DexValue::Int(bionic_kudroid_network_state());
        return true;
    }
    if (std::strcmp(name, "nativeTakeChange") == 0) {
        *result = DexValue::Int(bionic_kudroid_network_take_change());
        return true;
    }
    return false;
}

bool Invoke_android_content_pm_RuntimePermissions(Interpreter* /*interp*/, const char* name,
                                                  const DexValue* args, size_t num_args,
                                                  DexValue* result) {
    if (std::strcmp(name, "isGrantedNative") == 0) {
        if (num_args < 2) return false;
        *result = DexValue::Int(kudroid_runtime_permission_granted(GetStringUtf8(args[0]),
                                                                    GetStringUtf8(args[1])));
        return true;
    }
    return false;
}

bool Invoke_android_os_Vibrator(Interpreter* /*interp*/, const char* name, const DexValue* args,
                                size_t /*num_args*/, DexValue* /*result*/) {
    if (std::strcmp(name, "kudroid_vibrate_native") == 0) {
        kudroid_vibrate(args[0].i);
        return true;
    }
    return false;}

bool Invoke_android_os_PowerManager(const char* name, DexValue* result) {
    if (std::strcmp(name, "nativeThermalStatus") != 0) return false;
    *result = DexValue::Int(kudroid_get_thermal_status());
    return true;
}

// Choreographer callbacks share the NDK pacer; callbacks are global refs.
//
//     android.view.Choreographer->getInstance()Landroid/view/Choreographer;
//     android.view.Choreographer->postFrameCallback(...FrameCallback;)V
//     android.view.Choreographer->postFrameCallbackDelayed(...FrameCallback;J)V
namespace {

// A queued Java frame callback. Allocated per post and owned by the pacer until the
// callback fires or is removed.
struct JavaFrameCallback {
    Interpreter* interp;
    DexObject* callback;
    DexObject* choreographer;
};

// Every JavaFrameCallback handed to the pacer, so removeFrameCallback can find the
// context belonging to a given Java object and the entry can be freed exactly once.
std::mutex g_java_frame_mutex;
std::vector<JavaFrameCallback*> g_java_frame_contexts;

// Drops the callback's global ref once the last queued context naming it is gone.
// add/remove is refcounted implicitly by scanning the live context list: the same
// Java object may be posted more than once, and deleting its ref while another copy
// is still queued would break JNI reachability for that copy.
void FrameCallbackReleaseGlobalRef(Interpreter* interp, DexObject* callback) {
    if (interp == nullptr || callback == nullptr) return;
    bool still_queued = false;
    {
        std::lock_guard<std::mutex> lock(g_java_frame_mutex);
        for (JavaFrameCallback* c : g_java_frame_contexts) {
            if (c->callback == callback || c->choreographer == callback) {
                still_queued = true;
                break;
            }
        }
    }
    if (!still_queued && interp->jni_env() != nullptr) {
        // AsHandle()/AsObject() are identity casts in DexJniEnv; AddGlobalRef inserted
        // this DexObject*, so the same pointer erases it.
        interp->jni_env()->DeleteGlobalRef(reinterpret_cast<jobject>(callback));
    }
}

// Invoked by the pacer - either on the guest thread that polled its looper, or on the
// pacer thread. Runs bytecode, so it must take the VM lock, which Execute() does for
// itself when the calling thread does not already hold it.
void JavaFrameCallbackTrampoline(int64_t frame_time_ns, void* data) {
    auto* ctx = static_cast<JavaFrameCallback*>(data);
    if (ctx == nullptr) return;
    VmLockGuard guard;

    Interpreter* interp = ctx->interp;
    DexObject* callback = ctx->callback;
    {
        std::lock_guard<std::mutex> lock(g_java_frame_mutex);
        for (size_t i = 0; i < g_java_frame_contexts.size(); ++i) {
            if (g_java_frame_contexts[i] == ctx) {
                g_java_frame_contexts.erase(g_java_frame_contexts.begin() +
                                            static_cast<long>(i));
                break;
            }
        }
    }

    if (interp != nullptr && callback != nullptr && interp->linker() != nullptr) {
        SetLongField(ctx->choreographer, "mLastFrameTimeNanos", frame_time_ns);
        DexClass* klass = interp->linker()->ClassOfObject(callback);
        if (klass != nullptr) {
            // Virtual dispatch on the callback's real class. The interface method is
            // doFrame(J)V; a Proxy implementing FrameCallback resolves through the
            // proxy path inside Execute, which is why this looks the method up on the
            // receiver rather than on the interface.
            DexMethod* do_frame = klass->FindVirtualMethod("doFrame", "(J)V");
            if (do_frame != nullptr) {
                const DexValue call_args[3] = {DexValue::Ref(callback),
                                               DexValue::Long(frame_time_ns), DexValue()};
                interp->ClearPendingException();
                interp->Execute(do_frame, call_args, 2);
                // Clear callback exceptions; no Java frame above can catch them.
                if (interp->HasPendingException()) {
                    std::fprintf(stderr,
                                 "[KuART][Choreographer] exception in doFrame: %s\n",
                                 interp->last_error().c_str());
                    interp->ClearPendingException();
                }
            }
        }
    }
    FrameCallbackReleaseGlobalRef(interp, callback);
    FrameCallbackReleaseGlobalRef(interp, ctx->choreographer);
    delete ctx;
}

}  // namespace

bool Invoke_android_view_Choreographer(Interpreter* interp, const char* name,
                                       const DexValue* args, size_t num_args,
                                       DexValue* result) {
    if (std::strcmp(name, "nativePostFrameCallback") == 0) {
        // (this, FrameCallback, long delayMillis)
        if (num_args < 3) return false;
        DexObject* callback = args[1].l;
        if (callback == nullptr) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   "postFrameCallback with a null callback");
            return true;
        }
        // Global ref keeps the callback reachable while queued.
        if (interp->jni_env() != nullptr) {
            interp->jni_env()->AddGlobalRef(callback);
            interp->jni_env()->AddGlobalRef(args[0].l);
        }
        auto* ctx = new JavaFrameCallback{interp, callback, args[0].l};
        {
            std::lock_guard<std::mutex> lock(g_java_frame_mutex);
            g_java_frame_contexts.push_back(ctx);
        }
        const int64_t delay_ms = args[2].j;
        kudroid::frame_pacer_post(
            kudroid::frame_pacer_instance_for_this_thread(),
            reinterpret_cast<void*>(&JavaFrameCallbackTrampoline), ctx,
            delay_ms > 0 ? static_cast<uint64_t>(delay_ms) * 1000000ull : 0);
        return true;
    }
    if (std::strcmp(name, "nativeRemoveFrameCallback") == 0) {
        if (num_args < 2) return false;
        DexObject* callback = args[1].l;
        if (callback == nullptr) return true;
        // Every context naming this Java object, because the same callback posted
        // twice is queued twice and Android's remove clears both.
        std::vector<JavaFrameCallback*> matches;
        {
            std::lock_guard<std::mutex> lock(g_java_frame_mutex);
            for (size_t i = g_java_frame_contexts.size(); i-- > 0;) {
                if (g_java_frame_contexts[i]->callback == callback) {
                    matches.push_back(g_java_frame_contexts[i]);
                    g_java_frame_contexts.erase(g_java_frame_contexts.begin() +
                                                static_cast<long>(i));
                }
            }
        }
        for (JavaFrameCallback* ctx : matches) {
            // Only free the context once the pacer has agreed it no longer holds it.
            // A context the pacer had already dequeued is about to be freed by the
            // trampoline, and deleting it here as well would be a double free.
            if (kudroid::frame_pacer_remove(
                    reinterpret_cast<void*>(&JavaFrameCallbackTrampoline), ctx) > 0) {
                FrameCallbackReleaseGlobalRef(interp, ctx->choreographer);
                delete ctx;
            }
        }
        // All contexts for this object are gone (either deleted above or now owned by
        // the trampoline, which releases too); drop the ref if no copy remains queued.
        if (!matches.empty()) {
            FrameCallbackReleaseGlobalRef(interp, callback);
        }
        return true;
    }
    if (std::strcmp(name, "nativeGetFrameIntervalNanos") == 0) {
        *result = DexValue::Long(kudroid::frame_pacer_interval_ns());
        return true;
    }
    if (std::strcmp(name, "nativeGetFrameTimeNanos") == 0) {
        // Inside a frame callback this is that frame's timestamp; outside one there is
        // no frame, and "now" is more useful than an exception or a stale value. Both
        // are on CLOCK_MONOTONIC, the clock System.nanoTime reports.
        const int64_t in_frame = kudroid::frame_pacer_current_frame_time_ns();
        if (in_frame != 0) {
            *result = DexValue::Long(in_frame);
            return true;
        }
        *result = DexValue::Long(guest_monotonic_ns());
        return true;
    }
    return false;
}

bool Invoke_keep_screen_on(Interpreter* /*interp*/, const char* name, const DexValue* args,
                           size_t /*num_args*/, DexValue* /*result*/) {
    if (std::strcmp(name, "setKeepScreenOnNative") == 0) {
        kudroid_set_keep_screen_on(args[0].i != 0 ? 1 : 0);
        return true;
    }
    return false;
}

// AudioTrack forwards to the host AudioShim queue.
bool Invoke_android_media_MediaPlayer(const char* name, const DexValue* args,
                                     size_t count, DexValue* result) {
    if (std::strcmp(name, "nativeCreate") == 0) {
        VmLockRelease unlocked;
        *result = DexValue::Long(kudroid_media_player_create());
        return true;
    }
    if (std::strcmp(name, "nativeSource") == 0 && count == 5) {
        const std::string path = args[1].l ? GetStringUtf8(args[1]) : "";
        const bool hasPath = args[1].l != nullptr;
        VmLockRelease unlocked;
        *result = DexValue::Int(kudroid_media_player_source(args[0].j,
            hasPath ? path.c_str() : nullptr, args[2].i, args[3].j, args[4].j));
        return true;
    }
    if (std::strcmp(name, "nativeCommand") == 0 && count == 4) {
        VmLockRelease unlocked;
        *result = DexValue::Long(kudroid_media_player_command(args[0].j, args[1].i,
                                                            args[2].d, args[3].d));
        return true;
    }
    return false;
}

bool Invoke_android_media_AudioTrack(Interpreter* interp, const char* name,
                                     const DexValue* args, size_t num_args,
                                     DexValue* result) {
    // Static: (sampleRate, channelCount, encoding). No receiver, so args start at 0.
    if (std::strcmp(name, "nativeGetMinBufferSize") == 0) {
        if (num_args < 3) return false;
        *result = DexValue::Int(bionic_kudroid_audiotrack_min_buffer_size(
            args[0].i, args[1].i, args[2].i));
        return true;
    }
    if (std::strcmp(name, "nativeCreate") == 0) {
        if (num_args < 4) return false;
        *result = DexValue::Long(bionic_kudroid_audiotrack_create(
            args[0].i, args[1].i, args[2].i, args[3].i));
        return true;
    }

    // Remaining methods take the handle first; zero means released.
    if (std::strcmp(name, "nativeWrite") == 0) {
        // (track, byte[] data, offsetInBytes, sizeInBytes)
        if (num_args < 4) return false;
        auto* data = reinterpret_cast<DexArray*>(args[1].l);
        const int32_t offset = args[2].i;
        const int32_t size = args[3].i;
        if (data == nullptr) {
            *result = DexValue::Int(-3);  // ERROR_INVALID_OPERATION
            return true;
        }
        // Re-check bounds; reflection can bypass Java-side checks.
        if (offset < 0 || size < 0 || offset > data->length ||
            size > data->length - offset) {
            *result = DexValue::Int(-2);  // ERROR_BAD_VALUE
            return true;
        }
        const int64_t track = args[0].j;
        const bool blocking = num_args < 5 || args[4].i == 0;
        std::vector<uint8_t> pcm(data->Data() + offset, data->Data() + offset + size);
        {
            VmLockRelease unlocked;
            *result = DexValue::Int(bionic_kudroid_audiotrack_write_mode(
                track, pcm.data(), size, blocking));
        }
        return true;
    }
    if (std::strcmp(name, "nativeWriteShorts") == 0) {
        // (track, short[] data, offsetInShorts, sizeInShorts) - offsets are in ELEMENTS.
        if (num_args < 4) return false;
        auto* data = reinterpret_cast<DexArray*>(args[1].l);
        const int32_t offset = args[2].i;
        const int32_t count = args[3].i;
        if (data == nullptr) {
            *result = DexValue::Int(-3);
            return true;
        }
        if (offset < 0 || count < 0 || offset > data->length ||
            count > data->length - offset) {
            *result = DexValue::Int(-2);
            return true;
        }
        if (count > INT32_MAX / 2) {
            *result = DexValue::Int(-2);
            return true;
        }
        const int64_t track = args[0].j;
        const bool blocking = num_args < 5 || args[4].i == 0;
        const auto* begin = data->Data() + static_cast<size_t>(offset) * sizeof(int16_t);
        std::vector<uint8_t> pcm(begin, begin + count * sizeof(int16_t));
        int32_t written;
        {
            VmLockRelease unlocked;
            written = bionic_kudroid_audiotrack_write_mode(track, pcm.data(),
                                                        count * 2, blocking);
        }
        // Back to elements, because that is what the Java caller passed and expects.
        *result = DexValue::Int(written < 0 ? written
                                            : written / static_cast<int32_t>(sizeof(int16_t)));
        return true;
    }
    if (std::strcmp(name, "nativePlay") == 0) {
        if (num_args < 1) return false;
        VmLockRelease unlocked;
        *result = DexValue::Int(bionic_kudroid_audiotrack_play(args[0].j));
        return true;
    }
    if (std::strcmp(name, "nativePause") == 0) {
        if (num_args < 1) return false;
        VmLockRelease unlocked;
        *result = DexValue::Int(bionic_kudroid_audiotrack_pause(args[0].j));
        return true;
    }
    if (std::strcmp(name, "nativeStop") == 0) {
        if (num_args < 1) return false;
        VmLockRelease unlocked;
        *result = DexValue::Int(bionic_kudroid_audiotrack_stop(args[0].j));
        return true;
    }
    if (std::strcmp(name, "nativeFlush") == 0) {
        if (num_args < 1) return false;
        VmLockRelease unlocked;
        *result = DexValue::Int(bionic_kudroid_audiotrack_flush(args[0].j));
        return true;
    }
    if (std::strcmp(name, "nativeRelease") == 0) {
        if (num_args < 1) return false;
        VmLockRelease unlocked;
        *result = DexValue::Int(bionic_kudroid_audiotrack_release(args[0].j));
        return true;
    }
    if (std::strcmp(name, "nativeSetVolume") == 0) {
        if (num_args < 2) return false;
        *result = DexValue::Int(bionic_kudroid_audiotrack_set_volume(args[0].j, args[1].f));
        return true;
    }
    if (std::strcmp(name, "nativeGetPlaybackHeadPosition") == 0) {
        if (num_args < 1) return false;
        *result = DexValue::Int(bionic_kudroid_audiotrack_head_position(args[0].j));
        return true;
    }
    if (std::strcmp(name, "nativeGetLatencyFrames") == 0) {
        if (num_args < 1) return false;
        *result = DexValue::Int(bionic_kudroid_audiotrack_latency_frames(args[0].j));
        return true;
    }
    (void)interp;
    return false;
}

// GLSurfaceView manages EGL context and swapchain via GraphicsShim.
bool Invoke_android_opengl_GLSurfaceView(Interpreter* interp, const char* name,
                                         const DexValue* args, size_t num_args,
                                         DexValue* result) {
    if (std::strcmp(name, "nativeEglCreate") == 0) {
        if (num_args < 3) return false;
        VmLockRelease unlocked;
        *result = DexValue::Long(bionic_kudroid_gl_surface_create(args[0].i, args[1].i, args[2].i));
        return true;
    }
    if (std::strcmp(name, "nativeEglMakeCurrent") == 0) {
        if (num_args < 1) return false;
        VmLockRelease unlocked;
        *result = DexValue::Int(bionic_kudroid_gl_surface_make_current(args[0].j) ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "nativeEglSwap") == 0) {
        if (num_args < 1) return false;
        VmLockRelease unlocked;
        *result = DexValue::Int(bionic_kudroid_gl_surface_swap(args[0].j) ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "nativeEglDestroy") == 0) {
        if (num_args < 1) return false;
        VmLockRelease unlocked;
        bionic_kudroid_gl_surface_destroy(args[0].j);
        return true;
    }
    (void)interp;
    return false;
}

// Forward keyboard requests to the host; report whether host listened.
bool Invoke_android_view_inputmethod_InputMethodManager(Interpreter* /*interp*/,
                                                        const char* name,
                                                        const DexValue* args,
                                                        size_t num_args,
                                                        DexValue* result) {
    if (std::strcmp(name, "showSoftInputNative") == 0) {
        const int flags = num_args > 0 ? args[0].i : 0;
        *result = DexValue::Int(kudroid_show_soft_input(flags) != 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "hideSoftInputNative") == 0) {
        *result = DexValue::Int(kudroid_hide_soft_input() != 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isSoftInputVisibleNative") == 0) {
        *result = DexValue::Int(kudroid_is_soft_input_visible() != 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "setSoftInputTraitsNative") == 0) {
        if (num_args >= 2) kudroid_set_soft_input_traits(args[0].i, args[1].i);
        return true;
    }
    return false;
}

// Device memory from the host; fixed values mislead cache sizing.
bool Invoke_kudroid_memory(Interpreter* /*interp*/, const char* name,
                           const DexValue* /*args*/, size_t /*num_args*/,
                           DexValue* result) {
    if (std::strcmp(name, "nativeTotalMemory") == 0) {
        *result = DexValue::Long(
            static_cast<int64_t>(kudroid::query_system_memory().total_bytes));
        return true;
    }
    if (std::strcmp(name, "nativeAvailableMemory") == 0) {
        const kudroid::SystemMemory mem = kudroid::query_system_memory();
        // Prefer per-process headroom; system-wide invites jetsam kills.
        const uint64_t value = mem.process_available_bytes > 0
                                   ? mem.process_available_bytes
                                   : mem.available_bytes;
        *result = DexValue::Long(static_cast<int64_t>(value));
        return true;
    }
    if (std::strcmp(name, "nativeSystemAvailableMemory") == 0) {
        *result = DexValue::Long(
            static_cast<int64_t>(kudroid::query_system_memory().available_bytes));
        return true;
    }
    if (std::strcmp(name, "nativeProcessResidentMemory") == 0) {
        *result = DexValue::Long(
            static_cast<int64_t>(kudroid::query_system_memory().process_resident_bytes));
        return true;
    }
    if (std::strcmp(name, "nativeIsLowMemory") == 0) {
        *result = DexValue::Int(kudroid::query_system_memory().low_memory ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "nativeMemoryClass") == 0) {
        *result = DexValue::Int(kudroid::memory_class_mb());
        return true;
    }
    if (std::strcmp(name, "nativeLargeMemoryClass") == 0) {
        *result = DexValue::Int(kudroid::large_memory_class_mb());
        return true;
    }

    // Heap figures use the process budget; keep total + free == max.
    if (std::strcmp(name, "maxMemory") == 0) {
        const kudroid::SystemMemory mem = kudroid::query_system_memory();
        const uint64_t ceiling = mem.process_available_bytes > 0
                                     ? mem.process_resident_bytes + mem.process_available_bytes
                                     : mem.total_bytes;
        *result = DexValue::Long(static_cast<int64_t>(ceiling));
        return true;
    }
    if (std::strcmp(name, "totalMemory") == 0) {
        const kudroid::SystemMemory mem = kudroid::query_system_memory();
        // What the process has taken so far.
        uint64_t taken = mem.process_resident_bytes;
        if (taken == 0) taken = mem.total_bytes - mem.available_bytes;
        *result = DexValue::Long(static_cast<int64_t>(taken));
        return true;
    }
    if (std::strcmp(name, "freeMemory") == 0) {
        const kudroid::SystemMemory mem = kudroid::query_system_memory();
        const uint64_t free_bytes = mem.process_available_bytes > 0
                                        ? mem.process_available_bytes
                                        : mem.available_bytes;
        *result = DexValue::Long(static_cast<int64_t>(free_bytes));
        return true;
    }
    if (std::strcmp(name, "availableProcessors") == 0) {
        // Thread-pool sizes are derived from this, and it must agree with what native
        // code sees through sysconf and /proc/cpuinfo: a guest that sizes a Java pool
        // from Runtime and a native pool from sysconf would otherwise build two pools
        // for two different machines. Same source for both now.
        *result = DexValue::Int(
            static_cast<int32_t>(kudroid::query_cpu_topology().total_cores));
        return true;
    }
    return false;
}

// java.lang.Runtime
static LoadLibraryCallback g_load_lib_cb = nullptr;

