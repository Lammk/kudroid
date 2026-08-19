package android.view;

import android.content.Context;

/**
 * triển khai android.view.viewgroup tối thiểu.
 *
 * một view chứa các view khác. đối với khuôn khổ tối thiểu của kudroid, điều này
 * cung cấp quản lý con cơ bản.
 */
public abstract class ViewGroup extends View {
    private View[] mChildren;
    private int mChildCount = 0;

    public ViewGroup(Context context) {
        super(context);
        mChildren = new View[16];
    }

    /**
     * thêm một view con.
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
     * thêm một view con với các thông số bố cục.
     */
    public void addView(View child, LayoutParams params) {
        addView(child);
    }

    /**
     * xóa một view con.
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
     * xóa tất cả các view con.
     */
    public void removeAllViews() {
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].setParent(null);
        }
        mChildCount = 0;
    }

    /**
     * trả về số lượng các view con.
     */
    public int getChildCount() {
        return mChildCount;
    }

    /**
     * trả về view con ở chỉ mục đã cho.
     */
    public View getChildAt(int index) {
        if (index < 0 || index >= mChildCount) return null;
        return mChildren[index];
    }

    /**
     * tìm một view con theo id.
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
     * bố cục các con. các lớp con phải triển khai điều này.
     */
    protected abstract void onLayout(boolean changed, int l, int t, int r, int b);

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
     * thông số bố cục cho một view.
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
     * thông số bố cục lề.
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
