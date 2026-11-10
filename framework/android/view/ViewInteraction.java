package android.view;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.View.*;
import static android.view.View.*;

final class ViewInteraction {
    private ViewInteraction() {}

    static int[] getDrawableState(View hostView) {
        if (hostView.mDrawableState != null) {
            return hostView.mDrawableState;
        }
        hostView.mDrawableState = hostView.onCreateDrawableState(0);
        return hostView.mDrawableState;
    }

    static int[] onCreateDrawableState(View hostView, int extraSpace) {
        if (hostView.mDuplicateParentState && hostView.mParent instanceof View) {
            return ((View) hostView.mParent).onCreateDrawableState(extraSpace);
        }
        final int[] states = new int[8 + extraSpace];
        int n = 0;
        if (hostView.hasWindowFocus()) states[n++] = com.android.internal.R.attr.state_window_focused;
        if (hostView.mSelected) states[n++] = com.android.internal.R.attr.state_selected;
        if (hostView.mHasFocus) states[n++] = com.android.internal.R.attr.state_focused;
        if (hostView.mEnabled) states[n++] = com.android.internal.R.attr.state_enabled;
        if (hostView.mPressed) states[n++] = com.android.internal.R.attr.state_pressed;
        if (hostView.mActivated) states[n++] = com.android.internal.R.attr.state_activated;
        if (hostView.mHovered) states[n++] = com.android.internal.R.attr.state_hovered;
        final int[] result = new int[n + extraSpace];
        System.arraycopy(states, 0, result, 0, n);
        return result;
    }

    static int[] mergeDrawableStates(int[] baseState, int[] additionalState) {
        int i = baseState.length - 1;
        while (i >= 0 && baseState[i] == 0) {
            i--;
        }
        System.arraycopy(additionalState, 0, baseState, i + 1, additionalState.length);
        return baseState;
    }

    static void refreshDrawableState(View hostView) {
        hostView.mDrawableState = null;
        hostView.drawableStateChanged();
        if (hostView.mParent != null) {
            hostView.mParent.childDrawableStateChanged(hostView);
        }
    }

    static void drawableStateChanged(View hostView) {
        final int[] state = hostView.getDrawableState();
        boolean changed = false;
        if (hostView.mBackground != null && hostView.mBackground.isStateful()) {
            changed |= hostView.mBackground.setState(state);
        }
        if (hostView.mForeground != null && hostView.mForeground.isStateful()) {
            changed |= hostView.mForeground.setState(state);
        }
        if (hostView.mStateListAnimator != null) {
            hostView.mStateListAnimator.setState(state);
        }
        if (changed) {
            hostView.invalidate();
        }
    }

    static void jumpDrawablesToCurrentState(View hostView) {
        if (hostView.mBackground != null) hostView.mBackground.jumpToCurrentState();
        if (hostView.mStateListAnimator != null) hostView.mStateListAnimator.jumpToCurrentState();
        if (hostView.mForeground != null) hostView.mForeground.jumpToCurrentState();
    }

    static void drawableHotspotChanged(View hostView, float x, float y) {
        if (hostView.mBackground != null) hostView.mBackground.setHotspot(x, y);
        if (hostView.mForeground != null) hostView.mForeground.setHotspot(x, y);
        hostView.dispatchDrawableHotspotChanged(x, y);
    }

    static void invalidateDrawable(View hostView, android.graphics.drawable.Drawable drawable) {
        if (hostView.verifyDrawable(drawable)) {
            hostView.invalidate();
        }
    }

    static void scheduleDrawable(View hostView, android.graphics.drawable.Drawable who, Runnable what, long when) {
        if (hostView.verifyDrawable(who) && what != null) {
            final long delay = when - android.os.SystemClock.uptimeMillis();
            hostView.postDelayed(what, Math.max(0L, delay));
        }
    }

    static void unscheduleDrawable(View hostView, android.graphics.drawable.Drawable who, Runnable what) {
        if (hostView.verifyDrawable(who) && what != null) {
            hostView.removeCallbacks(what);
        }
    }

    static void setBackgroundTintList(View hostView, android.content.res.ColorStateList tint) {
        hostView.mBackgroundTintList = tint;
        ViewInteraction.applyBackgroundTint(hostView);
    }

