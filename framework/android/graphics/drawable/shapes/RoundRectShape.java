package android.graphics.drawable.shapes;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;

public class RoundRectShape extends RectShape {
    private final float[] mOuterRadii;
    private final RectF mInset;
    private final float[] mInnerRadii;

    public RoundRectShape(float[] outerRadii, RectF inset, float[] innerRadii) {
        if (outerRadii != null && outerRadii.length < 8) {
            throw new ArrayIndexOutOfBoundsException("outer radii must have >= 8 values");
        }
        if (innerRadii != null && innerRadii.length < 8) {
            throw new ArrayIndexOutOfBoundsException("inner radii must have >= 8 values");
        }
        mOuterRadii = outerRadii;
        mInset = inset;
        mInnerRadii = innerRadii;
    }

    @Override
    public void draw(Canvas canvas, Paint paint) {
        final float r = mOuterRadii != null ? mOuterRadii[0] : 0f;
        canvas.drawRoundRect(rect(), r, r, paint);
    }
}
