package android.app;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;

/**
 * minimal android.app.activity implementation.
 *
 * provides the lifecycle callbacks expected by the original games. for framework
 * kudroid's minimum, lifecycle methods are no-op that apps can override.
 */
public class Activity extends ContextThemeWrapper {
    public static final int SCREEN_ORIENTATION_UNSPECIFIED = -1;
    public static final int SCREEN_ORIENTATION_LANDSCAPE = 0;
    public static final int SCREEN_ORIENTATION_PORTRAIT = 1;
    public static final int SCREEN_ORIENTATION_USER = 2;
    public static final int SCREEN_ORIENTATION_BEHIND = 3;
    public static final int SCREEN_ORIENTATION_SENSOR = 4;
    public static final int SCREEN_ORIENTATION_NOSENSOR = 5;
    public static final int SCREEN_ORIENTATION_SENSOR_LANDSCAPE = 6;
    public static final int SCREEN_ORIENTATION_SENSOR_PORTRAIT = 7;
    public static final int SCREEN_ORIENTATION_REVERSE_LANDSCAPE = 8;
    public static final int SCREEN_ORIENTATION_REVERSE_PORTRAIT = 9;
    public static final int SCREEN_ORIENTATION_FULL_SENSOR = 10;

    private int mRequestedOrientation = SCREEN_ORIENTATION_UNSPECIFIED;
    private boolean mCreated = false;
    private boolean mStarted = false;
    private boolean mResumed = false;

    public Activity() {
    }

    /**
     * Give the Activity its base Context.
     *
     * Called by ActivityThread right after instantiation, the way Android does it.
     * ContextWrapper.attachBaseContext is protected and android.app cannot reach it
     * across the package boundary, so this public entry point exists for the same
     * reason Application.attach does.
     */
    public void attach(Context base) {
        attachBaseContext(base);
    }

    public void setRequestedOrientation(int requestedOrientation) {
        mRequestedOrientation = requestedOrientation;
        try {
            setRequestedOrientation_native(requestedOrientation);
        } catch (Throwable ignored) {}
    }

    public int getRequestedOrientation() {
        return mRequestedOrientation;
    }

    private static native void setRequestedOrientation_native(int requestedOrientation);

    /**
     * is called when the activity is first created.
     */
    protected void onCreate(Bundle savedInstanceState) {
    }

    /**
     * is called when the activity is about to become visible.
     */
    protected void onStart() {
    }

    /**
     * called when the activity is visible.
     */
    protected void onResume() {
    }

    /**
     * is called when the prepare operation is paused.
     */
    protected void onPause() {
    }

    /**
     * is called when the activity is no longer visible.
     */
    protected void onStop() {
    }

    /**
     * is called before the activity is destroyed.
     */
    protected void onDestroy() {
    }

    /**
     * is called when the activity is restarted.
     */
    protected void onRestart() {
    }

    protected void onSaveInstanceState(Bundle outState) {
    }

    protected void onRestoreInstanceState(Bundle savedInstanceState) {
    }

