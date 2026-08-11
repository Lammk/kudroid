package android.view;

import android.content.Context;

/**
 * Minimal android.view.ViewGroup implementation.
 *
 * A view that contains other views. For KuDroid's minimal framework, this
 * provides basic child management.
 */
public abstract class ViewGroup extends View {
    private View[] mChildren;
    private int mChildCount = 0;

    public ViewGroup(Context context) {
        super(context);
        mChildren = new View[16];
    }

    /**
     * Add a child view.
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
     * Add a child view with layout params.
     */
    public void addView(View child, LayoutParams params) {
        addView(child);
    }

    /**
     * Remove a child view.
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
     * Remove all child views.
     */
    public void removeAllViews() {
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].setParent(null);
        }
        mChildCount = 0;
    }

    /**
     * Return the number of child views.
     */
    public int getChildCount() {
        return mChildCount;
    }

    /**
     * Return the child view at the given index.
     */
    public View getChildAt(int index) {
        if (index < 0 || index >= mChildCount) return null;
        return mChildren[index];
    }

    /**
     * Find a child view by id.
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
     * Layout the children. Subclasses must implement this.
     */
    protected abstract void onLayout(boolean changed, int l, int t, int r, int b);

    /**
     * Layout params for a view.
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
     * Margin layout params.
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
}
