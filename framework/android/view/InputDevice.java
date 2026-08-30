package android.view;

/**
 * android.view.InputDevice — describes where an input event came from.
 *
 * Only the SOURCE_ constants are real here. They are what MotionEvent.getSource() and
 * KeyEvent.getSource() return, and native input code branches on them: AGDK routes an
 * event to its touch handler or its key handler by testing these bits, so a wrong value
 * means a correctly delivered event is ignored. The numbering is Android's and cannot be
 * chosen freely.
 */
public class InputDevice {
    // Class bits: the low byte says what kind of axes the source has.
    public static final int SOURCE_CLASS_MASK = 0x000000ff;
    public static final int SOURCE_CLASS_NONE = 0x00000000;
    public static final int SOURCE_CLASS_BUTTON = 0x00000001;
    public static final int SOURCE_CLASS_POINTER = 0x00000002;
    public static final int SOURCE_CLASS_TRACKBALL = 0x00000004;
    public static final int SOURCE_CLASS_POSITION = 0x00000008;
    public static final int SOURCE_CLASS_JOYSTICK = 0x00000010;

    public static final int SOURCE_UNKNOWN = 0x00000000;
    public static final int SOURCE_KEYBOARD = 0x00000100 | SOURCE_CLASS_BUTTON;
    public static final int SOURCE_DPAD = 0x00000200 | SOURCE_CLASS_BUTTON;
    public static final int SOURCE_GAMEPAD = 0x00000400 | SOURCE_CLASS_BUTTON;
    public static final int SOURCE_TOUCHSCREEN = 0x00001000 | SOURCE_CLASS_POINTER;
    public static final int SOURCE_MOUSE = 0x00002000 | SOURCE_CLASS_POINTER;
    public static final int SOURCE_STYLUS = 0x00004000 | SOURCE_CLASS_POINTER;
    public static final int SOURCE_TRACKBALL = 0x00010000 | SOURCE_CLASS_TRACKBALL;
    public static final int SOURCE_TOUCHPAD = 0x00100000 | SOURCE_CLASS_POSITION;
    public static final int SOURCE_JOYSTICK = 0x01000000 | SOURCE_CLASS_JOYSTICK;

    public static final int KEYBOARD_TYPE_NONE = 0;
    public static final int KEYBOARD_TYPE_NON_ALPHABETIC = 1;
    public static final int KEYBOARD_TYPE_ALPHABETIC = 2;

    public InputDevice() {}

    /** No enumerable input devices: KuDroid synthesises events rather than owning devices. */
    public static int[] getDeviceIds() {
        return new int[0];
    }

    public static InputDevice getDevice(int id) {
        return null;
    }

    public int getId() {
        return 0;
    }

    public String getName() {
        return "KuDroid Touchscreen";
    }

    public int getSources() {
        return SOURCE_TOUCHSCREEN;
    }

    /** A soft keyboard only, which is what KEYBOARD_TYPE_NONE means on Android. */
    public int getKeyboardType() {
        return KEYBOARD_TYPE_NONE;
    }

    public boolean isVirtual() {
        return false;
    }
}
