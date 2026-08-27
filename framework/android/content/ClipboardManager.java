package android.content;

/**
 * emulate android.content.clipboardmanager.
 *
 * clipboard management. for kudroid minimal framework, here is an emulation.
 */
public class ClipboardManager {
    private CharSequence mText;

    public ClipboardManager() {
    }

    public void setText(CharSequence text) {
        mText = text;
    }

    public CharSequence getText() {
        return mText;
    }

    public boolean hasText() {
        return mText != null && mText.length() > 0;
    }

    public void setPrimaryClip(ClipData clip) {
        if (clip != null && clip.getItemCount() > 0) {
            mText = clip.getItemAt(0).getText();
        }
    }

    public ClipData getPrimaryClip() {
        return null;
    }

    public boolean hasPrimaryClip() {
        return mText != null;
    }
}