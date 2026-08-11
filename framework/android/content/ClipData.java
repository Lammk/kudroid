package android.content;

/**
 * Stub android.content.ClipData.
 *
 * Represents clipboard data. For KuDroid's minimal framework, this is a stub.
 */
public class ClipData {
    private final Item[] mItems;

    public ClipData(CharSequence label, String[] mimeTypes, Item item) {
        mItems = new Item[] { item };
    }

    public int getItemCount() {
        return mItems != null ? mItems.length : 0;
    }

    public Item getItemAt(int index) {
        if (mItems == null || index < 0 || index >= mItems.length) return null;
        return mItems[index];
    }

    /**
     * A single item of clipboard data.
     */
    public static class Item {
        private final CharSequence mText;

        public Item(CharSequence text) {
            mText = text;
        }

        public CharSequence getText() {
            return mText;
        }
    }
}