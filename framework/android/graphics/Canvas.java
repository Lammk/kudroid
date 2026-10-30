package android.graphics;

/**
 * Implement android.graphics.Canvas with direct C++ Metal Native Canvas connection.
 */
public class Canvas {
    private Bitmap mBitmap;
    private int mWidth;
    private int mHeight;
    private float mTranslateX = 0.0f;
    private float mTranslateY = 0.0f;
    private float mScaleX = 1.0f;
    private float mScaleY = 1.0f;
    // Group alpha applied to every primitive; saveLayerAlpha() multiplies into it.
    private float mAlpha = 1.0f;
    // Device-space clip rectangle; clipping is axis aligned only.
    private boolean mHasClip;
    private float mClipLeft, mClipTop, mClipRight, mClipBottom;
    private final java.util.ArrayList<float[]> mStack = new java.util.ArrayList<>();

    public static final int ALL_SAVE_FLAG = 31;
    public static final int MATRIX_SAVE_FLAG = 1;
    public static final int CLIP_SAVE_FLAG = 2;
    public static final int HAS_ALPHA_LAYER_SAVE_FLAG = 4;
    public static final int FULL_COLOR_LAYER_SAVE_FLAG = 8;
    public static final int CLIP_TO_LAYER_SAVE_FLAG = 16;

    // Native JNI methods connect to C++ Metal Pipeline
    private static native void native_drawColor(int color);
    private static native void native_drawRect(float left, float top, float right, float bottom, int color);
    private static native void native_drawText(String text, float x, float y, int color, float textSize);
    private static native void native_drawBitmapText(int[] pixels, int width, int height,
            String text, float x, float y, int color, float textSize,
            float shadowRadius, float shadowDx, float shadowDy, int shadowColor);
    private static native void native_drawBitmap(int[] pixels, int width, int height, float x, float y,
            int alpha, int filterColor, int filterMode);
    private static native void native_drawBitmapRegion(int[] pixels, int srcW, int srcH,
            int srcX, int srcY, int subW, int subH, float dstX, float dstY, float dstW, float dstH,
            int alpha, int filterColor, int filterMode);
    private static native void native_flush();
    private static native int native_filterColor(int pixel, int color, int mode);
    private static native void native_blitBitmap(int[] target, int targetWidth, int targetHeight,
            int[] source, int sourceWidth, int sourceHeight, int srcX, int srcY, int srcWidth, int srcHeight,
            float dstX, float dstY, float dstWidth, float dstHeight,
            int clipLeft, int clipTop, int clipRight, int clipBottom, int alpha, int filterColor, int filterMode);

    private static native void native_drawRoundRect(float left, float top, float right, float bottom,
            float rx, float ry, int color, int style, float strokeWidth);
    private static native void native_drawCircle(float cx, float cy, float radius,
            int color, int style, float strokeWidth);
    private static native void native_drawOval(float left, float top, float right, float bottom,
            int color, int style, float strokeWidth);
    private static native void native_drawLine(float x0, float y0, float x1, float y1,
            int color, float strokeWidth);
    private static native void native_drawPath(byte[] verbs, int verbCount, float[] coords, int coordCount,
            int color, int style, float strokeWidth, boolean evenOdd);

    private static native int native_save();
    private static native void native_restore();
    private static native void native_restoreToCount(int saveCount);
    private static native void native_translate(float dx, float dy);
    private static native void native_scale(float sx, float sy);
    private static native void native_rotate(float deg);
    private static native void native_concat(float a, float b, float c, float d, float tx, float ty);
    private static native void native_clipRect(float left, float top, float right, float bottom);

    // Real surface size, set by kudroid_set_metal_layer from the CAMetalLayer.
    // These used to be hardcoded to 1080x1920, so on any other screen the layout was
    // computed for the wrong extent and everything past the real width was clipped.
    private static native int native_getSurfaceWidth();
    private static native int native_getSurfaceHeight();
    private static native boolean native_isSurfaceReady();

    /** Fallback used only if the native surface has not been bound yet. */
    private static final int DEFAULT_WIDTH = 1080;
    private static final int DEFAULT_HEIGHT = 1920;

