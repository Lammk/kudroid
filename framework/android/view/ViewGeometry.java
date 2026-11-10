package android.view;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.View.*;
import static android.view.View.*;

final class ViewGeometry {
    private ViewGeometry() {}

    static void forceLayout(View hostView) {
        hostView.mLayoutRequested = true;
        hostView.mMeasureValid = false;
    }

    static int getLayoutDirection(View hostView) {
        if (hostView.mLayoutDirection == View.LAYOUT_DIRECTION_LTR || hostView.mLayoutDirection == View.LAYOUT_DIRECTION_RTL)
            return hostView.mLayoutDirection;
        if (hostView.mLayoutDirection == View.LAYOUT_DIRECTION_LOCALE)
            return android.text.TextUtils.getLayoutDirectionFromLocale(hostView.getResources().getConfiguration().locale);
        return hostView.mParent != null ? hostView.mParent.getLayoutDirection() : View.LAYOUT_DIRECTION_LTR;
    }

    static long getDrawingTime(View hostView) {
        if (!hostView.mAttachedToWindow) return 0;
        if (hostView.mParent != null) return hostView.mParent.getDrawingTime();
        return hostView.mTraversalWindow != null ? hostView.mTraversalWindow.getDrawingTime() : 0;
    }

    static void addOnAttachStateChangeListener(View hostView, OnAttachStateChangeListener listener) {
        if (listener == null) return;
        if (hostView.mAttachStateListeners == null) {
            hostView.mAttachStateListeners =
                    new java.util.concurrent.CopyOnWriteArrayList<OnAttachStateChangeListener>();
        }
        hostView.mAttachStateListeners.add(listener);
    }

    static void dispatchAttachedToWindow(View hostView) {
        if (hostView.mAttachedToWindow) return;
        hostView.mAttachedToWindow = true;
        hostView.mWindowAttachCount++;
        hostView.onAttachedToWindow();
        if (hostView.mAttachStateListeners != null) {
            for (OnAttachStateChangeListener l : hostView.mAttachStateListeners) {
                l.onViewAttachedToWindow(hostView);
            }
        }
        hostView.refreshDrawableState();
        hostView.onVisibilityAggregated(hostView.isShown());
    }

    static void dispatchDetachedFromWindow(View hostView) {
        if (!hostView.mAttachedToWindow) return;
        hostView.onDetachedFromWindow();
        if (hostView.mAttachStateListeners != null) {
            for (OnAttachStateChangeListener l : hostView.mAttachStateListeners) {
                l.onViewDetachedFromWindow(hostView);
            }
        }
        hostView.mAttachedToWindow = false;
        hostView.onVisibilityAggregated(false);
    }

    static void onVisibilityAggregated(View hostView, boolean isVisible) {
        if (hostView.mBackground != null) hostView.mBackground.setVisible(isVisible, false);
        if (hostView.mForeground != null) hostView.mForeground.setVisible(isVisible, false);
    }

    static boolean dispatchVisibilityAggregated(View hostView, boolean isVisible) {
        final boolean thisVisible = hostView.getVisibility() == View.VISIBLE;
        if (thisVisible || !isVisible) {
            hostView.onVisibilityAggregated(isVisible);
        }
        return thisVisible && isVisible;
    }

    static void dispatchSystemUiVisibilityChanged(View hostView, int visibility) {
        if (hostView.mOnSystemUiVisibilityChangeListener != null) {
            hostView.mOnSystemUiVisibilityChangeListener.onSystemUiVisibilityChange(
                    visibility & ~View.SYSTEM_UI_FLAG_LOW_PROFILE);
        }
    }

    static void dispatchStartTemporaryDetach(View hostView) {
        hostView.mTemporarilyDetached = true;
        hostView.onStartTemporaryDetach();
    }

    static void dispatchFinishTemporaryDetach(View hostView) {
        hostView.mTemporarilyDetached = false;
        hostView.onFinishTemporaryDetach();
    }

    static void setHasTransientState(View hostView, boolean hasTransientState) {
        if (hostView.mHasTransientState == hasTransientState) return;
        hostView.mHasTransientState = hasTransientState;
        if (hostView.mParent != null) {
            hostView.mParent.childHasTransientStateChanged(hostView, hasTransientState);
        }
    }

    static void setHovered(View hostView, boolean hovered) {
        if (hostView.mHovered == hovered) return;
        hostView.mHovered = hovered;
        hostView.refreshDrawableState();
        hostView.onHoverChanged(hovered);
    }

