package android.net;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

/**
 * An immutable URI reference, stored as its string form and split into components on demand:
 * {@code scheme://authority/path?query#fragment}.
 */
public final class Uri implements android.os.Parcelable, Comparable<Uri> {
    /** The empty URI. */
    public static final Uri EMPTY = new Uri("");

    public static final android.os.Parcelable.Creator<Uri> CREATOR =
            new android.os.Parcelable.Creator<Uri>() {
                @Override
                public Uri createFromParcel(android.os.Parcel in) {
                    String s = in.readString();
                    return s == null ? null : new Uri(s);
                }

                @Override
                public Uri[] newArray(int size) {
                    return new Uri[size];
                }
            };

    private final String mString;
    // Offsets into mString, computed once: scheme end (-1 if none), authority [start, end),
    // path [start, end), query start (-1 if none), fragment start (-1 if none).
    private int mSchemeEnd = -2;
    private int mAuthorityStart;
    private int mAuthorityEnd;
    private int mPathStart;
    private int mPathEnd;
    private int mQueryStart;
    private int mFragmentStart;

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(android.os.Parcel out, int flags) {
        out.writeString(mString);
    }

    private Uri(String s) {
        mString = s;
    }

    /** Parses a uri from a string. */
    public static Uri parse(String uriString) {
        if (uriString == null) throw new NullPointerException("uriString");
        return new Uri(uriString);
    }

    public static Uri fromFile(java.io.File file) {
        if (file == null) throw new NullPointerException("file");
        return new Uri("file://" + encode(file.getAbsolutePath(), "/"));
    }

    public static Uri fromParts(String scheme, String ssp, String fragment) {
        if (scheme == null) throw new NullPointerException("scheme");
        if (ssp == null) throw new NullPointerException("ssp");
        StringBuilder sb = new StringBuilder(scheme).append(':').append(encode(ssp, "/?:@&=+$,;"));
        if (fragment != null) sb.append('#').append(encode(fragment));
        return new Uri(sb.toString());
    }

    public static Uri withAppendedPath(Uri baseUri, String pathSegment) {
        return baseUri.buildUpon().appendEncodedPath(pathSegment).build();
    }

    public String toString() {
        return mString;
    }

    private void parseParts() {
        if (mSchemeEnd != -2) return;
        final String s = mString;
        final int n = s.length();
        int colon = -1;
        for (int i = 0; i < n; i++) {
            final char c = s.charAt(i);
            if (c == ':') {
                colon = i;
                break;
            }
            if (c == '/' || c == '?' || c == '#') break;
        }
        final int specificStart = colon >= 0 ? colon + 1 : 0;
        int fragment = s.indexOf('#', specificStart);
        final int limit = fragment >= 0 ? fragment : n;
        int query = s.indexOf('?', specificStart);
        if (query >= limit) query = -1;
        final int pathLimit = query >= 0 ? query : limit;
        int authorityStart = 0;
        int authorityEnd = 0;
        int pathStart = specificStart;
        if (s.startsWith("//", specificStart)) {
            authorityStart = specificStart + 2;
            int end = authorityStart;
            while (end < pathLimit && s.charAt(end) != '/') end++;
            authorityEnd = end;
            pathStart = end;
        }
        // An opaque URI ("mailto:a@b") has no path/query structure beyond its specific part.
        mAuthorityStart = authorityStart;
        mAuthorityEnd = authorityEnd;
        mPathStart = pathStart;
        mPathEnd = pathLimit;
        mQueryStart = query;
        mFragmentStart = fragment;
        mSchemeEnd = colon;
    }

    public String getScheme() {
        parseParts();
        return mSchemeEnd > 0 ? mString.substring(0, mSchemeEnd) : null;
    }

    public boolean isAbsolute() {
        return getScheme() != null;
    }

    public boolean isRelative() {
        return !isAbsolute();
    }

    public boolean isHierarchical() {
        parseParts();
        if (mSchemeEnd < 0) return true;
        return mSchemeEnd + 1 < mString.length() && mString.charAt(mSchemeEnd + 1) == '/';
    }

