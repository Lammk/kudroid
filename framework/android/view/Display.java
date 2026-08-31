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
        try {
            android.graphics.Canvas c = new android.graphics.Canvas();
            int w = c.getWidth();
            if (w > 0) return w;
        } catch (Throwable ignored) {}
        return 1080;
    }

    public int getHeight() {
        try {
            android.graphics.Canvas c = new android.graphics.Canvas();
            int h = c.getHeight();
            if (h > 0) return h;
        } catch (Throwable ignored) {}
        return 1920;
    }

    public int getRotation() {
        int w = getWidth();
        int h = getHeight();
        if (w > h) return Surface.ROTATION_90;
        return Surface.ROTATION_0;
    }

    public int getOrientation() {
        return getRotation();
    }

    public void getSize(Point outSize) {
        if (outSize != null) {
            outSize.x = getWidth();
            outSize.y = getHeight();
        }
    }

    public void getRectSize(Rect outSize) {
        if (outSize != null) {
            outSize.set(0, 0, getWidth(), getHeight());
        }
    }

    public void getRealSize(Point outSize) {
        getSize(outSize);
    }

    public void getMetrics(DisplayMetrics outMetrics) {
        if (outMetrics != null) {
            int w = getWidth();
            int h = getHeight();
            outMetrics.widthPixels = w;
            outMetrics.heightPixels = h;
            float density = 3.0f;
            if (Math.min(w, h) <= 750) density = 2.0f;
            else if (Math.min(w, h) >= 1200) density = 3.0f;
            int dpi = (int)(density * 160.0f);
            outMetrics.density = density;
            outMetrics.densityDpi = dpi;
            outMetrics.scaledDensity = density;
            outMetrics.xdpi = dpi;
            outMetrics.ydpi = dpi;
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
        public Mode() {}
        public int getPhysicalWidth() {
            try {
                return new android.graphics.Canvas().getWidth();
            } catch (Throwable ignored) {
                return 1080;
            }
        }
        public int getPhysicalHeight() {
            try {
                return new android.graphics.Canvas().getHeight();
            } catch (Throwable ignored) {
                return 1920;
            }
        }
        public float getRefreshRate() { return 60.0f; }
    }
}
