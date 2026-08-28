package android.webkit;

import android.graphics.Bitmap;
import android.view.View;

public class WebChromeClient {
    public void onProgressChanged(WebView view, int newProgress) {}
    public void onReceivedTitle(WebView view, String title) {}
    public void onReceivedIcon(WebView view, Bitmap icon) {}
    public void onShowCustomView(View view, CustomViewCallback callback) {}
    public void onHideCustomView() {}
    public boolean onConsoleMessage(ConsoleMessage consoleMessage) { return false; }

    public interface CustomViewCallback {
        void onCustomViewHidden();
    }
}
