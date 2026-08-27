package java.nio;

public abstract class ByteBuffer extends Buffer implements Comparable<ByteBuffer> {
    final byte[] hb;
    final int offset;
    boolean isReadOnly;
    ByteOrder order = ByteOrder.BIG_ENDIAN;

    ByteBuffer(long address, int capacity, byte[] hb, int offset) {
        super(address, capacity);
        this.hb = hb;
        this.offset = offset;
    }

    public static ByteBuffer allocate(int capacity) {
        if (capacity < 0) throw new IllegalArgumentException();
        return new ByteArrayBuffer(capacity, false);
    }

    public static ByteBuffer allocateDirect(int capacity) {
        if (capacity < 0) throw new IllegalArgumentException();
        return new DirectByteBuffer(capacity);
    }

    public static ByteBuffer wrap(byte[] array, int offset, int length) {
        try {
            return new ByteArrayBuffer(array, offset, length, false);
        } catch (IllegalArgumentException x) {
            throw new IndexOutOfBoundsException();
        }
    }

    public static ByteBuffer wrap(byte[] array) {
        return wrap(array, 0, array.length);
    }

    public final ByteOrder order() {
        return order;
    }

    public final ByteBuffer order(ByteOrder bo) {
        order = (bo == null ? ByteOrder.LITTLE_ENDIAN : bo);
        return this;
    }

    public abstract byte get();
    public abstract ByteBuffer put(byte b);
    public abstract byte get(int index);
    public abstract ByteBuffer put(int index, byte b);

    public ByteBuffer get(byte[] dst, int offset, int length) {
        if (offset < 0 || length < 0 || offset + length > dst.length) throw new IndexOutOfBoundsException();
        if (length > remaining()) throw new BufferUnderflowException();
        for (int i = 0; i < length; i++) {
            dst[offset + i] = get();
        }
        return this;
    }

    public ByteBuffer get(byte[] dst) {
        return get(dst, 0, dst.length);
    }

    public ByteBuffer put(byte[] src, int offset, int length) {
        if (offset < 0 || length < 0 || offset + length > src.length) throw new IndexOutOfBoundsException();
        if (length > remaining()) throw new BufferOverflowException();
        for (int i = 0; i < length; i++) {
            put(src[offset + i]);
        }
        return this;
    }

    public final ByteBuffer put(byte[] src) {
        return put(src, 0, src.length);
    }

    public final byte[] array() {
        if (hb == null) throw new UnsupportedOperationException();
        if (isReadOnly) throw new ReadOnlyBufferException();
        return hb;
    }

    public final int arrayOffset() {
        if (hb == null) throw new UnsupportedOperationException();
        if (isReadOnly) throw new ReadOnlyBufferException();
        return offset;
    }

    public final boolean hasArray() {
        return (hb != null) && !isReadOnly;
    }

    @Override
    public int compareTo(ByteBuffer that) {
        int n = this.position() + Math.min(this.remaining(), that.remaining());
        for (int i = this.position(), j = that.position(); i < n; i++, j++) {
            int cmp = Byte.compare(this.get(i), that.get(j));
            if (cmp != 0) return cmp;
        }
        return this.remaining() - that.remaining();
    }
}
