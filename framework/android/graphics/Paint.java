package android.graphics;

/**
 * triển khai android.graphics.paint tối thiểu.
 *
 * lưu giữ thông tin kiểu và màu sắc để vẽ. đối với khuôn khổ tối thiểu
 * của kudroid, nó lưu trữ các thuộc tính vẽ cơ bản.
 */
public class Paint {
    /** kiểu vẽ: điền vào. */
    public static final int STYLE_FILL = 0;
    /** kiểu vẽ: nét. */
    public static final int STYLE_STROKE = 1;
    /** kiểu vẽ: điền vào và nét. */
    public static final int STYLE_FILL_AND_STROKE = 2;

    /** căn chỉnh: trái. */
    public static final int ALIGN_LEFT = 0;
    /** căn chỉnh: giữa. */
    public static final int ALIGN_CENTER = 1;
    /** căn chỉnh: phải. */
    public static final int ALIGN_RIGHT = 2;

    private int mColor = 0xFF000000;
    private int mStyle = STYLE_FILL;
    private float mStrokeWidth = 0.0f;
    private float mTextSize = 12.0f;
    private int mTextAlign = ALIGN_LEFT;
    private boolean mAntiAlias = false;
    private Typeface mTypeface = null;
    private ColorFilter mColorFilter = null;

    public Paint() {
    }

    public Paint(int flags) {
    }

    public Paint(Paint paint) {
        if (paint != null) {
            mColor = paint.mColor;
            mStyle = paint.mStyle;
            mStrokeWidth = paint.mStrokeWidth;
            mTextSize = paint.mTextSize;
            mTextAlign = paint.mTextAlign;
            mAntiAlias = paint.mAntiAlias;
            mTypeface = paint.mTypeface;
            mColorFilter = paint.mColorFilter;
        }
    }

    public void setColor(int color) {
        mColor = color;
    }

    public int getColor() {
        return mColor;
    }

    public void setStyle(int style) {
        mStyle = style;
    }

    public int getStyle() {
        return mStyle;
    }

    public void setStrokeWidth(float width) {
        mStrokeWidth = width;
    }

    public float getStrokeWidth() {
        return mStrokeWidth;
    }

    public void setTextSize(float textSize) {
        mTextSize = textSize;
    }

    public float getTextSize() {
        return mTextSize;
    }

    public void setTextAlign(int align) {
        mTextAlign = align;
    }

    public int getTextAlign() {
        return mTextAlign;
    }

    public void setAntiAlias(boolean aa) {
        mAntiAlias = aa;
    }

    public boolean isAntiAlias() {
        return mAntiAlias;
    }

    public void setTypeface(Typeface typeface) {
        mTypeface = typeface;
    }

    public Typeface getTypeface() {
        return mTypeface;
    }

    public void setColorFilter(ColorFilter colorFilter) {
        mColorFilter = colorFilter;
    }

    public ColorFilter getColorFilter() {
        return mColorFilter;
    }

    /**
     * trả về chiều rộng của văn bản đã cho.
     */
    public float measureText(String text) {
        return text != null ? text.length() * mTextSize * 0.5f : 0.0f;
    }

    /**
     * trả về số liệu phông chữ.
     */
    public float getFontMetrics() {
        return mTextSize;
    }

    public static class Cap {
        public Cap() {}
    }

    public static class FontMetricsInt {
        public FontMetricsInt() {}
    }

    public static class Join {
        public Join() {}
    }

}
