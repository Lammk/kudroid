package java.nio;

public abstract class FloatBuffer extends Buffer implements Comparable<FloatBuffer> {
    final float[] hb;
    final int offset;

    FloatBuffer(long address, int capacity, float[] hb, int offset) {
        super(address, capacity);
        this.hb = hb;
        this.offset = offset;
    }

    public static FloatBuffer allocate(int capacity) {
        if (capacity < 0) throw new IllegalArgumentException("capacity < 0");
        return wrap(new float[capacity], 0, capacity);
    }

    public static FloatBuffer wrap(float[] array, int offset, int length) {
        if (array == null) throw new NullPointerException("array must not be null");
        if (offset < 0 || length < 0 || offset + length > array.length) {
            throw new IndexOutOfBoundsException("offset=" + offset + " length=" + length
                    + " array.length=" + array.length);
        }
        return new FloatBuffer(0, length, array, offset) {
            public float get() { return hb[position++]; }
            public FloatBuffer put(float f) { hb[position++] = f; return this; }
            public float get(int index) { return hb[offset + index]; }
            public FloatBuffer put(int index, float f) { hb[offset + index] = f; return this; }
            public FloatBuffer get(float[] dst, int dstOffset, int len) {
                if (len > remaining()) throw new BufferUnderflowException();
                System.arraycopy(hb, offset + position, dst, dstOffset, len);
                position += len;
                return this;
            }
            public FloatBuffer get(float[] dst) { return get(dst, 0, dst.length); }
            public FloatBuffer put(float[] src, int srcOffset, int len) {
                if (len > remaining()) throw new BufferOverflowException();
                System.arraycopy(src, srcOffset, hb, offset + position, len);
                position += len;
                return this;
            }
            public FloatBuffer put(float[] src) { return put(src, 0, src.length); }
            public boolean isDirect() { return false; }
            public boolean isReadOnly() { return false; }
        };
    }

    public static FloatBuffer wrap(float[] array) {
        return wrap(array, 0, array.length);
    }

    public abstract float get();
    public abstract FloatBuffer put(float f);
    public abstract float get(int index);
    public abstract FloatBuffer put(int index, float f);
    public abstract FloatBuffer get(float[] dst, int dstOffset, int len);
    public abstract FloatBuffer get(float[] dst);
    public abstract FloatBuffer put(float[] src, int srcOffset, int len);
    public abstract FloatBuffer put(float[] src);

    public final boolean hasArray() { return hb != null; }
    public final float[] array() { return hb; }
    public final int arrayOffset() { return offset; }

    public FloatBuffer asReadOnlyBuffer() {
        final FloatBuffer source = this;
        return new FloatBuffer(address, capacity, hb, offset) {
            public float get() { return source.get(position); }
            public FloatBuffer put(float f) { throw new ReadOnlyBufferException(); }
            public float get(int index) { return source.get(index); }
            public FloatBuffer put(int index, float f) { throw new ReadOnlyBufferException(); }
            public FloatBuffer get(float[] dst, int dstOffset, int len) { throw new ReadOnlyBufferException(); }
            public FloatBuffer get(float[] dst) { throw new ReadOnlyBufferException(); }
            public FloatBuffer put(float[] src, int srcOffset, int len) { throw new ReadOnlyBufferException(); }
            public FloatBuffer put(float[] src) { throw new ReadOnlyBufferException(); }
            public boolean isDirect() { return source.isDirect(); }
            public boolean isReadOnly() { return true; }
        };
    }

    public FloatBuffer slice() { return wrap(hb, offset + position, remaining()); }
    public FloatBuffer duplicate() { return wrap(hb, offset, capacity); }
    public int compareTo(FloatBuffer that) { return 0; }
}
