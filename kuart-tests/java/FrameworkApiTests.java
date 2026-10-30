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
import java.io.FileOutputStream;
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
        try { atomics(); } catch (Throwable t) { TestSupport.fail("atomics", t); }
        try { unsafeAccess(); } catch (Throwable t) { TestSupport.fail("unsafeAccess", t); }
        try { readWriteLock(); } catch (Throwable t) { TestSupport.fail("readWriteLock", t); }
        try { widgetBasics(); } catch (Throwable t) { TestSupport.fail("widgetBasics", t); }
        try { sqlite(); } catch (Throwable t) { TestSupport.fail("sqlite", t); }
        try { drawables(); } catch (Throwable t) { TestSupport.fail("drawables", t); }
        try { textFormat(); } catch (Throwable t) { TestSupport.fail("textFormat", t); }
        try { drawPipeline(); } catch (Throwable t) { TestSupport.fail("drawPipeline", t); }
        try { textLayout(); } catch (Throwable t) { TestSupport.fail("textLayout", t); }
        try { dialogAndFiles(); } catch (Throwable t) { TestSupport.fail("dialogAndFiles", t); }
        try { layouts(); } catch (Throwable t) { TestSupport.fail("layouts", t); }
        try { frameworkExtras(); } catch (Throwable t) { TestSupport.fail("frameworkExtras", t); }
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

    // Channel segments store elements through lazySet; a no-op there loses every message.
    static void atomics() {
        java.util.concurrent.atomic.AtomicReferenceArray<String> arr =
                new java.util.concurrent.atomic.AtomicReferenceArray<String>(4);
        arr.lazySet(2, "x");
        check("x".equals(arr.get(2)), "AtomicReferenceArray.lazySet stores the value");
        check("xy".equals(arr.updateAndGet(2, new java.util.function.UnaryOperator<String>() {
            public String apply(String v) { return v + "y"; }
        })), "AtomicReferenceArray.updateAndGet");
        String stored = arr.get(2);
        check(arr.compareAndExchange(2, stored, "z") == stored && "z".equals(arr.get(2)),
                "AtomicReferenceArray.compareAndExchange returns the witness");

        java.util.concurrent.atomic.AtomicIntegerArray ia =
                new java.util.concurrent.atomic.AtomicIntegerArray(2);
        ia.lazySet(1, 5);
        check(ia.accumulateAndGet(1, 3, new java.util.function.IntBinaryOperator() {
            public int applyAsInt(int a, int b) { return a * b; }
        }) == 15, "AtomicIntegerArray.accumulateAndGet");

        java.util.concurrent.atomic.AtomicInteger ai = new java.util.concurrent.atomic.AtomicInteger(1);
        check(ai.getAndUpdate(new java.util.function.IntUnaryOperator() {
            public int applyAsInt(int v) { return v + 10; }
        }) == 1 && ai.get() == 11, "AtomicInteger.getAndUpdate");

        java.util.concurrent.atomic.AtomicReference<String> ref =
                new java.util.concurrent.atomic.AtomicReference<String>("a");
        check("ab".equals(ref.updateAndGet(new java.util.function.UnaryOperator<String>() {
            public String apply(String v) { return v + "b"; }
        })), "AtomicReference.updateAndGet");

        java.util.concurrent.atomic.AtomicLong al = new java.util.concurrent.atomic.AtomicLong(7);
        check(al.weakCompareAndSet(7L, 9L) && al.get() == 9L, "AtomicLong.weakCompareAndSet");
    }

    // Desugared JDK collections (ConcurrentHashMap) address array slots through
    // Unsafe; offsets are relative to the object base, not the field payload.
    static void unsafeAccess() throws Exception {
        sun.misc.Unsafe u = sun.misc.Unsafe.getUnsafe();
        Object[] objects = new Object[4];
        long base = u.arrayBaseOffset(Object[].class);
        int scale = u.arrayIndexScale(Object[].class);
        objects[1] = "one";
        check("one".equals(u.getObject(objects, base + scale)), "Unsafe reads an object array element");
        u.putObject(objects, base + 2L * scale, "two");
        check("two".equals(objects[2]), "Unsafe writes an object array element");
        Object witness = new Object();
        check(u.compareAndSwapObject(objects, base + 3L * scale, null, witness)
                && objects[3] == witness, "Unsafe CAS stores an object array element");

        int[] numbers = new int[4];
        numbers[2] = 7;
        long intBase = u.arrayBaseOffset(int[].class);
        int intScale = u.arrayIndexScale(int[].class);
        check(u.getInt(numbers, intBase + 2L * intScale) == 7, "Unsafe reads an int array element");
        u.putInt(numbers, intBase + 3L * intScale, 9);
        check(numbers[3] == 9, "Unsafe writes an int array element");

        Holder holder = new Holder();
        long valueOffset = u.objectFieldOffset(Holder.class.getDeclaredField("value"));
        long refOffset = u.objectFieldOffset(Holder.class.getDeclaredField("ref"));
        u.putInt(holder, valueOffset, 42);
        u.putObject(holder, refOffset, "field");
        check(holder.value == 42, "Unsafe writes an instance int field");
        check("field".equals(holder.ref), "Unsafe writes an instance reference field");
        check(u.getInt(holder, valueOffset) == 42, "Unsafe reads an instance int field");
    }

    static final class Holder {
        int value;
        Object ref;
    }

    static void readWriteLock() throws Exception {
        java.util.concurrent.locks.ReentrantReadWriteLock rw =
                new java.util.concurrent.locks.ReentrantReadWriteLock();
        rw.readLock().lock();
        check(rw.getReadLockCount() == 1 && rw.getReadHoldCount() == 1, "read hold is counted");
        rw.readLock().unlock();
        check(rw.getReadLockCount() == 0, "read unlock releases the count");
        rw.writeLock().lock();
        check(rw.isWriteLockedByCurrentThread() && rw.getWriteHoldCount() == 1, "write hold is counted");
        rw.writeLock().unlock();
        check(!rw.isWriteLocked(), "write unlock releases");
        java.util.concurrent.ScheduledExecutorService ex =
                java.util.concurrent.Executors.newScheduledThreadPool(1,
                        java.util.concurrent.Executors.defaultThreadFactory());
        check(ex != null, "Executors.newScheduledThreadPool(int, ThreadFactory)");
        ex.shutdown();
    }

    // Subclasses written by apps call the 3/4-argument super constructors; a missing one
    // is silently a no-op and leaves the object without a context.
    static void widgetBasics() throws Exception {
        InflaterTests.TestContext ctx = InflaterTests.context();
        check(new android.widget.ImageButton(ctx, null, 0).getContext() == ctx, "ImageButton 3-arg ctor reaches View");
        check(new android.widget.CheckBox(ctx, null, 0, 0).getContext() == ctx, "CheckBox 4-arg ctor reaches View");
        check(new android.widget.ProgressBar(ctx, null, 0).getContext() == ctx, "ProgressBar 3-arg ctor reaches View");
        check(new android.widget.ListView(ctx, null).getContext() == ctx, "ListView 2-arg ctor reaches View");

        android.widget.ImageView iv = new android.widget.ImageView(ctx);
        android.graphics.Matrix m = new android.graphics.Matrix();
        iv.setImageMatrix(m);
        check(iv.getImageMatrix() == m, "ImageView keeps the image matrix");

        android.view.ViewConfiguration vc = android.view.ViewConfiguration.get(ctx);
        check(vc != null && vc.getScaledTouchSlop() > 0 && vc.getScaledMaximumFlingVelocity() > vc.getScaledMinimumFlingVelocity(),
                "ViewConfiguration.get gives usable scaled values");
        check(android.view.ViewConfiguration.get(ctx) == vc, "ViewConfiguration is cached per density");

        android.view.VelocityTracker vt = android.view.VelocityTracker.obtain();
        check(vt != null, "VelocityTracker.obtain is non-null");
        vt.recycle();

        View v = new View(ctx);
        check(v.animate() != null && v.animate() == v.animate(), "View.animate returns one animator");
        android.view.ViewPropertyAnimator a = v.animate().alpha(0.25f).setDuration(0);
        check(a != null, "ViewPropertyAnimator chains");
        a.cancel();

        android.widget.CheckedTextView ct = new android.widget.CheckedTextView(ctx);
        ct.setChecked(true);
        check(ct.isChecked(), "CheckedTextView keeps its checked state");
    }

    // ---- SQLite

    static void sqlite() throws Exception {
        final String path = "/tmp/kudroid_sqlite_test.db";
        android.database.sqlite.SQLiteDatabase.deleteDatabase(new File(path));
        android.database.sqlite.SQLiteDatabase db;
        try {
            db = android.database.sqlite.SQLiteDatabase.openOrCreateDatabase(path, null);
        } catch (android.database.sqlite.SQLiteCantOpenDatabaseException e) {
            // A host built without libsqlite3 reports the database as unavailable.
            check(String.valueOf(e.getMessage()).contains("not available"), "SQLite unavailable is reported as such");
            return;
        }
        db.execSQL("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT, score REAL, blob BLOB)");
        android.content.ContentValues cv = new android.content.ContentValues();
        cv.put("name", "alpha");
        cv.put("score", 1.5);
        cv.put("blob", new byte[] {1, 2, 3});
        final long id = db.insert("t", null, cv);
        check(id == 1, "insert returns the new row id");
        db.execSQL("INSERT INTO t (name, score) VALUES (?, ?)", new Object[] {"beta", 2});
        android.database.Cursor c = db.rawQuery("SELECT id, name, score, blob FROM t ORDER BY id", null);
        check(c.getCount() == 2 && c.moveToFirst(), "rawQuery returns both rows");
        check(c.getLong(0) == 1 && "alpha".equals(c.getString(1)) && c.getDouble(2) == 1.5,
                "typed column reads");
        check(c.getBlob(3).length == 3 && c.getBlob(3)[2] == 3, "blob round-trips");
        check(c.getType(3) == android.database.Cursor.FIELD_TYPE_BLOB, "column type is reported");
        check(c.moveToNext() && c.isNull(3), "NULL column reads as null");
        c.close();

        check(db.update("t", cv, "id = ?", new String[] {"2"}) == 1, "update reports the changed rows");
        check(db.delete("t", "id = ?", new String[] {"2"}) == 1, "delete reports the removed rows");
        check(android.database.DatabaseUtils.queryNumEntries(db, "t") == 1, "one row is left");

        db.beginTransaction();
        db.execSQL("INSERT INTO t (name) VALUES ('gamma')");
        db.endTransaction();
        check(android.database.DatabaseUtils.queryNumEntries(db, "t") == 1, "an unmarked transaction rolls back");
        db.beginTransaction();
        db.execSQL("INSERT INTO t (name) VALUES ('delta')");
        db.setTransactionSuccessful();
        db.endTransaction();
        check(android.database.DatabaseUtils.queryNumEntries(db, "t") == 2, "a marked transaction commits");

        // Room reads through a factory that binds the arguments itself.
        android.database.Cursor viaFactory = db.rawQueryWithFactory(
                new android.database.sqlite.SQLiteDatabase.CursorFactory() {
                    public android.database.Cursor newCursor(android.database.sqlite.SQLiteDatabase d,
                            android.database.sqlite.SQLiteCursorDriver driver, String editTable,
                            android.database.sqlite.SQLiteQuery query) {
                        query.bindLong(1, 1);
                        return new android.database.sqlite.SQLiteCursor(driver, editTable, query);
                    }
                }, "SELECT name FROM t WHERE id = ?", new String[0], null);
        check(viaFactory.moveToFirst() && "alpha".equals(viaFactory.getString(0)),
                "rawQueryWithFactory runs with factory-bound arguments");
        viaFactory.close();

        android.database.sqlite.SQLiteStatement st = db.compileStatement("SELECT COUNT(*) FROM t");
        check(st.simpleQueryForLong() == 2, "SQLiteStatement.simpleQueryForLong");
        boolean constraint = false;
        try {
            db.execSQL("INSERT INTO t (id, name) VALUES (1, 'dup')");
        } catch (android.database.sqlite.SQLiteConstraintException e) {
            constraint = true;
        }
        check(constraint, "a primary-key clash is a SQLiteConstraintException");

        db.setVersion(7);
        check(db.getVersion() == 7, "user_version round-trips");
        db.close();
        check(!db.isOpen(), "close releases the database");

        final int[] calls = new int[3];
        android.database.sqlite.SQLiteDatabase.deleteDatabase(new File(path));
        android.database.sqlite.SQLiteOpenHelper helper = new android.database.sqlite.SQLiteOpenHelper(
                InflaterTests.context(), null, null, 3) {
            public void onCreate(android.database.sqlite.SQLiteDatabase d) { calls[0]++; d.execSQL("CREATE TABLE x (a)"); }
            public void onUpgrade(android.database.sqlite.SQLiteDatabase d, int o, int n) { calls[1]++; }
            public void onOpen(android.database.sqlite.SQLiteDatabase d) { calls[2]++; }
        };
        android.database.sqlite.SQLiteDatabase hd = helper.getWritableDatabase();
        check(calls[0] == 1, "open helper runs onCreate once (" + calls[0] + ")");
        check(calls[2] == 1, "open helper runs onOpen once (" + calls[2] + ")");
        check(hd.getVersion() == 3, "open helper stores the version (" + hd.getVersion() + ")");
        helper.close();
    }

    // ---- drawables

    static void drawables() throws Exception {
        android.graphics.drawable.ColorDrawable red = new android.graphics.drawable.ColorDrawable(0xFFFF0000);
        android.graphics.drawable.InsetDrawable inset = new android.graphics.drawable.InsetDrawable(red, 4, 6, 8, 10);
        inset.setBounds(0, 0, 100, 100);
        check(red.getBounds().left == 4 && red.getBounds().top == 6 && red.getBounds().right == 92
                && red.getBounds().bottom == 90, "InsetDrawable insets the child's bounds");
        android.graphics.Rect pad = new android.graphics.Rect();
        check(inset.getPadding(pad) && pad.left == 4 && pad.bottom == 10, "InsetDrawable reports its insets as padding");

        android.graphics.drawable.StateListDrawable list = new android.graphics.drawable.StateListDrawable();
        android.graphics.drawable.ColorDrawable pressed = new android.graphics.drawable.ColorDrawable(0xFF00FF00);
        android.graphics.drawable.ColorDrawable normal = new android.graphics.drawable.ColorDrawable(0xFF0000FF);
        list.addState(new int[] {com.android.internal.R.attr.state_pressed}, pressed);
        list.addState(new int[0], normal);
        list.setState(new int[0]);
        check(list.getCurrent() == normal, "StateListDrawable falls back to the wildcard item");
        list.setState(new int[] {com.android.internal.R.attr.state_pressed});
        check(list.getCurrent() == pressed, "StateListDrawable picks the matching item");

        android.graphics.drawable.GradientDrawable shape = new android.graphics.drawable.GradientDrawable();
        shape.setShape(android.graphics.drawable.GradientDrawable.OVAL);
        shape.setColor(0xFF123456);
        shape.setSize(24, 36);
        check(shape.getIntrinsicWidth() == 24 && shape.getIntrinsicHeight() == 36, "GradientDrawable size");
        android.graphics.drawable.ShapeDrawable sd = new android.graphics.drawable.ShapeDrawable(
                new android.graphics.drawable.shapes.RectShape());
        sd.setBounds(0, 0, 10, 10);
        check(sd.getShape().getWidth() == 10, "ShapeDrawable resizes its shape to the bounds");

        android.graphics.Canvas canvas = new android.graphics.Canvas(
                android.graphics.Bitmap.createBitmap(20, 20, android.graphics.Bitmap.Config.ARGB_8888));
        final int base = canvas.getSaveCount();
        canvas.save();
        canvas.translate(5, 5);
        canvas.clipRect(0, 0, 4, 4);
        android.graphics.Paint paint = new android.graphics.Paint();
        paint.setColor(0xFFFFFFFF);
        canvas.drawRect(0, 0, 10, 10, paint);
        canvas.restore();
        check(canvas.getSaveCount() == base, "save/restore keep the count balanced");
        android.graphics.Bitmap bm = canvas.getBitmap();
        check(bm.getPixel(6, 6) == 0xFFFFFFFF && bm.getPixel(12, 12) != 0xFFFFFFFF,
                "drawRect honours translate and the clip rectangle");
    }

    // ---- android.text.format

    static void textFormat() throws Exception {
        final long millis = 1700000000000L;
        check(android.text.format.DateFormat.format("yyyy", millis).toString().equals("2023"),
                "DateFormat.format formats the year");
        check(android.text.format.Formatter.formatFileSize(null, 1500).equals("1.50 kB"),
                "Formatter.formatFileSize scales by 1000");
        check(android.text.format.Formatter.formatFileSize(null, 512).equals("512 B"), "bytes stay bytes");
        check(android.text.format.DateUtils.formatElapsedTime(3725).equals("1:02:05"),
                "DateUtils.formatElapsedTime with hours");
    }

    // ---- Drawing

    static final class Frame extends android.widget.FrameLayout {
        Frame(android.content.Context c) { super(c); }
        @Override protected void onLayout(boolean changed, int l, int t, int r, int b) {}
    }

    static final class OverridingView extends View {
        int calls;
        OverridingView(android.content.Context c) { super(c); }
        @Override public void setBackground(android.graphics.drawable.Drawable d) { setBackgroundDrawable(d); }
        @Override public void setBackgroundDrawable(android.graphics.drawable.Drawable d) {
            calls++;
            super.setBackgroundDrawable(d);
        }
    }

    static void drawPipeline() throws Exception {
        InflaterTests.TestContext ctx = InflaterTests.context();
        Frame root = new Frame(ctx);
        root.setBackgroundColor(0xFFFF0000);
        Frame mid = new Frame(ctx);
        mid.setBackgroundColor(0xFF0000FF);
        View leaf = new View(ctx);
        leaf.setBackgroundColor(0xFF00FF00);
        mid.addView(leaf);
        root.addView(mid);
        root.layout(0, 0, 100, 100);
        mid.layout(10, 10, 60, 60);
        leaf.layout(5, 5, 15, 15);

        android.graphics.Bitmap bm = android.graphics.Bitmap.createBitmap(100, 100, android.graphics.Bitmap.Config.ARGB_8888);
        root.draw(new android.graphics.Canvas(bm));
        check(bm.getPixel(2, 2) == 0xFFFF0000, "root background paints the whole root");
        check(bm.getPixel(12, 12) == 0xFF0000FF, "child background is positioned by its frame");
        check(bm.getPixel(20, 20) == 0xFF00FF00, "grandchild is offset by every ancestor");
        check(bm.getPixel(80, 80) == 0xFFFF0000, "nothing paints outside the child frames");

        // Children are clipped to their own bounds.
        leaf.layout(5, 5, 200, 200);
        bm = android.graphics.Bitmap.createBitmap(100, 100, android.graphics.Bitmap.Config.ARGB_8888);
        root.draw(new android.graphics.Canvas(bm));
        check(bm.getPixel(80, 80) == 0xFFFF0000, "a child is clipped to its parent");
        leaf.layout(5, 5, 15, 15);

        // Scrolling moves content but not the background.
        mid.scrollTo(0, 5);
        bm = android.graphics.Bitmap.createBitmap(100, 100, android.graphics.Bitmap.Config.ARGB_8888);
        root.draw(new android.graphics.Canvas(bm));
        check(bm.getPixel(16, 12) == 0xFF00FF00, "scrolling shifts the content up");
        check(bm.getPixel(50, 50) == 0xFF0000FF, "the background stays in place while scrolling");
        mid.scrollTo(0, 0);

        // Alpha scales what the view and its content paint.
        leaf.setAlpha(0.5f);
        bm = android.graphics.Bitmap.createBitmap(100, 100, android.graphics.Bitmap.Config.ARGB_8888);
        root.draw(new android.graphics.Canvas(bm));
        int mixed = bm.getPixel(20, 20);
        int g = (mixed >> 8) & 0xFF, b = mixed & 0xFF;
        check(g > 110 && g < 145 && b > 110 && b < 145, "alpha 0.5 blends the child over its parent");
        leaf.setAlpha(0f);
        bm = android.graphics.Bitmap.createBitmap(100, 100, android.graphics.Bitmap.Config.ARGB_8888);
        root.draw(new android.graphics.Canvas(bm));
        check(bm.getPixel(20, 20) == 0xFF0000FF, "alpha 0 paints nothing");
        leaf.setAlpha(1f);

        // Z order: a higher elevation draws later.
        View low = new View(ctx);
        low.setBackgroundColor(0xFFFFFF00);
        View high = new View(ctx);
        high.setBackgroundColor(0xFF00FFFF);
        high.setElevation(4f);
        Frame stack = new Frame(ctx);
        stack.addView(high);
        stack.addView(low);
        stack.layout(0, 0, 40, 40);
        high.layout(0, 0, 40, 40);
        low.layout(0, 0, 40, 40);
        bm = android.graphics.Bitmap.createBitmap(40, 40, android.graphics.Bitmap.Config.ARGB_8888);
        stack.draw(new android.graphics.Canvas(bm));
        check(bm.getPixel(5, 5) == 0xFF00FFFF, "a higher elevation draws over its siblings");

        // setBackground/setBackgroundDrawable may be overridden to call each other.
        OverridingView ov = new OverridingView(ctx);
        android.graphics.drawable.ColorDrawable cd = new android.graphics.drawable.ColorDrawable(0xFF123456);
        ov.setBackground(cd);
        check(ov.calls == 1 && ov.getBackground() == cd, "setBackground reaches setBackgroundDrawable once");

        // Paths are filled, honouring winding and the canvas transform.
        android.graphics.Path triangle = new android.graphics.Path();
        triangle.moveTo(0, 0);
        triangle.lineTo(40, 0);
        triangle.lineTo(0, 40);
        triangle.close();
        android.graphics.Paint fill = new android.graphics.Paint();
        fill.setColor(0xFFAA5500);
        bm = android.graphics.Bitmap.createBitmap(100, 100, android.graphics.Bitmap.Config.ARGB_8888);
        android.graphics.Canvas pathCanvas = new android.graphics.Canvas(bm);
        pathCanvas.translate(10, 10);
        pathCanvas.drawPath(triangle, fill);
        check(bm.getPixel(20, 20) == 0xFFAA5500, "drawPath fills inside the path");
        check(bm.getPixel(10 + 35, 10 + 35) == 0, "drawPath leaves the outside alone");
        android.graphics.Path circle = new android.graphics.Path();
        circle.addCircle(50, 50, 20, android.graphics.Path.Direction.CW);
        bm = android.graphics.Bitmap.createBitmap(100, 100, android.graphics.Bitmap.Config.ARGB_8888);
        new android.graphics.Canvas(bm).drawPath(circle, fill);
        check(bm.getPixel(50, 50) == 0xFFAA5500 && bm.getPixel(50, 75) == 0 && bm.getPixel(68, 50) == 0xFFAA5500,
                "a circle path is filled as a disc");
        fill.setStyle(android.graphics.Paint.Style.STROKE);
        fill.setStrokeWidth(4f);
        check(fill.getStyle() == android.graphics.Paint.Style.STROKE, "Paint.getStyle returns the enum");
        bm = android.graphics.Bitmap.createBitmap(100, 100, android.graphics.Bitmap.Config.ARGB_8888);
        new android.graphics.Canvas(bm).drawPath(circle, fill);
        check(bm.getPixel(50, 50) == 0 && bm.getPixel(70, 50) == 0xFFAA5500, "a stroked circle paints the outline only");

        // An ImageView fills its content box with a drawable that has no intrinsic size.
        android.widget.ImageView iv = new android.widget.ImageView(ctx);
        iv.setImageDrawable(new android.graphics.drawable.ColorDrawable(0xFF336699));
        iv.measure(View.MeasureSpec.makeMeasureSpec(40, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(20, View.MeasureSpec.EXACTLY));
        iv.layout(0, 0, 40, 20);
        bm = android.graphics.Bitmap.createBitmap(40, 20, android.graphics.Bitmap.Config.ARGB_8888);
        iv.draw(new android.graphics.Canvas(bm));
        check(bm.getPixel(20, 10) == 0xFF336699, "ImageView draws its drawable");
    }

    static void textLayout() throws Exception {
        InflaterTests.TestContext ctx = InflaterTests.context();
        android.widget.TextView tv = new android.widget.TextView(ctx);
        tv.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX, 20f);
        tv.setText("alpha beta gamma delta epsilon zeta eta theta iota kappa");
        tv.setPadding(4, 2, 6, 2);
        tv.measure(View.MeasureSpec.makeMeasureSpec(120, View.MeasureSpec.AT_MOST),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
        check(tv.getMeasuredWidth() <= 120, "text measures within the width the parent allows");
        check(tv.getLineCount() > 1, "long text wraps");
        int wrappedHeight = tv.getMeasuredHeight();

        tv.setMaxLines(2);
        tv.setEllipsize(android.text.TextUtils.TruncateAt.END);
        tv.measure(View.MeasureSpec.makeMeasureSpec(120, View.MeasureSpec.AT_MOST),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
        check(tv.getLineCount() == 2, "maxLines caps the line count");
        check(tv.getMeasuredHeight() < wrappedHeight, "a capped TextView is shorter");

        android.widget.TextView one = new android.widget.TextView(ctx);
        one.setText("x");
        one.setPadding(10, 5, 10, 5);
        one.measure(View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
        check(one.getMeasuredWidth() > 20 && one.getMeasuredHeight() > 10, "padding is part of the measured size");
        check(one.getPaint() != null, "TextView.getPaint is available");
    }

    static void dialogAndFiles() throws Exception {
        InflaterTests.TestContext ctx = InflaterTests.context();
        android.app.Dialog d = new android.app.Dialog(ctx);
        check(d.getWindow() != null && d.getWindow().getCallback() == d, "a Dialog is its window's callback");
        d.setCancelable(false);
        d.show();
        check(d.isShowing(), "show makes the dialog showing");
        d.dismiss();
        check(!d.isShowing(), "dismiss ends it");

        android.app.AlertDialog.Builder builder = new android.app.AlertDialog.Builder(ctx);
        builder.setTitle("Test Title");
        builder.setMessage("Test Message");
        builder.setPositiveButton("OK", null);
        android.app.AlertDialog ad = builder.create();
        ad.show();
        check(ad.findViewById(android.R.id.message) != null, "AlertDialog message view is non-null");
        check(ad.getButton(android.content.DialogInterface.BUTTON_POSITIVE) != null, "AlertDialog positive button is non-null");
        check(ad.getButton(android.content.DialogInterface.BUTTON_NEGATIVE) != null, "AlertDialog negative button is non-null");
        ad.dismiss();

        android.app.ProgressDialog pd = new android.app.ProgressDialog(ctx);
        pd.setMessage("Loading");
        pd.show();
        check(pd.isShowing(), "ProgressDialog show works");
        pd.dismiss();

        File lockFile = new File("/tmp/kudroid_lock_test.lck");
        java.nio.channels.FileChannel ch = new FileOutputStream(lockFile).getChannel();
        java.nio.channels.FileLock lock = ch.lock();
        check(lock.isValid() && !lock.isShared(), "FileChannel.lock returns a valid exclusive lock");
        java.nio.channels.FileChannel other = new FileOutputStream(lockFile).getChannel();
        boolean overlap = false;
        try {
            other.tryLock();
        } catch (java.nio.channels.OverlappingFileLockException e) {
            overlap = true;
        }
        check(overlap, "an overlapping lock on the same file is refused");
        lock.release();
        check(!lock.isValid(), "release invalidates the lock");
        java.nio.channels.FileLock again = other.tryLock();
        check(again != null && again.isValid(), "the lock is available after release");
        other.close();
        ch.close();

        boolean threw = false;
        try {
            Runtime.getRuntime().exec(new String[] {"logcat", "-d"});
        } catch (java.io.IOException e) {
            threw = true;
        }
        check(threw, "Runtime.exec fails like a missing executable");
    }

    // ---- Framework gaps

    static void frameworkExtras() throws Exception {
        InflaterTests.TestContext ctx = InflaterTests.context();

        final int[] changed = new int[1];
        android.database.DataSetObserver observer = new android.database.DataSetObserver() {
            @Override public void onChanged() { changed[0]++; }
        };
        android.database.DataSetObservable observable = new android.database.DataSetObservable();
        observable.registerObserver(observer);
        observable.notifyChanged();
        check(changed[0] == 1, "DataSetObservable dispatches through the erased observer slot");
        observable.unregisterObserver(observer);
        observable.unregisterAll();

        android.graphics.Bitmap bitmap = android.graphics.Bitmap.createBitmap(3, 4,
                android.graphics.Bitmap.Config.ARGB_8888);
        android.text.style.ImageSpan image = new android.text.style.ImageSpan(ctx, bitmap);
        check(image.getDrawable() != null, "ImageSpan builds a drawable from a bitmap");
        check(image.getSize(new android.graphics.Paint(), "x", 0, 1, null) == 3,
                "ImageSpan reports the drawable width");
        android.graphics.drawable.Drawable sourceDrawable = new android.graphics.drawable.ColorDrawable(0);
        android.text.style.ImageSpan sourced = new android.text.style.ImageSpan(sourceDrawable, "src");
        check("src".equals(sourced.getSource()), "ImageSpan keeps its source string");

        android.widget.AutoCompleteTextView auto = new android.widget.AutoCompleteTextView(ctx);
        android.widget.AdapterView.OnItemClickListener click = (parent, view, position, id) -> {};
        android.widget.AdapterView.OnItemSelectedListener select = new android.widget.AdapterView.OnItemSelectedListener() {
            public void onItemSelected(android.widget.AdapterView<?> parent, View view, int position, long id) {}
            public void onNothingSelected(android.widget.AdapterView<?> parent) {}
        };
        auto.setOnItemClickListener(click);
        auto.setOnItemSelectedListener(select);
        check(auto.getOnItemClickListener() == click, "AutoCompleteTextView keeps its item-click listener");
        check(auto.getItemSelectedListener() == select, "AutoCompleteTextView keeps its item-selected listener");

        android.widget.EdgeEffect edge = new android.widget.EdgeEffect(ctx);
        check(edge.isFinished(), "EdgeEffect(Context) starts finished");
        edge.onPull(0.5f);
        check(edge.getDistance() == 0.5f, "EdgeEffect records a pull");

        // Class.getClasses and getDeclaredClasses
        check(FrameworkApiTests.class.getClasses() != null && FrameworkApiTests.class.getClasses().length == 0,
                "getClasses returns non-null array");
        check(FrameworkApiTests.class.getDeclaredClasses() != null && FrameworkApiTests.class.getDeclaredClasses().length == 0,
                "getDeclaredClasses returns non-null array");

        // Scanner with InputStream and horizon searching
        byte[] xmlData = "<app><id>test.app</id><version>1.2.3</version></app>".getBytes("UTF-8");
        java.util.Scanner xmlScanner = new java.util.Scanner(new java.io.ByteArrayInputStream(xmlData));
        String foundVersion = xmlScanner.findWithinHorizon("<version>(.*?)</version>", 0);
        check("<version>1.2.3</version>".equals(foundVersion), "Scanner findWithinHorizon matches tag");
        xmlScanner.close();

        // Scanner tokenization and numbers
        java.util.Scanner numScanner = new java.util.Scanner("42 3.14 true test\nsecond line");
        check(numScanner.nextInt() == 42, "Scanner nextInt");
        check(Math.abs(numScanner.nextDouble() - 3.14) < 0.001, "Scanner nextDouble");
        check(numScanner.nextBoolean(), "Scanner nextBoolean");
        check("test".equals(numScanner.next()), "Scanner next");
        check("second line".equals(numScanner.nextLine()), "Scanner nextLine");
        numScanner.close();

        // createPackageContext
        android.content.Context selfCtx = ctx.createPackageContext(ctx.getPackageName(), 0);
        check(selfCtx != null, "createPackageContext for self succeeds");
        boolean threw = false;
        try {
            ctx.createPackageContext("nonexistent.pkg.xyz", 0);
        } catch (android.content.pm.PackageManager.NameNotFoundException e) {
            threw = true;
        }
        check(threw, "createPackageContext for unknown throws NameNotFoundException");

        // ScaleGestureDetector
        android.view.ScaleGestureDetector.OnScaleGestureListener scaleListener =
                new android.view.ScaleGestureDetector.SimpleOnScaleGestureListener();
        android.view.ScaleGestureDetector detector =
                new android.view.ScaleGestureDetector(ctx, scaleListener);
        check(detector != null && !detector.isInProgress(), "ScaleGestureDetector initialized");

        // Arrays.sort and binarySearch on Objects
        String[] words = new String[] { "banana", "apple", "cherry", "date" };
        java.util.Arrays.sort(words);
        check("apple".equals(words[0]) && "cherry".equals(words[2]), "Arrays.sort(Object[]) orders elements");
        check(java.util.Arrays.binarySearch(words, "cherry") == 2, "Arrays.binarySearch found cherry");
        check(java.util.Arrays.binarySearch(words, "blueberry") < 0, "Arrays.binarySearch not found");

        // Component meta-data typed storage and Bundle coercion
        String[] metaKeys = new String[] { "boolVal", "intVal", "floatVal", "strVal" };
        String[] metaVals = new String[] { "false", "123", "45.6", "hello" };
        android.content.pm.SystemPackageManager.registerComponentMetaData("testComp", metaKeys, metaVals);
        android.os.Bundle metaBundle = ((java.util.HashMap<String, android.os.Bundle>)
                android.content.pm.SystemPackageManager.class.getDeclaredField("sComponentMetaData").get(null)).get("testComp");
        check(Boolean.FALSE.equals(metaBundle.get("boolVal")), "metaData boolVal is Boolean");
        check(!((Boolean) metaBundle.get("boolVal")), "metaData cast to Boolean succeeds");
        check(Integer.valueOf(123).equals(metaBundle.get("intVal")), "metaData intVal is Integer");
        check("hello".equals(metaBundle.get("strVal")), "metaData strVal is String");
        check(!metaBundle.getBoolean("boolVal", true), "metaBundle.getBoolean");
        check(metaBundle.getInt("intVal") == 123, "metaBundle.getInt");

        // KeyguardManager & KeyCharacterMap
        android.app.KeyguardManager km = new android.app.KeyguardManager();
        check(!km.inKeyguardRestrictedInputMode(), "KeyguardManager.inKeyguardRestrictedInputMode is false");
        check(!android.view.KeyCharacterMap.deviceHasKey(4), "deviceHasKey is false");
        boolean[] hasKeys = android.view.KeyCharacterMap.deviceHasKeys(new int[] { 4, 82 });
        check(hasKeys != null && hasKeys.length == 2 && !hasKeys[0] && !hasKeys[1], "deviceHasKeys returns array of false");

        // ParcelFileDescriptor & ContentResolver
        File tmpFile = File.createTempFile("kudroid_cr_test", ".txt");
        FileOutputStream fos = new FileOutputStream(tmpFile);
        fos.write("hello kudroid content".getBytes("UTF-8"));
        fos.close();

        android.os.ParcelFileDescriptor pfd = android.os.ParcelFileDescriptor.open(
                tmpFile, android.os.ParcelFileDescriptor.MODE_READ_ONLY);
        check(pfd != null && pfd.getFd() >= 0, "ParcelFileDescriptor.open succeeds");
        pfd.close();

        android.content.ContentResolver resolver = android.content.ContentResolver.getInstance();
        android.net.Uri fileUri = android.net.Uri.fromFile(tmpFile);
        android.content.res.AssetFileDescriptor afd = resolver.openAssetFileDescriptor(fileUri, "r");
        check(afd != null && afd.getLength() == tmpFile.length(), "ContentResolver.openAssetFileDescriptor succeeds");
        java.io.InputStream inStream = afd.createInputStream();
        byte[] readBuf = new byte[32];
        int readBytes = inStream.read(readBuf);
        check(readBytes > 0 && "hello kudroid content".equals(new String(readBuf, 0, readBytes, "UTF-8")),
                "AssetFileDescriptor createInputStream reads content");
        inStream.close();
        afd.close();

        java.io.InputStream directIn = resolver.openInputStream(fileUri);
        check(directIn != null, "ContentResolver.openInputStream succeeds");
        directIn.close();
        tmpFile.delete();
    }

    // ---- Layout

    static void layouts() throws Exception {
        InflaterTests.TestContext ctx = InflaterTests.context();
        final int match = ViewGroup.LayoutParams.MATCH_PARENT;
        final int wrap = ViewGroup.LayoutParams.WRAP_CONTENT;
        final int exactly = View.MeasureSpec.EXACTLY;

        // Vertical: fixed heights, one weighted child, margins and an explicit width.
        android.widget.LinearLayout col = new android.widget.LinearLayout(ctx);
        col.setOrientation(android.widget.LinearLayout.VERTICAL);
        View a = new View(ctx), b = new View(ctx), c = new View(ctx);
        col.addView(a, new android.widget.LinearLayout.LayoutParams(match, 40));
        android.widget.LinearLayout.LayoutParams bp = new android.widget.LinearLayout.LayoutParams(match, 0, 1f);
        bp.topMargin = 10;
        col.addView(b, bp);
        col.addView(c, new android.widget.LinearLayout.LayoutParams(50, 30));
        col.measure(View.MeasureSpec.makeMeasureSpec(200, exactly), View.MeasureSpec.makeMeasureSpec(300, exactly));
        col.layout(0, 0, 200, 300);
        check(a.getTop() == 0 && a.getBottom() == 40 && a.getWidth() == 200, "match_parent width and a fixed height are honoured");
        check(b.getTop() == 50 && b.getBottom() == 270, "a weighted child takes the leftover space after margins");
        check(c.getTop() == 270 && c.getHeight() == 30 && c.getWidth() == 50, "an exact width is not stretched");

        // Horizontal weights split the width by weight.
        android.widget.LinearLayout row = new android.widget.LinearLayout(ctx);
        row.setOrientation(android.widget.LinearLayout.HORIZONTAL);
        View w1 = new View(ctx), w2 = new View(ctx);
        row.addView(w1, new android.widget.LinearLayout.LayoutParams(0, match, 1f));
        row.addView(w2, new android.widget.LinearLayout.LayoutParams(0, match, 2f));
        row.measure(View.MeasureSpec.makeMeasureSpec(300, exactly), View.MeasureSpec.makeMeasureSpec(50, exactly));
        row.layout(0, 0, 300, 50);
        check(w1.getWidth() == 100 && w2.getWidth() == 200 && w2.getLeft() == 100, "weights 1:2 split 300 into 100 and 200");

        // wrap_content sums the children and the padding.
        android.widget.LinearLayout wrapCol = new android.widget.LinearLayout(ctx);
        wrapCol.setOrientation(android.widget.LinearLayout.VERTICAL);
        wrapCol.setPadding(5, 5, 5, 5);
        wrapCol.addView(new View(ctx), new android.widget.LinearLayout.LayoutParams(60, 40));
        wrapCol.addView(new View(ctx), new android.widget.LinearLayout.LayoutParams(80, 30));
        wrapCol.measure(View.MeasureSpec.makeMeasureSpec(500, View.MeasureSpec.AT_MOST),
                View.MeasureSpec.makeMeasureSpec(500, View.MeasureSpec.AT_MOST));
        check(wrapCol.getMeasuredHeight() == 80 && wrapCol.getMeasuredWidth() == 90,
                "wrap_content adds up the children and padding");

        // Gravity places the whole stack.
        android.widget.LinearLayout bottom = new android.widget.LinearLayout(ctx);
        bottom.setOrientation(android.widget.LinearLayout.VERTICAL);
        bottom.setGravity(android.view.Gravity.BOTTOM | android.view.Gravity.RIGHT);
        View g = new View(ctx);
        bottom.addView(g, new android.widget.LinearLayout.LayoutParams(20, 10));
        bottom.measure(View.MeasureSpec.makeMeasureSpec(100, exactly), View.MeasureSpec.makeMeasureSpec(100, exactly));
        bottom.layout(0, 0, 100, 100);
        check(g.getLeft() == 80 && g.getTop() == 90, "gravity bottom|right places the stack in the corner");

        // FrameLayout: margins, gravity and match_parent.
        android.widget.FrameLayout frame = new android.widget.FrameLayout(ctx);
        frame.setPadding(10, 10, 10, 10);
        View centered = new View(ctx), filler = new View(ctx);
        android.widget.FrameLayout.LayoutParams cp = new android.widget.FrameLayout.LayoutParams(40, 20,
                android.view.Gravity.CENTER);
        frame.addView(filler, new android.widget.FrameLayout.LayoutParams(match, match));
        frame.addView(centered, cp);
        frame.measure(View.MeasureSpec.makeMeasureSpec(200, exactly), View.MeasureSpec.makeMeasureSpec(100, exactly));
        frame.layout(0, 0, 200, 100);
        check(filler.getLeft() == 10 && filler.getTop() == 10 && filler.getWidth() == 180 && filler.getHeight() == 80,
                "match_parent fills the frame inside its padding");
        check(centered.getLeft() == 80 && centered.getTop() == 40, "FrameLayout centres a child by gravity");

        android.widget.FrameLayout wrapFrame = new android.widget.FrameLayout(ctx);
        wrapFrame.addView(new View(ctx), new android.widget.FrameLayout.LayoutParams(30, 20));
        wrapFrame.measure(View.MeasureSpec.makeMeasureSpec(500, View.MeasureSpec.AT_MOST),
                View.MeasureSpec.makeMeasureSpec(500, View.MeasureSpec.AT_MOST));
        check(wrapFrame.getMeasuredWidth() == 30 && wrapFrame.getMeasuredHeight() == 20, "a wrap_content FrameLayout wraps its child");

        // RelativeLayout resolves rules in dependency order, whatever the order of the children.
        android.widget.RelativeLayout rel = new android.widget.RelativeLayout(ctx);
        View body = new View(ctx);
        body.setId(102);
        android.widget.RelativeLayout.LayoutParams bodyParams = new android.widget.RelativeLayout.LayoutParams(match, match);
        bodyParams.addRule(android.widget.RelativeLayout.BELOW, 101);
        rel.addView(body, bodyParams);
        View header = new View(ctx);
        header.setId(101);
        android.widget.RelativeLayout.LayoutParams headerParams = new android.widget.RelativeLayout.LayoutParams(match, 40);
        headerParams.addRule(android.widget.RelativeLayout.ALIGN_PARENT_TOP);
        rel.addView(header, headerParams);
        View corner = new View(ctx);
        android.widget.RelativeLayout.LayoutParams cornerParams = new android.widget.RelativeLayout.LayoutParams(20, 20);
        cornerParams.addRule(android.widget.RelativeLayout.ALIGN_PARENT_RIGHT);
        cornerParams.addRule(android.widget.RelativeLayout.ALIGN_PARENT_BOTTOM);
        rel.addView(corner, cornerParams);
        View middle = new View(ctx);
        android.widget.RelativeLayout.LayoutParams middleParams = new android.widget.RelativeLayout.LayoutParams(10, 10);
        middleParams.addRule(android.widget.RelativeLayout.CENTER_IN_PARENT);
        rel.addView(middle, middleParams);
        rel.measure(View.MeasureSpec.makeMeasureSpec(200, exactly), View.MeasureSpec.makeMeasureSpec(100, exactly));
        rel.layout(0, 0, 200, 100);
        check(header.getTop() == 0 && header.getBottom() == 40 && header.getWidth() == 200, "alignParentTop with match_parent width");
        check(body.getTop() == 40 && body.getBottom() == 100 && body.getWidth() == 200, "below fills the rest, even when listed first");
        check(corner.getLeft() == 180 && corner.getTop() == 80, "alignParentRight/Bottom pins the corner");
        check(middle.getLeft() == 95 && middle.getTop() == 45, "centerInParent centres");

        // ScrollView measures its child unbounded and clamps the offset.
        android.widget.ScrollView scroll = new android.widget.ScrollView(ctx);
        View tall = new View(ctx);
        tall.setMinimumHeight(500);
        scroll.addView(tall, new android.widget.FrameLayout.LayoutParams(match, wrap));
        scroll.measure(View.MeasureSpec.makeMeasureSpec(100, exactly), View.MeasureSpec.makeMeasureSpec(200, exactly));
        scroll.layout(0, 0, 100, 200);
        check(tall.getHeight() == 500 && scroll.getHeight() == 200, "the scroll child keeps its own height");
        scroll.scrollTo(0, 1000);
        check(scroll.getScrollY() == 300, "scrolling stops at the end of the content");
        scroll.scrollTo(0, -20);
        check(scroll.getScrollY() == 0, "scrolling stops at the start");
    }
}
