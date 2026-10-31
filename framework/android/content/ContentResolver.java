package android.content;

/**
 * minimal android.content.contentresolver implementation.
 *
 * provides access to content providers. for kudroid minimal framework,
 *this is a simulation that returns null/default.
 */
public class ContentResolver {
    public static final String SCHEME_CONTENT = "content";
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

    public final ContentProviderClient acquireContentProviderClient(android.net.Uri uri) {
        if (uri == null) throw new NullPointerException("uri");
        if (!SCHEME_CONTENT.equals(uri.getScheme()) || uri.getAuthority() == null) return null;
        return acquireContentProviderClient(uri.getAuthority());
    }

    public final ContentProviderClient acquireContentProviderClient(String name) {
        if (name == null) throw new NullPointerException("name");
        // No content provider is registered.
        return null;
    }

    public final ContentProviderClient acquireUnstableContentProviderClient(android.net.Uri uri) {
        if (uri == null) throw new NullPointerException("uri");
        if (!SCHEME_CONTENT.equals(uri.getScheme()) || uri.getAuthority() == null) return null;
        return acquireUnstableContentProviderClient(uri.getAuthority());
    }

    public final ContentProviderClient acquireUnstableContentProviderClient(String name) {
        if (name == null) throw new NullPointerException("name");
        // No content provider is registered.
        return null;
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

    public android.content.res.AssetFileDescriptor openAssetFileDescriptor(
            android.net.Uri uri, String mode) throws java.io.FileNotFoundException {
        if (uri == null) {
            throw new java.io.FileNotFoundException("null uri");
        }
        final String scheme = uri.getScheme();
        final String path = uri.getPath();

        if (path != null && path.startsWith("/android_asset/")) {
            String sub = path.substring("/android_asset/".length());
            Context ctx = mContext != null ? mContext : (sInstance != null ? sInstance.mContext : null);
            if (ctx != null) {
                try {
                    return ctx.getAssets().openFd(sub);
                } catch (java.io.IOException ignored) {}
            }
        }

        if ("file".equals(scheme) || scheme == null) {
            if (path == null) {
                throw new java.io.FileNotFoundException("No path for URI: " + uri);
            }
            java.io.File file = new java.io.File(path);
            android.os.ParcelFileDescriptor pfd = android.os.ParcelFileDescriptor.open(
                    file, android.os.ParcelFileDescriptor.parseMode(mode));
            return new android.content.res.AssetFileDescriptor(pfd, 0, file.length());
        }

        if ("android.resource".equals(scheme)) {
            Context ctx = mContext != null ? mContext : (sInstance != null ? sInstance.mContext : null);
            if (ctx != null && path != null && path.length() > 1) {
                try {
                    int resId = Integer.parseInt(path.substring(1));
                    return ctx.getResources().openRawResourceFd(resId);
                } catch (NumberFormatException ignored) {}
            }
        }

        return null;
    }

    public android.content.res.AssetFileDescriptor openAssetFileDescriptor(
            android.net.Uri uri, String mode, android.os.CancellationSignal cancellationSignal)
            throws java.io.FileNotFoundException {
        return openAssetFileDescriptor(uri, mode);
    }

    public android.os.ParcelFileDescriptor openFileDescriptor(
            android.net.Uri uri, String mode) throws java.io.FileNotFoundException {
        android.content.res.AssetFileDescriptor afd = openAssetFileDescriptor(uri, mode);
        return afd != null ? afd.getParcelFileDescriptor() : null;
    }

    public android.os.ParcelFileDescriptor openFileDescriptor(
            android.net.Uri uri, String mode, android.os.CancellationSignal cancellationSignal)
            throws java.io.FileNotFoundException {
        return openFileDescriptor(uri, mode);
    }

    public java.io.InputStream openInputStream(android.net.Uri uri) throws java.io.FileNotFoundException {
        if (uri == null) {
            throw new java.io.FileNotFoundException("null uri");
        }
        android.content.res.AssetFileDescriptor afd = openAssetFileDescriptor(uri, "r");
        if (afd != null) {
            try {
                return afd.createInputStream();
            } catch (java.io.IOException e) {
                throw new java.io.FileNotFoundException("Unable to create stream: " + e.getMessage());
            }
        }
        return null;
    }

    public java.io.OutputStream openOutputStream(android.net.Uri uri) throws java.io.FileNotFoundException {
        return openOutputStream(uri, "wa");
    }

    public java.io.OutputStream openOutputStream(android.net.Uri uri, String mode)
            throws java.io.FileNotFoundException {
        if (uri == null) {
            throw new java.io.FileNotFoundException("null uri");
        }
        android.content.res.AssetFileDescriptor afd = openAssetFileDescriptor(uri, mode);
        if (afd != null) {
            try {
                return afd.createOutputStream();
            } catch (java.io.IOException e) {
                throw new java.io.FileNotFoundException("Unable to create stream: " + e.getMessage());
            }
        }
        return null;
    }

    public String getType(android.net.Uri uri) {
        return null;
    }
}
