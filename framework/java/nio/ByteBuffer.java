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

    private long readNumber(int index, int bytes) {
        if (index < 0 || index > limit() - bytes) throw new IndexOutOfBoundsException();
        long value = 0;
        for (int i = 0; i < bytes; i++) {
            int at = order == ByteOrder.BIG_ENDIAN ? index + i : index + bytes - 1 - i;
            value = (value << 8) | (get(at) & 255L);
        }
        return value;
    }

    private long readNumber(int bytes) {
        if (remaining() < bytes) throw new BufferUnderflowException();
        long value = readNumber(position(), bytes);
        position(position() + bytes);
        return value;
    }

    public short getShort(int index) { return (short) readNumber(index, 2); }
    public short getShort() { return (short) readNumber(2); }
    public char getChar(int index) { return (char) readNumber(index, 2); }
    public char getChar() { return (char) readNumber(2); }
    public int getInt(int index) { return (int) readNumber(index, 4); }
    public int getInt() { return (int) readNumber(4); }
    public long getLong(int index) { return readNumber(index, 8); }
    public long getLong() { return readNumber(8); }
    public float getFloat(int index) { return Float.intBitsToFloat(getInt(index)); }
    public float getFloat() { return Float.intBitsToFloat(getInt()); }
    public double getDouble(int index) { return Double.longBitsToDouble(getLong(index)); }
    public double getDouble() { return Double.longBitsToDouble(getLong()); }

    private ByteBuffer writeNumber(int index, long value, int bytes) {
        if (isReadOnly()) throw new ReadOnlyBufferException();
        if (index < 0 || index > limit() - bytes) throw new IndexOutOfBoundsException();
        for (int i = 0; i < bytes; i++) {
            int at = order == ByteOrder.BIG_ENDIAN ? index + bytes - 1 - i : index + i;
            put(at, (byte) (value >>> (i * 8)));
        }
        return this;
    }

    private ByteBuffer writeNumber(long value, int bytes) {
        if (isReadOnly()) throw new ReadOnlyBufferException();
        if (remaining() < bytes) throw new BufferOverflowException();
        int index = position();
        writeNumber(index, value, bytes);
        position(index + bytes);
        return this;
    }

    public ByteBuffer putShort(int index, short value) { return writeNumber(index, value, 2); }
    public ByteBuffer putShort(short value) { return writeNumber(value, 2); }
    public ByteBuffer putChar(int index, char value) { return writeNumber(index, value, 2); }
    public ByteBuffer putChar(char value) { return writeNumber(value, 2); }
    public ByteBuffer putInt(int index, int value) { return writeNumber(index, value, 4); }
    public ByteBuffer putInt(int value) { return writeNumber(value, 4); }
    public ByteBuffer putLong(int index, long value) { return writeNumber(index, value, 8); }
    public ByteBuffer putLong(long value) { return writeNumber(value, 8); }
    public ByteBuffer putFloat(int index, float value) { return putInt(index, Float.floatToRawIntBits(value)); }
    public ByteBuffer putFloat(float value) { return putInt(Float.floatToRawIntBits(value)); }
    public ByteBuffer putDouble(int index, double value) { return putLong(index, Double.doubleToRawLongBits(value)); }
    public ByteBuffer putDouble(double value) { return putLong(Double.doubleToRawLongBits(value)); }

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
