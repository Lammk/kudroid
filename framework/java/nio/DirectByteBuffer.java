package java.nio;

public class DirectByteBuffer extends ByteBuffer {
    public DirectByteBuffer(int capacity) {
        this(nAllocate(capacity), capacity);
    }

    public DirectByteBuffer(long address, int capacity) {
        super(address, capacity, null, 0);
    }

    // Backed by a real host malloc so GetDirectBufferAddress() returns a
    // writable pointer. Previously the address was hardcoded to 0, which made
    // every native writer (notably FMOD's fmodProcess) mix into NULL and die
    // with SIGSEGV. Memory is never freed (no Cleaner infra); direct buffers
    // in practice are few and long-lived (audio, graphics).
    private static native long nAllocate(int capacity);
    private static native byte nGet(long address, int index);
    private static native void nPut(long address, int index, byte b);
    private static native void nGetArray(long address, int index, byte[] dst, int offset, int length);
    private static native void nPutArray(long address, int index, byte[] src, int offset, int length);

    @Override
    public boolean isReadOnly() { return false; }

    @Override
    public boolean isDirect() { return true; }

    @Override
    public byte get() {
        if (position() >= limit()) throw new BufferUnderflowException();
        byte b = nGet(address, position());
        position(position() + 1);
        return b;
    }

    @Override
    public ByteBuffer put(byte b) {
        if (position() >= limit()) throw new BufferOverflowException();
        nPut(address, position(), b);
        position(position() + 1);
        return this;
    }

    @Override
    public byte get(int index) {
        if (index < 0 || index >= limit()) throw new IndexOutOfBoundsException();
        return nGet(address, index);
    }

    @Override
    public ByteBuffer put(int index, byte b) {
        if (index < 0 || index >= limit()) throw new IndexOutOfBoundsException();
        nPut(address, index, b);
        return this;
    }

    @Override
    public ByteBuffer get(byte[] dst, int offset, int length) {
        if (offset < 0 || length < 0 || offset + length > dst.length) throw new IndexOutOfBoundsException();
        if (length > remaining()) throw new BufferUnderflowException();
        nGetArray(address, position(), dst, offset, length);
        position(position() + length);
        return this;
    }

    @Override
    public ByteBuffer put(byte[] src, int offset, int length) {
        if (offset < 0 || length < 0 || offset + length > src.length) throw new IndexOutOfBoundsException();
        if (length > remaining()) throw new BufferOverflowException();
        nPutArray(address, position(), src, offset, length);
        position(position() + length);
        return this;
    }
}
