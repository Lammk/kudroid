package android.widget;

import android.content.Context;
import android.graphics.Canvas;
import android.util.AttributeSet;

/** Minimal edge-effect state holder; drawing is intentionally a no-op. */
public class EdgeEffect {
    private final Context mContext;
    private int mWidth;
    private int mHeight;
    private float mDistance;
    private int mColor;

    public EdgeEffect() {
        this(null);
    }

    public EdgeEffect(Context context) {
        mContext = context;
    }

    public EdgeEffect(Context context, AttributeSet attrs) {
        this(context);
    }

    public void setSize(int width, int height) {
        mWidth = width;
        mHeight = height;
    }

    public boolean isFinished() {
        return mDistance <= 0f;
    }

    public void finish() {
        mDistance = 0f;
    }

    public void onPull(float deltaDistance) {
        mDistance = Math.min(1f, mDistance + deltaDistance);
    }

    public void onPull(float deltaDistance, float displacement) {
        onPull(deltaDistance);
    }

    public void onRelease() {
        mDistance = 0f;
    }

    public void onAbsorb(int velocity) {
        mDistance = 0f;
    }

    public int getMaxHeight() {
        return mHeight;
    }

    public boolean draw(Canvas canvas) {
        return false;
    }

    public void setColor(int color) {
        mColor = color;
    }

    public int getColor() {
        return mColor;
    }

    public float getDistance() {
        return mDistance;
    }
}
