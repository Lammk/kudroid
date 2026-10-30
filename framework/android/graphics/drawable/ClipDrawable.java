package android.graphics.drawable;

import android.graphics.Canvas;

/** Shows a child clipped by level; without a clipping canvas it is shown whole at level > 0. */
public class ClipDrawable extends DrawableWrapper {
    public static final int HORIZONTAL = 1;
    public static final int VERTICAL = 2;

    private final int mGravity;
    private final int mOrientation;

    public ClipDrawable(Drawable drawable, int gravity, int orientation) {
        super(drawable);
        mGravity = gravity;
        mOrientation = orientation;
    }

    @Override
    public void draw(Canvas canvas) {
        if (getLevel() > 0 || getDrawable() == null) super.draw(canvas);
    }

    @Override
    protected boolean onLevelChange(int level) {
        super.onLevelChange(level);
        invalidateSelf();
        return true;
    }
}
