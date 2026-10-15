package java.nio;

class ByteArrayBuffer extends ByteBuffer {
    ByteArrayBuffer(int capacity, boolean isReadOnly) {
        this(new byte[capacity], 0, capacity, isReadOnly);
    }

    ByteArrayBuffer(byte[] array, int offset, int length, boolean isReadOnly) {
        super(0, length, array, offset);
        this.isReadOnly = isReadOnly;
    }

    @Override
    public boolean isReadOnly() { return isReadOnly; }

    @Override
    public boolean isDirect() { return false; }

    @Override
    public byte get() {
        if (position() >= limit()) throw new BufferUnderflowException();
        byte b = hb[offset + position()];
        position(position() + 1);
        return b;
    }

    @Override
    public ByteBuffer put(byte b) {
        if (isReadOnly) throw new ReadOnlyBufferException();
        if (position() >= limit()) throw new BufferOverflowException();
        hb[offset + position()] = b;
        position(position() + 1);
        return this;
    }

    @Override
    public byte get(int index) {
        if (index < 0 || index >= limit()) throw new IndexOutOfBoundsException();
        return hb[offset + index];
    }

    @Override
    public ByteBuffer put(int index, byte b) {
        if (isReadOnly) throw new ReadOnlyBufferException();
        if (index < 0 || index >= limit()) throw new IndexOutOfBoundsException();
        hb[offset + index] = b;
        return this;
    }
}
