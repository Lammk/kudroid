import android.app.Activity;
import android.app.ApplicationContext;
import android.content.Context;
import android.view.View;
import android.view.Window;
import android.widget.FrameLayout;

public final class WindowTraversalTests {
    public static String thermalProbe(int expected) {
        TestSupport.report.setLength(0);
        android.os.PowerManager power = new android.os.PowerManager();
        if (expected < 0) {
            boolean threw = false;
            try { power.getCurrentThermalStatus(); }
            catch (RuntimeException error) { threw = true; }
            TestSupport.check(threw, "unavailable host thermal status reports an error");
        } else {
            TestSupport.check(power.getCurrentThermalStatus() == expected,
                    "thermal status follows the host severity");
            TestSupport.check(new android.os.PowerManager().getCurrentThermalStatus() == expected,
                    "thermal status is shared across power manager instances");
        }
        return TestSupport.report.toString();
    }

    static final class CountingActivity extends Activity {
        int requests;
        @Override public void requestRender() { requests++; }
    }

    static final class CustomWindow extends Window {
        FrameLayout root;
        CustomWindow(Context context) {
            super(context);
            root = new FrameLayout(context);
        }
        @Override public View getDecorView() { return root; }
    }

    static final class FrameActivity extends Activity {
        int frames;
        @Override public void renderViewHierarchy() { frames++; }
    }

    public static String run() {
        TestSupport.report.setLength(0);
        try { traversal(); } catch (Throwable t) { TestSupport.fail("window traversal", t); }
        try { frameDelivery(); } catch (Throwable t) { TestSupport.fail("frame delivery", t); }
        try { safeArea(); } catch (Throwable t) { TestSupport.fail("safe area", t); }
        try { nativeViewport(); } catch (Throwable t) { TestSupport.fail("native viewport", t); }
        try { touchTimes(); } catch (Throwable t) { TestSupport.fail("queued touch timestamps", t); }
        try { fullyDrawn(); } catch (Throwable t) { TestSupport.fail("fully drawn report", t); }
        try { nativeFrameRequests(); } catch (Throwable t) { TestSupport.fail("native frame requests", t); }
        return TestSupport.report.toString();
    }

    static void fullyDrawn() throws Exception {
        Activity activity = new Activity();
        java.lang.reflect.Method report = Activity.class.getMethod("reportFullyDrawn");
        report.invoke(activity);
        report.invoke(activity);
        java.lang.reflect.Field reported = Activity.class.getDeclaredField("mFullyDrawnReported");
        reported.setAccessible(true);
        TestSupport.check(reported.getBoolean(activity), "fully drawn report is recorded once per activity");
        TestSupport.check(!reported.getBoolean(new Activity()), "new activity gets a fresh fully drawn report");
    }

    static void nativeFrameRequests() throws Exception {
        FrameActivity activity = new FrameActivity();
        activity.attach(new ApplicationContext());
        activity.setContentView(new android.view.SurfaceView(activity));
        activity.performResume();
        java.lang.reflect.Field pending = Activity.class.getDeclaredField("mRenderPending");
        pending.setAccessible(true);
        TestSupport.check(!pending.getBoolean(activity), "native surface avoids redundant software frame messages");
        drainFrames();
        TestSupport.check(activity.frames == 0, "native surface keeps software rendering idle");
        activity.setContentView(new View(activity));
        int before = activity.frames;
        activity.requestRender();
        drainFrames();
        TestSupport.check(activity.frames == before + 1, "software rendering resumes when native surface is removed");
        activity.performPause();
    }

