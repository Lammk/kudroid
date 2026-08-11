#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace kudroid {

/// chuyển đổi một tệp dex thành một tệp jar chứa các lớp giả.
///
/// điều này phân tích cấu trúc dex (lớp, phương thức, trường) và tạo ra một
/// tệp jar có một tệp .class cho mỗi lớp. mỗi lớp có:
///   - một hàm tạo mặc định
///   - tất cả các phương thức được khai báo trong dex, với chữ ký chính xác nhưng thân
///     trống (trả về giá trị mặc định)
///
/// điều này đủ để các ứng dụng tải java khi khởi động mà không gặp sự cố (các
/// lớp và phương thức được phân giải), trong khi logic phương thức thực tế bị để trống
/// cho đến khi bản dịch mã byte thực sự được triển khai (do các vấn đề github thúc đẩy).
///
/// đầu ra là một tệp jar (zip) có thể được cung cấp cho jvm avian dưới dạng đường dẫn lớp.
class DexToJar {
public:
    /// dữ liệu lớp đã phân tích.
    struct ClassInfo {
        std::string name;          // tên nội bộ jvm, ví dụ "com/foo/bar"
        std::string superName;     // tên nội bộ jvm của lớp cha
        uint32_t accessFlags;
        std::vector<std::string> interfaces; // tên nội bộ jvm
        // phương thức: {tên, bộ mô tả}
        std::vector<std::pair<std::string, std::string>> methods;
        // trường: {tên, bộ mô tả}
        std::vector<std::pair<std::string, std::string>> fields;
    };

    /// chuyển đổi tệp dex thành tệp jar. trả về true nếu thành công.
    /// nếu thành công, `outjar` chứa các byte của tệp jar.
    static bool convert(const std::string& dexPath, std::vector<uint8_t>& outJar,
                        std::string* error = nullptr);

    /// chuyển đổi các byte dex thành một tệp jar. trả về true nếu thành công.
    static bool convertBytes(const std::vector<uint8_t>& dexBytes,
                             std::vector<uint8_t>& outJar,
                             std::string* error = nullptr);

private:
    // các hàm trợ giúp nội bộ (được triển khai trong dextojar.cpp)
    static bool parseDex(const std::vector<uint8_t>& dex, std::vector<ClassInfo>& classes,
                         std::string* error);
    static bool buildJar(const std::vector<ClassInfo>& classes,
                         std::vector<uint8_t>& outJar, std::string* error);
};

} // namespace kudroid
