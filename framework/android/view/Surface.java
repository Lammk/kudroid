package android.view;

import android.graphics.Canvas;
import android.graphics.Rect;

/**
 * Stub tối thiểu của android.view.Surface cho KuDroid.
 *
 * Chỉ cung cấp API mà game canvas-based gọi phổ biến. Vẽ thực tế đi qua
 * pipeline Metal Canvas native (android/graphics/Canvas đã bridge sang C++),
 * nên các phương thức ở đây chỉ cần không crash và trả về object hợp lệ.
 */
public class Surface {
    public static final int ROTATION_0 = 0;
    public static final int ROTATION_90 = 1;
    public static final int ROTATION_180 = 2;
    public static final int ROTATION_270 = 3;

    private Object mNativeSurface = null;
    private boolean mValid = true;

    public Surface() {
    }

    /** Kiểm tra surface còn hợp lệ để vẽ. */
    public boolean isValid() {
        return mValid;
    }

    public void release() {
        mValid = false;
    }

    /** Khóa canvas phần mềm để vẽ 2D. */
    public Canvas lockCanvas() {
        return lockCanvas(null);
    }

    public Canvas lockCanvas(Rect dirty) {
        // Canvas stub của KuDroid bridge xuống C++ Metal pipeline.
        return new Canvas();
    }

    /** Canvas hardware-accelerated — trên KuDroid giống lockCanvas thường. */
    public Canvas lockHardwareCanvas() {
        return new Canvas();
    }

    /** Đăng ký canvas đã vẽ lên compositor. */
    public void unlockCanvasAndPost(Canvas canvas) {
        // no-op: pipeline native tự present qua CAMetalLayer.
    }

    @Deprecated
    public void unlockCanvas(Canvas canvas) {
        // legacy API, giữ để tương thích bytecode cũ.
    }

    /** Kích thước hiện tại của surface (width/height ghi đè bởi native). */
    public int getWidth() {
        return 0;
    }

    public int getHeight() {
        return 0;
    }
}
