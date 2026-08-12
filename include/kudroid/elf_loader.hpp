#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

namespace kudroid {

/// đăng ký module elf guest đã map (base/size/path) để crash handler
/// symbolicate được pc nằm trong guest .so (dladdr không biết region do
/// ELF loader mmap nên trước đây in "no symbol"). An toàn gọi nhiều lần.
extern "C" void kudroid_register_guest_module(void* base, std::size_t size,
                                              const char* path);

/// tìm module guest chứa địa chỉ addr; ghi "path+0x<offset>" vào out nếu tìm thấy.
/// trả về true nếu tìm thấy. Dùng trong crash handler (chỉ đọc, không lock).
extern "C" bool kudroid_lookup_guest_module(void* addr, char* out, std::size_t outSize);

class LibraryManager;

/// bề mặt trình tải elf64 (arm64) tối thiểu.
/// điểm vào giai đoạn 1 — phân tích, ánh xạ và các hàm giả định vị lại.
class ElfLoader {
public:
    struct Segment {
        std::uint64_t vaddr  = 0;   ///< địa chỉ ảo mục tiêu
        std::uint64_t offset = 0;   ///< phần bù tệp của dữ liệu pt_load
        std::uint64_t filesz = 0;   ///< số byte trong tệp
        std::uint64_t memsz  = 0;   ///< số byte trong bộ nhớ (>= filesz)
        std::uint32_t flags  = 0;   ///< pf_r | pf_w | pf_x
    };

    explicit ElfLoader(std::string path);
    ~ElfLoader();

    // không thể sao chép, có thể di chuyển
    ElfLoader(const ElfLoader&) = delete;
    ElfLoader& operator=(const ElfLoader&) = delete;
    ElfLoader(ElfLoader&&) noexcept;
    ElfLoader& operator=(ElfLoader&&) noexcept;

    /// phân tích các tiêu đề elf64 và điền vào danh sách phân đoạn.
    bool parse();

    /// ánh xạ các phân đoạn pt_load vào bộ nhớ có thể thực thi.
    bool map();

    /// áp dụng các định vị lại và phân giải các ký hiệu động.
    bool relocate();

    void setLibraryManager(LibraryManager* manager) { libraryManager_ = manager; }

    [[nodiscard]] std::uint64_t entryPoint() const { return entry_; }
    [[nodiscard]] const std::vector<Segment>& segments() const { return segments_; }
    [[nodiscard]] bool isLoaded() const { return base_ != nullptr; }
    [[nodiscard]] bool isParsed() const { return parsed_; }
    [[nodiscard]] void* baseAddress() const { return base_; }

    /// trả về thông báo lỗi cuối cùng (trống nếu không có lỗi).
    [[nodiscard]] const char* lastError() const;

    /// giai đoạn 2: tra cứu địa chỉ ký hiệu từ .dynsym
    /// @return  con trỏ hàm (base_ + st_value), hoặc nullptr nếu không tìm thấy.
    void* getSymbolAddress(const char* symbolName);

    /// giai đoạn 2: kiểm tra thực thi – gọi kudroid_add(40, 20) qua ký hiệu động.
    /// @return  chuỗi kết quả với trạng thái và giá trị được tính toán.
    std::string testExecution();

    /// thực thi các hàm tạo (dt_init và dt_init_array).
    void executeInit();

    /// thực thi các hàm hủy (dt_fini và dt_fini_array).
    void executeFini();

    /// đăng ký .eh_frame cho các ngoại lệ c++
    void registerEhFrame();
    void deregisterEhFrame();

private:
    // nội bộ: đọc nội dung tệp vào bộ đệm
    bool readFile(std::vector<char>& buf);

    std::string          path_;
    void*                base_     = nullptr;
    // Gốc của vùng mmap ban đầu (trước khi base_ được điều chỉnh bởi -minVaddr),
    // để destructor có thể munmap an toàn.
    void*                allocBase_ = nullptr;
    std::size_t          allocSize_ = 0;
    std::uint64_t        entry_    = 0;
    std::vector<Segment> segments_;
    bool                 parsed_   = false;
    std::string          lastError_;
    std::vector<char>    fileBuf_;  // byte tệp thô để phân tích bảng động
    LibraryManager*      libraryManager_ = nullptr;
    
    // tls
    std::uint64_t        tls_vaddr_  = 0;
    std::uint64_t        tls_filesz_ = 0;
    std::uint64_t        tls_memsz_  = 0;
    std::uint64_t        tls_align_  = 0;

    // hàm tạo / hàm hủy
    std::uint64_t        init_func_  = 0;
    std::uint64_t        init_array_ = 0;
    std::uint64_t        init_arraysz_ = 0;
    std::uint64_t        fini_func_  = 0;
    std::uint64_t        fini_array_ = 0;
    std::uint64_t        fini_arraysz_ = 0;

    // xử lý ngoại lệ (.eh_frame_hdr / pt_gnu_eh_frame)
    std::uint64_t        eh_frame_vaddr_ = 0;
    std::uint64_t        eh_frame_memsz_ = 0;
};

} // namespace kudroid

namespace kudroid {

/// trả về tên các thư viện dt_needed từ một đối tượng chia sẻ elf64.
std::vector<std::string> parse_elf_dependencies(const char* elf_path);

/// trích xuất các mục lib/arm64-v8a/*.so từ một tệp apk vào outputdirectory.
bool extract_arm64_libs_from_apk(const char* apkPath, const char* outputDirectory,
                                 std::string* error = nullptr);

class LibraryManager {
public:
    /// tải một elf và tất cả các phụ thuộc dt_needed từ thư mục của nó.
    bool loadRecursive(const std::string& path);
    /// trả về một ký hiệu từ bất kỳ elf nào được tải, sau đó bionicshim làm dự phòng.
    void* resolveGlobalSymbol(const char* name) const;
    /// trả về một ký hiệu cụ thể từ thư viện ứng dụng chính.
    void* resolveAppSymbol(const char* name);
    [[nodiscard]] const std::unordered_map<std::string, std::unique_ptr<ElfLoader>>& libraries() const {
        return libraries_;
    }
    [[nodiscard]] const std::string& lastError() const { return lastError_; }

private:
    std::unordered_map<std::string, std::unique_ptr<ElfLoader>> libraries_;
    std::string lastError_;
};

} // namespace kudroid