    public boolean isOpaque() {
        return !isHierarchical();
    }

    public String getSchemeSpecificPart() {
        parseParts();
        final int start = mSchemeEnd >= 0 ? mSchemeEnd + 1 : 0;
        final int end = mFragmentStart >= 0 ? mFragmentStart : mString.length();
        return decode(mString.substring(start, end));
    }

    public String getAuthority() {
        parseParts();
        return mAuthorityEnd > mAuthorityStart ? decode(mString.substring(mAuthorityStart, mAuthorityEnd)) : null;
    }

    public String getEncodedAuthority() {
        parseParts();
        return mAuthorityEnd > mAuthorityStart ? mString.substring(mAuthorityStart, mAuthorityEnd) : null;
    }

    public String getUserInfo() {
        final String authority = getEncodedAuthority();
        if (authority == null) return null;
        final int at = authority.lastIndexOf('@');
        return at >= 0 ? decode(authority.substring(0, at)) : null;
    }

    public String getHost() {
        final String authority = getEncodedAuthority();
        if (authority == null) return null;
        String host = authority.substring(authority.lastIndexOf('@') + 1);
        if (host.startsWith("[")) {
            final int close = host.indexOf(']');
            return close > 0 ? host.substring(0, close + 1) : host;
        }
        final int colon = host.indexOf(':');
        return decode(colon >= 0 ? host.substring(0, colon) : host);
    }

    public int getPort() {
        final String authority = getEncodedAuthority();
        if (authority == null) return -1;
        String host = authority.substring(authority.lastIndexOf('@') + 1);
        final int colon = host.lastIndexOf(':');
        if (colon < 0 || host.indexOf(']') > colon) return -1;
        try {
            return Integer.parseInt(host.substring(colon + 1));
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    public String getEncodedPath() {
        parseParts();
        if (!isHierarchical()) return null;
        return mString.substring(mPathStart, mPathEnd);
    }

    public String getPath() {
        final String path = getEncodedPath();
        return path == null ? null : decode(path);
    }

    public List<String> getPathSegments() {
        final String path = getEncodedPath();
        if (path == null || path.isEmpty()) return Collections.<String>emptyList();
        final ArrayList<String> segments = new ArrayList<String>();
        int start = path.startsWith("/") ? 1 : 0;
        while (start <= path.length()) {
            int end = path.indexOf('/', start);
            if (end < 0) end = path.length();
            if (end > start || end < path.length()) {
                // An empty segment between slashes is kept; a trailing slash adds nothing.
                if (!(end == path.length() && end == start)) segments.add(decode(path.substring(start, end)));
            }
            start = end + 1;
        }
        return Collections.unmodifiableList(segments);
    }

    public String getLastPathSegment() {
        final List<String> segments = getPathSegments();
        return segments.isEmpty() ? null : segments.get(segments.size() - 1);
    }

    public String getEncodedQuery() {
        parseParts();
        if (mQueryStart < 0) return null;
        return mString.substring(mQueryStart + 1, mFragmentStart >= 0 ? mFragmentStart : mString.length());
    }

    public String getQuery() {
        final String query = getEncodedQuery();
        return query == null ? null : decode(query);
    }

    public String getEncodedFragment() {
        parseParts();
        return mFragmentStart >= 0 ? mString.substring(mFragmentStart + 1) : null;
    }

    public String getFragment() {
        final String fragment = getEncodedFragment();
        return fragment == null ? null : decode(fragment);
    }

    public Set<String> getQueryParameterNames() {
        final String query = getEncodedQuery();
        if (query == null) return Collections.<String>emptySet();
        final LinkedHashSet<String> names = new LinkedHashSet<String>();
        int start = 0;
        do {
            int next = query.indexOf('&', start);
            int end = next < 0 ? query.length() : next;
            int separator = query.indexOf('=', start);
            if (separator > end || separator < 0) separator = end;
            names.add(decode(query.substring(start, separator)));
            start = end + 1;
        } while (start < query.length());
        return Collections.unmodifiableSet(names);
    }

    public List<String> getQueryParameters(String key) {
        if (key == null) throw new NullPointerException("key");
        final String query = getEncodedQuery();
        if (query == null) return Collections.<String>emptyList();
        final String encodedKey = encode(key, null);
        final ArrayList<String> values = new ArrayList<String>();
        int start = 0;
        do {
            int ampersand = query.indexOf('&', start);
            int end = ampersand < 0 ? query.length() : ampersand;
            int separator = query.indexOf('=', start);
            if (separator > end || separator < 0) separator = end;
            if (separator - start == encodedKey.length() && query.regionMatches(start, encodedKey, 0, encodedKey.length())) {
                values.add(separator == end ? "" : decode(query.substring(separator + 1, end)));
            }
            start = end + 1;
        } while (start < query.length());
        return Collections.unmodifiableList(values);
    }

    public String getQueryParameter(String key) {
        final List<String> values = getQueryParameters(key);
        return values.isEmpty() ? null : values.get(0);
    }

    public boolean getBooleanQueryParameter(String key, boolean defaultValue) {
        final String flag = getQueryParameter(key);
        if (flag == null) return defaultValue;
        final String lower = flag.toLowerCase();
        return !"false".equals(lower) && !"0".equals(lower);
    }

    public Uri normalizeScheme() {
        final String scheme = getScheme();
        if (scheme == null) return this;
        final String lower = scheme.toLowerCase();
        return scheme.equals(lower) ? this : new Uri(lower + mString.substring(scheme.length()));
    }

    public Builder buildUpon() {
        parseParts();
        final Builder builder = new Builder();
        builder.mScheme = getScheme();
        builder.mAuthority = getEncodedAuthority();
        builder.mPath = isHierarchical() ? getEncodedPath() : null;
        builder.mQuery = getEncodedQuery();
        builder.mFragment = getEncodedFragment();
        if (!isHierarchical()) builder.mOpaque = mString.substring(mSchemeEnd + 1,
                mFragmentStart >= 0 ? mFragmentStart : mString.length());
        return builder;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Uri)) return false;
        return mString.equals(((Uri) o).mString);
    }

