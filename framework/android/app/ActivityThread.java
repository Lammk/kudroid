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
    /** Framework classes the launch attempt found missing; shown in the fallback UI. */
    private java.util.ArrayList<String> mFrameworkGaps = new java.util.ArrayList<String>();
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

    /** Packages KuDroid itself is responsible for shipping (the boot classpath). */
    private static final String[] BOOT_PACKAGES = {
        "java.", "javax.", "android.", "androidx.", "dalvik.", "sun.", "libcore.",
        "com.android.", "org.json.", "org.xml.sax.", "org.w3c.dom.", "org.xmlpull.",
        "org.apache.harmony.",
    };

    private static boolean isBootClasspathClass(String name) {
        if (name == null) return false;
        for (int i = 0; i < BOOT_PACKAGES.length; i++) {
            if (name.startsWith(BOOT_PACKAGES[i])) return true;
        }
        return false;
    }

    /**
     * Pull the missing class name out of a ClassNotFoundException / NoClassDefFoundError.
     *
     * KuART reports these with dotted names, sometimes with a ".method" suffix and a
     * trailing explanation, e.g.
     *   "java.util.regex.Pattern.compile (class not implemented in KuDroid framework)"
     *   "java.text.SimpleDateFormat"
     * Older messages used '/' separators. Both forms are accepted; the previous version
     * only accepted '/' and therefore never matched anything KuART actually throws.
     *
     * Returns null when the throwable is not a missing-class error.
     */
    private static String extractMissingClassName(Throwable t) {
        for (Throwable c = t; c != null; c = c.getCause()) {
            String msg = c.getMessage();
            if (msg != null && !msg.isEmpty()) {
                String m = msg.trim();
                int space = m.indexOf(' ');
                if (space > 0) m = m.substring(0, space);
                m = m.replace('/', '.');
                if (m.indexOf('.') > 0) return stripMethodSuffix(m);
            }
            if (c.getCause() == c) break;
        }
        return null;
    }

    /**
     * "java.util.regex.Pattern.compile" -> "java.util.regex.Pattern".
     *
     * The class name ends at the first segment that starts with an upper-case letter,
     * since package segments are lower-case by convention and a trailing method name
     * follows the class. Fully obfuscated names have no upper-case segment, in which
     * case the whole string is returned unchanged.
     */
    private static String stripMethodSuffix(String dotted) {
        int start = 0;
        while (start < dotted.length()) {
            int dot = dotted.indexOf('.', start);
            int segEnd = (dot < 0) ? dotted.length() : dot;
            if (segEnd > start && Character.isUpperCase(dotted.charAt(start))) {
                return dotted.substring(0, segEnd);
            }
            if (dot < 0) break;
            start = dot + 1;
        }
        return dotted;
    }

    /**
     * If this failure was caused by a class KuDroid does not ship, return that class
     * name; otherwise null.
     *
     * The distinction decides what to do next, and getting it wrong is what produced a
     * blank screen: a missing framework class is KuDroid's own gap, so moving on to the
     * next manifest candidate just lands on some SDK Activity (a billing proxy, an OAuth
     * browser shim) that renders nothing. A genuinely wrong candidate — an app class that
     * does not exist — is the only case where trying the next one helps.
     */
    private static String frameworkGapOf(Throwable t) {
        for (Throwable c = t; c != null; c = c.getCause()) {
            final String type = c.getClass().getName();
            if ("java.lang.NoClassDefFoundError".equals(type)
                    || "java.lang.ClassNotFoundException".equals(type)
                    || "java.lang.UnsatisfiedLinkError".equals(type)
                    || "java.lang.AbstractMethodError".equals(type)) {
                String missing = extractMissingClassName(c);
                if (isBootClasspathClass(missing)) return missing;
            }
            if (c.getCause() == c) break;
        }
        return null;
    }

    private static String describeChain(Throwable t) {
        StringBuilder chain = new StringBuilder();
        for (Throwable c = t; c != null; c = c.getCause()) {
            if (chain.length() > 0) chain.append(" <- ");
            chain.append(c.getClass().getName());
            String m = c.getMessage();
            if (m != null && m.length() > 0) chain.append(": ").append(m);
            if (c.getCause() == c) break;
        }
        return chain.toString();
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

        // Names of framework classes KuDroid does not ship, collected while trying
        // candidates. If the FIRST (launcher) candidate fails only because of these,
        // the app is not at fault and no other candidate will do better.
        java.util.ArrayList<String> frameworkGaps = new java.util.ArrayList<String>();

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
                final String gap = frameworkGapOf(t);
                android.util.Log.e("ActivityThread",
                        "Candidate '" + name + "' failed: " + describeChain(t));

                if (gap == null) {
                    // A real app class that does not exist: the candidate is simply
                    // wrong, so the next one is worth a try.
                    continue;
                }

                if (!frameworkGaps.contains(gap)) frameworkGaps.add(gap);
                android.util.Log.e("ActivityThread",
                        "FRAMEWORK CLASS MISSING: " + gap +
                        " (add it under framework/ and rebuild framework.dex)");

                // The launcher activity is the one the app is designed to start. When
                // it fails on a KuDroid gap, walking the rest of the manifest only
                // reaches SDK activities — billing proxies, OAuth browser shims,
                // notification trampolines — none of which draw the app's UI. Stopping
                // here keeps the real cause on screen instead of burying it behind a
                // blank Activity that happened to load.
                if (i == 0) {
                    android.util.Log.e("ActivityThread",
                            "Launcher activity '" + name + "' needs framework classes KuDroid " +
                            "does not implement yet; not falling back to other manifest " +
                            "entries because they are SDK components, not the app UI.");
                    break;
                }
            }
        }
        if (resolvedName != null) {
            mInitialActivityName = resolvedName;
        }
        mFrameworkGaps = frameworkGaps;

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
                final String gap = frameworkGapOf(t);
                if (gap != null) {
                    if (!mFrameworkGaps.contains(gap)) mFrameworkGaps.add(gap);
                    android.util.Log.e("ActivityThread",
                            "FRAMEWORK CLASS MISSING: " + gap +
                            " (add it under framework/ and rebuild framework.dex)");
                }
                // The Activity exists but could not finish onCreate, so it has no
                // content view to draw. Show what stopped it rather than an empty screen.
                if (mInitialActivity == null || mInitialActivity.getContentView() == null) {
                    mInitialActivity = new Activity();
                    showDiagnosticScreen(mInitialActivity,
                            mInitialActivityName != null ? mInitialActivityName : "(unknown)",
                            t);
                } else {
                    mInitialActivity.renderViewHierarchy();
                }
            }
        } else {
            System.err.println("[ActivityThread] Could not resolve any Activity; showing diagnostics.");
            try {
                mInitialActivity = new Activity();
                showDiagnosticScreen(mInitialActivity, "(none resolved)", null);
            } catch (Throwable t) {
                t.printStackTrace();
            }
        }

        // Start Android's Main Event Loop
        android.util.Log.i("ActivityThread", "Entering Looper.loop() main event loop...");
        android.os.Looper.loop();
    }

    /**
     * On-screen report of why the app did not start.
     *
     * This replaces a mock "file explorer" that showed hardcoded folder names: it looked
     * like something was working while telling nothing about the actual failure. What is
     * useful on a blank screen is the name of the activity that was tried, the exception
     * that stopped it, and above all which framework classes KuDroid still has to
     * implement — that list is exactly the next piece of work.
     */
    private void showDiagnosticScreen(Activity host, String attempted, Throwable error) {
        android.widget.LinearLayout root = new android.widget.LinearLayout(host);
        root.setBackgroundColor(0xFF101014);

        android.widget.TextView title = new android.widget.TextView(host);
        title.setText("KuDroid could not start this app");
        title.setTextColor(0xFFFF7043);
        title.setTextSize(22.0f);
        root.addView(title);

        android.widget.TextView act = new android.widget.TextView(host);
        act.setText("\nActivity tried:\n  " + attempted);
        act.setTextColor(0xFFB0BEC5);
        act.setTextSize(14.0f);
        root.addView(act);

        if (mFrameworkGaps != null && !mFrameworkGaps.isEmpty()) {
            android.widget.TextView head = new android.widget.TextView(host);
            head.setText("\nMissing framework classes (" + mFrameworkGaps.size() + "):");
            head.setTextColor(0xFFFFD54F);
            head.setTextSize(16.0f);
            root.addView(head);

            for (int i = 0; i < mFrameworkGaps.size(); i++) {
                android.widget.TextView item = new android.widget.TextView(host);
                item.setText("  " + mFrameworkGaps.get(i));
                item.setTextColor(0xFF81D4FA);
                item.setTextSize(14.0f);
                root.addView(item);
            }

            android.widget.TextView hint = new android.widget.TextView(host);
            hint.setText("\nAdd these under framework/ then run framework/build.sh");
            hint.setTextColor(0xFF78909C);
            hint.setTextSize(13.0f);
            root.addView(hint);
        }

        if (error != null) {
            android.widget.TextView err = new android.widget.TextView(host);
            err.setText("\nError:\n  " + describeChain(error));
            err.setTextColor(0xFFEF9A9A);
            err.setTextSize(13.0f);
            root.addView(err);
        }

        android.widget.TextView tail = new android.widget.TextView(host);
        tail.setText("\nFull detail is in stderr.log and classes.log.");
        tail.setTextColor(0xFF546E7A);
        tail.setTextSize(13.0f);
        root.addView(tail);

        host.setContentView(root);
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
