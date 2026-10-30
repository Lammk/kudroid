package android.widget;

import android.content.Context;
import android.view.MotionEvent;
import android.view.VelocityTracker;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.ViewGroup;

/**
 * Scrolls a single child wider than itself. The child is measured with an unbounded width;
 * dragging scrolls it, and a quick release flings it.
 */
public class HorizontalScrollView extends FrameLayout {
    private OverScroller mScroller;
    private boolean mFillViewport;
    private boolean mSmoothScrollingEnabled = true;
    private boolean mIsBeingDragged;
    private float mLastMotionX;
    private int mActivePointerId = -1;
    private VelocityTracker mVelocityTracker;
    private int mTouchSlop;
    private int mMinimumVelocity;
    private int mMaximumVelocity;

    public HorizontalScrollView(Context context) { this(context, null); }
    public HorizontalScrollView(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public HorizontalScrollView(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public HorizontalScrollView(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        mScroller = new OverScroller(getContext());
        final ViewConfiguration configuration = ViewConfiguration.get(getContext());
        mTouchSlop = configuration.getScaledTouchSlop();
        mMinimumVelocity = configuration.getScaledMinimumFlingVelocity();
        mMaximumVelocity = configuration.getScaledMaximumFlingVelocity();
    }

    public void setFillViewport(boolean fillViewport) {
        if (fillViewport != mFillViewport) {
            mFillViewport = fillViewport;
            requestLayout();
        }
    }

    public boolean isFillViewport() { return mFillViewport; }

    public boolean isSmoothScrollingEnabled() { return mSmoothScrollingEnabled; }

    public void setSmoothScrollingEnabled(boolean smoothScrollingEnabled) {
        mSmoothScrollingEnabled = smoothScrollingEnabled;
    }

    private int getScrollRange() {
        if (getChildCount() == 0) return 0;
        final View child = getChildAt(0);
        final ViewGroup.MarginLayoutParams lp = (ViewGroup.MarginLayoutParams) child.getLayoutParams();
        final int content = child.getWidth() + lp.leftMargin + lp.rightMargin;
        return Math.max(0, content - (getWidth() - getPaddingLeft() - getPaddingRight()));
    }

    @Override
    protected void measureChildWithMargins(View child, int parentWidthMeasureSpec, int widthUsed,
            int parentHeightMeasureSpec, int heightUsed) {
        final ViewGroup.MarginLayoutParams lp = (ViewGroup.MarginLayoutParams) child.getLayoutParams();
        final int childHeightMeasureSpec = getChildMeasureSpec(parentHeightMeasureSpec,
                getPaddingTop() + getPaddingBottom() + lp.topMargin + lp.bottomMargin + heightUsed, lp.height);
        final int childWidthMeasureSpec = MeasureSpec.makeMeasureSpec(
                Math.max(0, MeasureSpec.getSize(parentWidthMeasureSpec)
                        - (getPaddingLeft() + getPaddingRight() + lp.leftMargin + lp.rightMargin + widthUsed)),
                MeasureSpec.UNSPECIFIED);
        child.measure(childWidthMeasureSpec, childHeightMeasureSpec);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        super.onMeasure(widthMeasureSpec, heightMeasureSpec);
        if (!mFillViewport) return;
        if (MeasureSpec.getMode(widthMeasureSpec) == MeasureSpec.UNSPECIFIED) return;
        if (getChildCount() > 0) {
            final View child = getChildAt(0);
            final ViewGroup.MarginLayoutParams lp = (ViewGroup.MarginLayoutParams) child.getLayoutParams();
            final int width = getMeasuredWidth() - getPaddingLeft() - getPaddingRight()
                    - lp.leftMargin - lp.rightMargin;
            if (child.getMeasuredWidth() < width) {
                final int childHeightMeasureSpec = getChildMeasureSpec(heightMeasureSpec,
                        getPaddingTop() + getPaddingBottom() + lp.topMargin + lp.bottomMargin, lp.height);
                child.measure(MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY), childHeightMeasureSpec);
            }
        }
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        super.onLayout(changed, l, t, r, b);
        scrollTo(getScrollX(), getScrollY());
    }

    @Override
    public void scrollTo(int x, int y) {
        if (getChildCount() > 0) {
            final View child = getChildAt(0);
            x = clamp(x, getWidth() - getPaddingRight() - getPaddingLeft(), child.getWidth());
            y = clamp(y, getHeight() - getPaddingBottom() - getPaddingTop(), child.getHeight());
            if (x != getScrollX() || y != getScrollY()) {
                super.scrollTo(x, y);
            }
        }
    }

    private static int clamp(int n, int my, int child) {
        if (my >= child || n < 0) return 0;
        if (my + n > child) return child - my;
        return n;
    }

    @Override
    protected int computeHorizontalScrollRange() {
        final int contentWidth = getWidth() - getPaddingLeft() - getPaddingRight();
        if (getChildCount() == 0) return contentWidth;
        return Math.max(getChildAt(0).getRight(), contentWidth);
    }

    @Override
    protected int computeHorizontalScrollOffset() {
        return Math.max(0, getScrollX());
    }

    @Override
    protected int computeHorizontalScrollExtent() {
        return getWidth() - getPaddingLeft() - getPaddingRight();
    }

    @Override
    public boolean canScrollHorizontally(int direction) {
        final int offset = getScrollX();
        final int range = getScrollRange();
        if (direction < 0) return offset > 0;
        return offset < range;
    }

    @Override
    public void computeScroll() {
        if (mScroller.computeScrollOffset()) {
            scrollTo(mScroller.getCurrX(), getScrollY());
            if (!mScroller.isFinished()) postInvalidateOnAnimation();
        }
    }

    public void fling(int velocityX) {
        if (getChildCount() > 0) {
            final int width = getWidth() - getPaddingRight() - getPaddingLeft();
            final int right = getChildAt(0).getWidth();
            mScroller.fling(getScrollX(), getScrollY(), velocityX, 0, 0, Math.max(0, right - width), 0, 0, 0, 0);
            postInvalidateOnAnimation();
        }
    }

    public final void smoothScrollBy(int dx, int dy) {
        if (getChildCount() == 0) return;
        if (!mSmoothScrollingEnabled) {
            scrollBy(dx, dy);
            return;
        }
        final int maxX = getScrollRange();
        final int scrollX = getScrollX();
        final int target = Math.max(0, Math.min(scrollX + dx, maxX));
        mScroller.startScroll(scrollX, getScrollY(), target - scrollX, 0);
        postInvalidateOnAnimation();
    }

    public final void smoothScrollTo(int x, int y) {
        smoothScrollBy(x - getScrollX(), y - getScrollY());
    }

    public boolean fullScroll(int direction) {
        final boolean right = direction == View.FOCUS_RIGHT;
        final int target = right ? getScrollRange() : 0;
        if (target == getScrollX()) return false;
        smoothScrollTo(target, 0);
        return true;
    }

    public boolean pageScroll(int direction) {
        final boolean right = direction == View.FOCUS_RIGHT;
        final int width = getWidth();
        final int target = right ? getScrollX() + width : getScrollX() - width;
        final int clamped = Math.max(0, Math.min(target, getScrollRange()));
        if (clamped == getScrollX()) return false;
        smoothScrollTo(clamped, 0);
        return true;
    }

    public boolean arrowScroll(int direction) {
        final int delta = (direction == View.FOCUS_RIGHT ? 1 : -1) * getMaxScrollAmount();
        if (delta == 0) return false;
        smoothScrollBy(delta, 0);
        return true;
    }

    public int getMaxScrollAmount() {
        return (int) (0.5f * (getRight() - getLeft()));
    }

    // ---- Touch.

    private void initVelocityTrackerIfNotExists() {
        if (mVelocityTracker == null) mVelocityTracker = VelocityTracker.obtain();
    }

    private void recycleVelocityTracker() {
        if (mVelocityTracker != null) {
            mVelocityTracker.recycle();
            mVelocityTracker = null;
        }
    }

    @Override
    public boolean onInterceptTouchEvent(MotionEvent ev) {
        final int action = ev.getAction() & MotionEvent.ACTION_MASK;
        if (action == MotionEvent.ACTION_MOVE && mIsBeingDragged) return true;
        switch (action) {
            case MotionEvent.ACTION_DOWN:
                mLastMotionX = ev.getX();
                mActivePointerId = ev.getPointerId(0);
                initVelocityTrackerIfNotExists();
                mVelocityTracker.addMovement(ev);
                mIsBeingDragged = !mScroller.isFinished();
                break;
            case MotionEvent.ACTION_MOVE: {
                if (mActivePointerId == -1) break;
                final int index = ev.findPointerIndex(mActivePointerId);
                if (index < 0) break;
                final float x = ev.getX(index);
                if (Math.abs(x - mLastMotionX) > mTouchSlop && getScrollRange() > 0) {
                    mIsBeingDragged = true;
                    mLastMotionX = x;
                    initVelocityTrackerIfNotExists();
                    mVelocityTracker.addMovement(ev);
                    final android.view.ViewParent p = getParent();
                    if (p != null) p.requestDisallowInterceptTouchEvent(true);
                }
                break;
            }
            case MotionEvent.ACTION_CANCEL:
            case MotionEvent.ACTION_UP:
                mIsBeingDragged = false;
                mActivePointerId = -1;
                recycleVelocityTracker();
                break;
        }
        return mIsBeingDragged;
    }

    @Override
    public boolean onTouchEvent(MotionEvent ev) {
        initVelocityTrackerIfNotExists();
        mVelocityTracker.addMovement(ev);
        final int action = ev.getAction() & MotionEvent.ACTION_MASK;
        switch (action) {
            case MotionEvent.ACTION_DOWN:
                if (getChildCount() == 0) return false;
                if (!mScroller.isFinished()) mScroller.abortAnimation();
                mLastMotionX = ev.getX();
                mActivePointerId = ev.getPointerId(0);
                return true;
            case MotionEvent.ACTION_MOVE: {
                final int index = ev.findPointerIndex(mActivePointerId);
                if (index < 0) return true;
                final float x = ev.getX(index);
                int deltaX = (int) (mLastMotionX - x);
                if (!mIsBeingDragged && Math.abs(deltaX) > mTouchSlop) {
                    mIsBeingDragged = true;
                    deltaX = deltaX > 0 ? deltaX - mTouchSlop : deltaX + mTouchSlop;
                }
                if (mIsBeingDragged) {
                    mLastMotionX = x;
                    scrollTo(getScrollX() + deltaX, getScrollY());
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
                if (mIsBeingDragged) {
                    mVelocityTracker.computeCurrentVelocity(1000, mMaximumVelocity);
                    final int initialVelocity = (int) mVelocityTracker.getXVelocity();
                    if (Math.abs(initialVelocity) > mMinimumVelocity) fling(-initialVelocity);
                }
                mActivePointerId = -1;
                mIsBeingDragged = false;
                recycleVelocityTracker();
                return true;
            case MotionEvent.ACTION_CANCEL:
                mActivePointerId = -1;
                mIsBeingDragged = false;
                recycleVelocityTracker();
                return true;
        }
        return true;
    }
}
