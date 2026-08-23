#pragma once

#include <string>

namespace kudroid {

// Tự sinh stub class cho các class android/* mà app tham chiếu nhưng chưa có
// trong classes.jar, rồi GỘP THẲNG vào chính classes.jar đó.
//
// Vì sao gộp vào classes.jar thay vì jar riêng?
//  - Không phụ thuộc việc JVM có duyệt hết mọi entry trên -Xbootclasspath/a
//    hay không (hành vi của Avian với nhiều jar là biến số không kiểm chứng).
//  - classes.jar đã chứa framework merge (từ DexAotCache) nên stub tay và
//    stub tự sinh nằm cùng một nơi, thứ tự classpath đơn giản.
//
// Cách hoạt động (chạy TRƯỚC khi khởi JVM, mỗi lần launch):
//  1. Parse constant pool TỪNG .class trong jar, thu thập MỌI tham chiếu
//     android/*|androidx/* — cả CONSTANT_Class lẫn tên class ẩn trong
//     descriptor Utf8 ("Landroid/graphics/X;" làm kiểu field/tham số — cách
//     MCPE tham chiếu PorterDuffColorFilter).
//  2. Thiếu = referenced − các class android/* hiện có trong jar.
//  3. Sinh .class tối thiểu cho từng cái (class có <init>()V hoặc interface
//     trống nếu app implements nó) và nối vào cuối jar (copy nguyên phần
//     local entries cũ, ghi lại central directory + EOCD).
//  4. Ghi qua file tạm + rename nguyên tử — hỏng thì cache gốc không bị chạm.
//
// Idempotent: lần chạy sau, các stub đã nằm trong jar → present đủ → không
// làm gì (trả về 0). DexAotCache rebuild (APK mới) sẽ xóa classes.jar và
// AutoStub tự đắp lại ở lần launch tiếp theo.
//
// Stub chỉ đủ để class LOAD được — gọi method thật sẽ NoSuchMethodError
// (lỗi rõ ràng, khoanh vùng đúng method thiếu) thay vì ClassNotFoundException
// mù mờ chặn cả Activity.
class AutoStub {
public:
    /// Quét `jarPath`, nối stub cho class android/* còn thiếu vào chính jar
    /// này (ghi qua file tạm + rename). Trả về số stub đã thêm
    /// (0 = không thiếu gì hoặc không đọc được jar).
    static int append_missing_stubs(const std::string& jarPath);
};

} // namespace kudroid
