package android.view;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.View.*;
import static android.view.View.*;

final class ViewAttributes {
    private ViewAttributes() {}

    static void addOnLayoutChangeListener(View hostView, OnLayoutChangeListener listener) {
        if (listener == null) return;
        if (hostView.mLayoutChangeListeners == null) {
            hostView.mLayoutChangeListeners = new java.util.ArrayList<OnLayoutChangeListener>();
        }
        if (!hostView.mLayoutChangeListeners.contains(listener)) hostView.mLayoutChangeListeners.add(listener);
    }

    static void applyViewAttributes(View hostView, android.content.res.TypedArray a) {
        int leftPadding = -1, topPadding = -1, rightPadding = -1, bottomPadding = -1;
        int padding = -1, paddingHorizontal = -1, paddingVertical = -1;
        int startPadding = -1, endPadding = -1;
        boolean fitsSystemWindows = false;
        final int n = a.getIndexCount();
        for (int i = 0; i < n; i++) {
            final int attr = a.getIndex(i);
            switch (attr) {
                case com.android.internal.R.styleable.View_background:
                    hostView.setBackground(a.getDrawable(attr));
                    break;
                case com.android.internal.R.styleable.View_padding:
                    padding = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingHorizontal:
                    paddingHorizontal = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingVertical:
                    paddingVertical = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingLeft:
                    leftPadding = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingTop:
                    topPadding = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingRight:
                    rightPadding = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingBottom:
                    bottomPadding = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingStart:
                    startPadding = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingEnd:
                    endPadding = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_id:
                    hostView.mId = a.getResourceId(attr, View.NO_ID);
                    break;
                case com.android.internal.R.styleable.View_tag:
                    hostView.mTag = a.getText(attr);
                    break;
                case com.android.internal.R.styleable.View_visibility:
                    hostView.mVisibility = View.VISIBILITY_FLAGS[a.getInt(attr, 0)];
                    break;
                case com.android.internal.R.styleable.View_minWidth:
                    hostView.mMinWidth = a.getDimensionPixelSize(attr, 0);
                    break;
                case com.android.internal.R.styleable.View_minHeight:
                    hostView.mMinHeight = a.getDimensionPixelSize(attr, 0);
                    break;
                case com.android.internal.R.styleable.View_foreground:
                    hostView.mForeground = a.getDrawable(attr);
                    break;
                case com.android.internal.R.styleable.View_clickable:
                    hostView.mClickable = a.getBoolean(attr, false);
                    break;
                case com.android.internal.R.styleable.View_longClickable:
                    hostView.mLongClickable = a.getBoolean(attr, false);
                    break;
                case com.android.internal.R.styleable.View_focusable:
                    hostView.mFocusable = a.getBoolean(attr, true);
                    break;
                case com.android.internal.R.styleable.View_focusableInTouchMode:
                    hostView.mFocusableInTouchMode = a.getBoolean(attr, false);
                    break;
                case com.android.internal.R.styleable.View_fitsSystemWindows:
                    fitsSystemWindows = a.getBoolean(attr, false);
                    break;
                case com.android.internal.R.styleable.View_keepScreenOn:
                    hostView.mKeepScreenOn = a.getBoolean(attr, false);
                    break;
                case com.android.internal.R.styleable.View_contentDescription:
                    hostView.mContentDescription = a.getText(attr);
                    break;
                case com.android.internal.R.styleable.View_alpha:
                    hostView.mAlpha = a.getFloat(attr, 1.0f);
                    break;
                case com.android.internal.R.styleable.View_elevation:
                    hostView.mElevation = a.getDimension(attr, 0f);
                    break;
                case com.android.internal.R.styleable.View_translationX:
                    hostView.mTranslationX = a.getDimension(attr, 0f);
                    break;
                case com.android.internal.R.styleable.View_translationY:
                    hostView.mTranslationY = a.getDimension(attr, 0f);
                    break;
                case com.android.internal.R.styleable.View_translationZ:
                    hostView.mTranslationZ = a.getDimension(attr, 0f);
                    break;
                case com.android.internal.R.styleable.View_scaleX:
                    hostView.mScaleX = a.getFloat(attr, 1.0f);
                    break;
                case com.android.internal.R.styleable.View_scaleY:
                    hostView.mScaleY = a.getFloat(attr, 1.0f);
                    break;
                case com.android.internal.R.styleable.View_rotation:
                    hostView.mRotation = a.getFloat(attr, 0f);
                    break;
                case com.android.internal.R.styleable.View_onClick:
                    final String handlerName = a.getString(attr);
                    if (handlerName != null) hostView.setOnClickListener(new DeclaredOnClickListener(hostView, handlerName));
                    break;
                default:
                    break;
            }
        }
        if (padding >= 0) {
            leftPadding = topPadding = rightPadding = bottomPadding = padding;
        }
        if (paddingHorizontal >= 0) leftPadding = rightPadding = paddingHorizontal;
        if (paddingVertical >= 0) topPadding = bottomPadding = paddingVertical;
        if (startPadding >= 0) leftPadding = startPadding;
        if (endPadding >= 0) rightPadding = endPadding;
        hostView.mPaddingLeft = leftPadding >= 0 ? leftPadding : hostView.mPaddingLeft;
        hostView.mPaddingTop = topPadding >= 0 ? topPadding : hostView.mPaddingTop;
        hostView.mPaddingRight = rightPadding >= 0 ? rightPadding : hostView.mPaddingRight;
        hostView.mPaddingBottom = bottomPadding >= 0 ? bottomPadding : hostView.mPaddingBottom;
        if (fitsSystemWindows) hostView.mFitsSystemWindows = true;
    }

