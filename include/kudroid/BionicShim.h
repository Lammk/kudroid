#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// khởi tạo tls luồng chính để tương thích với android
void bionic_init_main_thread_tls(void);

// chặn trực tiếp dlopen của android — các thư viện gpu (libvulkan.so, libglesv2.so,
// libegl.so) trả về một tay cầm giả và phân giải các ký hiệu thẳng thành gốc ios.
void* bionic_dlopen(const char* filename, int flags);
void* bionic_dlsym(void* handle, const char* symbol);

// Đăng ký template PT_TLS của module guest (từ elf_loader sau khi map) để
// các khối TLS per-thread có thể sao chép nó vào đúng vị trí.
void kudroid_tls_set_template(const void* tls_template, size_t tls_filesz);

// Khoảng cách (so với thread pointer của guest) nơi template TLS module được đặt
// trong khối TLS per-thread. Cùng giá trị này được elf_loader dùng làm bias
// cho relocations R_AARCH64_TLS_TPREL64 / TLS_DTPREL64.
size_t kudroid_tls_module_offset(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

namespace kudroid {

/// phân giải một ký hiệu android/bionic thành một triển khai tương thích ios/posix.
/// các ký hiệu không xác định trả về một hàm giả khác null và phát ra cảnh báo.
void* resolve_bionic_symbol(const char* name);

/// xóa và truy xuất các thông báo chẩn đoán được tạo ra bởi lớp đệm.
void bionic_shim_reset_trace();
const char* bionic_shim_trace();

/// xử lý sigtrap do các lệnh mrs x, tpidr_el0 được vá aot gây ra.
/// trả về true nếu xử lý thành công.
bool bionic_handle_tpidr_trap(void* ucontext);

} // namespace kudroid

#endif

