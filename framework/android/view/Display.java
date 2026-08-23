package android.view;

import android.graphics.Point;
import android.graphics.Rect;
import android.util.DisplayMetrics;

public class Display {
    public static final int DEFAULT_DISPLAY = 0;
    public static final int STATE_OFF = 1;
    public static final int STATE_ON = 2;
    public static final int STATE_DOZE = 3;
    public static final int STATE_DOZE_SUSPEND = 4;
    public static final int STATE_VR = 5;
    public static final int STATE_ON_SUSPEND = 6;

    private int mWidth = 1080;
    private int mHeight = 1920;
    private float mDensity = 2.0f;
    private int mDensityDpi = 320;

    public Display() {}

    public int getDisplayId() {
        return DEFAULT_DISPLAY;
    }

    public boolean isValid() {
        return true;
    }

    public int getWidth() {
        return mWidth;
    }

    public int getHeight() {
        return mHeight;
    }

    public int getRotation() {
        return Surface.ROTATION_0;
    }

    public int getOrientation() {
        return getRotation();
    }

    public void getSize(Point outSize) {
        if (outSize != null) {
            outSize.x = mWidth;
            outSize.y = mHeight;
        }
    }

    public void getRectSize(Rect outSize) {
        if (outSize != null) {
            outSize.set(0, 0, mWidth, mHeight);
        }
    }

    public void getRealSize(Point outSize) {
        getSize(outSize);
    }

    public void getMetrics(DisplayMetrics outMetrics) {
        if (outMetrics != null) {
            outMetrics.widthPixels = mWidth;
            outMetrics.heightPixels = mHeight;
            outMetrics.density = mDensity;
            outMetrics.densityDpi = mDensityDpi;
            outMetrics.scaledDensity = mDensity;
            outMetrics.xdpi = mDensityDpi;
            outMetrics.ydpi = mDensityDpi;
        }
    }

    public void getRealMetrics(DisplayMetrics outMetrics) {
        getMetrics(outMetrics);
    }

    public float getRefreshRate() {
        return 60.0f;
    }

    public int getState() {
        return STATE_ON;
    }

    public static class Mode {
        private int mWidth = 1080;
        private int mHeight = 1920;
        private float mRefreshRate = 60.0f;

        public Mode() {}

        public int getPhysicalWidth() { return mWidth; }
        public int getPhysicalHeight() { return mHeight; }
        public float getRefreshRate() { return mRefreshRate; }
    }
}
