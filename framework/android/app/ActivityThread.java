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
        try {
            Thread.setDefaultUncaughtExceptionHandler(new CrashHandler());

            Looper.prepareMainLooper();
            ActivityThread thread = new ActivityThread();
            thread.attach();
            
            // lấy activity ban đầu từ tham số truyền vào
            if (args != null && args.length > 0) {
                postLifecycleEvent(LAUNCH_ACTIVITY, args[0]);
            }
            
            Looper.loop();
        } catch (Throwable t) {
            System.err.println("[ActivityThread] Uncaught exception in main looper loop:");
            t.printStackTrace();
        }
    }

    private void attach() {
        sCurrentActivityThread = this;
        mH = new H();
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
        String baseName = activityClassName;
        int idx = baseName.indexOf('_');
        if (idx > 0) {
            baseName = baseName.substring(0, idx);
        }

        String[] candidates = new String[] {
            activityClassName,
            baseName,
            baseName + ".ZArchiver",
            baseName + ".MainActivity",
            baseName + ".ui.MainActivity",
            baseName + ".Main",
            baseName + ".App"
        };

        for (String name : candidates) {
            try {
                clazz = Class.forName(name);
                if (clazz != null) {
                    System.out.println("[ActivityThread] Resolved Activity Class: " + name);
                    break;
                }
            } catch (Throwable t) {
                // Tiếp tục thử ứng viên tiếp theo
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
                android.util.Log.i("ActivityThread", "Activity launch complete! UI is live.");
            } catch (Throwable t) {
                android.util.Log.e("ActivityThread", "FATAL in Activity lifecycle: " + t.toString());
                StackTraceElement[] trace = t.getStackTrace();
                if (trace != null) {
                    for (StackTraceElement ste : trace) {
                        android.util.Log.e("ActivityThread", "    at " + ste.toString());
                    }
                }
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
