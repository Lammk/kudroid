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
        private final int mModeId;

        public Mode() {
            this(1);
        }

        Mode(int modeId) {
            this.mModeId = modeId;
        }

        public int getModeId() {
            return mModeId;
        }

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

    /**
     * The one mode this display has.
     *
     * A single-element array rather than an empty one: callers iterate the result to pick a
     * refresh rate and an empty array leaves them with nothing to pick, which reads as a
     * display that cannot be driven. The mode reports the live surface size, so it stays
     * consistent with getSize() rather than describing a second, imaginary display.
     */
    public Mode[] getSupportedModes() {
        return new Mode[] { new Mode(1) };
    }

    public Mode getMode() {
        return new Mode(1);
    }

    /**
     * Nanoseconds by which this display's vsync is offset from the timestamp Choreographer
     * reports.
     *
     * Zero, and that is the honest answer rather than a placeholder: KuDroid presents through
     * CoreAnimation, which hands out the frame deadline directly, so there is no separate
     * hardware offset to correct for. An invented non-zero value would make a frame pacer
     * aim slightly early or late on every frame.
     */
    public long getAppVsyncOffsetNanos() {
        return 0L;
    }

    /**
     * Nanoseconds before vsync by which a frame must be submitted.
     *
     * One frame at the reported refresh rate, which is what a display with no deeper
     * pipeline reports. Zero would tell a pacer it may submit at the instant of vsync.
     */
    public long getPresentationDeadlineNanos() {
        final float rate = getRefreshRate();
        return rate > 0.0f ? (long) (1000000000.0f / rate) : 16666666L;
    }

    /**
     * Whether this display can show more than sRGB.
     *
     * False. iOS devices with a P3 panel do exist, but KuDroid's swapchain and the Metal
     * layer behind it are configured for sRGB — claiming wide gamut would have the app
     * submit P3 content that is then displayed as sRGB, which shifts every colour.
     */
    public boolean isWideColorGamut() {
        return false;
    }

    /** HDR capabilities. Null means "none", which is what the platform returns. */
    public HdrCapabilities getHdrCapabilities() {
        return null;
    }

    public static final class HdrCapabilities {
        public static final int HDR_TYPE_DOLBY_VISION = 1;
        public static final int HDR_TYPE_HDR10 = 2;
        public static final int HDR_TYPE_HLG = 3;
        public static final int HDR_TYPE_HDR10_PLUS = 4;

        public int[] getSupportedHdrTypes() {
            return new int[0];
        }
    }

    public String getName() {
        return "Built-in Screen";
    }

    /** Every flag off: no secure output, no presentation, no rotation lock. */
    public int getFlags() {
        return 0;
    }
}
