// Host test cho DexJniEnv: hai chiều Java ⇄ native.
//
// Chiều 1: bytecode invoke-static gọi method native đã RegisterNatives.
// Chiều 2: hàm native đó dùng JNIEnv gọi ngược vào bytecode, đọc/ghi field,
//          tạo string/array, ném exception.
//
// Vẫn dùng thủ thuật build hai lượt như test_kuart_object: bytecode tham chiếu
// entity bằng index, index chỉ chốt sau khi builder sort xong mọi bảng.
#include "kudroid/kuart/DexJniEnv.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dex_builder.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using namespace dexbuild;
using kudroid::kuart::DexValue;

uint16_t Op11x(uint8_t op, uint8_t a) { return static_cast<uint16_t>(op | (a << 8)); }
void Op21c(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint16_t idx) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(idx);
}
void Op22c(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint8_t b, uint16_t idx) {
    code->push_back(static_cast<uint16_t>(op | (a << 8) | (b << 12)));
    code->push_back(idx);
}
void Op35c(std::vector<uint16_t>* code, uint8_t op, uint16_t idx,
           const std::vector<uint8_t>& regs) {
    code->push_back(static_cast<uint16_t>(op | (regs.size() << 12)));
    code->push_back(idx);
    uint16_t packed = 0;
    for (size_t i = 0; i < regs.size() && i < 4; ++i) {
        packed |= static_cast<uint16_t>((regs[i] & 0xF) << (i * 4));
    }
    code->push_back(packed);
}

constexpr uint8_t kOpReturnVoid = 0x0e;
constexpr uint8_t kOpReturn = 0x0f;
constexpr uint8_t kOpReturnObject = 0x11;
constexpr uint8_t kOpMoveResult = 0x0a;
constexpr uint8_t kOpNewInstance = 0x22;
constexpr uint8_t kOpIget = 0x52;
constexpr uint8_t kOpIput = 0x59;
constexpr uint8_t kOpInvokeDirect = 0x70;
constexpr uint8_t kOpInvokeStatic = 0x71;

// ACC_PUBLIC|ACC_STATIC|ACC_NATIVE — method không có code item.
constexpr uint32_t kAccPublicStaticNative = 0x1 | 0x8 | 0x100;

struct Specs {
    FieldSpec value{"value", "I", 0x1};
    FieldSpec total{"total", "I", 0x9};

    MethodSpec nat_ctor;
    MethodSpec get_value;      // virtual: trả this.value
    MethodSpec native_add;     // static native (II)I
    MethodSpec native_probe;   // static native (LNat;)I — gọi ngược vào Java
    MethodSpec call_native;    // static: gọi nativeAdd
    MethodSpec call_probe;     // static: gọi nativeProbe
    MethodSpec make_nat;       // static: new Nat, set value, trả object

    Specs() {
        nat_ctor.name = "<init>";
        nat_ctor.access_flags = 0x10001;

        get_value.name = "getValue";
        get_value.return_type = "I";

        native_add.name = "nativeAdd";
        native_add.return_type = "I";
        native_add.params = {"I", "I"};
        native_add.access_flags = kAccPublicStaticNative;

        native_probe.name = "nativeProbe";
        native_probe.return_type = "I";
        native_probe.params = {"LNat;"};
        native_probe.access_flags = kAccPublicStaticNative;

        call_native.name = "callNative";
        call_native.return_type = "I";
        call_native.params = {"I", "I"};
        call_native.access_flags = 0x9;

        call_probe.name = "callProbe";
        call_probe.return_type = "I";
        call_probe.params = {"LNat;"};
        call_probe.access_flags = 0x9;

        make_nat.name = "makeNat";
        make_nat.return_type = "LNat;";
        make_nat.params = {"I"};
        make_nat.access_flags = 0x9;
    }
};

std::vector<ClassSpec> BuildClasses(const Specs& s) {
    ClassSpec object;
    object.descriptor = "Ljava/lang/Object;";
    object.superclass = "";
    {
        MethodSpec ctor;
        ctor.name = "<init>";
        ctor.access_flags = 0x10001;
        ctor.code = {Op11x(kOpReturnVoid, 0)};
        ctor.registers_size = 1;
        ctor.ins_size = 1;
        object.direct_methods.push_back(ctor);
    }

    ClassSpec string;
    string.descriptor = "Ljava/lang/String;";
    string.superclass = "Ljava/lang/Object;";

    ClassSpec nat;
    nat.descriptor = "LNat;";
    nat.superclass = "Ljava/lang/Object;";
    nat.instance_fields = {s.value};
    nat.static_fields = {s.total};
    nat.direct_methods = {s.nat_ctor,   s.native_add,  s.native_probe,
                          s.call_native, s.call_probe, s.make_nat};
    nat.virtual_methods = {s.get_value};

    return {object, string, nat};
}

