package android.app;

import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.os.Bundle;

/**
 * quản lý vòng đời ứng dụng và luồng ui chính theo chuẩn android.
 * sử dụng looper và handler để điều phối an toàn.
 */
public final class ActivityThread {
    public static final int LAUNCH_ACTIVITY = 100;
    public static final int PAUSE_ACTIVITY = 101;
    public static final int RESUME_ACTIVITY = 102;
    public static final int DESTROY_ACTIVITY = 103;

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
            }
        }
    }

    public static void main(String[] args) {
        Looper.prepareMainLooper();
        ActivityThread thread = new ActivityThread();
        thread.attach();
        
        // lấy activity ban đầu từ tham số truyền vào
        if (args != null && args.length > 0) {
            postLifecycleEvent(LAUNCH_ACTIVITY, args[0]);
        }
        
        Looper.loop();
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
                mInitialActivity = (Activity) clazz.newInstance();
                mInitialActivity.onCreate(null);
                mInitialActivity.onStart();
                mInitialActivity.onResume();
            } catch (Throwable t) {
                t.printStackTrace();
            }
        } else {
            System.err.println("[ActivityThread] Could not resolve any valid Activity class for: " + activityClassName);
        }
    }

    private void handlePauseActivity() {
        if (mInitialActivity != null) {
            mInitialActivity.onPause();
        }
    }

    private void handleResumeActivity() {
        if (mInitialActivity != null) {
            mInitialActivity.onResume();
        }
    }

    private void handleDestroyActivity() {
        if (mInitialActivity != null) {
            mInitialActivity.onDestroy();
        }
    }
}
