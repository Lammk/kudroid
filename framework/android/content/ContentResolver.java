package android.content;

/**
 * Minimal android.content.ContentResolver implementation.
 *
 * Provides access to content providers. For KuDroid's minimal framework, this
 * is a stub that returns null/defaults.
 */
public class ContentResolver {
    private final Context mContext;

    public ContentResolver(Context context) {
        mContext = context;
    }

    /**
     * Return the context this resolver was created with.
     */
    public Context getContext() {
        return mContext;
    }

    /**
     * Query a content URI. Returns null for now.
     */
    public android.database.Cursor query(android.net.Uri uri, String[] projection,
                                         String selection, String[] selectionArgs,
                                         String sortOrder) {
        return null;
    }

    /**
     * Insert a row. Returns null for now.
     */
    public android.net.Uri insert(android.net.Uri url, android.content.ContentValues values) {
        return null;
    }

    /**
     * Delete rows. Returns 0 for now.
     */
    public int delete(android.net.Uri url, String where, String[] selectionArgs) {
        return 0;
    }

    /**
     * Update rows. Returns 0 for now.
     */
    public int update(android.net.Uri uri, android.content.ContentValues values,
                      String where, String[] selectionArgs) {
        return 0;
    }
}