    /**
     * is called when the results of the operation are available.
     */
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
    }

    /**
     * called when a new intent is delivered.
     */
    protected void onNewIntent(Intent intent) {
    }

    /**
     * is called when the activity is created (called by the framework).
     */
    public void performCreate(Bundle savedInstanceState) {
        mCreated = true;
        onCreate(savedInstanceState);
    }

    /**
     * is called when the activity is started (called by the framework).
     */
    public void performStart() {
        mStarted = true;
        onStart();
    }

    /**
     * is called when the operation is resumed (called by the framework).
     */
    public void performResume() {
        mResumed = true;
        onResume();
    }

    /**
     * is called when the activity is paused (called by the framework).
     */
    public void performPause() {
        mResumed = false;
        onPause();
    }

    /**
     * is called when the activity is stopped (called by the framework).
     */
    public void performStop() {
        mStarted = false;
        onStop();
    }

    /**
     * is called when the activity is destroyed (called by the framework).
     */
    public void performDestroy() {
        mCreated = false;
        onDestroy();
    }

    /**
     * returns whether the activity has been created or not.
     */
    public boolean isCreated() {
        return mCreated;
    }

    /**
     * returns whether the operation has been started or not.
     */
    public boolean isStarted() {
        return mStarted;
    }

    /**
     * returns whether the operation has been continued or not.
     */
    public boolean isResumed() {
        return mResumed;
    }

    /**
     * end of operation.
     */
    public void finish() {
    }

    /**
     * returns the intent that started this activity.
     */
    private Intent mIntent;

    /**
     * returns the intent that started this activity.
     *
     * Built once and kept, with the component set to this Activity's own class. It
     * used to return {@code new Intent()} on every call — a fresh, empty Intent with
     * no component, so {@code getIntent().getComponent()} was null and
     * {@code getActivityInfo(null, ...)} could not name anything. That is the first
     * half of the AGDK GameActivity idiom for finding its native library.
     */
    public Intent getIntent() {
        if (mIntent == null) {
            mIntent = new Intent(Intent.ACTION_MAIN);
            final String cls = getClass().getName();
            String pkg = ActivityThread.getPackageName();
            if (pkg == null || pkg.isEmpty()) {
                try {
                    pkg = getPackageName();
                } catch (Throwable ignored) {
                    pkg = "";
                }
            }
            mIntent.setComponent(new android.content.ComponentName(pkg != null ? pkg : "", cls));
        }
        return mIntent;
    }

    /** Replace the launch Intent, as ActivityThread does when it starts a component. */
    public void setIntent(Intent intent) {
        mIntent = intent;
    }

    /**
     * sets the result of this operation.
     */
    public void setResult(int resultCode) {
    }

    /**
     * sets the result of this operation with data.
     */
    public void setResult(int resultCode, Intent data) {
    }

    /**
     * returns window.
     */
    private android.view.Window mWindow;

    /**
     * The Activity's window.
     *
     * One instance per Activity, cached. This used to build a fresh Window on every
     * call, which breaks two things at once: the window has no identity (an app that
     * stores getWindow() and compares it later sees a different object), and
     * everything set through it is lost — including the decor view, since
     * setContentView records it on whichever Window instance happened to be alive.
     *
     * The second half is what stopped Minecraft. androidx's
     * WindowCompat.setDecorFitsSystemWindows does
     * {@code window.getDecorView().getSystemUiVisibility()}, and getDecorView
     * answered null on a Window nobody had ever called setContentView on, so onCreate
     * died with a NullPointerException inside GameActivity.createSurfaceView.
     */
    public android.view.Window getWindow() {
        if (mWindow == null) mWindow = new android.view.Window(this);
        return mWindow;
    }

    private android.view.View mContentView;

    /**
     * Set the content view from a layout resource.
     *
     * KuDroid cannot inflate XML layouts yet — {@link android.view.LayoutInflater} has
     * no parser, so there is no way to turn a resource id into a view tree. Nothing is
     * drawn, and {@code findViewById} keeps returning null exactly as it did before the
     * call.
     *
     * This used to build a hardcoded file-browser screen ("ZArchiver - Storage /sdcard"
     * and a list of invented folders). That is the single most-called method in any
     * Java-UI app's onCreate, so EVERY app rendered someone else's mock UI instead of
     * its own — and worse, it looked like the layout system worked, so the real gap
     * stayed hidden behind a plausible-looking screen.
     *
     * Reporting it once per resource id is the useful behaviour: the id is what a
     * developer needs to find the layout, and repeating it every frame would bury the
     * rest of the log. Apps that build their views in code go through
     * {@link #setContentView(android.view.View)} and are unaffected.
     */
    public void setContentView(int layoutResID) {
        android.util.Log.w("Activity",
                "setContentView(0x" + Integer.toHexString(layoutResID) +
                ") ignored: XML layout inflation is not implemented, so no view tree" +
                " was created. Views built in code still work.");
    }

    /**
     * sets the content view to a view.
     */
    public void setContentView(android.view.View view) {
        mContentView = view;
        // Mirror it onto the Window so getDecorView() reflects what is on screen.
        // androidx reads window insets and system-UI flags off the decor view, and an
        // app that sets its content through the Activity expects the Window to agree.
        getWindow().setContentView(view);
        renderViewHierarchy();
    }

    /**
     * find a view by id.
     */
    public android.view.View findViewById(int id) {
        if (mContentView != null) {
            return mContentView.findViewById(id);
        }
        return null;
    }

    public boolean dispatchTouchEvent(android.view.MotionEvent event) {
        if (mContentView != null) {
            boolean handled = mContentView.dispatchTouchEvent(event);
            renderViewHierarchy();
            return handled;
        }
        return false;
    }

    public android.view.View getContentView() {
        return mContentView;
    }

    /**
     * Draw the entire view hierarchy on the Metal screen.
     *
     * Runs the full Android pipeline in order: measure with the real surface size,
     * layout so every child gets its own bounds, then draw. Skipping measure/layout
     * (which this used to do) left every child at 0,0,0,0 and drew the whole tree on
     * top of itself in the top-left corner.
     */
    public void renderViewHierarchy() {
        if (mContentView == null) {
            return;
        }
        try {
            android.graphics.Canvas canvas = new android.graphics.Canvas();
            final int width = canvas.getWidth();
            final int height = canvas.getHeight();

            if (!(mContentView instanceof android.view.SurfaceView)) {
                canvas.drawColor(0xFF181818);
            }

            // EXACTLY: the root view gets the whole screen, nothing more or less.
            mContentView.measure(
                    android.view.View.MeasureSpec.makeMeasureSpec(
                            width, android.view.View.MeasureSpec.EXACTLY),
                    android.view.View.MeasureSpec.makeMeasureSpec(
                            height, android.view.View.MeasureSpec.EXACTLY));
            mContentView.layout(0, 0, width, height);
            mContentView.draw(canvas);
            canvas.flush();
        } catch (Throwable t) {
            t.printStackTrace();
        }
    }

    /**
     * runs on the UI thread.
     */
    public void runOnUiThread(Runnable action) {
        if (action != null) {
            action.run();
            renderViewHierarchy();
        }
    }

    private static native void nativeRequestPermissions(Activity activity, String permissionsCsv, int requestCode);

    /**
     * requires runtime permissions (Android 6.0+).
     */
    public void requestPermissions(String[] permissions, int requestCode) {
        if (permissions == null || permissions.length == 0) return;
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < permissions.length; ++i) {
            if (i > 0) sb.append(",");
            sb.append(permissions[i]);
        }
        try {
            nativeRequestPermissions(this, sb.toString(), requestCode);
        } catch (Throwable t) {
            int[] grantResults = new int[permissions.length];
            for (int i = 0; i < permissions.length; ++i) {
                grantResults[i] = android.content.pm.PackageManager.PERMISSION_GRANTED;
            }
            onRequestPermissionsResult(requestCode, permissions, grantResults);
        }
    }

    /**
     * get runtime permission request results.
     */
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
    }
}
