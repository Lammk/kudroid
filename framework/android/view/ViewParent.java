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
}
