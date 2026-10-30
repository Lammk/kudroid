package android.util;

public class SparseArray<E> implements Cloneable {
    private int[] mKeys;
    private Object[] mValues;
    private int mSize;

    public SparseArray() {
        this(10);
    }

    public SparseArray(int initialCapacity) {
        if (initialCapacity <= 0) {
            mKeys = new int[0];
            mValues = new Object[0];
        } else {
            mKeys = new int[initialCapacity];
            mValues = new Object[initialCapacity];
        }
        mSize = 0;
    }

    @Override
    @SuppressWarnings("unchecked")
    public SparseArray<E> clone() {
        SparseArray<E> clone = null;
        try {
            clone = (SparseArray<E>) super.clone();
            clone.mKeys = mKeys.clone();
            clone.mValues = mValues.clone();
        } catch (CloneNotSupportedException ignored) {}
        return clone;
    }

    public E get(int key) {
        return get(key, null);
    }

    @SuppressWarnings("unchecked")
    public E get(int key, E valueIfKeyNotFound) {
        int i = binarySearch(mKeys, 0, mSize, key);
        if (i < 0) {
            return valueIfKeyNotFound;
        } else {
            return (E) mValues[i];
        }
    }

    public void delete(int key) {
        remove(key);
    }

    public void remove(int key) {
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

    public void put(int key, E value) {
        int i = binarySearch(mKeys, 0, mSize, key);
        if (i >= 0) {
            mValues[i] = value;
        } else {
            i = ~i;
            if (mSize >= mKeys.length) {
                int n = mSize < 4 ? 4 : mSize * 2;
                int[] nkeys = new int[n];
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

    public int keyAt(int index) {
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

    public int indexOfKey(int key) {
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

    public void append(int key, E value) {
        if (mSize != 0 && key <= mKeys[mSize - 1]) {
            put(key, value);
            return;
        }
        if (mSize >= mKeys.length) {
            int n = mSize < 4 ? 4 : mSize * 2;
            int[] nkeys = new int[n];
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

    private static int binarySearch(int[] a, int fromIndex, int toIndex, int key) {
        int low = fromIndex;
        int high = toIndex - 1;
        while (low <= high) {
            int mid = (low + high) >>> 1;
            int midVal = a[mid];
            if (midVal < key) low = mid + 1;
            else if (midVal > key) high = mid - 1;
            else return mid;
        }
        return ~low;
    }
}
