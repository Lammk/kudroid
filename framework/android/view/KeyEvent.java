package android.view;

/**
 * android.view.KeyEvent — sự kiện bàn phím / phím cứng.
 */
public class KeyEvent {
    /** Hành động: phím được nhấn xuống. */
    public static final int ACTION_DOWN = 0;
    /** Hành động: phím được nhả ra. */
    public static final int ACTION_UP = 1;
    /** Hành động: nhiều sự kiện gộp. */
    public static final int ACTION_MULTIPLE = 2;

    public static final int KEYCODE_UNKNOWN = 0;
    public static final int KEYCODE_BACK = 4;
    public static final int KEYCODE_HOME = 3;
    public static final int KEYCODE_MENU = 82;
    public static final int KEYCODE_SEARCH = 84;
    public static final int KEYCODE_ENTER = 66;
    public static final int KEYCODE_DEL = 67;
    public static final int KEYCODE_VOLUME_UP = 24;
    public static final int KEYCODE_VOLUME_DOWN = 25;
    public static final int KEYCODE_DPAD_UP = 19;
    public static final int KEYCODE_DPAD_DOWN = 20;
    public static final int KEYCODE_DPAD_LEFT = 21;
    public static final int KEYCODE_DPAD_RIGHT = 22;
    public static final int KEYCODE_DPAD_CENTER = 23;

    private final int mAction;
    private final int mKeyCode;
    private final int mRepeatCount;
    private final long mEventTime;

    public KeyEvent(int action, int keyCode) {
        this(action, keyCode, 0);
    }

    public KeyEvent(int action, int keyCode, int repeatCount) {
        mAction = action;
        mKeyCode = keyCode;
        mRepeatCount = repeatCount;
        mEventTime = android.os.SystemClock.uptimeMillis();
    }

    public int getAction() {
        return mAction;
    }

    public int getKeyCode() {
        return mKeyCode;
    }

    public int getRepeatCount() {
        return mRepeatCount;
    }

    public long getEventTime() {
        return mEventTime;
    }

    public long getDownTime() {
        return mEventTime;
    }

    public boolean isCanceled() {
        return false;
    }

    @Override
    public String toString() {
        return "KeyEvent{action=" + mAction + " keyCode=" + mKeyCode + "}";
    }
}