    public Canvas() {
        int w = DEFAULT_WIDTH;
        int h = DEFAULT_HEIGHT;
        try {
            int nw = native_getSurfaceWidth();
            int nh = native_getSurfaceHeight();
            if (nw > 0 && nh > 0) {
                w = nw;
                h = nh;
            }
        } catch (Throwable ignored) {}
        mWidth = w;
        mHeight = h;
    }

    public Canvas(Bitmap bitmap) {
        this();
        mBitmap = bitmap;
        if (bitmap != null) {
            mWidth = bitmap.getWidth();
            mHeight = bitmap.getHeight();
        }
    }

    public int getWidth() {
        return mWidth;
    }

    public int getHeight() {
        return mHeight;
    }

    /** True only after the host has attached a real framebuffer, not fallback dimensions. */
    public static boolean isSurfaceReady() {
        try {
            return native_isSurfaceReady();
        } catch (Throwable ignored) {
            return false;
        }
    }

    public void drawColor(int color) {
        if (mBitmap != null) {
            mBitmap.eraseColor(color);
            return;
        }
        try {
            native_drawColor(color);
        } catch (Throwable t) {}
    }

    public void drawColor(int color, PorterDuff.Mode mode) {
        drawColor(color);
    }

    public void drawColor(int color, BlendMode mode) {
        drawColor(color);
    }

    public void drawARGB(int a, int r, int g, int b) {
        drawColor((a << 24) | (r << 16) | (g << 8) | b);
    }

    public void drawRGB(int r, int g, int b) {
        drawColor(0xFF000000 | (r << 16) | (g << 8) | b);
    }

    public void drawPaint(Paint paint) {
        drawRect(0, 0, mWidth, mHeight, paint);
    }

    // ---- state: save/restore, translate/scale, clip

    private float[] snapshot() {
        return new float[] {mTranslateX, mTranslateY, mScaleX, mScaleY, mHasClip ? 1f : 0f,
                mClipLeft, mClipTop, mClipRight, mClipBottom, mAlpha};
    }

    public int save() {
        mStack.add(snapshot());
        if (mBitmap == null) {
            try {
                return native_save();
            } catch (Throwable ignored) {}
        }
        return mStack.size();
    }

    public int save(int saveFlags) {
        return save();
    }

    public int saveLayer(RectF bounds, Paint paint) { return save(); }
    public int saveLayer(RectF bounds, Paint paint, int saveFlags) { return save(); }
    public int saveLayer(float left, float top, float right, float bottom, Paint paint) { return save(); }
    public int saveLayer(float left, float top, float right, float bottom, Paint paint, int saveFlags) { return save(); }
    /** The layer is not composited off-screen; its alpha scales each primitive drawn inside. */
    public int saveLayerAlpha(RectF bounds, int alpha) { return saveAlpha(alpha); }
    public int saveLayerAlpha(RectF bounds, int alpha, int saveFlags) { return saveAlpha(alpha); }
    public int saveLayerAlpha(float left, float top, float right, float bottom, int alpha) { return saveAlpha(alpha); }
    public int saveLayerAlpha(float left, float top, float right, float bottom, int alpha, int saveFlags) { return saveAlpha(alpha); }

    private int saveAlpha(int alpha) {
        final int count = save();
        mAlpha *= Math.max(0, Math.min(255, alpha)) / 255f;
        return count;
    }

    /** Applies the group alpha to an ARGB colour. */
    private int withAlpha(int color) {
        if (mAlpha >= 1f) return color;
        final int a = Math.round(((color >>> 24) & 0xFF) * mAlpha);
        return (a << 24) | (color & 0x00FFFFFF);
    }

    public int getSaveCount() {
        return mStack.size() + 1;
    }

    private static int filterMode(ColorFilter filter) {
        PorterDuff.Mode mode = null;
        if (filter instanceof PorterDuffColorFilter) mode = ((PorterDuffColorFilter) filter).getMode();
        else if (filter instanceof BlendModeColorFilter)
            mode = BlendMode.blendModeToPorterDuffMode(((BlendModeColorFilter) filter).getMode());
        return mode == null ? -1 : mode.nativeInt;
    }
    private static int filterColor(ColorFilter filter) {
        if (filter instanceof PorterDuffColorFilter) return ((PorterDuffColorFilter) filter).getColor();
        if (filter instanceof BlendModeColorFilter) return ((BlendModeColorFilter) filter).getColor();
        return 0;
    }
    private int paintColor(Paint paint) {
        int color = paint == null ? 0xffffffff : paint.getColor();
        ColorFilter filter = paint == null ? null : paint.getColorFilter();
        int mode = filterMode(filter);
        if (mode >= 0) color = native_filterColor(color, filterColor(filter), mode);
        return withAlpha(color);
    }

