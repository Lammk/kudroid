package android.util;

public class LongSparseArray<E> implements Cloneable {
    private long[] mKeys;
    private Object[] mValues;
    private int mSize;

    public LongSparseArray() {
        this(10);
    }

    public LongSparseArray(int initialCapacity) {
        if (initialCapacity <= 0) {
            mKeys = new long[0];
            mValues = new Object[0];
        } else {
            mKeys = new long[initialCapacity];
            mValues = new Object[initialCapacity];
        }
        mSize = 0;
    }

    @Override
    @SuppressWarnings("unchecked")
    public LongSparseArray<E> clone() {
        LongSparseArray<E> clone = null;
        try {
            clone = (LongSparseArray<E>) super.clone();
            clone.mKeys = mKeys.clone();
            clone.mValues = mValues.clone();
        } catch (CloneNotSupportedException ignored) {}
        return clone;
    }

    public E get(long key) {
        return get(key, null);
    }

    @SuppressWarnings("unchecked")
    public E get(long key, E valueIfKeyNotFound) {
        int i = binarySearch(mKeys, 0, mSize, key);
        if (i < 0) {
            return valueIfKeyNotFound;
        } else {
            return (E) mValues[i];
        }
    }

    public void delete(long key) {
        remove(key);
    }

    public void remove(long key) {
        int i = binarySearch(mKeys, 0, mSize, key);
        if (i >= 0) {
            removeAt(i);
        }
    }

    public void removeAt(int index) {
        if (index < 0 || index >= mSize) return;
        System.arraycopy(mKeys, index + 1, mKeys, index, mSize - (index + 1));
        System.arraycopy(mValues, index + 1, mValues, index, mSize - (index + 1));
        mSize--;
        mValues[mSize] = null;
    }

    public void put(long key, E value) {
        int i = binarySearch(mKeys, 0, mSize, key);
        if (i >= 0) {
            mValues[i] = value;
        } else {
            i = ~i;
            if (mSize >= mKeys.length) {
                int n = mSize < 4 ? 4 : mSize * 2;
                long[] nkeys = new long[n];
                Object[] nvalues = new Object[n];
                System.arraycopy(mKeys, 0, nkeys, 0, mSize);
                System.arraycopy(mValues, 0, nvalues, 0, mSize);
                mKeys = nkeys;
                mValues = nvalues;
            }
            if (mSize - i != 0) {
                System.arraycopy(mKeys, i, mKeys, i + 1, mSize - i);
                System.arraycopy(mValues, i, mValues, i + 1, mSize - i);
            }
            mKeys[i] = key;
            mValues[i] = value;
            mSize++;
        }
    }

    public int size() {
        return mSize;
    }

    public long keyAt(int index) {
        if (index < 0 || index >= mSize) throw new ArrayIndexOutOfBoundsException(index);
        return mKeys[index];
    }

    @SuppressWarnings("unchecked")
    public E valueAt(int index) {
        if (index < 0 || index >= mSize) throw new ArrayIndexOutOfBoundsException(index);
        return (E) mValues[index];
    }

    public void setValueAt(int index, E value) {
        if (index < 0 || index >= mSize) throw new ArrayIndexOutOfBoundsException(index);
        mValues[index] = value;
    }

    public int indexOfKey(long key) {
        return binarySearch(mKeys, 0, mSize, key);
    }

    public int indexOfValue(E value) {
        for (int i = 0; i < mSize; i++) {
            if (mValues[i] == value) return i;
        }
        return -1;
    }

    public void clear() {
        for (int i = 0; i < mSize; i++) {
            mValues[i] = null;
        }
        mSize = 0;
    }

    public void append(long key, E value) {
        if (mSize != 0 && key <= mKeys[mSize - 1]) {
            put(key, value);
            return;
        }
        if (mSize >= mKeys.length) {
            int n = mSize < 4 ? 4 : mSize * 2;
            long[] nkeys = new long[n];
            Object[] nvalues = new Object[n];
            System.arraycopy(mKeys, 0, nkeys, 0, mSize);
            System.arraycopy(mValues, 0, nvalues, 0, mSize);
            mKeys = nkeys;
            mValues = nvalues;
        }
        mKeys[mSize] = key;
        mValues[mSize] = value;
        mSize++;
    }

    private static int binarySearch(long[] a, int fromIndex, int toIndex, long key) {
        int low = fromIndex;
        int high = toIndex - 1;
        while (low <= high) {
            int mid = (low + high) >>> 1;
            long midVal = a[mid];
            if (midVal < key) low = mid + 1;
            else if (midVal > key) high = mid - 1;
            else return mid;
        }
        return ~low;
    }
}
