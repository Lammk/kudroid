package android.os;

import java.util.ArrayList;
import java.util.List;

/**
 * android.os.Parcel — container tuần tự hoá phẳng.
 *
 * Parcel thật là buffer nhị phân chia sẻ giữa các process qua Binder. KuDroid
 * chạy mọi thứ trong một process nên không cần layout nhị phân tương thích;
 * chỉ cần thứ tự đọc/ghi khớp nhau để writeToParcel/createFromParcel của app
 * round-trip đúng. Dùng danh sách Object với con trỏ đọc tuần tự.
 */
public final class Parcel {
    private final List<Object> mItems = new ArrayList<Object>();
    private int mReadPos = 0;

    private Parcel() {
    }

    public static Parcel obtain() {
        return new Parcel();
    }

    /** Parcel do KuDroid cấp phát không nằm trong pool nên recycle chỉ reset. */
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
}
