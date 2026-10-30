package android.graphics.drawable;

import android.content.res.ColorStateList;
import android.content.res.Resources;
import android.content.res.TypedArray;
import android.content.res.XmlResourceParser;
import android.util.TypedValue;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;
import java.io.IOException;
import java.util.ArrayList;

/** Inflate drawable trees without changing the active resource table. */
public final class DrawableInflater {
    private static final int[] COLOR = {0x010101a5};
    private static final int[] ITEM = {0x010100d0, 0x01010199};
    private DrawableInflater() {}

    private static TypedArray attributes(Resources r, Resources.Theme theme,
            XmlResourceParser parser, int[] ids) {
        return theme == null ? r.obtainAttributes(parser, ids)
                : theme.obtainStyledAttributes(parser, ids, 0, 0);
    }

    private static final int[] STATE_ATTRS = {
        0x0101009d, 0x010100a1, 0x0101009c, 0x0101009e, 0x010100a7, 0x010102fe, 0x0101031b,
        0x01010367, 0x01010368, 0x01010369, 0x010100a0, 0x0101009f, 0x010100a2, 0x010100a3,
        0x010100a4, 0x010100a5, 0x010100a6
    };
    private static final int ATTR_ID = 0x010100d0;
    private static final int ATTR_DRAWABLE = 0x01010199;
    private static final int[] SHAPE = {0x0101019a};
    private static final int[] SOLID = {0x010101a5};
    private static final int[] STROKE = {0x01010159, 0x010101a5};
    private static final int[] CORNERS = {0x010101a8, 0x010101a9, 0x010101aa, 0x010101ab, 0x010101ac};
    private static final int[] SIZE = {0x01010159, 0x01010155};
    private static final int[] GRADIENT = {0x0101019d, 0x0101019e, 0x0101020b};
    private static final int[] INSET = {ATTR_DRAWABLE, 0x010101b7, 0x010101b8, 0x010101b9, 0x010101ba};
    private static final int[] CLIP = {ATTR_DRAWABLE, 0x010100af, 0x0101020a};

    private static boolean isStateAttr(int id) {
        if ((id >>> 24) == 0x7f) return true;
        for (int s : STATE_ATTRS) if (s == id) return true;
        return false;
    }

    private static void skipElement(XmlResourceParser parser) throws XmlPullParserException, IOException {
        final int depth = parser.getDepth();
        while (parser.next() != XmlPullParser.END_DOCUMENT) {
            if (parser.getEventType() == XmlPullParser.END_TAG && parser.getDepth() == depth) return;
        }
    }

    /** The drawable named by an element's own child element, or null when it has none. */
    private static Drawable firstChild(Resources r, XmlResourceParser parser, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        final int depth = parser.getDepth();
        Drawable child = null;
        while (parser.next() != XmlPullParser.END_DOCUMENT) {
            if (parser.getEventType() == XmlPullParser.END_TAG && parser.getDepth() == depth) break;
            if (parser.getEventType() != XmlPullParser.START_TAG) continue;
            if (child == null) child = create(r, parser, theme);
            else skipElement(parser);
        }
        return child;
    }

    private static Drawable resourceDrawable(Resources r, Resources.Theme theme, TypedArray a, int index) {
        if (a.getResourceId(index, 0) == 0) {
            final TypedValue value = a.peekValue(index);
            if (value != null && value.type >= TypedValue.TYPE_FIRST_COLOR_INT
                    && value.type <= TypedValue.TYPE_LAST_COLOR_INT) {
                return new ColorDrawable(value.data);
            }
            return null;
        }
        try {
            return r.getDrawable(a.getResourceId(index, 0), theme);
        } catch (Resources.NotFoundException e) {
            return null;
        }
    }

