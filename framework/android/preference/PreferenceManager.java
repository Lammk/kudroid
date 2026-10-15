package android.preference;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * android.preference.PreferenceManager.
 *
 * Only the part apps actually call outside a settings UI: getDefaultSharedPreferences.
 * Minecraft's getLegacyDeviceID calls it during onCreate, and an auto-stubbed class threw
 * NoClassDefFoundError there — the device ID came back null and the JNI call that received
 * it reported a fabricated handle.
 *
 * The default preferences file is named "<package>_preferences", which is the name Android
 * uses. It matters that it is the same name: an app that also calls
 * getSharedPreferences("<package>_preferences", 0) must see the same store, and Minecraft
 * reads its device ID back through both paths.
 */
public class PreferenceManager {

    private final Context mContext;

    public PreferenceManager(Context context) {
        mContext = context;
    }

    public static SharedPreferences getDefaultSharedPreferences(Context context) {
        if (context == null) {
            // A null context has nowhere to persist to, but returning null here would turn
            // a missing context into a NullPointerException inside the caller, one step
            // removed from the cause. An empty in-memory store is honest and survivable.
            return new android.content.SharedPreferencesImpl("_no_context");
        }
        return context.getSharedPreferences(getDefaultSharedPreferencesName(context),
                                            getDefaultSharedPreferencesMode());
    }

    public static String getDefaultSharedPreferencesName(Context context) {
        final String pkg = context != null ? context.getPackageName() : null;
        return (pkg != null ? pkg : "android") + "_preferences";
    }

    public static int getDefaultSharedPreferencesMode() {
        return Context.MODE_PRIVATE;
    }

    public SharedPreferences getSharedPreferences() {
        return getDefaultSharedPreferences(mContext);
    }

    /**
     * Android's own no-op when an app has no XML preference resources to migrate. Apps call
     * it defensively on startup.
     */
    public static void setDefaultValues(Context context, int resId, boolean readAgain) {}

    public static void setDefaultValues(Context context, String sharedPreferencesName,
                                        int sharedPreferencesMode, int resId,
                                        boolean readAgain) {}

    public Context getContext() { return mContext; }
}
