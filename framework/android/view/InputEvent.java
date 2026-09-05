package android.view;

/**
 * Base class for input events delivered to views and activities.
 *
 * Was absent, so the runtime auto-stubbed an empty class on first touch and
 * every subsequent touch died as NoClassDefFoundError inside
 * UnityPlayer.nativeInjectEvent (158 dropped injects in one run) — the game
 * could never receive input. The real hierarchy (KeyEvent/MotionEvent extend
 * this) matters because injectors check instanceof/casts against it.
 */
public abstract class InputEvent {

    protected InputEvent() {
    }

    /**
     * Source of the event (InputDevice.SOURCE_* bit mask).
     */
    public abstract int getSource();

    /**
     * ID of the input device that generated the event.
     */
    public abstract int getDeviceId();

    /**
     * Device that generated the event, or null when unknown.
     *
     * Null (not a stub exception) is the AOSP answer for an unregistered ID,
     * and it is what Unity's injector expects after asking: it null-checks
     * before use. Our device table is empty, so touchscreen/keyboard IDs
     * resolve to null until real devices are registered.
     */
    public InputDevice getDevice() {
        return InputDevice.getDevice(getDeviceId());
    }
}
