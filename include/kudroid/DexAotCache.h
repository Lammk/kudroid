#pragma once

#include <string>

namespace kudroid {

/// Pipeline AOT caching DEX→JAR (dịch bytecode) cho Avian JVM.
///
/// Avian không hiểu file `.dex` nên trước khi init JVM ta dịch toàn bộ
/// classes*.dex của APK đã extract thành một `classes.jar` bằng dex2jar
/// (`d2j-dex2jar.sh`), lưu vào thư mục dalvik-cache:
///
///   <cache_dir>/classes.jar
///   <cache_dir>/cache.hash   — SHA-256 của nội dung classes*.dex (nối tiếp)
///
/// Nếu hash khớp → dùng thẳng classes.jar (cache hit, không gọi dex2jar).
/// Nếu lệch/thiếu → dịch lại bằng dex2jar và ghi hash mới (ghi nguyên tử).
class DexAotCache {
public:
    /// Dịch toàn bộ classes*.dex trong `apk_extracted_path` (thư mục gốc APK đã
    /// extract, chứa classes.dex / classes2.dex ...) thành `classes.jar` trong
    /// `cache_dir`, dùng cache theo SHA-256. Trả về đường dẫn classes.jar nếu
    /// thành công; chuỗi rỗng nếu lỗi (chi tiết trong `error` nếu không null).
    static std::string translate_dex_if_needed(const std::string& apk_extracted_path,
                                               const std::string& cache_dir,
                                               std::string* error = nullptr);

    /// Tên script dex2jar: đọc env KUDROID_DEX2JAR nếu có, nếu không dùng
    /// "d2j-dex2jar.sh" (phải nằm trong PATH).
    static std::string dex2jar_command();
};

} // namespace kudroid
