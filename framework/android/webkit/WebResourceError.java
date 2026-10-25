package android.webkit;

public class WebResourceError {
    public static final int ERROR_UNKNOWN = -1;
    public static final int ERROR_HOST_LOOKUP = -2;
    public static final int ERROR_CONNECT = -6;
    public static final int ERROR_TIMEOUT = -8;
    public static final int ERROR_FAILED_SSL_HANDSHAKE = -11;
    public static final int ERROR_FILE_NOT_FOUND = -14;

    private final int mErrorCode;
    private final String mDescription;

    public WebResourceError() {
        this(ERROR_UNKNOWN, "");
    }

    public WebResourceError(int errorCode, String description) {
        mErrorCode = errorCode;
        mDescription = description != null ? description : "";
    }

    public int getErrorCode() { return mErrorCode; }
    public CharSequence getDescription() { return mDescription; }
}
