package android.view.inputmethod;

import android.os.Bundle;

/**
 * Editor info for an IME session.
 * Fields are public and written directly by callers under exact AOSP names.
 */
public class EditorInfo {

    // inputType mirrors InputType; values must agree.
    public static final int TYPE_MASK_CLASS = 0x0000000f;
    public static final int TYPE_MASK_VARIATION = 0x00000ff0;
    public static final int TYPE_MASK_FLAGS = 0x00fff000;
    public static final int TYPE_NULL = 0x00000000;
    public static final int TYPE_CLASS_TEXT = 0x00000001;
    public static final int TYPE_CLASS_NUMBER = 0x00000002;
    public static final int TYPE_CLASS_PHONE = 0x00000003;
    public static final int TYPE_CLASS_DATETIME = 0x00000004;

    // imeOptions.
    public static final int IME_MASK_ACTION = 0x000000ff;
    public static final int IME_ACTION_UNSPECIFIED = 0x00000000;
    public static final int IME_ACTION_NONE = 0x00000001;
    public static final int IME_ACTION_GO = 0x00000002;
    public static final int IME_ACTION_SEARCH = 0x00000003;
    public static final int IME_ACTION_SEND = 0x00000004;
    public static final int IME_ACTION_NEXT = 0x00000005;
    public static final int IME_ACTION_DONE = 0x00000006;
    public static final int IME_ACTION_PREVIOUS = 0x00000007;
    public static final int IME_FLAG_NO_PERSONALIZED_LEARNING = 0x01000000;
    public static final int IME_FLAG_NO_FULLSCREEN = 0x02000000;
    public static final int IME_FLAG_NAVIGATE_PREVIOUS = 0x04000000;
    public static final int IME_FLAG_NAVIGATE_NEXT = 0x08000000;
    public static final int IME_FLAG_NO_EXTRACT_UI = 0x10000000;
    public static final int IME_FLAG_NO_ACCESSORY_ACTION = 0x20000000;
    public static final int IME_FLAG_NO_ENTER_ACTION = 0x40000000;
    public static final int IME_FLAG_FORCE_ASCII = 0x80000000;
    public static final int IME_NULL = 0x00000000;

    /** How the text should be interpreted; see the TYPE_* constants. */
    public int inputType = TYPE_NULL;

    /** IME action and flags; see the IME_* constants. */
    public int imeOptions = IME_NULL;

    /** Custom action id, used when imeOptions carries IME_ACTION_UNSPECIFIED. */
    public int actionId;

    /** Label for a custom action. */
    public CharSequence actionLabel;

    /** Text to show when the field is empty. */
    public CharSequence hintText;

    /** Human-readable label for the field. */
    public CharSequence label;

    /** Resource id of the view being edited. */
    public int fieldId;

    /** Name of the field being edited, for the IME's benefit. */
    public String fieldName;

    /** Package that owns the edited view. */
    public String packageName;

    /** IME-private options string. */
    public String privateImeOptions;

    /** Initial selection, in characters, or -1 when unknown. */
    public int initialSelStart = -1;
    public int initialSelEnd = -1;

    /** Capitalisation mode in effect at the cursor. */
    public int initialCapsMode;

    /** MIME types the field accepts via commitContent, or null for none. */
    public String[] contentMimeTypes;

    /** Extra IME-specific data. */
    public Bundle extras;

    /** Hint locales, or null when the field expresses no preference. */
    public android.os.LocaleList hintLocales;

    private CharSequence mSurroundingBefore;
    private CharSequence mSurroundingSelected;
    private CharSequence mSurroundingAfter;

    public EditorInfo() {
    }

    /**
     * Record the text around the cursor for the IME to read back.
     *
     * The split follows initialSelStart/initialSelEnd, so an IME asking for text
     * before or after the cursor gets what the view actually holds rather than the
     * whole buffer.
     */
    public void setInitialSurroundingText(CharSequence sourceText) {
        setInitialSurroundingSubText(sourceText, 0);
    }

    public void setInitialSurroundingSubText(CharSequence subText, int subTextStart) {
        if (subText == null) {
            mSurroundingBefore = null;
            mSurroundingSelected = null;
            mSurroundingAfter = null;
            return;
        }
        final int length = subText.length();
        // Selection offsets are absolute; subTextStart says where this slice begins,
        // so both have to be rebased before they can index into subText.
        int start = initialSelStart - subTextStart;
        int end = initialSelEnd - subTextStart;
        if (start > end) {
            final int t = start;
            start = end;
            end = t;
        }
        if (start < 0) start = 0;
        if (end < 0) end = 0;
        if (start > length) start = length;
        if (end > length) end = length;

        mSurroundingBefore = subText.subSequence(0, start);
        mSurroundingSelected = subText.subSequence(start, end);
        mSurroundingAfter = subText.subSequence(end, length);
    }

    public CharSequence getInitialTextBeforeCursor(int length, int flags) {
        return trimToLength(mSurroundingBefore, length, /*fromEnd=*/true);
    }

    public CharSequence getInitialTextAfterCursor(int length, int flags) {
        return trimToLength(mSurroundingAfter, length, /*fromEnd=*/false);
    }

    public CharSequence getInitialSelectedText(int flags) {
        return mSurroundingSelected;
    }

    /** Keep at most `length` characters, from whichever end is adjacent to the cursor. */
    private static CharSequence trimToLength(CharSequence text, int length, boolean fromEnd) {
        if (text == null || length < 0) return null;
        final int n = text.length();
        if (n <= length) return text;
        return fromEnd ? text.subSequence(n - length, n) : text.subSequence(0, length);
    }

    public final void makeCompatible(int targetSdkVersion) {
    }

    public int describeContents() {
        return 0;
    }

    public void writeToParcel(android.os.Parcel dest, int flags) {
    }

    @Override
    public String toString() {
        return "EditorInfo{inputType=0x" + Integer.toHexString(inputType) +
               " imeOptions=0x" + Integer.toHexString(imeOptions) +
               " fieldId=" + fieldId + "}";
    }
}
