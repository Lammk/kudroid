package android.content.res;

public class ColorStateList {
    private final int[][] states;
    private final int[] colors;
    private final int defaultColor;
    public ColorStateList() { this(new int[][]{new int[0]}, new int[]{0}); }
    public ColorStateList(int[][] states, int[] colors) {
        if (states == null || colors == null || states.length != colors.length || colors.length == 0)
            throw new IllegalArgumentException("Invalid color state list");
        this.states = new int[states.length][]; this.colors = colors.clone();
        int def = colors[0];
        for (int i = 0; i < states.length; i++) {
            this.states[i] = states[i].clone();
            if (states[i].length == 0) def = colors[i];
        }
        defaultColor = def;
    }
    private static final int ATTR_COLOR = 0x010101a5;
    private static final int ATTR_ALPHA = 0x0101031f;
    private static final int[] ITEM_ATTRS = {ATTR_COLOR, ATTR_ALPHA};

    /** Inflate a color selector (a <selector> of <item android:color=... state_*=.../>). */
    public static ColorStateList createFromXml(Resources r, org.xmlpull.v1.XmlPullParser parser,
            Resources.Theme theme) throws org.xmlpull.v1.XmlPullParserException, java.io.IOException {
        final android.util.AttributeSet attrs = android.util.Xml.asAttributeSet(parser);
        int type;
        while ((type = parser.next()) != org.xmlpull.v1.XmlPullParser.START_TAG
                && type != org.xmlpull.v1.XmlPullParser.END_DOCUMENT) {
            // Skip to the root element.
        }
        if (type != org.xmlpull.v1.XmlPullParser.START_TAG) {
            throw new org.xmlpull.v1.XmlPullParserException("No start tag found");
        }
        final String name = parser.getName();
        if (!name.equals("selector")) {
            throw new org.xmlpull.v1.XmlPullParserException(
                    parser.getPositionDescription() + ": invalid color state list tag " + name);
        }
        return inflate(r, parser, attrs, theme);
    }

    private static ColorStateList inflate(Resources r, org.xmlpull.v1.XmlPullParser parser,
            android.util.AttributeSet attrs, Resources.Theme theme)
            throws org.xmlpull.v1.XmlPullParserException, java.io.IOException {
        final int innerDepth = parser.getDepth() + 1;
        int depth;
        int type;
        int defaultColor = 0xFFFF0000;
        int[][] stateSpecList = new int[20][];
        int[] colorList = new int[stateSpecList.length];
        int listSize = 0;
        while ((type = parser.next()) != org.xmlpull.v1.XmlPullParser.END_DOCUMENT
                && ((depth = parser.getDepth()) >= innerDepth
                || type != org.xmlpull.v1.XmlPullParser.END_TAG)) {
            if (type != org.xmlpull.v1.XmlPullParser.START_TAG || depth > innerDepth
                    || !parser.getName().equals("item")) {
                continue;
            }
            final TypedArray a = theme != null
                    ? theme.obtainStyledAttributes(attrs, ITEM_ATTRS, 0, 0)
                    : r.obtainAttributes(attrs, ITEM_ATTRS);
            final int baseColor = a.getColor(0, 0xFFFF00FF);
            final float alphaMod = a.getFloat(1, 1.0f);
            a.recycle();
            // Every attribute that is not color or alpha is a state specifier.
            int j = 0;
            final int numAttrs = attrs.getAttributeCount();
            int[] stateSpec = new int[numAttrs];
            for (int i = 0; i < numAttrs; i++) {
                final int stateResId = attrs.getAttributeNameResource(i);
                if (stateResId == ATTR_COLOR || stateResId == ATTR_ALPHA || stateResId == 0) continue;
                stateSpec[j++] = attrs.getAttributeBooleanValue(i, false) ? stateResId : -stateResId;
            }
            int[] trimmed = new int[j];
            System.arraycopy(stateSpec, 0, trimmed, 0, j);
            final int color = modulateColorAlpha(baseColor, alphaMod);
            if (listSize == 0 || trimmed.length == 0) defaultColor = color;
            if (listSize + 1 > colorList.length) {
                int[][] newSpecs = new int[listSize * 2][];
                int[] newColors = new int[listSize * 2];
                System.arraycopy(stateSpecList, 0, newSpecs, 0, listSize);
                System.arraycopy(colorList, 0, newColors, 0, listSize);
                stateSpecList = newSpecs;
                colorList = newColors;
            }
            colorList[listSize] = color;
            stateSpecList[listSize] = trimmed;
            listSize++;
        }
        if (listSize == 0) return valueOf(defaultColor);
        final int[] colors = new int[listSize];
        final int[][] stateSpecs = new int[listSize][];
        System.arraycopy(colorList, 0, colors, 0, listSize);
        System.arraycopy(stateSpecList, 0, stateSpecs, 0, listSize);
        return new ColorStateList(stateSpecs, colors);
    }

    private static int modulateColorAlpha(int baseColor, float alphaMod) {
        if (alphaMod == 1.0f) return baseColor;
        final int baseAlpha = baseColor >>> 24;
        final int alpha = Math.max(0, Math.min(255, (int) (baseAlpha * alphaMod + 0.5f)));
        return (baseColor & 0xFFFFFF) | (alpha << 24);
    }

    public static ColorStateList valueOf(int color) { return new ColorStateList(new int[][]{new int[0]}, new int[]{color}); }
    public int getDefaultColor() { return defaultColor; }
    public boolean isStateful() { for (int[] spec : states) if (spec.length != 0) return true; return false; }
    public boolean isOpaque() { for (int color : colors) if ((color >>> 24) != 255) return false; return true; }
    public int getColorForState(int[] stateSet, int def) {
        for (int i = 0; i < states.length; i++) {
            boolean match = true;
            for (int state : states[i]) {
                if (state == 0) break;
                boolean found = false;
                if (stateSet != null) for (int active : stateSet) if (active == Math.abs(state)) { found = true; break; }
                if (found != (state > 0)) { match = false; break; }
            }
            if (match) return colors[i];
        }
        return def;
    }
    public ColorStateList withAlpha(int alpha) {
        int[] copy = colors.clone();
        for (int i = 0; i < copy.length; i++) copy[i] = (copy[i] & 0xffffff) | ((alpha & 255) << 24);
        return new ColorStateList(states, copy);
    }
}