    static View findViewById(View hostView, int id) {
        if (id == View.NO_ID) return null;
        return hostView.mId == id ? hostView : null;
    }

    static View findViewWithTag(View hostView, Object tag) {
        if (tag == null) return null;
        return tag.equals(hostView.mTag) ? hostView : null;
    }

    static View getRootView(View hostView) {
        if (hostView.mParent instanceof View) {
            return ((View) hostView.mParent).getRootView();
        }
        return hostView;
    }

    static void layout(View hostView, int l, int t, int r, int b) {
        final boolean changed = (l != hostView.mLeft) || (t != hostView.mTop) || (r != hostView.mRight) || (b != hostView.mBottom);
        final boolean layoutNeeded = changed || hostView.mLayoutRequired || hostView.mLayoutRequested;
        final int oldLeft = hostView.mLeft, oldTop = hostView.mTop, oldRight = hostView.mRight, oldBottom = hostView.mBottom;
        final int oldWidth = hostView.mRight - hostView.mLeft;
        final int oldHeight = hostView.mBottom - hostView.mTop;
        hostView.mLeft = l;
        hostView.mTop = t;
        hostView.mRight = r;
        hostView.mBottom = b;
        hostView.mLaidOut = true;
        if (layoutNeeded) {
            hostView.mLayoutRequested = false;
            hostView.mLayoutRequired = false;
        }
        final int width = r - l;
        final int height = b - t;
        if (width != oldWidth || height != oldHeight) {
            hostView.onSizeChanged(width, height, oldWidth, oldHeight);
        }
        if (layoutNeeded) {
            hostView.mInLayout = true;
            try {
                hostView.onLayout(changed, l, t, r, b);
            } finally {
                hostView.mInLayout = false;
            }
        }
        if (changed && hostView.mLayoutChangeListeners != null) {
            OnLayoutChangeListener[] listeners = hostView.mLayoutChangeListeners.toArray(
                    new OnLayoutChangeListener[hostView.mLayoutChangeListeners.size()]);
            for (OnLayoutChangeListener listener : listeners) {
                listener.onLayoutChange(hostView, l, t, r, b, oldLeft, oldTop, oldRight, oldBottom);
            }
        }
    }

    static void setVisibility(View hostView, int visibility) {
        if (hostView.mVisibility == visibility) return;
        final int old = hostView.mVisibility;
        hostView.mVisibility = visibility;
        if ((old == View.GONE) != (visibility == View.GONE)) {
            hostView.requestLayout();
        }
        hostView.invalidate();
        hostView.dispatchVisibilityChanged(hostView, visibility);
        if (hostView.mAttachedToWindow) {
            hostView.dispatchVisibilityAggregated(hostView.isShown());
        }
    }