    public void restore() {
        if (mStack.isEmpty()) return;
        final float[] s = mStack.remove(mStack.size() - 1);
        mTranslateX = s[0];
        mTranslateY = s[1];
        mScaleX = s[2];
        mScaleY = s[3];
        mHasClip = s[4] != 0f;
        mClipLeft = s[5];
        mClipTop = s[6];
        mClipRight = s[7];
        mClipBottom = s[8];
        mAlpha = s[9];
        if (mBitmap == null) {
            try {
                native_restore();
            } catch (Throwable ignored) {}
        }
    }

    public void restoreToCount(int saveCount) {
        if (saveCount < 1) saveCount = 1;
        while (mStack.size() >= saveCount) {
            restore();
        }
    }

    public void translate(float dx, float dy) {
        mTranslateX += dx * mScaleX;
        mTranslateY += dy * mScaleY;
        if (mBitmap == null) {
            try {
                native_translate(dx, dy);
            } catch (Throwable ignored) {}
        }
    }

    public void scale(float sx, float sy) {
        mScaleX *= sx;
        mScaleY *= sy;
        if (mBitmap == null) {
            try {
                native_scale(sx, sy);
            } catch (Throwable ignored) {}
        }
    }

    public final void scale(float sx, float sy, float px, float py) {
        translate(px, py);
        scale(sx, sy);
        translate(-px, -py);
    }

    public void rotate(float degrees) {
        if (mBitmap == null) {
            try {
                native_rotate(degrees);
            } catch (Throwable ignored) {}
        }
    }

    public final void rotate(float degrees, float px, float py) {
        translate(px, py);
        rotate(degrees);
        translate(-px, -py);
    }

    public void skew(float sx, float sy) {}

    /** Applies the scale and translation parts of the matrix; rotation and skew are ignored. */
    public void concat(Matrix matrix) {
        if (matrix == null) return;
        final float[] v = new float[9];
        matrix.getValues(v);
        translate(v[Matrix.MTRANS_X], v[Matrix.MTRANS_Y]);
        scale(v[Matrix.MSCALE_X], v[Matrix.MSCALE_Y]);
        if (mBitmap == null) {
            try {
                native_concat(v[0], v[3], v[1], v[4], v[2], v[5]);
            } catch (Throwable ignored) {}
        }
    }

    public void setMatrix(Matrix matrix) {
        mTranslateX = 0f;
        mTranslateY = 0f;
        mScaleX = 1f;
        mScaleY = 1f;
        concat(matrix);
    }

    public void getMatrix(Matrix ctm) {
        ctm.setScale(mScaleX, mScaleY);
        ctm.postTranslate(mTranslateX, mTranslateY);
    }

    public final Matrix getMatrix() {
        final Matrix m = new Matrix();
        getMatrix(m);
        return m;
    }

    public boolean clipRect(float left, float top, float right, float bottom) {
        final float l = left * mScaleX + mTranslateX;
        final float t = top * mScaleY + mTranslateY;
        final float r = right * mScaleX + mTranslateX;
        final float b = bottom * mScaleY + mTranslateY;
        if (!mHasClip) {
            mHasClip = true;
            mClipLeft = l;
            mClipTop = t;
            mClipRight = r;
            mClipBottom = b;
        } else {
            mClipLeft = Math.max(mClipLeft, l);
            mClipTop = Math.max(mClipTop, t);
            mClipRight = Math.min(mClipRight, r);
            mClipBottom = Math.min(mClipBottom, b);
        }
        if (mBitmap == null) {
            try {
                native_clipRect(left, top, right, bottom);
            } catch (Throwable ignored) {}
        }
        return mClipRight > mClipLeft && mClipBottom > mClipTop;
    }

    public boolean clipRect(float left, float top, float right, float bottom, Region.Op op) {
        return op == Region.Op.INTERSECT || op == Region.Op.REPLACE
                ? clipRect(left, top, right, bottom) : true;
    }

