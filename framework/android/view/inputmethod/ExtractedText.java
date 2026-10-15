package android.view.inputmethod;

/**
 * A snapshot of the text an IME is editing, plus where the selection sits in it.
 *
 * The fields are public and written directly by callers, matching AOSP — an app
 * fills one in from its own text and hands it to
 * InputMethodManager.updateExtractedText.
 */
public class ExtractedText {
    public CharSequence text;
    public int startOffset;
    public int partialStartOffset = -1;
    public int partialEndOffset = -1;
    public int selectionStart;
    public int selectionEnd;
    public int flags;

    public static final int FLAG_SINGLE_LINE = 0x0001;
    public static final int FLAG_SELECTING = 0x0002;

    public ExtractedText() {}
}
