package android.view;

import android.content.Context;

/** Touch and timing constants, scaled by the display density like the platform's. */
public class ViewConfiguration {
    private static final int SCROLL_BAR_SIZE = 4;
    private static final int SCROLL_BAR_FADE_DURATION = 250;
    private static final int SCROLL_BAR_DEFAULT_DELAY = 300;
    private static final int FADING_EDGE_LENGTH = 12;
    private static final int PRESSED_STATE_DURATION = 64;
    private static final int DEFAULT_LONG_PRESS_TIMEOUT = 500;
    private static final int TAP_TIMEOUT = 100;
    private static final int JUMP_TAP_TIMEOUT = 500;
    private static final int DOUBLE_TAP_TIMEOUT = 300;
    private static final int DOUBLE_TAP_MIN_TIME = 40;
    private static final int HOVER_TAP_TIMEOUT = 150;
    private static final int HOVER_TAP_SLOP = 20;
    private static final int ZOOM_CONTROLS_TIMEOUT = 3000;
    private static final int EDGE_SLOP = 12;
    private static final int TOUCH_SLOP = 8;
    private static final int PAGING_TOUCH_SLOP = TOUCH_SLOP * 2;
    private static final int DOUBLE_TAP_TOUCH_SLOP = TOUCH_SLOP;
    private static final int DOUBLE_TAP_SLOP = 100;
    private static final int WINDOW_TOUCH_SLOP = 16;
    private static final int MINIMUM_FLING_VELOCITY = 50;
    private static final int MAXIMUM_FLING_VELOCITY = 8000;
    private static final int MAXIMUM_DRAWING_CACHE_SIZE = 480 * 800 * 4;
    private static final int OVERSCROLL_DISTANCE = 0;
    private static final int OVERFLING_DISTANCE = 6;
    private static final float SCROLL_FRICTION = 0.015f;
    private static final float AMBIGUOUS_GESTURE_MULTIPLIER = 2f;
    private static final int KEY_REPEAT_TIMEOUT = 400;
    private static final int KEY_REPEAT_DELAY = 50;
    private static final int GLOBAL_ACTIONS_KEY_TIMEOUT = 500;
    private static final int MULTI_PRESS_TIMEOUT = 300;

    private static final java.util.HashMap<Float, ViewConfiguration> sConfigurations =
            new java.util.HashMap<>();

    private final int mEdgeSlop;
    private final int mFadingEdgeLength;
    private final int mMinimumFlingVelocity;
    private final int mMaximumFlingVelocity;
    private final int mScrollbarSize;
    private final int mTouchSlop;
    private final int mHoverSlop;
    private final int mPagingTouchSlop;
    private final int mDoubleTapTouchSlop;
    private final int mDoubleTapSlop;
    private final int mWindowTouchSlop;
    private final int mMaximumDrawingCacheSize;
    private final int mOverscrollDistance;
    private final int mOverflingDistance;
    private final float mVerticalScrollFactor;
    private final float mHorizontalScrollFactor;

    /** @deprecated Use {@link #get(Context)}; this assumes a density of 1. */
    @Deprecated
    public ViewConfiguration() {
        this(1.0f);
    }

    private ViewConfiguration(float density) {
        mEdgeSlop = (int) (density * EDGE_SLOP + 0.5f);
        mFadingEdgeLength = (int) (density * FADING_EDGE_LENGTH + 0.5f);
        mScrollbarSize = (int) (density * SCROLL_BAR_SIZE + 0.5f);
        mTouchSlop = (int) (density * TOUCH_SLOP + 0.5f);
        mHoverSlop = (int) (density * (TOUCH_SLOP / 2f) + 0.5f);
        mPagingTouchSlop = (int) (density * PAGING_TOUCH_SLOP + 0.5f);
        mDoubleTapTouchSlop = (int) (density * DOUBLE_TAP_TOUCH_SLOP + 0.5f);
        mDoubleTapSlop = (int) (density * DOUBLE_TAP_SLOP + 0.5f);
        mWindowTouchSlop = (int) (density * WINDOW_TOUCH_SLOP + 0.5f);
        mMinimumFlingVelocity = (int) (density * MINIMUM_FLING_VELOCITY + 0.5f);
        mMaximumFlingVelocity = (int) (density * MAXIMUM_FLING_VELOCITY + 0.5f);
        mMaximumDrawingCacheSize = MAXIMUM_DRAWING_CACHE_SIZE;
        mOverscrollDistance = (int) (density * OVERSCROLL_DISTANCE + 0.5f);
        mOverflingDistance = (int) (density * OVERFLING_DISTANCE + 0.5f);
        mVerticalScrollFactor = 64f * density;
        mHorizontalScrollFactor = 64f * density;
    }

