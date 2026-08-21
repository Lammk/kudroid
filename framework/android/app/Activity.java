package android.app;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;

/**
 * triển khai android.app.activity tối thiểu.
 *
 * cung cấp các lệnh gọi lại vòng đời mà các trò chơi gốc mong đợi. đối với khuôn khổ
 * tối thiểu của kudroid, các phương thức vòng đời là no-op mà các ứng dụng có thể ghi đè.
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
     * được gọi khi hoạt động được tạo lần đầu tiên.
     */
    protected void onCreate(Bundle savedInstanceState) {
    }

    /**
     * được gọi khi hoạt động chuẩn bị hiển thị.
     */
    protected void onStart() {
    }

    /**
     * được gọi khi hoạt động đã hiển thị.
     */
    protected void onResume() {
    }

    /**
     * được gọi khi hoạt động chuẩn bị bị tạm dừng.
     */
    protected void onPause() {
    }

    /**
     * được gọi khi hoạt động không còn hiển thị nữa.
     */
    protected void onStop() {
    }

    /**
     * được gọi trước khi hoạt động bị phá hủy.
     */
    protected void onDestroy() {
    }

    /**
     * được gọi khi hoạt động được khởi động lại.
     */
    protected void onRestart() {
    }

    /**
     * được gọi khi kết quả hoạt động có sẵn.
     */
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
    }

    /**
     * được gọi khi một intent mới được chuyển đến.
     */
    protected void onNewIntent(Intent intent) {
    }

    /**
     * được gọi khi hoạt động được tạo (được gọi bởi khuôn khổ).
     */
    public void performCreate(Bundle savedInstanceState) {
        mCreated = true;
        onCreate(savedInstanceState);
    }

    /**
     * được gọi khi hoạt động được bắt đầu (được gọi bởi khuôn khổ).
     */
    public void performStart() {
        mStarted = true;
        onStart();
    }

    /**
     * được gọi khi hoạt động được tiếp tục (được gọi bởi khuôn khổ).
     */
    public void performResume() {
        mResumed = true;
        onResume();
    }

    /**
     * được gọi khi hoạt động bị tạm dừng (được gọi bởi khuôn khổ).
     */
    public void performPause() {
        mResumed = false;
        onPause();
    }

    /**
     * được gọi khi hoạt động bị dừng (được gọi bởi khuôn khổ).
     */
    public void performStop() {
        mStarted = false;
        onStop();
    }

    /**
     * được gọi khi hoạt động bị phá hủy (được gọi bởi khuôn khổ).
     */
    public void performDestroy() {
        mCreated = false;
        onDestroy();
    }

    /**
     * trả về việc hoạt động đã được tạo hay chưa.
     */
    public boolean isCreated() {
        return mCreated;
    }

    /**
     * trả về việc hoạt động đã được bắt đầu hay chưa.
     */
    public boolean isStarted() {
        return mStarted;
    }

    /**
     * trả về việc hoạt động đã được tiếp tục hay chưa.
     */
    public boolean isResumed() {
        return mResumed;
    }

    /**
     * kết thúc hoạt động.
     */
    public void finish() {
    }

    /**
     * trả về intent đã bắt đầu hoạt động này.
     */
    public Intent getIntent() {
        return new Intent();
    }

    /**
     * đặt kết quả của hoạt động này.
     */
    public void setResult(int resultCode) {
    }

    /**
     * đặt kết quả của hoạt động này kèm theo dữ liệu.
     */
    public void setResult(int resultCode, Intent data) {
    }

    /**
     * trả về cửa sổ.
     */
    public android.view.Window getWindow() {
        return new android.view.Window(this);
    }

    private android.view.View mContentView;

    /**
     * đặt chế độ xem nội dung từ một tài nguyên bố cục.
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
     * đặt chế độ xem nội dung thành một dạng xem.
     */
    public void setContentView(android.view.View view) {
        mContentView = view;
        renderViewHierarchy();
    }

    /**
     * tìm một dạng xem theo id.
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

    /**
     * vẽ toàn bộ cây view hierarchy lên màn hình Metal.
     */
    public void renderViewHierarchy() {
        if (mContentView == null) {
            setContentView(0);
            return;
        }
        try {
            android.graphics.Canvas canvas = new android.graphics.Canvas();
            canvas.drawColor(0xFF181818);
            mContentView.layout(0, 0, canvas.getWidth(), canvas.getHeight());
            mContentView.draw(canvas);
            canvas.flush();
        } catch (Throwable t) {
            t.printStackTrace();
        }
    }

    /**
     * chạy trên luồng giao diện người dùng.
     */
    public void runOnUiThread(Runnable action) {
        if (action != null) {
            action.run();
            renderViewHierarchy();
        }
    }

    /**
     * yêu cầu các quyền runtime (Android 6.0+).
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
     * nhận kết quả yêu cầu quyền runtime.
     */
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
    }
}
