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
        return false;
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
        return FLAG_SOFT_KEYBOARD;
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
