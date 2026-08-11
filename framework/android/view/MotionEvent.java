package android.view;

/**
 * triển khai android.view.motionevent tối thiểu.
 *
 * đại diện cho một sự kiện chạm/chuyển động. đối với khuôn khổ tối thiểu của kudroid, lưu trữ
 * hành động, tọa độ và thông tin con trỏ.
 */
public final class MotionEvent {
    /** hành động: xuống. */
    public static final int ACTION_DOWN = 0;
    /** hành động: lên. */
    public static final int ACTION_UP = 1;
    /** hành động: di chuyển. */
    public static final int ACTION_MOVE = 2;
    /** hành động: hủy bỏ. */
    public static final int ACTION_CANCEL = 3;
    /** hành động: bên ngoài. */
    public static final int ACTION_OUTSIDE = 4;
    /** mặt nạ hành động. */
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
     * lấy một sự kiện chuyển động.
     */
    public static MotionEvent obtain(int action, float x, float y, long eventTime) {
        return new MotionEvent(action, x, y, eventTime, 1);
    }

    /**
     * trả về hành động.
     */
    public int getAction() {
        return mAction;
    }

    /**
     * trả về tọa độ x.
     */
    public float getX() {
        return mX;
    }

    /**
     * trả về tọa độ x cho một con trỏ.
     */
    public float getX(int pointerIndex) {
        return mX;
    }

    /**
     * trả về tọa độ y.
     */
    public float getY() {
        return mY;
    }

    /**
     * trả về tọa độ y cho một con trỏ.
     */
    public float getY(int pointerIndex) {
        return mY;
    }

    /**
     * trả về thời gian sự kiện.
     */
    public long getEventTime() {
        return mEventTime;
    }

    /**
     * trả về thời gian xuống.
     */
    public long getDownTime() {
        return mEventTime;
    }

    /**
     * trả về số lượng con trỏ.
     */
    public int getPointerCount() {
        return mPointerCount;
    }

    /**
     * tái chế sự kiện.
     */
    public void recycle() {
    }
}
