package android.text.style;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.text.TextPaint;

/** Base class for spans that replace the text range they cover. */
public class ReplacementSpan extends MetricAffectingSpan {
    public ReplacementSpan() {}

    public int getSize(Paint paint, CharSequence text, int start, int end,
            Paint.FontMetricsInt fm) {
        return 0;
    }

    public void draw(Canvas canvas, CharSequence text, int start, int end, float x,
            int top, int y, int bottom, Paint paint) {
    }

    public void updateMeasureState(TextPaint p) {}

    public void updateDrawState(TextPaint ds) {}
}
