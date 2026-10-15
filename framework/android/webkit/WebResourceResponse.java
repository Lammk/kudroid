package android.webkit;

import java.io.InputStream;
import java.util.Map;

public class WebResourceResponse {
    private String mMimeType;
    private String mEncoding;
    private int mStatusCode = 200;
    private String mReasonPhrase = "OK";
    private InputStream mData;

    public WebResourceResponse(String mimeType, String encoding, InputStream data) {
        mMimeType = mimeType;
        mEncoding = encoding;
        mData = data;
    }
    public String getMimeType() { return mMimeType; }
    public String getEncoding() { return mEncoding; }
    public int getStatusCode() { return mStatusCode; }
    public String getReasonPhrase() { return mReasonPhrase; }
    public InputStream getData() { return mData; }
}
