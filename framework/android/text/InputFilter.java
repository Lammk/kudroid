package android.text;

/**
 * A filter that inspects, and may alter, text as it is entered.
 *
 * Attached to a text field through {@code setFilters(InputFilter[])}; every insertion
 * passes through the chain before it reaches the buffer. Returning null accepts the
 * source unchanged, which is why the default of "do nothing" is expressed that way
 * rather than by returning the input.
 *
 * This was an empty interface with no nested classes, so
 * {@code new InputFilter.LengthFilter(n)} resolved to an auto-stub and threw
 * NoClassDefFoundError. That is what stopped Minecraft's onCreate: androidx's
 * gametextinput sets a length cap on its editor, three frames deep inside
 * GameActivity.createSurfaceView.
 */
public interface InputFilter {

    /**
     * Filter one insertion.
     *
     * @param source the text being inserted
     * @param start  first character of `source` to consider
     * @param end    one past the last character to consider
     * @param dest   the buffer being edited
     * @param dstart first character of `dest` being replaced
     * @param dend   one past the last character being replaced
     * @return the replacement text, or null to accept `source` as-is
     */
    CharSequence filter(CharSequence source, int start, int end,
                        Spanned dest, int dstart, int dend);

    /**
     * Caps the total length of the field.
     *
     * The arithmetic is the part worth getting right: the room left is the maximum
     * minus what stays in the buffer, and what stays is everything except the range
     * being replaced. Ignoring the replaced range — the obvious mistake — rejects
     * valid edits as soon as the field is near its limit, because overwriting a
     * selection does not add to the length.
     */
    public static class LengthFilter implements InputFilter {
        private final int mMax;

        public LengthFilter(int max) {
            mMax = max;
        }

        public int getMax() {
            return mMax;
        }

        @Override
        public CharSequence filter(CharSequence source, int start, int end,
                                   Spanned dest, int dstart, int dend) {
            final int destLength = dest != null ? dest.length() : 0;
            final int kept = destLength - (dend - dstart);
            final int room = mMax - kept;

            if (room <= 0) return "";                 // no space: reject the insertion
            if (room >= end - start) return null;     // it all fits: accept unchanged

            // Partial fit. Keep the leading characters that do fit, and do not split a
            // surrogate pair: half of one is not a character, and appending it corrupts
            // the buffer for every later index.
            int keepEnd = start + room;
            if (keepEnd > start && Character.isHighSurrogate(source.charAt(keepEnd - 1))) {
                --keepEnd;
                if (keepEnd == start) return "";
            }
            return source.subSequence(start, keepEnd);
        }
    }

    /** Forces every inserted character to upper case. */
    public static class AllCaps implements InputFilter {
        public AllCaps() {}

        @Override
        public CharSequence filter(CharSequence source, int start, int end,
                                   Spanned dest, int dstart, int dend) {
            if (source == null) return null;
            final String piece = source.subSequence(start, end).toString();
            final String upper = piece.toUpperCase();
            // null when nothing changed, so the caller keeps the original including any
            // spans it carried.
            return upper.equals(piece) ? null : upper;
        }
    }
}
