package android.graphics.drawable.shapes;

import android.graphics.Canvas;
import android.graphics.Outline;
import android.graphics.Paint;

/** A shape that draws itself at a size set by resize(). */
public abstract class Shape implements Cloneable {
    private float mWidth;
    private float mHeight;

    public final float getWidth() { return mWidth; }
    public final float getHeight() { return mHeight; }

    public abstract void draw(Canvas canvas, Paint paint);

    public final void resize(float width, float height) {
        if (width < 0) width = 0;
        if (height < 0) height = 0;
        if (mWidth != width || mHeight != height) {
            mWidth = width;
            mHeight = height;
            onResize(width, height);
        }
    }

    public boolean hasAlpha() { return true; }

    protected void onResize(float width, float height) {}

    public void getOutline(Outline outline) {}

    @Override
    public Shape clone() throws CloneNotSupportedException {
        return (Shape) super.clone();
    }
}
