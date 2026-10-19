package android.view;

import android.graphics.Rect;

/**
 * android.view.ViewParent — what a View can ask of whatever contains it.
 *
 * An interface, not a class, because that is what it is on Android and because the
 * distinction is visible to guest code: apps reference
 * {@code View.getParent()Landroid/view/ViewParent;} and assign the result to a
 * ViewParent variable. KuDroid used to declare getParent() as returning ViewGroup, so
 * that reference resolved to nothing — a NoSuchMethodError on a method that looked
 * present, which is the confusing shape of the failure.
 *
 * ViewGroup implements it. Nothing else does, and nothing else needs to: the interface
 * exists so a View can talk upwards without naming a concrete container, and KuDroid has
 * exactly one kind of container.
 */
public interface ViewParent {
    /** The parent of this parent, or null at the root of the hierarchy. */
    ViewParent getParent();

    /** A child wants to be measured and laid out again. */
    void requestLayout();

    /** True once a requestLayout is pending and not yet served. */
    boolean isLayoutRequested();

    /** A child's content changed within the given rectangle. */
    void invalidateChild(View child, Rect r);

    /**
     * Ask ancestors to stop intercepting touch events for the current gesture.
     *
     * Scrolling containers call this on the way down, and a parent that swallows the
     * request silently is why nested scrolling appears to work until the child needs to
     * take over. KuDroid has no interception to disable, so the implementation is empty
     * rather than absent — the call has to be there for the child to make.
     */
    void requestDisallowInterceptTouchEvent(boolean disallowIntercept);

    /** Focus is moving to `child`; ancestors may need to scroll it into view. */
    void requestChildFocus(View child, View focused);

    /** `child` no longer holds focus. */
    void clearChildFocus(View child);

    /** The view that currently holds focus in this subtree, or null. */
    View focusSearch(View v, int direction);

    /** A child asks for the given rectangle (in its coordinates) to be brought on screen. */
    boolean requestChildRectangleOnScreen(View child, Rect rectangle, boolean immediate);

    /** A child's transient state (an animation in flight, say) changed. */
    void childHasTransientStateChanged(View child, boolean hasTransientState);

    /** A child's drawable state changed. */
    void childDrawableStateChanged(View child);

    /** Brings a child to the front of the drawing order. */
    void bringChildToFront(View child);

    /** Computes the part of a child's rectangle that is visible in this parent's ancestry. */
    boolean getChildVisibleRect(View child, Rect r, android.graphics.Point offset);

    void requestTransparentRegion(View child);

    void recomputeViewAttributes(View child);

    boolean showContextMenuForChild(View originalView);

    android.view.ActionMode startActionModeForChild(View originalView,
            android.view.ActionMode.Callback callback, int type);

    void notifySubtreeAccessibilityStateChanged(View child, View source, int changeType);

    /** Nested scrolling: a descendant starts a scroll this parent may take part in. */
    boolean onStartNestedScroll(View child, View target, int nestedScrollAxes);

    void onNestedScrollAccepted(View child, View target, int nestedScrollAxes);

    void onStopNestedScroll(View target);

    void onNestedScroll(View target, int dxConsumed, int dyConsumed, int dxUnconsumed,
            int dyUnconsumed);

    void onNestedPreScroll(View target, int dx, int dy, int[] consumed);

    boolean onNestedFling(View target, float velocityX, float velocityY, boolean consumed);

    boolean onNestedPreFling(View target, float velocityX, float velocityY);

    boolean onNestedPrePerformAccessibilityAction(View target, int action, android.os.Bundle arguments);
}
