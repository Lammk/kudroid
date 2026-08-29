package android.view.inputmethod;

/**
 * A completion the IME may offer for the text being edited.
 *
 * KuDroid has no IME, so nothing ever consumes these; the class exists because
 * InputMethodManager.displayCompletions takes an array of them and an app that
 * builds completions would otherwise fail to resolve the type.
 */
public final class CompletionInfo {
    private final long mId;
    private final int mPosition;
    private final CharSequence mText;
    private final CharSequence mLabel;

    public CompletionInfo(long id, int index, CharSequence text) {
        this(id, index, text, null);
    }

    public CompletionInfo(long id, int index, CharSequence text, CharSequence label) {
        mId = id;
        mPosition = index;
        mText = text;
        mLabel = label;
    }

    public long getId() { return mId; }
    public int getPosition() { return mPosition; }
    public CharSequence getText() { return mText; }
    public CharSequence getLabel() { return mLabel; }

    @Override
    public String toString() {
        return "CompletionInfo{#" + mPosition + " " + mText + "}";
    }
}