    public static Drawable create(Resources r, XmlResourceParser parser, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        String tag = parser.getName();
        if ("vector".equals(tag)) return VectorDrawable.create(r, parser, theme);
        if ("selector".equals(tag) || "animated-selector".equals(tag)) return selector(r, parser, theme);
        if ("shape".equals(tag)) return shape(r, parser, theme);
        if ("inset".equals(tag)) return inset(r, parser, theme);
        if ("clip".equals(tag)) return clip(r, parser, theme);
        if ("color".equals(tag)) {
            TypedArray a = attributes(r, theme, parser, SOLID);
            Drawable d = new ColorDrawable(a.getColor(0, 0));
            a.recycle();
            skipElement(parser);
            return d;
        }
        if ("bitmap".equals(tag) || "nine-patch".equals(tag)) {
            skipElement(parser);
            return new BitmapDrawable();
        }
        boolean ripple = "ripple".equals(tag);
        if (!ripple && !"layer-list".equals(tag)) {
            // An element this framework cannot build: draw nothing rather than fail the
            // whole view that asked for it.
            android.util.Log.w("DrawableInflater", "Unsupported drawable element " + tag);
            skipElement(parser);
            return new ColorDrawable(0);
        }
        ColorStateList color = null;
        if (ripple) {
            TypedArray a = attributes(r, theme, parser, COLOR);
            color = a.getColorStateList(0);
            a.recycle();
            if (color == null) throw new XmlPullParserException("Ripple requires a color");
        }
        int depth = parser.getDepth();
        ArrayList<Drawable> layers = new ArrayList<>();
        ArrayList<Integer> ids = new ArrayList<>();
        while (parser.next() != XmlPullParser.END_DOCUMENT) {
            if (parser.getEventType() == XmlPullParser.END_TAG && parser.getDepth() == depth) break;
            if (parser.getEventType() != XmlPullParser.START_TAG) continue;
            if (!"item".equals(parser.getName()) || parser.getDepth() != depth + 1)
                throw new XmlPullParserException("Expected drawable item");
            TypedArray a = attributes(r, theme, parser, ITEM);
            int id = a.getResourceId(0, -1);
            TypedValue value = a.peekValue(1);
            Drawable child = null;
            if (value != null) {
                if (value.type >= TypedValue.TYPE_FIRST_COLOR_INT
                        && value.type <= TypedValue.TYPE_LAST_COLOR_INT)
                    child = new ColorDrawable(value.data);
                else {
                    int resource = a.getResourceId(1, 0);
                    if (resource != 0) child = r.getDrawable(resource, theme);
                }
            }
            a.recycle();
            int itemDepth = parser.getDepth();
            while (parser.next() != XmlPullParser.END_DOCUMENT) {
                if (parser.getEventType() == XmlPullParser.END_TAG
                        && parser.getDepth() == itemDepth) break;
                if (parser.getEventType() != XmlPullParser.START_TAG) continue;
                if (child != null) throw new XmlPullParserException("Multiple drawables in item");
                child = create(r, parser, theme);
            }
            if (child == null) throw new XmlPullParserException("Drawable item has no drawable");
            layers.add(child);
            ids.add(id);
        }
        Drawable[] children = layers.toArray(new Drawable[layers.size()]);
        LayerDrawable result = ripple ? new RippleDrawable(color, children)
                : new LayerDrawable(children);
        for (int i = 0; i < ids.size(); i++) result.setId(i, ids.get(i));
        return result;
    }

    private static Drawable selector(Resources r, XmlResourceParser parser, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        final StateListDrawable list = new StateListDrawable();
        final int depth = parser.getDepth();
        while (parser.next() != XmlPullParser.END_DOCUMENT) {
            if (parser.getEventType() == XmlPullParser.END_TAG && parser.getDepth() == depth) break;
            if (parser.getEventType() != XmlPullParser.START_TAG) continue;
            if (!"item".equals(parser.getName())) {
                skipElement(parser);
                continue;
            }
            // State attributes are the item's boolean attributes; the rest describe the item.
            final int count = parser.getAttributeCount();
            int[] states = new int[count];
            int n = 0;
            for (int i = 0; i < count; i++) {
                final int name = parser.getAttributeNameResource(i);
                if (name == 0 || name == ATTR_ID || name == ATTR_DRAWABLE || !isStateAttr(name)) continue;
                states[n++] = parser.getAttributeBooleanValue(i, false) ? name : -name;
            }
            states = java.util.Arrays.copyOf(states, n);
            TypedArray a = attributes(r, theme, parser, new int[] {ATTR_DRAWABLE});
            Drawable child = resourceDrawable(r, theme, a, 0);
            a.recycle();
            final Drawable nested = firstChild(r, parser, theme);
            if (child == null) child = nested;
            if (child != null) list.addState(states, child);
        }
        return list;
    }

