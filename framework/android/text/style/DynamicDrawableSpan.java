package android.text.style;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;

/** A ReplacementSpan backed by a Drawable whose bounds are stable across calls. */
public abstract class DynamicDrawableSpan extends ReplacementSpan {
    public static final int ALIGN_BOTTOM = 0;
    public static final int ALIGN_BASELINE = 1;
    public static final int ALIGN_CENTER = 2;

    protected final int mVerticalAlignment;
    private java.lang.ref.WeakReference<Drawable> mDrawableRef;

    public DynamicDrawableSpan() {
        this(ALIGN_BOTTOM);
    }

    protected DynamicDrawableSpan(int verticalAlignment) {
        mVerticalAlignment = verticalAlignment;
    }

    public int getVerticalAlignment() {
        return mVerticalAlignment;
    }

    public abstract Drawable getDrawable();

    @Override
    public int getSize(Paint paint, CharSequence text, int start, int end,
            Paint.FontMetricsInt fm) {
        Drawable drawable = getCachedDrawable();
        if (drawable == null) return 0;
        Rect rect = drawable.getBounds();
        if (fm != null) {
            fm.ascent = -rect.bottom;
            fm.descent = 0;
            fm.top = fm.ascent;
            fm.bottom = 0;
        }
        return rect.right;
    }

    @Override
    public void draw(Canvas canvas, CharSequence text, int start, int end, float x,
            int top, int y, int bottom, Paint paint) {
        Drawable drawable = getCachedDrawable();
        if (drawable == null) return;
        canvas.save();
        int transY = bottom - drawable.getBounds().bottom;
        if (mVerticalAlignment == ALIGN_BASELINE) {
            transY -= paint.getFontMetricsInt().descent;
        } else if (mVerticalAlignment == ALIGN_CENTER) {
            transY = (bottom - top) / 2 - drawable.getBounds().height() / 2;
        }
        canvas.translate(x, transY);
        drawable.draw(canvas);
        canvas.restore();
    }

    private Drawable getCachedDrawable() {
        java.lang.ref.WeakReference<Drawable> ref = mDrawableRef;
        Drawable drawable = ref != null ? ref.get() : null;
        if (drawable == null) {
            drawable = getDrawable();
            mDrawableRef = new java.lang.ref.WeakReference<Drawable>(drawable);
        }
        return drawable;
    }
}
