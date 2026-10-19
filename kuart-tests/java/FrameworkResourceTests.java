/** Framework (package 0x01) resources: values, names and identifiers without framework-res. */
public final class FrameworkResourceTests {
    private static void check(boolean ok, String name) { TestSupport.check(ok, name); }

    public static String run() {
        TestSupport.report.setLength(0);
        try { values(); } catch (Throwable t) { TestSupport.fail("values", t); }
        try { identifiers(); } catch (Throwable t) { TestSupport.fail("identifiers", t); }
        try { names(); } catch (Throwable t) { TestSupport.fail("names", t); }
        try { typed(); } catch (Throwable t) { TestSupport.fail("typed", t); }
        return TestSupport.report.toString();
    }

    static final int WHITE = 0x0106000b;
    static final int BLACK = 0x0106000c;

    static void values() {
        android.content.res.Resources r = new android.content.res.Resources();
        check(r.getColor(WHITE) == 0xFFFFFFFF, "android.R.color.white is opaque white");
        check(r.getColor(BLACK) == 0xFF000000, "android.R.color.black is opaque black");
        android.util.TypedValue v = new android.util.TypedValue();
        r.getValue(WHITE, v, true);
        check(v.type == android.util.TypedValue.TYPE_INT_COLOR_ARGB8 && v.resourceId == WHITE,
                "getValue reports the color type and the resolved id");
        check(r.getColorStateList(WHITE).getDefaultColor() == 0xFFFFFFFF,
                "a plain framework color gives a ColorStateList");
        check(r.getString(0x0104000a).equals("OK"), "android.R.string.ok");
        try {
            r.getValue(0x01060ffe, v, true);
            check(false, "an id the framework does not have throws");
        } catch (android.content.res.Resources.NotFoundException expected) {
            check(true, "an id the framework does not have throws NotFoundException");
        }
    }

    static void identifiers() {
        android.content.res.Resources r = new android.content.res.Resources();
        check(r.getIdentifier("white", "color", "android") == WHITE, "name + type + package");
        check(r.getIdentifier("color/white", null, "android") == WHITE, "type/entry with package");
        check(r.getIdentifier("android:color/white", null, null) == WHITE, "package:type/entry");
        check(r.getIdentifier("17170443", null, null) == 17170443, "a numeric string is the id");
        check(r.getIdentifier("no_such_thing", "color", "android") == 0, "unknown name is 0");
        int status = r.getIdentifier("status_bar_height", "dimen", "android");
        check(status != 0, "a private framework dimen is found by name");
    }

    static void names() {
        android.content.res.Resources r = new android.content.res.Resources();
        check("android:color/white".equals(r.getResourceName(WHITE)), "getResourceName");
        check("color".equals(r.getResourceTypeName(WHITE)), "getResourceTypeName");
        check("white".equals(r.getResourceEntryName(WHITE)), "getResourceEntryName");
        check("android".equals(r.getResourcePackageName(WHITE)), "getResourcePackageName");
        try {
            r.getResourceName(0x01060ffe);
            check(false, "an unknown id has no name");
        } catch (android.content.res.Resources.NotFoundException expected) {
            check(true, "an unknown id has no name (NotFoundException)");
        }
    }

    static void typed() {
        android.content.res.Resources r = new android.content.res.Resources();
        int anim = r.getIdentifier("config_mediumAnimTime", "integer", "android");
        check(anim != 0 && r.getInteger(anim) == 400, "getInteger reads config_mediumAnimTime");
        int status = r.getIdentifier("status_bar_height", "dimen", "android");
        android.util.TypedValue v = new android.util.TypedValue();
        r.getValue(status, v, true);
        check(v.type == android.util.TypedValue.TYPE_DIMENSION
                && v.getComplexUnit() == android.util.TypedValue.COMPLEX_UNIT_DIP,
                "status_bar_height follows its reference to a dp dimension");
        check(r.getDimensionPixelSize(status) > 0, "and converts to pixels");
        check(Math.abs(v.getDimension(r.getDisplayMetrics()) - 24f * r.getDisplayMetrics().density) < 0.01f,
                "24dp scales by the display density");
        int tabs = r.getIdentifier("action_bar_embed_tabs", "bool", "android");
        check(tabs != 0 && r.getBoolean(tabs), "getBoolean reads a private bool");
        int fraction = r.getIdentifier("dialog_min_width_major", "dimen", "android");
        android.util.TypedValue f = new android.util.TypedValue();
        r.getValue(fraction, f, true);
        check(f.type == android.util.TypedValue.TYPE_FRACTION
                && Math.abs(f.getFraction(100f, 100f) - 65f) < 0.01f, "a fraction resource keeps 65%");
        check(Math.abs(r.getFraction(fraction, 200, 100) - 130f) < 0.01f, "getFraction applies the base");
        try {
            r.getInteger(status);
            check(false, "getInteger on a dimension throws");
        } catch (android.content.res.Resources.NotFoundException expected) {
            check(true, "getInteger on a dimension throws NotFoundException");
        }
        try {
            r.getDimension(WHITE);
            check(false, "getDimension on a color throws");
        } catch (android.content.res.Resources.NotFoundException expected) {
            check(true, "getDimension on a color throws NotFoundException");
        }
    }
}
