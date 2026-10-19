package android.graphics.drawable;

import android.content.res.Resources;
import android.content.res.XmlResourceParser;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.util.TypedValue;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;
import java.util.ArrayList;

/** Compiled vector tree rendered into an ARGB bitmap. */
public class VectorDrawable extends Drawable {
    private static final int STRIDE = 16;
    private final ArrayList<String> paths = new ArrayList<>();
    private final ArrayList<float[]> commands = new ArrayList<>();
    private final ArrayList<Integer> colors = new ArrayList<>();
    private float viewportWidth, viewportHeight, rootAlpha = 1;
    private int width, height, alpha = 255;
    private Bitmap cache;
    private int cacheWidth, cacheHeight;
    private Integer tint;

    private static native int[] nativeRasterize(String[] paths, float[] commands, int[] colors,
            int width, int height, float viewportWidth, float viewportHeight);

    public VectorDrawable() {}
    private static TypedValue value(Resources r, Resources.Theme theme, XmlResourceParser p, String name) {
        for (int i = 0; i < p.getAttributeCount(); i++) {
            if (!name.equals(p.getAttributeName(i)) ||
                    !"http://schemas.android.com/apk/res/android".equals(p.getAttributeNamespace(i))) continue;
            TypedValue v = p.getAttributeTypedValue(i);
            if (v != null && v.type == TypedValue.TYPE_ATTRIBUTE) {
                TypedValue resolved = new TypedValue();
                if (theme == null || !theme.resolveAttribute(v.data, resolved, true))
                    throw new Resources.NotFoundException("Unresolved vector theme attribute");
                v = resolved;
            }
            if (v != null && v.type == TypedValue.TYPE_REFERENCE && v.data != 0) {
                TypedValue resolved = new TypedValue(); r.getValue(v.data, resolved, true); v = resolved;
            }
            return v;
        }
        return null;
    }
    private static float number(Resources r, Resources.Theme theme, XmlResourceParser p, String name, float def) {
        TypedValue v = value(r, theme, p, name);
        if (v == null) return def;
        if (v.type == TypedValue.TYPE_FLOAT) return Float.intBitsToFloat(v.data);
        if (v.type == TypedValue.TYPE_DIMENSION) return TypedValue.complexToDimension(v.data, r.getDisplayMetrics());
        if (v.type >= 16 && v.type <= 31) return v.data;
        throw new Resources.NotFoundException("Invalid numeric vector attribute " + name);
    }
    private static int color(Resources r, Resources.Theme theme, XmlResourceParser p, String name, int def) {
        TypedValue v = value(r, theme, p, name);
        if (v == null || v.type == TypedValue.TYPE_NULL || (v.type == 1 && v.data == 0)) return def;
        if (v.type >= 28 && v.type <= 31) return v.data;
        throw new Resources.NotFoundException("Unsupported vector color " + name);
    }
    public static VectorDrawable create(Resources r, XmlResourceParser p, Resources.Theme theme)
            throws XmlPullParserException, java.io.IOException {
        p.require(XmlPullParser.START_TAG, null, "vector");
        VectorDrawable v = new VectorDrawable();
        v.viewportWidth = number(r, theme, p, "viewportWidth", 0);
        v.viewportHeight = number(r, theme, p, "viewportHeight", 0);
        v.width = Math.round(number(r, theme, p, "width", 0));
        v.height = Math.round(number(r, theme, p, "height", 0));
        v.rootAlpha = number(r, theme, p, "alpha", 1);
        TypedValue tint = value(r, theme, p, "tint");
        if (tint != null && tint.type >= 28 && tint.type <= 31) v.tint = tint.data;
        if (!(v.viewportWidth > 0) || !(v.viewportHeight > 0) ||
                Float.isInfinite(v.viewportWidth) || Float.isInfinite(v.viewportHeight) ||
                v.width <= 0 || v.height <= 0) throw new XmlPullParserException("Invalid vector size");
        int depth = p.getDepth(), pathCount = 0;
        while (p.next() != XmlPullParser.END_DOCUMENT) {
            if (p.getEventType() == XmlPullParser.END_TAG) {
                if (p.getDepth() == depth) break;
                if ("group".equals(p.getName())) v.add(null, new float[]{3}, 0, 0);
                continue;
            }
            if (p.getEventType() != XmlPullParser.START_TAG) continue;
            String tag = p.getName();
            if ("group".equals(tag)) {
                float rotation = number(r, theme, p, "rotation", 0);
                float px = number(r, theme, p, "pivotX", 0), py = number(r, theme, p, "pivotY", 0);
                float sx = number(r, theme, p, "scaleX", 1), sy = number(r, theme, p, "scaleY", 1);
                float tx = number(r, theme, p, "translateX", 0), ty = number(r, theme, p, "translateY", 0);
                double radians = rotation * Math.PI / 180;
                float c = (float) Math.cos(radians), s = (float) Math.sin(radians);
                float a = c * sx, b = s * sx, cc = -s * sy, d = c * sy;
                v.add(null, new float[]{2, a, b, cc, d, tx + px - a * px - cc * py,
                        ty + py - b * px - d * py}, 0, 0);
            } else if ("path".equals(tag) || "clip-path".equals(tag)) {
                if ("path".equals(tag)) pathCount++;
                TypedValue data = value(r, theme, p, "pathData");
                if (data == null || data.string == null) throw new XmlPullParserException("Missing vector pathData");
                float[] command = new float[STRIDE];
                command[0] = "clip-path".equals(tag) ? 1 : 0;
                command[1] = number(r, theme, p, "fillAlpha", 1);
                command[2] = number(r, theme, p, "strokeAlpha", 1);
                command[3] = number(r, theme, p, "strokeWidth", 0);
                command[4] = number(r, theme, p, "strokeMiterLimit", 4);
                command[5] = number(r, theme, p, "strokeLineCap", 0);
                command[6] = number(r, theme, p, "strokeLineJoin", 0);
                command[7] = number(r, theme, p, "fillType", 0);
                command[8] = number(r, theme, p, "trimPathStart", 0);
                command[9] = number(r, theme, p, "trimPathEnd", 1);
                command[10] = number(r, theme, p, "trimPathOffset", 0);
                v.add(data.string.toString(), command, color(r, theme, p, "fillColor", 0),
                        color(r, theme, p, "strokeColor", 0));
            } else throw new XmlPullParserException("Unsupported vector element " + tag);
        }
        if (pathCount == 0) throw new XmlPullParserException("Vector has no paths");
        return v;
    }
    private void add(String path, float[] c, int fill, int stroke) {
        float[] command = new float[STRIDE]; System.arraycopy(c, 0, command, 0, c.length);
        commands.add(command); paths.add(path); colors.add(fill); colors.add(stroke);
    }
    @Override public int getIntrinsicWidth() { return width; }
    @Override public int getIntrinsicHeight() { return height; }
    @Override public void setAlpha(int value) { alpha = Math.max(0, Math.min(255, value)); cache = null; invalidateSelf(); }
    public int getAlpha() { return alpha; }
    @Override public void setTint(int color) { tint = color; cache = null; invalidateSelf(); }
    @Override public void setTintList(android.content.res.ColorStateList list) {
        tint = list == null ? null : list.getColorForState(getState(), list.getDefaultColor());
        cache = null; invalidateSelf();
    }
    @Override public void draw(Canvas canvas) {
        Rect bounds = getBounds(); int w = bounds.width(), h = bounds.height();
        if (w <= 0 || h <= 0 || canvas == null) return;
        if (cache == null || cacheWidth != w || cacheHeight != h) {
            float[] operations = new float[commands.size() * STRIDE];
            int[] argb = new int[colors.size()];
            for (int i = 0; i < commands.size(); i++) System.arraycopy(commands.get(i), 0, operations, i * STRIDE, STRIDE);
            for (int i = 0; i < argb.length; i++) argb[i] = colors.get(i);
            int[] pixels = nativeRasterize(paths.toArray(new String[paths.size()]), operations, argb,
                    w, h, viewportWidth, viewportHeight);
            if (pixels == null) throw new Resources.NotFoundException("Cannot rasterize vector drawable");
            for (int i = 0; i < pixels.length; i++) {
                int pixel = pixels[i];
                int a = Math.round((pixel >>> 24) * Math.max(0, Math.min(1, rootAlpha)) * alpha / 255);
                if (tint != null) a = a * (tint >>> 24) / 255;
                pixels[i] = (a << 24) | ((tint != null ? tint : pixel) & 0xffffff);
            }
            cache = Bitmap.createBitmap(pixels, w, h, Bitmap.Config.ARGB_8888);
            cacheWidth = w; cacheHeight = h;
        }
        canvas.drawBitmap(cache, bounds.left, bounds.top, null);
    }
}
