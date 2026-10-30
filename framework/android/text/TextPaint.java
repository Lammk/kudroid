package android.text;

import android.graphics.Paint;

/** Paint with the extra state text spans draw with. */
public class TextPaint extends Paint {
    public int bgColor;
    public int baselineShift;
    public int linkColor;
    public int[] drawableState;
    public float density = 1.0f;
    public int underlineColor = 0;
    public float underlineThickness;

    public TextPaint() {
        super();
    }

    public TextPaint(int flags) {
        super(flags);
    }

    public TextPaint(Paint p) {
        super(p);
    }

    public void set(TextPaint tp) {
        super.set(tp);
        bgColor = tp.bgColor;
        baselineShift = tp.baselineShift;
        linkColor = tp.linkColor;
        drawableState = tp.drawableState;
        density = tp.density;
        underlineColor = tp.underlineColor;
        underlineThickness = tp.underlineThickness;
    }

    public void setUnderlineText(int color, float thickness) {
        underlineColor = color;
        underlineThickness = thickness;
    }
}
