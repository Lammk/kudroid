# khu n kh  kudroid android

m t khu n kh  android t i thi u (java) cung c p c c l p `android.*` m  c c  ng d ng
c n   t i java khi kh i  ng, tr c khi chuy n sang m  `.so` g c.

## m c  ch

h u h t c c tr  ch i g c (unity il2cpp, godot, sdl) ch  ch m v o java m t th i gian ng n l c
kh i  ng (`jni_onload`, `anativeactivity_oncreate`), sau   ch y ho n to n th ng qua
c c th  vi n `.so` c/c++. khu n kh  n y cung c p v a   c c l p `android.*`
  c c  ng d ng   kh ng g p s  c  khi t i java.

## nh ng g   c bao g m

**tri n khai th c t ** ( nh h ng  n h nh vi c a  ng d ng):
- `android.util.log` → maps to `__android_log_print`
- `android.os.handler` / `looper` / `messagequeue` / `message` / `bundle`
- `android.app.activity` / `application` / `dialog` / `alertdialog`
- `android.content.context` / `contextwrapper` / `intent` / `sharedpreferences`
- `android.view.view` / `viewgroup` / `motionevent` / `window`
- `android.widget.textview` / `button` / `linearlayout` / `toast`
- `android.graphics.*` (canvas, paint, bitmap, color, rect, ...)

**m  ph ng** (tr  v  c c gi  tr  m c  nh   c c  ng d ng kh ng b  s  c ):
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

## x y d ng

```bash
# y u c u jdk (javac + jar)
./build.sh                 # t o ra framework/build/framework.jar
./build.sh --bootimage     # c ng t o ra framework/build/boot.jar cho avian
```

## th m l p

1. t o t p `.java` d i `framework/android/<package>/`.
2. ch y `./build.sh`   bi n d ch l i.
3. t p jar  c nh ng v o t p nh  ph n kudroid d i d ng classpath kh i  ng avian.

##  ng g p

khu n kh  n y c  t nh t i thi u. n u m t  ng d ng c n m t l p
b  thi u, h y th m n  (ho c m  m t v n  ). m c ti u l  ph t tri n n  d a tr n nhu c u th c t  c a  ng d ng,
kh ng ph i   sao ch p to n b  android sdk.
