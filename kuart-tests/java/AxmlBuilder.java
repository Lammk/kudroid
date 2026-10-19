import java.io.ByteArrayOutputStream;
import java.util.ArrayList;

/**
 * Builds compiled binary XML (the AXML chunk format) for tests, in the layout aapt2 emits:
 * a string pool whose first strings are the attribute names that carry resource ids, then
 * the resource-id map, then namespace and element chunks.
 */
public final class AxmlBuilder {
    static final String ANDROID_NS = "http://schemas.android.com/apk/res/android";
    static final int TYPE_REFERENCE = 0x01;
    static final int TYPE_ATTRIBUTE = 0x02;
    static final int TYPE_STRING = 0x03;
    static final int TYPE_FLOAT = 0x04;
    static final int TYPE_DIMENSION = 0x05;
    static final int TYPE_INT_DEC = 0x10;
    static final int TYPE_INT_BOOLEAN = 0x12;
    static final int TYPE_INT_COLOR_ARGB8 = 0x1c;

    private static final class Attr {
        String ns;      // null: no namespace
        String name;
        int resId;      // 0: the name has no resource id
        int type;
        int data;
        String text;    // TYPE_STRING value
    }

    private static final class Element {
        String name;
        boolean end;
        ArrayList<Attr> attrs = new ArrayList<Attr>();
    }

    private final ArrayList<Element> elements = new ArrayList<Element>();

    /** Open an element; attributes are added to the most recently opened one. */
    public AxmlBuilder start(String name) {
        Element e = new Element();
        e.name = name;
        elements.add(e);
        return this;
    }

    public AxmlBuilder end() {
        Element e = new Element();
        e.end = true;
        elements.add(e);
        return this;
    }

    private Element open() {
        for (int i = elements.size() - 1; i >= 0; i--) {
            if (!elements.get(i).end) return elements.get(i);
        }
        throw new IllegalStateException("no open element");
    }

    private AxmlBuilder add(String ns, String name, int resId, int type, int data, String text) {
        Attr a = new Attr();
        a.ns = ns; a.name = name; a.resId = resId; a.type = type; a.data = data; a.text = text;
        // The element being built is the last START that has not been closed by end().
        Element target = null;
        int depth = 0;
        for (int i = elements.size() - 1; i >= 0; i--) {
            Element e = elements.get(i);
            if (e.end) depth++;
            else if (depth == 0) { target = e; break; }
            else depth--;
        }
        if (target == null) throw new IllegalStateException("no open element");
        target.attrs.add(a);
        return this;
    }

    /** An android:-namespaced attribute with a typed value. */
    public AxmlBuilder attr(int resId, String name, int type, int data) {
        return add(ANDROID_NS, name, resId, type, data, null);
    }

    public AxmlBuilder attrString(int resId, String name, String value) {
        return add(ANDROID_NS, name, resId, TYPE_STRING, 0, value);
    }

    /** An attribute with no namespace, such as class, style or layout. */
    public AxmlBuilder plain(String name, int type, int data) {
        return add(null, name, 0, type, data, null);
    }

    public AxmlBuilder plainString(String name, String value) {
        return add(null, name, 0, TYPE_STRING, 0, value);
    }

    // ---- serialisation

    private final ArrayList<String> pool = new ArrayList<String>();

    private int intern(String s) {
        if (s == null) return -1;
        int i = pool.indexOf(s);
        if (i >= 0) return i;
        pool.add(s);
        return pool.size() - 1;
    }

    private static void u16(ByteArrayOutputStream out, int v) {
        out.write(v & 255);
        out.write((v >> 8) & 255);
    }

    private static void u32(ByteArrayOutputStream out, int v) {
        u16(out, v & 0xffff);
        u16(out, (v >>> 16) & 0xffff);
    }

