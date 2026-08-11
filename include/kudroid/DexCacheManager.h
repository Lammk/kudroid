#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace kudroid {

/// cachemanager cho các tệp dex đã dịch.
///
/// dịch một tệp dex sang dạng mà ios có thể hiểu được (hiện tại là jar cho
/// jvm avian) và lưu trữ kết quả được khóa bằng:
///   - hàm băm sha-256 của tệp dex gốc (phát hiện cập nhật apk)
///   - số nguyên phiên bản công cụ (phát hiện nâng cấp trình biên dịch)
///
/// nếu hàm băm hoặc phiên bản thay đổi, bộ đệm được coi là cũ
/// và dex được dịch lại. điều này mô phỏng mô hình bộ đệm .oat/.odex của art.
///
/// bố cục bộ đệm:
///   <cachedir>/<dex_sha256>_v<version>.bin        — dữ liệu đã dịch
///   <cachedir>/<dex_sha256>_v<version>.meta.json  — siêu dữ liệu (hàm băm, phiên bản)
class DexCacheManager {
public:
    static DexCacheManager& getInstance();

    /// đặt thư mục bộ đệm (ví dụ: documents/android_cache).
    void setCacheDirectory(const std::string& dir);

    /// trả về thư mục bộ đệm hiện tại.
    const std::string& cacheDirectory() const { return cacheDir_; }

    /// kiểm tra xem mục bộ đệm hợp lệ có tồn tại cho phiên bản dex + công cụ đã cho hay không.
    /// chỉ trả về true nếu cả hàm băm dex và phiên bản đều khớp.
    bool hasValidCache(const std::string& dexPath, int toolVersion);

    /// tải dữ liệu đã dịch được lưu trong bộ đệm cho dex + phiên bản đã cho.
    /// trả về true và điền vào `out` nếu thành công.
    bool loadCache(const std::string& dexPath, int toolVersion,
                   std::vector<uint8_t>& out);

    /// lưu dữ liệu đã dịch vào bộ đệm cho dex + phiên bản đã cho.
    /// ghi nguyên tử (tệp tmp + đổi tên) để tránh hỏng dữ liệu khi gặp sự cố.
    bool saveCache(const std::string& dexPath, int toolVersion,
                   const std::vector<uint8_t>& data);

    /// dịch một tệp dex thành một tệp jar (thông qua dextojar), sử dụng bộ đệm.
    ///
    /// nếu một mục bộ đệm hợp lệ tồn tại (hàm băm + phiên bản khớp), tệp jar trong bộ đệm sẽ được
    /// tải. ngược lại dex được dịch, lưu vào bộ đệm và trả về.
    /// trả về true và điền vào `outjar` nếu thành công.
    bool translateAndCache(const std::string& dexPath, int toolVersion,
                           std::vector<uint8_t>& outJar, std::string* error = nullptr);

    /// xóa tất cả các mục bộ đệm cho dex đã cho (tất cả các phiên bản).
    void clearCacheForDex(const std::string& dexPath);

    /// xóa toàn bộ nội dung thư mục bộ đệm.
    void clearCache();

    /// tính toán mã băm hex sha-256 của một tệp. trả về chuỗi rỗng nếu có lỗi.
    static std::string sha256File(const std::string& path);

private:
    DexCacheManager() = default;

    /// tính toán đường dẫn tệp bộ đệm (không có phần mở rộng) cho một dex + phiên bản.
    std::string cacheBasePath(const std::string& dexPath, int version) const;

    /// tính toán mã băm hex sha-256 của một bộ đệm byte.
    static std::string sha256(const uint8_t* data, size_t len);

    std::string cacheDir_;
};

} // namespace kudroid
