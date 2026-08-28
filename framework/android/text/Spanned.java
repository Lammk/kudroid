package android.text;

public interface Spanned extends CharSequence {
    int SPAN_POINT_MARK_MASK = 0x33;
    int SPAN_MARK_MARK = 0x00;
    int SPAN_POINT_MARK = 0x01;
    int SPAN_MARK_POINT = 0x02;
    int SPAN_POINT_POINT = 0x03;
    int SPAN_PARAGRAPH = 0x33;
    int SPAN_INCLUSIVE_EXCLUSIVE = SPAN_MARK_MARK;
    int SPAN_INCLUSIVE_INCLUSIVE = SPAN_MARK_POINT;
    int SPAN_EXCLUSIVE_EXCLUSIVE = SPAN_POINT_MARK;
    int SPAN_EXCLUSIVE_INCLUSIVE = SPAN_POINT_POINT;

    <T> T[] getSpans(int start, int end, Class<T> type);
    int getSpanStart(Object tag);
    int getSpanEnd(Object tag);
    int getSpanFlags(Object tag);
    int nextSpanTransition(int start, int limit, Class type);
}
