package java.nio.charset;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;

public abstract class CharsetEncoder {
    private final Charset charset;
    private final float averageBytesPerChar;
    private final float maxBytesPerChar;
    private byte[] replacement = new byte[]{63};
    private CodingErrorAction malformed = CodingErrorAction.REPORT;
    private CodingErrorAction unmappable = CodingErrorAction.REPORT;
    private int state;

    protected CharsetEncoder(Charset cs, float averageBytesPerChar, float maxBytesPerChar) {
        this.charset = cs;
        if (!(averageBytesPerChar > 0) || !(maxBytesPerChar > 0) || averageBytesPerChar > maxBytesPerChar)
            throw new IllegalArgumentException("bytes per character");
        this.averageBytesPerChar = averageBytesPerChar;
        this.maxBytesPerChar = maxBytesPerChar;
    }

    public final Charset charset() {
        return charset;
    }

    public final float averageBytesPerChar() {
        return averageBytesPerChar;
    }

    public final float maxBytesPerChar() {
        return maxBytesPerChar;
    }

    public final ByteBuffer encode(CharBuffer in) throws CharacterCodingException {
        reset();
        int n = (int)(in.remaining() * averageBytesPerChar);
        ByteBuffer out = ByteBuffer.allocate(n);
        while (true) {
            CoderResult result = encode(in, out, true);
            if (result.isError()) throw codingFailure(result);
            if (result.isOverflow()) { out = grow(out); continue; }
            break;
        }
        while (true) {
            CoderResult result = flush(out);
            if (result.isError()) throw codingFailure(result);
            if (result.isOverflow()) { out = grow(out); continue; }
            break;
        }
        out.flip();
        return out;
    }

    private ByteBuffer grow(ByteBuffer old) {
        if (old.capacity() == Integer.MAX_VALUE) throw new OutOfMemoryError();
        int capacity = old.capacity() > (Integer.MAX_VALUE - 1) / 2
                ? Integer.MAX_VALUE : old.capacity() * 2 + 1;
        ByteBuffer grown = ByteBuffer.allocate(capacity);
        old.flip();
        while (old.hasRemaining()) grown.put(old.get());
        return grown;
    }

    private CharacterCodingException codingFailure(CoderResult result) {
        return result.isMalformed() ? new MalformedInputException(result.length())
                : new UnmappableCharacterException(result.length());
    }

    public final CoderResult encode(CharBuffer in, ByteBuffer out, boolean endOfInput) {
        if (state == 3 || (state == 2 && !endOfInput)) throw new IllegalStateException("encoder state");
        state = endOfInput ? 2 : 1;
        while (true) {
            CoderResult result = encodeLoop(in, out);
            if (result.isUnderflow()) {
                if (!endOfInput || !in.hasRemaining()) return result;
                result = CoderResult.malformedForLength(in.remaining());
            }
            if (result.isOverflow()) return result;
            CodingErrorAction action = result.isMalformed() ? malformed : unmappable;
            if (action == CodingErrorAction.REPORT) return result;
            if (action == CodingErrorAction.REPLACE) {
                if (out.remaining() < replacement.length) return CoderResult.OVERFLOW;
                out.put(replacement);
            }
            in.position(in.position() + result.length());
        }
    }

    public final CoderResult flush(ByteBuffer out) {
        if (state == 3) return CoderResult.UNDERFLOW;
        if (state != 2) throw new IllegalStateException("encoder state");
        CoderResult result = implFlush(out);
        if (result.isUnderflow()) state = 3;
        return result;
    }

    public final CharsetEncoder reset() {
        state = 0;
        implReset();
        return this;
    }

    public final byte[] replacement() { return replacement.clone(); }
    public final CharsetEncoder replaceWith(byte[] bytes) {
        if (bytes == null || bytes.length == 0 || bytes.length > maxBytesPerChar)
            throw new IllegalArgumentException("replacement");
        replacement = bytes.clone();
        implReplaceWith(replacement);
        return this;
    }
    public final CharsetEncoder onMalformedInput(CodingErrorAction action) {
        if (action == null) throw new IllegalArgumentException("action");
        malformed = action;
        implOnMalformedInput(action);
        return this;
    }
    public final CharsetEncoder onUnmappableCharacter(CodingErrorAction action) {
        if (action == null) throw new IllegalArgumentException("action");
        unmappable = action;
        implOnUnmappableCharacter(action);
        return this;
    }
    public final CodingErrorAction malformedInputAction() { return malformed; }
    public final CodingErrorAction unmappableCharacterAction() { return unmappable; }
    protected void implReset() {}
    protected void implReplaceWith(byte[] bytes) {}
    protected void implOnMalformedInput(CodingErrorAction action) {}
    protected void implOnUnmappableCharacter(CodingErrorAction action) {}
    protected CoderResult implFlush(ByteBuffer out) { return CoderResult.UNDERFLOW; }

    protected abstract CoderResult encodeLoop(CharBuffer in, ByteBuffer out);
}
