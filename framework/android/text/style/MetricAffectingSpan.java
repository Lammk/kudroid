package android.text.style;

import android.text.TextPaint;

public abstract class MetricAffectingSpan extends CharacterStyle {
    public abstract void updateMeasureState(TextPaint paint);
    @Override public MetricAffectingSpan getUnderlying() { return this; }
}