    static void setBackgroundTintMode(View hostView, android.graphics.PorterDuff.Mode tintMode) {
        hostView.mBackgroundTintMode = tintMode;
        ViewInteraction.applyBackgroundTint(hostView);
    }

    static void applyBackgroundTint(View hostView) {
        if (hostView.mBackground != null && (hostView.mBackgroundTintList != null || hostView.mBackgroundTintMode != null)) {
            hostView.mBackground = hostView.mBackground.mutate();
            hostView.mBackground.setTintList(hostView.mBackgroundTintList);
            hostView.mBackground.setTintMode(hostView.mBackgroundTintMode);
            if (hostView.mBackground.isStateful()) {
                hostView.mBackground.setState(hostView.getDrawableState());
            }
            hostView.invalidate();
        }
    }

    static void setForegroundTintList(View hostView, android.content.res.ColorStateList tint) {
        hostView.mForegroundTintList = tint;
        ViewInteraction.applyForegroundTint(hostView);
    }

    static void setForegroundTintMode(View hostView, android.graphics.PorterDuff.Mode tintMode) {
        hostView.mForegroundTintMode = tintMode;
        ViewInteraction.applyForegroundTint(hostView);
    }

    static void applyForegroundTint(View hostView) {
        if (hostView.mForeground != null && (hostView.mForegroundTintList != null || hostView.mForegroundTintMode != null)) {
            hostView.mForeground = hostView.mForeground.mutate();
            hostView.mForeground.setTintList(hostView.mForegroundTintList);
            hostView.mForeground.setTintMode(hostView.mForegroundTintMode);
            if (hostView.mForeground.isStateful()) {
                hostView.mForeground.setState(hostView.getDrawableState());
            }
            hostView.invalidate();
        }
    }

    static boolean requestFocus(View hostView, int direction, android.graphics.Rect previouslyFocusedRect) {
        if (!hostView.mFocusable || hostView.mVisibility != View.VISIBLE) {
            return false;
        }
        return hostView.requestFocus();
    }

    static java.util.ArrayList<View> getFocusables(View hostView, int direction) {
        final java.util.ArrayList<View> result = new java.util.ArrayList<View>(24);
        hostView.addFocusables(result, direction);
        return result;
    }

    static void addFocusables(View hostView, java.util.ArrayList<View> views, int direction, int focusableMode) {
        if (views == null || !hostView.mFocusable || hostView.mVisibility != View.VISIBLE || !hostView.mEnabled) {
            return;
        }
        if ((focusableMode & View.FOCUSABLES_TOUCH_MODE) == View.FOCUSABLES_TOUCH_MODE && hostView.isInTouchMode()
                && !hostView.mFocusableInTouchMode) {
            return;
        }
        views.add(hostView);
    }

    static java.util.ArrayList<View> getTouchables(View hostView) {
        final java.util.ArrayList<View> result = new java.util.ArrayList<View>();
        hostView.addTouchables(result);
        return result;
    }

    static void addTouchables(View hostView, java.util.ArrayList<View> views) {
        if (hostView.mVisibility == View.VISIBLE && hostView.mEnabled && (hostView.mClickable || hostView.mLongClickable)) {
            views.add(hostView);
        }
    }

    static void findViewsWithText(View hostView, java.util.ArrayList<View> outViews, CharSequence searched,
            int flags) {
        if ((flags & View.FIND_VIEWS_WITH_CONTENT_DESCRIPTION) != 0 && searched != null
                && searched.length() > 0 && hostView.mContentDescription != null
                && hostView.mContentDescription.length() > 0) {
            final String searchedLowerCase = searched.toString().toLowerCase();
            final String contentDescriptionLowerCase = hostView.mContentDescription.toString().toLowerCase();
            if (contentDescriptionLowerCase.contains(searchedLowerCase)) {
                outViews.add(hostView);
            }
        }
    }

    static boolean dispatchGenericMotionEvent(View hostView, MotionEvent event) {
        if (hostView.mOnGenericMotionListener != null && hostView.mOnGenericMotionListener.onGenericMotion(hostView, event)) {
            return true;
        }
        return hostView.onGenericMotionEvent(event);
    }

