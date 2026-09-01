package android.text;

public class DynamicLayout extends Layout {
    public static class ChangeWatcher implements TextWatcher, SpanWatcher {
        public ChangeWatcher() {}
        public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
        public void onTextChanged(CharSequence s, int start, int before, int count) {}
        public void afterTextChanged(Editable s) {}
        public void onSpanAdded(Spannable text, Object what, int start, int end) {}
        public void onSpanRemoved(Spannable text, Object what, int start, int end) {}
        public void onSpanChanged(Spannable text, Object what, int ostart, int oend, int nstart, int nend) {}
    }

    public DynamicLayout(CharSequence base, TextPaint paint, int width, Alignment align, float spacingmult, float spacingadd, boolean includepad) {
        super(base, paint, width, align, spacingmult, spacingadd);
    }
}
