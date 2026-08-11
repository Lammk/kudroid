package android.content;

/**
 * mô phỏng android.content.clipboardmanager.
 *
 * quản lý khay nhớ tạm. đối với khuôn khổ tối thiểu của kudroid, đây là một mô phỏng.
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