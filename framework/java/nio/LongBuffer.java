package java.nio;

public abstract class LongBuffer extends Buffer implements Comparable<LongBuffer> {
    final long[] hb;
    final int offset;

    LongBuffer(long address, int capacity, long[] hb, int offset) {
        super(address, capacity);
        this.hb = hb;
        this.offset = offset;
    }

    public static LongBuffer wrap(long[] array, int offset, int length) {
        return new LongBuffer(0, array.length, array, offset) {
            public long get() { return hb[position++]; }
            public LongBuffer put(long l) { hb[position++] = l; return this; }
            public long get(int index) { return hb[index]; }
            public LongBuffer put(int index, long l) { hb[index] = l; return this; }
            public boolean isDirect() { return false; }
            public boolean isReadOnly() { return false; }
        };
    }
    public static LongBuffer wrap(long[] array) {
        return wrap(array, 0, array.length);
    }
    public final boolean hasArray() { return hb != null; }
    public final long[] array() { return hb; }
    public final int arrayOffset() { return offset; }
    public int compareTo(LongBuffer that) { return 0; }
}
