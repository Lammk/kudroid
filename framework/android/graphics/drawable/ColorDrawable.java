package android.graphics.drawable;

/**
 * android.graphics.drawable.ColorDrawable — a solid colour.
 *
 * Was an empty generated stub with only a no-argument constructor, while all five real APKs
 * in the corpus construct it as {@code ColorDrawable(I)V} — the colour is the whole point of
 * the class. That reference resolved to nothing, so apps failed while building their initial
 * view background.
 *
 * Extends Drawable so it can be handed to View.setBackground and Window.setBackgroundDrawable
 * like any other; the stub did not, which would have been a ClassCastException the moment it
 * was used even if the constructor had existed.
 */
public class ColorDrawable extends Drawable {
    private int mColor;
    private int mAlpha = 255;

    public ColorDrawable() {
        this(0);
    }

    public ColorDrawable(int color) {
        mColor = color;
    }

    public int getColor() {
        return mColor;
    }

    public void setColor(int color) {
        mColor = color;
    }

    @Override
    public void draw(android.graphics.Canvas canvas) {
        if (canvas == null) return;
        // Alpha is applied here rather than stored into mColor, so setAlpha() is reversible
        // and getColor() keeps returning what was set.
        final int alpha = (mAlpha * ((mColor >>> 24) & 0xFF)) / 255;
        canvas.drawColor((alpha << 24) | (mColor & 0x00FFFFFF));
    }

    @Override
    public void setAlpha(int alpha) {
        mAlpha = alpha < 0 ? 0 : (alpha > 255 ? 255 : alpha);
    }

    public int getAlpha() {
        return mAlpha;
    }

    /**
     * Opacity, which layout code uses to decide whether it can skip drawing what is behind.
     *
     * Computed from the colour rather than assumed: reporting OPAQUE for a transparent colour
     * makes a parent skip its own background and leaves whatever was in the buffer showing.
     * The values are PixelFormat's (TRANSPARENT=0, TRANSLUCENT=-3, OPAQUE=-1).
     */
    @Override
    public int getOpacity() {
        final int alpha = (mAlpha * ((mColor >>> 24) & 0xFF)) / 255;
        if (alpha == 0) return 0;
        if (alpha == 255) return -1;
        return -3;
    }

    @Override
    public int getIntrinsicWidth() {
        return -1;  // no inherent size: a colour fills whatever bounds it is given
    }

    @Override
    public int getIntrinsicHeight() {
        return -1;
    }
}
