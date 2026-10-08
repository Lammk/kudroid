package android.os;

import android.util.ArrayMap;
import android.util.SparseArray;
import java.io.Serializable;
import java.util.ArrayList;

/**
 * A mapping from String keys to various Parcelable values following AOSP contract.
 */
public final class Bundle extends BaseBundle implements Cloneable, Parcelable {

    public static final Bundle EMPTY;

    static {
        EMPTY = new Bundle();
        EMPTY.mMap = new ArrayMap<String, Object>();
    }

    public static final Parcelable.Creator<Bundle> CREATOR = new Parcelable.Creator<Bundle>() {
        @Override
        public Bundle createFromParcel(Parcel in) {
            return in.readBundle();
        }

        @Override
        public Bundle[] newArray(int size) {
            return new Bundle[size];
        }
    };

    public Bundle() {
        super();
    }

    public Bundle(ClassLoader loader) {
        super(loader);
    }

    public Bundle(int capacity) {
        super(capacity);
    }

    public Bundle(Bundle b) {
        super(b);
    }

    public Bundle(PersistableBundle b) {
        super(b);
    }

    public void putAll(Bundle bundle) {
        if (bundle != null && bundle.mMap != null) {
            mMap.putAll(bundle.mMap);
        }
    }

    public boolean hasFileDescriptors() {
        return false;
    }

    public void putParcelable(String key, Parcelable value) {
        mMap.put(key, value);
    }

    @SuppressWarnings("unchecked")
    public <T extends Parcelable> T getParcelable(String key) {
        Object o = mMap.get(key);
        return (o instanceof Parcelable) ? (T) o : null;
    }

    public void putParcelableArray(String key, Parcelable[] value) {
        mMap.put(key, value);
    }

    public Parcelable[] getParcelableArray(String key) {
        Object o = mMap.get(key);
        return (o instanceof Parcelable[]) ? (Parcelable[]) o : null;
    }

    public void putParcelableArrayList(String key, ArrayList<? extends Parcelable> value) {
        mMap.put(key, value);
    }

    @SuppressWarnings("unchecked")
    public <T extends Parcelable> ArrayList<T> getParcelableArrayList(String key) {
        Object o = mMap.get(key);
        return (o instanceof ArrayList) ? (ArrayList<T>) o : null;
    }

    public void putSparseParcelableArray(String key, SparseArray<? extends Parcelable> value) {
        mMap.put(key, value);
    }

    @SuppressWarnings("unchecked")
    public <T extends Parcelable> SparseArray<T> getSparseParcelableArray(String key) {
        Object o = mMap.get(key);
        return (o instanceof SparseArray) ? (SparseArray<T>) o : null;
    }

    public void putBundle(String key, Bundle value) {
        mMap.put(key, value);
    }

    public Bundle getBundle(String key) {
        Object o = mMap.get(key);
        return (o instanceof Bundle) ? (Bundle) o : null;
    }

    public void putBinder(String key, IBinder value) {
        mMap.put(key, value);
    }

    public IBinder getBinder(String key) {
        Object o = mMap.get(key);
        return (o instanceof IBinder) ? (IBinder) o : null;
    }

    @Override
    public Object clone() {
        return new Bundle(this);
    }

    public Bundle deepCopy() {
        Bundle copy = new Bundle();
        copy.copyInternal(this, true);
        return copy;
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel parcel, int flags) {
        parcel.writeBundle(this);
    }

    public void readFromParcel(Parcel parcel) {
        Bundle b = parcel.readBundle();
        if (b != null) {
            mMap.clear();
            mMap.putAll(b.mMap);
        }
    }
}
