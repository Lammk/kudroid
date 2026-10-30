package android.widget;

import android.content.Context;
import android.view.MotionEvent;
import android.view.VelocityTracker;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.ViewGroup;

/**
 * Scrolls a single child taller than itself. The child is measured with an unbounded height;
 * dragging scrolls it, and a quick release flings it.
 */
public class ScrollView extends FrameLayout {
    private OverScroller mScroller;
    private boolean mFillViewport;
    private boolean mSmoothScrollingEnabled = true;
    private boolean mIsBeingDragged;
    private float mLastMotionY;
    private int mActivePointerId = -1;
    private VelocityTracker mVelocityTracker;
    private int mTouchSlop;
    private int mMinimumVelocity;
    private int mMaximumVelocity;

    public ScrollView(Context context) { this(context, null); }
    public ScrollView(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public ScrollView(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public ScrollView(Context context, android.util.AttributeSet attrs, int defStyleAttr,
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
        final int content = child.getHeight() + lp.topMargin + lp.bottomMargin;
        return Math.max(0, content - (getHeight() - getPaddingTop() - getPaddingBottom()));
    }

    @Override
    protected void measureChildWithMargins(View child, int parentWidthMeasureSpec, int widthUsed,
            int parentHeightMeasureSpec, int heightUsed) {
        final ViewGroup.MarginLayoutParams lp = (ViewGroup.MarginLayoutParams) child.getLayoutParams();
        final int childWidthMeasureSpec = getChildMeasureSpec(parentWidthMeasureSpec,
                getPaddingLeft() + getPaddingRight() + lp.leftMargin + lp.rightMargin + widthUsed, lp.width);
        final int childHeightMeasureSpec = MeasureSpec.makeMeasureSpec(
                Math.max(0, MeasureSpec.getSize(parentHeightMeasureSpec)
                        - (getPaddingTop() + getPaddingBottom() + lp.topMargin + lp.bottomMargin + heightUsed)),
                MeasureSpec.UNSPECIFIED);
        child.measure(childWidthMeasureSpec, childHeightMeasureSpec);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        super.onMeasure(widthMeasureSpec, heightMeasureSpec);
        if (!mFillViewport) return;
        if (MeasureSpec.getMode(heightMeasureSpec) == MeasureSpec.UNSPECIFIED) return;
        if (getChildCount() > 0) {
            final View child = getChildAt(0);
            final ViewGroup.MarginLayoutParams lp = (ViewGroup.MarginLayoutParams) child.getLayoutParams();
            final int height = getMeasuredHeight() - getPaddingTop() - getPaddingBottom()
                    - lp.topMargin - lp.bottomMargin;
            if (child.getMeasuredHeight() < height) {
                final int childWidthMeasureSpec = getChildMeasureSpec(widthMeasureSpec,
                        getPaddingLeft() + getPaddingRight() + lp.leftMargin + lp.rightMargin, lp.width);
                child.measure(childWidthMeasureSpec, MeasureSpec.makeMeasureSpec(height, MeasureSpec.EXACTLY));
            }
        }
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        super.onLayout(changed, l, t, r, b);
        // The range may have shrunk; keep the offset inside it.
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
    protected int computeVerticalScrollRange() {
        final int count = getChildCount();
        final int contentHeight = getHeight() - getPaddingBottom() - getPaddingTop();
        if (count == 0) return contentHeight;
        return Math.max(getChildAt(0).getBottom(), contentHeight);
    }

    @Override
    protected int computeVerticalScrollOffset() {
        return Math.max(0, super.computeVerticalScrollOffset());
    }

    @Override
    protected int computeVerticalScrollExtent() {
        return getHeight() - getPaddingTop() - getPaddingBottom();
    }

    @Override
    public boolean canScrollVertically(int direction) {
        final int offset = getScrollY();
        final int range = getScrollRange();
        if (direction < 0) return offset > 0;
        return offset < range;
    }

    @Override
    public void computeScroll() {
        if (mScroller.computeScrollOffset()) {
            scrollTo(getScrollX(), mScroller.getCurrY());
            if (!mScroller.isFinished()) postInvalidateOnAnimation();
        }
    }

    public void fling(int velocityY) {
        if (getChildCount() > 0) {
            final int height = getHeight() - getPaddingBottom() - getPaddingTop();
            final int bottom = getChildAt(0).getHeight();
            mScroller.fling(getScrollX(), getScrollY(), 0, velocityY, 0, 0, 0,
                    Math.max(0, bottom - height), 0, 0);
            postInvalidateOnAnimation();
        }
    }

    public final void smoothScrollBy(int dx, int dy) {
        if (getChildCount() == 0) return;
        if (!mSmoothScrollingEnabled) {
            scrollBy(dx, dy);
            return;
        }
        final int maxY = getScrollRange();
        final int scrollY = getScrollY();
        final int target = Math.max(0, Math.min(scrollY + dy, maxY));
        mScroller.startScroll(getScrollX(), scrollY, 0, target - scrollY);
        postInvalidateOnAnimation();
    }

    public final void smoothScrollTo(int x, int y) {
        smoothScrollBy(x - getScrollX(), y - getScrollY());
    }

    public boolean fullScroll(int direction) {
        final boolean down = direction == View.FOCUS_DOWN;
        final int target = down ? getScrollRange() : 0;
        if (target == getScrollY()) return false;
        smoothScrollTo(0, target);
        return true;
    }

    public boolean pageScroll(int direction) {
        final boolean down = direction == View.FOCUS_DOWN;
        final int height = getHeight();
        final int target = down ? getScrollY() + height : getScrollY() - height;
        final int clamped = Math.max(0, Math.min(target, getScrollRange()));
        if (clamped == getScrollY()) return false;
        smoothScrollTo(0, clamped);
        return true;
    }

    public boolean arrowScroll(int direction) {
        final int delta = (direction == View.FOCUS_DOWN ? 1 : -1) * getMaxScrollAmount();
        if (delta == 0) return false;
        smoothScrollBy(0, delta);
        return true;
    }

    public int getMaxScrollAmount() {
        return (int) (0.5f * (getBottom() - getTop()));
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
                mLastMotionY = ev.getY();
                mActivePointerId = ev.getPointerId(0);
                initVelocityTrackerIfNotExists();
                mVelocityTracker.addMovement(ev);
                mIsBeingDragged = !mScroller.isFinished();
                break;
            case MotionEvent.ACTION_MOVE: {
                if (mActivePointerId == -1) break;
                final int index = ev.findPointerIndex(mActivePointerId);
                if (index < 0) break;
                final float y = ev.getY(index);
                if (Math.abs(y - mLastMotionY) > mTouchSlop && getScrollRange() > 0) {
                    mIsBeingDragged = true;
                    mLastMotionY = y;
                    initVelocityTrackerIfNotExists();
                    mVelocityTracker.addMovement(ev);
                    final ViewGroup parent = getParentGroupForTouch();
                    if (parent != null) parent.requestDisallowInterceptTouchEvent(true);
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

    private ViewGroup getParentGroupForTouch() {
        final android.view.ViewParent p = getParent();
        return p instanceof ViewGroup ? (ViewGroup) p : null;
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
                mLastMotionY = ev.getY();
                mActivePointerId = ev.getPointerId(0);
                return true;
            case MotionEvent.ACTION_MOVE: {
                final int index = ev.findPointerIndex(mActivePointerId);
                if (index < 0) return true;
                final float y = ev.getY(index);
                int deltaY = (int) (mLastMotionY - y);
                if (!mIsBeingDragged && Math.abs(deltaY) > mTouchSlop) {
                    mIsBeingDragged = true;
                    deltaY = deltaY > 0 ? deltaY - mTouchSlop : deltaY + mTouchSlop;
                }
                if (mIsBeingDragged) {
                    mLastMotionY = y;
                    scrollTo(getScrollX(), getScrollY() + deltaY);
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
                if (mIsBeingDragged) {
                    mVelocityTracker.computeCurrentVelocity(1000, mMaximumVelocity);
                    final int initialVelocity = (int) mVelocityTracker.getYVelocity();
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
