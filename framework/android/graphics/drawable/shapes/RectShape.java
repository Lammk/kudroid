package android.graphics.drawable.shapes;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;

public class RectShape extends Shape {
    private RectF mRect = new RectF();

    public RectShape() {}

    @Override
    public void draw(Canvas canvas, Paint paint) {
        canvas.drawRect(mRect, paint);
    }

    @Override
    protected void onResize(float width, float height) {
        mRect.set(0, 0, width, height);
    }

    protected final RectF rect() { return mRect; }

    @Override
    public RectShape clone() throws CloneNotSupportedException {
        final RectShape shape = (RectShape) super.clone();
        shape.mRect = new RectF(mRect);
        return shape;
    }
}
