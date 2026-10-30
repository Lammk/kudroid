package android.graphics.drawable.shapes;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;

public class PathShape extends Shape {
    private final Path mPath;
    private final float mStdWidth;
    private final float mStdHeight;
    private float mScaleX = 1f;
    private float mScaleY = 1f;

    public PathShape(Path path, float stdWidth, float stdHeight) {
        mPath = path;
        mStdWidth = stdWidth;
        mStdHeight = stdHeight;
    }

    @Override
    public void draw(Canvas canvas, Paint paint) {
        canvas.save();
        canvas.scale(mScaleX, mScaleY);
        canvas.drawPath(mPath, paint);
        canvas.restore();
    }

    @Override
    protected void onResize(float width, float height) {
        mScaleX = mStdWidth > 0 ? width / mStdWidth : 1f;
        mScaleY = mStdHeight > 0 ? height / mStdHeight : 1f;
    }
}
