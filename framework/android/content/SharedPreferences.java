package android.content;

import java.util.Map;

/**
 * minimal android.content.sharedpreferences implementation.
 *
 * provides a simple in-memory key-value store. for kudroid minimal framework,
 * data is not saved to disk (returns default value on reboot).
 */
public interface SharedPreferences {

    /**
     * takes a string value.
     */
    String getString(String key, String defValue);

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

    /**
     * interface for modifying values ​​in sharedpreferences object.
     */
    interface Editor {
        Editor putString(String key, String value);
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
