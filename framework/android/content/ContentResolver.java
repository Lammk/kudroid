package android.content;

/**
 * minimal android.content.contentresolver implementation.
 *
 * provides access to content providers. for kudroid minimal framework,
 *this is a simulation that returns null/default.
 */
public class ContentResolver {
    private final Context mContext;

    public ContentResolver(Context context) {
        mContext = context;
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
