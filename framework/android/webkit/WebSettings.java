package android.webkit;

public class WebSettings {
    private boolean mJavaScriptEnabled = true;
    private boolean mDomStorageEnabled = true;
    private String mUserAgent = "Mozilla/5.0 (Linux; Android 10; Pixel 4)";

    public WebSettings() {}
    public void setJavaScriptEnabled(boolean flag) { mJavaScriptEnabled = flag; }
    public boolean getJavaScriptEnabled() { return mJavaScriptEnabled; }
    public void setDomStorageEnabled(boolean flag) { mDomStorageEnabled = flag; }
    public boolean getDomStorageEnabled() { return mDomStorageEnabled; }
    public void setUserAgentString(String ua) { mUserAgent = ua; }
    public String getUserAgentString() { return mUserAgent; }
    public void setAllowFileAccess(boolean allow) {}
    public void setAllowContentAccess(boolean allow) {}
    public void setLoadsImagesAutomatically(boolean flag) {}
    public void setBlockNetworkImage(boolean flag) {}
    public void setBlockNetworkLoads(boolean flag) {}
    public void setDatabaseEnabled(boolean flag) {}
    public void setAppCacheEnabled(boolean flag) {}
    public void setAppCachePath(String appCachePath) {}
    public void setGeolocationEnabled(boolean flag) {}
    public void setSupportZoom(boolean support) {}
    public void setBuiltInZoomControls(boolean enabled) {}
    public void setDisplayZoomControls(boolean enabled) {}
    public void setUseWideViewPort(boolean use) {}
    public void setLoadWithOverviewMode(boolean overview) {}
    public void setMediaPlaybackRequiresUserGesture(boolean require) {}
    public void setMixedContentMode(int mode) {}
}
