# khuôn khổ kudroid android

một khuôn khổ android tối thiểu (java) cung cấp các lớp `android.*` mà các ứng dụng
cần để tải java khi khởi động, trước khi chuyển sang mã `.so` gốc.

## mục đích

hầu hết các trò chơi gốc (unity il2cpp, godot, sdl) chỉ chạm vào java một thời gian ngắn lúc
khởi động (`jni_onload`, `anativeactivity_oncreate`), sau đó chạy hoàn toàn thông qua
các thư viện `.so` c/c++. khuôn khổ này cung cấp vừa đủ các lớp `android.*`
để các ứng dụng đó không gặp sự cố khi tải java.

## những gì được bao gồm

**triển khai thực tế** (ảnh hưởng đến hành vi của ứng dụng):
- `android.util.log` → maps to `__android_log_print`
- `android.os.handler` / `looper` / `messagequeue` / `message` / `bundle`
- `android.app.activity` / `application` / `dialog` / `alertdialog`
- `android.content.context` / `contextwrapper` / `intent` / `sharedpreferences`
- `android.view.view` / `viewgroup` / `motionevent` / `window`
- `android.widget.textview` / `button` / `linearlayout` / `toast`
- `android.graphics.*` (canvas, paint, bitmap, color, rect, ...)

**mô phỏng** (trả về các giá trị mặc định để các ứng dụng không bị sự cố):
- `android.telephony.telephonymanager`
- `android.bluetooth.bluetoothadapter`
- `android.app.notificationmanager` / `notification`
- `android.location.locationmanager`
- `android.net.wifi.wifimanager`
- `android.hardware.sensormanager`
- `android.media.audiomanager`
- `android.os.vibrator` / `powermanager`
- `android.net.connectivitymanager`
- `android.provider.settings`

## xây dựng

```bash
# yêu cầu jdk (javac + jar)
./build.sh                 # tạo ra framework/build/framework.jar
./build.sh --bootimage     # cũng tạo ra framework/build/boot.jar cho avian
```

## thêm lớp

1. tạo tệp `.java` dưới `framework/android/<package>/`.
2. chạy `./build.sh` để biên dịch lại.
3. tệp jar được nhúng vào tệp nhị phân kudroid dưới dạng classpath khởi động avian.

## đóng góp

khuôn khổ này cố tình tối thiểu. nếu một ứng dụng cần một lớp
bị thiếu, hãy thêm nó (hoặc mở một vấn đề). mục tiêu là phát triển nó dựa trên nhu cầu thực tế của ứng dụng,
không phải để sao chép toàn bộ android sdk.