    static void setParent(View hostView, ViewGroup parent) {
        hostView.mParent = parent;
        hostView.resolveRtlPropertiesIfNeeded();
    }

    static void setOnClickListener(View hostView, OnClickListener l) {
        if (!hostView.mClickable) hostView.mClickable = true;
        hostView.mOnClickListener = l;
    }

    static void setOnLongClickListener(View hostView, OnLongClickListener l) {
        if (!hostView.mLongClickable) hostView.mLongClickable = true;
        hostView.mOnLongClickListener = l;
    }

    static void setFocus(View hostView, boolean hasFocus) {
        if (hostView.mHasFocus == hasFocus) return;
        if (hasFocus) {
            // Focus is exclusive: the previous owner loses it before this view gains it.
            final View previous = View.sFocusedView;
            View.sFocusedView = hostView;
            if (previous != null && previous != hostView) previous.setFocus(false);
        } else if (View.sFocusedView == hostView) {
            View.sFocusedView = null;
        }
        hostView.mHasFocus = hasFocus;
        hostView.refreshDrawableState();
        hostView.onFocusChanged(hasFocus, 0, null);
        if (hostView.mOnFocusChangeListener != null) {
            hostView.mOnFocusChangeListener.onFocusChange(hostView, hasFocus);
        }
    }

    static boolean dispatchKeyEvent(View hostView, KeyEvent event) {
        if (event == null) return false;
        if (hostView.mOnKeyListener != null &&
            hostView.mOnKeyListener.onKey(hostView, event.getKeyCode(), event)) {
            return true;
        }
        return event.dispatch(hostView, hostView.getKeyDispatcherState(), hostView);
    }

    static KeyEvent.DispatcherState getKeyDispatcherState(View hostView) {
        Window window = hostView.getRootView().mTraversalWindow;
        return hostView.mAttachedToWindow && window != null ? window.getKeyDispatcherState() : null;
    }

    static void setSystemUiVisibility(View hostView, int visibility) {
        if (hostView.mSystemUiVisibility == visibility) return;
        hostView.mSystemUiVisibility = visibility;
        hostView.requestApplyInsets();
        if (hostView.mOnSystemUiVisibilityChangeListener != null) {
            hostView.mOnSystemUiVisibilityChangeListener.onSystemUiVisibilityChange(visibility);
        }
    }

    static void setOnSystemUiVisibilityChangeListener(View hostView, 
            OnSystemUiVisibilityChangeListener l) {
        hostView.mOnSystemUiVisibilityChangeListener = l;
    }

    static void setLayoutParams(View hostView, ViewGroup.LayoutParams params) {
        if (params == null) throw new NullPointerException("Layout parameters cannot be null");
        hostView.mLayoutParams = params;
        hostView.resolveLayoutParams();
        hostView.requestLayout();
    }

    static void setLayoutParamsNoRequest(View hostView, ViewGroup.LayoutParams params) {
        hostView.mLayoutParams = params;
        hostView.resolveLayoutParams();
    }

    static boolean resolveRtlPropertiesIfNeeded(View hostView) {
        final int direction = hostView.getLayoutDirection();
        if (hostView.mResolvedLayoutDirection == direction) return false;
        hostView.mResolvedLayoutDirection = direction;
        hostView.mMeasureValid = false;
        hostView.mLayoutRequested = true;
        hostView.resolveLayoutParams();
        hostView.onRtlPropertiesChanged(direction);
        return true;
    }

    static android.view.inputmethod.InputConnection onCreateInputConnection(View hostView, 
            android.view.inputmethod.EditorInfo outAttrs) {
        return null;
    }

    static boolean requestFocus(View hostView) {
        hostView.setFocus(true);
        return true;
    }

    static void setFocusableInTouchMode(View hostView, boolean focusable) {
        hostView.mFocusableInTouchMode = focusable;
        if (focusable) hostView.mFocusable = true;
    }

