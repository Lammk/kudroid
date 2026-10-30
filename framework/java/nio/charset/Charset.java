package java.nio.charset;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

public abstract class Charset implements Comparable<Charset> {
    private final String canonicalName;
    private final Set<String> aliases;

    private static final Map<String, Charset> sCache = new HashMap<String, Charset>();

    private static class SimpleCharset extends Charset {
        SimpleCharset(String canonicalName, String[] aliases) {
            super(canonicalName, aliases);
        }

        @Override
        public boolean contains(Charset cs) {
            return cs != null && this.name().equalsIgnoreCase(cs.name());
        }

        @Override
        public CharsetDecoder newDecoder() {
            return new CharsetDecoder(this, 1.0f, 1.0f) {
                @Override
                protected CoderResult decodeLoop(ByteBuffer in, CharBuffer out) {
                    while (in.hasRemaining() && out.hasRemaining()) {
                        out.put((char)(in.get() & 0xFF));
                    }
                    return in.hasRemaining() ? CoderResult.OVERFLOW : CoderResult.UNDERFLOW;
                }
            };
        }

        @Override
        public CharsetEncoder newEncoder() {
            final String name = name();
            final boolean utf8 = name.equals("UTF-8");
            final boolean utf16 = name.startsWith("UTF-16");
            final boolean little = name.equals("UTF-16LE");
            final boolean bom = name.equals("UTF-16");
            final int maximum = name.equals("US-ASCII") ? 127 : 255;
            CharsetEncoder encoder = new CharsetEncoder(this, utf16 ? 2 : 1, bom ? 4 : utf16 ? 2 : utf8 ? 3 : 1) {
                private boolean needsBom = bom;
                @Override protected void implReset() { needsBom = bom; }
                @Override
                protected CoderResult encodeLoop(CharBuffer in, ByteBuffer out) {
                    if (needsBom && in.hasRemaining()) {
                        if (out.remaining() < 2) return CoderResult.OVERFLOW;
                        out.put((byte)0xfe).put((byte)0xff);
                        needsBom = false;
                    }
                    while (in.hasRemaining()) {
                        int start = in.position();
                        int cp = in.get(start);
                        int units = 1;
                        if (cp >= 0xd800 && cp <= 0xdbff) {
                            if (in.remaining() < 2) return CoderResult.UNDERFLOW;
                            int low = in.get(start + 1);
                            if (low < 0xdc00 || low > 0xdfff) return CoderResult.malformedForLength(1);
                            cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                            units = 2;
                        } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                            return CoderResult.malformedForLength(1);
                        }
                        if (!utf8 && !utf16 && cp > maximum) return CoderResult.unmappableForLength(units);
                        int bytes = utf16 ? units * 2 : utf8 ? (cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4) : 1;
                        if (out.remaining() < bytes) return CoderResult.OVERFLOW;
                        if (utf16) {
                            for (int i = 0; i < units; i++) {
                                int c = in.get(start + i);
                                out.put((byte)(little ? c : c >> 8));
                                out.put((byte)(little ? c >> 8 : c));
                            }
                        } else if (utf8 && bytes > 1) {
                            out.put((byte)((bytes == 2 ? 0xc0 : bytes == 3 ? 0xe0 : 0xf0) | cp >> (6 * (bytes - 1))));
                            for (int i = bytes - 2; i >= 0; i--) out.put((byte)(0x80 | (cp >> (6 * i) & 0x3f)));
                        } else {
                            out.put((byte)cp);
                        }
                        in.position(start + units);
                    }
                    return CoderResult.UNDERFLOW;
                }
            };
            if (utf16) encoder.replaceWith(little ? new byte[]{(byte)0xfd,(byte)0xff} : new byte[]{(byte)0xff,(byte)0xfd});
            return encoder;
        }
    }

    private static final Charset UTF_8 = new SimpleCharset("UTF-8", new String[]{"utf-8", "utf8", "unicode-1-1-utf-8"});
    private static final Charset UTF_16 = new SimpleCharset("UTF-16", new String[]{"utf-16", "utf16"});
    private static final Charset UTF_16BE = new SimpleCharset("UTF-16BE", new String[]{"utf-16be"});
    private static final Charset UTF_16LE = new SimpleCharset("UTF-16LE", new String[]{"utf-16le"});
    private static final Charset US_ASCII = new SimpleCharset("US-ASCII", new String[]{"us-ascii", "ascii", "iso-ir-6", "ansi_x3.4-1968"});
    private static final Charset ISO_8859_1 = new SimpleCharset("ISO-8859-1", new String[]{"iso-8859-1", "iso_8859_1", "latin1", "l1"});

    static {
        registerCharset(UTF_8);
        registerCharset(UTF_16);
        registerCharset(UTF_16BE);
        registerCharset(UTF_16LE);
        registerCharset(US_ASCII);
        registerCharset(ISO_8859_1);
    }

    private static void registerCharset(Charset cs) {
        sCache.put(cs.name().toUpperCase(java.util.Locale.ROOT), cs);
        for (String alias : cs.aliases()) {
            sCache.put(alias.toUpperCase(java.util.Locale.ROOT), cs);
        }
    }

    protected Charset(String canonicalName, String[] aliases) {
        if (canonicalName == null) {
            throw new IllegalArgumentException("canonicalName == null");
        }
        this.canonicalName = canonicalName;
        Set<String> aliasSet = new HashSet<String>();
        if (aliases != null) {
            for (String alias : aliases) {
                aliasSet.add(alias);
            }
        }
        this.aliases = Collections.unmodifiableSet(aliasSet);
    }

    public static Charset forName(String charsetName) {
        if (charsetName == null) {
            throw new IllegalArgumentException("charsetName == null");
        }
        if (charsetName.isEmpty()) throw new IllegalCharsetNameException(charsetName);
        for (int i = 0; i < charsetName.length(); i++) {
            char c = charsetName.charAt(i);
            boolean alphanumeric = c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9';
            if (!alphanumeric && (i == 0 || c != '-' && c != '+' && c != ':' && c != '_' && c != '.'))
                throw new IllegalCharsetNameException(charsetName);
        }
        Charset cs = sCache.get(charsetName.toUpperCase(java.util.Locale.ROOT));
        if (cs != null) {
            return cs;
        }
        throw new UnsupportedCharsetException(charsetName);
    }

    public static boolean isSupported(String charsetName) {
        try { forName(charsetName); return true; }
        catch (UnsupportedCharsetException error) { return false; }
    }

    public static Charset defaultCharset() {
        return UTF_8;
    }

    public final String name() {
        return canonicalName;
    }

    public final Set<String> aliases() {
        return aliases;
    }

    public String displayName() {
        return canonicalName;
    }

    public abstract boolean contains(Charset cs);

    public abstract CharsetDecoder newDecoder();

    public abstract CharsetEncoder newEncoder();

    public final ByteBuffer encode(CharBuffer cb) {
        try {
            return newEncoder().onMalformedInput(CodingErrorAction.REPLACE)
                    .onUnmappableCharacter(CodingErrorAction.REPLACE).encode(cb);
        } catch (CharacterCodingException error) {
            throw new IllegalStateException(error);
        }
    }

    public final ByteBuffer encode(String str) {
        return encode(CharBuffer.wrap(str));
    }

    public final CharBuffer decode(ByteBuffer bb) {
        return newDecoder().decode(bb);
    }

    @Override
    public final int compareTo(Charset that) {
        return this.canonicalName.compareToIgnoreCase(that.canonicalName);
    }

    @Override
    public final int hashCode() {
        return canonicalName.hashCode();
    }

    @Override
    public final boolean equals(Object obj) {
        if (obj instanceof Charset) {
            Charset that = (Charset) obj;
            return this.canonicalName.equalsIgnoreCase(that.canonicalName);
        }
        return false;
    }

    @Override
    public final String toString() {
        return canonicalName;
    }
}
