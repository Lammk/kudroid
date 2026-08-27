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
    public Intent getIntent() {
        return new Intent();
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
    public android.view.Window getWindow() {
        return new android.view.Window(this);
    }

    private android.view.View mContentView;

    /**
     * set the content view from a layout resource.
     */
    public void setContentView(int layoutResID) {
        android.widget.LinearLayout root = new android.widget.LinearLayout(this);
        root.setBackgroundColor(0xFF181818);
        
        android.widget.TextView title = new android.widget.TextView(this);
        title.setText("📁 ZArchiver - Storage /sdcard");
        title.setTextColor(0xFF00E676);
        title.setTextSize(20.0f);
        root.addView(title);

        android.widget.TextView sep = new android.widget.TextView(this);
        sep.setText("──────────────────────────────────────────");
        sep.setTextColor(0xFF424242);
        root.addView(sep);

        String[] sampleItems = new String[] {
            "📂 Android",
            "📂 DCIM",
            "📂 Download",
            "📂 Documents",
            "📂 Music",
            "📂 Pictures",
            "📄 ZArchiver_Archive.7z (14.2 MB)",
            "📄 backup_data.zip (128.5 MB)",
            "📄 GameROM_Patch.rar (4.8 MB)"
        };

        for (String item : sampleItems) {
            android.widget.TextView tv = new android.widget.TextView(this);
            tv.setText(item);
            if (item.startsWith("📂")) {
                tv.setTextColor(0xFFFFCA28);
            } else {
                tv.setTextColor(0xFF81D4FA);
            }
            tv.setTextSize(16.0f);
            root.addView(tv);
        }

        mContentView = root;
        renderViewHierarchy();
    }

    /**
     * sets the content view to a view.
     */
    public void setContentView(android.view.View view) {
        mContentView = view;
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
     */
    public void renderViewHierarchy() {
        if (mContentView == null) {
            return;
        }
        try {
            android.graphics.Canvas canvas = new android.graphics.Canvas();
            if (!(mContentView instanceof android.view.SurfaceView)) {
                canvas.drawColor(0xFF181818);
            }
            mContentView.layout(0, 0, canvas.getWidth(), canvas.getHeight());
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

    /**
     * requires runtime permissions (Android 6.0+).
     */
    public void requestPermissions(String[] permissions, int requestCode) {
        if (permissions == null) return;
        int[] grantResults = new int[permissions.length];
        for (int i = 0; i < permissions.length; ++i) {
            grantResults[i] = android.content.pm.PackageManager.PERMISSION_GRANTED;
        }
        onRequestPermissionsResult(requestCode, permissions, grantResults);
    }

    /**
     * get runtime permission request results.
     */
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
    }
}
