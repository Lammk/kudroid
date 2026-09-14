package android.view;

/**
 * android.view.KeyCharacterMap — key-to-character mapping.
 *
 * No hardware keyboard is attached, so this is an empty binding set.
 */
public final class KeyCharacterMap {
    public static final int VIRTUAL_KEYBOARD = -1;
    public static final int BUILT_IN_KEYBOARD = 0;

    // Fallback behaviors (frameworks/base/core/java/android/view/KeyCharacterMap.java).
    public static final int FALLBACK_ACTION = 1;
    public static final int FALLBACK_NONE = 0;

    public static final int KEYCODE_UNKNOWN = 0;

    private final int mId;

    private KeyCharacterMap(int id) {
        mId = id;
    }

    /** The AOSP shape: device 0 and the virtual keyboard always resolve. */
    public static KeyCharacterMap load(int id) {
        return new KeyCharacterMap(id);
    }

    public int getId() {
        return mId;
    }

    /** No keyboard attached, so the type is NONE (AOSP numbering). */
    public static final int KEYBOARD_TYPE_NONE = 0;
    public static final int KEYBOARD_TYPE_ALPHABETIC = 2;

    public int getKeyboardType() {
        return KEYBOARD_TYPE_NONE;
    }

    /** Empty table: no key produces characters. */
    public int get(int keyCode, int metaState) {
        return 0;
    }

    public char[] getCharacters(int keyCode, int metaState) {
        return new char[0];
    }

    /** No dead-key fallback table. */
    public KeyEvent getKeyData(int keyCode, KeyData results) {
        return null;
    }

    /** First-codepoint constants the engine compares against. */
    public int getMatch(char[] chars, int metaState) {
        return KEYCODE_UNKNOWN;
    }

    public boolean isPrintingKey(int keyCode) {
        return false;
    }

    /** Container for getKeyData; kept because the engine references the type. */
    public static class KeyData {
        public static final int META_LENGTH = 4;
        public char displayLabel;
        public char number;
        public char[] chars = new char[META_LENGTH];
    }
}
