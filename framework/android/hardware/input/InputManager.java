package android.hardware.input;

/**
 * android.hardware.input.InputManager.
 *
 * A game asks this class what input hardware is attached, and then decides which control
 * scheme to present: on-screen controls, or gamepad handling with a listener registered for
 * hot-plug. KuDroid reports NO input devices, and that is the accurate answer — iOS exposes
 * game controllers through GameController.framework, which nothing here bridges yet, so
 * there is no device whose events would ever arrive.
 *
 * Reporting a device that does not exist is the worse failure. A game that believes a
 * gamepad is present hides its touch controls and then receives no input at all, which is
 * unrecoverable from the user's side; a game told there are none shows touch controls and
 * works.
 *
 * Every method here still has to EXIST. Unity calls getInputDeviceIds() and
 * registerInputDeviceListener() during startup, and a missing method is a NoSuchMethodError
 * that propagates out of Activity creation rather than being handled as "no devices".
 */
public class InputManager {

    public static final String ACTION_QUERY_KEYBOARD_LAYOUTS =
            "android.hardware.input.action.QUERY_KEYBOARD_LAYOUTS";

    public InputManager() {}

    /**
     * Hot-plug notifications. Registration is accepted and the listener is simply never
     * called, because no device is ever added or removed.
     */
    public interface InputDeviceListener {
        void onInputDeviceAdded(int deviceId);
        void onInputDeviceRemoved(int deviceId);
        void onInputDeviceChanged(int deviceId);
    }

    /**
     * An empty array, never null: callers iterate the result without checking, and null
     * turns "no devices" into a NullPointerException inside their startup path.
     */
    public int[] getInputDeviceIds() {
        return new int[0];
    }

    /** Null for any id, since getInputDeviceIds() reported none. */
    public android.view.InputDevice getInputDevice(int id) {
        return null;
    }

    // Accepted and remembered nowhere: with no devices there is nothing to report, and
    // holding the listener would only keep it alive for events that cannot happen.
    public void registerInputDeviceListener(InputDeviceListener listener,
                                           android.os.Handler handler) {}

    public void unregisterInputDeviceListener(InputDeviceListener listener) {}

    /** No vibrator is reachable through an input device; the phone's own is via Context. */
    public android.os.Vibrator getInputDeviceVibrator(int deviceId) {
        return null;
    }
}
