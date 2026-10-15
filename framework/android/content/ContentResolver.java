package android.content;

/**
 * minimal android.content.contentresolver implementation.
 *
 * provides access to content providers. for kudroid minimal framework,
 *this is a simulation that returns null/default.
 */
public class ContentResolver {
    private final Context mContext;

    /**
     * The process-wide resolver.
     *
     * One instance, not one per Context, because observers are registered through whichever
     * resolver an app happens to hold and expected to fire regardless of which Context it
     * came from. Per-context instances would each keep their own observer list and drop
     * every notification registered through a different one.
     */
    private static ContentResolver sInstance;

    public static synchronized ContentResolver getInstance() {
        if (sInstance == null) sInstance = new ContentResolver(null);
        return sInstance;
    }

    public ContentResolver(Context context) {
        mContext = context;
    }

    // Observers, kept so registration succeeds and unregistration is symmetric.
    //
    // Nothing here ever notifies them: KuDroid has no content providers, so no data changes
    // to report. Storing them anyway means an app can register, unregister, and re-register
    // without its bookkeeping diverging from the resolver's — and it leaves one place to
    // notify from if providers ever arrive.
    private final java.util.List<android.database.ContentObserver> mObservers =
            new java.util.ArrayList<android.database.ContentObserver>();

    public void registerContentObserver(android.net.Uri uri, boolean notifyForDescendants,
                                       android.database.ContentObserver observer) {
        if (observer == null) return;
        synchronized (mObservers) {
            if (!mObservers.contains(observer)) mObservers.add(observer);
        }
    }

    public void unregisterContentObserver(android.database.ContentObserver observer) {
        if (observer == null) return;
        synchronized (mObservers) {
            mObservers.remove(observer);
        }
    }

    /**
     * Tell registered observers that `uri` changed.
     *
     * Present because an app may notify its own observers even with no provider involved,
     * which is a pattern in code that shares one observer between real and synthetic data.
     */
    public void notifyChange(android.net.Uri uri, android.database.ContentObserver observer) {
        java.util.List<android.database.ContentObserver> copy;
        synchronized (mObservers) {
            copy = new java.util.ArrayList<android.database.ContentObserver>(mObservers);
        }
        for (android.database.ContentObserver o : copy) {
            if (o == observer) continue;  // self-notification is opt-in on Android
            o.dispatchChange(false, uri);
        }
    }

    public void notifyChange(android.net.Uri uri, android.database.ContentObserver observer,
                             boolean syncToNetwork) {
        notifyChange(uri, observer);
    }

    /**
     * returns the context in which this resolver was created.
     */
    public Context getContext() {
        return mContext;
    }

    /**
     * query a content uri. currently returns null.
     */
    public android.database.Cursor query(android.net.Uri uri, String[] projection,
                                         String selection, String[] selectionArgs,
                                         String sortOrder) {
        return null;
    }

    /**
     * insert a row. currently returns null.
     */
    public android.net.Uri insert(android.net.Uri url, android.content.ContentValues values) {
        return null;
    }

    /**
     * delete rows. currently returns 0.
     */
    public int delete(android.net.Uri url, String where, String[] selectionArgs) {
        return 0;
    }

    /**
     * update rows. currently returns 0.
     */
    public int update(android.net.Uri uri, android.content.ContentValues values,
                      String where, String[] selectionArgs) {
        return 0;
    }
}
