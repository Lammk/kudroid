import android.app.ApplicationContext;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.res.CompiledXmlResourceParser;
import android.content.res.Resources;
import android.content.res.XmlResourceParser;
import android.graphics.drawable.ColorDrawable;
import android.util.AttributeSet;
import android.view.InflateException;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewStub;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import com.android.internal.R;
import java.util.HashMap;
import kdtest.CustomGroup;
import kdtest.CustomView;

/** LayoutInflater and XML attribute reading, driven by compiled XML built in the test. */
public final class InflaterTests {
    private static void check(boolean ok, String name) { TestSupport.check(ok, name); }

    public static String run() {
        TestSupport.report.setLength(0);
        try { attributes(); } catch (Throwable t) { TestSupport.fail("attributes", t); }
        try { layoutParams(); } catch (Throwable t) { TestSupport.fail("layoutParams", t); }
        try { attachSemantics(); } catch (Throwable t) { TestSupport.fail("attachSemantics", t); }
        try { mergeAndInclude(); } catch (Throwable t) { TestSupport.fail("mergeAndInclude", t); }
        try { viewStub(); } catch (Throwable t) { TestSupport.fail("viewStub", t); }
        try { factories(); } catch (Throwable t) { TestSupport.fail("factories", t); }
        try { errors(); } catch (Throwable t) { TestSupport.fail("errors", t); }
        try { frameworkReference(); } catch (Throwable t) { TestSupport.fail("frameworkReference", t); }
        try { colorStateList(); } catch (Throwable t) { TestSupport.fail("colorStateList", t); }
        return TestSupport.report.toString();
    }

    // ---- fixtures

    /** Resources whose layouts come from a table of compiled XML instead of an APK. */
    static final class TestResources extends Resources {
        final HashMap<Integer, byte[]> layouts = new HashMap<Integer, byte[]>();

        @Override
        public XmlResourceParser getXml(int id) throws NotFoundException {
            byte[] bytes = layouts.get(id);
            if (bytes == null) throw new NotFoundException("layout 0x" + Integer.toHexString(id));
            try {
                return new CompiledXmlResourceParser(bytes);
            } catch (org.xmlpull.v1.XmlPullParserException e) {
                throw new NotFoundException(e.getMessage());
            }
        }
    }

    static final class TestContext extends ContextWrapper {
        final TestResources resources;
        final LayoutInflater inflater;

        TestContext(TestResources resources) {
            super(new ApplicationContext());
            this.resources = resources;
            this.inflater = new LayoutInflater(this);
        }

        @Override public Resources getResources() { return resources; }
        @Override public Resources.Theme getTheme() { return resources.getDefaultTheme(); }
        @Override public Object getSystemService(String name) {
            return Context.LAYOUT_INFLATER_SERVICE.equals(name) ? inflater : super.getSystemService(name);
        }
    }

    static TestContext context() { return new TestContext(new TestResources()); }

    static CompiledXmlResourceParser parse(AxmlBuilder b) throws Exception {
        CompiledXmlResourceParser parser = new CompiledXmlResourceParser(b.build());
        return parser;
    }

    static int attrId(int[] styleable, int index) { return styleable[index]; }

