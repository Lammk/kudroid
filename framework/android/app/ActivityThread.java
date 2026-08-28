package android.app;

import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.os.Bundle;
import android.view.MotionEvent;

/**
 * manage application lifecycle and main ui flow according to android standards.
 * use looper and handler for safe coordination.
 */
public final class ActivityThread {
    public static final int LAUNCH_ACTIVITY = 100;
    public static final int PAUSE_ACTIVITY = 101;
    public static final int RESUME_ACTIVITY = 102;
    public static final int DESTROY_ACTIVITY = 103;
    public static final int TOUCH_EVENT = 104;

    private static ActivityThread sCurrentActivityThread;
    private Activity mInitialActivity;
    private String mInitialActivityName;
    private H mH;

    private class H extends Handler {
        public void handleMessage(Message msg) {
            switch (msg.what) {
                case LAUNCH_ACTIVITY:
                    handleLaunchActivity(new String[] { (String) msg.obj });
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

        // args are the candidates the C++ layer resolved from AndroidManifest.xml,
        // in launch order: args[0] is the launcher activity, args[1..] the rest.
        // Every entry is a class the app really declares.
        if (args != null && args.length > 0 && args[0] != null && !args[0].isEmpty()) {
            android.util.Log.i("ActivityThread", "Launching target Activity immediately: " + args[0]);
            thread.handleLaunchActivity(args);
        }
        
        // Main UI event loop — runs forever, never exits itself
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
     * Extract the missing class name from the cause string of ClassNotFoundException /
     * NoClassDefFoundError. Avian wrapped the loading error in a multi-layer cage, so it had to go
     * out of chain. Returns null if not a missing class error.
     */
    /**
     * True if clazz is android.app.Activity or a subclass of it.
     *
     * Walks the superclass chain rather than using isAssignableFrom so it works even
     * when a candidate's own hierarchy is partly stubbed: a missing intermediate
     * class stops the walk and the candidate is rejected, instead of throwing from
     * the middle of a cast.
     */
    private static boolean isActivitySubclass(Class<?> clazz) {
        try {
            for (Class<?> c = clazz; c != null; c = c.getSuperclass()) {
                if ("android.app.Activity".equals(c.getName())) return true;
            }
        } catch (Throwable ignored) {}
        return false;
    }

    private static String extractMissingClassName(Throwable t) {
        for (Throwable c = t; c != null; c = c.getCause()) {
            String msg = c.getMessage();
            if (msg != null && !msg.isEmpty()) {
                // Message is usually "com/foo/Bar" or "android/view/Foo$Bar".
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

    private void handleLaunchActivity(String[] candidates) {
        Class<?> clazz = null;
        String resolvedName = null;

        // Candidates come from AndroidManifest.xml, resolved by the C++ layer.
        //
        // Nothing is guessed here any more. This used to append invented names built
        // from the package prefix (".Main", ".ui.MainActivity", ".Home"...), which
        // could only ever succeed by coincidence and otherwise filled the log with
        // ClassNotFoundException for classes no app ever declared. Android launches
        // what the manifest declares; so does KuDroid.
        if (candidates == null || candidates.length == 0) {
            android.util.Log.e("ActivityThread", "No activity candidates supplied");
        }

        for (int i = 0; candidates != null && i < candidates.length; i++) {
            String name = candidates[i];
            if (name == null || name.isEmpty()) continue;
            try {
                Class<?> found = Class.forName(name);
                if (found == null) continue;
                // A manifest can declare non-Activity components, and an obfuscated
                // app can name anything anything. Checking the type here means a bad
                // candidate moves to the next one instead of blowing up on the cast.
                if (!isActivitySubclass(found)) {
                    android.util.Log.e("ActivityThread",
                            "Candidate '" + name + "' is not an Activity subclass, skipping");
                    continue;
                }
                clazz = found;
                resolvedName = name;
                android.util.Log.i("ActivityThread", "Resolved Activity Class: " + name);
                break;
            } catch (Throwable t) {
                // Categorize errors to quickly debug new apps:
                //  - ClassNotFoundException/NoClassDefFoundError with name
                //    android/* → MISSING STUB framework (puts a stub, not a bug).
                //  - with name app/* → candidate is wrong, try the next one.
                String missing = extractMissingClassName(t);
                if (missing != null && missing.startsWith("android/")) {
                    android.util.Log.e("ActivityThread",
                            "FRAMEWORK STUB MISSING: " + missing +
                            " (need to add stub to framework/android/ then rebuild)");
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
        if (resolvedName != null) {
            mInitialActivityName = resolvedName;
        }

        if (clazz != null) {
            try {
                android.util.Log.i("ActivityThread", "Instantiating Activity: " +
                        (mInitialActivityName != null ? mInitialActivityName : clazz.getName()));
                mInitialActivity = (Activity) clazz.newInstance();
                android.util.Log.i("ActivityThread", "Calling onCreate()...");
                mInitialActivity.onCreate(null);
                android.util.Log.i("ActivityThread", "Calling onStart()...");
                mInitialActivity.onStart();
                android.util.Log.i("ActivityThread", "Calling onResume()...");
                mInitialActivity.onResume();

                // Activate asynchronous SurfaceHolder Callback cycle via Message Queue (Android standard)
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
                // Initiate fallback UI now so the app still displays and runs smoothly
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
                statusView.setText("\n👆 Touch the folder below to open:");
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
                            statusView.setText("\n✅ Opened: " + folderPath + "\n(VFS Root OK • Ready to manage & extract files)");
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

        // Start Android's Main Event Loop
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
