package android.text;

import java.util.ArrayList;

/** Span ranges shared by immutable and editable text. */
final class SpanStore {
    private static final class Entry {
        final Object tag;
        int start, end, flags;
        Entry(Object tag, int start, int end, int flags) {
            this.tag = tag; this.start = start; this.end = end; this.flags = flags;
        }
    }
    private final ArrayList<Entry> entries = new ArrayList<>();

    static void checkRange(int start, int end, int length) {
        if (start < 0 || end < start || end > length) throw new IndexOutOfBoundsException();
    }

    private Entry find(Object tag) {
        for (Entry entry : entries) if (entry.tag == tag) return entry;
        return null;
    }

    void set(CharSequence text, Object tag, int start, int end, int flags) {
        checkRange(start, end, text.length());
        if ((flags & Spanned.SPAN_POINT_MARK_MASK) == Spanned.SPAN_PARAGRAPH) {
            if (!paragraphBoundary(text, start) || !paragraphBoundary(text, end)) {
                throw new RuntimeException("Paragraph span must start and end at paragraph boundaries");
            }
        }
        put(tag, start, end, flags);
    }

    private static boolean paragraphBoundary(CharSequence text, int offset) {
        return offset == 0 || offset == text.length() || text.charAt(offset - 1) == '\n';
    }

    private void put(Object tag, int start, int end, int flags) {
        Entry entry = find(tag);
        if (entry == null) entries.add(new Entry(tag, start, end, flags));
        else { entry.start = start; entry.end = end; entry.flags = flags; }
    }

    void copy(CharSequence source, int start, int end, int offset, boolean ignoreNoCopy) {
        copy(source, start, end, offset, ignoreNoCopy, null);
    }

    void copy(CharSequence source, int start, int end, int offset, boolean ignoreNoCopy,
            CharSequence editable) {
        if (!(source instanceof Spanned)) return;
        Spanned spanned = (Spanned) source;
        for (Object tag : spanned.getSpans(start, end, Object.class)) {
            if (ignoreNoCopy && tag instanceof NoCopySpan) continue;
            if (find(tag) != null) continue;
            int s = Math.max(start, spanned.getSpanStart(tag)) - start + offset;
            int e = Math.min(end, spanned.getSpanEnd(tag)) - start + offset;
            int flags = spanned.getSpanFlags(tag);
            if (editable != null) {
                int endpoints = flags & Spanned.SPAN_POINT_MARK_MASK;
                if (s == e && endpoints == Spanned.SPAN_EXCLUSIVE_EXCLUSIVE) continue;
                if (endpoints == Spanned.SPAN_PARAGRAPH
                        && (!paragraphBoundary(editable, s) || !paragraphBoundary(editable, e))) continue;
            }
            put(tag, s, e, flags);
        }
    }

    void remove(Object tag) {
        Entry entry = find(tag);
        if (entry != null) entries.remove(entry);
    }
    void clear() { entries.clear(); }
    int start(Object tag) { Entry e = find(tag); return e == null ? -1 : e.start; }
    int end(Object tag) { Entry e = find(tag); return e == null ? -1 : e.end; }
    int flags(Object tag) { Entry e = find(tag); return e == null ? 0 : e.flags; }

    <T> T[] spans(int start, int end, Class<T> kind) {
        if (kind == null) kind = (Class<T>) Object.class;
        ArrayList<Entry> matches = new ArrayList<>();
        for (Entry entry : entries) {
            if (entry.start > end || entry.end < start || !kind.isInstance(entry.tag)) continue;
            if (entry.start != entry.end && start != end
                    && (entry.start == end || entry.end == start)) continue;
            int i = 0;
            int priority = entry.flags & Spanned.SPAN_PRIORITY;
            while (i < matches.size() && (matches.get(i).flags & Spanned.SPAN_PRIORITY) >= priority) i++;
            matches.add(i, entry);
        }
        T[] result = (T[]) java.lang.reflect.Array.newInstance(kind, matches.size());
        for (int i = 0; i < result.length; i++) result[i] = (T) matches.get(i).tag;
        return result;
    }

    int transition(int start, int limit, Class kind) {
        for (Entry entry : entries) {
            if (kind != null && !kind.isInstance(entry.tag)) continue;
            if (entry.start > start && entry.start < limit) limit = entry.start;
            if (entry.end > start && entry.end < limit) limit = entry.end;
        }
        return limit;
    }

    void replace(int start, int end, int inserted, CharSequence oldText) {
        int oldLength = oldText.length();
        for (int i = entries.size() - 1; i >= 0; i--) {
            Entry entry = entries.get(i);
            if ((entry.flags & Spanned.SPAN_POINT_MARK_MASK) == Spanned.SPAN_PARAGRAPH) {
                if (entry.start > start && entry.start <= end) entry.start = nextParagraph(oldText, end);
                if (entry.end > start && entry.end <= end) entry.end = nextParagraph(oldText, end);
            }
            if ((entry.flags & Spanned.SPAN_POINT_MARK_MASK) == Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                    && entry.start >= start && entry.end <= end
                    && (inserted == 0 || entry.start > start || entry.end < end)) {
                entries.remove(i);
                continue;
            }
            entry.start = move(entry.start, start, end, inserted, (entry.flags >> 4) & 0xf, oldLength);
            entry.end = move(entry.end, start, end, inserted, entry.flags & 0xf, oldLength);
        }
    }

    private static int nextParagraph(CharSequence text, int end) {
        int position = end;
        while (position < text.length()) {
            if (position > end && text.charAt(position - 1) == '\n') break;
            position++;
        }
        return position;
    }

    private static int move(int position, int start, int end, int inserted, int flag, int oldLength) {
        if (position < start) return position;
        if (position > end) return position + inserted - (end - start);
        boolean point = flag == 2 || (flag == 3 && position == oldLength);
        if (start == end) return point ? start + inserted : start;
        if (position == end) return start + inserted;
        if (position == start && inserted != 0) return start;
        return point ? start + inserted : start;
    }
}