    static void traversal() {
        if (android.os.Looper.myLooper() == null) android.os.Looper.prepareMainLooper();
        CountingActivity activity = new CountingActivity();
        activity.attach(new ApplicationContext());
        activity.performResume();
        TestSupport.check(activity.requests > 0, "resume schedules first frame without input");
        CustomWindow window = new CustomWindow(new ApplicationContext());
        window.setCallback(activity);
        View child = new View(window.getDecorView().getContext());
        window.root.addView(child, new FrameLayout.LayoutParams(37, 41));
        window.updateSurfaceSize(414, 896, true);
        TestSupport.check(window.measureAndLayout(), "custom window traverses");
        TestSupport.check(window.root.getWidth() == 414 && window.root.getHeight() == 896,
                "actual window root is measured and laid out");
        TestSupport.check(child.isAttachedToWindow() && child.getWidth() == 37 && child.getHeight() == 41,
                "custom root children attach and layout");
        window.beginDrawFrame();
        long drawingTime = window.root.getDrawingTime();
        TestSupport.check(drawingTime > 0 && child.getDrawingTime() == drawingTime,
                "attached hierarchy shares frame drawing time");
        window.root.invalidate();
        TestSupport.check(window.root.getDrawingTime() == drawingTime,
                "invalidation preserves drawing time until next frame");
        int before = activity.requests;
        child.invalidate();
        TestSupport.check(activity.requests > before, "child invalidation schedules frame without touch");
        before = activity.requests;
        window.root.invalidate();
        TestSupport.check(activity.requests > before, "ordinary root invalidation schedules frame");
        before = activity.requests;
        child.setLayoutParams(new FrameLayout.LayoutParams(53, 61));
        TestSupport.check(activity.requests > before, "layout request schedules traversal");
        window.measureAndLayout();
        TestSupport.check(child.getWidth() == 53 && child.getHeight() == 61, "requested layout updates bounds");
        FrameLayout oldRoot = window.root;
        window.root = new FrameLayout(oldRoot.getContext());
        window.measureAndLayout();
        TestSupport.check(!oldRoot.isAttachedToWindow(), "replaced root is detached");
        TestSupport.check(oldRoot.getDrawingTime() == 0 && child.getDrawingTime() == 0,
                "detached hierarchy no longer exposes frame time");
        before = activity.requests;
        oldRoot.invalidate();
        oldRoot.requestLayout();
        TestSupport.check(activity.requests == before, "detached root cannot schedule frames");
        View detached = new View(oldRoot.getContext());
        detached.invalidate();
        detached.requestLayout();
        TestSupport.check(activity.requests == before, "unattached views cannot schedule frames");
        window.updateSurfaceSize(0, 0, false);
        TestSupport.check(!window.root.isAttachedToWindow(), "surface destruction detaches root");
    }

    static void drainFrames() throws Exception {
        final boolean[] finished = {false};
        new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(new Runnable() {
            public void run() { finished[0] = true; }
        }, 32);
        java.lang.reflect.Method next = android.os.MessageQueue.class.getDeclaredMethod("next");
        next.setAccessible(true);
        for (int i = 0; i < 20 && !finished[0]; i++) {
            android.os.Message message = (android.os.Message) next.invoke(android.os.Looper.myQueue());
            message.getTarget().dispatchMessage(message);
            message.recycle();
        }
        TestSupport.check(finished[0], "frame queue drains within a bounded number of messages");
    }