    public byte[] build() {
        pool.clear();
        // Attribute names that have resource ids come first in the pool, in id order of
        // first appearance; their ids form the resource map.
        ArrayList<Integer> ids = new ArrayList<Integer>();
        for (Element e : elements) {
            for (Attr a : e.attrs) {
                if (a.resId != 0 && pool.indexOf(a.name) < 0) {
                    pool.add(a.name);
                    ids.add(a.resId);
                }
            }
        }
        int nsPrefix = intern("android");
        int nsUri = intern(ANDROID_NS);

        ByteArrayOutputStream body = new ByteArrayOutputStream();
        // namespace start
        u16(body, 0x100); u16(body, 16); u32(body, 24); u32(body, 1); u32(body, -1);
        u32(body, nsPrefix); u32(body, nsUri);
        ArrayList<String> open = new ArrayList<String>();
        int line = 2;
        for (Element e : elements) {
            if (e.end) {
                String name = open.remove(open.size() - 1);
                u16(body, 0x103); u16(body, 16); u32(body, 24); u32(body, line++); u32(body, -1);
                u32(body, -1); u32(body, intern(name));
            } else {
                open.add(e.name);
                int count = e.attrs.size();
                u16(body, 0x102); u16(body, 16); u32(body, 36 + 20 * count); u32(body, line++); u32(body, -1);
                u32(body, -1); u32(body, intern(e.name));
                u16(body, 20); u16(body, 20); u16(body, count); u16(body, 0); u16(body, 0); u16(body, 0);
                for (Attr a : e.attrs) {
                    u32(body, a.ns == null ? -1 : intern(a.ns));
                    u32(body, intern(a.name));
                    int data = a.data;
                    if (a.type == TYPE_STRING) data = intern(a.text);
                    u32(body, a.type == TYPE_STRING ? intern(a.text) : -1);
                    u16(body, 8); body.write(0); body.write(a.type);
                    u32(body, data);
                }
            }
        }
        // namespace end
        u16(body, 0x101); u16(body, 16); u32(body, 24); u32(body, line); u32(body, -1);
        u32(body, nsPrefix); u32(body, nsUri);

        // String pool (UTF-16), built after interning is complete.
        ByteArrayOutputStream strings = new ByteArrayOutputStream();
        int[] offsets = new int[pool.size()];
        for (int i = 0; i < pool.size(); i++) {
            offsets[i] = strings.size();
            String s = pool.get(i);
            u16(strings, s.length());
            for (int j = 0; j < s.length(); j++) u16(strings, s.charAt(j));
            u16(strings, 0);
        }
        while ((strings.size() & 3) != 0) strings.write(0);
        int stringsStart = 28 + 4 * pool.size();
        ByteArrayOutputStream poolChunk = new ByteArrayOutputStream();
        u16(poolChunk, 0x001); u16(poolChunk, 28); u32(poolChunk, stringsStart + strings.size());
        u32(poolChunk, pool.size()); u32(poolChunk, 0); u32(poolChunk, 0);
        u32(poolChunk, stringsStart); u32(poolChunk, 0);
        for (int offset : offsets) u32(poolChunk, offset);
        byte[] stringBytes = strings.toByteArray();
        poolChunk.write(stringBytes, 0, stringBytes.length);

        ByteArrayOutputStream map = new ByteArrayOutputStream();
        u16(map, 0x180); u16(map, 8); u32(map, 8 + 4 * ids.size());
        for (int id : ids) u32(map, id);

        byte[] poolBytes = poolChunk.toByteArray();
        byte[] mapBytes = map.toByteArray();
        byte[] bodyBytes = body.toByteArray();
        int total = 8 + poolBytes.length + mapBytes.length + bodyBytes.length;
        ByteArrayOutputStream file = new ByteArrayOutputStream();
        u16(file, 0x003); u16(file, 8); u32(file, total);
        file.write(poolBytes, 0, poolBytes.length);
        file.write(mapBytes, 0, mapBytes.length);
        file.write(bodyBytes, 0, bodyBytes.length);
        return file.toByteArray();
    }
}
