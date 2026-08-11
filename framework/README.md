# KuDroid Android Framework

A minimal Android framework (Java) that provides the `android.*` classes apps
need to load Java at startup, before switching to native `.so` code.

## Purpose

Most native games (Unity IL2CPP, Godot, SDL) only touch Java briefly at
startup (`JNI_OnLoad`, `ANativeActivity_onCreate`), then run entirely through
C/C++ `.so` libraries. This framework provides just enough `android.*` classes
so those apps don't crash while loading Java.

## What's included

**Real implementations** (affect app behavior):
- `android.util.Log` → maps to `__android_log_print`
- `android.os.Handler` / `Looper` / `MessageQueue` / `Message` / `Bundle`
- `android.app.Activity` / `Application` / `Dialog` / `AlertDialog`
- `android.content.Context` / `ContextWrapper` / `Intent` / `SharedPreferences`
- `android.view.View` / `ViewGroup` / `MotionEvent` / `Window`
- `android.widget.TextView` / `Button` / `LinearLayout` / `Toast`
- `android.graphics.*` (Canvas, Paint, Bitmap, Color, Rect, ...)

**Stubs** (return defaults so apps don't crash):
- `android.telephony.TelephonyManager`
- `android.bluetooth.BluetoothAdapter`
- `android.app.NotificationManager` / `Notification`
- `android.location.LocationManager`
- `android.net.wifi.WifiManager`
- `android.hardware.SensorManager`
- `android.media.AudioManager`
- `android.os.Vibrator` / `PowerManager`
- `android.net.ConnectivityManager`
- `android.provider.Settings`

## Building

```bash
# Requires a JDK (javac + jar)
./build.sh                 # produces framework/build/framework.jar
./build.sh --bootimage     # also produces framework/build/boot.jar for Avian
```

## Adding classes

1. Create the `.java` file under `framework/android/<package>/`.
2. Run `./build.sh` to recompile.
3. The JAR is embedded into the KuDroid binary as the Avian boot classpath.

## Contributing

This framework is intentionally minimal. If an app needs a class that's
missing, add it (or open an issue). The goal is to grow it based on real app
needs, not to replicate the full Android SDK.
