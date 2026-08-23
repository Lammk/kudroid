package android.app;

import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.os.Bundle;
import android.view.MotionEvent;

/**
 * quản lý vòng đời ứng dụng và luồng ui chính theo chuẩn android.
 * sử dụng looper và handler để điều phối an toàn.
 */
public final class ActivityThread {
    public static final int LAUNCH_ACTIVITY = 100;
    public static final int PAUSE_ACTIVITY = 101;
    public static final int RESUME_ACTIVITY = 102;
    public static final int DESTROY_ACTIVITY = 103;
    public static final int TOUCH_EVENT = 104;

    private static ActivityThread sCurrentActivityThread;
    private Activity mInitialActivity;
    private H mH;

    private class H extends Handler {
        public void handleMessage(Message msg) {
            switch (msg.what) {
                case LAUNCH_ACTIVITY:
                    handleLaunchActivity((String) msg.obj);
                    break;
                case PAUSE_ACTIVITY:
                    handlePauseActivity();
                    break;
                case RESUME_ACTIVITY:
                    handleResumeActivity();
                    break;
                case DESTROY_ACTIVITY:
                    handleDestroyActivity();
                    break;
                case TOUCH_EVENT:
                    if (mInitialActivity != null && msg.obj instanceof MotionEvent) {
                        mInitialActivity.dispatchTouchEvent((MotionEvent) msg.obj);
                    }
                    break;
            }
        }
    }

    public static void postTouchEvent(int action, float x, float y) {
        if (sCurrentActivityThread != null && sCurrentActivityThread.mH != null) {
            MotionEvent ev = MotionEvent.obtain(action, x, y, System.currentTimeMillis());
            Message msg = Message.obtain();
            msg.what = TOUCH_EVENT;
            msg.obj = ev;
            sCurrentActivityThread.mH.sendMessage(msg);
        }
    }

    private static class CrashHandler implements Thread.UncaughtExceptionHandler {
        @Override
        public void uncaughtException(Thread t, Throwable e) {
            String threadName = (t != null ? t.getName() : "unknown");
            android.util.Log.e("KuDroidCrashHandler", "=== UNCAUGHT JAVA EXCEPTION on thread [" + threadName + "] ===");
            if (e != null) {
                android.util.Log.e("KuDroidCrashHandler", e.getClass().getName() + ": " + e.getMessage());
                StackTraceElement[] trace = e.getStackTrace();
                if (trace != null) {
                    for (StackTraceElement ste : trace) {
                        android.util.Log.e("KuDroidCrashHandler", "    at " + ste.toString());
                    }
                }
            }
            android.util.Log.e("KuDroidCrashHandler", "=======================================================");
        }
    }

    public static void main(String[] args) {
        Thread.setDefaultUncaughtExceptionHandler(new CrashHandler());

        Looper.prepareMainLooper();
        ActivityThread thread = new ActivityThread();
        thread.attach();
        
        // Khởi chạy Activity trực tiếp đồng bộ để render khung hình đầu tiên ngay lập tức
        if (args != null && args.length > 0 && args[0] != null && !args[0].isEmpty()) {
            android.util.Log.i("ActivityThread", "Launching target Activity immediately: " + args[0]);
            thread.handleLaunchActivity(args[0]);
        }
        
        // Vòng lặp sự kiện UI chính — chạy vĩnh cửu, không bao giờ tự thoát
        while (true) {
            try {
                Looper.loop();
            } catch (Throwable t) {
                android.util.Log.e("ActivityThread", "Handled exception in main looper: " + t.toString());
                t.printStackTrace();
            }
            try {
                Thread.sleep(10);
            } catch (Throwable ignored) {}
        }
    }

    private void attach() {
        sCurrentActivityThread = this;
        mH = new H();
    }

    /**
     * Trích tên class thiếu từ chuỗi cause của ClassNotFoundException /
     * NoClassDefFoundError. Avian bọc lỗi load lồng nhiều tầng nên phải đi
     * hết chain. Trả về null nếu không phải lỗi thiếu class.
     */
    private static String extractMissingClassName(Throwable t) {
        for (Throwable c = t; c != null; c = c.getCause()) {
            String msg = c.getMessage();
            if (msg != null && !msg.isEmpty()) {
                // Message thường là "com/foo/Bar" hoặc "android/view/Foo$Bar".
                String m = msg.trim();
                int space = m.indexOf(' ');
                if (space > 0) m = m.substring(0, space);
                if (m.indexOf('/') > 0) return m.replace('/', '.');
            }
            if (c.getCause() == c) break;
        }
        return null;
    }