// Runtime dùng chung cho các hàm native bên dưới; chúng chỉ nhận JNIEnv nên
// mọi thứ khác phải lấy qua env hoặc biến file-scope này.
kudroid::kuart::DexClassLinker* g_linker = nullptr;

// ── các hàm native được RegisterNatives ──

jint JNICALL NativeAdd(JNIEnv* env, jclass clazz, jint a, jint b) {
    Check(env != nullptr, "native nhận JNIEnv khác null");
    Check(clazz != nullptr, "native static nhận jclass khác null");
    return a + b;
}

// Kiểm tra toàn bộ chiều native → Java trong một lần gọi.
jint JNICALL NativeProbe(JNIEnv* env, jclass, jobject nat) {
    // FindClass nhận tên không có L; — DexJniEnv phải tự bọc thành descriptor.
    jclass k = env->FindClass("Nat");
    Check(k != nullptr, "FindClass(\"Nat\") không cần L;");

    jclass obj_class = env->GetObjectClass(nat);
    Check(obj_class == k, "GetObjectClass trả đúng class");
    Check(env->IsInstanceOf(nat, k) == JNI_TRUE, "IsInstanceOf");

    jclass super = env->GetSuperclass(k);
    Check(super != nullptr, "GetSuperclass trả java/lang/Object");
    Check(env->IsAssignableFrom(k, super) == JNI_TRUE, "IsAssignableFrom(Nat, Object)");

    // Gọi ngược vào bytecode.
    jmethodID get_value = env->GetMethodID(k, "getValue", "()I");
    Check(get_value != nullptr, "GetMethodID(getValue)");
    const jint from_java = env->CallIntMethod(nat, get_value);
    Check(from_java == 11, "CallIntMethod chạy bytecode, trả 11");

    // Field instance.
    jfieldID fid = env->GetFieldID(k, "value", "I");
    Check(fid != nullptr, "GetFieldID(value)");
    Check(env->GetIntField(nat, fid) == 11, "GetIntField");
    env->SetIntField(nat, fid, 25);
    Check(env->GetIntField(nat, fid) == 25, "SetIntField rồi đọc lại");
    Check(env->CallIntMethod(nat, get_value) == 25, "bytecode thấy field vừa ghi");

    // Field static.
    jfieldID sfid = env->GetStaticFieldID(k, "total", "I");
    Check(sfid != nullptr, "GetStaticFieldID(total)");
    env->SetStaticIntField(k, sfid, 77);
    Check(env->GetStaticIntField(k, sfid) == 77, "static field ghi/đọc");

    // Gọi static method của Java (makeNat) rồi kiểm tra object trả về.
    jmethodID make = env->GetStaticMethodID(k, "makeNat", "(I)LNat;");
    Check(make != nullptr, "GetStaticMethodID(makeNat)");
    jobject made = env->CallStaticObjectMethod(k, make, 99);
    Check(made != nullptr, "CallStaticObjectMethod trả object");
    Check(env->GetIntField(made, fid) == 99, "object do bytecode tạo có field đúng");

    // Chuỗi.
    jstring str = env->NewStringUTF("kudroid");
    Check(str != nullptr, "NewStringUTF");
    Check(env->GetStringUTFLength(str) == 7, "GetStringUTFLength");
    const char* utf = env->GetStringUTFChars(str, nullptr);
    Check(utf != nullptr && std::strcmp(utf, "kudroid") == 0, "GetStringUTFChars");
    env->ReleaseStringUTFChars(str, utf);
    char region[8] = {0};
    env->GetStringUTFRegion(str, 1, 3, region);
    Check(std::strcmp(region, "udr") == 0, "GetStringUTFRegion");

    // Mảng nguyên thủy.
    jintArray arr = env->NewIntArray(4);
    Check(arr != nullptr, "NewIntArray");
    Check(env->GetArrayLength(arr) == 4, "GetArrayLength");
    const jint src[4] = {5, 6, 7, 8};
    env->SetIntArrayRegion(arr, 0, 4, src);
    jint dst[4] = {0, 0, 0, 0};
    env->GetIntArrayRegion(arr, 0, 4, dst);
    Check(dst[0] == 5 && dst[3] == 8, "Set/GetIntArrayRegion");
    jint* elems = env->GetIntArrayElements(arr, nullptr);
    Check(elems != nullptr && elems[2] == 7, "GetIntArrayElements trỏ thẳng vào mảng");
    elems[2] = 70;
    env->ReleaseIntArrayElements(arr, elems, 0);
    env->GetIntArrayRegion(arr, 2, 1, dst);
    Check(dst[0] == 70, "ghi qua GetIntArrayElements có hiệu lực");

    // Mảng object.
    jclass string_class = env->FindClass("java/lang/String");
    Check(string_class != nullptr, "FindClass(java/lang/String)");
    jobjectArray objs = env->NewObjectArray(2, string_class, nullptr);
    Check(objs != nullptr && env->GetArrayLength(objs) == 2, "NewObjectArray");
    env->SetObjectArrayElement(objs, 1, str);
    Check(env->GetObjectArrayElement(objs, 1) == str, "Set/GetObjectArrayElement");

    // Reference.
    jobject global = env->NewGlobalRef(nat);
    Check(env->IsSameObject(global, nat) == JNI_TRUE, "NewGlobalRef cùng object");
    Check(env->GetObjectRefType(global) == JNIGlobalRefType, "GetObjectRefType global");
    env->DeleteGlobalRef(global);

    env->PushLocalFrame(8);
    jstring inner = env->NewStringUTF("tmp");
    jobject kept = env->PopLocalFrame(inner);
    Check(kept != nullptr, "PopLocalFrame giữ lại kết quả");

    // JavaVM.
    JavaVM* vm = nullptr;
    Check(env->GetJavaVM(&vm) == JNI_OK && vm != nullptr, "GetJavaVM");
    JNIEnv* from_vm = nullptr;
    Check(vm->GetEnv(reinterpret_cast<void**>(&from_vm), JNI_VERSION_1_6) == JNI_OK &&
              from_vm == env,
          "JavaVM::GetEnv trả lại đúng JNIEnv");

    // Exception.
    Check(env->ExceptionCheck() == JNI_FALSE, "chưa có exception");
    env->ThrowNew(k, "test");
    Check(env->ExceptionCheck() == JNI_TRUE, "ThrowNew đặt exception");
    Check(env->ExceptionOccurred() != nullptr, "ExceptionOccurred");
    env->ExceptionClear();
    Check(env->ExceptionCheck() == JNI_FALSE, "ExceptionClear");

    return from_java + env->GetIntField(nat, fid);  // 11 + 25
}

}  // namespace

