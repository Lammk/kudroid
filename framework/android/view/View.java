package android.view;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Rect;

/**
 * Minimal android.view.View implementation.
 *
 * The base class for all UI widgets. For KuDroid's minimal framework, this
 * provides basic layout/draw stubs.
 */
public class View {
    /** View visibility: visible. */
    public static final int VISIBLE = 0;
    /** View visibility: invisible. */
    public static final int INVISIBLE = 4;
    /** View visibility: gone. */
    public static final int GONE = 8;

    /** Measure spec mode: unspecified. */
    public static final int UNSPECIFIED = 0;
    /** Measure spec mode: exactly. */
    public static final int EXACTLY = 1;
    /** Measure spec mode: at most. */
    public static final int AT_MOST = 2;

    protected final Context mContext;
    private int mLeft;
    private int mTop;
    private int mRight;
    private int mBottom;
    private int mVisibility = VISIBLE;
    private int mId = -1;
    private ViewGroup mParent;
    private OnClickListener mOnClickListener;

    /**
     * Interface for click callbacks.
     */
    public interface OnClickListener {
        void onClick(View v);
    }

    public View(Context context) {
        mContext = context;
    }

    /**
     * Return the context this view was created with.
     */
    public Context getContext() {
        return mContext;
    }

    /**
     * Return the view's id.
     */
    public int getId() {
        return mId;
    }

    /**
     * Set the view's id.
     */
    public void setId(int id) {
        mId = id;
    }

    /**
     * Return the view's left position.
     */
    public int getLeft() {
        return mLeft;
    }

    /**
     * Return the view's top position.
     */
    public int getTop() {
        return mTop;
    }

    /**
     * Return the view's right position.
     */
    public int getRight() {
        return mRight;
    }

    /**
     * Return the view's bottom position.
     */
    public int getBottom() {
        return mBottom;
    }

    /**
     * Return the view's width.
     */
    public int getWidth() {
        return mRight - mLeft;
    }

    /**
     * Return the view's height.
     */
    public int getHeight() {
        return mBottom - mTop;
    }

    /**
     * Set the view's layout bounds.
     */
    public void layout(int l, int t, int r, int b) {
        mLeft = l;
        mTop = t;
        mRight = r;
        mBottom = b;
    }

    /**
     * Return the view's visibility.
     */
    public int getVisibility() {
        return mVisibility;
    }

    /**
     * Set the view's visibility.
     */
    public void setVisibility(int visibility) {
        mVisibility = visibility;
    }

    /**
     * Return the view's parent.
     */
    public ViewGroup getParent() {
        return mParent;
    }

    /**
     * Set the view's parent.
     */
    public void setParent(ViewGroup parent) {
        mParent = parent;
    }

    /**
     * Set the click listener.
     */
    public void setOnClickListener(OnClickListener l) {
        mOnClickListener = l;
    }

    /**
     * Perform a click.
     */
    public boolean performClick() {
        if (mOnClickListener != null) {
            mOnClickListener.onClick(this);
            return true;
        }
        return false;
    }

    /**
     * Measure the view.
     */
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
    }

    /**
     * Measure the view (invoked by parent).
     */
    public void measure(int widthMeasureSpec, int heightMeasureSpec) {
        onMeasure(widthMeasureSpec, heightMeasureSpec);
    }

    /**
     * Draw the view.
     */
    protected void onDraw(Canvas canvas) {
    }

    /**
     * Draw the view (invoked by parent).
     */
    public void draw(Canvas canvas) {
        onDraw(canvas);
    }

    /**
     * Return the view's visibility state.
     */
    public boolean isShown() {
        return mVisibility == VISIBLE;
    }

    /**
     * Return the view's background.
     */
    public android.graphics.drawable.Drawable getBackground() {
        return null;
    }

    /**
     * Set the view's background.
     */
    public void setBackgroundColor(int color) {
    }

    /**
     * Set the view's background drawable.
     */
    public void setBackgroundDrawable(android.graphics.drawable.Drawable background) {
    }

    /**
     * Set the view's padding.
     */
    public void setPadding(int left, int top, int right, int bottom) {
    }

    /**
     * Invalidate the view.
     */
    public void invalidate() {
    }

    /**
     * Post a runnable to the UI thread.
     */
    public boolean post(Runnable action) {
        action.run();
        return true;
    }

    /**
     * Return the view's tag.
     */
    public Object getTag() {
        return null;
    }

    /**
     * Set the view's tag.
     */
    public void setTag(Object tag) {
    }
}