    public boolean clipRect(int left, int top, int right, int bottom) {
        return clipRect((float) left, (float) top, (float) right, (float) bottom);
    }

    public boolean clipRect(Rect rect) {
        return clipRect(rect.left, rect.top, rect.right, rect.bottom);
    }

    public boolean clipRect(Rect rect, Region.Op op) {
        return clipRect(rect.left, rect.top, rect.right, rect.bottom, op);
    }

    public boolean clipRect(RectF rect) {
        return clipRect(rect.left, rect.top, rect.right, rect.bottom);
    }

    public boolean clipRect(RectF rect, Region.Op op) {
        return clipRect(rect.left, rect.top, rect.right, rect.bottom, op);
    }

    public boolean clipOutRect(RectF rect) { return true; }
    public boolean clipOutRect(Rect rect) { return true; }
    public boolean clipOutRect(float left, float top, float right, float bottom) { return true; }
    public boolean clipOutRect(int left, int top, int right, int bottom) { return true; }

    /** A path clip is reduced to the path's bounds. */
    public boolean clipPath(Path path) {
        final RectF b = new RectF();
        path.computeBounds(b, true);
        return clipRect(b);
    }

    public boolean clipPath(Path path, Region.Op op) {
        return op == Region.Op.INTERSECT || op == Region.Op.REPLACE ? clipPath(path) : true;
    }

    public boolean getClipBounds(Rect bounds) {
        if (!mHasClip) {
            bounds.set(0, 0, mWidth, mHeight);
            return true;
        }
        bounds.set((int) ((mClipLeft - mTranslateX) / mScaleX), (int) ((mClipTop - mTranslateY) / mScaleY),
                (int) ((mClipRight - mTranslateX) / mScaleX), (int) ((mClipBottom - mTranslateY) / mScaleY));
        return !bounds.isEmpty();
    }

    public final Rect getClipBounds() {
        final Rect r = new Rect();
        getClipBounds(r);
        return r;
    }

    public boolean quickReject(RectF rect) { return false; }
    public boolean quickReject(float left, float top, float right, float bottom) { return false; }
    public boolean quickReject(Path path) { return false; }

    public boolean isHardwareAccelerated() { return false; }
    public boolean isOpaque() { return false; }
    public int getDensity() { return 160; }
    public void setDensity(int density) {}
    public void setBitmap(Bitmap bitmap) {
        mBitmap = bitmap;
        if (bitmap != null) {
            mWidth = bitmap.getWidth();
            mHeight = bitmap.getHeight();
        }
    }
    public void enableZ() {}
    public void disableZ() {}

    // ---- drawing

    /** Fills a device-space rectangle, honouring the clip. */
    private void fillDevice(float l, float t, float r, float b, int color) {
        color = withAlpha(color);
        if ((color >>> 24) == 0) return;
        if (mHasClip) {
            l = Math.max(l, mClipLeft);
            t = Math.max(t, mClipTop);
            r = Math.min(r, mClipRight);
            b = Math.min(b, mClipBottom);
        }
        if (r <= l || b <= t) return;
        if (mBitmap != null) {
            mBitmap.fillRect(Math.max(0, (int) l), Math.max(0, (int) t),
                    Math.min(mWidth, (int) r), Math.min(mHeight, (int) b), color);
            return;
        }
        try {
            native_drawRect(l, t, r, b, color);
        } catch (Throwable ignored) {}
    }

    public void drawRect(float left, float top, float right, float bottom, Paint paint) {
        final int color = paintColor(paint);
        if ((color >>> 24) == 0) return;
        if (mBitmap != null) {
            fillDevice(left * mScaleX + mTranslateX, top * mScaleY + mTranslateY,
                    right * mScaleX + mTranslateX, bottom * mScaleY + mTranslateY, color);
            return;
        }
        try {
            native_drawRect(left, top, right, bottom, color);
        } catch (Throwable ignored) {}
    }

    public void drawRect(Rect rect, Paint paint) {
        if (rect != null) {
            drawRect(rect.left, rect.top, rect.right, rect.bottom, paint);
        }
    }

    public void drawRect(RectF rect, Paint paint) {
        if (rect != null) {
            drawRect(rect.left, rect.top, rect.right, rect.bottom, paint);
        }
    }

