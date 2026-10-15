package android.os;

import android.util.ArrayMap;

/**
 * A mapping from String keys to values that can be saved to persistent storage.
 */
public final class PersistableBundle extends BaseBundle implements Cloneable, Parcelable {

    public static final PersistableBundle EMPTY;

    static {
        EMPTY = new PersistableBundle();
        EMPTY.mMap = new ArrayMap<String, Object>();
    }

    public static final Parcelable.Creator<PersistableBundle> CREATOR =
            new Parcelable.Creator<PersistableBundle>() {
        @Override
        public PersistableBundle createFromParcel(Parcel in) {
            PersistableBundle b = new PersistableBundle();
            Bundle raw = in.readBundle();
            if (raw != null) {
                b.mMap.putAll(raw.mMap);
            }
            return b;
        }

        @Override
        public PersistableBundle[] newArray(int size) {
            return new PersistableBundle[size];
        }
    };

    public PersistableBundle() {
        super();
    }

    public PersistableBundle(int capacity) {
        super(capacity);
    }

    public PersistableBundle(PersistableBundle b) {
        super(b);
    }

    public PersistableBundle(Bundle b) {
        super();
        if (b != null && b.mMap != null) {
            mMap.putAll(b.mMap);
        }
    }

    @Override
    public Object clone() {
        return new PersistableBundle(this);
    }

    public PersistableBundle deepCopy() {
        PersistableBundle copy = new PersistableBundle();
        copy.copyInternal(this, true);
        return copy;
    }

    public void putPersistableBundle(String key, PersistableBundle value) {
        mMap.put(key, value);
    }

    public PersistableBundle getPersistableBundle(String key) {
        Object o = mMap.get(key);
        return (o instanceof PersistableBundle) ? (PersistableBundle) o : null;
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel parcel, int flags) {
        Bundle b = new Bundle();
        b.mMap.putAll(mMap);
        parcel.writeBundle(b);
    }
}
