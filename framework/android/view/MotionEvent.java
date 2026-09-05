package android.view;

/**
 * android.view.MotionEvent — touch and pointer events.
 *
 * The getters here are almost all read from native code rather than from Java. AGDK's
 * GameActivity resolves them by name in initializeNativeCode and calls them to fill its
 * own GameActivityMotionEvent struct, so a missing one is a NoSuchMethodError thrown
 * inside Activity creation — which aborts onCreate before the surface exists, and the app
 * then sits on a black screen with no crash. Twenty-five of these were missing at once for
 * exactly that reason.
 *
 * Values are those of a single-touch capacitive touchscreen, which is what KuDroid
 * presents. Where a concept has no counterpart on iOS (scan codes, edge flags, hardware
 * device IDs) the answer is the one Android gives for "none", not a guess: a guest reading
 * a fabricated non-zero value would act on it.
 */
public final class MotionEvent extends InputEvent {
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
    public static final int ACTION_POINTER_DOWN = 5;
    public static final int ACTION_POINTER_UP = 6;
    public static final int ACTION_HOVER_MOVE = 7;
    public static final int ACTION_SCROLL = 8;
    public static final int ACTION_HOVER_ENTER = 9;
    public static final int ACTION_HOVER_EXIT = 10;
    public static final int ACTION_BUTTON_PRESS = 11;
    public static final int ACTION_BUTTON_RELEASE = 12;
    /** action mask. */
    public static final int ACTION_MASK = 0xff;
    public static final int ACTION_POINTER_INDEX_MASK = 0xff00;
    public static final int ACTION_POINTER_INDEX_SHIFT = 8;

    // Axis identifiers, as passed to getAxisValue. AGDK reads pointer geometry through
    // that call rather than through individual getters, so these have to match Android's
    // numbering or it reads the wrong axis.
    public static final int AXIS_X = 0;
    public static final int AXIS_Y = 1;
    public static final int AXIS_PRESSURE = 2;
    public static final int AXIS_SIZE = 3;
    public static final int AXIS_TOUCH_MAJOR = 4;
    public static final int AXIS_TOUCH_MINOR = 5;
    public static final int AXIS_TOOL_MAJOR = 6;
    public static final int AXIS_TOOL_MINOR = 7;
    public static final int AXIS_ORIENTATION = 8;
    public static final int AXIS_VSCROLL = 9;
    public static final int AXIS_HSCROLL = 10;
    public static final int AXIS_RELATIVE_X = 27;
    public static final int AXIS_RELATIVE_Y = 28;

    public static final int TOOL_TYPE_UNKNOWN = 0;
    public static final int TOOL_TYPE_FINGER = 1;
    public static final int TOOL_TYPE_STYLUS = 2;
    public static final int TOOL_TYPE_MOUSE = 3;
    public static final int TOOL_TYPE_ERASER = 4;

    public static final int BUTTON_PRIMARY = 1;
    public static final int BUTTON_SECONDARY = 2;
    public static final int BUTTON_TERTIARY = 4;

    public static final int EDGE_TOP = 1;
    public static final int EDGE_BOTTOM = 2;
    public static final int EDGE_LEFT = 4;
    public static final int EDGE_RIGHT = 8;

    public static final int CLASSIFICATION_NONE = 0;
    public static final int CLASSIFICATION_AMBIGUOUS_GESTURE = 1;
    public static final int CLASSIFICATION_DEEP_PRESS = 2;

    /**
     * Device ID reported for every touch.
     *
     * Non-zero on purpose: zero means the virtual/synthetic device on Android, and input
     * code that filters synthetic events would discard everything KuDroid delivers. Any
     * stable positive value works, since nothing can look the device up.
     */
    private static final int TOUCHSCREEN_DEVICE_ID = 1;

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
     * Copy an event. Our touch pipeline (ActivityThread.postTouchEvent)
     * copies every event before dispatch; without this it died as
     * NoSuchMethodError and no touch ever reached the game.
     */
    public static MotionEvent obtain(MotionEvent other) {
        if (other == null) throw new IllegalArgumentException("other must not be null");
        return new MotionEvent(other.mAction, other.mX, other.mY, other.mEventTime,
                other.mPointerCount);
    }

    /**
     * returns action.
     */
    public int getAction() {
        return mAction;
    }

    /** Action with the pointer index stripped off. */
    public int getActionMasked() {
        return mAction & ACTION_MASK;
    }