    static boolean isOpaque(View hostView) {
        return hostView.mBackground != null
                && hostView.mBackground.getOpacity() == android.graphics.PixelFormat.OPAQUE;
    }

    static void setLayerType(View hostView, int layerType, android.graphics.Paint paint) {
        if (layerType < View.LAYER_TYPE_NONE || layerType > View.LAYER_TYPE_HARDWARE) {
            throw new IllegalArgumentException("Layer type can only be one of: LAYER_TYPE_NONE, "
                    + "LAYER_TYPE_SOFTWARE or LAYER_TYPE_HARDWARE");
        }
        hostView.mLayerType = layerType;
        hostView.invalidate();
    }

    static void setTransitionAlpha(View hostView, float alpha) {
        hostView.mTransitionAlpha = alpha;
        hostView.invalidate();
    }

    static boolean getClipBounds(View hostView, android.graphics.Rect outRect) {
        if (hostView.mClipBounds != null) {
            outRect.set(hostView.mClipBounds);
            return true;
        }
        return false;
    }

    static void setClipBounds(View hostView, android.graphics.Rect clipBounds) {
        hostView.mClipBounds = clipBounds != null ? new android.graphics.Rect(clipBounds) : null;
        hostView.invalidate();
    }

    static void setStateListAnimator(View hostView, android.animation.StateListAnimator stateListAnimator) {
        if (hostView.mStateListAnimator == stateListAnimator) return;
        if (hostView.mStateListAnimator != null) {
            hostView.mStateListAnimator.setTarget(null);
        }
        hostView.mStateListAnimator = stateListAnimator;
        if (stateListAnimator != null) {
            stateListAnimator.setTarget(hostView);
            if (hostView.mAttachedToWindow) {
                stateListAnimator.setState(hostView.getDrawableState());
            }
        }
    }

    static void setLeftTopRightBottom(View hostView, int left, int top, int right, int bottom) {
        final int oldWidth = hostView.mRight - hostView.mLeft;
        final int oldHeight = hostView.mBottom - hostView.mTop;
        hostView.mLeft = left;
        hostView.mTop = top;
        hostView.mRight = right;
        hostView.mBottom = bottom;
        final int newWidth = right - left;
        final int newHeight = bottom - top;
        if (newWidth != oldWidth || newHeight != oldHeight) {
            hostView.onSizeChanged(newWidth, newHeight, oldWidth, oldHeight);
        }
        hostView.invalidate();
    }

    static void offsetLeftAndRight(View hostView, int offset) {
        if (offset != 0) {
            hostView.mLeft += offset;
            hostView.mRight += offset;
            hostView.invalidate();
        }
    }

    static void offsetTopAndBottom(View hostView, int offset) {
        if (offset != 0) {
            hostView.mTop += offset;
            hostView.mBottom += offset;
            hostView.invalidate();
        }
    }

    static void setRotationX(View hostView, float rotationX) {
        hostView.mRotationX = rotationX;
        hostView.invalidate();
    }

    static void setRotationY(View hostView, float rotationY) {
        hostView.mRotationY = rotationY;
        hostView.invalidate();
    }

    static void setAnimationMatrix(View hostView, android.graphics.Matrix matrix) {
        hostView.mAnimationMatrix = matrix;
        hostView.invalidate();
    }

    static android.graphics.Matrix getMatrix(View hostView) {
        final android.graphics.Matrix m = new android.graphics.Matrix();
        final float px = hostView.getPivotX();
        final float py = hostView.getPivotY();
        m.postTranslate(-px, -py);
        m.postScale(hostView.mScaleX, hostView.mScaleY);
        m.postRotate(hostView.mRotation);
        m.postTranslate(px + hostView.mTranslationX, py + hostView.mTranslationY);
        return m;
    }

    static boolean hasIdentityMatrix(View hostView) {
        return hostView.mScaleX == 1f && hostView.mScaleY == 1f && hostView.mRotation == 0f
                && hostView.mTranslationX == 0f && hostView.mTranslationY == 0f;
    }

    static void transformMatrixToGlobal(View hostView, android.graphics.Matrix matrix) {
        final ViewParent parent = hostView.mParent;
        if (parent instanceof View) {
            final View vp = (View) parent;
            vp.transformMatrixToGlobal(matrix);
            matrix.preTranslate(-vp.mScrollX, -vp.mScrollY);
        }
        matrix.preTranslate(hostView.mLeft, hostView.mTop);
        if (!hostView.hasIdentityMatrix()) {
            matrix.preConcat(hostView.getMatrix());
        }
    }

