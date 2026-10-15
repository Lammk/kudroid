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
            return new CharsetEncoder(this, 1.0f, 1.0f) {
                @Override
                protected CoderResult encodeLoop(CharBuffer in, ByteBuffer out) {
                    while (in.hasRemaining() && out.hasRemaining()) {
                        out.put((byte)(in.get() & 0xFF));
                    }
                    return in.hasRemaining() ? CoderResult.OVERFLOW : CoderResult.UNDERFLOW;
                }
            };
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
        sCache.put(cs.name().toUpperCase(), cs);
        for (String alias : cs.aliases()) {
            sCache.put(alias.toUpperCase(), cs);
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
        Charset cs = sCache.get(charsetName.trim().toUpperCase());
        if (cs != null) {
            return cs;
        }
        Charset created = new SimpleCharset(charsetName, new String[]{charsetName});
        registerCharset(created);
        return created;
    }

    public static boolean isSupported(String charsetName) {
        return charsetName != null;
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
        return newEncoder().encode(cb);
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
