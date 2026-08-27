package android.view;

/**
 * minimal android.view.motionevent implementation.
 *
 * represents a touch/motion event. for kudroid minimal framework, archive
 * actions, coordinates and cursor information.
 */
public final class MotionEvent {
    /** action: down. */
    public static final int ACTION_DOWN = 0;
    /** action: up. */
    public static final int ACTION_UP = 1;
    /** action: move. */
    public static final int ACTION_MOVE = 2;
    /** action: cancel. */
    public static final int ACTION_CANCEL = 3;
    /** action: external. */
    public static final int ACTION_OUTSIDE = 4;
    /** action mask. */
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
     * get a motion event.
     */
    public static MotionEvent obtain(int action, float x, float y, long eventTime) {
        return new MotionEvent(action, x, y, eventTime, 1);
    }

    /**
     * returns action.
     */
    public int getAction() {
        return mAction;
    }

    /**
     * returns x coordinates.
     */
    public float getX() {
        return mX;
    }

    /**
     * returns the x coordinate for a pointer.
     */
    public float getX(int pointerIndex) {
        return mX;
    }

    /**
     * returns the y coordinate.
     */
    public float getY() {
        return mY;
    }

    /**
     * returns the y coordinate for a pointer.
     */
    public float getY(int pointerIndex) {
        return mY;
    }

    /**
     * returns event time.
     */
    public long getEventTime() {
        return mEventTime;
    }

    /**
     * returns down time.
     */
    public long getDownTime() {
        return mEventTime;
    }

    /**
     * returns the number of pointers.
     */
    public int getPointerCount() {
        return mPointerCount;
    }

    /**
     * event recycling.
     */
    public void recycle() {
    }
}