    static void safeArea() {
        Window window = new Window(new ApplicationContext());
        View content = new View(new ApplicationContext());
        window.setContentView(content);
        window.updateSurfaceSize(414, 896, true);
        window.measureAndLayout();
        TestSupport.check(content.getWidth() == 404 && content.getHeight() == 818,
                "window content fits host safe area while decor retains surface size");
        TestSupport.check(content.getRootWindowInsets().getSystemWindowInsetTop() == 44,
                "attached descendants can read original root insets");
        android.graphics.Rect visible = new android.graphics.Rect();
        content.getWindowVisibleDisplayFrame(visible);
        TestSupport.check(visible.equals(new android.graphics.Rect(4, 44, 408, 862)),
                "visible display frame uses window dimensions and safe area");
        window.getDecorView().setSystemUiVisibility(0x400 | 0x200);
        window.measureAndLayout();
        TestSupport.check(content.getWidth() == 414 && content.getHeight() == 896,
                "edge-to-edge layout flags retain full surface bounds");
        window.getDecorView().setSystemUiVisibility(0);
        window.measureAndLayout();
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN);
        window.measureAndLayout();
        TestSupport.check(content.getHeight() == 862,
                "runtime fullscreen flag reapplies insets without top padding");
        window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN);
        window.measureAndLayout();
        TestSupport.check(content.getHeight() == 818,
                "clearing fullscreen restores safe-area padding");
        window.dispatchSurfaceDestroyed();
        TestSupport.check(content.getRootWindowInsets() == null,
                "detached views do not expose stale root insets");
    }

    static void nativeViewport() {
        Window window = new Window(new ApplicationContext());
        View surface = new android.view.SurfaceView(new ApplicationContext());
        final float[] point = new float[2];
        surface.setOnTouchListener(new View.OnTouchListener() {
            public boolean onTouch(View view, android.view.MotionEvent event) {
                point[0] = event.getX(); point[1] = event.getY();
                return true;
            }
        });
        window.setContentView(surface);
        window.updateSurfaceSize(414, 896, true);
        window.measureAndLayout();
        TestSupport.check(surface.getWidth() == 414 && surface.getHeight() == 896,
                "host-backed surface layout matches the full native framebuffer");
        TestSupport.check(window.superDispatchTouchEvent(android.view.MotionEvent.obtain(1, 1, 0, 20, 60, 0))
                && point[0] == 20 && point[1] == 60,
                "native surface input shares the framebuffer coordinate origin");
        TestSupport.check(surface.getRootWindowInsets().getSystemWindowInsetTop() == 44,
                "native content retains host inset information without a second viewport offset");
        View software = new View(new ApplicationContext());
        window.setContentView(software);
        window.measureAndLayout();
        TestSupport.check(software.getWidth() == 404 && software.getHeight() == 818,
                "replacing host-backed content restores software safe-area layout");
        window.dispatchSurfaceDestroyed();
    }

    static void frameDelivery() throws Exception {
        FrameActivity activity = new FrameActivity();
        activity.attach(new ApplicationContext());
        activity.performResume();
        for (int i = 0; i < 10; i++) activity.requestRender();
        drainFrames();
        TestSupport.check(activity.frames == 1, "initial frame delivered and repeated requests coalesced");
        activity.requestRender();
        activity.performPause();
        drainFrames();
        TestSupport.check(activity.frames == 1, "queued frame skips paused activity");
        activity.performResume();
        drainFrames();
        TestSupport.check(activity.frames == 2, "resume delivers frame without touch");
    }

    static final class TouchActivity extends Activity {
        final long[] eventTimes = new long[3];
        final long[] downTimes = new long[3];
        int count;
        @Override public boolean dispatchTouchEvent(android.view.MotionEvent event) {
            eventTimes[count] = event.getEventTime();
            downTimes[count++] = event.getDownTime();
            return true;
        }
    }

    static void touchTimes() throws Exception {
        android.app.ActivityThread thread = new android.app.ActivityThread();
        java.lang.reflect.Field current = thread.getClass().getDeclaredField("sCurrentActivityThread");
        current.setAccessible(true);
        Object previous = current.get(null);
        try {
            java.lang.reflect.Method attach = thread.getClass().getDeclaredMethod("attach");
            attach.setAccessible(true);
            attach.invoke(thread);
            TouchActivity activity = new TouchActivity();
            java.lang.reflect.Field target = thread.getClass().getDeclaredField("mInitialActivity");
            target.setAccessible(true);
            target.set(thread, activity);
            long start = android.os.SystemClock.uptimeMillis() - 300;
            android.app.ActivityThread.postTouchEvent(0, 1, 0, 0, start);
            android.app.ActivityThread.postTouchEvent(2, 1, 50, 0, start + 50);
            android.app.ActivityThread.postTouchEvent(2, 1, 100, 0, start + 100);
            android.app.ActivityThread.postTouchEvent(1, 1, 150, 0, start + 150);
            drainFrames();
            TestSupport.check(activity.count == 3 && activity.eventTimes[0] == start
                    && activity.eventTimes[1] == start + 100 && activity.eventTimes[2] == start + 150,
                    "delayed touch dispatch preserves arrival times and latest coalesced move");
            TestSupport.check(activity.downTimes[0] == start && activity.downTimes[1] == start
                    && activity.downTimes[2] == start, "queued gesture retains one original down time");
        } finally { current.set(null, previous); }
    }
}
