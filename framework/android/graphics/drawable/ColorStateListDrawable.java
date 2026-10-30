package android.graphics.drawable;

import android.content.res.ColorStateList;
import android.graphics.Canvas;
import android.graphics.Paint;

/** A solid color taken from a ColorStateList for the current state. */
public class ColorStateListDrawable extends Drawable {
    private ColorStateList mColors;
    private int mAlpha = 255;
    private int mCurrent;

    public ColorStateListDrawable() {}

    public ColorStateListDrawable(ColorStateList colors) {
        setColorStateList(colors);
    }

    public ColorStateList getColorStateList() { return mColors; }

    public void setColorStateList(ColorStateList colors) {
        mColors = colors;
        mCurrent = colors != null ? colors.getColorForState(getState(), colors.getDefaultColor()) : 0;
        invalidateSelf();
    }

    @Override
    public void draw(Canvas canvas) {
        if (canvas == null || mColors == null) return;
        final int alpha = (mAlpha * ((mCurrent >>> 24) & 0xFF)) / 255;
        final Paint paint = new Paint();
        paint.setColor((alpha << 24) | (mCurrent & 0x00FFFFFF));
        canvas.drawRect(getBounds(), paint);
    }

    @Override
    public void setAlpha(int alpha) { mAlpha = alpha; }

    @Override
    public int getAlpha() { return mAlpha; }

    @Override
    public boolean isStateful() { return mColors != null && mColors.isStateful(); }

    @Override
    protected boolean onStateChange(int[] state) {
        if (mColors == null) return false;
        final int color = mColors.getColorForState(state, mColors.getDefaultColor());
        if (color == mCurrent) return false;
        mCurrent = color;
        invalidateSelf();
        return true;
    }

    @Override
    public int getOpacity() {
        return android.graphics.PixelFormat.TRANSLUCENT;
    }
}