    static void transformMatrixToLocal(View hostView, android.graphics.Matrix matrix) {
        final android.graphics.Matrix inverse = new android.graphics.Matrix();
        final android.graphics.Matrix toGlobal = new android.graphics.Matrix();
        hostView.transformMatrixToGlobal(toGlobal);
        if (toGlobal.invert(inverse)) {
            matrix.preConcat(inverse);
        }
    }

    static void getDrawingRect(View hostView, android.graphics.Rect outRect) {
        outRect.left = hostView.mScrollX;
        outRect.top = hostView.mScrollY;
        outRect.right = hostView.mScrollX + (hostView.mRight - hostView.mLeft);
        outRect.bottom = hostView.mScrollY + (hostView.mBottom - hostView.mTop);
    }

    static boolean getLocalVisibleRect(View hostView, android.graphics.Rect r) {
        final android.graphics.Point offset = new android.graphics.Point();
        if (hostView.getGlobalVisibleRect(r, offset)) {
            r.offset(-offset.x, -offset.y);
            return true;
        }
        return false;
    }

    static boolean getGlobalVisibleRect(View hostView, android.graphics.Rect r, android.graphics.Point globalOffset) {
        final int width = hostView.mRight - hostView.mLeft;
        final int height = hostView.mBottom - hostView.mTop;
        if (width > 0 && height > 0) {
            r.set(0, 0, width, height);
            if (globalOffset != null) {
                globalOffset.set(-hostView.mScrollX, -hostView.mScrollY);
            }
            return hostView.mParent == null || hostView.getParentGroup().getChildVisibleRect(hostView, r, globalOffset);
        }
        return false;
    }

    static void getWindowVisibleDisplayFrame(View hostView, android.graphics.Rect outRect) {
        final android.util.DisplayMetrics dm = hostView.getResources().getDisplayMetrics();
        View root = hostView.getRootView();
        WindowInsets insets = hostView.getRootWindowInsets();
        int width = root.getWidth() > 0 ? root.getWidth() : dm.widthPixels;
        int height = root.getHeight() > 0 ? root.getHeight() : dm.heightPixels;
        outRect.set(insets == null ? 0 : insets.getSystemWindowInsetLeft(),
                insets == null ? 0 : insets.getSystemWindowInsetTop(),
                width - (insets == null ? 0 : insets.getSystemWindowInsetRight()),
                height - (insets == null ? 0 : insets.getSystemWindowInsetBottom()));
    }

    static void bringToFront(View hostView) {
        if (hostView.mParent != null) {
            hostView.mParent.bringChildToFront(hostView);
        }
    }

    static <T extends View> T requireViewById(View hostView, int id) {
        final T view = (T) hostView.findViewById(id);
        if (view == null) {
            throw new IllegalArgumentException("ID does not reference a View inside this View");
        }
        return view;
    }

    static String toString(View hostView) {
        final StringBuilder out = new StringBuilder(128);
        out.append(hostView.getClass().getName());
        out.append('{');
        out.append(Integer.toHexString(System.identityHashCode(hostView)));
        out.append(' ');
        out.append(hostView.mVisibility == View.VISIBLE ? 'V' : hostView.mVisibility == View.INVISIBLE ? 'I' : 'G');
        out.append(hostView.mFocusable ? 'F' : '.');
        out.append(hostView.mEnabled ? 'E' : '.');
        out.append(hostView.mWillNotDraw ? '.' : 'D');
        out.append(hostView.mClickable ? 'C' : '.');
        out.append(hostView.mLongClickable ? 'L' : '.');
        out.append(' ');
        out.append(hostView.mLeft).append(',').append(hostView.mTop).append('-').append(hostView.mRight).append(',')
                .append(hostView.mBottom);
        if (hostView.mId != View.NO_ID) {
            out.append(" #").append(Integer.toHexString(hostView.mId));
            final android.content.res.Resources r = hostView.mContext != null ? hostView.mContext.getResources() : null;
            if (r != null && (hostView.mId >>> 24) != 0) {
                try {
                    final String name = r.getResourceName(hostView.mId);
                    final int slash = name.lastIndexOf('/');
                    out.append(" ").append(name.substring(slash + 1));
                } catch (RuntimeException e) {
                    // Unknown id: the hex form above is all there is.
                }
            }
        }
        out.append("}");
        return out.toString();
    }

    static void onScrollChanged(View hostView, int l, int t, int oldl, int oldt) {
        if (hostView.mOnScrollChangeListener != null) {
            hostView.mOnScrollChangeListener.onScrollChange(hostView, l, t, oldl, oldt);
        }
    }

