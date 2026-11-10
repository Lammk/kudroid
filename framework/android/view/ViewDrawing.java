package android.view;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.View.*;
import static android.view.View.*;

final class ViewDrawing {
    private ViewDrawing() {}

    static void draw(View hostView, Canvas canvas) {
        if (canvas == null) return;
        ViewDrawing.drawBackground(hostView, canvas);
        hostView.onDraw(canvas);
        hostView.dispatchDraw(canvas);
        hostView.onDrawForeground(canvas);
    }

    static void drawBackground(View hostView, Canvas canvas) {
        final android.graphics.drawable.Drawable background = hostView.mBackground;
        if (background == null) return;
        final int width = hostView.mRight - hostView.mLeft;
        final int height = hostView.mBottom - hostView.mTop;
        final android.graphics.Rect bounds = background.getBounds();
        if (bounds.left != 0 || bounds.top != 0 || bounds.right != width || bounds.bottom != height) {
            background.setBounds(0, 0, width, height);
        }
        if ((hostView.mScrollX | hostView.mScrollY) != 0) {
            canvas.translate(hostView.mScrollX, hostView.mScrollY);
            background.draw(canvas);
            canvas.translate(-hostView.mScrollX, -hostView.mScrollY);
        } else {
            background.draw(canvas);
        }
    }

    static boolean draw(View hostView, Canvas canvas, ViewGroup parent, long drawingTime) {
        hostView.computeScroll();
        final float alpha = hostView.mAlpha;
        if (alpha <= 0f) return false;
        final int count = canvas.save();
        canvas.translate(hostView.mLeft - hostView.mScrollX, hostView.mTop - hostView.mScrollY);
        if (!hostView.hasIdentityMatrix()) {
            canvas.concat(hostView.getMatrix());
        }
        if (parent == null || parent.getClipChildren()) {
            canvas.clipRect(hostView.mScrollX, hostView.mScrollY, hostView.mScrollX + (hostView.mRight - hostView.mLeft), hostView.mScrollY + (hostView.mBottom - hostView.mTop));
        }
        if (hostView.mClipBounds != null) {
            canvas.clipRect(hostView.mClipBounds);
        }
        if (alpha < 1f) {
            canvas.saveLayerAlpha(0, 0, 0, 0, Math.round(alpha * 255));
        }
        hostView.draw(canvas);
        canvas.restoreToCount(count);
        return false;
    }

    static boolean isShown(View hostView) {
        View current = hostView;
        do {
            if (current.mVisibility != View.VISIBLE) {
                return false;
            }
            current = current.mParent;
        } while (current != null);
        return true;
    }

    static void setBackgroundDrawable(View hostView, android.graphics.drawable.Drawable background) {
        if (hostView.mBackground == background) return;
        final int oldMinWidth = hostView.getSuggestedMinimumWidth();
        final int oldMinHeight = hostView.getSuggestedMinimumHeight();
        if (hostView.mBackground != null) {
            hostView.mBackground.setCallback(null);
        }
        hostView.mBackground = background;
        if (background != null) {
            background.setCallback(hostView);
            background.setVisible(hostView.mVisibility == View.VISIBLE, false);
            if (background.isStateful()) {
                background.setState(hostView.getDrawableState());
            }
            ViewInteraction.applyBackgroundTint(hostView);
        }
        if (oldMinWidth != hostView.getSuggestedMinimumWidth() || oldMinHeight != hostView.getSuggestedMinimumHeight()) {
            hostView.requestLayout();
        }
        hostView.invalidate();
    }

    static void getLocationOnScreen(View hostView, int[] location) {
        if (location == null || location.length < 2) return;
        int x = 0;
        int y = 0;
        View v = hostView;
        while (v != null) {
            x += v.mLeft - v.mScrollX;
            y += v.mTop - v.mScrollY;
            v = v.mParent;
        }
        location[0] = x;
        location[1] = y;
    }

    static android.content.res.Resources getResources(View hostView) {
        return hostView.mContext != null ? hostView.mContext.getResources()
                                : android.content.res.Resources.getSystem();
    }

