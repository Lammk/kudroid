package android.app;

import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.FrameLayout;

public final class SurfaceLifecycleTests {
    private static final StringBuilder report = new StringBuilder();
    private static native void configureSurface(boolean ready, int width, int height);

    private static void check(boolean ok, String name) {
        report.append(ok ? "PASS " : "FAIL ").append(name).append('\n');
    }

    private static final class CallbackActivity extends Activity implements SurfaceHolder.Callback2 {
        int creates, changes, destroys, redraws, wrongHolder;
        SurfaceHolder expected;
        int width, height, format;

        private void holder(SurfaceHolder holder) {
            if (holder != expected) wrongHolder++;
        }
        public void surfaceCreated(SurfaceHolder holder) { holder(holder); creates++; }
        public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
            holder(holder); changes++; this.format = format; width = w; height = h;
        }
        public void surfaceDestroyed(SurfaceHolder holder) { holder(holder); destroys++; }
        public void surfaceRedrawNeeded(SurfaceHolder holder) { holder(holder); redraws++; }
    }

    private static final class NativeWindowActivity extends NativeActivity {
        int creates, changes, destroys, wrongHolder;
        public void surfaceCreated(SurfaceHolder holder) {
            super.surfaceCreated(holder);
            creates++;
            if (holder != getWindow().getSurfaceHolder()) wrongHolder++;
        }
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            super.surfaceChanged(holder, format, width, height); changes++;
        }
        public void surfaceDestroyed(SurfaceHolder holder) {
            super.surfaceDestroyed(holder); destroys++;
        }
    }

    public static String run() {
        android.os.Looper.prepareMainLooper();
        ActivityThread thread = new ActivityThread();
        configureSurface(true, 800, 600);
        CallbackActivity activity = new CallbackActivity();
        activity.attach(new ApplicationContext());
        SurfaceView view = new SurfaceView(activity);
        activity.expected = view.getHolder();
        view.getHolder().addCallback(activity);
        activity.setContentView(view);
        thread.dispatchSurfaceCallbacks(activity);
        check(activity.creates == 1 && activity.changes == 1 && activity.redraws == 1,
                "registered callback receives one creation, change and redraw");
        check(activity.wrongHolder == 0, "callbacks retain the registered SurfaceView holder");
        long handle = view.getHolder().getSurface().mNativeObject;
        thread.dispatchSurfaceCallbacks(activity);
        thread.dispatchSurfaceCallbacks(activity);
        check(activity.creates == 1 && activity.changes == 1 && activity.redraws == 1,
                "repeated traversal does not recreate the surface");
        check(handle != 0 && view.getHolder().getSurface().mNativeObject == handle,
                "repeated traversal retains the native window");
        configureSurface(true, 640, 480);
        thread.dispatchSurfaceCallbacks(activity);
        check(activity.creates == 1 && activity.changes == 2 && activity.redraws == 2
                && activity.width == view.getWidth() && activity.height == view.getHeight(),
                "resize changes dimensions without another creation");
        configureSurface(false, 640, 480);
        thread.dispatchSurfaceCallbacks(activity);
        thread.dispatchSurfaceCallbacks(activity);
        check(activity.destroys == 1 && !view.getHolder().getSurface().isValid(),
                "surface loss destroys exactly once and invalidates the holder");
        configureSurface(true, 640, 480);
        thread.dispatchSurfaceCallbacks(activity);
        check(activity.creates == 2 && activity.changes == 3 && activity.wrongHolder == 0,
                "surface recreation preserves callback ownership");
        view.getHolder().removeCallback(activity);
        thread.dispatchSurfaceCallbacks(activity);
        check(activity.creates == 2 && activity.changes == 3,
                "removed callbacks stay removed");

        check(activity.format == android.graphics.PixelFormat.RGBA_8888,
                "default format describes the RGBA surface");
        CallbackActivity formatted = new CallbackActivity();
        formatted.attach(new ApplicationContext());
        SurfaceView formattedView = new SurfaceView(formatted);
        formatted.expected = formattedView.getHolder();
        formatted.expected.addCallback(formatted);
        formatted.expected.setFormat(android.graphics.PixelFormat.RGBX_8888);
        check(formatted.changes == 0, "format selection before creation defers notification");
        formatted.setContentView(formattedView);
        thread.dispatchSurfaceCallbacks(formatted);
        check(formatted.creates == 1 && formatted.changes == 1
                && formatted.format == android.graphics.PixelFormat.RGBX_8888,
                "initial callback reports the requested format");
        formatted.expected.setFormat(android.graphics.PixelFormat.RGBA_8888);
        thread.dispatchSurfaceCallbacks(formatted);
        check(formatted.creates == 1 && formatted.changes == 2 && formatted.redraws == 2
                && formatted.format == android.graphics.PixelFormat.RGBA_8888,
                "format changes notify and redraw without another creation");
        formatted.expected.setFormat(android.graphics.PixelFormat.RGBA_8888);
        thread.dispatchSurfaceCallbacks(formatted);
        check(formatted.changes == 2 && formatted.redraws == 2,
                "unchanged format does not notify again");
        formatted.expected.setFormat(android.graphics.PixelFormat.OPAQUE);
        thread.dispatchSurfaceCallbacks(formatted);
        check(formatted.format == android.graphics.PixelFormat.RGB_565 && formatted.changes == 3,
                "opaque requests normalize to RGB565");
        configureSurface(false, 640, 480);
        thread.dispatchSurfaceCallbacks(formatted);
        configureSurface(true, 640, 480);
        thread.dispatchSurfaceCallbacks(formatted);
        check(formatted.creates == 2 && formatted.changes == 4
                && formatted.format == android.graphics.PixelFormat.RGB_565,
                "surface recreation retains its selected format");

        CallbackActivity unregistered = new CallbackActivity();
        unregistered.attach(new ApplicationContext());
        unregistered.setContentView(new SurfaceView(unregistered));
        thread.dispatchSurfaceCallbacks(unregistered);
        check(unregistered.creates == 0 && unregistered.changes == 0,
                "implementing Callback alone does not subscribe an Activity");

        Activity multiple = new Activity();
        multiple.attach(new ApplicationContext());
        FrameLayout layout = new FrameLayout(multiple);
        SurfaceView first = new SurfaceView(multiple);
        SurfaceView second = new SurfaceView(multiple);
        CallbackActivity firstCallback = new CallbackActivity();
        CallbackActivity secondCallback = new CallbackActivity();
        firstCallback.expected = first.getHolder();
        secondCallback.expected = second.getHolder();
        first.getHolder().addCallback(firstCallback);
        second.getHolder().addCallback(secondCallback);
        layout.addView(first);
        layout.addView(second);
        multiple.setContentView(layout);
        thread.dispatchSurfaceCallbacks(multiple);
        check(firstCallback.creates == 1 && secondCallback.creates == 1
                && firstCallback.wrongHolder == 0 && secondCallback.wrongHolder == 0,
                "each SurfaceView dispatches its own registered callbacks");

        NativeWindowActivity nativeActivity = new NativeWindowActivity();
        nativeActivity.attach(new ApplicationContext());
        nativeActivity.onCreate(null);
        thread.dispatchSurfaceCallbacks(nativeActivity);
        thread.dispatchSurfaceCallbacks(nativeActivity);
        check(nativeActivity.creates == 1 && nativeActivity.changes == 1
                && nativeActivity.wrongHolder == 0,
                "NativeActivity explicitly owns one Window surface lifecycle");
        configureSurface(false, 640, 480);
        thread.dispatchSurfaceCallbacks(nativeActivity);
        check(nativeActivity.destroys == 1, "NativeActivity receives Window surface loss");
        return report.toString();
    }
}
