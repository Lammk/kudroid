package android.database;

/**
 * android.database.DataSetObserver — receive notifications when Adapter data changes.
 *
 * Android declares an abstract class with two empty functions; The subclass overrides the need.
 */
public abstract class DataSetObserver {
    public void onChanged() {
    }

    public void onInvalidated() {
    }
}
