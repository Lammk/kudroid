#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// điểm vào tự kiểm tra cho thư viện kudroid_core.
/// trả về 0 nếu thành công, khác 0 nếu thất bại.
int kudroid_self_test(void);

/// tự kiểm tra với đầu ra nhật ký chi tiết.
/// trả về một chuỗi được malloc chứa nhật ký gỡ lỗi từng bước.
/// người gọi phải giải phóng chuỗi được trả về bằng free().
const char* kudroid_self_test_log(void);

/// tải một tệp đối tượng chia sẻ elf (.so).
/// @param path  đường dẫn tuyệt đối đến tệp .so.
/// @return      chuỗi nhật ký được malloc chứa kết quả phân tích.
///              người gọi phải giải phóng chuỗi được trả về bằng free().
const char* kudroid_load_elf(const char* path);

/// thực thi một hàm gốc từ tệp .so được tải (kiểm tra giai đoạn 2).
/// phải được gọi sau kudroid_load_elf.
/// @return  chuỗi nhật ký được malloc chứa kết quả thực thi.
///          người gọi phải giải phóng chuỗi được trả về bằng free().
const char* kudroid_execution_test(const char* path);

/// thực thi một hàm gốc từ tệp .so được tải (kiểm tra giai đoạn 2).
/// phải được gọi sau kudroid_load_elf.
/// @return  chuỗi nhật ký được malloc chứa kết quả thực thi.
///          người gọi phải giải phóng chuỗi được trả về bằng free().
const char* kudroid_syscall_so_test(const char* path);
const char* kudroid_jni_massive_so_test(const char* path);

/// tải thư viện kiểm tra bionic shim và thực thi kudroid_bionic_test().
/// trả về một nhật ký chẩn đoán được malloc; người gọi phải giải phóng nó bằng free().
const char* kudroid_bionic_execution_test(const char* path);

/// tải các thư viện elf được đóng gói thông qua librarymanager và kiểm tra phân giải toàn cục.
/// trả về một nhật ký chẩn đoán được malloc; người gọi phải giải phóng nó bằng free().
const char* kudroid_multi_elf_test(const char* consumerPath, const char* providerPath);

/// báo cáo xem jit (bộ nhớ có thể thực thi) có sẵn cho quá trình này hay không.
/// @return  một chuỗi được malloc "jit: enabled" hoặc "jit: disabled".
///          người gọi phải giải phóng chuỗi được trả về bằng free().
const char* kudroid_jit_status(void);

/// đặt thư mục nơi kudroid_core ghi các tệp nhật ký .txt và bãi chứa sự cố.
/// gọi một lần lúc khởi động với thư mục documents có thể ghi của ứng dụng.
/// cũng cài đặt các trình xử lý tín hiệu để một sự cố gốc vẫn để lại tệp nhật ký.
void kudroid_set_log_dir(const char* dir);

/// đặt thư mục documents được vfspathremapper sử dụng.
void kudroid_set_documents_dir(const char* dir);

/// đặt con trỏ cametallayer hoặc uiview được sử dụng cho các ràng buộc bề mặt anativewindow.
/// width/height là kích thước pixel thật (UIScreen.bounds * scale); density = scale
/// (3.0 cho @3x) — được đẩy tiếp vào DisplayMetrics của Java lúc JVM khởi tạo.
void kudroid_set_metal_layer(void* layer, int width, int height, float density);

/// chèn một sự kiện chạm vào ứng dụng android gốc
/// @param x tọa độ x của lần chạm
/// @param y tọa độ y của lần chạm
/// @param action 0=down, 1=up, 2=move (được ánh xạ tới amotion_event_action của android)
void kudroid_inject_touch_event(float x, float y, int action);
void kudroid_inject_touch_event_multi(float x, float y, int action, int pointerId, int pointerCount);