    /** Index of the pointer this action refers to, encoded in the upper action bits. */
    public int getActionIndex() {
        return (mAction & ACTION_POINTER_INDEX_MASK) >> ACTION_POINTER_INDEX_SHIFT;
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
     * Coordinates relative to the screen rather than the view.
     *
     * The same values as getX/getY: KuDroid runs the guest activity full-screen with no
     * window offset, so the two frames coincide. They would differ only once a guest view
     * could be positioned inside a larger window.
     */
    public float getRawX() {
        return mX;
    }

    public float getRawY() {
        return mY;
    }

    public float getRawX(int pointerIndex) {
        return mX;
    }

    public float getRawY(int pointerIndex) {
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
     * Identifier that follows one finger across a gesture.
     *
     * Always 0, matching the single pointer this class carries. A guest tracking pointers
     * by ID sees one finger appear and disappear, which is consistent; inventing distinct
     * IDs for a stream that only ever holds one pointer would not be.
     */
    public int getPointerId(int pointerIndex) {
        return 0;
    }

    public int findPointerIndex(int pointerId) {
        return pointerId == 0 ? 0 : -1;
    }

    /** A finger, since the only input KuDroid synthesises is touch. */
    public int getToolType(int pointerIndex) {
        return TOOL_TYPE_FINGER;
    }

    public int getDeviceId() {
        return TOUCHSCREEN_DEVICE_ID;
    }

    public int getSource() {
        return InputDevice.SOURCE_TOUCHSCREEN;
    }

    public int getFlags() {
        return 0;
    }

    public int getMetaState() {
        return 0;
    }

    /** No physical buttons on a touchscreen, so no button is pressed or changing. */
    public int getButtonState() {
        return 0;
    }

    public int getActionButton() {
        return 0;
    }

    /** Nothing here classifies gestures, and NONE is Android's answer for that. */
    public int getClassification() {
        return CLASSIFICATION_NONE;
    }

    /** Edge detection is a hardware feature with no iOS counterpart. */
    public int getEdgeFlags() {
        return 0;
    }

    /**
     * Coordinates are delivered one event at a time, never batched, so there is no
     * history. Returning 0 is what makes the getHistorical* methods below unreachable;
     * a non-zero answer would invite reads of samples that do not exist.
     */
    public int getHistorySize() {
        return 0;
    }

    public long getHistoricalEventTime(int pos) {
        return mEventTime;
    }

    public float getHistoricalX(int pos) {
        return mX;
    }

    public float getHistoricalY(int pos) {
        return mY;
    }

    public float getHistoricalX(int pointerIndex, int pos) {
        return mX;
    }

    public float getHistoricalY(int pointerIndex, int pos) {
        return mY;
    }

    public float getHistoricalAxisValue(int axis, int pointerIndex, int pos) {
        return getAxisValue(axis, pointerIndex);
    }

    /**
     * Scale factor between reported coordinates and physical pixels.
     *
     * 1.0 because coordinates are already in pixels. Zero would be the dangerous answer:
     * guests divide by this to convert to device units.
     */
    public float getXPrecision() {
        return 1.0f;
    }

    public float getYPrecision() {
        return 1.0f;
    }

    /** Full contact, which is all a capacitive digitiser without force reports can say. */
    public float getPressure() {
        return 1.0f;
    }

    public float getPressure(int pointerIndex) {
        return 1.0f;
    }

    public float getSize() {
        return 1.0f;
    }

    public float getSize(int pointerIndex) {
        return 1.0f;
    }

    public float getOrientation() {
        return 0.0f;
    }

    public float getOrientation(int pointerIndex) {
        return 0.0f;
    }

    /**
     * Contact patch long/short axis in pixels. Unity asks for these on every
     * touch and died as NoSuchMethodError without them. A fixed plausible
     * finger width: nothing here measures the real patch, but zero would read
     * as "no contact" and break pressure-ratio math downstream.
     */
    public float getTouchMajor() {
        return 10.0f;
    }

    public float getTouchMajor(int pointerIndex) {
        return 10.0f;
    }

    public float getTouchMinor() {
        return 10.0f;
    }

    public float getTouchMinor(int pointerIndex) {
        return 10.0f;
    }

    /**
     * Axis value, the route AGDK uses to read pointer geometry.
     *
     * X and Y come from the event; the contact-shape axes report no measurement rather
     * than a fabricated size, since nothing here measures the contact patch.
     */
    public float getAxisValue(int axis) {
        return getAxisValue(axis, 0);
    }

    public float getAxisValue(int axis, int pointerIndex) {
        switch (axis) {
            case AXIS_X: return mX;
            case AXIS_Y: return mY;
            case AXIS_PRESSURE: return 1.0f;
            case AXIS_SIZE: return 1.0f;
            default: return 0.0f;
        }
    }

    /**
     * event recycling.
     */
    public void recycle() {
    }

    @Override
    public String toString() {
        return "MotionEvent{action=" + mAction + " x=" + mX + " y=" + mY + "}";
    }
}
