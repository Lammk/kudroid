#pragma once

#include <string>

namespace kudroid {

// Tự sinh stub class cho các class android/* mà app tham chiếu nhưng
// framework chưa có — giải quyết ClassNotFoundException/NoClassDefFoundError
// kiểu "PorterDuffColorFilter" mà không cần vá tay từng class vào framework.
//
// Cách hoạt động (chạy TRƯỚC khi khởi JVM, mỗi lần launch):
//  1. Đọc classes.jar của app, parse constant pool từng .class, thu thập
//     MỌI tham chiếu android/* (superclass, interface, kiểu field/method —
//     constant pool chứa đủ).
//  2. So với danh sách class android/* đã có trong chính classes.jar
//     (jar đã được merge với framework) → ra danh sách thiếu.
//  3. Sinh stub.jar chứa .class tối thiểu cho từng class thiếu:
//     - class: extends Object + constructor <init>()V rỗng (instantiable)
//     - interface (app implements nó): interface trống
//  4. stub.jar được thêm vào -Xbootclasspath/a → JVM resolve được hierarchy.
//
// Stub chỉ đủ để class LOAD được — gọi method thật của nó sẽ
// NoSuchMethodError (lỗi rõ ràng, chỉ đúng method bị thiếu, dễ debug hơn
// ClassNotFoundException mù mờ chặn cả Activity).
class AutoStub {
public:
    /// Quét `appJarPath`, sinh `outStubJarPath` chứa stub cho class thiếu.
    /// Trả về số stub đã sinh (0 = không thiếu gì hoặc lỗi đọc jar).
    static int build_stub_jar(const std::string& appJarPath,
                              const std::string& outStubJarPath);
};

} // namespace kudroid
