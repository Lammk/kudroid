package android.view;

import android.graphics.Bitmap;

public class PointerIcon {
    private final Bitmap mBitmap;
    private final float mHotSpotX;
    private final float mHotSpotY;

    private PointerIcon(Bitmap bitmap, float hotSpotX, float hotSpotY) {
        mBitmap = bitmap;
        mHotSpotX = hotSpotX;
        mHotSpotY = hotSpotY;
    }

    public static PointerIcon create(Bitmap bitmap, float hotSpotX, float hotSpotY) {
        if (bitmap == null) throw new IllegalArgumentException("bitmap must not be null");
        return new PointerIcon(bitmap, hotSpotX, hotSpotY);
    }
}