    static void setBackgroundColor(View hostView, int color) {
        if (hostView.mBackground instanceof android.graphics.drawable.ColorDrawable) {
            ((android.graphics.drawable.ColorDrawable) hostView.mBackground).setColor(color);
            hostView.invalidate();
        } else {
            hostView.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(color));
        }
    }

    static void setPadding(View hostView, int left, int top, int right, int bottom) {
        final boolean changed = hostView.mPaddingLeft != left || hostView.mPaddingTop != top
                || hostView.mPaddingRight != right || hostView.mPaddingBottom != bottom;
        hostView.mPaddingLeft = left;
        hostView.mPaddingTop = top;
        hostView.mPaddingRight = right;
        hostView.mPaddingBottom = bottom;
        if (changed) hostView.requestLayout();
    }

    static void invalidate(View hostView) {
        View root = hostView;
        while (root.mParent != null) {
            root = root.mParent;
        }
        if (root != hostView) {
            root.invalidate();
        } else if (hostView.mAttachedToWindow && hostView.mTraversalWindow != null) {
            hostView.mTraversalWindow.scheduleTraversal();
        }
    }

    static void requestLayout(View hostView) {
        hostView.mLayoutRequested = true;
        hostView.mMeasureValid = false;
        if (hostView.mParent != null) {
            hostView.mParent.requestLayout();
        } else if (hostView.mAttachedToWindow && hostView.mTraversalWindow != null) {
            hostView.mTraversalWindow.scheduleTraversal();
        }
    }

    static android.os.Handler getRunQueue(View hostView) {
        android.os.Looper mainLooper = android.os.Looper.getMainLooper();
        if (mainLooper == null) return null;
        if (hostView.mRunQueue == null || hostView.mRunQueue.getLooper() != mainLooper) {
            hostView.mRunQueue = new android.os.Handler(mainLooper);
        }
        return hostView.mRunQueue;
    }

    static boolean post(View hostView, Runnable action) {
        if (action == null) return false;
        android.os.Handler handler = View.moduleGetRunQueue(hostView);
        return handler != null && handler.post(action);
    }

    static boolean postDelayed(View hostView, Runnable action, long delayMillis) {
        if (action == null) return false;
        android.os.Handler handler = View.moduleGetRunQueue(hostView);
        return handler != null && handler.postDelayed(action, delayMillis);
    }

    static boolean removeCallbacks(View hostView, Runnable action) {
        if (action == null) return false;
        android.os.Handler handler = View.moduleGetRunQueue(hostView);
        if (handler != null) handler.removeCallbacks(action);
        return true;
    }

    static void onCancelPendingInputEvents(View hostView) {
        hostView.mTouchDownTime = 0;
        hostView.mLongClickFired = false;
    }

    static void setTag(View hostView, int key, Object tag) {
        if (hostView.mKeyedTags == null) hostView.mKeyedTags = new android.util.SparseArray<Object>();
        hostView.mKeyedTags.put(key, tag);
    }

    static int generateViewId() {
        for (;;) {
            final int result = View.sNextGeneratedId.get();
            int newValue = result + 1;
            if (newValue > 0x00FFFFFF) newValue = 1; // roll over, skipping 0
            if (View.sNextGeneratedId.compareAndSet(result, newValue)) return result;
        }
    }

    static void requestFitSystemWindows(View hostView) {
        Window window = hostView.getRootView().mTraversalWindow;
        if (window != null) window.requestApplyInsets();
    }

    static WindowInsets onApplyWindowInsets(View hostView, WindowInsets insets) {
        if (hostView.fitSystemWindows(new android.graphics.Rect(insets.getSystemWindowInsetLeft(),
                insets.getSystemWindowInsetTop(), insets.getSystemWindowInsetRight(),
                insets.getSystemWindowInsetBottom()))) return insets.consumeSystemWindowInsets();
        return insets;
    }

    static boolean fitSystemWindows(View hostView, android.graphics.Rect insets) {
        if (!hostView.mFitsSystemWindows) return false;
        hostView.setPadding(insets.left, insets.top, insets.right, insets.bottom);
        return true;
    }

    static WindowInsets dispatchApplyWindowInsets(View hostView, WindowInsets insets) {
        if (hostView.mOnApplyWindowInsetsListener != null) {
            WindowInsets result = hostView.mOnApplyWindowInsetsListener.onApplyWindowInsets(hostView, insets);
            return result != null ? result : insets;
        }
        return hostView.onApplyWindowInsets(insets);
    }

    static WindowInsets getRootWindowInsets(View hostView) {
        Window window = hostView.getRootView().mTraversalWindow;
        return hostView.mAttachedToWindow && window != null ? window.getRootWindowInsets() : null;
    }

    static WindowInsets computeSystemWindowInsets(View hostView, WindowInsets in, android.graphics.Rect outLocalInsets) {
        outLocalInsets.set(in.getSystemWindowInsetLeft(), in.getSystemWindowInsetTop(),
                in.getSystemWindowInsetRight(), in.getSystemWindowInsetBottom());
        return in.consumeSystemWindowInsets().inset(outLocalInsets);
    }

    static void setFitsSystemWindows(View hostView, boolean fitSystemWindows) {
        if (hostView.mFitsSystemWindows != fitSystemWindows) {
            hostView.mFitsSystemWindows = fitSystemWindows; hostView.requestApplyInsets();
        }
    }

    static void setKeepScreenOn(View hostView, boolean keepScreenOn) {
        hostView.mKeepScreenOn = keepScreenOn;
        try {
            View.moduleSetKeepScreenOnNative(keepScreenOn);
        } catch (Throwable ignored) {}
    }

    static void scrollTo(View hostView, int x, int y) {
        if (hostView.mScrollX != x || hostView.mScrollY != y) {
            final int oldX = hostView.mScrollX;
            final int oldY = hostView.mScrollY;
            hostView.mScrollX = x;
            hostView.mScrollY = y;
            hostView.onScrollChanged(hostView.mScrollX, hostView.mScrollY, oldX, oldY);
            hostView.invalidate();
        }
    }

    static void saveAttributeDataForStyleable(View hostView, Context context, int[] styleable,
            android.util.AttributeSet set, android.content.res.TypedArray attrs,
            int defStyleAttr, int defStyleRes) {
    }

    static void setForeground(View hostView, android.graphics.drawable.Drawable foreground) {
        if (hostView.mForeground == foreground) return;
        if (hostView.mForeground != null) {
            hostView.mForeground.setCallback(null);
        }
        hostView.mForeground = foreground;
        if (foreground != null) {
            foreground.setCallback(hostView);
            foreground.setVisible(hostView.mVisibility == View.VISIBLE, false);
            if (foreground.isStateful()) {
                foreground.setState(hostView.getDrawableState());
            }
            ViewInteraction.applyForegroundTint(hostView);
        }
        hostView.invalidate();
    }

    static void setEnabled(View hostView, boolean enabled) {
        if (hostView.mEnabled == enabled) return;
        hostView.mEnabled = enabled;
        hostView.refreshDrawableState();
        hostView.invalidate();
        if (!enabled) hostView.cancelPendingInputEvents();
    }

    static void setSelected(View hostView, boolean selected) {
        if (hostView.mSelected == selected) return;
        hostView.mSelected = selected;
        hostView.invalidate();
        hostView.refreshDrawableState();
        hostView.dispatchSetSelected(selected);
    }

    static void setPressed(View hostView, boolean pressed) {
        if (hostView.mPressed == pressed) return;
        hostView.mPressed = pressed;
        hostView.refreshDrawableState();
        hostView.invalidate();
        hostView.dispatchSetPressed(pressed);
    }

    static void setActivated(View hostView, boolean activated) {
        if (hostView.mActivated == activated) return;
        hostView.mActivated = activated;
        hostView.invalidate();
        hostView.refreshDrawableState();
        hostView.dispatchSetActivated(activated);
    }

    static ViewOverlay getOverlay(View hostView) {
        if (hostView.mOverlay == null) hostView.mOverlay = new ViewOverlay(hostView);
        return hostView.mOverlay;
    }

    static ViewPropertyAnimator animate(View hostView) {
        if (hostView.mAnimator == null) hostView.mAnimator = new ViewPropertyAnimator(hostView);
        return hostView.mAnimator;
    }

    static void setAlpha(View hostView, float alpha) {
        if (hostView.mAlpha != alpha) {
            hostView.mAlpha = alpha;
            hostView.invalidate();
        }
    }

    static void setTranslationX(View hostView, float v) {
        if (hostView.mTranslationX != v) {
            hostView.mTranslationX = v;
            hostView.invalidate();
        }
    }

    static void setTranslationY(View hostView, float v) {
        if (hostView.mTranslationY != v) {
            hostView.mTranslationY = v;
            hostView.invalidate();
        }
    }

    static void setTranslationZ(View hostView, float v) {
        if (hostView.mTranslationZ != v) {
            hostView.mTranslationZ = v;
            hostView.invalidate();
        }
    }

    static void setScaleX(View hostView, float v) {
        if (hostView.mScaleX != v) {
            hostView.mScaleX = v;
            hostView.invalidate();
        }
    }

    static void setScaleY(View hostView, float v) {
        if (hostView.mScaleY != v) {
            hostView.mScaleY = v;
            hostView.invalidate();
        }
    }

    static void setRotation(View hostView, float v) {
        if (hostView.mRotation != v) {
            hostView.mRotation = v;
            hostView.invalidate();
        }
    }

    static void setMinimumWidth(View hostView, int minWidth) {
        if (hostView.mMinWidth != minWidth) {
            hostView.mMinWidth = minWidth;
            hostView.requestLayout();
        }
    }

    static void setMinimumHeight(View hostView, int minHeight) {
        if (hostView.mMinHeight != minHeight) {
            hostView.mMinHeight = minHeight;
            hostView.requestLayout();
        }
    }

    static int resolveSizeAndState(int size, int measureSpec, int childMeasuredState) {
        final int specMode = MeasureSpec.getMode(measureSpec);
        final int specSize = MeasureSpec.getSize(measureSpec);
        final int result;
        switch (specMode) {
            case MeasureSpec.AT_MOST:
                if (specSize < size) {
                    result = specSize | View.MEASURED_STATE_TOO_SMALL;
                } else {
                    result = size;
                }
                break;
            case MeasureSpec.EXACTLY:
                result = specSize;
                break;
            case MeasureSpec.UNSPECIFIED:
            default:
                result = size;
        }
        return result | (childMeasuredState & View.MEASURED_STATE_MASK);
    }

    static int getMeasuredState(View hostView) {
        return (hostView.mMeasuredWidth & View.MEASURED_STATE_MASK)
                | ((hostView.mMeasuredHeight >> View.MEASURED_HEIGHT_STATE_SHIFT)
                & (View.MEASURED_STATE_MASK >> View.MEASURED_HEIGHT_STATE_SHIFT));
    }
}