    /** Rounded corners are built from horizontal strips whose width follows the corner arc. */
    public void drawRoundRect(float left, float top, float right, float bottom, float rx, float ry,
            Paint paint) {
        final int color = paintColor(paint);
        if ((color >>> 24) == 0) return;
        if (mBitmap == null) {
            try {
                int style = (paint != null) ? paint.getStyleInt() : 0;
                float strokeWidth = (paint != null) ? Math.max(1f, paint.getStrokeWidth()) : 1f;
                native_drawRoundRect(left, top, right, bottom, rx, ry, color, style, strokeWidth);
                return;
            } catch (Throwable ignored) {}
        }
        final float w = right - left;
        final float h = bottom - top;
        rx = Math.min(rx, w / 2f);
        ry = Math.min(ry, h / 2f);
        if (rx <= 0f || ry <= 0f || ry > 200f) {
            drawRect(left, top, right, bottom, paint);
            return;
        }
        final int strips = Math.max(1, (int) Math.ceil(ry));
        final float step = ry / strips;
        for (int i = 0; i < strips; i++) {
            final float dy = ry - (i + 0.5f) * step;
            final float inset = rx - rx * (float) Math.sqrt(Math.max(0f, 1f - (dy * dy) / (ry * ry)));
            drawRect(left + inset, top + i * step, right - inset, top + (i + 1) * step, paint);
            drawRect(left + inset, bottom - (i + 1) * step, right - inset, bottom - i * step, paint);
        }
        drawRect(left, top + ry, right, bottom - ry, paint);
    }

    public void drawRoundRect(RectF rect, float rx, float ry, Paint paint) {
        if (rect != null) drawRoundRect(rect.left, rect.top, rect.right, rect.bottom, rx, ry, paint);
    }

    public void drawOval(RectF oval, Paint paint) {
        if (oval != null) drawOval(oval.left, oval.top, oval.right, oval.bottom, paint);
    }

    public void drawOval(float left, float top, float right, float bottom, Paint paint) {
        final int color = paintColor(paint);
        if ((color >>> 24) == 0) return;
        if (mBitmap == null) {
            try {
                int style = (paint != null) ? paint.getStyleInt() : 0;
                float strokeWidth = (paint != null) ? Math.max(1f, paint.getStrokeWidth()) : 1f;
                native_drawOval(left, top, right, bottom, color, style, strokeWidth);
                return;
            } catch (Throwable ignored) {}
        }
        drawRoundRect(left, top, right, bottom, (right - left) / 2f, (bottom - top) / 2f, paint);
    }

    public void drawCircle(float cx, float cy, float radius, Paint paint) {
        if (radius <= 0f) return;
        final int color = paintColor(paint);
        if ((color >>> 24) == 0) return;
        if (mBitmap == null) {
            try {
                int style = (paint != null) ? paint.getStyleInt() : 0;
                float strokeWidth = (paint != null) ? Math.max(1f, paint.getStrokeWidth()) : 1f;
                native_drawCircle(cx, cy, radius, color, style, strokeWidth);
                return;
            } catch (Throwable ignored) {}
        }
        drawOval(cx - radius, cy - radius, cx + radius, cy + radius, paint);
    }

    public void drawArc(RectF oval, float startAngle, float sweepAngle, boolean useCenter, Paint paint) {}

    public void drawArc(float left, float top, float right, float bottom, float startAngle,
            float sweepAngle, boolean useCenter, Paint paint) {}

