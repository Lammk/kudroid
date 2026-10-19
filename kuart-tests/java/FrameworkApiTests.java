import android.animation.ArgbEvaluator;
import android.animation.ObjectAnimator;
import android.animation.ValueAnimator;
import android.graphics.Path;
import android.graphics.PathMeasure;
import android.graphics.RectF;
import android.os.LocaleList;
import android.util.FloatProperty;
import android.util.PathParser;
import android.util.Property;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.PathInterpolator;
import android.widget.FrameLayout;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.EOFException;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.util.Locale;

/** Library classes and View members that app libraries reach for: files, paths, animation, state. */
public final class FrameworkApiTests {
    private static void check(boolean ok, String name) { TestSupport.check(ok, name); }

    public static String run() {
        TestSupport.report.setLength(0);
        try { randomAccessFile(); } catch (Throwable t) { TestSupport.fail("randomAccessFile", t); }
        try { dataStreams(); } catch (Throwable t) { TestSupport.fail("dataStreams", t); }
        try { bufferedStreams(); } catch (Throwable t) { TestSupport.fail("bufferedStreams", t); }
        try { paths(); } catch (Throwable t) { TestSupport.fail("paths", t); }
        try { pathInterpolator(); } catch (Throwable t) { TestSupport.fail("pathInterpolator", t); }
        try { properties(); } catch (Throwable t) { TestSupport.fail("properties", t); }
        try { valueAnimator(); } catch (Throwable t) { TestSupport.fail("valueAnimator", t); }
        try { viewState(); } catch (Throwable t) { TestSupport.fail("viewState", t); }
        try { locales(); } catch (Throwable t) { TestSupport.fail("locales", t); }
        return TestSupport.report.toString();
    }

    // ---- java.io

    static void randomAccessFile() throws Exception {
        final String path = "/tmp/kudroid_raf_test.bin";
        new File(path).delete();
        RandomAccessFile f = new RandomAccessFile(path, "rw");
        f.writeInt(0x01020304);
        f.writeUTF("héllo");
        f.writeLong(-2L);
        f.writeDouble(1.5);
        f.write(new byte[] {9, 8, 7}, 0, 3);
        check(f.getFilePointer() == f.length(), "the pointer follows the writes");
        final long length = f.length();
        f.seek(0);
        check(f.readInt() == 0x01020304, "readInt after seek(0)");
        check("héllo".equals(f.readUTF()), "readUTF round-trips a non-ASCII string");
        check(f.readLong() == -2L, "readLong");
        check(f.readDouble() == 1.5, "readDouble");
        byte[] tail = new byte[3];
        f.readFully(tail);
        check(tail[0] == 9 && tail[2] == 7, "readFully");
        check(f.read() == -1, "read at end of file is -1");
        boolean eof = false;
        try { f.readInt(); } catch (EOFException e) { eof = true; }
        check(eof, "readInt at end of file throws EOFException");
        f.seek(4);
        check(f.skipBytes(2) == 2 && f.getFilePointer() == 6, "skipBytes moves forward");
        check(f.skipBytes(1000) == length - 6, "skipBytes stops at the end");
        f.setLength(4);
        check(f.length() == 4 && f.getFilePointer() == 4, "setLength truncates and clamps the pointer");
        java.nio.channels.FileChannel channel = f.getChannel();
        check(channel.size() == 4, "the channel reports the file size");
        f.close();
        f.close();
        boolean closed = false;
        try { f.length(); } catch (IOException e) { closed = true; }
        check(closed, "use after close throws IOException");

        RandomAccessFile ro = new RandomAccessFile(path, "r");
        boolean refused = false;
        try { ro.write(1); } catch (IOException e) { refused = true; }
        check(refused, "a read-only file refuses writes");
        ro.close();

        boolean missing = false;
        try { new RandomAccessFile("/tmp/kudroid_no_such_file.bin", "r"); }
        catch (FileNotFoundException e) { missing = true; }
        check(missing, "a missing file throws FileNotFoundException");
        boolean badMode = false;
        try { new RandomAccessFile(path, "x"); } catch (IllegalArgumentException e) { badMode = true; }
        check(badMode, "an unknown mode is rejected");
        new File(path).delete();
    }