/// gửi sự kiện vòng đời ứng dụng java (101=pause, 102=resume) vào luồng ui
void kudroid_send_lifecycle_event(int eventType);

/// dịch một tệp dex thành một tệp jar của các lớp giả (có bộ đệm).
/// trả về một nhật ký chẩn đoán được malloc; người gọi phải giải phóng nó bằng free().
const char* kudroid_translate_dex(const char* dexPath);

/// chạy kiểm tra tự động chuyển hướng vfs và i/o; trả về một nhật ký được malloc.
const char* kudroid_vfs_self_test_log(void);
const char* kudroid_vfs_extended_test_log(void);

/// kiểm tra tích hợp jvm.
/// trả về một nhật ký chẩn đoán được malloc; người gọi phải giải phóng nó bằng free().
char* kudroid_test_jvm(const char* rt_jar_path);
char* kudroid_test_gpu(void);
char* kudroid_test_audio(void);
const char* kudroid_run_so_test(const char* soPath, const char* entrypoint);

/// tải tệp .so kiểm tra gpu arm64 và thực thi kiểm tra vulkan của nó thông qua tính năng chặn bionicshim.
/// trả về một nhật ký chẩn đoán được malloc; người gọi phải giải phóng nó bằng free().
const char* kudroid_gpu_vulkan_so_test(const char* path);

/// tải tệp .so kiểm tra gpu arm64 và thực thi kiểm tra opengl+egl của nó thông qua tính năng chặn bionicshim.
/// trả về một nhật ký chẩn đoán được malloc; người gọi phải giải phóng nó bằng free().
const char* kudroid_gpu_opengl_so_test(const char* path);

/// trích xuất và cài đặt các thư viện gốc arm64-v8a của apk vào android_root.
/// trả về một nhật ký chẩn đoán được malloc; người gọi phải giải phóng nó bằng free().
const char* kudroid_install_apk(const char* apkPath);

/// quét thư mục thư viện của apk đã cài đặt và tải tất cả các thư viện gốc (.so) của nó.
/// trả về một nhật ký chẩn đoán được malloc; người gọi phải giải phóng nó bằng free().
const char* kudroid_run_apk(const char* appName);

/// xóa các thư mục bộ đệm nội bộ của một ứng dụng.
/// trả về 1 nếu thành công, 0 nếu thất bại.
int kudroid_clear_app_cache(const char* package_name);

/// xóa hoàn toàn một ứng dụng đã cài đặt và dữ liệu của nó.
/// trả về 1 nếu thành công, 0 nếu thất bại.
int kudroid_delete_app(const char* package_name);

/// lấy thông tin cơ bản về một ứng dụng đã cài đặt.
/// trả về một chuỗi được malloc (ví dụ: json hoặc văn bản được định dạng); người gọi phải giải phóng nó bằng free().
const char* kudroid_get_app_info(const char* package_name);

/// kiểm tra xem ứng dụng khách có bị sập (gentle crash) trong phiên chạy vừa qua hay không.
/// trả về 1 nếu có crash, 0 nếu bình thường.
int kudroid_has_crashed(void);

/// xóa trạng thái crash sau khi giao diện đã hiển thị thông báo.
void kudroid_clear_crash_state(void);

/// trích xuất tối đa 30 dòng log cuối cùng trước khi crash.
/// trả về chuỗi malloc; người gọi phải giải phóng bằng free().
const char* kudroid_get_last_crash_tail(void);

// Android Runtime Permission Manager APIs
int kudroid_check_permission(const char* packageName, const char* permissionName);
void kudroid_set_group_permission(const char* packageName, const char* groupKey, int granted);
int kudroid_is_group_granted(const char* packageName, const char* groupKey);
void kudroid_grant_all_permissions(const char* packageName);
const char* kudroid_get_app_permissions_json(const char* packageName);
void kudroid_set_app_permissions_json(const char* packageName, const char* jsonStr);

#ifdef __cplusplus
}
#endif