    @Override
    public int hashCode() {
        return mString.hashCode();
    }

    @Override
    public int compareTo(Uri other) {
        return mString.compareTo(other.mString);
    }

    // Percent-encoding (RFC 3986 unreserved set).

    /** Percent-encodes everything outside the unreserved set. */
    public static String encode(String s) {
        return encode(s, null);
    }

    /**
     * Percent-encodes `s`, leaving the characters in `allow` alone as well.
     *
     * The two-argument form is what makes the function usable for a path: passing "/" keeps
     * the separators while still escaping the segments.
     */
    public static String encode(String s, String allow) {
        if (s == null) {
            return null;
        }
        StringBuilder out = new StringBuilder(s.length());
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (isUnreserved(c) || (allow != null && allow.indexOf(c) >= 0)) {
                out.append(c);
                continue;
            }
            // Encode the UTF-8 bytes, not the char: a non-ASCII character is more than one
            // byte and encoding the code unit would produce a sequence no decoder accepts.
            appendPercentEncoded(out, s.substring(i, i + 1));
        }
        return out.toString();
    }

    /** Inverse of encode. Malformed input is passed through rather than throwing. */
    public static String decode(String s) {
        if (s == null) {
            return null;
        }
        byte[] buf = new byte[s.length()];
        int len = 0;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '%' && i + 2 < s.length()) {
                final int hi = hexValue(s.charAt(i + 1));
                final int lo = hexValue(s.charAt(i + 2));
                if (hi >= 0 && lo >= 0) {
                    buf[len++] = (byte) ((hi << 4) | lo);
                    i += 2;
                    continue;
                }
                // Not a valid escape. Keeping the '%' is what the platform does; dropping it
                // would silently corrupt a string that merely contains a percent sign.
            }
            if (c == '+') {
                buf[len++] = (byte) ' ';
                continue;
            }
            byte[] bytes = utf8Bytes(s.substring(i, i + 1));
            for (int k = 0; k < bytes.length && len < buf.length; k++) {
                buf[len++] = bytes[k];
            }
        }
        byte[] exact = new byte[len];
        for (int i = 0; i < len; i++) {
            exact[i] = buf[i];
        }
        return new String(exact);
    }

    private static boolean isUnreserved(char c) {
        if (c >= 'A' && c <= 'Z') return true;
        if (c >= 'a' && c <= 'z') return true;
        if (c >= '0' && c <= '9') return true;
        return "_-!.~'()*".indexOf(c) >= 0;
    }

    private static void appendPercentEncoded(StringBuilder out, String piece) {
        byte[] bytes = utf8Bytes(piece);
        for (int i = 0; i < bytes.length; i++) {
            final int b = bytes[i] & 0xFF;
            out.append('%');
            out.append(HEX[(b >> 4) & 0xF]);
            out.append(HEX[b & 0xF]);
        }
    }

    private static byte[] utf8Bytes(String s) {
        // String.getBytes() on this runtime already produces UTF-8; going through it keeps
        // one encoder rather than a second implementation that can disagree.
        return s.getBytes();
    }

    private static int hexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    private static final char[] HEX = {
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
    };

    /** Builds a Uri from encoded components. */
    public static class Builder {
        String mScheme;
        String mAuthority;
        String mPath;
        String mQuery;
        String mFragment;
        String mOpaque;

        public Builder() {}

        public Builder scheme(String scheme) {
            mScheme = scheme;
            return this;
        }

        public Builder opaquePart(String opaque) {
            mOpaque = encode(opaque, "/?:@&=+$,;");
            return this;
        }

        public Builder encodedOpaquePart(String opaque) {
            mOpaque = opaque;
            return this;
        }

        public Builder authority(String authority) {
            mAuthority = authority == null ? null : encode(authority, "@:[]");
            return this;
        }

        public Builder encodedAuthority(String authority) {
            mAuthority = authority;
            return this;
        }

        public Builder path(String path) {
            mPath = path == null ? null : encode(path, "/");
            return this;
        }

        public Builder encodedPath(String path) {
            mPath = path;
            return this;
        }

        public Builder appendPath(String newSegment) {
            return appendEncodedPath(encode(newSegment, null));
        }

        public Builder appendEncodedPath(String newSegment) {
            if (newSegment == null) throw new NullPointerException("newSegment");
            final String old = mPath == null ? "" : mPath;
            if (old.endsWith("/") && newSegment.startsWith("/")) {
                mPath = old + newSegment.substring(1);
            } else if (!old.isEmpty() && !old.endsWith("/") && !newSegment.startsWith("/")) {
                mPath = old + "/" + newSegment;
            } else {
                mPath = old + newSegment;
            }
            return this;
        }

        public Builder query(String query) {
            mQuery = query == null ? null : encode(query, "=&");
            return this;
        }

        public Builder encodedQuery(String query) {
            mQuery = query;
            return this;
        }

        public Builder fragment(String fragment) {
            mFragment = fragment == null ? null : encode(fragment, null);
            return this;
        }

        public Builder encodedFragment(String fragment) {
            mFragment = fragment;
            return this;
        }

        public Builder appendQueryParameter(String key, String value) {
            final String pair = encode(key, null) + "=" + encode(value, null);
            mQuery = mQuery == null || mQuery.isEmpty() ? pair : mQuery + "&" + pair;
            return this;
        }

        public Builder clearQuery() {
            mQuery = null;
            return this;
        }

        public Uri build() {
            final StringBuilder sb = new StringBuilder();
            if (mScheme != null) sb.append(mScheme).append(':');
            if (mOpaque != null) {
                sb.append(mOpaque);
            } else {
                if (mAuthority != null) sb.append("//").append(mAuthority);
                if (mPath != null) {
                    if (mAuthority != null && !mPath.isEmpty() && !mPath.startsWith("/")) sb.append('/');
                    sb.append(mPath);
                }
                if (mQuery != null) sb.append('?').append(mQuery);
            }
            if (mFragment != null) sb.append('#').append(mFragment);
            return new Uri(sb.toString());
        }

        @Override
        public String toString() {
            return build().toString();
        }
    }

}
