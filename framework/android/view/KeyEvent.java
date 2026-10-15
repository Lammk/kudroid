package android.view;

/**
 * android.view.KeyEvent — keyboard / hard key events.
 *
 * Like MotionEvent, most of the getters below exist for native callers rather than Java
 * ones: AGDK resolves them by name and calls them to fill its GameActivityKeyEvent, so a
 * missing one throws NoSuchMethodError during Activity creation and stops onCreate before
 * a surface exists. Seven were missing, which is what left Minecraft on a black screen.
 */
public class KeyEvent extends InputEvent {
    /** Action: key is pressed down. */
    public static final int ACTION_DOWN = 0;
    /** Action: key is released. */
    public static final int ACTION_UP = 1;
    /** Action: multiple events combined. */
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

    public static final int META_SHIFT_ON = 0x1;
    public static final int META_ALT_ON = 0x2;
    public static final int META_SYM_ON = 0x4;
    public static final int META_CTRL_ON = 0x1000;
    public static final int META_META_ON = 0x10000;

    public static final int FLAG_SOFT_KEYBOARD = 0x2;

    /**
     * Device ID reported for every key event.
     *
     * Non-zero for the same reason as MotionEvent's: zero identifies the virtual device on
     * Android, and input code that skips synthetic events would drop everything. Distinct
     * from the touchscreen's ID so a guest tracking devices sees two consistent sources
     * rather than one device producing both.
     */
    private static final int KEYBOARD_DEVICE_ID = 2;

    private final int mAction;
    private final int mKeyCode;
    private final int mRepeatCount;
    private final long mEventTime;
    private final int mMetaState;
    private final int mUnicodeChar;

    private int mFlags = FLAG_SOFT_KEYBOARD;

    public KeyEvent(int action, int keyCode) {
        this(action, keyCode, 0);
    }

    public KeyEvent(int action, int keyCode, int repeatCount) {
        this(action, keyCode, repeatCount, 0, 0);
    }

    public KeyEvent(int action, int keyCode, int repeatCount, int metaState, int unicodeChar) {
        mAction = action;
        mKeyCode = keyCode;
        mRepeatCount = repeatCount;
        mMetaState = metaState;
        mUnicodeChar = unicodeChar;
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
        return (mFlags & FLAG_CANCELED) != 0;
    }

    public int getDeviceId() {
        return KEYBOARD_DEVICE_ID;
    }

    /**
     * Reported as the soft keyboard, because that is what it is: text arrives from iOS's
     * on-screen keyboard through the InputConnection, not from a physical device.
     */
    public int getSource() {
        return InputDevice.SOURCE_KEYBOARD;
    }

    public int getFlags() {
        return mFlags;
    }

    public int getMetaState() {
        return mMetaState;
    }

    /** The modifier subset of the meta state, which is what callers testing modifiers want. */
    public int getModifiers() {
        return mMetaState & (META_SHIFT_ON | META_ALT_ON | META_CTRL_ON | META_META_ON | META_SYM_ON);
    }

    /**
     * Hardware scan code.
     *
     * Zero, which Android uses for events with no physical origin. A fabricated code would
     * be worse than none: guests map scan codes through keyboard layouts, and a wrong one
     * produces a wrong character rather than no character.
     */
    public int getScanCode() {
        return 0;
    }

    /**
     * The character this key produces, or 0 for a key that produces none.
     *
     * Carried on the event rather than derived from the key code: the key code space cannot
     * express the characters an iOS keyboard sends, so the character is passed in
     * alongside it.
     */
    public int getUnicodeChar() {
        return mUnicodeChar;
    }

    public int getUnicodeChar(int metaState) {
        return mUnicodeChar;
    }

    public static final int FLAG_CANCELED = 0x20;
    public static final int FLAG_TRACKING = 0x200;
    public static final int FLAG_LONG_PRESS = 0x80;
    public static final int FLAG_START_TRACKING = 0x40000000;

    public interface Callback {
        boolean onKeyDown(int keyCode, KeyEvent event);
        boolean onKeyLongPress(int keyCode, KeyEvent event);
        boolean onKeyUp(int keyCode, KeyEvent event);
        boolean onKeyMultiple(int keyCode, int count, KeyEvent event);
    }

    public static class DispatcherState {
        private int mDownKeyCode;
        private Object mDownTarget;
        private final android.util.SparseIntArray mActiveLongPresses = new android.util.SparseIntArray();

        public void reset() {
            mDownKeyCode = 0;
            mDownTarget = null;
            mActiveLongPresses.clear();
        }

        public void reset(Object target) {
            if (mDownTarget == target) {
                mDownKeyCode = 0;
                mDownTarget = null;
            }
        }

        public void startTracking(KeyEvent event, Object target) {
            if (event.getAction() != ACTION_DOWN) {
                throw new IllegalArgumentException("Can only start tracking on a down event");
            }
            mDownKeyCode = event.getKeyCode();
            mDownTarget = target;
        }

        public boolean isTracking(KeyEvent event) {
            return mDownKeyCode == event.getKeyCode();
        }

        public void performedLongPress(KeyEvent event) {
            mActiveLongPresses.put(event.getKeyCode(), 1);
        }

        public void handleUpEvent(KeyEvent event) {
            final int keyCode = event.getKeyCode();
            mActiveLongPresses.delete(keyCode);
            if (mDownKeyCode == keyCode) {
                mDownKeyCode = 0;
                mDownTarget = null;
            }
        }
    }

    public void startTracking() {
        mFlags |= FLAG_START_TRACKING;
    }

    public boolean isTracking() {
        return (mFlags & FLAG_TRACKING) != 0;
    }

    public boolean isLongPress() {
        return (mFlags & FLAG_LONG_PRESS) != 0;
    }

    public final boolean dispatch(Callback receiver, DispatcherState state, Object target) {
        switch (mAction) {
            case ACTION_DOWN: {
                mFlags &= ~FLAG_START_TRACKING;
                boolean res = receiver.onKeyDown(mKeyCode, this);
                if (state != null) {
                    if (res && mRepeatCount == 0 && (mFlags & FLAG_START_TRACKING) != 0) {
                        state.startTracking(this, target);
                    } else if (isLongPress() && state.isTracking(this)) {
                        try {
                            if (receiver.onKeyLongPress(mKeyCode, this)) {
                                state.performedLongPress(this);
                                res = true;
                            }
                        } catch (AbstractMethodError ignored) {
                        }
                    }
                }
                return res;
            }
            case ACTION_UP:
                if (state != null) {
                    state.handleUpEvent(this);
                }
                return receiver.onKeyUp(mKeyCode, this);
            case ACTION_MULTIPLE:
                final int count = mRepeatCount;
                final int code = mKeyCode;
                if (receiver.onKeyMultiple(code, count, this)) {
                    return true;
                }
                return false;
        }
        return false;
    }

    public boolean isShiftPressed() {
        return (mMetaState & META_SHIFT_ON) != 0;
    }

    public boolean isAltPressed() {
        return (mMetaState & META_ALT_ON) != 0;
    }

    public boolean isCtrlPressed() {
        return (mMetaState & META_CTRL_ON) != 0;
    }

    public boolean isSystem() {
        return mKeyCode == KEYCODE_HOME || mKeyCode == KEYCODE_BACK || mKeyCode == KEYCODE_MENU;
    }

    @Override
    public String toString() {
        return "KeyEvent{action=" + mAction + " keyCode=" + mKeyCode + "}";
    }
}
