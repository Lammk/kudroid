package android.view;

import android.graphics.Bitmap;

public class PointerIcon {
    public static final int TYPE_NULL = 0;
    public static final int TYPE_ARROW = 1000;
    public static final int TYPE_CONTEXT_MENU = 1001;
    public static final int TYPE_HAND = 1002;
    public static final int TYPE_HELP = 1003;
    public static final int TYPE_WAIT = 1004;
    public static final int TYPE_CELL = 1006;
    public static final int TYPE_CROSSHAIR = 1007;
    public static final int TYPE_TEXT = 1008;
    public static final int TYPE_VERTICAL_TEXT = 1009;
    public static final int TYPE_ALIAS = 1010;
    public static final int TYPE_COPY = 1011;
    public static final int TYPE_NO_DROP = 1012;
    public static final int TYPE_ALL_SCROLL = 1013;
    public static final int TYPE_HORIZONTAL_DOUBLE_ARROW = 1014;
    public static final int TYPE_VERTICAL_DOUBLE_ARROW = 1015;
    public static final int TYPE_TOP_RIGHT_DIAGONAL_DOUBLE_ARROW = 1016;
    public static final int TYPE_TOP_LEFT_DIAGONAL_DOUBLE_ARROW = 1017;
    public static final int TYPE_ZOOM_IN = 1018;
    public static final int TYPE_ZOOM_OUT = 1019;
    public static final int TYPE_GRAB = 1020;
    public static final int TYPE_GRABBING = 1021;
    public static final int TYPE_DEFAULT = TYPE_ARROW;

    private final Bitmap mBitmap;
    private final float mHotSpotX;
    private final float mHotSpotY;

    private PointerIcon(Bitmap bitmap, float hotSpotX, float hotSpotY) {
        mBitmap = bitmap;
        mHotSpotX = hotSpotX;
        mHotSpotY = hotSpotY;
    }

    /** Touch devices have no pointer; a bitmap-less icon stands in for each system type. */
    public static PointerIcon getSystemIcon(android.content.Context context, int type) {
        if (context == null) throw new IllegalArgumentException("context must not be null");
        return new PointerIcon(null, 0f, 0f);
    }

    public static PointerIcon getNullIcon() {
        return new PointerIcon(null, 0f, 0f);
    }

    public static PointerIcon create(Bitmap bitmap, float hotSpotX, float hotSpotY) {
        if (bitmap == null) throw new IllegalArgumentException("bitmap must not be null");
        return new PointerIcon(bitmap, hotSpotX, hotSpotY);
    }
}