    static boolean dispatchHoverEvent(View hostView, MotionEvent event) {
        if (hostView.mOnHoverListener != null && hostView.mOnHoverListener.onHover(hostView, event)) {
            return true;
        }
        return hostView.onHoverEvent(event);
    }

    static boolean dispatchDragEvent(View hostView, DragEvent event) {
        if (hostView.mOnDragListener != null && hostView.mOnDragListener.onDrag(hostView, event)) {
            return true;
        }
        return hostView.onDragEvent(event);
    }

    static boolean startDragAndDrop(View hostView, android.content.ClipData data,
            DragShadowBuilder shadowBuilder, Object myLocalState, int flags) {
        return false;
    }

    static boolean startDrag(View hostView, android.content.ClipData data, DragShadowBuilder shadowBuilder,
            Object myLocalState, int flags) {
        return hostView.startDragAndDrop(data, shadowBuilder, myLocalState, flags);
    }

    static void setOnContextClickListener(View hostView, OnContextClickListener l) {
        if (!hostView.mContextClickable) hostView.mContextClickable = true;
        hostView.mOnContextClickListener = l;
    }

    static void addOnUnhandledKeyEventListener(View hostView, OnUnhandledKeyEventListener listener) {
        if (hostView.mUnhandledKeyListeners == null) {
            hostView.mUnhandledKeyListeners = new java.util.ArrayList<OnUnhandledKeyEventListener>();
        }
        hostView.mUnhandledKeyListeners.add(listener);
    }

    static boolean performContextClick(View hostView) {
        if (hostView.mOnContextClickListener != null) {
            return hostView.mOnContextClickListener.onContextClick(hostView);
        }
        return false;
    }

    static void createContextMenu(View hostView, ContextMenu menu) {
        final ContextMenu.ContextMenuInfo menuInfo = hostView.getContextMenuInfo();
        hostView.onCreateContextMenu(menu);
        if (hostView.mOnCreateContextMenuListener != null) {
            hostView.mOnCreateContextMenuListener.onCreateContextMenu(menu, hostView, menuInfo);
        }
    }

    static boolean requestRectangleOnScreen(View hostView, android.graphics.Rect rectangle, boolean immediate) {
        if (hostView.mParent == null) {
            return false;
        }
        View child = hostView;
        final android.graphics.RectF position = new android.graphics.RectF();
        position.set(rectangle);
        ViewParent parent = hostView.mParent;
        boolean scrolled = false;
        while (parent != null) {
            rectangle.set((int) position.left, (int) position.top,
                    (int) position.right, (int) position.bottom);
            scrolled |= parent.requestChildRectangleOnScreen(child, rectangle, immediate);
            if (!(parent instanceof View)) {
                break;
            }
            position.offset(child.getLeft() - child.getScrollX(), child.getTop() - child.getScrollY());
            child = (View) parent;
            parent = child.getParent();
        }
        return scrolled;
    }

    static void setNestedScrollingEnabled(View hostView, boolean enabled) {
        if (!enabled) {
            hostView.stopNestedScroll();
        }
        hostView.mNestedScrollingEnabled = enabled;
    }

    static boolean startNestedScroll(View hostView, int axes) {
        if (hostView.hasNestedScrollingParent()) {
            return true;
        }
        if (hostView.isNestedScrollingEnabled()) {
            ViewParent p = hostView.getParent();
            View child = hostView;
            while (p != null) {
                if (p.onStartNestedScroll(child, hostView, axes)) {
                    hostView.mNestedScrollingParent = p;
                    p.onNestedScrollAccepted(child, hostView, axes);
                    return true;
                }
                if (p instanceof View) {
                    child = (View) p;
                }
                p = p.getParent();
            }
        }
        return false;
    }

    static void stopNestedScroll(View hostView) {
        if (hostView.mNestedScrollingParent != null) {
            hostView.mNestedScrollingParent.onStopNestedScroll(hostView);
            hostView.mNestedScrollingParent = null;
        }
    }

