package android.webkit;

import android.graphics.Bitmap;

public class WebViewClient {
    public boolean shouldOverrideUrlLoading(WebView view, String url) { return false; }

    public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
        return shouldOverrideUrlLoading(view, request.getUrl().toString());
    }

    public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
        return shouldInterceptRequest(view, request.getUrl().toString());
    }

    public WebResourceResponse shouldInterceptRequest(WebView view, String url) {
        return null;
    }

    public void onPageStarted(WebView view, String url, Bitmap favicon) {}
    public void onPageFinished(WebView view, String url) {}
    public void onPageCommitVisible(WebView view, String url) {}
    public void onLoadResource(WebView view, String url) {}
    public void doUpdateVisitedHistory(WebView view, String url, boolean isReload) {}

    public void onReceivedError(WebView view, int errorCode, String description, String failingUrl) {}

    public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
        if (request.isForMainFrame()) {
            onReceivedError(view, error.getErrorCode(),
                            String.valueOf(error.getDescription()),
                            request.getUrl().toString());
        }
    }

    public void onReceivedHttpError(WebView view, WebResourceRequest request, WebResourceResponse errorResponse) {}

    public void onReceivedSslError(WebView view, SslErrorHandler handler, SslError error) {
        if (handler != null) handler.proceed();
    }

    public void onScaleChanged(WebView view, float oldScale, float newScale) {}
    public void onUnhandledKeyEvent(WebView view, android.view.KeyEvent event) {}
}