int main() {
    std::printf("=== KuART JNI: Java ⇄ native ===\n");

    Specs probe;
    DexBuilder index_builder;
    index_builder.Build(BuildClasses(probe));

    const uint16_t kNatType = static_cast<uint16_t>(index_builder.TypeIndexOf("LNat;"));
    const uint16_t kFieldValue =
        static_cast<uint16_t>(index_builder.FieldIndexOf("LNat;", probe.value));
    const uint16_t kMethodNatCtor =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LNat;", probe.nat_ctor));
    const uint16_t kMethodNativeAdd =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LNat;", probe.native_add));
    const uint16_t kMethodNativeProbe =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LNat;", probe.native_probe));

    Specs s;
    s.nat_ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.nat_ctor.registers_size = 1;
    s.nat_ctor.ins_size = 1;

    // Nat.getValue()I { return this.value; }
    {
        std::vector<uint16_t> c;
        Op22c(&c, kOpIget, 0, 1, kFieldValue);
        c.push_back(Op11x(kOpReturn, 0));
        s.get_value.code = c;
        s.get_value.registers_size = 2;
        s.get_value.ins_size = 1;
    }
    // Nat.callNative(int a, int b) { return nativeAdd(a, b); }
    {
        std::vector<uint16_t> c;
        Op35c(&c, kOpInvokeStatic, kMethodNativeAdd, {1, 2});
        c.push_back(Op11x(kOpMoveResult, 0));
        c.push_back(Op11x(kOpReturn, 0));
        s.call_native.code = c;
        s.call_native.registers_size = 3;
        s.call_native.ins_size = 2;
        s.call_native.outs_size = 2;
    }
    // Nat.callProbe(Nat n) { return nativeProbe(n); }
    {
        std::vector<uint16_t> c;
        Op35c(&c, kOpInvokeStatic, kMethodNativeProbe, {1});
        c.push_back(Op11x(kOpMoveResult, 0));
        c.push_back(Op11x(kOpReturn, 0));
        s.call_probe.code = c;
        s.call_probe.registers_size = 2;
        s.call_probe.ins_size = 1;
        s.call_probe.outs_size = 1;
    }
    // Nat.makeNat(int v) { Nat n = new Nat(); n.value = v; return n; }
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpNewInstance, 0, kNatType);
        Op35c(&c, kOpInvokeDirect, kMethodNatCtor, {0});
        Op22c(&c, kOpIput, 1, 0, kFieldValue);
        c.push_back(Op11x(kOpReturnObject, 0));
        s.make_nat.code = c;
        s.make_nat.registers_size = 2;
        s.make_nat.ins_size = 1;
        s.make_nat.outs_size = 1;
    }

    DexBuilder builder;
    const std::vector<uint8_t> dex = builder.Build(BuildClasses(s));
    std::printf("DEX synthetic: %zu bytes\n", dex.size());
    Check(builder.TypeIndexOf("LNat;") == kNatType, "index ổn định giữa hai lượt build");

    kudroid::kuart::DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(dex.data(), dex.size(), "test.dex", &error)) {
        std::printf("  FAIL AddDexFile: %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }
    g_linker = &linker;

    kudroid::kuart::DexClass* nat = linker.FindClass("LNat;");
    if (nat == nullptr) {
        std::printf("  FAIL FindClass(LNat;): %s\n=== FAILED ===\n", linker.last_error().c_str());
        return 1;
    }

    kudroid::kuart::Interpreter interp(&linker);
    kudroid::kuart::DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);

    // Method native chưa liên kết thì invoke phải ném UnsatisfiedLinkError.
    {
        kudroid::kuart::DexMethod* m = nat->FindDirectMethod("callNative", "(II)I");
        Check(m != nullptr, "tìm được callNative");
        DexValue args[2] = {DexValue::Int(1), DexValue::Int(2)};
        interp.ClearPendingException();
        interp.Execute(m, args, 2);
        Check(interp.HasPendingException(), "native chưa liên kết → có exception");
        interp.ClearPendingException();
    }

    // RegisterNatives như JNI_OnLoad của game. JNINativeMethod khai báo char*
    // (không const) nên tên/chữ ký phải là buffer ghi được.
    char n1[] = "nativeAdd";
    char s1[] = "(II)I";
    char n2[] = "nativeProbe";
    char s2[] = "(LNat;)I";
    const JNINativeMethod natives[] = {
        {n1, s1, reinterpret_cast<void*>(&NativeAdd)},
        {n2, s2, reinterpret_cast<void*>(&NativeProbe)},
    };
    Check(jni.RegisterNatives(nat, natives, 2) == JNI_OK, "RegisterNatives");

    // Chiều 1: bytecode → native.
    {
        kudroid::kuart::DexMethod* m = nat->FindDirectMethod("callNative", "(II)I");
        DexValue args[2] = {DexValue::Int(20), DexValue::Int(22)};
        interp.ClearPendingException();
        const DexValue r = interp.Execute(m, args, 2);
        Check(!interp.HasPendingException(), "invoke native không ném exception");
        Check(r.i == 42, "bytecode gọi native, nhận 42");
    }

    // RegisterNatives với method không tồn tại phải báo lỗi, không crash.
    {
        char bad_name[] = "khongCo";
        char bad_sig[] = "()V";
        const JNINativeMethod bad[] = {{bad_name, bad_sig, reinterpret_cast<void*>(&NativeAdd)}};
        Check(jni.RegisterNatives(nat, bad, 1) == JNI_ERR, "RegisterNatives method sai → ERR");
    }

    // Chiều 2: bytecode → native → bytecode, qua toàn bộ vtable JNIEnv.
    {
        kudroid::kuart::DexMethod* make = nat->FindDirectMethod("makeNat", "(I)LNat;");
        Check(make != nullptr, "tìm được makeNat");
        DexValue mk_arg[1] = {DexValue::Int(11)};
        interp.ClearPendingException();
        const DexValue obj = interp.Execute(make, mk_arg, 1);
        Check(obj.l != nullptr, "makeNat trả object");

        kudroid::kuart::DexMethod* m = nat->FindDirectMethod("callProbe", "(LNat;)I");
        DexValue args[1] = {DexValue::Ref(obj.l)};
        interp.ClearPendingException();
        const DexValue r = interp.Execute(m, args, 1);
        Check(!interp.HasPendingException(), "nativeProbe không để lại exception");
        Check(r.i == 36, "nativeProbe trả 11 + 25 = 36");
    }

    // Gọi Java trực tiếp từ host qua CallJavaA (đường mà JNI vtable dùng).
    {
        kudroid::kuart::DexMethod* make = nat->FindDirectMethod("makeNat", "(I)LNat;");
        jvalue mk[1];
        mk[0].i = 7;
        const DexValue obj = jni.CallJavaA(nullptr, make, mk, /*virtual_dispatch=*/false);
        Check(obj.l != nullptr, "CallJavaA static trả object");

        kudroid::kuart::DexMethod* getter = nat->FindVirtualMethod("getValue", "()I");
        const DexValue v = jni.CallJavaA(obj.l, getter, nullptr, /*virtual_dispatch=*/true);
        Check(v.i == 7, "CallJavaA virtual trả 7");
    }

    // Bảng ref: local tích luỹ trong frame ngoài cùng, global đã xoá hết.
    Check(jni.NumGlobalRefs() == 0, "global ref đã dọn sạch");
    Check(jni.NumLocalRefs() > 0, "local ref còn trong frame ngoài cùng");

    std::printf("=== %s (%d lỗi) ===\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
