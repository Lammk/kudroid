package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.Animation;

/**
 * Base container that shows one child at a time. Animations are accepted but the
 * switch itself is immediate: the guest only relies on which child is visible.
 */
public class ViewAnimator extends FrameLayout {

    private int mWhichChild = 0;
    private boolean mFirstTime = true;
    private Animation mInAnimation;
    private Animation mOutAnimation;
    private boolean mAnimateFirstTime = true;

    public ViewAnimator(Context context) {
        super(context);
    }

    public ViewAnimator(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public ViewAnimator(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public ViewAnimator(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }

    public void setDisplayedChild(int whichChild) {
        final int count = getChildCount();
        if (count == 0) {
            mWhichChild = 0;
            return;
        }
        int index = whichChild % count;
        if (index < 0) index += count;
        mWhichChild = index;
        mFirstTime = false;
        showOnly(mWhichChild);
    }

    public int getDisplayedChild() {
        return mWhichChild;
    }

    public void showNext() {
        setDisplayedChild(mWhichChild + 1);
    }

    public void showPrevious() {
        setDisplayedChild(mWhichChild - 1);
    }

    public View getCurrentView() {
        if (getChildCount() == 0) return null;
        return getChildAt(mWhichChild);
    }

    public void setInAnimation(Animation inAnimation) {
        mInAnimation = inAnimation;
    }

    public Animation getInAnimation() {
        return mInAnimation;
    }

    public void setOutAnimation(Animation outAnimation) {
        mOutAnimation = outAnimation;
    }

    public Animation getOutAnimation() {
        return mOutAnimation;
    }

    public void setAnimateFirstView(boolean animate) {
        mAnimateFirstTime = animate;
    }

    public boolean getAnimateFirstView() {
        return mAnimateFirstTime;
    }

    @Override
    public void addView(View child, int index, ViewGroup.LayoutParams params) {
        super.addView(child, index, params);
        showOnly(mWhichChild);
    }

    private void showOnly(int childIndex) {
        final int count = getChildCount();
        if (count == 0) return;
        if (childIndex < 0 || childIndex >= count) childIndex = 0;
        for (int i = 0; i < count; i++) {
            final View child = getChildAt(i);
            if (child != null) {
                child.setVisibility(i == childIndex ? VISIBLE : GONE);
            }
        }
    }
}
