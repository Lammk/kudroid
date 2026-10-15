# KuDroid Android Framework (Java)

A lightweight Android Java framework providing standard `android.*` and core `java.*` runtime classes required by Android applications and game engines (Unity, Unreal Engine, Godot, NativeActivity, Minecraft) during startup and execution on KuART.

---

## 🎯 Purpose

Most Android native games and apps only touch the Java layer briefly during initialization (`JNI_OnLoad`, `ANativeActivity_onCreate`, activity lifecycle, asset extraction, surface creation), and then execute primarily in native C/C++ ARM64 `.so` libraries.

This framework provides real working implementations of core data structures, graphics math, regular expressions, text formatting, and Android lifecycle classes, while providing safe stubs for peripheral background services.

---

## 📦 Architecture & License Breakdown

The framework comprises 778 Java source files organized into three tiers based on provenance and license terms:

| Tier | Package Scope | Provenance | License |
| :--- | :--- | :--- | :--- |
| **KuDroid Core** | `android.app`, `android.view`, `android.widget`, `android.content`, `java.lang.*` | Custom Written | **MIT** |
| **AOSP Core** | `android.util.*`, `android.graphics.*`, `android.os.*`, `com.android.internal.*`, `libcore.*` | Android 10 (AOSP `android-10.0.0_r47`) | **Apache-2.0** |
| **Harmony Core** | `java.math.*`, `java.util.regex.*`, `java.text.*`, `java.util.Calendar`, `org.apache.harmony.*` | Apache Harmony (`libcore/luni`) | **Apache-2.0** |
| **OpenJDK Core** | `java.util.BitSet`, `java.util.Optional*`, `java.util.PriorityQueue`, `java.util.StringJoiner` | OpenJDK (`libcore/ojluni`) | **GPLv2 + Classpath Exception** |

---

## 🛡️ License Compliance

- **Root License**: KuDroid native C++ core, iOS bridge, and custom Java framework files are licensed under the **MIT License**.
- **Apache 2.0 Components**: All original headers and copyright notices from the Android Open Source Project (AOSP) and the Apache Software Foundation are preserved verbatim.
- **GPLv2 + Classpath Exception**: As granted by Oracle's Classpath Exception, compiling or linking OpenJDK utility files into `framework.dex` or embedding them in `include/kudroid/framework_dex_bytes.h` does not subject the surrounding KuDroid codebase or host applications to copyleft restrictions.
- For complete license texts and attributions, see `NOTICE`, `LICENSE-APACHE-2.0`, and `LICENSE-GPLv2-CPE` at the repository root.

---

## 🔨 Building Framework DEX

Building the framework compiles all Java sources with `javac` (targeting Java 8 bytecode), converts `.class` files into a compact `framework.dex` via `d8/r8`, and generates the embedded C++ byte header `include/kudroid/framework_dex_bytes.h`:

```bash
# Requires JDK (javac) and Python 3
bash framework/build.sh
```

---

## 🧪 Verification

All framework classes are verified by the KuART test suite in `kuart-tests/`:

```bash
# Build and run the framework integration test
cmake --build build --target test_kuart_framework
./build/test_kuart_framework
```