    /**
     * Fills a path by scanline (no anti-aliasing), or draws its flattened outline as thin
     * quads for stroke styles.
     */
    public void drawPath(Path path, Paint paint) {
        if (path == null || paint == null || path.isEmpty()) return;
        final int color = paintColor(paint);
        if ((color >>> 24) == 0) return;
        if (mBitmap == null) {
            try {
                int style = paint.getStyleInt();
                float strokeWidth = Math.max(1f, paint.getStrokeWidth());
                boolean evenOdd = (path.getFillType() == Path.FillType.EVEN_ODD
                        || path.getFillType() == Path.FillType.INVERSE_EVEN_ODD);
                native_drawPath(path.mVerbs, path.mVerbCount, path.mCoords, path.mCoordCount,
                        color, style, strokeWidth, evenOdd);
                return;
            } catch (Throwable ignored) {}
        }
        final Paint.Style style = paint.getStyle();
        if (style == Paint.Style.FILL || style == Paint.Style.FILL_AND_STROKE) {
            final java.util.ArrayList<Integer> starts = new java.util.ArrayList<Integer>();
            final float[] flat = path.flatten(0.25f, starts, null);
            fillPolygons(flat, starts, path.getFillType() == Path.FillType.EVEN_ODD
                    || path.getFillType() == Path.FillType.INVERSE_EVEN_ODD, color);
        }
        if (style == Paint.Style.STROKE || style == Paint.Style.FILL_AND_STROKE) {
            strokeFlattened(path, Math.max(1f, paint.getStrokeWidth()), color);
        }
    }

    private void strokeFlattened(Path path, float width, int color) {
        final java.util.ArrayList<Integer> starts = new java.util.ArrayList<Integer>();
        final java.util.ArrayList<Boolean> closed = new java.util.ArrayList<Boolean>();
        final float[] flat = path.flatten(0.25f, starts, closed);
        final int points = flat.length / 3;
        final float half = width / 2f;
        // One quad per segment, all wound the same way, filled non-zero so they merge.
        final java.util.ArrayList<Float> quads = new java.util.ArrayList<Float>();
        final java.util.ArrayList<Integer> quadStarts = new java.util.ArrayList<Integer>();
        for (int c = 0; c < starts.size(); c++) {
            final int begin = starts.get(c);
            final int end = c + 1 < starts.size() ? starts.get(c + 1) : points;
            final int segments = end - begin - 1 + (closed.get(c) ? 1 : 0);
            for (int i = 0; i < segments; i++) {
                final int a = begin + i;
                final int b = begin + (i + 1 < end - begin ? i + 1 : 0);
                final float x0 = flat[a * 3 + 1], y0 = flat[a * 3 + 2];
                final float x1 = flat[b * 3 + 1], y1 = flat[b * 3 + 2];
                final float dx = x1 - x0, dy = y1 - y0;
                final float len = (float) Math.hypot(dx, dy);
                if (len == 0f) continue;
                final float nx = -dy / len * half, ny = dx / len * half;
                quadStarts.add(quads.size() / 3);
                quads.add(0f); quads.add(x0 + nx); quads.add(y0 + ny);
                quads.add(0f); quads.add(x1 + nx); quads.add(y1 + ny);
                quads.add(0f); quads.add(x1 - nx); quads.add(y1 - ny);
                quads.add(0f); quads.add(x0 - nx); quads.add(y0 - ny);
            }
        }
        final float[] flatQuads = new float[quads.size()];
        for (int i = 0; i < flatQuads.length; i++) flatQuads[i] = quads.get(i);
        fillPolygons(flatQuads, quadStarts, false, color);
    }

