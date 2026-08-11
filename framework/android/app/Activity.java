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
    private boolean mCreated = false;
    private boolean mStarted = false;
    private boolean mResumed = false;

    public Activity() {
    }

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

    /**
     * đặt chế độ xem nội dung từ một tài nguyên bố cục.
     */
    public void setContentView(int layoutResID) {
    }

    /**
     * đặt chế độ xem nội dung thành một dạng xem.
     */
    public void setContentView(android.view.View view) {
    }

    /**
     * tìm một dạng xem theo id.
     */
    public android.view.View findViewById(int id) {
        return null;
    }

    /**
     * chạy trên luồng giao diện người dùng.
     */
    public void runOnUiThread(Runnable action) {
        action.run();
    }
}
