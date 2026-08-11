package android.content;

/**
 * mô phỏng android.content.clipdata.
 *
 * đại diện cho dữ liệu khay nhớ tạm. đối với khuôn khổ tối thiểu của kudroid, đây là một mô phỏng.
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
     * một mục duy nhất của dữ liệu khay nhớ tạm.
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