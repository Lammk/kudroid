package android.database;

import java.util.ArrayList;
import java.util.List;

/**
 * android.database.DataSetObservable — danh sách observer + hàm dispatch.
 */
public class DataSetObservable {
    protected final List<DataSetObserver> mObservers = new ArrayList<DataSetObserver>();

    public void registerObserver(DataSetObserver observer) {
        if (observer == null) throw new IllegalArgumentException("observer is null");
        synchronized (mObservers) {
            if (mObservers.contains(observer)) {
                throw new IllegalStateException("Observer " + observer + " is already registered.");
            }
            mObservers.add(observer);
        }
    }

    public void unregisterObserver(DataSetObserver observer) {
        if (observer == null) throw new IllegalArgumentException("observer is null");
        synchronized (mObservers) {
            mObservers.remove(observer);
        }
    }

    public void unregisterAll() {
        synchronized (mObservers) {
            mObservers.clear();
        }
    }

    /** Dispatch ngược để observer tự bỏ đăng ký trong callback vẫn an toàn. */
    public void notifyChanged() {
        synchronized (mObservers) {
            for (int i = mObservers.size() - 1; i >= 0; i--) {
                mObservers.get(i).onChanged();
            }
        }
    }

    public void notifyInvalidated() {
        synchronized (mObservers) {
            for (int i = mObservers.size() - 1; i >= 0; i--) {
                mObservers.get(i).onInvalidated();
            }
        }
    }
}
