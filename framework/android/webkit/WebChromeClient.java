package android.webkit;

import android.graphics.Bitmap;
import android.net.Uri;
import android.view.View;
import android.webkit.WebView;

public class WebChromeClient {
    public void onProgressChanged(WebView view, int newProgress) {}
    public void onReceivedTitle(WebView view, String title) {}
    public void onReceivedIcon(WebView view, Bitmap icon) {}
    public void onShowCustomView(View view, CustomViewCallback callback) {}
    public void onHideCustomView() {}
    public boolean onConsoleMessage(ConsoleMessage consoleMessage) { return false; }

    public void onGeolocationPermissionsShowPrompt(String origin, GeolocationPermissions.Callback callback) {
        if (callback != null) callback.invoke(origin, false, false);
    }

    public void onGeolocationPermissionsHidePrompt() {}

    public void onPermissionRequest(PermissionRequest request) {
        if (request != null) request.deny();
    }

    public void onPermissionRequestCanceled(PermissionRequest request) {}

    public boolean onShowFileChooser(WebView webView, ValueCallback<Uri[]> filePathCallback,
                                     FileChooserParams fileChooserParams) {
        return false;
    }

    public boolean onJsAlert(WebView view, String url, String message,
                             JsResult result) {
        if (result != null) result.confirm();
        return false;
    }

    public boolean onJsConfirm(WebView view, String url, String message,
                               JsResult result) {
        if (result != null) result.cancel();
        return false;
    }

    public boolean onJsPrompt(WebView view, String url, String message,
                              String defaultValue, JsPromptResult result) {
        if (result != null) result.cancel();
        return false;
    }

    public interface CustomViewCallback {
        void onCustomViewHidden();
    }

    public static class FileChooserParams {
        public static final int MODE_OPEN = 0;
        public static final int MODE_OPEN_MULTIPLE = 1;
        public static final int MODE_SAVE = 3;

        public int getMode() { return MODE_OPEN; }
        public String[] getAcceptTypes() { return new String[]{"*/*"}; }
        public boolean isCaptureEnabled() { return false; }
        public CharSequence getTitle() { return null; }
        public String getFilenameHint() { return null; }
    }
}