    /** Scanline fill of closed polygons given as (length, x, y) triples in user space. */
    private void fillPolygons(float[] flat, java.util.ArrayList<Integer> starts, boolean evenOdd, int color) {
        final int points = flat.length / 3;
        if (points < 3 || starts.isEmpty()) return;
        // Device-space edges.
        final float[] ex0 = new float[points], ey0 = new float[points], ex1 = new float[points], ey1 = new float[points];
        final int[] dir = new int[points];
        int edges = 0;
        float minY = Float.MAX_VALUE, maxY = -Float.MAX_VALUE;
        for (int c = 0; c < starts.size(); c++) {
            final int begin = starts.get(c);
            final int end = c + 1 < starts.size() ? starts.get(c + 1) : points;
            if (end - begin < 2) continue;
            for (int i = begin; i < end; i++) {
                final int j = i + 1 < end ? i + 1 : begin;
                final float x0 = flat[i * 3 + 1] * mScaleX + mTranslateX, y0 = flat[i * 3 + 2] * mScaleY + mTranslateY;
                final float x1 = flat[j * 3 + 1] * mScaleX + mTranslateX, y1 = flat[j * 3 + 2] * mScaleY + mTranslateY;
                minY = Math.min(minY, Math.min(y0, y1));
                maxY = Math.max(maxY, Math.max(y0, y1));
                if (y0 == y1) continue;
                ex0[edges] = x0; ey0[edges] = y0; ex1[edges] = x1; ey1[edges] = y1;
                dir[edges] = y1 > y0 ? 1 : -1;
                edges++;
            }
        }
        if (edges == 0) return;
        int yStart = (int) Math.floor(minY);
        int yEnd = (int) Math.ceil(maxY);
        yStart = Math.max(yStart, mHasClip ? (int) Math.floor(mClipTop) : 0);
        yEnd = Math.min(yEnd, mHasClip ? (int) Math.ceil(mClipBottom) : mHeight);
        final float[] xs = new float[edges];
        final int[] ds = new int[edges];
        for (int y = yStart; y < yEnd; y++) {
            final float sample = y + 0.5f;
            int n = 0;
            for (int e = 0; e < edges; e++) {
                final float ya = Math.min(ey0[e], ey1[e]);
                final float yb = Math.max(ey0[e], ey1[e]);
                if (sample < ya || sample >= yb) continue;
                final float t = (sample - ey0[e]) / (ey1[e] - ey0[e]);
                final float x = ex0[e] + t * (ex1[e] - ex0[e]);
                // Insertion sort by x.
                int k = n++;
                while (k > 0 && xs[k - 1] > x) {
                    xs[k] = xs[k - 1];
                    ds[k] = ds[k - 1];
                    k--;
                }
                xs[k] = x;
                ds[k] = dir[e];
            }
            int winding = 0;
            for (int k = 0; k + 1 < n; k++) {
                winding = evenOdd ? (winding ^ 1) : winding + ds[k];
                if (winding != 0) {
                    fillDevice(Math.round(xs[k]), y, Math.round(xs[k + 1]), y + 1, color);
                }
            }
        }
    }

    public void drawPoint(float x, float y, Paint paint) {
        drawRect(x, y, x + 1f, y + 1f, paint);
    }

    public void drawPoints(float[] pts, int offset, int count, Paint paint) {
        for (int i = offset; i + 1 < offset + count; i += 2) drawPoint(pts[i], pts[i + 1], paint);
    }

    public void drawPoints(float[] pts, Paint paint) {
        drawPoints(pts, 0, pts.length, paint);
    }

    public void drawLine(float startX, float startY, float stopX, float stopY, Paint paint) {
        final int color = paintColor(paint);
        if ((color >>> 24) == 0) return;
        if (mBitmap == null) {
            try {
                float strokeWidth = (paint != null) ? Math.max(1f, paint.getStrokeWidth()) : 1f;
                native_drawLine(startX, startY, stopX, stopY, color, strokeWidth);
                return;
            } catch (Throwable ignored) {}
        }
        drawRect(startX, startY, stopX, stopY + 1.0f, paint);
    }

    public void drawText(String text, float x, float y, Paint paint) {
        if (text == null || text.isEmpty()) return;
        final int color = paintColor(paint);
        if ((color >>> 24) == 0) return;
        final float textSize = (paint != null) ? paint.getTextSize() : 16.0f;
        float ax = x;
        if (paint != null && paint.getTextAlign() != Paint.Align.LEFT) {
            final float w = paint.measureText(text);
            ax = paint.getTextAlign() == Paint.Align.CENTER ? x - w / 2f : x - w;
        }
        if (mBitmap != null) {
            final float dx = ax * mScaleX + mTranslateX;
            final float dy = y * mScaleY + mTranslateY;
            if (mHasClip && (dy < mClipTop || dy - textSize > mClipBottom || dx > mClipRight)) return;
            native_drawBitmapText(mBitmap.getPixels(), mWidth, mHeight, text, dx, dy, color, textSize,
                    paint != null ? paint.getShadowRadius() : 0,
                    paint != null ? paint.getShadowDx() : 0,
                    paint != null ? paint.getShadowDy() : 0,
                    paint != null ? paint.getShadowColor() : 0);
            return;
        }
        try {
            native_drawText(text, ax, y, color, textSize);
        } catch (Throwable t) {}
    }

    public void drawText(String text, int start, int end, float x, float y, Paint paint) {
        if (text != null) drawText(text.substring(start, end), x, y, paint);
    }

    public void drawText(CharSequence text, int start, int end, float x, float y, Paint paint) {
        if (text != null) drawText(text.subSequence(start, end).toString(), x, y, paint);
    }

