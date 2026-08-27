package android.net;

/**
 * minimal android.net.uri implementation.
 *
 * represents a uri. for kudroid minimal framework we store as string
 * and provides basic parsing helpers.
 */
public final class Uri {
    private final String mString;

    private Uri(String s) {
        mString = s;
    }

    /**
     * parses a uri from a string.
     */
    public static Uri parse(String uriString) {
        return new Uri(uriString);
    }

    /**
     * returns the string form of this uri.
     */
    public String toString() {
        return mString;
    }

    /**
     * returns the protocol (e.g. "http", "content").
     */
    public String getScheme() {
        if (mString == null) return null;
        int idx = mString.indexOf(':');
        return (idx > 0) ? mString.substring(0, idx) : null;
    }

    /**
     * returns the path part.
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
     * returns the query part (without '?').
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

    public static class Builder {
        public Builder() {}
    }

}
