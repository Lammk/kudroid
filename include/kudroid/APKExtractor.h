#pragma once

#include <string>

namespace kudroid {

class APKExtractor {
public:
    // Giải nén một APK thường: lib/arm64-v8a/*.so, *.dex, assets/, AndroidManifest.xml
    // vào targetDirectory (giữ nguyên cây thư mục).
    static bool extract_apk(const std::string& apkPath,
                            const std::string& targetDirectory);

    // Giải nén một split APK (một phần của bundle): như extract_apk nhưng không
    // bắt buộc phải có entry (split chỉ chứa res/ là bình thường) và bỏ qua
    // AndroidManifest.xml (manifest của split là wrapper `<split ...>`, chỉ cần
    // manifest của base).
    static bool extract_split(const std::string& apkPath,
                              const std::string& targetDirectory);

    // True nếu file là container split-APK (.xapk / .apks / .apkm): một ZIP có
    // entry .apk ở top-level (hoặc dưới splits/).
    static bool is_bundle_container(const std::string& path);

    // Giải nén container: lấy các APK liên quan (base + arm64 + config không phải
    // ABI khác), gộp lib/assets/dex/manifest của chúng vào targetDirectory.
    static bool extract_bundle(const std::string& containerPath,
                               const std::string& targetDirectory);

    static const std::string& lastError();
};

} // namespace kudroid
