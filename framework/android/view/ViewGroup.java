package android.view;

import android.content.Context;

/**
 * minimal android.view.viewgroup implementation.
 *
 * a view contains other views. for kudroid minimal framework, this
 * Provides basic child management.
 */
public abstract class ViewGroup extends View {
    private View[] mChildren;
    private int mChildCount = 0;

    public ViewGroup(Context context) {
        super(context);
        mChildren = new View[16];
    }

    /**
     * add a child view.
     */
    public void addView(View child) {
        if (mChildCount >= mChildren.length) {
            View[] bigger = new View[mChildren.length * 2];
            System.arraycopy(mChildren, 0, bigger, 0, mChildren.length);
            mChildren = bigger;
        }
        mChildren[mChildCount++] = child;
        child.setParent(this);
    }

    /**
     * add a child view with layout parameters.
     */
    public void addView(View child, LayoutParams params) {
        addView(child);
    }

    /**
     * delete a child view.
     */
    public void removeView(View view) {
        for (int i = 0; i < mChildCount; i++) {
            if (mChildren[i] == view) {
                System.arraycopy(mChildren, i + 1, mChildren, i, mChildCount - i - 1);
                mChildCount--;
                view.setParent(null);
                return;
            }
        }
    }

    /**
     * delete all child views.
     */
    public void removeAllViews() {
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].setParent(null);
        }
        mChildCount = 0;
    }

    /**
     * returns the number of child views.
     */
    public int getChildCount() {
        return mChildCount;
    }

    /**
     * returns the child view at the given index.
     */
    public View getChildAt(int index) {
        if (index < 0 || index >= mChildCount) return null;
        return mChildren[index];
    }

    /**
     * find a child view by id.
     */
    public View findViewById(int id) {
        if (getId() == id) return this;
        for (int i = 0; i < mChildCount; i++) {
            View child = mChildren[i];
            if (child instanceof ViewGroup) {
                View found = ((ViewGroup) child).findViewById(id);
                if (found != null) return found;
            } else if (child.getId() == id) {
                return child;
            }
        }
        return null;
    }

    /**
     * layout of children. subclasses must implement this.
     */
    protected abstract void onLayout(boolean changed, int l, int t, int r, int b);

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (!isShown() || event == null) return false;
        float x = event.getX();
        float y = event.getY();
        for (int i = mChildCount - 1; i >= 0; i--) {
            View child = mChildren[i];
            if (child != null && child.isShown()) {
                if (x >= child.getLeft() && x <= child.getRight() &&
                    y >= child.getTop() && y <= child.getBottom()) {
                    if (child.dispatchTouchEvent(event)) {
                        return true;
                    }
                }
            }
        }
        return onTouchEvent(event);
    }

    @Override
    public void draw(android.graphics.Canvas canvas) {
        super.draw(canvas);
        dispatchDraw(canvas);
    }

    protected void dispatchDraw(android.graphics.Canvas canvas) {
        for (int i = 0; i < mChildCount; i++) {
            View child = mChildren[i];
            if (child != null && child.isShown()) {
                child.draw(canvas);
            }
        }
    }

    /**
     * layout parameters for a view.
     */
    public static class LayoutParams {
        public int width;
        public int height;

        public LayoutParams(int width, int height) {
            this.width = width;
            this.height = height;
        }
    }

    /**
     * margin layout parameters.
     */
    public static class MarginLayoutParams extends LayoutParams {
        public int leftMargin;
        public int topMargin;
        public int rightMargin;
        public int bottomMargin;

        public MarginLayoutParams(int width, int height) {
            super(width, height);
        }
    }

    public interface OnHierarchyChangeListener {
    }

}
