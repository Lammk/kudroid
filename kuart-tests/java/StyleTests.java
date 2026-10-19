import android.content.res.CompiledXmlResourceParser;
import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.drawable.ColorDrawable;
import com.android.internal.R;

/** Styled attribute precedence against a real resources.arsc (built by the native harness). */
public final class StyleTests {
    // Ids shared with test_kuart_styles.cpp.
    static final int STYLE_THEME = 0x7f010000;
    static final int STYLE_DEF = 0x7f010001;
    static final int STYLE_XML = 0x7f010002;
    static final int STYLE_RES = 0x7f010003;
    static final int STYLE_CHILD = 0x7f010004;
    static final int ATTR_DEF_STYLE = 0x7f040001;
    static final int ATTR_PAD = 0x7f040002;
    static final int COLOR_ACCENT = 0x7f020000;

    static final int MIN_WIDTH = 0x0101013f;
    static final int MIN_HEIGHT = 0x01010140;
    static final int PADDING_LEFT = 0x010100d6;
    static final int PADDING_TOP = 0x010100d7;
    static final int PADDING_RIGHT = 0x010100d8;
    static final int VISIBILITY = 0x010100dc;
    static final int BACKGROUND = 0x010100d4;
    static final int[] ATTRS = {MIN_WIDTH, MIN_HEIGHT, PADDING_LEFT, PADDING_TOP, PADDING_RIGHT,
            VISIBILITY, BACKGROUND};

    private static void check(boolean ok, String name) { TestSupport.check(ok, name); }

    public static String run() {
        TestSupport.report.setLength(0);
        try { identifiersAndValues(); } catch (Throwable t) { TestSupport.fail("identifiersAndValues", t); }
        try { precedence(); } catch (Throwable t) { TestSupport.fail("precedence", t); }
        try { defaultStyleResource(); } catch (Throwable t) { TestSupport.fail("defaultStyleResource", t); }
        try { noThemeAndParents(); } catch (Throwable t) { TestSupport.fail("noThemeAndParents", t); }
        return TestSupport.report.toString();
    }

    static CompiledXmlResourceParser element(AxmlBuilder b) throws Exception {
        CompiledXmlResourceParser parser = new CompiledXmlResourceParser(b.build());
        while (parser.next() != org.xmlpull.v1.XmlPullParser.START_TAG) { }
        return parser;
    }

    static Resources.Theme theme(Resources r) {
        Resources.Theme theme = r.newTheme();
        theme.applyStyle(STYLE_THEME, true);
        return theme;
    }

    static void identifiersAndValues() {
        Resources r = new Resources();
        check(r.getIdentifier("DefStyle", "style", "com.example.styles") == STYLE_DEF, "style identifiers are registered");
        check(r.getColor(COLOR_ACCENT) == 0xFF336699, "the application color resolves");
        check("com.example.styles:style/DefStyle".equals(r.getResourceName(STYLE_DEF)), "an application resource has a name");
        check("style".equals(r.getResourceTypeName(STYLE_DEF)) && "DefStyle".equals(r.getResourceEntryName(STYLE_DEF)),
                "and its type and entry names");
    }