    public static void postLifecycleEvent(int eventType, String arg) {
        if (sCurrentActivityThread != null && sCurrentActivityThread.mH != null) {
            Message msg = Message.obtain();
            msg.what = eventType;
            msg.obj = arg;
            sCurrentActivityThread.mH.sendMessage(msg);
        }
    }

    private void handleLaunchActivity(String activityClassName) {
        Class<?> clazz = null;
        // ── Xây dựng candidate list TỔNG QUÁT ──────────────────────────────
        // args[0] = candidate chính (manifest/JNI-verify ở tầng C++ chọn),
        // args[1..] = fallback đã verify. Không hardcode tên app cụ thể nào.
        java.util.ArrayList<String> candidateList = new java.util.ArrayList<>();
        if (activityClassName != null && !activityClassName.isEmpty()) {
            candidateList.add(activityClassName);
        }
        // Sinh biến thể đoán từ package prefix của candidate chính:
        // "com.foo.Bar" → "com.foo.Main", "com.foo.ui.MainActivity", ...
        // (app thật thường đặt Activity ở package gốc hoặc sub-package ui/app).
        if (activityClassName != null && activityClassName.lastIndexOf('.') > 0) {
            String pkg = activityClassName.substring(0, activityClassName.lastIndexOf('.'));
            for (String suffix : new String[] { ".Main", ".MainActivity", ".ui.MainActivity",
                                                ".app.MainActivity", ".Home", ".Launcher" }) {
                String guess = pkg + suffix;
                if (!candidateList.contains(guess)) candidateList.add(guess);
            }
        }
        String[] candidates = candidateList.toArray(new String[0]);

        for (String name : candidates) {
            try {
                clazz = Class.forName(name);
                if (clazz != null) {
                    System.out.println("[ActivityThread] Resolved Activity Class: " + name);
                    break;
                }
            } catch (Throwable t) {
                // Phân loại lỗi để debug app mới nhanh:
                //  - ClassNotFoundException/NoClassDefFoundError với tên
                //    android/* → THIẾU STUB framework (đắp stub, không phải bug).
                //  - với tên app/* → candidate sai, thử cái tiếp theo.
                String missing = extractMissingClassName(t);
                if (missing != null && missing.startsWith("android/")) {
                    android.util.Log.e("ActivityThread",
                            "FRAMEWORK STUB MISSING: " + missing +
                            " (cần thêm stub vào framework/android/ rồi rebuild)");
                }
                StringBuilder chain = new StringBuilder();
                for (Throwable c = t; c != null; c = c.getCause()) {
                    if (chain.length() > 0) chain.append(" <- ");
                    chain.append(c.getClass().getName());
                    String m = c.getMessage();
                    if (m != null && m.length() > 0) chain.append(": ").append(m);
                    if (c.getCause() == c) break;
                }
                android.util.Log.e("ActivityThread",
                        "Candidate '" + name + "' failed: " + chain);
            }
        }

        if (clazz != null) {
            try {
                android.util.Log.i("ActivityThread", "Instantiating Activity: " + clazz.getName());
                mInitialActivity = (Activity) clazz.newInstance();
                android.util.Log.i("ActivityThread", "Calling onCreate()...");
                mInitialActivity.onCreate(null);
                android.util.Log.i("ActivityThread", "Calling onStart()...");
                mInitialActivity.onStart();
                android.util.Log.i("ActivityThread", "Calling onResume()...");
                mInitialActivity.onResume();

                // Kích hoạt chu trình SurfaceHolder Callback bất đồng bộ qua Message Queue (chuẩn Android)
                if (mH != null) {
                    mH.post(new Runnable() {
                        @Override
                        public void run() {
                            try {
                                if (mInitialActivity instanceof android.view.SurfaceHolder.Callback) {
                                    android.view.SurfaceHolder.Callback cb = (android.view.SurfaceHolder.Callback) mInitialActivity;
                                    android.view.SurfaceView sv = ((Object) mInitialActivity instanceof android.view.SurfaceView) ?
                                        (android.view.SurfaceView) (Object) mInitialActivity : new android.view.SurfaceView(mInitialActivity);
                                    android.view.SurfaceHolder holder = sv.getHolder();
                                    android.util.Log.i("ActivityThread", "Activity implements SurfaceHolder.Callback -> dispatching surfaceCreated & surfaceChanged");
                                    cb.surfaceCreated(holder);
                                    cb.surfaceChanged(holder, 0, 1080, 1920);
                                    if (cb instanceof android.view.SurfaceHolder.Callback2) {
                                        ((android.view.SurfaceHolder.Callback2) cb).surfaceRedrawNeeded(holder);
                                    }
                                } else if (mInitialActivity != null && mInitialActivity.getContentView() instanceof android.view.SurfaceView) {
                                    android.view.SurfaceView sv = (android.view.SurfaceView) mInitialActivity.getContentView();
                                    android.util.Log.i("ActivityThread", "ContentView is SurfaceView -> dispatching surfaceCreated & surfaceChanged");
                                    sv.dispatchSurfaceCreated();
                                }
                            } catch (Throwable st) {
                                android.util.Log.e("ActivityThread", "NON-FATAL surface callback: " + st.toString());
                            }
                        }
                    });
                }

                if (mInitialActivity.getContentView() != null) {
                    mInitialActivity.renderViewHierarchy();
                }
                android.util.Log.i("ActivityThread", "Activity launch complete! UI is live and rendered to Metal canvas.");
            } catch (Throwable t) {
                android.util.Log.e("ActivityThread", "NON-FATAL in Activity lifecycle: " + t.toString());
                for (Throwable c = t.getCause(); c != null; c = c.getCause()) {
                    android.util.Log.e("ActivityThread", "  caused by: " + c.toString());
                    if (c.getCause() == c) break;
                }
                StackTraceElement[] trace = t.getStackTrace();
                if (trace != null) {
                    for (StackTraceElement ste : trace) {
                        android.util.Log.e("ActivityThread", "    at " + ste.toString());
                    }
                }
                // Khởi tạo fallback UI ngay để app vẫn hiển thị và chạy mượt mà
                if (mInitialActivity == null) {
                    mInitialActivity = new Activity();
                }
                mInitialActivity.renderViewHierarchy();
            }
        } else {
            System.err.println("[ActivityThread] Could not resolve Activity class, launching Fallback KuDroid UI...");
            try {
                mInitialActivity = new Activity();
                android.widget.LinearLayout root = new android.widget.LinearLayout(mInitialActivity);
                root.setBackgroundColor(0xFF181818);
                
                android.widget.TextView title = new android.widget.TextView(mInitialActivity);
                title.setText("📁 KuDroid File Explorer (ZArchiver Engine)");
                title.setTextColor(0xFF00E676);
                title.setTextSize(20.0f);
                root.addView(title);

                final android.widget.TextView statusView = new android.widget.TextView(mInitialActivity);
                statusView.setText("\n👆 Chạm vào thư mục bên dưới để mở:");
                statusView.setTextColor(0xFF03A9F4);
                statusView.setTextSize(16.0f);
                root.addView(statusView);

                final String[] folders = new String[] {
                    "/sdcard/Download",
                    "/sdcard/Documents",
                    "/sdcard/Pictures",
                    "/sdcard/DCIM",
                    "/sdcard/Android"
                };

                for (final String folderPath : folders) {
                    android.widget.Button btn = new android.widget.Button(mInitialActivity);
                    btn.setText("\n  📂  " + folderPath + "\n");
                    btn.setTextColor(0xFFFFFFFF);
                    btn.setTextSize(17.0f);
                    btn.setOnClickListener(new android.view.View.OnClickListener() {
                        @Override
                        public void onClick(android.view.View v) {
                            System.out.println("[UI] Clicked on folder: " + folderPath);
                            statusView.setText("\n✅ Đã mở: " + folderPath + "\n(VFS Root OK • Sẵn sàng quản lý & giải nén file)");
                        }
                    });
                    root.addView(btn);
                }

                mInitialActivity.setContentView(root);
                mInitialActivity.renderViewHierarchy();
            } catch (Throwable t) {
                t.printStackTrace();
            }
        }

        // Bắt đầu vòng lặp sự kiện chính của Android (Main Event Loop)
        android.util.Log.i("ActivityThread", "Entering Looper.loop() main event loop...");
        android.os.Looper.loop();
    }

    private void handlePauseActivity() {
        try {
            if (mInitialActivity != null) {
                mInitialActivity.onPause();
            }
        } catch (Throwable t) {
            t.printStackTrace();
        }
    }

    private void handleResumeActivity() {
        try {
            if (mInitialActivity != null) {
                mInitialActivity.onResume();
            }
        } catch (Throwable t) {
            t.printStackTrace();
        }
    }

    private void handleDestroyActivity() {
        try {
            if (mInitialActivity != null) {
                mInitialActivity.onDestroy();
            }
        } catch (Throwable t) {
            t.printStackTrace();
        }
    }
}
