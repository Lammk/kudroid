package android.content.res;

import android.util.AttributeSet;
import android.util.TypedValue;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;
import java.io.InputStream;
import java.io.Reader;
import java.io.IOException;
import java.util.ArrayList;

/** Pull parser for compiled XML resource chunks. */
public final class CompiledXmlResourceParser implements XmlResourceParser {
    private static final String ANDROID = "http://schemas.android.com/apk/res/android";
    private static final class Attribute {
        String namespace, name, raw;
        int id, type, data;
    }
    private static final class Event {
        int type, depth, line;
        String name, namespace, text;
        Attribute[] attrs = new Attribute[0];
    }
    private static final class Namespace {
        int depth;
        String prefix, uri;
    }
    private final ArrayList<Event> events = new ArrayList<>();
    private final ArrayList<Namespace> namespaces = new ArrayList<>();
    private String[] strings = new String[0];
    private int[] resourceIds = new int[0];
    private int position;
    private boolean closed;

    public CompiledXmlResourceParser() {
        Event start = new Event(); start.type = START_DOCUMENT; events.add(start);
        Event end = new Event(); end.type = END_DOCUMENT; events.add(end);
    }
    public CompiledXmlResourceParser(byte[] bytes) throws XmlPullParserException {
        this();
        try { parse(bytes); }
        catch (IndexOutOfBoundsException e) { throw error("Truncated compiled XML"); }
    }
    private XmlPullParserException error(String message) { return new XmlPullParserException(message); }
    private static int u16(byte[] b, int p) {
        if (p < 0 || p > b.length - 2) throw new IndexOutOfBoundsException();
        return (b[p] & 255) | ((b[p + 1] & 255) << 8);
    }
    private static int i32(byte[] b, int p) {
        return u16(b, p) | (u16(b, p + 2) << 16);
    }
    private String string(int index) throws XmlPullParserException {
        if (index == -1) return "";
        if (index < 0 || index >= strings.length) throw error("Invalid XML string index");
        return strings[index];
    }
    private int length(byte[] b, int[] p, int end, boolean utf8) throws XmlPullParserException {
        int size = utf8 ? 1 : 2;
        if (p[0] > end - size) throw error("Truncated XML string length");
        int n = utf8 ? b[p[0]] & 255 : u16(b, p[0]); p[0] += size;
        int flag = utf8 ? 128 : 32768;
        if ((n & flag) != 0) {
            if (p[0] > end - size) throw error("Truncated XML string length");
            n = ((n & (flag - 1)) << (utf8 ? 8 : 16)) |
                    (utf8 ? b[p[0]] & 255 : u16(b, p[0]));
            p[0] += size;
        }
        return n;
    }
    private void pool(byte[] b, int at, int header, int size) throws XmlPullParserException {
        if (header < 28) throw error("Invalid XML string pool header");
        int count = i32(b, at + 8), styles = i32(b, at + 12);
        int start = i32(b, at + 20), styleStart = i32(b, at + 24);
        if (count < 0 || styles < 0 || (long) header + ((long) count + styles) * 4 > size ||
                start < header + ((long) count + styles) * 4 || start > size ||
                (styleStart != 0 && (styleStart < start || styleStart > size)))
            throw error("Invalid XML string pool bounds");
        boolean utf8 = (i32(b, at + 16) & 256) != 0;
        int end = at + (styleStart == 0 ? size : styleStart);
        strings = new String[count];
        for (int i = 0; i < count; i++) {
            int offset = i32(b, at + header + i * 4);
            if (offset < 0 || offset >= end - at - start) throw error("Invalid XML string offset");
            int[] p = {at + start + offset};
            int n = length(b, p, end, utf8);
            if (utf8) n = length(b, p, end, true);
            long bytes = utf8 ? n : (long) n * 2;
            int terminator = utf8 ? 1 : 2;
            if (n < 0 || bytes > end - p[0] - terminator) throw error("Invalid XML string size");
            if ((utf8 ? b[p[0] + (int) bytes] : u16(b, p[0] + (int) bytes)) != 0)
                throw error("Unterminated XML string");
            if (utf8) strings[i] = new String(b, p[0], (int) bytes);
            else {
                char[] chars = new char[n];
                for (int j = 0; j < n; j++) chars[j] = (char) u16(b, p[0] + j * 2);
                strings[i] = new String(chars);
            }
        }
    }
    private void parse(byte[] b) throws XmlPullParserException {
        if (b == null || b.length < 8 || u16(b, 0) != 3 || u16(b, 2) < 8 || i32(b, 4) != b.length)
            throw error("Expected compiled XML resource");
        events.remove(events.size() - 1);
        ArrayList<Event> stack = new ArrayList<>();
        ArrayList<Namespace> active = new ArrayList<>();
        boolean rootSeen = false, poolSeen = false;
        for (int at = u16(b, 2); at < b.length;) {
            if (at > b.length - 8) throw error("Truncated XML chunk");
            int type = u16(b, at), header = u16(b, at + 2), size = i32(b, at + 4);
            if (header < 8 || size < header || size > b.length - at) throw error("Invalid XML chunk size");
            if (type == 1) {
                if (poolSeen) throw error("Duplicate XML string pool");
                pool(b, at, header, size); poolSeen = true;
            } else if (type == 0x180) {
                if ((size - header) % 4 != 0) throw error("Invalid XML resource map");
                resourceIds = new int[(size - header) / 4];
                for (int i = 0; i < resourceIds.length; i++) resourceIds[i] = i32(b, at + header + i * 4);
            } else if (type >= 0x100 && type <= 0x104) {
                if (!poolSeen || header < 16) throw error("Invalid XML node header");
                int ext = at + header;
                if (type == 0x100 || type == 0x101) {
                    if (size - header < 8) throw error("Invalid namespace chunk");
                    String prefix = string(i32(b, ext)), uri = string(i32(b, ext + 4));
                    if (type == 0x100) {
                        if (active.size() >= 256) throw error("XML namespace limit exceeded");
                        Namespace ns = new Namespace(); ns.depth = stack.size() + 1;
                        ns.prefix = prefix; ns.uri = uri; active.add(ns);
                    } else {
                        if (active.isEmpty()) throw error("Unbalanced XML namespace");
                        Namespace ns = active.remove(active.size() - 1);
                        if (!ns.prefix.equals(prefix) || !ns.uri.equals(uri)) throw error("Mismatched XML namespace");
                    }
                } else {
                    Event e = new Event(); e.line = i32(b, at + 8);
                    if (type == 0x104) {
                        if (size - header < 12 || stack.isEmpty()) throw error("Invalid XML text chunk");
                        e.type = TEXT; e.text = string(i32(b, ext)); e.depth = stack.size();
                    } else {
                        if (size - header < 8) throw error("Invalid XML element");
                        e.namespace = string(i32(b, ext)); e.name = string(i32(b, ext + 4));
                        if (type == 0x102) {
                            if (size - header < 20 || (stack.isEmpty() && rootSeen)) throw error("Invalid XML root");
                            int start = u16(b, ext + 8), stride = u16(b, ext + 10), count = u16(b, ext + 12);
                            if (start < 20 || stride < 20 || (long) header + start + (long) stride * count > size)
                                throw error("Invalid XML attribute bounds");
                            e.type = START_TAG; e.depth = stack.size() + 1; e.attrs = new Attribute[count];
                            for (int i = 0; i < count; i++) {
                                int p = ext + start + stride * i, name = i32(b, p + 4);
                                if (u16(b, p + 12) < 8 || u16(b, p + 12) > stride - 12)
                                    throw error("Invalid XML typed value");
                                Attribute a = new Attribute(); a.namespace = string(i32(b, p)); a.name = string(name);
                                a.id = name >= 0 && name < resourceIds.length ? resourceIds[name] : 0;
                                int raw = i32(b, p + 8); a.raw = raw == -1 ? null : string(raw);
                                a.type = b[p + 15] & 255; a.data = i32(b, p + 16);
                                if (a.type == 3) string(a.data);
                                e.attrs[i] = a;
                            }
                            if (stack.size() >= 256) throw error("XML nesting limit exceeded");
                            stack.add(e); rootSeen = true;
                        } else {
                            if (stack.isEmpty()) throw error("Unbalanced XML element");
                            Event open = stack.remove(stack.size() - 1);
                            if (!e.name.equals(open.name) || !e.namespace.equals(open.namespace)) throw error("Mismatched XML element");
                            e.type = END_TAG; e.depth = stack.size() + 1;
                        }
                    }
                    events.add(e);
                    // Snapshot active namespaces for this event.
                    for (Namespace ns : active) {
                        Namespace copy = new Namespace(); copy.depth = ns.depth;
                        copy.prefix = ns.prefix; copy.uri = ns.uri;
                        namespaces.add(copy);
                    }
                    namespaceOffsets.add(namespaces.size());
                }
            }
            at += size;
        }
        if (!rootSeen || !stack.isEmpty() || !active.isEmpty()) throw error("Incomplete compiled XML");
        Event end = new Event(); end.type = END_DOCUMENT; events.add(end);
    }
    private final ArrayList<Integer> namespaceOffsets = new ArrayList<>();
    private Event current() { return events.get(position); }
    private Attribute attribute(int index) {
        if (current().type != START_TAG || index < 0 || index >= current().attrs.length)
            throw new IndexOutOfBoundsException();
        return current().attrs[index];
    }
    private int index(String ns, String name) {
        if (current().type != START_TAG) return -1;
        for (int i = 0; i < current().attrs.length; i++) {
            Attribute a = current().attrs[i];
            // A null namespace means "no namespace" (an unprefixed attribute), not "any".
            if (a.name.equals(name) && a.namespace.equals(ns == null ? "" : ns)) return i;
        }
        return -1;
    }
    /** Index of the attribute whose name resolves to resource id `resId`, or -1. */
    public int indexOfAttributeResource(int resId) {
        if (resId == 0 || current().type != START_TAG) return -1;
        Attribute[] attrs = current().attrs;
        for (int i = 0; i < attrs.length; i++) {
            if (attrs[i].id == resId) return i;
        }
        return -1;
    }
    public TypedValue getAttributeTypedValue(int index) {
        Attribute a = attribute(index); TypedValue v = new TypedValue();
        v.type = a.type; v.data = a.data;
        if (a.type == TypedValue.TYPE_STRING) {
            try { v.string = string(a.data); } catch (XmlPullParserException e) { return null; }
        }
        return v;
    }
    public int getAttributeNameResource(int i) { return attribute(i).id; }
    public int getAttributeResourceValue(int i, int def) { Attribute a = attribute(i); return a.type == 1 ? a.data : def; }
    public int getAttributeResourceValue(String ns, String name, int def) { int i = index(ns, name); return i < 0 ? def : getAttributeResourceValue(i, def); }
    public int getAttributeIntValue(int i, int def) { Attribute a = attribute(i); return a.type >= 16 && a.type <= 31 ? a.data : def; }
    public int getAttributeIntValue(String ns, String name, int def) { int i = index(ns, name); return i < 0 ? def : getAttributeIntValue(i, def); }
    public int getAttributeUnsignedIntValue(int i, int def) { int v = getAttributeIntValue(i, def); return v < 0 ? def : v; }
    public int getAttributeUnsignedIntValue(String ns, String name, int def) { int i = index(ns, name); return i < 0 ? def : getAttributeUnsignedIntValue(i, def); }
    public boolean getAttributeBooleanValue(int i, boolean def) { return getAttributeIntValue(i, def ? 1 : 0) != 0; }
    public boolean getAttributeBooleanValue(String ns, String name, boolean def) { int i = index(ns, name); return i < 0 ? def : getAttributeBooleanValue(i, def); }
    public float getAttributeFloatValue(int i, float def) { Attribute a = attribute(i); return a.type == 4 ? Float.intBitsToFloat(a.data) : def; }
    public float getAttributeFloatValue(String ns, String name, float def) { int i = index(ns, name); return i < 0 ? def : getAttributeFloatValue(i, def); }
    public int getAttributeListValue(int i, String[] options, int def) { String s = getAttributeValue(i); for (int n = 0; n < options.length; n++) if (options[n].equals(s)) return n; return def; }
    public int getAttributeListValue(String ns, String name, String[] options, int def) { int i = index(ns, name); return i < 0 ? def : getAttributeListValue(i, options, def); }
    public String getIdAttribute() { return getAttributeValue(ANDROID, "id"); }
    public String getClassAttribute() { return getAttributeValue("", "class"); }
    public int getIdAttributeResourceValue(int def) { return getAttributeResourceValue(ANDROID, "id", def); }
    public int getStyleAttribute() { return getAttributeResourceValue("", "style", 0); }
    public int getAttributeCount() { return current().type == START_TAG ? current().attrs.length : -1; }
    public String getAttributeNamespace(int i) { return attribute(i).namespace; }
    public String getAttributeName(int i) { return attribute(i).name; }
    public String getAttributeValue(int i) {
        Attribute a = attribute(i); if (a.raw != null) return a.raw;
        if (a.type == 3) { try { return string(a.data); } catch (XmlPullParserException e) { return null; } }
        if (a.type == 1 || a.type == 2) return (a.type == 1 ? "@" : "?") + a.data;
        if (a.type == 4) return Float.toString(Float.intBitsToFloat(a.data));
        if (a.type == 18) return a.data == 0 ? "false" : "true";
        if (a.type >= 28 && a.type <= 31) return "#" + Integer.toHexString(a.data);
        return Integer.toString(a.data);
    }
    public String getAttributeValue(String ns, String name) { int i = index(ns, name); return i < 0 ? null : getAttributeValue(i); }
    public String getAttributeType(int i) { attribute(i); return "CDATA"; }
    public boolean isAttributeDefault(int i) { attribute(i); return false; }
    public String getAttributePrefix(int i) { return prefix(getAttributeNamespace(i)); }
    private String prefix(String uri) { if (uri == null || uri.length() == 0) return null; for (int i = getNamespaceCount(getDepth()) - 1; i >= 0; i--) if (uri.equals(getNamespaceUri(i))) return getNamespacePrefix(i); return null; }
    private int nsStart() { return position <= 1 ? 0 : namespaceOffsets.get(position - 2); }
    private int nsEnd() { return position == 0 || position > namespaceOffsets.size() ? 0 : namespaceOffsets.get(position - 1); }
    public int getNamespaceCount(int depth) { int n = 0; for (int i = nsStart(); i < nsEnd(); i++) if (namespaces.get(i).depth <= depth) n++; return n; }
    public String getNamespacePrefix(int p) { if (p < 0 || nsStart() + p >= nsEnd()) throw new IndexOutOfBoundsException(); return namespaces.get(nsStart() + p).prefix; }
    public String getNamespaceUri(int p) { if (p < 0 || nsStart() + p >= nsEnd()) throw new IndexOutOfBoundsException(); return namespaces.get(nsStart() + p).uri; }
    public String getNamespace(String prefix) { for (int i = nsEnd() - 1; i >= nsStart(); i--) if (namespaces.get(i).prefix.equals(prefix == null ? "" : prefix)) return namespaces.get(i).uri; return null; }
    public int getDepth() { return current().depth; }
    public int getLineNumber() { return current().line; }
    public int getColumnNumber() { return -1; }
    public String getPositionDescription() { return "Binary XML file line #" + getLineNumber(); }
    public String getName() { return current().name; }
    public String getNamespace() { return current().namespace; }
    public String getPrefix() { return prefix(getNamespace()); }
    public String getText() { return current().text; }
    public char[] getTextCharacters(int[] holder) { String s = getText(); if (s == null) return null; holder[0] = 0; holder[1] = s.length(); return s.toCharArray(); }
    public boolean isWhitespace() { String text = getText(); return text != null && text.trim().length() == 0; }
    public boolean isEmptyElementTag() { return false; }
    public int getEventType() { return current().type; }
    public int next() throws XmlPullParserException { if (closed) throw error("Parser closed"); if (position < events.size() - 1) position++; return getEventType(); }
    public int nextToken() throws XmlPullParserException { return next(); }
    public int nextTag() throws XmlPullParserException { int t; do { t = next(); } while (t == TEXT && isWhitespace()); if (t != START_TAG && t != END_TAG) throw error("Expected XML tag"); return t; }
    public String nextText() throws XmlPullParserException { require(START_TAG, null, null); int t = next(); String s = t == TEXT ? getText() : ""; if (t == TEXT) t = next(); if (t != END_TAG) throw error("Expected end tag"); return s; }
    public void require(int type, String ns, String name) throws XmlPullParserException { if (getEventType() != type || (ns != null && !ns.equals(getNamespace())) || (name != null && !name.equals(getName()))) throw error("Unexpected XML event"); }
    public void close() { closed = true; }
    public String getInputEncoding() { return "UTF-8"; }
    public void setInput(Reader in) throws XmlPullParserException { throw error("Compiled resource parser is read-only"); }
    public void setInput(InputStream in, String encoding) throws XmlPullParserException { throw error("Compiled resource parser is read-only"); }
    public void defineEntityReplacementText(String name, String text) throws XmlPullParserException { throw error("Compiled XML has no entities"); }
    public void setFeature(String name, boolean value) throws XmlPullParserException { if (!getFeature(name) || !value) throw error("Unsupported parser feature"); }
    public boolean getFeature(String name) { return "http://xmlpull.org/v1/doc/features.html#process-namespaces".equals(name); }
    public void setProperty(String name, Object value) throws XmlPullParserException { throw error("Unsupported parser property"); }
    public Object getProperty(String name) { return null; }
}
