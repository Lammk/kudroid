package android.graphics.drawable.shapes;

import android.graphics.Canvas;
import android.graphics.Paint;

public class ArcShape extends RectShape {
    private final float mStartAngle;
    private final float mSweepAngle;

    public ArcShape(float startAngle, float sweepAngle) {
        mStartAngle = startAngle;
        mSweepAngle = sweepAngle;
    }

    public final float getStartAngle() { return mStartAngle; }
    public final float getSweepAngle() { return mSweepAngle; }

    @Override
    public void draw(Canvas canvas, Paint paint) {
        canvas.drawArc(rect(), mStartAngle, mSweepAngle, true, paint);
    }
}
