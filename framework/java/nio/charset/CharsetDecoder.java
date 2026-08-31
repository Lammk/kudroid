package java.nio.charset;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;

public abstract class CharsetDecoder {
    private final Charset charset;
    private final float averageCharsPerByte;
    private final float maxCharsPerByte;

    protected CharsetDecoder(Charset cs, float averageCharsPerByte, float maxCharsPerByte) {
        this.charset = cs;
        this.averageCharsPerByte = averageCharsPerByte;
        this.maxCharsPerByte = maxCharsPerByte;
    }

    public final Charset charset() {
        return charset;
    }

    public final float averageCharsPerByte() {
        return averageCharsPerByte;
    }

    public final float maxCharsPerByte() {
        return maxCharsPerByte;
    }

    public final CharBuffer decode(ByteBuffer in) {
        int n = (int)(in.remaining() * averageCharsPerByte);
        CharBuffer out = CharBuffer.allocate(Math.max(n, 16));
        decodeLoop(in, out);
        out.flip();
        return out;
    }

    public final CoderResult decode(ByteBuffer in, CharBuffer out, boolean endOfInput) {
        return decodeLoop(in, out);
    }

    public final CoderResult flush(CharBuffer out) {
        return CoderResult.UNDERFLOW;
    }

    public final CharsetDecoder reset() {
        return this;
    }

    protected abstract CoderResult decodeLoop(ByteBuffer in, CharBuffer out);
}
