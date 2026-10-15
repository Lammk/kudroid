package android.webkit;

import android.content.Context;
import android.widget.FrameLayout;
import java.util.Map;

public class WebView extends FrameLayout {
    private WebSettings mSettings = new WebSettings();
    private WebViewClient mClient = new WebViewClient();
    private WebChromeClient mChromeClient = new WebChromeClient();
    private String mUrl = "";

    public WebView(Context context) { super(context); }
    public void loadUrl(String url) { this.mUrl = url; }
    public void loadUrl(String url, Map<String, String> additionalHttpHeaders) { this.mUrl = url; }
    public void loadData(String data, String mimeType, String encoding) {}
    public void loadDataWithBaseURL(String baseUrl, String data, String mimeType, String encoding, String historyUrl) {}
    public String getUrl() { return mUrl; }
    public WebSettings getSettings() { return mSettings; }
    public void setWebViewClient(WebViewClient client) { this.mClient = client != null ? client : new WebViewClient(); }
    public WebViewClient getWebViewClient() { return mClient; }
    public void setWebChromeClient(WebChromeClient client) { this.mChromeClient = client != null ? client : new WebChromeClient(); }
    public WebChromeClient getWebChromeClient() { return mChromeClient; }
    public void addJavascriptInterface(Object object, String name) {}
    public void removeJavascriptInterface(String name) {}
    public void evaluateJavascript(String script, ValueCallback<String> resultCallback) { if (resultCallback != null) resultCallback.onReceiveValue(""); }
    public void reload() {}
    public boolean canGoBack() { return false; }
    public void goBack() {}
    public boolean canGoForward() { return false; }
    public void goForward() {}
    public void stopLoading() {}
    public void destroy() {}
}