    static void precedence() throws Exception {
        Resources r = new Resources();
        Resources.Theme theme = theme(r);
        // minWidth set inline (40), style="@style/XmlStyle", theme default style attribute.
        AxmlBuilder b = new AxmlBuilder().start("View")
                .attr(MIN_WIDTH, "minWidth", AxmlBuilder.TYPE_DIMENSION, 40 << 8)
                .plain("style", AxmlBuilder.TYPE_REFERENCE, STYLE_XML)
                .end();
        TypedArray a = theme.obtainStyledAttributes(element(b), ATTRS, ATTR_DEF_STYLE, 0);
        check(a.getDimensionPixelSize(0, -1) == 40, "an inline XML attribute beats the element's style");
        check(a.getDimensionPixelSize(1, -1) == 22, "defStyleAttr's style beats the theme (minHeight 22, not 33)");
        check(a.getDimensionPixelSize(2, -1) == 5, "a value only the default style has is used");
        check(a.getDimensionPixelSize(3, -1) == 7, "a value only the theme has is used");
        check(a.getInt(5, -1) == 4, "the element's style supplies visibility");
        check(a.getDimensionPixelSize(4, -1) == 11, "?attr in a style resolves through the theme");
        check(a.getDrawable(6) instanceof ColorDrawable
                && ((ColorDrawable) a.getDrawable(6)).getColor() == 0xFF336699,
                "a @color reference in a style becomes a ColorDrawable");
        check(a.hasValue(6) && a.getResourceId(6, 0) == COLOR_ACCENT, "the referenced resource id is kept");

        // Without the inline attribute the element's style beats defStyleAttr (30, not 20).
        AxmlBuilder noInline = new AxmlBuilder().start("View")
                .plain("style", AxmlBuilder.TYPE_REFERENCE, STYLE_XML).end();
        TypedArray b2 = theme.obtainStyledAttributes(element(noInline), ATTRS, ATTR_DEF_STYLE, 0);
        check(b2.getDimensionPixelSize(0, -1) == 30, "the element's style beats the default style");

        // No XML at all: defStyleAttr (20) beats the theme.
        TypedArray b3 = theme.obtainStyledAttributes(null, ATTRS, ATTR_DEF_STYLE, 0);
        check(b3.getDimensionPixelSize(0, -1) == 20, "with no XML the default style applies");
        check(!b3.hasValue(5), "an attribute no layer sets is absent");
    }

    static void defaultStyleResource() throws Exception {
        Resources r = new Resources();
        Resources.Theme theme = theme(r);
        // defStyleAttr = 0: defStyleRes is the default style.
        TypedArray a = theme.obtainStyledAttributes(null, ATTRS, 0, STYLE_RES);
        check(a.getDimensionPixelSize(0, -1) == 50 && a.getDimensionPixelSize(2, -1) == 9,
                "defStyleRes applies when there is no defStyleAttr");
        check(a.getDimensionPixelSize(1, -1) == 33, "and the theme fills in what it leaves out");
        // A defStyleAttr that the theme resolves wins over defStyleRes.
        TypedArray b = theme.obtainStyledAttributes(null, ATTRS, ATTR_DEF_STYLE, STYLE_RES);
        check(b.getDimensionPixelSize(0, -1) == 20, "a theme default style attribute wins over defStyleRes");
        // The int[] + resid form is the default-style form.
        TypedArray c = theme.obtainStyledAttributes(STYLE_RES, ATTRS);
        check(c.getDimensionPixelSize(0, -1) == 50, "obtainStyledAttributes(resid, attrs) reads that style");
    }

    static void noThemeAndParents() throws Exception {
        Resources r = new Resources();
        AxmlBuilder b = new AxmlBuilder().start("View")
                .attr(PADDING_LEFT, "paddingLeft", AxmlBuilder.TYPE_DIMENSION, 3 << 8)
                .plain("style", AxmlBuilder.TYPE_REFERENCE, STYLE_CHILD)
                .end();
        TypedArray a = r.obtainAttributes(element(b), ATTRS);
        check(a.getDimensionPixelSize(2, -1) == 3, "Resources.obtainAttributes reads inline attributes");
        check(a.getDimensionPixelSize(0, -1) == 21, "and the element's style");
        check(a.getInt(5, -1) == -1 && !a.hasValue(3), "with no theme nothing else is filled in");
        check(a.getPositionDescription().startsWith("Binary XML file line #"), "the position description is the XML position");

        // Parent chain: Child (minWidth 21) inherits DefStyle's paddingLeft 5.
        Resources.Theme theme = r.newTheme();
        theme.applyStyle(STYLE_CHILD, true);
        TypedArray c = theme.obtainStyledAttributes(null, ATTRS, 0, 0);
        check(c.getDimensionPixelSize(0, -1) == 21 && c.getDimensionPixelSize(2, -1) == 5,
                "applyStyle follows the parent chain");
        android.util.TypedValue v = new android.util.TypedValue();
        check(theme.resolveAttribute(MIN_WIDTH, v, true) && v.type == android.util.TypedValue.TYPE_DIMENSION,
                "Theme.resolveAttribute reads an applied value");
    }
}