    public static ViewConfiguration get(Context context) {
        float density = 1.0f;
        if (context != null && context.getResources() != null
                && context.getResources().getDisplayMetrics() != null) {
            density = context.getResources().getDisplayMetrics().density;
            if (density <= 0f) density = 1.0f;
        }
        synchronized (sConfigurations) {
            ViewConfiguration configuration = sConfigurations.get(density);
            if (configuration == null) {
                configuration = new ViewConfiguration(density);
                sConfigurations.put(density, configuration);
            }
            return configuration;
        }
    }

    public static int getScrollBarSize() { return SCROLL_BAR_SIZE; }
    public static int getScrollBarFadeDuration() { return SCROLL_BAR_FADE_DURATION; }
    public static int getScrollDefaultDelay() { return SCROLL_BAR_DEFAULT_DELAY; }
    public static int getFadingEdgeLength() { return FADING_EDGE_LENGTH; }
    public static int getPressedStateDuration() { return PRESSED_STATE_DURATION; }
    public static int getLongPressTimeout() { return DEFAULT_LONG_PRESS_TIMEOUT; }

    /** Distance between two taps, in pixels, still counted as a double tap. */
    public static int getDoubleTapSlop() { return 100; }

    /** Factor applied to touch slop for an ambiguous gesture (long press versus scroll). */
    public static float getAmbiguousGestureMultiplier() { return 2f; }
    public static int getKeyRepeatTimeout() { return KEY_REPEAT_TIMEOUT; }
    public static int getKeyRepeatDelay() { return KEY_REPEAT_DELAY; }
    public static int getTapTimeout() { return TAP_TIMEOUT; }
    public static int getJumpTapTimeout() { return JUMP_TAP_TIMEOUT; }
    public static int getDoubleTapTimeout() { return DOUBLE_TAP_TIMEOUT; }
    public static int getDoubleTapMinTime() { return DOUBLE_TAP_MIN_TIME; }
    public static int getHoverTapTimeout() { return HOVER_TAP_TIMEOUT; }
    public static int getHoverTapSlop() { return HOVER_TAP_SLOP; }
    public static int getEdgeSlop() { return EDGE_SLOP; }
    public static int getTouchSlop() { return TOUCH_SLOP; }
    public static int getWindowTouchSlop() { return WINDOW_TOUCH_SLOP; }
    public static int getMinimumFlingVelocity() { return MINIMUM_FLING_VELOCITY; }
    public static int getMaximumFlingVelocity() { return MAXIMUM_FLING_VELOCITY; }
    public static int getMaximumDrawingCacheSize() { return MAXIMUM_DRAWING_CACHE_SIZE; }
    public static long getZoomControlsTimeout() { return ZOOM_CONTROLS_TIMEOUT; }
    public static long getGlobalActionKeyTimeout() { return GLOBAL_ACTIONS_KEY_TIMEOUT; }
    public static float getScrollFriction() { return SCROLL_FRICTION; }
    public static int getMultiPressTimeout() { return MULTI_PRESS_TIMEOUT; }

    public int getScaledScrollBarSize() { return mScrollbarSize; }
    public int getScaledFadingEdgeLength() { return mFadingEdgeLength; }
    public int getScaledEdgeSlop() { return mEdgeSlop; }
    public int getScaledTouchSlop() { return mTouchSlop; }
    public int getScaledHoverSlop() { return mHoverSlop; }
    public int getScaledDoubleTapTouchSlop() { return mDoubleTapTouchSlop; }
    public int getScaledPagingTouchSlop() { return mPagingTouchSlop; }
    public int getScaledDoubleTapSlop() { return mDoubleTapSlop; }
    public int getScaledWindowTouchSlop() { return mWindowTouchSlop; }
    public int getScaledMinimumFlingVelocity() { return mMinimumFlingVelocity; }
    public int getScaledMaximumFlingVelocity() { return mMaximumFlingVelocity; }
    public int getScaledMaximumDrawingCacheSize() { return mMaximumDrawingCacheSize; }
    public int getScaledOverscrollDistance() { return mOverscrollDistance; }
    public int getScaledOverflingDistance() { return mOverflingDistance; }
    public float getScaledVerticalScrollFactor() { return mVerticalScrollFactor; }
    public float getScaledHorizontalScrollFactor() { return mHorizontalScrollFactor; }
    public float getScaledAmbiguousGestureMultiplier() { return AMBIGUOUS_GESTURE_MULTIPLIER; }
    public boolean hasPermanentMenuKey() { return false; }
    public boolean shouldShowMenuShortcutsWhenKeyboardPresent() { return false; }
}
