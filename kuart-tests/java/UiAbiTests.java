import android.content.ContextWrapper;
import android.app.ApplicationContext;
import android.content.res.CompiledXmlResourceParser;
import android.content.res.Resources;
import android.util.TypedValue;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.TextView;

public final class UiAbiTests {
    static final int EXACTLY = 0x40000000;
    static final int AT_MOST = 0x80000000;

    static final class TestContext extends ContextWrapper {
        final Resources resources = new Resources();
        TestContext() { super(new ApplicationContext()); }
        @Override public Resources getResources() { return resources; }
        @Override public Resources.Theme getTheme() { return resources.getDefaultTheme(); }
    }

    static final class StateView extends View {
        StateView(android.content.Context context) { super(context); }
        @Override protected void onMeasure(int w, int h) {
            setMeasuredDimension(80 | 0x01000000, 40 | 0x01000000);
        }
    }
    static final class CountingView extends View {
        int measures, layouts;
        boolean requestDuringLayout;
        boolean requestDuringSizeChange;
        CountingView(android.content.Context context) { super(context); }
        @Override protected void onMeasure(int w, int h) { measures++; super.onMeasure(w, h); }
        @Override protected void onLayout(boolean changed, int l, int t, int r, int b) {
            layouts++;
            if (requestDuringLayout) { requestDuringLayout = false; requestLayout(); }
        }
        @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
            if (requestDuringSizeChange) { requestDuringSizeChange = false; requestLayout(); }
        }
    }
    static final class DirectionParams extends android.view.ViewGroup.MarginLayoutParams {
        int resolved = -1;
        DirectionParams() { super(0, -1); }
        @Override public void resolveLayoutDirection(int direction) {
            super.resolveLayoutDirection(direction);
            resolved = direction;
        }
    }
    static final class AnchorParent extends android.view.ViewGroup {
        AnchorParent(android.content.Context context) { super(context); }
        @Override protected void onMeasure(int w, int h) {
            View child = getChildAt(0);
            DirectionParams params = (DirectionParams) child.getLayoutParams();
            child.measure(EXACTLY | (params.resolved == getLayoutDirection() ? 200 : 0), EXACTLY | 80);
            setMeasuredDimension(200, 80);
        }
    }

    static CompiledXmlResourceParser element(AxmlBuilder builder) throws Exception {
        CompiledXmlResourceParser parser = new CompiledXmlResourceParser(builder.build());
        while (parser.next() != org.xmlpull.v1.XmlPullParser.START_TAG) {}
        return parser;
    }

    public static String run() {
        TestSupport.report.setLength(0);
        try { measurement(); } catch (Throwable t) { TestSupport.fail("measurement ABI", t); }
        try { visibility(); } catch (Throwable t) { TestSupport.fail("visibility ABI", t); }
        try { images(); } catch (Throwable t) { TestSupport.fail("image attributes", t); }
        try { text(); } catch (Throwable t) { TestSupport.fail("text factories", t); }
        try { spans(); } catch (Throwable t) { TestSupport.fail("span contracts", t); }
        try { attributeIndices(); } catch (Throwable t) { TestSupport.fail("attribute indices", t); }
        try { traversalCaching(); } catch (Throwable t) { TestSupport.fail("traversal caching", t); }
        try { textLayout(); } catch (Throwable t) { TestSupport.fail("text layout", t); }
        try { rowGeometry(); } catch (Throwable t) { TestSupport.fail("row geometry", t); }
        try { drawingTime(); } catch (Throwable t) { TestSupport.fail("drawing time", t); }
        try { directionResolution(); } catch (Throwable t) { TestSupport.fail("direction resolution", t); }
        try { scrolling(); } catch (Throwable t) { TestSupport.fail("scroll touch dispatch", t); }
        try { touchGeometry(); } catch (Throwable t) { TestSupport.fail("touch geometry", t); }
        try { lineLimits(); } catch (Throwable t) { TestSupport.fail("line limit semantics", t); }
        try { drawableColors(); } catch (Throwable t) { TestSupport.fail("drawable selection colors", t); }
        try { shapeAndImageTints(); } catch (Throwable t) { TestSupport.fail("shape and image tint states", t); }
        try { insetContracts(); } catch (Throwable t) { TestSupport.fail("window inset contracts", t); }
        try { dispatchContracts(); } catch (Throwable t) { TestSupport.fail("method dispatch contracts", t); }
        try { reflectionContracts(); } catch (Throwable t) { TestSupport.fail("reflection exception contracts", t); }
        try { filenameEncoding(); } catch (Throwable t) { TestSupport.fail("filename encoding", t); }
        try { fileIntents(); } catch (Throwable t) { TestSupport.fail("file intents", t); }
        return TestSupport.report.toString();
    }

    static final class TouchSurface extends android.view.SurfaceView {
        int down, up, cancel;
        float x, y;
        TouchSurface(android.content.Context context) { super(context); }
        @Override public boolean onTouchEvent(android.view.MotionEvent event) {
            x = event.getX(); y = event.getY();
            if (event.getActionMasked() == 0) down++;
            if (event.getActionMasked() == 1) up++;
            if (event.getActionMasked() == 3) cancel++;
            return true;
        }
    }

    static final class ReverseTouchGroup extends FrameLayout {
        ReverseTouchGroup(android.content.Context context) {
            super(context); setChildrenDrawingOrderEnabled(true);
        }
        @Override protected int getChildDrawingOrder(int count, int index) {
            return count - index - 1;
        }
    }

    static void touchGeometry() {
        TestContext context = new TestContext();
        FrameLayout root = new FrameLayout(context);
        TouchSurface surface = new TouchSurface(context);
        root.addView(surface, new FrameLayout.LayoutParams(100, 100));
        root.layout(0, 0, 300, 200);
        surface.layout(0, 0, 100, 100);
        surface.setTranslationX(100);
        android.view.MotionEvent down = android.view.MotionEvent.obtain(1, 1, 0, 120, 30, 0);
        TestSupport.check(root.dispatchTouchEvent(down) && surface.down == 1
                && surface.x == 20 && surface.y == 30,
                "translated surface receives touch in its visible local coordinates");
        root.dispatchTouchEvent(android.view.MotionEvent.obtain(1, 2, 1, 120, 30, 0));
        TestSupport.check(surface.up == 1 && surface.x == 20,
                "translated surface retains the target through release");
        TestSupport.check(down.getX() == 120 && down.getRawX() == 120,
                "child dispatch preserves the original event");

        surface.setTranslationX(0);
        surface.setPivotX(0); surface.setPivotY(0);
        surface.setScaleX(2); surface.setScaleY(2);
        TestSupport.check(root.dispatchTouchEvent(android.view.MotionEvent.obtain(3, 3, 0, 150, 60, 0))
                && surface.x == 75 && surface.y == 30,
                "scaled surface hit testing follows its visible bounds");
        root.dispatchTouchEvent(android.view.MotionEvent.obtain(3, 4, 3, 150, 60, 0));
        surface.setScaleX(1); surface.setScaleY(1);
        surface.layout(100, 0, 200, 100);
        surface.setRotation(90);
        TestSupport.check(root.dispatchTouchEvent(android.view.MotionEvent.obtain(5, 5, 0, 50, 20, 0))
                && Math.abs(surface.x - 20) < 0.001f && Math.abs(surface.y - 50) < 0.001f,
                "rotated surface receives inverse-transformed touch coordinates");
        root.dispatchTouchEvent(android.view.MotionEvent.obtain(5, 6, 3, 50, 20, 0));

        surface.setRotation(0); surface.layout(0, 0, 100, 100);
        surface.setElevation(10);
        TouchSurface lower = new TouchSurface(context);
        root.addView(lower, new FrameLayout.LayoutParams(100, 100));
        lower.layout(0, 0, 100, 100);
        int priorDown = surface.down;
        TestSupport.check(root.dispatchTouchEvent(android.view.MotionEvent.obtain(7, 7, 0, 20, 20, 0))
                && surface.down == priorDown + 1 && lower.down == 0,
                "touch targets the surface drawn above an overlapping sibling");
        root.dispatchTouchEvent(android.view.MotionEvent.obtain(7, 8, 3, 20, 20, 0));
        priorDown = surface.down;
        root.dispatchTouchEvent(android.view.MotionEvent.obtain(8, 8, 0, 20, 20, 0));
        int priorCancel = surface.cancel;
        surface.setScaleX(0);
        root.dispatchTouchEvent(android.view.MotionEvent.obtain(8, 9, 3, 20, 20, 0));
        TestSupport.check(surface.down == priorDown + 1 && surface.cancel == priorCancel + 1,
                "collapsed touch targets still receive gesture cancellation");
        root.removeView(lower);
        TestSupport.check(!root.dispatchTouchEvent(android.view.MotionEvent.obtain(9, 9, 0, 20, 20, 0)),
                "noninvertible view transforms cannot acquire a touch target");
        surface.setScaleX(1);

        ReverseTouchGroup reverse = new ReverseTouchGroup(context);
        TouchSurface front = new TouchSurface(context), back = new TouchSurface(context);
        reverse.addView(front); reverse.addView(back);
        reverse.layout(0, 0, 100, 100);
        front.layout(0, 0, 100, 100); back.layout(0, 0, 100, 100);
        reverse.dispatchTouchEvent(android.view.MotionEvent.obtain(10, 10, 0, 20, 20, 0));
        TestSupport.check(front.down == 1 && back.down == 0,
                "custom drawing order also determines touch priority");
        reverse.dispatchTouchEvent(android.view.MotionEvent.obtain(10, 11, 3, 20, 20, 0));
        back.setElevation(20);
        reverse.dispatchTouchEvent(android.view.MotionEvent.obtain(12, 12, 0, 20, 20, 0));
        TestSupport.check(back.down == 1 && front.down == 1,
                "elevated children retain priority with custom drawing order");
        reverse.dispatchTouchEvent(android.view.MotionEvent.obtain(12, 13, 3, 20, 20, 0));

        android.view.MotionEvent multiple = android.view.MotionEvent.obtainWithPointers(9, 9, 2,
                new int[]{2, 7}, new float[]{120, 180}, new float[]{60, 100}, 2);
        multiple.offsetLocation(-100, -20);
        android.graphics.Matrix inverse = new android.graphics.Matrix();
        inverse.setScale(0.5f, 0.5f);
        multiple.transform(inverse);
        TestSupport.check(multiple.getX(0) == 10 && multiple.getY(0) == 20
                && multiple.getX(1) == 40 && multiple.getY(1) == 40,
                "all touch pointers receive the same local transform");
        TestSupport.check(multiple.getRawX(0) == 120 && multiple.getRawY(0) == 60
                && multiple.getRawX(1) == 180 && multiple.getRawY(1) == 100,
                "transformed multi-touch preserves raw screen coordinates");
        android.view.MotionEvent copy = android.view.MotionEvent.obtain(multiple);
        copy.offsetLocation(2, 3); copy.transform(inverse);
        TestSupport.check(copy.getRawX(1) == 180 && copy.getRawY(1) == 100
                && multiple.getX(1) == 40,
                "copied and repeatedly transformed events retain independent pointer state");
    }

    static void filenameEncoding() throws Exception {
        java.nio.charset.Charset utf8 = java.nio.charset.Charset.forName("UTF-8");
        TestSupport.check(java.util.Arrays.equals("Nhạc 🎵".getBytes(utf8),
                new byte[]{78,104,(byte)225,(byte)186,(byte)161,99,32,
                           (byte)240,(byte)159,(byte)142,(byte)181}),
                "UTF-8 preserves a non-ASCII filename and surrogate pair");
        TestSupport.check(java.util.Arrays.equals("é".getBytes("latin1"), new byte[]{(byte)233}),
                "named charset aliases preserve Latin-1 bytes");
        TestSupport.check(java.util.Arrays.equals("é🎵".getBytes(java.nio.charset.StandardCharsets.US_ASCII),
                new byte[]{63,63}), "ASCII replaces one unmappable code point at a time");
        TestSupport.check(java.util.Arrays.equals("A".getBytes(java.nio.charset.StandardCharsets.UTF_16),
                new byte[]{(byte)254,(byte)255,0,65}), "UTF-16 emits a big-endian BOM");
        TestSupport.check(java.util.Arrays.equals("🎵".getBytes(java.nio.charset.StandardCharsets.UTF_16LE),
                new byte[]{60,(byte)216,(byte)181,(byte)223}), "UTF-16LE retains both surrogate units");
        TestSupport.check("".getBytes(java.nio.charset.StandardCharsets.UTF_16).length == 0,
                "empty UTF-16 encoding emits no BOM");
        String malformed = new String(new char[]{(char)0xd800, 'A', (char)0xdc00});
        TestSupport.check(java.util.Arrays.equals(malformed.getBytes(utf8), new byte[]{63,65,63}),
                "UTF-8 replaces isolated surrogate units");
        TestSupport.check(java.util.Arrays.equals(malformed.getBytes(java.nio.charset.StandardCharsets.UTF_16BE),
                new byte[]{(byte)255,(byte)253,0,65,(byte)255,(byte)253}),
                "UTF-16 replaces malformed surrogate units");
        TestSupport.check(java.util.Arrays.equals(new String(new char[]{'A',0,'B'}).getBytes(utf8),
                new byte[]{65,0,66}), "encoding preserves embedded NUL");
        String nul = new String(new char[]{'A',0,'B'});
        TestSupport.check(nul.intern().length() == 3 && nul.substring(1).length() == 2
                && nul.concat(nul).length() == 6 && nul.replace((char)0, 'C').equals("ACB"),
                "string transformations and interning retain embedded NUL");
        boolean rejected = false;
        try { "a".getBytes((java.nio.charset.Charset)null); }
        catch (NullPointerException expected) { rejected = true; }
        TestSupport.check(rejected, "null Charset is rejected");
        rejected = false;
        try { "a".getBytes((String)null); }
        catch (NullPointerException expected) { rejected = true; }
        TestSupport.check(rejected, "null charset name is rejected");
        rejected = false;
        try { "a".getBytes("missing-encoding"); }
        catch (java.io.UnsupportedEncodingException expected) { rejected = true; }
        TestSupport.check(rejected, "unknown encoding name reports UnsupportedEncodingException");
        TestSupport.check(java.util.Arrays.equals("Nhạc 🎵".getBytes(), "Nhạc 🎵".getBytes(utf8)),
                "default UTF-8 encoding matches the explicit charset");
        java.nio.charset.Charset custom = new java.nio.charset.Charset("test-pairs", new String[0]) {
            public boolean contains(java.nio.charset.Charset other) { return other == this; }
            public java.nio.charset.CharsetDecoder newDecoder() { throw new UnsupportedOperationException(); }
            public java.nio.charset.CharsetEncoder newEncoder() {
                return new java.nio.charset.CharsetEncoder(this, 1, 2) {
                    protected java.nio.charset.CoderResult encodeLoop(java.nio.CharBuffer in, java.nio.ByteBuffer out) {
                        while (in.hasRemaining()) {
                            if (out.remaining() < 2) return java.nio.charset.CoderResult.OVERFLOW;
                            char c = in.get();
                            out.put((byte)c).put((byte)'!');
                        }
                        return java.nio.charset.CoderResult.UNDERFLOW;
                    }
                };
            }
        };
        TestSupport.check(java.util.Arrays.equals("abc".getBytes(custom), new byte[]{97,33,98,33,99,33}),
                "custom Charset encoding survives output-buffer growth");
        java.nio.CharBuffer chars = java.nio.CharBuffer.wrap("xabc", 1, 4);
        TestSupport.check(chars.isReadOnly() && chars.remaining() == 3 && chars.charAt(0) == 'a',
                "CharBuffer wrapping preserves the requested position and limit");
        rejected = false;
        try { chars.charAt(-1); } catch (IndexOutOfBoundsException expected) { rejected = true; }
        TestSupport.check(rejected, "relative CharBuffer indexing rejects a negative index");
        java.nio.charset.CharsetEncoder encoder = utf8.newEncoder();
        chars = java.nio.CharBuffer.wrap("é");
        java.nio.ByteBuffer shortBuffer = java.nio.ByteBuffer.allocate(1);
        TestSupport.check(encoder.encode(chars, shortBuffer, true).isOverflow() && chars.position() == 0,
                "encoder overflow retains the unencoded character");
        java.nio.ByteBuffer complete = java.nio.ByteBuffer.allocate(2);
        TestSupport.check(encoder.encode(chars, complete, true).isUnderflow() && chars.position() == 1,
                "encoder retries after overflow without losing input");
        TestSupport.check(encoder.flush(complete).isUnderflow(), "encoder flush completes after end-of-input");
        encoder.reset();
        chars = java.nio.CharBuffer.wrap(new char[]{(char)0xd800});
        TestSupport.check(encoder.encode(chars, complete, false).isUnderflow() && chars.position() == 0,
                "encoder preserves a trailing high surrogate before end-of-input");
        TestSupport.check(encoder.encode(chars, complete, true).isMalformed(),
                "encoder reports the unfinished surrogate at end-of-input");
        rejected = false;
        try { utf8.newEncoder().encode(java.nio.CharBuffer.wrap(new char[]{(char)0xd800})); }
        catch (java.nio.charset.MalformedInputException expected) { rejected = expected.getInputLength() == 1; }
        TestSupport.check(rejected, "reporting encoders throw the typed malformed-input exception");
        rejected = false;
        try { java.nio.charset.StandardCharsets.US_ASCII.newEncoder().encode(java.nio.CharBuffer.wrap("🎵")); }
        catch (java.nio.charset.UnmappableCharacterException expected) { rejected = expected.getInputLength() == 2; }
        TestSupport.check(rejected, "reporting encoders preserve the unmappable surrogate-pair length");
    }

    static void fileIntents() {
        final int[] queries = new int[1];
        android.content.ContentResolver resolver = new android.content.ContentResolver(null) {
            @Override public String getType(android.net.Uri uri) { queries[0]++; return "video/test"; }
        };
        android.content.Intent intent = new android.content.Intent();
        TestSupport.check(intent.getScheme() == null && intent.resolveTypeIfNeeded(resolver) == null,
                "empty intent has no scheme or MIME type");
        intent.setData(android.net.Uri.parse("content://media/1"));
        TestSupport.check("content".equals(intent.getScheme()) && "video/test".equals(intent.resolveTypeIfNeeded(resolver))
                && queries[0] == 1, "content intent resolves its MIME type through the resolver");
        intent.setComponent(new android.content.ComponentName("test.package", "test.Activity"));
        TestSupport.check(intent.resolveTypeIfNeeded(resolver) == null && queries[0] == 1,
                "explicit component avoids content MIME lookup");
        intent.setDataAndType(android.net.Uri.parse("file:///sdcard/video.mp4"), "video/mp4");
        TestSupport.check("file".equals(intent.getScheme()) && "video/mp4".equals(intent.resolveTypeIfNeeded(resolver)),
                "file intent preserves explicit data and MIME type");
        intent.setType("audio/test");
        TestSupport.check(intent.getData() == null, "setType clears prior intent data");
        intent.setData(android.net.Uri.parse("file:///sdcard/audio.ogg"));
        TestSupport.check(intent.getType() == null, "setData clears prior intent MIME type");
        TestSupport.check(android.view.accessibility.AccessibilityManager.getInstance(null)
                .getEnabledAccessibilityServiceList(-1).isEmpty(), "disabled accessibility returns an empty service list");
    }

    static void measurement() {
        TestContext context = new TestContext();
        TestSupport.check(View.MeasureSpec.getMode(EXACTLY | 123) == EXACTLY,
                "getMode returns Android exact mask");
        TestSupport.check(View.MeasureSpec.getMode(AT_MOST | 321) == AT_MOST,
                "getMode returns Android bounded mask");
        TestSupport.check(View.MeasureSpec.makeMeasureSpec(123, EXACTLY) == (EXACTLY | 123),
                "makeMeasureSpec accepts precompiled Android exact constant");
        TestSupport.check(View.MeasureSpec.makeMeasureSpec(321, AT_MOST) == (AT_MOST | 321),
                "makeMeasureSpec accepts precompiled Android bounded constant");
        TextView text = new TextView(context);
        text.setText("text");
        text.measure(EXACTLY | 200, EXACTLY | 75);
        TestSupport.check(text.getMeasuredWidth() == 200 && text.getMeasuredHeight() == 75,
                "widget honors Android exact specs");
        StateView state = new StateView(context);
        state.measure(0, 0);
        TestSupport.check(state.getMeasuredWidth() == 80 && state.getMeasuredHeight() == 40,
                "measured sizes exclude packed state bits");
        TestSupport.check(state.getMeasuredWidthAndState() == (80 | 0x01000000)
                && state.getMeasuredState() == 0x01000100, "packed measured states remain available");
    }

    static void visibility() throws Exception {
        TestContext context = new TestContext();
        View invisible = new View(context, element(new AxmlBuilder().start("View")
                .attr(0x010100dc, "visibility", AxmlBuilder.TYPE_INT_DEC, 1).end()));
        View gone = new View(context, element(new AxmlBuilder().start("View")
                .attr(0x010100dc, "visibility", AxmlBuilder.TYPE_INT_DEC, 2).end()));
        TestSupport.check(invisible.getVisibility() == 4, "XML invisible ordinal maps to Android flag");
        TestSupport.check(gone.getVisibility() == 8, "XML gone ordinal maps to Android flag");
        FrameLayout frame = new FrameLayout(context);
        frame.addView(gone, new FrameLayout.LayoutParams(900, 900));
        frame.measure(AT_MOST | 200, AT_MOST | 300);
        TestSupport.check(frame.getMeasuredWidth() == 0 && frame.getMeasuredHeight() == 0,
                "XML gone child reserves no layout space");
    }

    static void images() throws Exception {
        TestContext context = new TestContext();
        ImageView image = new ImageView(context, element(new AxmlBuilder().start("ImageView")
                .attr(0x01010119, "src", AxmlBuilder.TYPE_INT_COLOR_ARGB8, 0xff123456)
                .attr(0x0101011d, "scaleType", AxmlBuilder.TYPE_INT_DEC, 6)
                .attr(0x0101011e, "adjustViewBounds", AxmlBuilder.TYPE_INT_BOOLEAN, 1)
                .attr(0x0101011f, "maxWidth", AxmlBuilder.TYPE_DIMENSION, 37 << 8).end()));
        TestSupport.check(image.getDrawable() instanceof android.graphics.drawable.ColorDrawable,
                "XML src initializes image drawable");
        TestSupport.check(image.getScaleType() == ImageView.ScaleType.CENTER_CROP,
                "XML scale type uses Android ordinal");
        TestSupport.check(image.getAdjustViewBounds() && image.getMaxWidth() == 37,
                "XML image sizing attributes retained");
        image.setBaseline(12);
        TestSupport.check(image.getBaseline() == 12, "image baseline retained");
        image.measure(EXACTLY | 80, EXACTLY | 42);
        image.setBaselineAlignBottom(true);
        TestSupport.check(image.getBaseline() == 42, "image bottom baseline follows measured height");
        image.setImageBitmap(android.graphics.Bitmap.createBitmap(100, 50, android.graphics.Bitmap.Config.ARGB_8888));
        image.measure(AT_MOST | 30, EXACTLY | 100);
        TestSupport.check(image.getMeasuredWidth() == 30, "image aspect adjustment respects bounded width");
        image.measure(AT_MOST | 30, AT_MOST | 200);
        TestSupport.check(image.getMeasuredWidth() == 30 && image.getMeasuredHeight() == 15,
                "image preserves aspect ratio when both dimensions are bounded");
    }

    static void text() throws Exception {
        java.lang.reflect.Method valueOf = android.text.SpannableString.class.getMethod(
                "valueOf", CharSequence.class);
        android.text.SpannableString source = new android.text.SpannableString("visible text");
        Object same = valueOf.invoke(null, source);
        TestSupport.check(same == source, "SpannableString.valueOf retains existing instance");
        java.lang.reflect.Method create = android.text.PrecomputedText.class.getMethod(
                "create", CharSequence.class, android.text.PrecomputedText.Params.class);
        android.text.PrecomputedText.Params params = new android.text.PrecomputedText.Params(
                new android.text.TextPaint(), android.text.TextDirectionHeuristics.FIRSTSTRONG_LTR, 1, 1);
        Object computed = create.invoke(null, source, params);
        TestSupport.check(computed instanceof CharSequence && computed instanceof android.text.Spannable,
                "precomputed text implements guest text interfaces");
        TestSupport.check("visible text".contentEquals((CharSequence) computed),
                "precomputed text preserves content");
        TextView view = new TextView(new TestContext());
        view.setText((CharSequence) computed);
        TestSupport.check("visible text".contentEquals(view.getText()), "precomputed text remains visible text");
    }

    static void spans() {
        TestSupport.check(android.text.Spanned.SPAN_INCLUSIVE_EXCLUSIVE == 0x11
                && android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE == 0x21,
                "span endpoints match Android ABI");
        android.text.SpannableString source = new android.text.SpannableString("abc\ndef");
        Object span = new Object();
        Object priority = new Object();
        android.text.NoCopySpan local = new android.text.NoCopySpan.Concrete();
        source.setSpan(span, 1, 3, 0x21);
        source.setSpan(priority, 1, 2, 0x21 | (2 << 16));
        source.setSpan(local, 0, 0, 0x11);
        TestSupport.check(source.getSpanStart(span) == 1 && source.getSpanEnd(span) == 3
                && source.getSpanFlags(span) == 0x21, "span ranges and flags retained");
        TestSupport.check(source.getSpans(1, 3, Object.class)[0] == priority,
                "higher priority spans precede lower priorities");
        TestSupport.check(source.getSpans(3, 4, Object.class).length == 0,
                "nonempty query excludes touching span endpoints");
        TestSupport.check(source.getSpans(3, 3, Object.class).length == 1,
                "empty query includes span endpoint");
        TestSupport.check(source.nextSpanTransition(0, 7, Object.class) == 1,
                "span transition reports next range boundary");
        android.text.Spanned slice = (android.text.Spanned) source.subSequence(2, 4);
        TestSupport.check(slice.getSpanStart(span) == 0 && slice.getSpanEnd(span) == 1,
                "text subsequence clips and rebases spans");
        android.text.SpannedString frozen = new android.text.SpannedString(source);
        source.removeSpan(span);
        TestSupport.check(frozen.getSpanStart(span) == 1 && source.getSpanStart(span) == -1,
                "immutable text copies span storage");
        android.text.PrecomputedText computed = android.text.PrecomputedText.create(frozen,
                new android.text.PrecomputedText.Params.Builder(new android.text.TextPaint()).build());
        TestSupport.check(computed.getSpanStart(span) == 1 && computed.getSpanStart(local) == -1,
                "precomputed text preserves styling and excludes NoCopySpan");
        TestSupport.check(computed.getParagraphCount() == 2 && computed.getParagraphEnd(0) == 4
                && computed.getParagraphStart(1) == 4 && computed.getParagraphEnd(1) == 7,
                "precomputed paragraph boundaries preserve newlines");
        boolean rejected = false;
        try { computed.setSpan(new android.text.style.ReplacementSpan(), 0, 1, 0x21); }
        catch (IllegalArgumentException expected) { rejected = true; }
        TestSupport.check(rejected, "precomputed text rejects metric changes");
        Object overlay = new Object();
        computed.setSpan(overlay, 0, 1, 0x21);
        computed.removeSpan(overlay);
        TestSupport.check(computed.getSpanStart(overlay) == -1, "precomputed nonmetric spans remain mutable");
        android.text.SpannableStringBuilder editable = new android.text.SpannableStringBuilder(frozen);
        TestSupport.check(editable.getSpanStart(local) == -1 && editable.getSpanStart(span) == 1,
                "editable copy retains style and excludes NoCopySpan");
        editable.insert(1, "XY");
        TestSupport.check(editable.getSpanStart(span) == 3 && editable.getSpanEnd(span) == 5,
                "exclusive span shifts across insertion at start");
        editable.insert(5, "Z");
        TestSupport.check(editable.getSpanEnd(span) == 5, "exclusive span excludes insertion at end");
        editable.delete(3, 5);
        TestSupport.check(editable.getSpanStart(span) == -1, "deleting exclusive span content removes span");
        Object inclusive = new Object();
        editable.setSpan(inclusive, 0, 1, 0x12);
        editable.insert(0, "Q");
        editable.insert(2, "R");
        TestSupport.check(editable.getSpanStart(inclusive) == 0 && editable.getSpanEnd(inclusive) == 3,
                "inclusive endpoints absorb adjacent insertions");
        editable.clearSpans();
        TestSupport.check(editable.getSpans(0, editable.length(), Object.class).length == 0,
                "clearSpans removes retained ranges");
        android.text.SpannableStringBuilder paragraphs = new android.text.SpannableStringBuilder("abc\ndef\n");
        Object paragraph = new Object();
        paragraphs.setSpan(paragraph, 0, 4, 0x33);
        paragraphs.delete(3, 4);
        TestSupport.check(paragraphs.getSpanEnd(paragraph) == 7,
                "deleting paragraph boundary moves paragraph span to next boundary");
    }

    static void attributeIndices() throws Exception {
        TestContext context = new TestContext();
        int[] attributes = { 0x010100dc, 0x010100d5, 0x0101014f, 0x01010119 };
        android.content.res.TypedArray values = context.getTheme().obtainStyledAttributes(
                element(new AxmlBuilder().start("View")
                        .attr(0x010100d5, "padding", AxmlBuilder.TYPE_DIMENSION, 8 << 8)
                        .attr(0x0101014f, "text", 0, 1).end()), attributes, 0, 0);
        TestSupport.check(values.getIndexCount() == 2 && values.getIndex(0) == 1 && values.getIndex(1) == 2,
                "styled attribute indices retain sparse order and explicit empty values");
        TestSupport.check(!values.hasValue(2) && values.hasValueOrEmpty(2)
                && !values.hasValueOrEmpty(0), "explicit empty differs from missing attribute");
        boolean rejected = false;
        try { values.getIndex(2); } catch (IndexOutOfBoundsException expected) { rejected = true; }
        TestSupport.check(rejected, "attribute index outside present count rejected");
        values.recycle();
    }

    static void traversalCaching() {
        TestContext context = new TestContext();
        FrameLayout root = new FrameLayout(context);
        CountingView child = new CountingView(context);
        root.addView(child, new FrameLayout.LayoutParams(-1, -1));
        root.measure(EXACTLY | 200, EXACTLY | 75);
        root.layout(0, 0, 200, 75);
        root.invalidate();
        root.measure(EXACTLY | 200, EXACTLY | 75);
        root.layout(0, 0, 200, 75);
        TestSupport.check(child.measures == 1 && child.layouts == 1,
                "redraw with unchanged specs reuses measurement and layout");
        child.requestLayout();
        TestSupport.check(child.isLaidOut() && child.isLayoutRequested() && root.isLayoutRequested(),
                "layout request preserves previous layout and propagates to parent");
        root.measure(EXACTLY | 200, EXACTLY | 75);
        root.layout(0, 0, 200, 75);
        TestSupport.check(child.measures == 2 && child.layouts == 2,
                "child layout request invalidates ancestor measurement cache");
        root.measure(EXACTLY | 220, EXACTLY | 75);
        root.layout(0, 0, 220, 75);
        TestSupport.check(child.measures == 3 && child.layouts == 3 && child.getMeasuredWidth() == 220,
                "changed measure spec recalculates child sizes");
        child.forceLayout();
        TestSupport.check(child.isLayoutRequested() && !root.isLayoutRequested(),
                "forceLayout invalidates only the requested view");
        child.measure(EXACTLY | 220, EXACTLY | 75);
        child.layout(0, 0, 220, 75);
        TestSupport.check(child.measures == 4 && child.layouts == 4,
                "forceLayout reruns unchanged measurement and layout");
        child.requestDuringLayout = true;
        child.requestLayout();
        root.measure(EXACTLY | 220, EXACTLY | 75);
        root.layout(0, 0, 220, 75);
        TestSupport.check(child.isLayoutRequested() && root.isLayoutRequested(),
                "request during layout remains pending for next traversal");
        child.requestDuringSizeChange = true;
        root.measure(EXACTLY | 240, EXACTLY | 75);
        root.layout(0, 0, 240, 75);
        TestSupport.check(child.isLayoutRequested() && root.isLayoutRequested(),
                "request during size change remains pending for next traversal");
        View backgroundView = new View(context);
        backgroundView.measure(0, 0);
        backgroundView.layout(0, 0, 0, 0);
        backgroundView.setBackground(new android.graphics.drawable.BitmapDrawable(context.getResources(),
                android.graphics.Bitmap.createBitmap(30, 20, android.graphics.Bitmap.Config.ARGB_8888)));
        backgroundView.measure(0, 0);
        TestSupport.check(backgroundView.getMeasuredWidth() == 30 && backgroundView.getMeasuredHeight() == 20,
                "background minimum size invalidates cached measurement");
    }

    static void textLayout() throws Exception {
        java.lang.reflect.Method getter = TextView.class.getMethod("getLayout");
        java.lang.reflect.Method ellipsis = android.text.Layout.class.getMethod("getEllipsisCount", int.class);
        TextView view = new TextView(new TestContext());
        TestSupport.check(getter.invoke(view) == null, "text layout absent before initial measurement");
        view.setText("A long line that cannot fit into the narrow view");
        view.setSingleLine(true);
        view.setEllipsize(android.text.TextUtils.TruncateAt.END);
        view.measure(EXACTLY | 80, AT_MOST | 100);
        android.text.Layout layout = (android.text.Layout) getter.invoke(view);
        TestSupport.check(layout != null, "measurement exposes real text layout");
        TestSupport.check(((Integer) ellipsis.invoke(layout, 0)).intValue() > 0,
                "layout reports rendered text truncation");
        TestSupport.check(((Integer) ellipsis.invoke(layout, 1)).intValue() == 0,
                "ellipsis sentinel row is zero");
        view.setText("a");
        view.measure(EXACTLY | 80, AT_MOST | 100);
        layout = (android.text.Layout) getter.invoke(view);
        TestSupport.check(((Integer) ellipsis.invoke(layout, 0)).intValue() == 0,
                "shorter text refreshes ellipsis metadata");
        TestSupport.check(layout.getWidth() == 80 && layout.getText().toString().equals("a")
                && layout.getLineStart(0) == 0 && layout.getLineEnd(0) == 1,
                "layout exposes measured width and original line range");
        view.setSingleLine(false);
        view.setMaxLines(2);
        view.setText("a\nb\nc");
        view.measure(EXACTLY | 80, AT_MOST | 200);
        layout = view.getLayout();
        TestSupport.check(layout.getLineCount() == 2 && layout.getEllipsisCount(1) > 0
                && layout.getEllipsisCount(2) == 0, "multiline truncation has ellipsis and sentinel metadata");
        int height = layout.getHeight();
        view.setLineSpacing(8, 1.5f);
        view.measure(EXACTLY | 80, AT_MOST | 200);
        layout = view.getLayout();
        TestSupport.check(layout.getHeight() > height && layout.getSpacingAdd() == 8f
                && layout.getSpacingMultiplier() == 1.5f, "line spacing refreshes layout heights");
        TestSupport.check(layout.getLineForOffset(2) == 1 && layout.getLineForVertical(layout.getLineTop(1)) == 1,
                "line lookup follows measured ranges and vertical positions");
        view.layout(0, 0, 120, view.getMeasuredHeight());
        TestSupport.check(view.getLayout().getWidth() == 120,
                "size change exposes new text width before subclass callbacks");
        TestSupport.check(android.text.Layout.Alignment.ALIGN_CENTER.ordinal() == 2,
                "text alignment uses Android enum ABI");
    }

    static void rowGeometry() {
        TestContext context = new TestContext();
        android.widget.LinearLayout row = new android.widget.LinearLayout(context);
        View first = new View(context), second = new View(context);
        row.addView(first, new android.widget.LinearLayout.LayoutParams(60, 40));
        row.addView(second, new android.widget.LinearLayout.LayoutParams(70, 40));
        row.measure(EXACTLY | 200, EXACTLY | 40);
        row.layout(0, 0, 200, 40);
        TestSupport.check(row.getOrientation() == 0, "linear layout defaults to Android horizontal orientation");
        TestSupport.check(second.getLeft() == 60 && second.getTop() == 0 && second.getHeight() == 40,
                "default row places second item beside first at full height");
    }

    static void drawingTime() throws Exception {
        java.lang.reflect.Method getter = View.class.getMethod("getDrawingTime");
        View detached = new View(new TestContext());
        TestSupport.check(((Long) getter.invoke(detached)).longValue() == 0,
                "unattached view has no hierarchy drawing time");
    }

    static void directionResolution() {
        TestContext context = new TestContext();
        View child = new View(context);
        DirectionParams params = new DirectionParams();
        child.setLayoutParams(params);
        TestSupport.check(params.resolved == 0, "setting parameters resolves physical anchors");
        AnchorParent parent = new AnchorParent(context);
        parent.addView(child, params);
        parent.measure(EXACTLY | 200, EXACTLY | 80);
        TestSupport.check(child.getMeasuredWidth() == 200, "resolved anchors fill zero-dimension constrained child");
        parent.setLayoutDirection(1);
        parent.measure(EXACTLY | 200, EXACTLY | 80);
        TestSupport.check(child.getLayoutDirection() == 1 && params.resolved == 1,
                "parent direction changes resolve inherited child parameters before parent measurement");
        params.setMarginStart(12);
        params.setMarginEnd(7);
        child.setLayoutParams(params);
        TestSupport.check(params.isMarginRelative() && params.leftMargin == 7 && params.rightMargin == 12,
                "relative margins resolve into RTL physical sides");
        android.view.ViewGroup.MarginLayoutParams copy = new android.view.ViewGroup.MarginLayoutParams(params);
        copy.resolveLayoutDirection(0);
        TestSupport.check(copy.leftMargin == 12 && copy.rightMargin == 7,
                "copied relative margins can resolve into opposite direction");
    }

    static final class TouchContent extends View {
        boolean cancelled;
        TouchContent(android.content.Context context) { super(context); setMinimumHeight(600); }
        @Override public boolean onTouchEvent(android.view.MotionEvent event) {
            if (event.getActionMasked() == 3) cancelled = true;
            return true;
        }
    }
    static void scrolling() {
        TestContext context = new TestContext();
        android.widget.ScrollView scroll = new android.widget.ScrollView(context);
        TouchContent content = new TouchContent(context);
        scroll.addView(content, new FrameLayout.LayoutParams(-1, -2));
        scroll.measure(EXACTLY | 200, EXACTLY | 100);
        scroll.layout(0, 0, 200, 100);
        TestSupport.check(content.getWidth() == 200 && content.getHeight() == 600,
                "vertical scroll measures full-width content with unbounded height");
        long now = android.os.SystemClock.uptimeMillis();
        scroll.dispatchTouchEvent(android.view.MotionEvent.obtain(now, now, 0, 50, 90, 0));
        scroll.dispatchTouchEvent(android.view.MotionEvent.obtain(now, now + 20, 2, 50, 40, 0));
        scroll.dispatchTouchEvent(android.view.MotionEvent.obtain(now, now + 40, 2, 50, 10, 0));
        TestSupport.check(content.cancelled && scroll.getScrollY() > 0,
                "drag interception cancels child and scrolls content");
        scroll.dispatchTouchEvent(android.view.MotionEvent.obtain(now, now + 50, 3, 50, 10, 0));
        scroll.scrollTo(0, 10000);
        TestSupport.check(scroll.getScrollY() == 500 && !scroll.canScrollVertically(1)
                && scroll.canScrollVertically(-1), "scroll offsets clamp to measured content range");
    }

    static void lineLimits() {
        TextView view = new TextView(new TestContext());
        TestSupport.check(view.getMaxLines() == Integer.MAX_VALUE && view.getMinLines() == 0
                && view.getMaxHeight() == -1 && view.getMinHeight() == -1,
                "default text limits are lines with unlimited maximum");
        view.setSingleLine(true);
        view.setEllipsize(android.text.TextUtils.TruncateAt.END);
        view.setText("long text that must be ellipsized");
        view.measure(EXACTLY | 30, AT_MOST | 100);
        view.layout(0, 0, 30, view.getMeasuredHeight());
        TestSupport.check(view.getMaxLines() == 1 && view.getMinLines() == 1
                && view.getLayout().getEllipsisCount(view.getMaxLines()) == 0,
                "single-line text exposes one-line limits and valid layout sentinel");
        view.setSingleLine(false);
        view.setLines(3);
        view.setMaxLines(2);
        TestSupport.check(view.getMinLines() == 3 && view.getMaxLines() == 2,
                "maximum lines replace only the previous maximum");
        view.setMaxHeight(40);
        TestSupport.check(view.getMaxLines() == -1 && view.getMaxHeight() == 40,
                "pixel maximum replaces line maximum");
        view.setMaxLines(4);
        TestSupport.check(view.getMaxHeight() == -1 && view.getMaxLines() == 4,
                "line maximum replaces pixel maximum");
        view.setHeight(60);
        view.setMinLines(2);
        TestSupport.check(view.getMinHeight() == -1 && view.getMinLines() == 2
                && view.getMaxHeight() == 60 && view.getMaxLines() == -1,
                "minimum and maximum limit modes remain independent");
    }

    static final class DrawableCallbacks implements android.graphics.drawable.Drawable.Callback {
        int invalidations;
        public void invalidateDrawable(android.graphics.drawable.Drawable who) { invalidations++; }
        public void scheduleDrawable(android.graphics.drawable.Drawable who, Runnable task, long when) {}
        public void unscheduleDrawable(android.graphics.drawable.Drawable who, Runnable task) {}
    }
    static int pixel(android.graphics.drawable.Drawable drawable) {
        android.graphics.Bitmap bitmap = android.graphics.Bitmap.createBitmap(2, 2,
                android.graphics.Bitmap.Config.ARGB_8888);
        drawable.setBounds(0, 0, 2, 2);
        drawable.draw(new android.graphics.Canvas(bitmap));
        return bitmap.getPixel(0, 0);
    }
    static void drawableColors() {
        final int orange = 0xffff8800, gray = 0xff666666;
        android.content.res.ColorStateList tint = new android.content.res.ColorStateList(
                new int[][] {{0x010100a1}, {}}, new int[] {orange, gray});
        android.graphics.drawable.ColorDrawable color = new android.graphics.drawable.ColorDrawable(0xff000000);
        DrawableCallbacks callback = new DrawableCallbacks();
        color.setCallback(callback);
        color.setTintList(tint);
        TestSupport.check(color.isStateful() && pixel(color) == gray,
                "solid drawable renders its default tint");
        int before = callback.invalidations;
        color.setState(new int[] {0x010100a1});
        TestSupport.check(pixel(color) == orange && callback.invalidations > before,
                "selected solid drawable changes tint and invalidates owner");
        android.graphics.PorterDuffColorFilter filter = new android.graphics.PorterDuffColorFilter(
                0xff00ff00, android.graphics.PorterDuff.Mode.SRC_IN);
        color.setColorFilter(filter);
        TestSupport.check(color.getColorFilter() == filter && pixel(color) == 0xff00ff00,
                "explicit drawable color filter takes precedence over tint");
        color.clearColorFilter();
        TestSupport.check(pixel(color) == orange, "clearing explicit filter restores drawable tint");
        color.setTintList(null);
        before = callback.invalidations;
        color.setAlpha(128);
        TestSupport.check(color.getAlpha() == 128 && color.getColor() == 0x80000000
                && pixel(color) == 0x80000000 && callback.invalidations > before,
                "solid alpha affects returned color and schedules repaint");
        color.setColor(0xff112233);
        TestSupport.check(color.getAlpha() == 255 && pixel(color) == 0xff112233,
                "setting solid color replaces prior alpha modulation");
        android.graphics.Bitmap source = android.graphics.Bitmap.createBitmap(new int[]{0xff123456},
                1, 1, android.graphics.Bitmap.Config.ARGB_8888);
        android.graphics.drawable.BitmapDrawable bitmap = new android.graphics.drawable.BitmapDrawable(source);
        bitmap.setTintList(tint);
        bitmap.setState(new int[]{0x010100a1});
        TestSupport.check(bitmap.isStateful() && pixel(bitmap) == orange
                && source.getPixel(0, 0) == 0xff123456,
                "bitmap selection tint preserves source pixels");
        bitmap.setAlpha(128);
        TestSupport.check(pixel(bitmap) == 0x80ff8800, "bitmap paint alpha affects tinted pixels");
        android.graphics.Paint paint = new android.graphics.Paint();
        paint.setColor(0x80112233);
        paint.setColorFilter(new android.graphics.PorterDuffColorFilter(0xff00ff00,
                android.graphics.PorterDuff.Mode.SRC_IN));
        android.graphics.Bitmap target = android.graphics.Bitmap.createBitmap(2, 2,
                android.graphics.Bitmap.Config.ARGB_8888);
        new android.graphics.Canvas(target).drawRect(0, 0, 2, 2, paint);
        TestSupport.check(target.getPixel(0, 0) == 0x8000ff00,
                "canvas filters primitive color while preserving source alpha");
        target.eraseColor(0);
        paint.setAlpha(255);
        new android.graphics.Canvas(target).drawBitmap(source, null,
                new android.graphics.Rect(0, 0, 2, 2), paint);
        TestSupport.check(target.getPixel(1, 1) == 0xff00ff00,
                "bitmap-backed canvas scales and filters into its own destination");
        target.eraseColor(0);
        android.graphics.Canvas clipped = new android.graphics.Canvas(target);
        clipped.clipRect(1, 0, 2, 2);
        clipped.scale(2, 2);
        clipped.drawBitmap(source, 0, 0, paint);
        TestSupport.check(target.getPixel(0, 0) == 0 && target.getPixel(1, 1) == 0xff00ff00,
                "bitmap draw honors canvas scale and clip");
        target.setPixel(0, 0, 0xff112233);
        target.setPixel(1, 0, 0xff445566);
        new android.graphics.Canvas(target).drawBitmap(target, new android.graphics.Rect(0, 0, 1, 1),
                new android.graphics.Rect(1, 0, 2, 1), null);
        TestSupport.check(target.getPixel(1, 0) == 0xff112233,
                "overlapping bitmap draw snapshots source before destination writes");
    }

    static void shapeAndImageTints() {
        final int orange = 0xffff8800, gray = 0xff666666;
        android.content.res.ColorStateList tint = new android.content.res.ColorStateList(
                new int[][] {{0x010100a1}, {}}, new int[] {orange, gray});
        android.graphics.drawable.GradientDrawable gradient = new android.graphics.drawable.GradientDrawable();
        gradient.setColor(0xff112233);
        android.graphics.drawable.ShapeDrawable shape = new android.graphics.drawable.ShapeDrawable(
                new android.graphics.drawable.shapes.RectShape());
        shape.getPaint().setColor(0xff112233);
        android.graphics.drawable.Drawable[] drawables = {gradient, shape};
        android.graphics.PorterDuffColorFilter filter = new android.graphics.PorterDuffColorFilter(
                0xff00ff00, android.graphics.PorterDuff.Mode.SRC_IN);
        for (android.graphics.drawable.Drawable drawable : drawables) {
            DrawableCallbacks callback = new DrawableCallbacks();
            drawable.setCallback(callback);
            drawable.setTintList(tint);
            TestSupport.check(drawable.isStateful() && pixel(drawable) == gray,
                    "shape drawable renders default state tint");
            int before = callback.invalidations;
            drawable.setState(new int[] {0x010100a1});
            TestSupport.check(pixel(drawable) == orange && callback.invalidations > before,
                    "shape selection updates tint and invalidates callback");
            drawable.setColorFilter(filter);
            TestSupport.check(drawable.getColorFilter() == filter && pixel(drawable) == 0xff00ff00,
                    "shape explicit filter overrides tint");
            drawable.clearColorFilter();
            TestSupport.check(pixel(drawable) == orange, "shape filter removal restores selection tint");
            before = callback.invalidations;
            drawable.setAlpha(128);
            TestSupport.check(pixel(drawable) == 0x80ff8800 && callback.invalidations > before,
                    "shape alpha preserves tint and requests redraw");
        }
        TestSupport.check(shape.getPaint().getColor() == 0xff112233
                && shape.getPaint().getColorFilter() == null, "shape drawing restores caller paint");
        ImageView image = new ImageView(new TestContext());
        image.setSelected(true);
        image.setImageDrawable(new android.graphics.drawable.ColorDrawable(0xff112233));
        image.setImageTintList(tint);
        TestSupport.check(pixel(image.getDrawable()) == orange,
                "late image tint receives existing selected state");
        image.setImageDrawable(new android.graphics.drawable.ColorDrawable(0xff445566));
        TestSupport.check(pixel(image.getDrawable()) == orange,
                "replacement image receives selected state after tint");
        image.setSelected(false);
        TestSupport.check(pixel(image.getDrawable()) == gray,
                "image deselection propagates to tinted drawable");
    }

    public static int traversalBench(int count) {
        TestContext context = new TestContext();
        FrameLayout root = new FrameLayout(context);
        for (int i = 0; i < 30; i++) {
            TextView child = new TextView(context);
            child.setText("redraw traversal");
            root.addView(child, new FrameLayout.LayoutParams(-1, -1));
        }
        root.measure(EXACTLY | 200, EXACTLY | 75);
        root.layout(0, 0, 200, 75);
        int sum = 0;
        for (int i = 0; i < count; i++) {
            root.invalidate();
            root.measure(EXACTLY | 200, EXACTLY | 75);
            root.layout(0, 0, 200, 75);
            sum += root.getMeasuredWidth();
        }
        return sum;
    }

    public static int viewBench(int count) throws Exception {
        TestContext context = new TestContext();
        byte[] xml = new AxmlBuilder().start("TextView")
                .attrString(0x0101014f, "text", "view construction")
                .attr(0x010100d5, "padding", AxmlBuilder.TYPE_DIMENSION, 8 << 8)
                .attr(0x0101013f, "minWidth", AxmlBuilder.TYPE_DIMENSION, 24 << 8).end().build();
        int sum = 0;
        for (int i = 0; i < count; i++) {
            CompiledXmlResourceParser parser = new CompiledXmlResourceParser(xml);
            while (parser.next() != org.xmlpull.v1.XmlPullParser.START_TAG) {}
            TextView view = new TextView(context, parser);
            view.measure(EXACTLY | 200, EXACTLY | 75);
            sum += view.getMeasuredWidth();
            parser.close();
        }
        return sum;
    }

    public static int drawBench(int count) {
        TestContext context = new TestContext();
        FrameLayout root = new FrameLayout(context);
        android.graphics.Bitmap bitmap = android.graphics.Bitmap.createBitmap(24, 24,
                android.graphics.Bitmap.Config.ARGB_8888);
        bitmap.eraseColor(0xff112233);
        for (int i = 0; i < 30; i++) {
            ImageView icon = new ImageView(context);
            icon.setImageBitmap(bitmap);
            icon.setImageTintList(android.content.res.ColorStateList.valueOf(0xffff8800));
            FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(24, 24);
            params.leftMargin = i % 6 * 30;
            params.topMargin = i / 6 * 50;
            root.addView(icon, params);
            TextView text = new TextView(context);
            text.setText("tab");
            FrameLayout.LayoutParams label = new FrameLayout.LayoutParams(24, 20);
            label.leftMargin = params.leftMargin;
            label.topMargin = params.topMargin + 24;
            root.addView(text, label);
        }
        root.measure(EXACTLY | 200, EXACTLY | 300);
        root.layout(0, 0, 200, 300);
        android.graphics.Canvas canvas = new android.graphics.Canvas();
        int sum = 0;
        for (int i = 0; i < count; i++) {
            root.draw(canvas);
            sum += root.getWidth();
        }
        return sum;
    }

    static void insetContracts() throws Exception {
        android.view.WindowInsets.Builder builder = new android.view.WindowInsets.Builder();
        java.lang.reflect.Method setter = builder.getClass().getMethod("setSystemWindowInsets", android.graphics.Insets.class);
        setter.invoke(builder, android.graphics.Insets.of(4, 44, 6, 34));
        android.view.WindowInsets insets = builder.build();
        TestSupport.check(insets.getSystemWindowInsetTop() == 44 && insets.getSystemWindowInsetBottom() == 34
                && !insets.isConsumed(), "window insets preserve populated edges");
        FrameLayout parent = new FrameLayout(new TestContext());
        View first = new View(parent.getContext());
        View second = new View(parent.getContext());
        first.setFitsSystemWindows(true);
        second.setFitsSystemWindows(true);
        parent.addView(first);
        parent.addView(second);
        parent.dispatchApplyWindowInsets(insets);
        TestSupport.check(first.getPaddingTop() == 44 && first.getPaddingBottom() == 34
                && second.getPaddingTop() == 44, "sibling views receive original insets independently");
        android.view.WindowInsets copy = new android.view.WindowInsets(insets);
        TestSupport.check(copy.getSystemWindowInsetRight() == 6
                && insets.consumeSystemWindowInsets().getSystemWindowInsetTop() == 0
                && insets.getSystemWindowInsetTop() == 44, "inset copy and consumption preserve original value");
        android.graphics.Rect local = new android.graphics.Rect();
        View plain = new View(parent.getContext());
        android.view.WindowInsets remaining = plain.computeSystemWindowInsets(insets, local);
        TestSupport.check(local.equals(new android.graphics.Rect(4, 44, 6, 34))
                && remaining.getSystemWindowInsetTop() == 0, "local inset computation returns consumed system edges");
    }

    static class DispatchBase {
        final int base;
        DispatchBase(int value) { base = value; }
        private int privateValue() { return base * 2; }
        int value() { return privateValue(); }
        int value(int extra) { return value() + extra; }
    }
    static final class DispatchFirst extends DispatchBase {
        DispatchFirst(int value) { super(value); }
        @Override int value() { return super.value() + 3; }
    }
    static final class DispatchSecond extends DispatchBase {
        DispatchSecond(int value) { super(value); }
        @Override int value() { return super.value() + 7; }
    }
    static void dispatchContracts() {
        int sum = 0;
        for (int i = 0; i < 100; i++) {
            DispatchBase object = i % 2 == 0 ? new DispatchFirst(i) : new DispatchSecond(i);
            sum += object.value(2);
        }
        TestSupport.check(sum == 10600, "repeated constructor, private, super and polymorphic overload dispatch");
    }

    static void reflectionContracts() throws Exception {
        Class<?> parent = Class.forName("java.lang.ReflectiveOperationException");
        for (Class<?> child : new Class<?>[] {NoSuchMethodException.class, NoSuchFieldException.class,
                IllegalAccessException.class, InstantiationException.class, ClassNotFoundException.class,
                java.lang.reflect.InvocationTargetException.class}) {
            TestSupport.check(parent.isAssignableFrom(child), "reflection failures share their declared parent");
        }
        final boolean[] handled = {false};
        View root = new View(new TestContext());
        root.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            public android.view.WindowInsets onApplyWindowInsets(View view, android.view.WindowInsets insets) {
                try { probePrivateInsets(); }
                catch (ReflectiveOperationException expected) { handled[0] = true; }
                catch (Exception wrongHierarchy) { handled[0] = false; }
                return insets;
            }
        });
        android.view.WindowInsets insets = new android.view.WindowInsets.Builder()
                .setSystemWindowInsets(android.graphics.Insets.of(0, 44, 0, 34)).build();
        TestSupport.check(root.dispatchApplyWindowInsets(insets) == insets && handled[0],
                "unsupported private inset probe is caught without aborting traversal");
        java.lang.reflect.Method getter = android.app.Activity.class.getMethod("getChangingConfigurations");
        TestSupport.check(((Integer) getter.invoke(new android.app.Activity())).intValue() == 0,
                "normal activity reports no pending recreation configuration changes");
    }

    static void probePrivateInsets() throws Exception {
        View.class.getDeclaredMethod("getViewRootImpl");
    }
}
