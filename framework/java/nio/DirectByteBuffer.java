package java.nio;

public class DirectByteBuffer extends ByteBuffer {
    public DirectByteBuffer(int capacity) {
        this(0, capacity);
    }

    public DirectByteBuffer(long address, int capacity) {
        super(address, capacity, null, 0);
    }

    @Override
    public boolean isReadOnly() { return false; }

    @Override
    public boolean isDirect() { return true; }

    @Override
    public byte get() {
        if (position() >= limit()) throw new BufferUnderflowException();
        byte b = 0;
        position(position() + 1);
        return b;
    }

    @Override
    public ByteBuffer put(byte b) {
        if (position() >= limit()) throw new BufferOverflowException();
        position(position() + 1);
        return this;
    }

    @Override
    public byte get(int index) {
        if (index < 0 || index >= limit()) throw new IndexOutOfBoundsException();
        return 0;
    }

    @Override
    public ByteBuffer put(int index, byte b) {
        if (index < 0 || index >= limit()) throw new IndexOutOfBoundsException();
        return this;
    }
}
