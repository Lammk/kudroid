package android.database;

/**
 * android.database.DataSetObservable — observer list + dispatch function.
 *
 * The generic base intentionally stays in the erased descriptor: app code compiled
 * against Android's DataSetObservable expects registerObserver(Object).
 */
public class DataSetObservable extends Observable<DataSetObserver> {
    /** Reverse dispatch so an observer can unregister itself in the callback. */
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
