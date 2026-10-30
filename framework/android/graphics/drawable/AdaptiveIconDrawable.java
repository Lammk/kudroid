package android.graphics.drawable;

import android.graphics.Canvas;
import android.graphics.Rect;

/** Background and foreground layers drawn in order; the mask shape is not applied. */
public class AdaptiveIconDrawable extends Drawable {
    private final Drawable mBackground;
    private final Drawable mForeground;

    public AdaptiveIconDrawable(Drawable backgroundDrawable, Drawable foregroundDrawable) {
        mBackground = backgroundDrawable;
        mForeground = foregroundDrawable;
    }

    public Drawable getBackground() { return mBackground; }
    public Drawable getForeground() { return mForeground; }

    @Override
    protected void onBoundsChange(Rect bounds) {
        if (mBackground != null) mBackground.setBounds(bounds);
        if (mForeground != null) mForeground.setBounds(bounds);
    }

    @Override
    public void draw(Canvas canvas) {
        if (mBackground != null) mBackground.draw(canvas);
        if (mForeground != null) mForeground.draw(canvas);
    }

    @Override
    public int getOpacity() {
        return android.graphics.PixelFormat.TRANSLUCENT;
    }
}
