package android.content;

import java.util.Map;
import java.util.Set;

/**
 * android.content.SharedPreferences.
 *
 * The declarations here are what an app can actually call. That is not a formality: an app
 * holds a variable of this interface type and the DEX reference names the INTERFACE, so a
 * method present only on SharedPreferencesImpl does not resolve — kuart_verify reported
 * getAll, getStringSet and the two listener methods as METHOD_ABSENT while the impl had
 * them all along.
 *
 * Values are persisted by SharedPreferencesImpl; see the note there on why in-memory is not
 * enough (an app storing a generated device ID gets a different one on every launch).
 */
public interface SharedPreferences {

    /**
     * Every stored key and value.
     *
     * Needed by four apps in the corpus. The returned map is a snapshot: Android documents
     * that mutating it is unsupported, and apps iterate it while writing.
     */
    Map<String, ?> getAll();

    /**
     * takes a string value.
     */
    String getString(String key, String defValue);

    /**
     * A string set, or defValues when absent.
     *
     * `defValues` may be null and is returned as-is in that case, which is what Android
     * does — a caller passing null expects null back rather than an empty set.
     */
    Set<String> getStringSet(String key, Set<String> defValues);

    /**
     * takes an integer value.
     */
    int getInt(String key, int defValue);

    /**
     * takes a long value.
     */
    long getLong(String key, long defValue);

    /**
     * takes a float value.
     */
    float getFloat(String key, float defValue);

    /**
     * takes a boolean value.
     */
    boolean getBoolean(String key, boolean defValue);

    /**
     * checks if options contain a key.
     */
    boolean contains(String key);

    /**
     * returns an editor to modify options.
     */
    Editor edit();

    /** Register for change callbacks. Needed by four apps in the corpus. */
    void registerOnSharedPreferenceChangeListener(OnSharedPreferenceChangeListener listener);

    void unregisterOnSharedPreferenceChangeListener(OnSharedPreferenceChangeListener listener);

    /**
     * interface for modifying values ​​in sharedpreferences object.
     */
    interface Editor {
        Editor putString(String key, String value);
        Editor putStringSet(String key, Set<String> values);
        Editor putInt(String key, int value);
        Editor putLong(String key, long value);
        Editor putFloat(String key, float value);
        Editor putBoolean(String key, boolean value);
        Editor remove(String key);
        Editor clear();
        boolean commit();
        void apply();
    }

    /**
     * Callback when a key is changed.
     */
    interface OnSharedPreferenceChangeListener {
        void onSharedPreferenceChanged(SharedPreferences sharedPreferences, String key);
    }
}
