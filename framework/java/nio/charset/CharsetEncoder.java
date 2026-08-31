package java.nio.charset;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;

public abstract class CharsetEncoder {
    private final Charset charset;
    private final float averageBytesPerChar;
    private final float maxBytesPerChar;

    protected CharsetEncoder(Charset cs, float averageBytesPerChar, float maxBytesPerChar) {
        this.charset = cs;
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

    public final ByteBuffer encode(CharBuffer in) {
        int n = (int)(in.remaining() * averageBytesPerChar);
        ByteBuffer out = ByteBuffer.allocate(Math.max(n, 16));
        encodeLoop(in, out);
        out.flip();
        return out;
    }

    public final CoderResult encode(CharBuffer in, ByteBuffer out, boolean endOfInput) {
        return encodeLoop(in, out);
    }

    public final CoderResult flush(ByteBuffer out) {
        return CoderResult.UNDERFLOW;
    }

    public final CharsetEncoder reset() {
        return this;
    }

    protected abstract CoderResult encodeLoop(CharBuffer in, ByteBuffer out);
}
