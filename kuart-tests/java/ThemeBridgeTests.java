import android.app.Activity;
import android.app.ActivityThread;
import android.app.Application;
import android.app.ApplicationContext;
import android.content.res.Configuration;
import java.lang.reflect.Field;
import java.lang.reflect.Method;

public final class ThemeBridgeTests {
    static final class ThemeActivity extends Activity {
        int changes;
        int renders;
        @Override public void requestRender() { renders++; }
        @Override public void onConfigurationChanged(Configuration config) {
            changes++;
            TestSupport.check(config.uiMode == getResources().getConfiguration().uiMode,
                    "activity resources update before callback");
            config.uiMode = 0;
        }
    }

    static void set(ActivityThread thread, String name, Object value) throws Exception {
        Field field = ActivityThread.class.getDeclaredField(name);
        field.setAccessible(true);
        field.set(thread, value);
    }

    public static String run() {
        TestSupport.report.setLength(0);
        try { changes(); } catch (Throwable t) { TestSupport.fail("appearance bridge", t); }
        return TestSupport.report.toString();
    }

    static void changes() throws Exception {
        if (android.os.Looper.myLooper() == null) android.os.Looper.prepareMainLooper();
        final Application application = new Application();
        application.attach(new ApplicationContext());
        final ThemeActivity activity = new ThemeActivity();
        activity.attach(new ApplicationContext());
        Configuration day = new Configuration(activity.getResources().getConfiguration());
        day.uiMode = Configuration.UI_MODE_TYPE_NORMAL | Configuration.UI_MODE_NIGHT_NO;
        application.getResources().updateConfiguration(day, null);
        activity.getResources().updateConfiguration(day, null);
        final int[] applicationChanges = {0};
        application.registerComponentCallbacks(new android.content.ComponentCallbacks() {
            public void onLowMemory() {}
            public void onConfigurationChanged(Configuration config) {
                applicationChanges[0]++;
                TestSupport.check(config.uiMode == application.getResources().getConfiguration().uiMode
                        && config.uiMode == activity.getResources().getConfiguration().uiMode,
                        "both resource contexts update before application callback");
            }
        });
        ActivityThread previous = ActivityThread.currentActivityThread();
        try {
            set(null, "sCurrentActivityThread", null);
            ActivityThread.postDarkModeChanged(true);
            ActivityThread thread = new ActivityThread();
            set(thread, "mApplication", application);
            set(thread, "mInitialActivity", activity);
            Method attach = ActivityThread.class.getDeclaredMethod("attach");
            attach.setAccessible(true);
            attach.invoke(thread);
            ActivityThread.postDarkModeChanged(false);
            ActivityThread.postDarkModeChanged(true);
            android.os.MessageQueue queue = android.os.Looper.myQueue();
            Method next = android.os.MessageQueue.class.getDeclaredMethod("next");
            next.setAccessible(true);
            android.os.Message message = (android.os.Message) next.invoke(queue);
            message.getTarget().dispatchMessage(message);
            message.recycle();
            TestSupport.check(activity.changes == 1 && applicationChanges[0] == 1,
                    "startup appearance retained and rapid changes coalesced");
            TestSupport.check(activity.renders == 1, "appearance change requests frame without touch");
            TestSupport.check(activity.getResources().getConfiguration().uiMode
                    == (Configuration.UI_MODE_TYPE_NORMAL | Configuration.UI_MODE_NIGHT_YES),
                    "dark state and mode type preserved after callback");
            ActivityThread.applyDarkMode(true);
            TestSupport.check(activity.changes == 1 && applicationChanges[0] == 1,
                    "duplicate appearance skips callbacks and redraw");
            ActivityThread.applyDarkMode(false);
            TestSupport.check(activity.changes == 2 && applicationChanges[0] == 2 && activity.renders == 2,
                    "light transition updates resources, callbacks, and frame request");
            TestSupport.check(activity.getResources().getConfiguration().uiMode
                    == (Configuration.UI_MODE_TYPE_NORMAL | Configuration.UI_MODE_NIGHT_NO),
                    "light state preserved after callback");
        } finally {
            set(null, "sCurrentActivityThread", previous);
        }
    }
}