    static ViewTreeObserver getViewTreeObserver(View hostView) {
        if (hostView.mParent instanceof View) return ((View) hostView.mParent).getViewTreeObserver();
        if (hostView.mViewTreeObserver == null) hostView.mViewTreeObserver = new ViewTreeObserver();
        return hostView.mViewTreeObserver;
    }

    static boolean dispatchTouchEvent(View hostView, MotionEvent event) {
        if (hostView.mVisibility != View.VISIBLE) return false;
        // OnTouchListener runs BEFORE onTouchEvent and has event consumption —
        // This order is Android's contract, the app relies on it to block clicks.
        if (hostView.mOnTouchListener != null && hostView.mOnTouchListener.onTouch(hostView, event)) {
            return true;
        }
        return hostView.onTouchEvent(event);
    }

    static boolean onTouchEvent(View hostView, MotionEvent event) {
        if (event == null) return false;
        final int action = event.getAction();
        if (action == MotionEvent.ACTION_DOWN) {
            hostView.mTouchDownTime = android.os.SystemClock.uptimeMillis();
            hostView.mLongClickFired = false;
            return hostView.mClickable || hostView.mLongClickable;
        }
        if (action == MotionEvent.ACTION_MOVE) {
            if (!hostView.mLongClickFired && hostView.mOnLongClickListener != null && hostView.mTouchDownTime > 0 &&
                android.os.SystemClock.uptimeMillis() - hostView.mTouchDownTime >= View.LONG_PRESS_TIMEOUT) {
                hostView.mLongClickFired = true;
                return hostView.performLongClick();
            }
            return hostView.mClickable || hostView.mLongClickable;
        }
        if (action == MotionEvent.ACTION_UP) {
            final long held = hostView.mTouchDownTime > 0
                    ? android.os.SystemClock.uptimeMillis() - hostView.mTouchDownTime : 0;
            hostView.mTouchDownTime = 0;
            if (hostView.mLongClickFired) return true;
            if (hostView.mOnLongClickListener != null && held >= View.LONG_PRESS_TIMEOUT) {
                return hostView.performLongClick();
            }
            if (hostView.mOnClickListener != null) {
                hostView.performClick();
                return true;
            }
            return hostView.mLongClickable;
        }
        return hostView.mClickable || hostView.mLongClickable;
    }

    static boolean performClick(View hostView) {
        if (hostView.mOnClickListener != null) {
            hostView.mOnClickListener.onClick(hostView);
            return true;
        }
        return false;
    }

    static boolean performLongClick(View hostView) {
        if (hostView.mOnLongClickListener != null) {
            return hostView.mOnLongClickListener.onLongClick(hostView);
        }
        return false;
    }

    static void onMeasure(View hostView, int widthMeasureSpec, int heightMeasureSpec) {
        hostView.setMeasuredDimension(View.getDefaultSize(hostView.getSuggestedMinimumWidth(), widthMeasureSpec),
                             View.getDefaultSize(hostView.getSuggestedMinimumHeight(), heightMeasureSpec));
    }

    static void measure(View hostView, int widthMeasureSpec, int heightMeasureSpec) {
        hostView.resolveRtlPropertiesIfNeeded();
        if (hostView.mMeasureValid && !hostView.mLayoutRequested
                && widthMeasureSpec == hostView.mOldWidthMeasureSpec
                && heightMeasureSpec == hostView.mOldHeightMeasureSpec) return;
        hostView.onMeasure(widthMeasureSpec, heightMeasureSpec);
        hostView.mOldWidthMeasureSpec = widthMeasureSpec;
        hostView.mOldHeightMeasureSpec = heightMeasureSpec;
        hostView.mMeasureValid = true;
        hostView.mLayoutRequired = true;
    }

    static void setMeasuredDimension(View hostView, int measuredWidth, int measuredHeight) {
        hostView.mMeasuredWidth = measuredWidth;
        hostView.mMeasuredHeight = measuredHeight;
    }

    static int getDefaultSize(int size, int measureSpec) {
        final int specMode = MeasureSpec.getMode(measureSpec);
        final int specSize = MeasureSpec.getSize(measureSpec);
        switch (specMode) {
            case MeasureSpec.AT_MOST:
            case MeasureSpec.EXACTLY:
                return specSize;
            default:
                return size;
        }
    }
}
