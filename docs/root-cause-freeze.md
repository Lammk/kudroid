# Phân tích freeze của app

Ngày phân tích: 2026-10-10  
Build ghi trong log: `37a01e3`  
Mốc đối chiếu: `bcbea8a` (`local-backup-pre-aosp-rewrite`)

## Kết luận

Capture hiện có cho thấy app không đi vào vòng render của game sau khi Activity khởi chạy. Đây giống **render/game loop không được khởi động** hơn là process chết, hết RAM, hay main thread bị kẹt trong một phép tính dài. Bằng chứng trực tiếp nhất là KuART báo Activity launch hoàn tất, sau đó main thread nằm chờ trong `MessageQueue.next()`; log không có frame telemetry, `nativeRender`, hoặc frame census.

Nguyên nhân có khả năng cao nhất trong capture này là **framework không implement overload `Activity.setContentView(View, ViewGroup.LayoutParams)` mà app gọi**. Runtime ghi rõ đã auto-stub method này. Stub không gắn view vào Activity/Window; vì vậy cây view chứa surface có thể không được gắn, và `ActivityThread` không tìm thấy `SurfaceView` để phát callback khởi tạo renderer. Đây là đường giải thích khớp nhất giữa method bị thiếu và việc không có frame nào xuất hiện. Tuy nhiên log hiện tại không ghi lại callback `surfaceCreated`/`surfaceChanged`, nên phần “surface không được gắn nên callback không tới” là suy luận có độ tin cậy cao, chưa phải bằng chứng trực tiếp.

Hai exception sau launch cũng cần xử lý riêng: `ClassCastException: java.net.URL$1 not javax.net.ssl.HttpsURLConnection` và `NullPointerException: array-length on null`. Chúng có thể làm hỏng công việc khởi tạo hoặc tải dữ liệu của app, nhưng log không có stack trace đủ để kết luận exception nào chặn đường render. Không nên gộp chúng với lỗi `setContentView` thành một nguyên nhân đã được chứng minh.

## Timeline từ log

- `21:24:14.902`: KuART gọi `ActivityThread.main` cho `com.min.n.MainActivity`.
- Khoảng `21:24:15`: `performCreate`, `performStart`, `performResume` chạy xong; log ghi `Activity launch complete.`
- Cùng lúc startup ghi nhận method `Activity.setContentView(View, LayoutParams)` bị auto-stub, rồi hai exception nói trên.
- Trong gần 35 giây kế tiếp, watchdog thấy `ActivityThread.main` còn sống. Breadcrumb cuối ghi `MessageQueue.next()` thoát sau khoảng `35,393 ms`; đây là queue chờ, không phải bằng chứng main thread bận CPU.
- `logs/kudroid_android_logs.txt` không có dòng `frames: looper=...` hoặc frame census. `scripts/analyze_logs.sh` vì vậy tính delta khung hình là 0; đây là thiếu telemetry/frame, không phải phép đo chứng minh có frame bị drop.
- `logs/stderr.log` sau đó ghi `teardown quit looper of main`, `Main Looper exited`, và reset signal khi shell dừng app. Trình tự này phù hợp với teardown từ shell/người dùng; không có dấu hiệu trong capture rằng watchdog tự kết thúc app do crash.
- Mức dùng bộ nhớ trong watchdog xấp xỉ 296 MB footprint và `low_memory=0`; không ủng hộ giả thuyết hết bộ nhớ.

## Đối chiếu `bcbea8a`

`bcbea8a` là tổ tiên của HEAD và được gắn tag `local-backup-pre-aosp-rewrite`. Từ mốc đó tới HEAD có thay đổi lớn ở framework/runtime: 74 file thay đổi, khoảng 8,981 dòng thêm và 714 dòng xóa. Vì vậy đây là đối chiếu giữa hai trạng thái rộng, không phải một commit đơn lẻ đủ để quy lỗi.

Đáng chú ý, `Activity.setContentView(View, LayoutParams)` **đã thiếu ngay tại `bcbea8a`**; Activity ở mốc đó chỉ có các overload `setContentView(int)` và `setContentView(View)`. Do đó method bị thiếu là lỗi tương thích API có thật trong capture, nhưng không thể khẳng định nó được giới thiệu bởi các commit sau mốc stable. Nếu app chạy được tại `bcbea8a`, cần kiểm tra APK/build lúc đó có gọi overload này hay không; khác biệt APK hoặc nhánh khởi tạo có thể làm triệu chứng chỉ xuất hiện ở build hiện tại.

Các commit gần HEAD như `2c3f54b`, `f32396b`, và `37a01e3` tập trung sửa thời điểm/độ trễ phát surface callback, đồng bộ layout và focus. Những sửa đổi này cho thấy surface lifecycle là vùng rủi ro, nhưng log capture không chứng minh callback starvation còn là lỗi chính sau các sửa đổi đó. Cần xác minh DEX thực sự chạy và callback nào đã được phát trước khi quy regression cho các commit này.

## Mức độ chắc chắn

| Kết luận | Mức độ | Bằng chứng |
|---|---|---|
| Không có bằng chứng process crash hoặc hết RAM | Cao | `low_memory=0`; không có FATAL/tombstone trong log được cung cấp; teardown có chủ đích |
| Main queue chờ, không bị kẹt trong native render dài | Cao | breadcrumb kết thúc ở `MessageQueue.next()` sau 35 giây; `native_active=0` |
| Game/render loop không khởi động trong capture | Cao | không có frame telemetry/census hoặc `nativeRender` |
| Thiếu overload `setContentView(View, LayoutParams)` có thể khiến view/surface không được gắn | Cao về lỗi API; vừa-cao về quan hệ nhân quả | runtime báo auto-stub; source không implement overload; frame và callback không có bằng chứng |
| URL cast hoặc null array là nguyên nhân trực tiếp làm freeze | Thấp-vừa | exception có thật nhưng stack trace/quan hệ với renderer còn thiếu |
| Regression được đưa vào sau `bcbea8a` | Chưa xác định | overload đã thiếu ở baseline; log không cung cấp đối chiếu cùng APK trên hai build |

## Bằng chứng cần lấy để chốt regression

1. Chạy cùng một APK trên build `bcbea8a` và build hiện tại; giữ lại `stderr.log`, Android log, breadcrumbs và log surface/render.
2. Ghi log ngay lúc `Activity.setContentView` được gọi: descriptor overload, class của view, decor child count, kích thước view, và `SurfaceView` tìm được.
3. Ghi từng lần gọi `surfaceCreated`, `surfaceChanged`, window surface-ready, lần đầu `nativeRender`, và swap count; nếu callback có chạy mà không có swap thì chuyển điều tra sang EGL/Vulkan/renderer.
4. Lấy full stack trace cho hai exception sau launch. Riêng lỗi URL cần xác định caller nào cast kết quả `openConnection()` sang `HttpsURLConnection` và URL handler đang trả kiểu gì.

## Tệp đã xem

- `logs/stderr.log`
- `logs/native_breadcrumbs.log`
- `logs/kudroid_android_logs.txt`
- `scripts/analyze_logs.sh`
- `framework/android/app/Activity.java`
- `framework/android/app/ActivityThread.java`
- `framework/android/view/SurfaceView.java`
- `framework/android/os/Looper.java`
- `src/bridge/AppLifecycle.cpp`

Chỉ tạo báo cáo này; không sửa mã nguồn hoặc chạy test.