    static boolean canScrollHorizontally(View hostView, int direction) {
        final int offset = hostView.computeHorizontalScrollOffset();
        final int range = hostView.computeHorizontalScrollRange() - hostView.computeHorizontalScrollExtent();
        if (range == 0) return false;
        if (direction < 0) {
            return offset > 0;
        }
        return offset < range - 1;
    }

    static boolean canScrollVertically(View hostView, int direction) {
        final int offset = hostView.computeVerticalScrollOffset();
        final int range = hostView.computeVerticalScrollRange() - hostView.computeVerticalScrollExtent();
        if (range == 0) return false;
        if (direction < 0) {
            return offset > 0;
        }
        return offset < range - 1;
    }

    static boolean overScrollBy(View hostView, int deltaX, int deltaY, int scrollX, int scrollY,
            int scrollRangeX, int scrollRangeY, int maxOverScrollX, int maxOverScrollY,
            boolean isTouchEvent) {
        final int overScrollMode = hostView.mOverScrollMode;
        final boolean canScrollHorizontal = hostView.computeHorizontalScrollRange() > hostView.computeHorizontalScrollExtent();
        final boolean canScrollVertical = hostView.computeVerticalScrollRange() > hostView.computeVerticalScrollExtent();
        final boolean overScrollHorizontal = overScrollMode == View.OVER_SCROLL_ALWAYS
                || (overScrollMode == View.OVER_SCROLL_IF_CONTENT_SCROLLS && canScrollHorizontal);
        final boolean overScrollVertical = overScrollMode == View.OVER_SCROLL_ALWAYS
                || (overScrollMode == View.OVER_SCROLL_IF_CONTENT_SCROLLS && canScrollVertical);

        int newScrollX = scrollX + deltaX;
        if (!overScrollHorizontal) {
            maxOverScrollX = 0;
        }
        int newScrollY = scrollY + deltaY;
        if (!overScrollVertical) {
            maxOverScrollY = 0;
        }
        final int left = -maxOverScrollX;
        final int right = maxOverScrollX + scrollRangeX;
        final int top = -maxOverScrollY;
        final int bottom = maxOverScrollY + scrollRangeY;

        boolean clampedX = false;
        if (newScrollX > right) {
            newScrollX = right;
            clampedX = true;
        } else if (newScrollX < left) {
            newScrollX = left;
            clampedX = true;
        }
        boolean clampedY = false;
        if (newScrollY > bottom) {
            newScrollY = bottom;
            clampedY = true;
        } else if (newScrollY < top) {
            newScrollY = top;
            clampedY = true;
        }
        hostView.onOverScrolled(newScrollX, newScrollY, clampedX, clampedY);
        return clampedX || clampedY;
    }

    static void setOverScrollMode(View hostView, int overScrollMode) {
        if (overScrollMode != View.OVER_SCROLL_ALWAYS && overScrollMode != View.OVER_SCROLL_IF_CONTENT_SCROLLS
                && overScrollMode != View.OVER_SCROLL_NEVER) {
            throw new IllegalArgumentException("Invalid overscroll mode " + overScrollMode);
        }
        hostView.mOverScrollMode = overScrollMode;
    }

    static float getBottomFadingEdgeStrength(View hostView) {
        return hostView.computeVerticalScrollOffset() + hostView.computeVerticalScrollExtent()
                < hostView.computeVerticalScrollRange() ? 1.0f : 0.0f;
    }

    static float getRightFadingEdgeStrength(View hostView) {
        return hostView.computeHorizontalScrollOffset() + hostView.computeHorizontalScrollExtent()
                < hostView.computeHorizontalScrollRange() ? 1.0f : 0.0f;
    }

    static void onDrawForeground(View hostView, Canvas canvas) {
        hostView.onDrawScrollBars(canvas);
        if (hostView.mForeground != null) {
            hostView.mForeground.setBounds(0, 0, hostView.getWidth(), hostView.getHeight());
            if ((hostView.mScrollX | hostView.mScrollY) != 0) {
                canvas.translate(hostView.mScrollX, hostView.mScrollY);
                hostView.mForeground.draw(canvas);
                canvas.translate(-hostView.mScrollX, -hostView.mScrollY);
            } else {
                hostView.mForeground.draw(canvas);
            }
        }
    }

    static void postInvalidateDelayed(View hostView, long delayMilliseconds, int left, int top, int right,
            int bottom) {
        hostView.postInvalidateDelayed(delayMilliseconds);
    }
}
