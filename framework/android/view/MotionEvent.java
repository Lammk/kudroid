package android.view;

/**
 * Minimal android.view.MotionEvent implementation.
 *
 * Represents a touch/motion event. For KuDroid's minimal framework, stores
 * action, coordinates, and pointer info.
 */
public final class MotionEvent {
    /** Action: down. */
    public static final int ACTION_DOWN = 0;
    /** Action: up. */
    public static final int ACTION_UP = 1;
    /** Action: move. */
    public static final int ACTION_MOVE = 2;
    /** Action: cancel. */
    public static final int ACTION_CANCEL = 3;
    /** Action: outside. */
    public static final int ACTION_OUTSIDE = 4;
    /** Action mask. */
    public static final int ACTION_MASK = 0xff;

    private final int mAction;
    private final float mX;
    private final float mY;
    private final long mEventTime;
    private final int mPointerCount;

    private MotionEvent(int action, float x, float y, long eventTime, int pointerCount) {
        mAction = action;
        mX = x;
        mY = y;
        mEventTime = eventTime;
        mPointerCount = pointerCount;
    }

    /**
     * Obtain a motion event.
     */
    public static MotionEvent obtain(int action, float x, float y, long eventTime) {
        return new MotionEvent(action, x, y, eventTime, 1);
    }

    /**
     * Return the action.
     */
    public int getAction() {
        return mAction;
    }

    /**
     * Return the X coordinate.
     */
    public float getX() {
        return mX;
    }

    /**
     * Return the X coordinate for a pointer.
     */
    public float getX(int pointerIndex) {
        return mX;
    }

    /**
     * Return the Y coordinate.
     */
    public float getY() {
        return mY;
    }

    /**
     * Return the Y coordinate for a pointer.
     */
    public float getY(int pointerIndex) {
        return mY;
    }

    /**
     * Return the event time.
     */
    public long getEventTime() {
        return mEventTime;
    }

    /**
     * Return the down time.
     */
    public long getDownTime() {
        return mEventTime;
    }

    /**
     * Return the number of pointers.
     */
    public int getPointerCount() {
        return mPointerCount;
    }

    /**
     * Recycle the event.
     */
    public void recycle() {
    }
}
