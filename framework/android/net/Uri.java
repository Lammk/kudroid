package android.net;

/**
 * Minimal android.net.Uri implementation.
 *
 * Represents a URI. For KuDroid's minimal framework, we store the string form
 * and provide basic parsing helpers.
 */
public final class Uri {
    private final String mString;

    private Uri(String s) {
        mString = s;
    }

    /**
     * Parse a URI from a string.
     */
    public static Uri parse(String uriString) {
        return new Uri(uriString);
    }

    /**
     * Return the string form of this URI.
     */
    public String toString() {
        return mString;
    }

    /**
     * Return the scheme (e.g. "http", "content").
     */
    public String getScheme() {
        if (mString == null) return null;
        int idx = mString.indexOf(':');
        return (idx > 0) ? mString.substring(0, idx) : null;
    }

    /**
     * Return the path portion.
     */
    public String getPath() {
        if (mString == null) return null;
        int schemeIdx = mString.indexOf(':');
        int start = (schemeIdx >= 0) ? schemeIdx + 1 : 0;
        int queryIdx = mString.indexOf('?', start);
        int end = (queryIdx >= 0) ? queryIdx : mString.length();
        return mString.substring(start, end);
    }

    /**
     * Return the query portion (without '?').
     */
    public String getQuery() {
        if (mString == null) return null;
        int queryIdx = mString.indexOf('?');
        return (queryIdx >= 0) ? mString.substring(queryIdx + 1) : null;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Uri)) return false;
        Uri other = (Uri) o;
        return mString == null ? other.mString == null : mString.equals(other.mString);
    }

    @Override
    public int hashCode() {
        return mString == null ? 0 : mString.hashCode();
    }
}
