package android.graphics.drawable;

import android.content.res.ColorStateList;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Rect;

/** Content layers with state feedback clipped to the mask alpha. */
public class RippleDrawable extends LayerDrawable {
    private static final int MASK = 0x0102002e;
    private ColorStateList color;
    private int alpha = 255;
    private boolean active;

    public RippleDrawable(ColorStateList color, Drawable content, Drawable mask) {
        super(content == null ? (mask == null ? new Drawable[0] : new Drawable[]{mask})
                : (mask == null ? new Drawable[]{content} : new Drawable[]{content, mask}));
        if (mask != null) setId(getNumberOfLayers() - 1, MASK);
        setColor(color);
    }

    RippleDrawable(ColorStateList color, Drawable[] layers) {
        super(layers);
        setColor(color);
    }

    public void setColor(ColorStateList value) {
        if (value == null) throw new IllegalArgumentException("color == null");
        color = value;
        invalidateSelf();
    }

    public ColorStateList getColor() { return color; }
    @Override public boolean isStateful() { return true; }
    @Override public void setAlpha(int value) {
        alpha = Math.max(0, Math.min(255, value));
        for (int i = 0; i < getNumberOfLayers(); i++) {
            Drawable layer = getDrawable(i);
            if (layer != null && getId(i) != MASK) layer.setAlpha(alpha);
        }
        invalidateSelf();
    }
    @Override public int getAlpha() { return alpha; }
    @Override protected boolean onStateChange(int[] states) {
        boolean enabled = false, interacting = false;
        for (int state : states) {
            if (state == 0x0101009e) enabled = true;
            if (state == 0x010100a7 || state == 0x0101009c || state == 0x01010367)
                interacting = true;
        }
        active = enabled && interacting;
        super.onStateChange(states);
        invalidateSelf();
        return true;
    }

    @Override public void draw(Canvas canvas) {
        if (!isVisible()) return;
        Drawable mask = findDrawableByLayerId(MASK);
        for (int i = 0; i < getNumberOfLayers(); i++) {
            Drawable layer = getDrawable(i);
            if (layer != null && getId(i) != MASK) layer.draw(canvas);
        }
        Rect bounds = getBounds();
        int width = bounds.width(), height = bounds.height();
        if (!active || width <= 0 || height <= 0) return;
        int tint = color.getColorForState(getState(), color.getDefaultColor());
        int opacity = (tint >>> 24) * alpha / 255;
        if (opacity == 0) return;
        Bitmap bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        if (mask != null || getNumberOfLayers() > 0) {
            Canvas buffer = new Canvas(bitmap);
            if (mask != null) drawLocal(mask, buffer);
            else for (int i = 0; i < getNumberOfLayers(); i++)
                drawLocal(getDrawable(i), buffer);
        } else bitmap.eraseColor(0xffffffff);
        int[] pixels = bitmap.getPixels();
        for (int i = 0; i < pixels.length; i++)
            pixels[i] = ((pixels[i] >>> 24) * opacity / 255 << 24) | (tint & 0xffffff);
        canvas.drawBitmap(bitmap, bounds.left, bounds.top, null);
        bitmap.recycle();
    }

    private void drawLocal(Drawable drawable, Canvas canvas) {
        if (drawable == null) return;
        Rect original = drawable.copyBounds();
        Rect bounds = getBounds();
        Drawable.Callback callback = drawable.getCallback();
        drawable.setCallback(null);
        try {
            drawable.setBounds(original.left - bounds.left, original.top - bounds.top,
                    original.right - bounds.left, original.bottom - bounds.top);
            drawable.draw(canvas);
        } finally {
            drawable.setBounds(original);
            drawable.setCallback(callback);
        }
    }
    @Override public int getOpacity() { return -3; }
}