    public void drawText(char[] text, int index, int count, float x, float y, Paint paint) {
        if (text != null) drawText(new String(text, index, count), x, y, paint);
    }

    public void drawBitmap(Bitmap bitmap, float left, float top, Paint paint) {
        if (bitmap == null) return;
        if (mBitmap != null) {
            blitBitmap(bitmap, 0, 0, bitmap.getWidth(), bitmap.getHeight(), left, top,
                    bitmap.getWidth(), bitmap.getHeight(), paint);
            return;
        }
        int[] pixels = bitmap.getPixels();
        if (pixels != null) {
            ColorFilter filter = paint == null ? null : paint.getColorFilter();
            native_drawBitmap(pixels, bitmap.getWidth(), bitmap.getHeight(), left, top,
                    Math.round((paint == null ? 255 : paint.getAlpha()) * mAlpha), filterColor(filter), filterMode(filter));
        }
    }

    private void blitBitmap(Bitmap bitmap, int sx, int sy, int sw, int sh,
            float x, float y, float width, float height, Paint paint) {
        ColorFilter filter = paint == null ? null : paint.getColorFilter();
        native_blitBitmap(mBitmap.getPixels(), mWidth, mHeight, bitmap.getPixels(),
                bitmap.getWidth(), bitmap.getHeight(), sx, sy, sw, sh,
                x * mScaleX + mTranslateX, y * mScaleY + mTranslateY, width * mScaleX, height * mScaleY,
                mHasClip ? bitmapClipEdge(mClipLeft, mWidth) : 0,
                mHasClip ? bitmapClipEdge(mClipTop, mHeight) : 0,
                mHasClip ? bitmapClipEdge(mClipRight, mWidth) : mWidth,
                mHasClip ? bitmapClipEdge(mClipBottom, mHeight) : mHeight,
                Math.round((paint == null ? 255 : paint.getAlpha()) * mAlpha), filterColor(filter), filterMode(filter));
    }

    private static int bitmapClipEdge(float edge, int limit) {
        return (int) Math.ceil(Math.max(0, Math.min(limit, edge)));
    }

    public void drawBitmap(Bitmap bitmap, Rect src, Rect dst, Paint paint) {
        if (bitmap == null || dst == null) return;
        drawBitmap(bitmap, src, new RectF(dst), paint);
    }

    public void drawBitmap(Bitmap bitmap, Rect src, RectF dst, Paint paint) {
        if (bitmap == null || dst == null) return;
        int bw = bitmap.getWidth();
        int bh = bitmap.getHeight();
        if (bw <= 0 || bh <= 0) return;
        int sx = (src != null) ? Math.max(0, src.left) : 0;
        int sy = (src != null) ? Math.max(0, src.top) : 0;
        int sw = (src != null) ? Math.min(bw - sx, src.width()) : bw;
        int sh = (src != null) ? Math.min(bh - sy, src.height()) : bh;
        if (sw <= 0 || sh <= 0) return;

        if (mBitmap == null) {
            int[] pixels = bitmap.getPixels();
            if (pixels != null) {
                try {
                    ColorFilter filter = paint == null ? null : paint.getColorFilter();
                    native_drawBitmapRegion(pixels, bw, bh, sx, sy, sw, sh, dst.left, dst.top, dst.width(), dst.height(),
                            Math.round((paint == null ? 255 : paint.getAlpha()) * mAlpha), filterColor(filter), filterMode(filter));
                    return;
                } catch (Throwable t) {}
            }
        }

        if (mBitmap != null) blitBitmap(bitmap, sx, sy, sw, sh,
                dst.left, dst.top, dst.width(), dst.height(), paint);
    }

    public void drawBitmap(Bitmap bitmap, Matrix matrix, Paint paint) {
        if (matrix == null) {
            drawBitmap(bitmap, 0f, 0f, paint);
            return;
        }
        final float[] v = new float[9];
        matrix.getValues(v);
        drawBitmap(bitmap, v[Matrix.MTRANS_X], v[Matrix.MTRANS_Y], paint);
    }

    public void flush() {
        if (mBitmap != null) return;
        try {
            native_flush();
        } catch (Throwable t) {}
    }

    public Bitmap getBitmap() {
        return mBitmap;
    }
}