    static void dataStreams() throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(bytes);
        out.writeBoolean(true);
        out.writeByte(-3);
        out.writeShort(-2);
        out.writeChar('Z');
        out.writeInt(123456789);
        out.writeLong(1L << 40);
        out.writeFloat(2.5f);
        out.writeUTF("abé€");
        out.writeBytes("xy");
        check(out.size() == bytes.size(), "size() counts the bytes written");
        DataInputStream in = new DataInputStream(new ByteArrayInputStream(bytes.toByteArray()));
        check(in.readBoolean(), "readBoolean");
        check(in.readByte() == -3, "readByte");
        check(in.readShort() == -2, "readShort");
        check(in.readChar() == 'Z', "readChar");
        check(in.readInt() == 123456789, "readInt");
        check(in.readLong() == (1L << 40), "readLong");
        check(in.readFloat() == 2.5f, "readFloat");
        String utf = in.readUTF();
        check(utf.length() == 4, "modified UTF-8 keeps every char (length " + utf.length() + ")");
        check(utf.length() == 4 && utf.charAt(0) == 'a' && utf.charAt(1) == 'b', "ASCII decodes");
        check(utf.length() == 4 && utf.charAt(2) == 0x00e9 && utf.charAt(3) == 0x20ac, "2- and 3-byte sequences decode");
        // NUL is encoded as C0 80, and the length is a 16-bit byte count.
        byte[] encodedNul = {0, 2, (byte) 0xC0, (byte) 0x80};
        check(new DataInputStream(new ByteArrayInputStream(encodedNul)).readUnsignedShort() == 2, "length prefix");
        ByteArrayOutputStream nulBytes = new ByteArrayOutputStream();
        new DataOutputStream(nulBytes).writeUTF(new String(new char[] {'x'}));
        check(nulBytes.size() == 3 && nulBytes.toByteArray()[2] == 'x', "writeUTF prefixes the byte length");
        check(in.readUnsignedByte() == 'x', "readUnsignedByte");
        check(in.skipBytes(5) == 1, "skipBytes reports what was skipped");
        boolean eof = false;
        try { in.readInt(); } catch (EOFException e) { eof = true; }
        check(eof, "reading past the end throws EOFException");
    }

    static void bufferedStreams() throws Exception {
        byte[] data = new byte[20000];
        for (int i = 0; i < data.length; i++) data[i] = (byte) (i * 7);
        BufferedInputStream in = new BufferedInputStream(new ByteArrayInputStream(data), 64);
        check(in.markSupported(), "buffered streams support mark");
        in.mark(100);
        byte[] first = new byte[50];
        check(in.read(first, 0, 50) == 50, "read after mark");
        in.reset();
        byte[] again = new byte[50];
        check(in.read(again, 0, 50) == 50 && java.util.Arrays.equals(first, again),
                "reset replays the marked bytes");
        byte[] rest = new byte[data.length];
        int total = 0;
        int n;
        in.reset();
        while ((n = in.read(rest, total, rest.length - total)) > 0) total += n;
        check(total == data.length && java.util.Arrays.equals(rest, data), "all bytes arrive intact");

        ByteArrayOutputStream sink = new ByteArrayOutputStream();
        BufferedOutputStream out = new BufferedOutputStream(sink, 16);
        out.write(1);
        check(sink.size() == 0, "small writes stay buffered");
        out.write(data, 0, 100);
        out.flush();
        check(sink.size() == 101, "flush pushes everything through");
        out.close();
    }

    // ---- graphics

    static void paths() throws Exception {
        Path rect = new Path();
        rect.addRect(1, 2, 11, 7, Path.Direction.CW);
        RectF bounds = new RectF();
        check(rect.isRect(bounds), "an added rectangle is a rect");
        check(bounds.left == 1 && bounds.top == 2 && bounds.right == 11 && bounds.bottom == 7, "rect bounds");

        Path curve = new Path();
        curve.moveTo(0, 0);
        curve.cubicTo(0, 10, 10, 10, 10, 0);
        RectF exact = new RectF();
        curve.computeBounds(exact, true);
        check(exact.bottom > 5.5f && exact.bottom < 8f, "exact bounds follow the curve, not the control points");
        RectF loose = new RectF();
        curve.computeBounds(loose, false);
        check(loose.bottom == 10f, "approximate bounds use the control points");

        float[] approx = curve.approximate(0.01f);
        check(approx.length % 3 == 0 && approx.length >= 6, "approximate returns (fraction, x, y) triples");
        check(approx[0] == 0f && approx[approx.length - 3] == 1f, "fractions run from 0 to 1");
        boolean monotonic = true;
        for (int i = 3; i < approx.length; i += 3) if (approx[i] < approx[i - 3]) monotonic = false;
        check(monotonic, "fractions never decrease");

        Path line = new Path();
        line.moveTo(0, 0);
        line.lineTo(3, 4);
        PathMeasure measure = new PathMeasure(line, false);
        check(Math.abs(measure.getLength() - 5f) < 0.001f, "a 3-4-5 segment measures 5");
        float[] pos = new float[2];
        float[] tan = new float[2];
        check(measure.getPosTan(2.5f, pos, tan), "getPosTan inside the path");
        check(Math.abs(pos[0] - 1.5f) < 0.001f && Math.abs(pos[1] - 2f) < 0.001f, "position at half length");
        check(Math.abs(tan[0] - 0.6f) < 0.001f && Math.abs(tan[1] - 0.8f) < 0.001f, "unit tangent");
        check(!measure.getPosTan(6f, pos, tan), "past the end there is no position");
        Path segment = new Path();
        check(measure.getSegment(1f, 3f, segment, true) && !segment.isEmpty(), "getSegment copies a sub-range");

        Path parsed = PathParser.createPathFromPathData("M0,0 L10,0 L10,10 L0,10 z");
        check(parsed != null && parsed.isRect(new RectF()), "path data parses to a rectangle");
        Path arc = PathParser.createPathFromPathData("M0,10 a10,10 0 0 1 20,0");
        RectF arcBounds = new RectF();
        arc.computeBounds(arcBounds, true);
        check(arcBounds.top < 1f && arcBounds.right > 19.9f, "an SVG arc reaches the top of its circle");
        check(PathParser.createPathFromPathData("L1,1") == null, "path data must start with a moveto");
        PathParser.PathData from = new PathParser.PathData("M0,0 L10,0");
        PathParser.PathData to = new PathParser.PathData("M0,0 L20,0");
        check(PathParser.canMorph(from, to), "same commands can morph");
        PathParser.PathData mid = new PathParser.PathData(from);
        PathParser.interpolatePathData(mid, from, to, 0.5f);
        Path midPath = new Path();
        PathParser.createPathFromPathData(midPath, mid);
        RectF midBounds = new RectF();
        midPath.computeBounds(midBounds, false);
        check(midBounds.right == 15f, "interpolated path data is halfway");
    }

    static void pathInterpolator() throws Exception {
        PathInterpolator ease = new PathInterpolator(0.4f, 0f, 0.2f, 1f);
        check(ease.getInterpolation(0f) == 0f, "an interpolator starts at 0");
        check(ease.getInterpolation(1f) == 1f, "an interpolator ends at 1");
        float previous = 0f;
        boolean monotonic = true;
        for (int i = 1; i <= 20; i++) {
            float v = ease.getInterpolation(i / 20f);
            if (v < previous - 0.0001f) monotonic = false;
            previous = v;
        }
        check(monotonic, "the standard ease curve is monotonic");
        float mid = ease.getInterpolation(0.5f);
        check(mid > 0.5f && mid < 1f, "ease-in-out is past halfway at the midpoint");
        PathInterpolator linear = new PathInterpolator(0f, 0f, 1f, 1f);
        check(Math.abs(linear.getInterpolation(0.25f) - 0.25f) < 0.01f, "a straight path is linear");
        Path p = new Path();
        p.lineTo(1f, 1f);
        check(new PathInterpolator(p).getInterpolation(1f) == 1f, "a Path based interpolator works");
        boolean rejected = false;
        Path bad = new Path();
        bad.lineTo(0.5f, 0.5f);
        try { new PathInterpolator(bad); } catch (IllegalArgumentException e) { rejected = true; }
        check(rejected, "a path that does not end at (1,1) is rejected");
    }

    // ---- android.util.Property and animators

    public static class Bean {
        public float alpha = 0f;
        public float getAlpha() { return alpha; }
        public void setAlpha(float a) { alpha = a; }
    }

    static final Property<Bean, Float> ALPHA = new FloatProperty<Bean>("alpha") {
        @Override public void setValue(Bean b, float v) { b.alpha = v; }
        @Override public Float get(Bean b) { return b.alpha; }
    };

    static void properties() throws Exception {
        Bean b = new Bean();
        ALPHA.set(b, 0.75f);
        check(b.alpha == 0.75f, "a FloatProperty writes through setValue");
        check(ALPHA.get(b) == 0.75f, "and reads back");
        check("alpha".equals(ALPHA.getName()) && ALPHA.getType() == Float.class, "name and type");
        Property<Bean, Float> custom = new Property<Bean, Float>(Float.class, "custom") {
            @Override public Float get(Bean x) { return x.alpha; }
        };
        check(custom.isReadOnly() == false, "isReadOnly defaults to false");
        boolean readOnlyFailed = false;
        try { custom.set(b, 1f); } catch (UnsupportedOperationException e) { readOnlyFailed = true; }
        check(readOnlyFailed, "set without an override throws");
        Property<Bean, Float> reflective = Property.of(Bean.class, Float.class, "alpha");
        reflective.set(b, 0.25f);
        check(b.alpha == 0.25f && reflective.get(b) == 0.25f, "Property.of reaches the accessors by name");
    }

    static void valueAnimator() throws Exception {
        ValueAnimator floats = ValueAnimator.ofFloat(0f, 10f);
        floats.setDuration(100);
        floats.setCurrentFraction(0.5f);
        check(((Float) floats.getAnimatedValue()) == 5f, "half way through ofFloat is the midpoint");
        check(floats.getAnimatedFraction() == 0.5f, "animated fraction");
        check(floats.getDuration() == 100, "duration");
        check(!floats.isRunning(), "setCurrentFraction does not start the animator");

        ValueAnimator ints = ValueAnimator.ofInt(10, 20);
        ints.setDuration(10);
        ints.setCurrentFraction(1f);
        check(((Integer) ints.getAnimatedValue()) == 20, "ofInt ends at the last value");

        ValueAnimator colors = ValueAnimator.ofArgb(0xFF000000, 0xFFFFFFFF);
        colors.setDuration(10);
        colors.setCurrentFraction(0.5f);
        int c = (Integer) colors.getAnimatedValue();
        check((c & 0xFF) >= 0xB0 && (c & 0xFF) <= 0xC0, "ofArgb blends in linear space (gamma 2.2)");
        check(((Integer) new ArgbEvaluator().evaluate(0f, 0xFFFF0000, 0xFF0000FF)) == 0xFFFF0000,
                "ArgbEvaluator at 0 is the start colour");

        Bean b = new Bean();
        ObjectAnimator o = ObjectAnimator.ofFloat(b, ALPHA, 0f, 1f);
        o.setDuration(100);
        o.setInterpolator(new android.view.animation.LinearInterpolator());
        o.setCurrentFraction(0.25f);
        check(b.alpha == 0.25f, "an ObjectAnimator drives its target's property (alpha=" + b.alpha + ")");
        ObjectAnimator byName = ObjectAnimator.ofFloat(b, "alpha", 2f, 4f);
        byName.setDuration(100);
        byName.setCurrentFraction(0.5f);
        check(b.alpha == 3f, "an ObjectAnimator by name finds the setter");
    }

    // ---- View

    static void viewState() throws Exception {
        InflaterTests.TestContext ctx = InflaterTests.context();
        View v = new View(ctx);
        check(contains(v.getDrawableState(), com.android.internal.R.attr.state_enabled), "enabled by default");
        check(!contains(v.getDrawableState(), com.android.internal.R.attr.state_pressed), "not pressed by default");
        v.setPressed(true);
        check(contains(v.getDrawableState(), com.android.internal.R.attr.state_pressed), "pressed shows in the state");
        v.setPressed(false);
        v.setEnabled(false);
        check(!contains(v.getDrawableState(), com.android.internal.R.attr.state_enabled), "disabled drops enabled");
        v.setSelected(true);
        check(contains(v.getDrawableState(), com.android.internal.R.attr.state_selected), "selected shows in the state");

        v.setWillNotDraw(true);
        check(v.willNotDraw(), "willNotDraw round-trips");
        check(v.getImportantForAccessibility() == View.IMPORTANT_FOR_ACCESSIBILITY_AUTO, "auto by default");
        v.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        check(v.getImportantForAccessibility() == View.IMPORTANT_FOR_ACCESSIBILITY_NO && !v.isImportantForAccessibility(),
                "importantForAccessibility NO hides the view");

        v.layout(10, 20, 110, 70);
        v.scrollTo(5, 6);
        check(v.getScrollX() == 5 && v.getScrollY() == 6, "scroll offsets");
        final int[] seen = new int[4];
        v.setOnScrollChangeListener(new View.OnScrollChangeListener() {
            @Override public void onScrollChange(View view, int x, int y, int ox, int oy) {
                seen[0] = x; seen[1] = y; seen[2] = ox; seen[3] = oy;
            }
        });
        v.scrollTo(7, 9);
        check(seen[0] == 7 && seen[1] == 9 && seen[2] == 5 && seen[3] == 6, "scroll listener sees old and new offsets");
        v.setX(50f);
        check(v.getX() == 50f && v.getTranslationX() == 40f, "setX stores a translation relative to left");
        v.setLeft(0);
        check(v.getWidth() == 110, "setLeft resizes without a layout pass");

        FrameLayout group = new FrameLayout(ctx);
        View child = new View(ctx);
        group.addView(child);
        check(child.getParent() == group && !child.isAttachedToWindow(), "not attached before a window");
        child.setVisibility(View.GONE);
        check(!child.isShown() && child.getVisibility() == View.GONE, "GONE hides the view");
        group.setVisibility(View.INVISIBLE);
        child.setVisibility(View.VISIBLE);
        check(!child.isShown(), "isShown looks at the ancestors");

        group.setAddStatesFromChildren(true);
        child.setPressed(true);
        check(contains(group.getDrawableState(), com.android.internal.R.attr.state_pressed),
                "a group that adds children's states shows a pressed child");

        android.util.SparseArray<android.os.Parcelable> container = new android.util.SparseArray<android.os.Parcelable>();
        View withId = new View(ctx);
        withId.setId(0x7f080010);
        withId.saveHierarchyState(container);
        check(container.get(0x7f080010) == View.BaseSavedState.EMPTY_STATE, "an id'd view saves empty state");
        withId.restoreHierarchyState(container);
        withId.setSaveEnabled(false);
        android.util.SparseArray<android.os.Parcelable> none = new android.util.SparseArray<android.os.Parcelable>();
        withId.saveHierarchyState(none);
        check(none.size() == 0, "setSaveEnabled(false) saves nothing");
        check(new View(ctx).getParent() == null && new View(ctx).getBaseline() == -1, "baseline defaults to -1");
    }

    static boolean contains(int[] state, int attr) {
        for (int s : state) if (s == attr) return true;
        return false;
    }

    // ---- locale

    static void locales() {
        Locale vi = Locale.forLanguageTag("vi-VN");
        check("vi".equals(vi.getLanguage()) && "VN".equals(vi.getCountry()), "forLanguageTag parses language and region");
        check("zh-Hans-CN".equals(Locale.forLanguageTag("zh-Hans-CN").toLanguageTag()), "script round-trips");
        LocaleList list = new LocaleList(vi, Locale.US, vi);
        check(list.size() == 2, "duplicates are dropped");
        check("vi-VN,en-US".equals(list.toLanguageTags()), "toLanguageTags joins with commas");
        check(LocaleList.forLanguageTags("fr-FR,de").get(1).getLanguage().equals("de"), "forLanguageTags parses a list");
        check(list.indexOf(Locale.US) == 1, "indexOf");
        check(Locale.getDefault(Locale.Category.FORMAT) != null, "Locale.Category exists");
    }
}
