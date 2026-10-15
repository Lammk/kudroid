package android.graphics.drawable;

/**
 * android.graphics.drawable.LayerDrawable — several drawables stacked back to front.
 *
 * Was an empty generated stub with only a no-argument constructor; all five real APKs in the
 * corpus construct it as {@code LayerDrawable([Landroid/graphics/drawable/Drawable;)V},
 * which is the only form that makes sense — the array IS the content.
 *
 * Extends Drawable so it can be used wherever a background can, which the stub could not.
 */
public class LayerDrawable extends Drawable {
    private final Drawable[] mLayers;
    private final int[] mInsetLeft;
    private final int[] mInsetTop;
    private final int[] mInsetRight;
    private final int[] mInsetBottom;

    public LayerDrawable(Drawable[] layers) {
        mLayers = layers != null ? layers : new Drawable[0];
        mInsetLeft = new int[mLayers.length];
        mInsetTop = new int[mLayers.length];
        mInsetRight = new int[mLayers.length];
        mInsetBottom = new int[mLayers.length];
    }

    public int getNumberOfLayers() {
        return mLayers.length;
    }

    public Drawable getDrawable(int index) {
        return index >= 0 && index < mLayers.length ? mLayers[index] : null;
    }

    public boolean setDrawableByLayerId(int id, Drawable drawable) {
        return false;  // no layer ids without resource parsing
    }

    public Drawable findDrawableByLayerId(int id) {
        return null;
    }

    public void setLayerInset(int index, int l, int t, int r, int b) {
        if (index < 0 || index >= mLayers.length) return;
        mInsetLeft[index] = l;
        mInsetTop[index] = t;
        mInsetRight[index] = r;
        mInsetBottom[index] = b;
    }

    @Override
    public void setBounds(int left, int top, int right, int bottom) {
        super.setBounds(left, top, right, bottom);
        // Insets are applied here rather than at draw time so each layer's own getBounds()
        // reports where it will actually be drawn.
        for (int i = 0; i < mLayers.length; ++i) {
            if (mLayers[i] == null) continue;
            mLayers[i].setBounds(left + mInsetLeft[i], top + mInsetTop[i],
                                 right - mInsetRight[i], bottom - mInsetBottom[i]);
        }
    }

    /** Back to front, which is the order that makes the last layer the visible one. */
    @Override
    public void draw(android.graphics.Canvas canvas) {
        for (Drawable layer : mLayers) {
            if (layer != null) layer.draw(canvas);
        }
    }

    @Override
    public void setAlpha(int alpha) {
        for (Drawable layer : mLayers) {
            if (layer != null) layer.setAlpha(alpha);
        }
    }

    /**
     * The largest intrinsic size among the layers, which is what the stack needs to show all
     * of them. -1 when no layer has one, meaning "no inherent size".
     */
    @Override
    public int getIntrinsicWidth() {
        int widest = -1;
        for (Drawable layer : mLayers) {
            if (layer == null) continue;
            final int w = layer.getIntrinsicWidth();
            if (w > widest) widest = w;
        }
        return widest;
    }

    @Override
    public int getIntrinsicHeight() {
        int tallest = -1;
        for (Drawable layer : mLayers) {
            if (layer == null) continue;
            final int h = layer.getIntrinsicHeight();
            if (h > tallest) tallest = h;
        }
        return tallest;
    }

    /**
     * TRANSLUCENT unless every layer is opaque.
     *
     * Erring towards translucent is the safe direction: a parent told OPAQUE skips drawing
     * behind this, and any gap then shows stale buffer contents.
     */
    @Override
    public int getOpacity() {
        if (mLayers.length == 0) return 0;  // TRANSPARENT
        for (Drawable layer : mLayers) {
            if (layer == null || layer.getOpacity() != -1) return -3;  // TRANSLUCENT
        }
        return -1;  // OPAQUE
    }
}