    static boolean dispatchNestedScroll(View hostView, int dxConsumed, int dyConsumed, int dxUnconsumed,
            int dyUnconsumed, int[] offsetInWindow) {
        if (hostView.isNestedScrollingEnabled() && hostView.mNestedScrollingParent != null) {
            if (dxConsumed != 0 || dyConsumed != 0 || dxUnconsumed != 0 || dyUnconsumed != 0) {
                int startX = 0;
                int startY = 0;
                if (offsetInWindow != null) {
                    hostView.getLocationInWindow(offsetInWindow);
                    startX = offsetInWindow[0];
                    startY = offsetInWindow[1];
                }
                hostView.mNestedScrollingParent.onNestedScroll(hostView, dxConsumed, dyConsumed,
                        dxUnconsumed, dyUnconsumed);
                if (offsetInWindow != null) {
                    hostView.getLocationInWindow(offsetInWindow);
                    offsetInWindow[0] -= startX;
                    offsetInWindow[1] -= startY;
                }
                return true;
            } else if (offsetInWindow != null) {
                offsetInWindow[0] = 0;
                offsetInWindow[1] = 0;
            }
        }
        return false;
    }

    static boolean dispatchNestedPreScroll(View hostView, int dx, int dy, int[] consumed, int[] offsetInWindow) {
        if (hostView.isNestedScrollingEnabled() && hostView.mNestedScrollingParent != null) {
            if (dx != 0 || dy != 0) {
                int startX = 0;
                int startY = 0;
                if (offsetInWindow != null) {
                    hostView.getLocationInWindow(offsetInWindow);
                    startX = offsetInWindow[0];
                    startY = offsetInWindow[1];
                }
                if (consumed == null) {
                    consumed = new int[2];
                }
                consumed[0] = 0;
                consumed[1] = 0;
                hostView.mNestedScrollingParent.onNestedPreScroll(hostView, dx, dy, consumed);
                if (offsetInWindow != null) {
                    hostView.getLocationInWindow(offsetInWindow);
                    offsetInWindow[0] -= startX;
                    offsetInWindow[1] -= startY;
                }
                return consumed[0] != 0 || consumed[1] != 0;
            } else if (offsetInWindow != null) {
                offsetInWindow[0] = 0;
                offsetInWindow[1] = 0;
            }
        }
        return false;
    }

    static boolean dispatchNestedFling(View hostView, float velocityX, float velocityY, boolean consumed) {
        if (hostView.isNestedScrollingEnabled() && hostView.mNestedScrollingParent != null) {
            return hostView.mNestedScrollingParent.onNestedFling(hostView, velocityX, velocityY, consumed);
        }
        return false;
    }

    static boolean dispatchNestedPreFling(View hostView, float velocityX, float velocityY) {
        if (hostView.isNestedScrollingEnabled() && hostView.mNestedScrollingParent != null) {
            return hostView.mNestedScrollingParent.onNestedPreFling(hostView, velocityX, velocityY);
        }
        return false;
    }

    static boolean dispatchNestedPrePerformAccessibilityAction(View hostView, int action, Bundle arguments) {
        ViewParent p = hostView.getParent();
        while (p != null) {
            if (p.onNestedPrePerformAccessibilityAction(hostView, action, arguments)) {
                return true;
            }
            p = p.getParent();
        }
        return false;
    }

    static boolean isImportantForAccessibility(View hostView) {
        if (hostView.mImportantForAccessibility == View.IMPORTANT_FOR_ACCESSIBILITY_NO
                || hostView.mImportantForAccessibility == View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS) {
            return false;
        }
        ViewParent parent = hostView.mParent;
        while (parent instanceof View) {
            if (((View) parent).getImportantForAccessibility()
                    == View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS) {
                return false;
            }
            parent = parent.getParent();
        }
        return hostView.mImportantForAccessibility == View.IMPORTANT_FOR_ACCESSIBILITY_YES
                || hostView.mClickable || hostView.mLongClickable || hostView.mFocusable || hostView.mContentDescription != null;
    }

    static boolean isImportantForAutofill(View hostView) {
        return hostView.mImportantForAutofill != View.IMPORTANT_FOR_AUTOFILL_NO
                && hostView.mImportantForAutofill != View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS;
    }

