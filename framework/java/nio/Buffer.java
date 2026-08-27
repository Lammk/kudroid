package java.nio;

public abstract class Buffer {
    public long address;
    public int capacity;
    public int limit;
    public int position = 0;
    public int mark = -1;

    Buffer(long address, int capacity) {
        this.address = address;
        this.capacity = capacity;
        this.limit = capacity;
    }

    public final int capacity() {
        return capacity;
    }

    public final int position() {
        return position;
    }

    public final Buffer position(int newPosition) {
        if (newPosition < 0 || newPosition > limit) {
            throw new IllegalArgumentException("Bad position " + newPosition + " (limit=" + limit + ")");
        }
        position = newPosition;
        if (mark > position) mark = -1;
        return this;
    }

    public final int limit() {
        return limit;
    }

    public final Buffer limit(int newLimit) {
        if (newLimit < 0 || newLimit > capacity) {
            throw new IllegalArgumentException("Bad limit " + newLimit + " (capacity=" + capacity + ")");
        }
        limit = newLimit;
        if (position > limit) position = limit;
        if (mark > limit) mark = -1;
        return this;
    }

    public final Buffer mark() {
        mark = position;
        return this;
    }

    public final Buffer reset() {
        if (mark < 0) throw new InvalidMarkException();
        position = mark;
        return this;
    }

    public final Buffer clear() {
        position = 0;
        limit = capacity;
        mark = -1;
        return this;
    }

    public final Buffer flip() {
        limit = position;
        position = 0;
        mark = -1;
        return this;
    }

    public final Buffer rewind() {
        position = 0;
        mark = -1;
        return this;
    }

    public final int remaining() {
        return limit - position;
    }

    public final boolean hasRemaining() {
        return position < limit;
    }

    public abstract boolean isReadOnly();
    public abstract boolean isDirect();
}
