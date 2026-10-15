package android.os;

import java.util.ArrayList;
import java.util.List;

/**
 * android.os.Parcel — flat serialization container.
 *
 * Real parcel is a binary buffer shared between processes via Binder. KuDroid
 * runs everything in one process so no need for a compatible binary layout;
 * just need the read/write order to match for the app's writeToParcel/createFromParcel
 * round-trip correct. Use an Object list with sequential reading pointers.
 */
public final class Parcel {
    private final List<Object> mItems = new ArrayList<Object>();
    private int mReadPos = 0;

    private Parcel() {
    }

    public static Parcel obtain() {
        return new Parcel();
    }

    /** Parcel allocated by KuDroid is not in the pool so recycle only resets. */
    public void recycle() {
        mItems.clear();
        mReadPos = 0;
    }

    public int dataSize() {
        return mItems.size();
    }

    public int dataPosition() {
        return mReadPos;
    }

    public void setDataPosition(int pos) {
        mReadPos = (pos < 0) ? 0 : pos;
    }

    public void writeInt(int value) {
        mItems.add(Integer.valueOf(value));
    }

    public int readInt() {
        Object o = next();
        return (o instanceof Integer) ? ((Integer) o).intValue() : 0;
    }

    public void writeLong(long value) {
        mItems.add(Long.valueOf(value));
    }

    public long readLong() {
        Object o = next();
        return (o instanceof Long) ? ((Long) o).longValue() : 0L;
    }

    public void writeFloat(float value) {
        mItems.add(Float.valueOf(value));
    }

    public float readFloat() {
        Object o = next();
        return (o instanceof Float) ? ((Float) o).floatValue() : 0.0f;
    }

    public void writeDouble(double value) {
        mItems.add(Double.valueOf(value));
    }

    public double readDouble() {
        Object o = next();
        return (o instanceof Double) ? ((Double) o).doubleValue() : 0.0;
    }

    public void writeString(String value) {
        mItems.add(value);
    }

    public String readString() {
        Object o = next();
        return (o instanceof String) ? (String) o : null;
    }

    public void writeByte(byte value) {
        mItems.add(Byte.valueOf(value));
    }

    public byte readByte() {
        Object o = next();
        return (o instanceof Byte) ? ((Byte) o).byteValue() : 0;
    }

    public void writeByteArray(byte[] value) {
        mItems.add(value);
    }

    public byte[] readByteArray() {
        Object o = next();
        return (o instanceof byte[]) ? (byte[]) o : null;
    }

    public void writeIntArray(int[] value) {
        mItems.add(value);
    }

    public int[] readIntArray() {
        Object o = next();
        return (o instanceof int[]) ? (int[]) o : null;
    }

    public void writeStringArray(String[] value) {
        mItems.add(value);
    }

    public String[] readStringArray() {
        Object o = next();
        return (o instanceof String[]) ? (String[]) o : null;
    }

    public void writeValue(Object value) {
        mItems.add(value);
    }

    public Object readValue(ClassLoader loader) {
        return next();
    }

    public void writeParcelable(Parcelable value, int flags) {
        if (value == null) {
            mItems.add(null);
            return;
        }
        mItems.add(value.getClass().getName());
        value.writeToParcel(this, flags);
    }

    public void writeBundle(Bundle value) {
        mItems.add(value);
    }

    public Bundle readBundle() {
        Object o = next();
        return (o instanceof Bundle) ? (Bundle) o : null;
    }

    public void writeBooleanArray(boolean[] value) {
        mItems.add(value);
    }

    public boolean[] readBooleanArray() {
        Object o = next();
        return (o instanceof boolean[]) ? (boolean[]) o : null;
    }

    public void writeStrongBinder(IBinder value) {
        mItems.add(value);
    }

    public IBinder readStrongBinder() {
        Object o = next();
        return (o instanceof IBinder) ? (IBinder) o : null;
    }

    private Object next() {
        if (mReadPos < 0 || mReadPos >= mItems.size()) return null;
        return mItems.get(mReadPos++);
    }

    public Bundle readBundle(ClassLoader loader) {
        Object o = next();
        if (o instanceof Bundle) {
            Bundle b = (Bundle) o;
            if (loader != null) b.setClassLoader(loader);
            return b;
        }
        return null;
    }

    @SuppressWarnings("unchecked")
    public <T extends Parcelable> T readParcelable(ClassLoader loader) {
        Object name = next();
        if (name == null) return null;
        try {
            Class<?> clazz = (loader != null) ? Class.forName((String) name, true, loader) : Class.forName((String) name);
            java.lang.reflect.Field field = clazz.getField("CREATOR");
            Parcelable.Creator<?> creator = (Parcelable.Creator<?>) field.get(null);
            return (T) creator.createFromParcel(this);
        } catch (Exception e) {
            return null;
        }
    }

    public void writeSerializable(java.io.Serializable s) {
        mItems.add(s);
    }

    public java.io.Serializable readSerializable() {
        Object o = next();
        return (o instanceof java.io.Serializable) ? (java.io.Serializable) o : null;
    }

    public void writeStringList(List<String> val) {
        if (val == null) {
            writeInt(-1);
            return;
        }
        writeInt(val.size());
        for (String s : val) {
            writeString(s);
        }
    }

    public void readStringList(List<String> list) {
        int n = readInt();
        if (n < 0 || list == null) return;
        list.clear();
        for (int i = 0; i < n; i++) {
            list.add(readString());
        }
    }

    public ArrayList<String> createStringArrayList() {
        int n = readInt();
        if (n < 0) return null;
        ArrayList<String> list = new ArrayList<String>(n);
        for (int i = 0; i < n; i++) {
            list.add(readString());
        }
        return list;
    }

    public <T extends Parcelable> void writeTypedList(List<T> val) {
        if (val == null) {
            writeInt(-1);
            return;
        }
        writeInt(val.size());
        for (T item : val) {
            if (item != null) {
                writeInt(1);
                item.writeToParcel(this, 0);
            } else {
                writeInt(0);
            }
        }
    }

    public <T> ArrayList<T> createTypedArrayList(Parcelable.Creator<T> c) {
        int n = readInt();
        if (n < 0) return null;
        ArrayList<T> list = new ArrayList<T>(n);
        for (int i = 0; i < n; i++) {
            if (readInt() != 0) {
                list.add(c.createFromParcel(this));
            } else {
                list.add(null);
            }
        }
        return list;
    }

    public byte[] createByteArray() {
        return readByteArray();
    }

    public int[] createIntArray() {
        return readIntArray();
    }

    public boolean[] createBooleanArray() {
        return readBooleanArray();
    }

    public String[] createStringArray() {
        return readStringArray();
    }
}
