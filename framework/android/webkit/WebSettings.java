package android.webkit;

public class WebSettings {
    public static final int MIXED_CONTENT_ALWAYS_ALLOW = 0;
    public static final int MIXED_CONTENT_NEVER_ALLOW = 1;
    public static final int MIXED_CONTENT_COMPATIBILITY_MODE = 2;

    public static final int LOAD_DEFAULT = -1;
    public static final int LOAD_NORMAL = 0;
    public static final int LOAD_CACHE_ELSE_NETWORK = 1;
    public static final int LOAD_NO_CACHE = 2;
    public static final int LOAD_CACHE_ONLY = 3;

    private WebView mOwner;

    private boolean mJavaScriptEnabled = true;
    private boolean mDomStorageEnabled = true;
    private String mUserAgent = "Mozilla/5.0 (Linux; Android 10; Pixel 4) AppleWebKit/537.36";
    private boolean mAllowFileAccess = true;
    private boolean mAllowContentAccess = true;
    private boolean mLoadsImagesAutomatically = true;
    private boolean mBlockNetworkImage = false;
    private boolean mBlockNetworkLoads = false;
    private boolean mDatabaseEnabled = true;
    private boolean mGeolocationEnabled = true;
    private boolean mSupportZoom = true;
    private boolean mBuiltInZoomControls = true;
    private boolean mDisplayZoomControls = true;
    private boolean mUseWideViewPort = false;
    private boolean mLoadWithOverviewMode = false;
    private boolean mMediaPlaybackRequiresUserGesture = true;
    private boolean mJavaScriptCanOpenWindowsAutomatically = false;
    private boolean mSupportMultipleWindows = false;
    private boolean mSaveFormData = true;
    private int mMixedContentMode = MIXED_CONTENT_COMPATIBILITY_MODE;
    private int mCacheMode = LOAD_DEFAULT;
    private String mDefaultTextEncodingName = "UTF-8";

    public WebSettings() {}

    // Called by WebView so setting changes are pushed to the host view.
    void attach(WebView owner) { mOwner = owner; }

    private void changed() {
        if (mOwner != null) mOwner.onSettingsChanged();
    }

    public void setJavaScriptEnabled(boolean flag) { mJavaScriptEnabled = flag; changed(); }
    public boolean getJavaScriptEnabled() { return mJavaScriptEnabled; }
    public void setDomStorageEnabled(boolean flag) { mDomStorageEnabled = flag; changed(); }
    public boolean getDomStorageEnabled() { return mDomStorageEnabled; }
    public void setUserAgentString(String ua) { mUserAgent = ua; changed(); }
    public String getUserAgentString() { return mUserAgent; }
    public void setAllowFileAccess(boolean allow) { mAllowFileAccess = allow; }
    public boolean getAllowFileAccess() { return mAllowFileAccess; }
    public void setAllowContentAccess(boolean allow) { mAllowContentAccess = allow; }
    public boolean getAllowContentAccess() { return mAllowContentAccess; }
    public void setLoadsImagesAutomatically(boolean flag) { mLoadsImagesAutomatically = flag; }
    public boolean getLoadsImagesAutomatically() { return mLoadsImagesAutomatically; }
    public void setBlockNetworkImage(boolean flag) { mBlockNetworkImage = flag; }
    public boolean getBlockNetworkImage() { return mBlockNetworkImage; }
    public void setBlockNetworkLoads(boolean flag) { mBlockNetworkLoads = flag; }
    public boolean getBlockNetworkLoads() { return mBlockNetworkLoads; }
    public void setDatabaseEnabled(boolean flag) { mDatabaseEnabled = flag; }
    public boolean getDatabaseEnabled() { return mDatabaseEnabled; }
    public void setAppCacheEnabled(boolean flag) {}
    public void setAppCachePath(String appCachePath) {}
    public void setGeolocationEnabled(boolean flag) { mGeolocationEnabled = flag; }
    public boolean getGeolocationEnabled() { return mGeolocationEnabled; }
    public void setSupportZoom(boolean support) { mSupportZoom = support; }
    public boolean supportZoom() { return mSupportZoom; }
    public void setBuiltInZoomControls(boolean enabled) { mBuiltInZoomControls = enabled; }
    public boolean getBuiltInZoomControls() { return mBuiltInZoomControls; }
    public void setDisplayZoomControls(boolean enabled) { mDisplayZoomControls = enabled; }
    public boolean getDisplayZoomControls() { return mDisplayZoomControls; }
    public void setUseWideViewPort(boolean use) { mUseWideViewPort = use; }
    public boolean getUseWideViewPort() { return mUseWideViewPort; }
    public void setLoadWithOverviewMode(boolean overview) { mLoadWithOverviewMode = overview; }
    public boolean getLoadWithOverviewMode() { return mLoadWithOverviewMode; }
    public void setMediaPlaybackRequiresUserGesture(boolean require) { mMediaPlaybackRequiresUserGesture = require; changed(); }
    public boolean getMediaPlaybackRequiresUserGesture() { return mMediaPlaybackRequiresUserGesture; }
    public void setJavaScriptCanOpenWindowsAutomatically(boolean flag) { mJavaScriptCanOpenWindowsAutomatically = flag; }
    public boolean getJavaScriptCanOpenWindowsAutomatically() { return mJavaScriptCanOpenWindowsAutomatically; }
    public void setSupportMultipleWindows(boolean support) { mSupportMultipleWindows = support; }
    public boolean supportMultipleWindows() { return mSupportMultipleWindows; }
    public void setSaveFormData(boolean save) { mSaveFormData = save; }
    public boolean getSaveFormData() { return mSaveFormData; }
    public void setMixedContentMode(int mode) { mMixedContentMode = mode; }
    public int getMixedContentMode() { return mMixedContentMode; }
    public void setCacheMode(int mode) { mCacheMode = mode; }
    public int getCacheMode() { return mCacheMode; }
    public void setDefaultTextEncodingName(String name) { mDefaultTextEncodingName = name; }
    public String getDefaultTextEncodingName() { return mDefaultTextEncodingName; }
}
