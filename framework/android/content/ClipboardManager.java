package android.content;

public class ClipboardManager {
    private ClipData mClip;

    public ClipboardManager() {}
    public void setPrimaryClip(ClipData clip) { mClip = clip; }
    public ClipData getPrimaryClip() { return mClip; }
    public boolean hasPrimaryClip() { return mClip != null; }
    public CharSequence getText() {
        return (mClip != null && mClip.getItemCount() > 0) ? mClip.getItemAt(0).getText() : null;
    }
    public void setText(CharSequence text) {
        setPrimaryClip(ClipData.newPlainText(null, text));
    }
}
