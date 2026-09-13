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

    // Axis codes are Android's; native input code looks them up by number.
    public static final int AXIS_X = 0;
    public static final int AXIS_Y = 1;
    public static final int AXIS_PRESSURE = 2;
    public static final int AXIS_SIZE = 3;
    public static final int AXIS_TOUCH_MAJOR = 4;
    public static final int AXIS_TOUCH_MINOR = 5;
    public static final int AXIS_TOOL_MAJOR = 6;
    public static final int AXIS_TOOL_MINOR = 7;
    public static final int AXIS_ORIENTATION = 8;

    private static final InputDevice sTouchDevice = new InputDevice(1, "KuDroid Touchscreen", SOURCE_TOUCHSCREEN);

    private final int mId;
    private final String mName;
    private final int mSources;

    public InputDevice() {
        this(1, "KuDroid Touchscreen", SOURCE_TOUCHSCREEN);
    }

    public InputDevice(int id, String name, int sources) {
        mId = id;
        mName = name;
        mSources = sources;
    }

    /** Return touchscreen device ID 1. */
    public static int[] getDeviceIds() {
        return new int[] { 1 };
    }

    public static InputDevice getDevice(int id) {
        if (id == 1 || id == 0) {
            return sTouchDevice;
        }
        return null;
    }

    public int getId() {
        return mId;
    }

    public String getName() {
        return mName;
    }

    public int getSources() {
        return mSources;
    }

    /** A soft keyboard only, which is what KEYBOARD_TYPE_NONE means on Android. */
    public int getKeyboardType() {
        return KEYBOARD_TYPE_NONE;
    }

    public boolean isVirtual() {
        return false;
    }

    /**
     * The character map Unity's input init reads through. Its absence was a
     * NoSuchMethodError on first device query, aborting the engine's input
     * setup so later touch dispatches were dropped whole (live: 158 injects
     * with zero movement in-game). An empty map is what a keyboard-less
     * touchscreen answers.
     */
    public android.view.KeyCharacterMap getKeyCharacterMap() {
        return android.view.KeyCharacterMap.load(KeyCharacterMap.BUILT_IN_KEYBOARD);
    }

    /** Which of the given key codes exist on this device: all, as the AOSP
     *  virtual-device answer — callers index the result per input code. */
    public boolean[] hasKeys(int... keyCodes) {
        boolean[] result = new boolean[keyCodes == null ? 0 : keyCodes.length];
        java.util.Arrays.fill(result, true);
        return result;
    }

    /**
     * android.view.InputDevice$MotionRange — native input code enumerates this to
     * decide whether a device is a usable touchscreen. Without it (missing method
     * or empty ranges) Unity concludes there is no touchscreen and drops every
     * touch its own injectEvent receives. Bounds come from the default display so
     * they match the pixel space MotionEvent coordinates live in.
     */
    public static final class MotionRange {
        private final int mAxis;
        private final int mSource;
        private final float mMin;
        private final float mMax;
        private final float mFlat;
        private final float mFuzz;

        MotionRange(int axis, int source, float min, float max, float flat, float fuzz) {
            mAxis = axis;
            mSource = source;
            mMin = min;
            mMax = max;
            mFlat = flat;
            mFuzz = fuzz;
        }

        public int getAxis() {
            return mAxis;
        }

        public int getSource() {
            return mSource;
        }

        public float getMin() {
            return mMin;
        }

        public float getMax() {
            return mMax;
        }

        public float getRange() {
            return mMax - mMin;
        }

        public float getFlat() {
            return mFlat;
        }

        public float getFuzz() {
            return mFuzz;
        }

        public float getResolution() {
            return 0;
        }
    }

    private static float sRangeMaxX = -1;
    private static float sRangeMaxY = -1;

    private static float rangeMaxX() {
        if (sRangeMaxX <= 0) {
            try {
                sRangeMaxX = new Display().getWidth();
            } catch (Throwable ignored) {
                sRangeMaxX = 1080;
            }
            if (sRangeMaxX <= 0) sRangeMaxX = 1080;
        }
        return sRangeMaxX;
    }

    private static float rangeMaxY() {
        if (sRangeMaxY <= 0) {
            try {
                sRangeMaxY = new Display().getHeight();
            } catch (Throwable ignored) {
                sRangeMaxY = 1920;
            }
            if (sRangeMaxY <= 0) sRangeMaxY = 1920;
        }
        return sRangeMaxY;
    }

    public MotionRange getMotionRange(int axis) {
        return getMotionRange(axis, mSources);
    }

    public MotionRange getMotionRange(int axis, int source) {
        switch (axis) {
            case AXIS_X:
                return new MotionRange(axis, source, 0, rangeMaxX(), 0, 0);
            case AXIS_Y:
                return new MotionRange(axis, source, 0, rangeMaxY(), 0, 0);
            case AXIS_PRESSURE:
            case AXIS_SIZE:
                return new MotionRange(axis, source, 0, 1, 0, 0);
            case AXIS_TOUCH_MAJOR:
            case AXIS_TOUCH_MINOR:
            case AXIS_TOOL_MAJOR:
            case AXIS_TOOL_MINOR:
                return new MotionRange(axis, source, 0, rangeMaxX() > rangeMaxY()
                        ? rangeMaxX() : rangeMaxY(), 0, 0);
            default:
                return null;
        }
    }

    public MotionRange[] getMotionRanges() {
        return new MotionRange[] {
            getMotionRange(AXIS_X),
            getMotionRange(AXIS_Y),
            getMotionRange(AXIS_PRESSURE),
        };
    }

    public int getProductId() {
        return 1;
    }

    public int getVendorId() {
        return 1;
    }

    public String getDescriptor() {
        return "KuDroid Touchscreen";
    }
}
