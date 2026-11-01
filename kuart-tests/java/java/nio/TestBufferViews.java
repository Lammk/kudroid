package java.nio;

public final class TestBufferViews extends ByteBuffer {
    private TestBufferViews(byte[] bytes, int start, int length) {
        super(0, length, bytes, start);
        isReadOnly = true;
    }
    public static ByteBuffer readOnlySlice(byte[] bytes, int start, int length) {
        return new TestBufferViews(bytes, start, length);
    }
    public boolean isReadOnly() { return true; }
    public boolean isDirect() { return false; }
    public byte get(int index) {
        if (index < 0 || index >= limit()) throw new IndexOutOfBoundsException();
        return hb[offset + index];
    }
    public byte get() {
        if (!hasRemaining()) throw new BufferUnderflowException();
        byte value = get(position());
        position(position() + 1);
        return value;
    }
    public ByteBuffer put(byte value) { throw new ReadOnlyBufferException(); }
    public ByteBuffer put(int index, byte value) { throw new ReadOnlyBufferException(); }
}
