package android.net;

/**
 * triển khai android.net.uri tối thiểu.
 *
 * đại diện cho một uri. đối với khuôn khổ tối thiểu của kudroid, chúng tôi lưu trữ dạng chuỗi
 * và cung cấp các trình trợ giúp phân tích cú pháp cơ bản.
 */
public final class Uri {
    private final String mString;

    private Uri(String s) {
        mString = s;
    }

    /**
     * phân tích cú pháp một uri từ một chuỗi.
     */
    public static Uri parse(String uriString) {
        return new Uri(uriString);
    }

    /**
     * trả về dạng chuỗi của uri này.
     */
    public String toString() {
        return mString;
    }

    /**
     * trả về giao thức (ví dụ: "http", "content").
     */
    public String getScheme() {
        if (mString == null) return null;
        int idx = mString.indexOf(':');
        return (idx > 0) ? mString.substring(0, idx) : null;
    }

    /**
     * trả về phần đường dẫn.
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
     * trả về phần truy vấn (không có '?').
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
