package android.view;

import android.graphics.Canvas;
import android.graphics.Rect;

/**
 * Stub tối thiểu của android.view.SurfaceHolder cho KuDroid.
 *
 * Game Android cổ điển (MCPE v.v.) khai báo
 *   class MainActivity extends SurfaceView implements SurfaceHolder.Callback2
 * nên JVM bắt buộc phải resolve được interface này khi load class — thiếu nó
 * gây ClassNotFoundException ngay cả trước khi onCreate chạy.
 */
public interface SurfaceHolder {
    /** Loại buffer của surface. */
    public static final int SURFACE_TYPE_NORMAL = 0;
    public static final int SURFACE_TYPE_HARDWARE = 1;
    public static final int SURFACE_TYPE_GPU = 2;
    public static final int SURFACE_TYPE_PUSH_BUFFERS = 3;

    /** Callback cơ bản: tạo/thay đổi/hủy surface. */
    public interface Callback {
        void surfaceCreated(SurfaceHolder holder);
        void surfaceChanged(SurfaceHolder holder, int format, int width, int height);
        void surfaceDestroyed(SurfaceHolder holder);
    }

    /**
     * Callback2 mở rộng Callback với surfaceRedrawNeeded — Activity dùng
     * surface làm cửa sổ chính (window takes surface) cần implement cái này.
     */
    public interface Callback2 extends Callback {
        void surfaceRedrawNeeded(SurfaceHolder holder);
    }

    /** Thêm callback nhận sự kiện surface. */
    void addCallback(Callback callback);

    void removeCallback(Callback callback);

    /** Trả về Surface thật backing view này. */
    Surface getSurface();

    Rect getSurfaceFrame();

    boolean isCreating();

    @Deprecated
    void setType(int type);

    void setFixedSize(int width, int height);

    void setSizeFromLayout();

    void setFormat(int format);

    void setKeepScreenOn(boolean screenOn);

    Canvas lockCanvas();

    Canvas lockCanvas(Rect dirty);

    void unlockCanvasAndPost(Canvas canvas);

    @Deprecated
    Canvas lockCanvasAndroidOnly(Rect dirty);
}
