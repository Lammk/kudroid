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
}