    /** layout_width / layout_height, which every child of a group must supply. */
    static AxmlBuilder size(AxmlBuilder b, int width, int height) {
        return b.attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_width),
                        "layout_width", AxmlBuilder.TYPE_INT_DEC, width)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_height),
                        "layout_height", AxmlBuilder.TYPE_INT_DEC, height);
    }

    static final int ID_ROOT = 0x7f080001;
    static final int ID_CHILD = 0x7f080002;
    static final int ID_INNER = 0x7f080003;

    static final int MATCH = -1;
    static final int WRAP = -2;

    // ---- tests

    static void attributes() throws Exception {
        TestContext c = context();
        AxmlBuilder b = new AxmlBuilder()
                .start("LinearLayout")
                .attr(attrId(R.styleable.View, R.styleable.View_id), "id", AxmlBuilder.TYPE_REFERENCE, ID_ROOT)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_width),
                        "layout_width", AxmlBuilder.TYPE_INT_DEC, MATCH)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_height),
                        "layout_height", AxmlBuilder.TYPE_INT_DEC, WRAP)
                .attr(attrId(R.styleable.LinearLayout, R.styleable.LinearLayout_orientation),
                        "orientation", AxmlBuilder.TYPE_INT_DEC, 1)
                .attr(attrId(R.styleable.View, R.styleable.View_paddingLeft), "paddingLeft",
                        AxmlBuilder.TYPE_DIMENSION, 12 << 8)
                .attr(attrId(R.styleable.View, R.styleable.View_visibility), "visibility",
                        AxmlBuilder.TYPE_INT_DEC, View.INVISIBLE)
                .attrString(attrId(R.styleable.View, R.styleable.View_tag), "tag", "root-tag")
                .attr(attrId(R.styleable.View, R.styleable.View_clickable), "clickable",
                        AxmlBuilder.TYPE_INT_BOOLEAN, -1)
                .attr(attrId(R.styleable.View, R.styleable.View_minWidth), "minWidth",
                        AxmlBuilder.TYPE_DIMENSION, 40 << 8)
                .start("kdtest.CustomView")
                .attr(attrId(R.styleable.View, R.styleable.View_id), "id", AxmlBuilder.TYPE_REFERENCE, ID_CHILD)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_width),
                        "layout_width", AxmlBuilder.TYPE_DIMENSION, 100 << 8)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_height),
                        "layout_height", AxmlBuilder.TYPE_INT_DEC, MATCH)
                .end()
                .end();
        View root = c.inflater.inflate(parse(b), null);
        check(root instanceof LinearLayout, "short tag name resolves to android.widget.LinearLayout");
        check(root.getId() == ID_ROOT, "android:id is read");
        check(root.getPaddingLeft() == 12, "android:paddingLeft dimension is read in pixels");
        check(root.getVisibility() == View.INVISIBLE, "android:visibility enum is read");
        check("root-tag".equals(root.getTag()), "android:tag string is read");
        check(root.isClickable(), "android:clickable is read");
        check(root.getMinimumWidth() == 40, "android:minWidth is read");
        check(((LinearLayout) root).getOrientation() == 1, "android:orientation is read");
        ViewGroup group = (ViewGroup) root;
        check(group.getChildCount() == 1, "the child element is added");
        View child = group.getChildAt(0);
        check(child instanceof CustomView && child.getId() == ID_CHILD, "a fully qualified tag creates the class");
        check(root.findViewById(ID_CHILD) == child, "findViewById finds the inflated child");
        check(root.findViewById(View.NO_ID) == null, "findViewById(NO_ID) is null");
        check(root.getLayoutParams() == null, "a root inflated without a parent group has no LayoutParams");
    }

    static void layoutParams() throws Exception {
        TestContext c = context();
        AxmlBuilder b = new AxmlBuilder()
                .start("LinearLayout")
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_width),
                        "layout_width", AxmlBuilder.TYPE_INT_DEC, MATCH)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_height),
                        "layout_height", AxmlBuilder.TYPE_INT_DEC, MATCH)
                .start("kdtest.CustomView")
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_width),
                        "layout_width", AxmlBuilder.TYPE_INT_DEC, 0)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_height),
                        "layout_height", AxmlBuilder.TYPE_INT_DEC, WRAP)
                .attr(attrId(R.styleable.LinearLayout_Layout, R.styleable.LinearLayout_Layout_layout_weight),
                        "layout_weight", AxmlBuilder.TYPE_FLOAT, Float.floatToIntBits(2.5f))
                .attr(attrId(R.styleable.ViewGroup_MarginLayout, R.styleable.ViewGroup_MarginLayout_layout_margin),
                        "layout_margin", AxmlBuilder.TYPE_DIMENSION, 6 << 8)
                .end()
                .start("FrameLayout")
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_width),
                        "layout_width", AxmlBuilder.TYPE_INT_DEC, MATCH)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_height),
                        "layout_height", AxmlBuilder.TYPE_INT_DEC, MATCH)
                .start("kdtest.CustomView")
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_width),
                        "layout_width", AxmlBuilder.TYPE_INT_DEC, WRAP)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_height),
                        "layout_height", AxmlBuilder.TYPE_INT_DEC, WRAP)
                .attr(attrId(R.styleable.FrameLayout_Layout, R.styleable.FrameLayout_Layout_layout_gravity),
                        "layout_gravity", AxmlBuilder.TYPE_INT_DEC, 0x11)
                .end()
                .end()
                .end();
        ViewGroup root = (ViewGroup) c.inflater.inflate(parse(b), null);
        ViewGroup.LayoutParams lp = root.getChildAt(0).getLayoutParams();
        check(lp instanceof LinearLayout.LayoutParams, "a LinearLayout child gets LinearLayout.LayoutParams");
        LinearLayout.LayoutParams llp = (LinearLayout.LayoutParams) lp;
        check(llp.width == 0 && llp.height == ViewGroup.LayoutParams.WRAP_CONTENT, "layout_width/height are read");
        check(llp.weight == 2.5f, "layout_weight is read");
        check(llp.leftMargin == 6 && llp.bottomMargin == 6, "layout_margin sets every margin");
        ViewGroup frame = (ViewGroup) root.getChildAt(1);
        ViewGroup.LayoutParams flp = frame.getChildAt(0).getLayoutParams();
        check(flp instanceof FrameLayout.LayoutParams && ((FrameLayout.LayoutParams) flp).gravity == 0x11,
                "a FrameLayout child gets layout_gravity");
        check(frame.getLayoutParams() instanceof LinearLayout.LayoutParams,
                "a FrameLayout inside a LinearLayout has LinearLayout.LayoutParams");
    }

    static void attachSemantics() throws Exception {
        TestContext c = context();
        FrameLayout rootGroup = new FrameLayout(c);
        AxmlBuilder one = new AxmlBuilder()
                .start("kdtest.CustomView")
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_width),
                        "layout_width", AxmlBuilder.TYPE_INT_DEC, MATCH)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_height),
                        "layout_height", AxmlBuilder.TYPE_INT_DEC, WRAP)
                .end();
        View attached = c.inflater.inflate(parse(one), rootGroup, true);
        check(attached == rootGroup && rootGroup.getChildCount() == 1, "attachToRoot=true returns the root with the child added");
        check(rootGroup.getChildAt(0).getLayoutParams() instanceof FrameLayout.LayoutParams,
                "the child has the root's LayoutParams type");
        FrameLayout other = new FrameLayout(c);
        View detached = c.inflater.inflate(parse(one), other, false);
        check(detached instanceof CustomView && detached.getParent() == null && other.getChildCount() == 0,
                "attachToRoot=false returns the child, unattached");
        check(detached.getLayoutParams() instanceof FrameLayout.LayoutParams
                && detached.getLayoutParams().height == ViewGroup.LayoutParams.WRAP_CONTENT,
                "and it carries LayoutParams generated from its attributes");
        View bare = c.inflater.inflate(parse(one), null, true);
        check(bare instanceof CustomView && bare.getLayoutParams() == null,
                "a null root returns the bare view with no LayoutParams");
    }

    static void mergeAndInclude() throws Exception {
        TestContext c = context();
        // <merge> with two children
        AxmlBuilder merge = new AxmlBuilder().start("merge");
        size(merge.start("kdtest.CustomView")
                .attr(attrId(R.styleable.View, R.styleable.View_id), "id", AxmlBuilder.TYPE_REFERENCE, ID_CHILD),
                WRAP, WRAP).end();
        size(merge.start("kdtest.CustomView")
                .attr(attrId(R.styleable.View, R.styleable.View_id), "id", AxmlBuilder.TYPE_REFERENCE, ID_INNER),
                WRAP, WRAP).end();
        merge.end();
        FrameLayout target = new FrameLayout(c);
        View result = c.inflater.inflate(parse(merge), target, true);
        check(result == target && target.getChildCount() == 2, "<merge> children go straight into the root");
        try {
            c.inflater.inflate(parse(merge), target, false);
            check(false, "<merge> without attachToRoot is rejected");
        } catch (InflateException expected) {
            check(true, "<merge> without attachToRoot is rejected");
        }
        try {
            c.inflater.inflate(parse(merge), null);
            check(false, "<merge> without a root is rejected");
        } catch (InflateException expected) {
            check(true, "<merge> without a root is rejected");
        }

        // <include layout=...> of a <merge> file and of a single-view file
        final int LAYOUT_MERGE = 0x7f0b0001;
        final int LAYOUT_SINGLE = 0x7f0b0002;
        AxmlBuilder mergeFile = new AxmlBuilder().start("merge");
        size(mergeFile.start("kdtest.CustomView")
                .attr(attrId(R.styleable.View, R.styleable.View_id), "id", AxmlBuilder.TYPE_REFERENCE, ID_INNER),
                WRAP, WRAP).end();
        c.resources.layouts.put(LAYOUT_MERGE, mergeFile.end().build());
        c.resources.layouts.put(LAYOUT_SINGLE, new AxmlBuilder()
                .start("kdtest.CustomGroup")
                .attr(attrId(R.styleable.View, R.styleable.View_id), "id", AxmlBuilder.TYPE_REFERENCE, ID_INNER)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_width),
                        "layout_width", AxmlBuilder.TYPE_INT_DEC, MATCH)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_height),
                        "layout_height", AxmlBuilder.TYPE_INT_DEC, MATCH)
                .end().build());
        AxmlBuilder host = new AxmlBuilder()
                .start("FrameLayout")
                .start("include").plain("layout", AxmlBuilder.TYPE_REFERENCE, LAYOUT_MERGE).end()
                .start("include").plain("layout", AxmlBuilder.TYPE_REFERENCE, LAYOUT_SINGLE)
                .attr(attrId(R.styleable.Include, R.styleable.Include_id), "id", AxmlBuilder.TYPE_REFERENCE, ID_CHILD)
                .attr(attrId(R.styleable.Include, R.styleable.Include_visibility), "visibility",
                        AxmlBuilder.TYPE_INT_DEC, 2)
                .end()
                .end();
        ViewGroup hostRoot = (ViewGroup) c.inflater.inflate(parse(host), null);
        check(hostRoot.getChildCount() == 2, "each <include> contributes one child");
        check(hostRoot.getChildAt(0) instanceof CustomView && hostRoot.getChildAt(0).getId() == ID_INNER,
                "an included <merge> inlines its children");
        View single = hostRoot.getChildAt(1);
        check(single instanceof CustomGroup && ((CustomGroup) single).finished,
                "an included layout is inflated and finished");
        check(single.getId() == ID_CHILD, "the <include> id overrides the included root's id");
        check(single.getVisibility() == View.GONE, "the <include> visibility overrides too");
        try {
            AxmlBuilder bad = new AxmlBuilder().start("FrameLayout")
                    .start("include").end().end();
            c.inflater.inflate(parse(bad), null);
            check(false, "<include> without layout is rejected");
        } catch (InflateException expected) {
            check(true, "<include> without layout is rejected");
        }
    }

    static void viewStub() throws Exception {
        TestContext c = context();
        final int LAYOUT_STUBBED = 0x7f0b0003;
        c.resources.layouts.put(LAYOUT_STUBBED, new AxmlBuilder()
                .start("kdtest.CustomView")
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_width),
                        "layout_width", AxmlBuilder.TYPE_INT_DEC, MATCH)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_height),
                        "layout_height", AxmlBuilder.TYPE_INT_DEC, MATCH)
                .end().build());
        AxmlBuilder b = new AxmlBuilder()
                .start("LinearLayout")
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_width),
                        "layout_width", AxmlBuilder.TYPE_INT_DEC, MATCH)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_height),
                        "layout_height", AxmlBuilder.TYPE_INT_DEC, MATCH)
                .start("ViewStub")
                .attr(attrId(R.styleable.ViewStub, R.styleable.ViewStub_id), "id", AxmlBuilder.TYPE_REFERENCE, ID_CHILD)
                .attr(attrId(R.styleable.ViewStub, R.styleable.ViewStub_inflatedId), "inflatedId",
                        AxmlBuilder.TYPE_REFERENCE, ID_INNER)
                .attr(attrId(R.styleable.ViewStub, R.styleable.ViewStub_layout), "layout",
                        AxmlBuilder.TYPE_REFERENCE, LAYOUT_STUBBED)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_width),
                        "layout_width", AxmlBuilder.TYPE_INT_DEC, MATCH)
                .attr(attrId(R.styleable.ViewGroup_Layout, R.styleable.ViewGroup_Layout_layout_height),
                        "layout_height", AxmlBuilder.TYPE_INT_DEC, WRAP)
                .end()
                .end();
        ViewGroup root = (ViewGroup) c.inflater.inflate(parse(b), null);
        View stub = root.getChildAt(0);
        check(stub instanceof ViewStub && stub.getId() == ID_CHILD && stub.getVisibility() == View.GONE,
                "a ViewStub starts GONE with its id");
        check(((ViewStub) stub).getInflatedId() == ID_INNER && ((ViewStub) stub).getLayoutResource() == LAYOUT_STUBBED,
                "ViewStub reads inflatedId and layout");
        View inflated = ((ViewStub) stub).inflate();
        check(inflated instanceof CustomView && inflated.getId() == ID_INNER, "inflate() creates the view with the inflated id");
        check(root.getChildCount() == 1 && root.getChildAt(0) == inflated, "the stub is replaced at its position");
        check(inflated.getLayoutParams().height == ViewGroup.LayoutParams.WRAP_CONTENT,
                "the inflated view takes the stub's LayoutParams");
        try {
            ((ViewStub) stub).inflate();
            check(false, "a replaced stub cannot inflate again");
        } catch (IllegalStateException expected) {
            check(true, "a replaced stub cannot inflate again");
        }
    }

    static void factories() throws Exception {
        TestContext c = context();
        final View[] seenParent = new View[1];
        c.inflater.setFactory2(new LayoutInflater.Factory2() {
            public View onCreateView(View parent, String name, Context context, AttributeSet attrs) {
                if ("Special".equals(name)) {
                    seenParent[0] = parent;
                    return new CustomView(context, attrs);
                }
                return null;
            }
            public View onCreateView(String name, Context context, AttributeSet attrs) {
                return onCreateView(null, name, context, attrs);
            }
        });
        AxmlBuilder b = new AxmlBuilder().start("FrameLayout");
        size(b.start("Special"), WRAP, WRAP).end().end();
        ViewGroup root = (ViewGroup) c.inflater.inflate(parse(b), null);
        check(root.getChildAt(0) instanceof CustomView, "a factory can supply the view for a tag");
        check(seenParent[0] == root, "Factory2 receives the parent view");
        try {
            c.inflater.setFactory2(new LayoutInflater.Factory2() {
                public View onCreateView(View parent, String name, Context context, AttributeSet attrs) { return null; }
                public View onCreateView(String name, Context context, AttributeSet attrs) { return null; }
            });
            check(false, "a second factory is rejected");
        } catch (IllegalStateException expected) {
            check(true, "a second factory is rejected");
        }

        TestContext d = context();
        final int[] privateCalls = new int[1];
        d.inflater.setPrivateFactory(new LayoutInflater.Factory2() {
            public View onCreateView(View parent, String name, Context context, AttributeSet attrs) {
                privateCalls[0]++;
                return "Special".equals(name) ? new CustomView(context, attrs) : null;
            }
            public View onCreateView(String name, Context context, AttributeSet attrs) {
                return onCreateView(null, name, context, attrs);
            }
        });
        ViewGroup root2 = (ViewGroup) d.inflater.inflate(parse(b), null);
        check(root2.getChildAt(0) instanceof CustomView && privateCalls[0] >= 1,
                "the private factory is consulted when no other factory answers");

        // The "view" tag names its class in the class attribute.
        AxmlBuilder viaTag = new AxmlBuilder().start("FrameLayout");
        size(viaTag.start("view").plainString("class", "kdtest.CustomView"), WRAP, WRAP).end().end();
        ViewGroup root3 = (ViewGroup) context().inflater.inflate(parse(viaTag), null);
        check(root3.getChildAt(0) instanceof CustomView, "<view class=...> creates that class");
    }

    static void errors() throws Exception {
        TestContext c = context();
        try {
            c.inflater.inflate(parse(new AxmlBuilder().start("NoSuchWidgetAnywhere").end()), null);
            check(false, "an unknown class fails");
        } catch (InflateException e) {
            check(e.getCause() instanceof ClassNotFoundException || e.getMessage() != null,
                    "an unknown class fails with InflateException");
        }
        try {
            c.inflater.inflate(parse(new AxmlBuilder().start("kdtest.NoAttrsView").end()), null);
            check(false, "a view without the attrs constructor fails");
        } catch (InflateException e) {
            check(e.getMessage() != null && e.getMessage().contains("Error inflating class"),
                    "a view without the (Context, AttributeSet) constructor fails with a clear message");
        }
        try {
            c.inflater.inflate(parse(new AxmlBuilder().start("FrameLayout")
                    .start("merge").end().end()), null);
            check(false, "a nested <merge> fails");
        } catch (InflateException expected) {
            check(true, "a nested <merge> fails");
        }
        try {
            c.inflater.inflate(0x7f0bffff, null);
            check(false, "an unknown layout id fails");
        } catch (Resources.NotFoundException expected) {
            check(true, "an unknown layout id fails with NotFoundException");
        }
        // The inflater stays usable after a failure.
        View ok = c.inflater.inflate(parse(new AxmlBuilder().start("kdtest.CustomView").end()), null);
        check(ok instanceof CustomView, "the inflater is usable after an error");
    }

    static void frameworkReference() throws Exception {
        TestContext c = context();
        AxmlBuilder b = new AxmlBuilder().start("kdtest.CustomView")
                .attr(attrId(R.styleable.View, R.styleable.View_background), "background",
                        AxmlBuilder.TYPE_REFERENCE, 0x0106000b)
                .end();
        View v = c.inflater.inflate(parse(b), null);
        check(v.getBackground() instanceof ColorDrawable
                && ((ColorDrawable) v.getBackground()).getColor() == 0xFFFFFFFF,
                "background=@android:color/white resolves through the framework table");
        AxmlBuilder colorAttr = new AxmlBuilder().start("kdtest.CustomView")
                .attr(attrId(R.styleable.View, R.styleable.View_background), "background",
                        AxmlBuilder.TYPE_INT_COLOR_ARGB8, 0xFF112233)
                .end();
        View w = c.inflater.inflate(parse(colorAttr), null);
        check(w.getBackground() instanceof ColorDrawable
                && ((ColorDrawable) w.getBackground()).getColor() == 0xFF112233,
                "a literal color background becomes a ColorDrawable");
    }

    static void colorStateList() throws Exception {
        final int STATE_PRESSED = 0x010100a7;
        final int STATE_ENABLED = 0x0101009e;
        final int COLOR = 0x010101a5;
        final int ALPHA = 0x0101031f;
        AxmlBuilder b = new AxmlBuilder().start("selector");
        b.start("item")
                .attr(STATE_PRESSED, "state_pressed", AxmlBuilder.TYPE_INT_BOOLEAN, -1)
                .attr(COLOR, "color", AxmlBuilder.TYPE_INT_COLOR_ARGB8, 0xFF0000FF).end();
        b.start("item")
                .attr(STATE_ENABLED, "state_enabled", AxmlBuilder.TYPE_INT_BOOLEAN, 0)
                .attr(COLOR, "color", AxmlBuilder.TYPE_INT_COLOR_ARGB8, 0xFF808080)
                .attr(ALPHA, "alpha", AxmlBuilder.TYPE_FLOAT, Float.floatToIntBits(0.5f)).end();
        b.start("item")
                .attr(COLOR, "color", AxmlBuilder.TYPE_INT_COLOR_ARGB8, 0xFF00FF00).end();
        b.end();
        android.content.res.ColorStateList list = android.content.res.ColorStateList.createFromXml(
                new Resources(), new CompiledXmlResourceParser(b.build()), null);
        check(list.getColorForState(new int[] {STATE_PRESSED, STATE_ENABLED}, 0) == 0xFF0000FF,
                "a pressed state selects the pressed item");
        check(list.getDefaultColor() == 0xFF00FF00, "the item with no state is the default color");
        check(list.getColorForState(new int[0], 0) == 0x80808080 || list.getColorForState(new int[0], 0) == 0xFF00FF00,
                "an unmatched state falls through the list");
        check(list.getColorForState(new int[] {STATE_ENABLED}, 7) == 0xFF00FF00,
                "enabled (not pressed, not disabled) reaches the default");
        check(list.isStateful(), "a selector with states is stateful");
        // android:alpha modulates the item color.
        AxmlBuilder one = new AxmlBuilder().start("selector");
        one.start("item")
                .attr(STATE_ENABLED, "state_enabled", AxmlBuilder.TYPE_INT_BOOLEAN, 0)
                .attr(COLOR, "color", AxmlBuilder.TYPE_INT_COLOR_ARGB8, 0xFF808080)
                .attr(ALPHA, "alpha", AxmlBuilder.TYPE_FLOAT, Float.floatToIntBits(0.5f)).end();
        one.end();
        android.content.res.ColorStateList disabled = android.content.res.ColorStateList.createFromXml(
                new Resources(), new CompiledXmlResourceParser(one.build()), null);
        check(disabled.getColorForState(new int[0], 0) == 0x80808080, "android:alpha scales the color's alpha");
    }
}
