package android.content;

/**
 * emulate android.content.clipdata.
 *
 * represents clipboard data. for kudroid minimal framework, here is an emulation.
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
     * a single item of clipboard data.
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