    private static Drawable shape(Resources r, XmlResourceParser parser, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        final GradientDrawable shape = new GradientDrawable();
        TypedArray a = attributes(r, theme, parser, SHAPE);
        shape.setShape(a.getInt(0, GradientDrawable.RECTANGLE));
        a.recycle();
        final int depth = parser.getDepth();
        while (parser.next() != XmlPullParser.END_DOCUMENT) {
            if (parser.getEventType() == XmlPullParser.END_TAG && parser.getDepth() == depth) break;
            if (parser.getEventType() != XmlPullParser.START_TAG) continue;
            final String name = parser.getName();
            if ("solid".equals(name)) {
                a = attributes(r, theme, parser, SOLID);
                final ColorStateList color = a.getColorStateList(0);
                if (color != null) shape.setColor(color);
                a.recycle();
            } else if ("stroke".equals(name)) {
                a = attributes(r, theme, parser, STROKE);
                final ColorStateList color = a.getColorStateList(1);
                shape.setStroke(a.getDimensionPixelSize(0, 0), color != null ? color
                        : ColorStateList.valueOf(0xFF000000));
                a.recycle();
            } else if ("corners".equals(name)) {
                a = attributes(r, theme, parser, CORNERS);
                final float radius = a.getDimension(0, 0f);
                final float tl = a.getDimension(1, radius), tr = a.getDimension(2, radius);
                final float bl = a.getDimension(3, radius), br = a.getDimension(4, radius);
                if (tl == radius && tr == radius && bl == radius && br == radius) {
                    shape.setCornerRadius(radius);
                } else {
                    shape.setCornerRadii(new float[] {tl, tl, tr, tr, br, br, bl, bl});
                }
                a.recycle();
            } else if ("size".equals(name)) {
                a = attributes(r, theme, parser, SIZE);
                shape.setSize(a.getDimensionPixelSize(0, -1), a.getDimensionPixelSize(1, -1));
                a.recycle();
            } else if ("gradient".equals(name)) {
                a = attributes(r, theme, parser, GRADIENT);
                final int start = a.getColor(0, 0), end = a.getColor(1, 0);
                if (a.hasValue(2)) shape.setColors(new int[] {start, a.getColor(2, 0), end});
                else shape.setColors(new int[] {start, end});
                a.recycle();
            }
            skipElement(parser);
        }
        return shape;
    }

    private static Drawable inset(Resources r, XmlResourceParser parser, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        TypedArray a = attributes(r, theme, parser, INSET);
        Drawable child = resourceDrawable(r, theme, a, 0);
        final int l = a.getDimensionPixelOffset(1, 0), rt = a.getDimensionPixelOffset(2, 0);
        final int t = a.getDimensionPixelOffset(3, 0), b = a.getDimensionPixelOffset(4, 0);
        a.recycle();
        final Drawable nested = firstChild(r, parser, theme);
        if (child == null) child = nested;
        if (child == null) child = new ColorDrawable(0);
        return new InsetDrawable(child, l, t, rt, b);
    }

    private static Drawable clip(Resources r, XmlResourceParser parser, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        TypedArray a = attributes(r, theme, parser, CLIP);
        Drawable child = resourceDrawable(r, theme, a, 0);
        final int gravity = a.getInt(1, 3), orientation = a.getInt(2, ClipDrawable.HORIZONTAL);
        a.recycle();
        final Drawable nested = firstChild(r, parser, theme);
        if (child == null) child = nested;
        if (child == null) child = new ColorDrawable(0);
        return new ClipDrawable(child, gravity, orientation);
    }
}
