package android.text;

/** Cursor and selection stored as marker spans on a Spannable. */
public class Selection {
    private static final class Marker {
        private final String mName;
        Marker(String name) { mName = name; }
        @Override public String toString() { return mName; }
    }

    public static final Object SELECTION_START = new Marker("SELECTION_START");
    public static final Object SELECTION_END = new Marker("SELECTION_END");

    private Selection() {}

    public static final int getSelectionStart(CharSequence text) {
        return text instanceof Spanned ? ((Spanned) text).getSpanStart(SELECTION_START) : -1;
    }

    public static final int getSelectionEnd(CharSequence text) {
        return text instanceof Spanned ? ((Spanned) text).getSpanStart(SELECTION_END) : -1;
    }

    public static void setSelection(Spannable text, int start, int stop) {
        final int length = text.length();
        if (start < 0 || stop < 0 || start > length || stop > length) {
            throw new IndexOutOfBoundsException("selection " + start + " .. " + stop
                    + " has end before start or is out of range 0.." + length);
        }
        text.setSpan(SELECTION_START, start, start, Spanned.SPAN_POINT_POINT);
        text.setSpan(SELECTION_END, stop, stop, Spanned.SPAN_POINT_POINT);
    }

    public static void setSelection(Spannable text, int index) {
        setSelection(text, index, index);
    }

    public static void selectAll(Spannable text) {
        setSelection(text, 0, text.length());
    }

    public static void removeSelection(Spannable text) {
        text.removeSpan(SELECTION_START);
        text.removeSpan(SELECTION_END);
    }

    public static boolean moveToLeftEdge(Spannable text, Layout layout) { return false; }
    public static boolean moveToRightEdge(Spannable text, Layout layout) { return false; }
}
