package android.hardware.input;

/**
 * android.hardware.input.InputManager.
 *
 * KuDroid exposes exactly one input device: the touchscreen (id 1, see
 * android.view.InputDevice). That must be visible HERE too — native game code
 * enumerates devices through this class, and an empty table makes Unity
 * conclude there is no usable touchscreen and drop every touch its own
 * injectEvent receives, even though the Java delivery path works.
 *
 * Every method still has to EXIST. Unity calls getInputDeviceIds() and
 * registerInputDeviceListener() during startup, and a missing method is a
 * NoSuchMethodError that propagates out of Activity creation.
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
     * The touchscreen this class always reports. Callers iterate the result without
     * checking, and null turns "devices" into a NullPointerException inside their
     * startup path — so never null, and consistent with getInputDevice().
     */
    public int[] getInputDeviceIds() {
        return new int[] { 1 };
    }

    /** Device 1 is the touchscreen singleton; anything else is absent. */
    public android.view.InputDevice getInputDevice(int id) {
        return android.view.InputDevice.getDevice(id);
    }

    private final java.util.ArrayList<InputDeviceListener> mListeners =
            new java.util.ArrayList<InputDeviceListener>();

    // Remembered and immediately told about the touchscreen: a listener that never
    // hears onInputDeviceAdded may conclude its device went away and stop listening.
    public void registerInputDeviceListener(InputDeviceListener listener,
                                           android.os.Handler handler) {
        if (listener == null) return;
        synchronized (mListeners) {
            if (!mListeners.contains(listener)) {
                mListeners.add(listener);
            }
        }
        try {
            listener.onInputDeviceAdded(1);
        } catch (Throwable ignored) {}
    }

    public void unregisterInputDeviceListener(InputDeviceListener listener) {
        if (listener == null) return;
        synchronized (mListeners) {
            mListeners.remove(listener);
        }
    }

    /** No vibrator is reachable through an input device; the phone's own is via Context. */
    public android.os.Vibrator getInputDeviceVibrator(int deviceId) {
        return null;
    }
}
