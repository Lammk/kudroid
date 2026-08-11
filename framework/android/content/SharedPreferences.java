package android.content;

import java.util.Map;

/**
 * Minimal android.content.SharedPreferences implementation.
 *
 * Provides a simple in-memory key-value store. For KuDroid's minimal framework,
 * data is not persisted to disk (returns defaults on restart).
 */
public interface SharedPreferences {

    /**
     * Retrieve a String value.
     */
    String getString(String key, String defValue);

    /**
     * Retrieve an int value.
     */
    int getInt(String key, int defValue);

    /**
     * Retrieve a long value.
     */
    long getLong(String key, long defValue);

    /**
     * Retrieve a float value.
     */
    float getFloat(String key, float defValue);

    /**
     * Retrieve a boolean value.
     */
    boolean getBoolean(String key, boolean defValue);

    /**
     * Check whether the preferences contain a key.
     */
    boolean contains(String key);

    /**
     * Return an editor for modifying the preferences.
     */
    Editor edit();

    /**
     * Interface for modifying values in a SharedPreferences object.
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
}
