package android.content;

import android.os.Parcel;
import android.os.Parcelable;

public class ClipData implements Parcelable {
    private final CharSequence mText;

    public ClipData(CharSequence label, String[] mimeTypes, Item item) {
        mText = (item != null) ? item.getText() : null;
    }
    public static class Item {
        private final CharSequence mText;
        public Item(CharSequence text) { mText = text; }
        public CharSequence getText() { return mText; }
    }
    public static ClipData newPlainText(CharSequence label, CharSequence text) {
        return new ClipData(label, new String[]{"text/plain"}, new Item(text));
    }
    public int getItemCount() { return 1; }
    public Item getItemAt(int index) { return new Item(mText); }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
