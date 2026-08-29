package android.view.inputmethod;

/**
 * What an IME wants when it asks for extracted text.
 *
 * Public fields written directly by the caller, as in AOSP.
 */
public class ExtractedTextRequest {
    public int token;
    public android.os.Bundle extras;
    public int flags;
    public int hintMaxLines;
    public int hintMaxChars;

    public ExtractedTextRequest() {}
}
