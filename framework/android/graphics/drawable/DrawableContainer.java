package android.graphics.drawable;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Rect;

import java.util.ArrayList;

/** Holds several drawables and shows one of them, chosen by subclasses. */
public class DrawableContainer extends Drawable implements Drawable.Callback {
    private DrawableContainerState mState;
    private Drawable mCurrDrawable;
    private int mCurIndex = -1;
    private int mAlpha = 0xFF;

    public DrawableContainer() {
        mState = new DrawableContainerState();
    }

    protected void setConstantState(DrawableContainerState state) {
        mState = state;
        if (mCurIndex >= 0) {
            final int index = mCurIndex;
            mCurIndex = -1;
            selectDrawable(index);
        }
    }

    protected DrawableContainerState cloneConstantState() {
        return mState;
    }

    public boolean selectDrawable(int index) {
        if (index == mCurIndex) return false;
        if (index >= 0 && index < mState.getChildCount()) {
            final Drawable d = mState.getChild(index);
            if (mCurrDrawable != null) mCurrDrawable.setVisible(false, false);
            mCurrDrawable = d;
            mCurIndex = index;
            if (d != null) {
                d.setCallback(this);
                d.setVisible(isVisible(), true);
                d.setState(getState());
                d.setLevel(getLevel());
                d.setBounds(getBounds());
                d.setAlpha(mAlpha);
            }
        } else {
            mCurrDrawable = null;
            mCurIndex = -1;
        }
        invalidateSelf();
        return true;
    }

    public Drawable getCurrent() {
        return mCurrDrawable;
    }

    public int getCurrentIndex() {
        return mCurIndex;
    }

    @Override
    public void draw(Canvas canvas) {
        if (mCurrDrawable != null) mCurrDrawable.draw(canvas);
    }

    @Override
    protected void onBoundsChange(Rect bounds) {
        if (mCurrDrawable != null) mCurrDrawable.setBounds(bounds);
    }

    @Override
    public boolean isStateful() {
        return mState.isStateful();
    }

    @Override
    protected boolean onStateChange(int[] state) {
        return mCurrDrawable != null && mCurrDrawable.setState(state);
    }

    @Override
    protected boolean onLevelChange(int level) {
        return mCurrDrawable != null && mCurrDrawable.setLevel(level);
    }

    @Override
    public void setAlpha(int alpha) {
        mAlpha = alpha;
        if (mCurrDrawable != null) mCurrDrawable.setAlpha(alpha);
    }

    @Override
    public int getAlpha() {
        return mAlpha;
    }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
        if (mCurrDrawable != null) mCurrDrawable.setColorFilter(colorFilter);
    }

    @Override
    public void setTintList(android.content.res.ColorStateList tint) {
        for (int i = 0; i < mState.getChildCount(); i++) {
            final Drawable d = mState.getChild(i);
            if (d != null) d.setTintList(tint);
        }
    }

    @Override
    public void setTintMode(android.graphics.PorterDuff.Mode tintMode) {
        for (int i = 0; i < mState.getChildCount(); i++) {
            final Drawable d = mState.getChild(i);
            if (d != null) d.setTintMode(tintMode);
        }
    }

    @Override
    public int getOpacity() {
        return mCurrDrawable != null ? mCurrDrawable.getOpacity()
                : android.graphics.PixelFormat.TRANSPARENT;
    }

    @Override
    public boolean setVisible(boolean visible, boolean restart) {
        final boolean changed = super.setVisible(visible, restart);
        if (mCurrDrawable != null) mCurrDrawable.setVisible(visible, restart);
        return changed;
    }

    @Override
    public int getIntrinsicWidth() {
        return mCurrDrawable != null ? mCurrDrawable.getIntrinsicWidth() : -1;
    }

    @Override
    public int getIntrinsicHeight() {
        return mCurrDrawable != null ? mCurrDrawable.getIntrinsicHeight() : -1;
    }

    @Override
    public int getMinimumWidth() {
        return mCurrDrawable != null ? mCurrDrawable.getMinimumWidth() : 0;
    }

    @Override
    public int getMinimumHeight() {
        return mCurrDrawable != null ? mCurrDrawable.getMinimumHeight() : 0;
    }

    @Override
    public boolean getPadding(Rect padding) {
        return mCurrDrawable != null && mCurrDrawable.getPadding(padding);
    }

    @Override
    public void invalidateDrawable(Drawable who) {
        if (who == mCurrDrawable) invalidateSelf();
    }

    @Override
    public void scheduleDrawable(Drawable who, Runnable what, long when) {
        if (who == mCurrDrawable) scheduleSelf(what, when);
    }

    @Override
    public void unscheduleDrawable(Drawable who, Runnable what) {
        if (who == mCurrDrawable) unscheduleSelf(what);
    }

    @Override
    public ConstantState getConstantState() {
        return mState;
    }

    /** The children and the facts derived from them. */
    public static class DrawableContainerState extends ConstantState {
        private final ArrayList<Drawable> mDrawables = new ArrayList<>();

        public DrawableContainerState() {}

        public final int addChild(Drawable dr) {
            mDrawables.add(dr);
            return mDrawables.size() - 1;
        }

        public final int getChildCount() {
            return mDrawables.size();
        }

        public final Drawable[] getChildren() {
            return mDrawables.toArray(new Drawable[0]);
        }

        public final Drawable getChild(int index) {
            return index >= 0 && index < mDrawables.size() ? mDrawables.get(index) : null;
        }

        public boolean isStateful() {
            for (int i = 0; i < mDrawables.size(); i++) {
                final Drawable d = mDrawables.get(i);
                if (d != null && d.isStateful()) return true;
            }
            return false;
        }

        @Override
        public Drawable newDrawable() {
            final DrawableContainer container = new DrawableContainer();
            container.setConstantState(this);
            return container;
        }
    }
}