    static ViewParent getParentForAccessibility(View hostView) {
        ViewParent parent = hostView.mParent;
        while (parent instanceof View) {
            if (((View) parent).isImportantForAccessibility()) {
                return parent;
            }
            parent = parent.getParent();
        }
        return null;
    }

    static void sendAccessibilityEvent(View hostView, int eventType) {
        if (hostView.mAccessibilityDelegate != null) {
            hostView.mAccessibilityDelegate.sendAccessibilityEvent(hostView, eventType);
        }
    }

    static boolean performAccessibilityAction(View hostView, int action, Bundle arguments) {
        if (hostView.mAccessibilityDelegate != null) {
            return hostView.mAccessibilityDelegate.performAccessibilityAction(hostView, action, arguments);
        }
        return false;
    }

    static boolean dispatchPopulateAccessibilityEvent(View hostView, 
            android.view.accessibility.AccessibilityEvent event) {
        return false;
    }

    static android.view.accessibility.AccessibilityNodeProvider getAccessibilityNodeProvider(View hostView) {
        return hostView.mAccessibilityDelegate != null
                ? hostView.mAccessibilityDelegate.getAccessibilityNodeProvider(hostView) : null;
    }

    static void setLayoutDirection(View hostView, int layoutDirection) {
        if (hostView.mLayoutDirection == layoutDirection) return;
        hostView.mLayoutDirection = layoutDirection;
        hostView.resolveRtlPropertiesIfNeeded();
        hostView.requestLayout();
        hostView.invalidate();
    }

    static void setAnimation(View hostView, android.view.animation.Animation animation) {
        hostView.mCurrentAnimation = animation;
        if (animation != null) {
            animation.reset();
        }
    }

    static void startAnimation(View hostView, android.view.animation.Animation animation) {
        animation.setStartTime(android.view.animation.Animation.START_ON_FIRST_FRAME);
        hostView.setAnimation(animation);
        hostView.invalidate();
        ViewInteraction.scheduleAnimationTick(hostView);
    }

    static void clearAnimation(View hostView) {
        if (hostView.mCurrentAnimation != null) {
            hostView.mCurrentAnimation.detach();
        }
        hostView.mCurrentAnimation = null;
    }

    static void scheduleAnimationTick(View hostView) {
        if (hostView.mAnimationTicker != null) {
            return;
        }
        final android.view.animation.Transformation transformation =
                new android.view.animation.Transformation();
        hostView.mAnimationTicker = new Choreographer.FrameCallback() {
            @Override
            public void doFrame(long frameTimeNanos) {
                hostView.mAnimationTicker = null;
                final android.view.animation.Animation a = hostView.mCurrentAnimation;
                if (a == null) {
                    return;
                }
                final long now = frameTimeNanos / 1000000L;
                final boolean more = a.getTransformation(now, transformation);
                if (more) {
                    hostView.invalidate();
                    ViewInteraction.scheduleAnimationTick(hostView);
                } else {
                    hostView.mCurrentAnimation = null;
                    hostView.invalidate();
                }
            }
        };
        Choreographer.getInstance().postFrameCallback(hostView.mAnimationTicker);
    }

    static void onRestoreInstanceState(View hostView, android.os.Parcelable state) {
        if (state != null && state != BaseSavedState.EMPTY_STATE) {
            throw new IllegalArgumentException("Wrong state class, expecting View State but "
                    + "received " + state.getClass().toString() + " instead. This usually happens "
                    + "when two views of different type have the same id in the same hierarchy.");
        }
    }

    static void dispatchSaveInstanceState(View hostView, android.util.SparseArray<android.os.Parcelable> container) {
        if (hostView.mId != View.NO_ID && hostView.mSaveEnabled) {
            final android.os.Parcelable state = hostView.onSaveInstanceState();
            if (state != null) {
                container.put(hostView.mId, state);
            }
        }
    }

    static void dispatchRestoreInstanceState(View hostView, 
            android.util.SparseArray<android.os.Parcelable> container) {
        if (hostView.mId != View.NO_ID) {
            final android.os.Parcelable state = container.get(hostView.mId);
            if (state != null) {
                hostView.onRestoreInstanceState(state);
            }
        }
    }
}
