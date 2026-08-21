## KuDroid is not an emulator

this project simply just a translation layer for ios to running android apps

code is 100% AI writen,i do the debug and testing also tell what it to do next,because just a personal project though anyone who doesn't like it just ignore 

please don't send me death threat

i don't really want to make money from this,just want to make something crazy anyone who like this could fork and star

i don't care what you guy did to the source code after forked it,just do whatever you all want

i might still continue updating it whenever i have time

warning: I recommend using this app with JIT enabled,but you still can use without JIT

SPECIAL THANK TO sakayori!!!! (Support me to do many things,if he doesn't help kudroid will take more longer to complete)


Made by kuzei13 aka Retoshi Kuzei

---

## Build

### Prerequisites
- CMake 3.20+
- A C++17 compiler (clang on macOS/iOS)
- JDK 11+ (for building the Avian JVM and the Android framework)
- Xcode (for iOS builds)

### Build the Android framework (Java)
```bash
./framework/build.sh --bootimage
```
Produces `framework/build/framework.jar` (and `boot.jar` for Avian embedding).

### Build Avian JVM for iOS
```bash
cd third_party/jvm/avian
make platform=ios arch=arm64 use-clang=true
```
Produces `build/libavian.a` + bootimage. The CMake build links this if present.

### Build KuDroid core (Linux)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Build KuDroid core (iOS)
```bash
cmake -B build-ios \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build-ios --parallel
```

### CI
GitHub Actions builds everything automatically:
- `build.yml` — main build (Linux, macOS, iOS, test .so files)
- `build-avian.yml` — builds the Avian JVM for iOS

## Architecture

```
src/
  elf_loader.cpp        Custom ELF64 loader (parse/map/relocate)
  kudroid_jni.cpp       JNI bridge backed by Avian JVM
  kudroid_bridge.cpp    C bridge + crash handlers + test entry points
  shims/
    SyscallShim.cpp     pthread/mmap/clock/futex/epoll/dlopen/TLS
    GraphicsShim.cpp    ANativeWindow/EGL → CAMetalLayer
    InputShim.cpp       AInputQueue + AMotionEvent + sensors
    AudioShim.cpp       OpenSL ES / AAudio stubs
framework/
  android/              Minimal Android framework (Java)
  build.sh              Compile framework → JAR
ios-app/
  KuDroidShell/         SwiftUI app (Apps + Debug tabs)
```

## License

MIT — see [LICENSE](LICENSE). Do whatever you want with the code.

