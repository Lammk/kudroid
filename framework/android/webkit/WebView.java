package android.webkit;

import android.content.Context;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.widget.FrameLayout;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * WebView backed by the platform web view (WKWebView on iOS) through the
 * WebViewHost bridge (include/kudroid/platform/WebViewHost.h).
 *
 * This is the "hybrid app" surface: Tauri, Cordova, Capacitor and plain
 * WebView apps drive everything through this class. The Java side owns all
 * Android-facing state and client dispatch; the host owns rendering.
 *
 * Host -> guest events arrive on a poll loop posted to the main Looper, so
 * WebViewClient/WebChromeClient callbacks always run on the guest main
 * thread, like real Android. Event wire format (produced by WebViewHost):
 * "<type>\t<field>\t..." with the type codes mirrored below.
 */
public class WebView extends FrameLayout {
    // KuDroidWebViewEvent codes (WebViewHost.h).
    private static final int EV_PAGE_STARTED = 1;
    private static final int EV_PAGE_FINISHED = 2;
    private static final int EV_TITLE = 3;
    private static final int EV_PROGRESS = 4;
    private static final int EV_EVAL_RESULT = 5;
    private static final int EV_JS_MESSAGE = 6;
    private static final int EV_INTERCEPT = 7;
    private static final int EV_GEO_PROMPT = 8;
    private static final int EV_CONSOLE = 9;
    private static final int EV_NAV_STATE = 10;
    private static final int EV_PERMISSION = 11;
    private static final int EV_FILE_CHOOSER = 12;

    private static final AtomicLong NEXT_HANDLE = new AtomicLong(1);

    private final long mHandle = NEXT_HANDLE.getAndIncrement();
    private final WebSettings mSettings = new WebSettings();
    private WebViewClient mClient = new WebViewClient();
    private WebChromeClient mChromeClient = new WebChromeClient();
    private final Map<String, Object> mJsInterfaces = new HashMap<>();
    private final Map<Long, ValueCallback<String>> mEvalCallbacks = new HashMap<>();
    private final AtomicLong mNextEvalId = new AtomicLong(1);
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    private boolean mDestroyed = false;
    private String mUrl = "";
    private String mTitle = "";
    private int mProgress = 0;

    private final Runnable mPollTask = new Runnable() {
        @Override public void run() {
            if (mDestroyed) return;
            drainEvents();
            mMainHandler.postDelayed(this, 16);
        }
    };

    public WebView(Context context) {
        super(context);
        mSettings.attach(this);
        nativeCreate(mHandle, buildConfigJson());
        updateInterceptEnabled();
        mMainHandler.post(mPollTask);
    }

    // ------------------------------------------------------------------ API

    public void loadUrl(String url) {
        if (url == null) return;
        mUrl = url;
        nativeLoadUrl(mHandle, url);
    }

    public void loadUrl(String url, Map<String, String> additionalHttpHeaders) {
        // Header-carrying loads are rare in hybrid runtimes; the host bridge
        // has no header channel yet, so headers are dropped (logged by the
        // host in debug builds). Tracked in docs/sorayori-rust-beta-plan.md.
        loadUrl(url);
    }

    public void postUrl(String url, byte[] postData) {
        // POST navigations are not supported by the host bridge yet; treat
        // as a plain navigation so apps degrade instead of hanging.
        loadUrl(url);
    }

    public void loadData(String data, String mimeType, String encoding) {
        nativeLoadData(mHandle, data != null ? data : "", mimeType, null);
    }

    public void loadDataWithBaseURL(String baseUrl, String data, String mimeType,
                                    String encoding, String historyUrl) {
        nativeLoadData(mHandle, data != null ? data : "", mimeType, baseUrl);
    }

    public String getUrl() {
        String url = nativeGetUrl(mHandle);
        return url != null ? url : mUrl;
    }

    public String getOriginalUrl() { return getUrl(); }
    public String getTitle() { return mTitle; }
    public int getProgress() { return mProgress; }

    public WebSettings getSettings() { return mSettings; }

    public void setWebViewClient(WebViewClient client) {
        mClient = client != null ? client : new WebViewClient();
        updateInterceptEnabled();
    }

    public WebViewClient getWebViewClient() { return mClient; }

    public void setWebChromeClient(WebChromeClient client) {
        mChromeClient = client != null ? client : new WebChromeClient();
    }

    public WebChromeClient getWebChromeClient() { return mChromeClient; }

    public void addJavascriptInterface(Object object, String name) {
        if (object == null || name == null) return;
        mJsInterfaces.put(name, object);
        // Define the JS-side proxy: window.<name>.<method>(...args) posts a
        // message to the host channel; the host forwards it as EV_JS_MESSAGE
        // and invokeJsInterface() runs the method here on the main thread.
        // Android's addJavascriptInterface is synchronous; this bridge is
        // message-based, so return values are NOT available to JS (documented
        // limitation - runtimes that need sync replies use URL interception).
        String script =
            "(function(){if(window[" + jsQuote(name) + "])return;" +
            "window[" + jsQuote(name) + "]=new Proxy({},{get:function(t,p){" +
            "if(typeof p!=='string')return undefined;" +
            "return function(){var a=[];for(var i=0;i<arguments.length;i++)a.push(arguments[i]);" +
            "window.webkit.messageHandlers.kudroid_jsi.postMessage(JSON.stringify(" +
            "{iface:" + jsQuote(name) + ",method:p,args:a}));};}});})();";
        nativeEval(mHandle, 0, script);
    }

    public void removeJavascriptInterface(String name) {
        mJsInterfaces.remove(name);
        nativeEval(mHandle, 0, "try{delete window[" + jsQuote(name) + "]}catch(e){}");
    }

    public void evaluateJavascript(String script, ValueCallback<String> resultCallback) {
        if (script == null) {
            if (resultCallback != null) resultCallback.onReceiveValue("null");
            return;
        }
        long evalId = 0;
        if (resultCallback != null) {
            evalId = mNextEvalId.getAndIncrement();
            mEvalCallbacks.put(evalId, resultCallback);
        }
        nativeEval(mHandle, evalId, script);
    }

    public void reload() { nativeReload(mHandle); }
    public boolean canGoBack() { return nativeCanGoBack(mHandle); }
    public void goBack() { nativeGoBack(mHandle); }
    public boolean canGoForward() { return nativeCanGoForward(mHandle); }
    public void goForward() { nativeGoForward(mHandle); }
    public void stopLoading() { nativeStopLoading(mHandle); }
    public void onPause() { nativeSetVisible(mHandle, false); }
    public void onResume() { nativeSetVisible(mHandle, true); }

    public void destroy() {
        if (mDestroyed) return;
        mDestroyed = true;
        mMainHandler.removeCallbacks(mPollTask);
        mEvalCallbacks.clear();
        mJsInterfaces.clear();
        nativeDestroy(mHandle);
    }

    // ------------------------------------------------------------- View glue

    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        nativeSetVisible(mHandle, true);
        nativeSetFrame(mHandle, 0, 0, getWidth(), getHeight());
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        nativeSetFrame(mHandle, 0, 0, w, h);
    }

    @Override public void setVisibility(int visibility) {
        super.setVisibility(visibility);
        nativeSetVisible(mHandle, visibility == VISIBLE);
    }

    // -------------------------------------------------------- host event pump

    private void drainEvents() {
        String ev;
        int guard = 0;
        while ((ev = nativePollEvent(mHandle)) != null && guard++ < 64) {
            dispatchHostEvent(ev);
        }
    }

    private void dispatchHostEvent(String ev) {
        int tab = ev.indexOf('\t');
        if (tab < 0) return;
        int type;
        try {
            type = Integer.parseInt(ev.substring(0, tab));
        } catch (NumberFormatException e) {
            return;
        }
        String rest = ev.substring(tab + 1);
        switch (type) {
            case EV_PAGE_STARTED: {
                mUrl = rest;
                mClient.onPageStarted(this, rest, null);
                break;
            }
            case EV_PAGE_FINISHED: {
                mUrl = rest;
                mClient.onPageFinished(this, rest);
                mClient.onPageCommitVisible(this, rest);
                break;
            }
            case EV_TITLE: {
                mTitle = rest;
                mChromeClient.onReceivedTitle(this, rest);
                break;
            }
            case EV_PROGRESS: {
                try {
                    mProgress = Integer.parseInt(rest.trim());
                } catch (NumberFormatException ignored) {
                    mProgress = 0;
                }
                mChromeClient.onProgressChanged(this, mProgress);
                break;
            }
            case EV_EVAL_RESULT: {
                int t = rest.indexOf('\t');
                if (t < 0) break;
                long evalId;
                try {
                    evalId = Long.parseLong(rest.substring(0, t));
                } catch (NumberFormatException e) {
                    break;
                }
                ValueCallback<String> cb = mEvalCallbacks.remove(evalId);
                if (cb != null) cb.onReceiveValue(rest.substring(t + 1));
                break;
            }
            case EV_JS_MESSAGE: {
                int t = rest.indexOf('\t');
                if (t < 0) break;
                invokeJsInterface(rest.substring(0, t), rest.substring(t + 1));
                break;
            }
            case EV_INTERCEPT: {
                handleIntercept(rest);
                break;
            }
            case EV_GEO_PROMPT: {
                final String origin = rest;
                mChromeClient.onGeolocationPermissionsShowPrompt(origin,
                    new GeolocationPermissions.Callback() {
                        @Override public void invoke(String o, boolean allow, boolean retain) {
                            // The host platform owns the actual location
                            // grant (app-level permission); the guest answer
                            // is accepted for API compatibility.
                        }
                    });
                break;
            }
            case EV_PERMISSION: {
                int t = rest.indexOf('\t');
                if (t < 0) break;
                String[] resources = rest.substring(t + 1).split(",");
                PermissionRequest req = new PermissionRequestImpl(rest.substring(0, t), resources);
                mChromeClient.onPermissionRequest(req);
                break;
            }
            case EV_FILE_CHOOSER: {
                // The host handles <input type=file> natively; the client is
                // still notified for compatibility and may veto by returning
                // true (in which case it owns the result).
                WebChromeClient.FileChooserParams params = new WebChromeClient.FileChooserParams();
                mChromeClient.onShowFileChooser(this, new ValueCallback<Uri[]>() {
                    @Override public void onReceiveValue(Uri[] value) {}
                }, params);
                break;
            }
            case EV_CONSOLE:
            case EV_NAV_STATE:
            default:
                break;
        }
    }

    // ------------------------------------------------------------ interception

    private void handleIntercept(String rest) {
        // rest: "<requestId>\t<method>\t<url>\t<isMainFrame>"
        String[] parts = rest.split("\t", 4);
        if (parts.length < 4) return;
        long requestId;
        try {
            requestId = Long.parseLong(parts[0]);
        } catch (NumberFormatException e) {
            return;
        }
        String info = nativeInterceptInfo(requestId);
        Map<String, String> headers = new HashMap<>();
        String method = parts[1];
        String url = parts[2];
        boolean mainFrame = "1".equals(parts[3]);
        if (info != null) {
            String[] ip = info.split("\t", 4);
            if (ip.length >= 3) {
                method = ip[0];
                url = ip[1];
                try {
                    JSONObject h = new JSONObject(ip[2]);
                    JSONArray names = h.names();
                    if (names != null) {
                        for (int i = 0; i < names.length(); i++) {
                            String k = names.getString(i);
                            headers.put(k, h.getString(k));
                        }
                    }
                } catch (Exception ignored) {
                }
                mainFrame = ip.length >= 4 && "1".equals(ip[3]);
            }
        }
        WebResourceRequest request = new RequestImpl(url, method, headers, mainFrame);
        WebResourceResponse response = null;
        try {
            response = mClient.shouldInterceptRequest(this, request);
        } catch (Throwable t) {
            response = null;
        }
        if (response == null) {
            nativeRespond(requestId, true, 0, null, null);
            return;
        }
        byte[] body = null;
        InputStream in = response.getData();
        if (in != null) {
            try {
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) >= 0) out.write(buf, 0, n);
                body = out.toByteArray();
            } catch (Exception ignored) {
                body = null;
            }
        }
        nativeRespond(requestId, false, response.getStatusCode(),
                      response.getMimeType(), body);
    }

    private void invokeJsInterface(String name, String json) {
        Object target = mJsInterfaces.get(name);
        if (target == null) return;
        try {
            JSONObject msg = new JSONObject(json);
            String methodName = msg.getString("method");
            JSONArray args = msg.optJSONArray("args");
            int argc = args != null ? args.length() : 0;
            for (Method m : target.getClass().getMethods()) {
                if (!m.getName().equals(methodName)) continue;
                if (m.getAnnotation(JavascriptInterface.class) == null) continue;
                Class<?>[] params = m.getParameterTypes();
                if (params.length != argc) continue;
                Object[] callArgs = new Object[argc];
                boolean ok = true;
                for (int i = 0; i < argc; i++) {
                    callArgs[i] = convertArg(args.get(i), params[i]);
                    if (callArgs[i] == CONVERSION_FAILED) { ok = false; break; }
                }
                if (!ok) continue;
                m.invoke(target, callArgs);
                return;
            }
        } catch (Throwable ignored) {
            // A failing JS-interface method must not kill the event pump.
        }
    }

    private static final Object CONVERSION_FAILED = new Object();

    private static Object convertArg(Object json, Class<?> type) {
        if (json == null || json == JSONObject.NULL) return null;
        try {
            if (type == String.class) return json.toString();
            if (type == int.class || type == Integer.class) return ((Number) json).intValue();
            if (type == long.class || type == Long.class) return ((Number) json).longValue();
            if (type == double.class || type == Double.class) return ((Number) json).doubleValue();
            if (type == float.class || type == Float.class) return ((Number) json).floatValue();
            if (type == boolean.class || type == Boolean.class) return json;
            if (type == Object.class) return json;
        } catch (ClassCastException e) {
            return CONVERSION_FAILED;
        }
        return CONVERSION_FAILED;
    }

    // Minimal JSON string quoting (framework JSONObject has no quote()).
    private static String jsQuote(String s) {
        StringBuilder sb = new StringBuilder(s.length() + 2);
        sb.append('"');
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"': sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                default:
                    if (c < 0x20) sb.append(String.format("\\u%04x", (int) c));
                    else sb.append(c);
            }
        }
        sb.append('"');
        return sb.toString();
    }

    // -------------------------------------------------------------- internals

    void onSettingsChanged() {
        nativeApplySettings(mHandle, buildConfigJson());
    }

    private String buildConfigJson() {
        StringBuilder sb = new StringBuilder(128);
        sb.append('{');
        sb.append("\"ua\":").append(jsQuote(mSettings.getUserAgentString()));
        sb.append(",\"js\":").append(mSettings.getJavaScriptEnabled());
        sb.append(",\"domStorage\":").append(mSettings.getDomStorageEnabled());
        sb.append(",\"mediaGesture\":").append(mSettings.getMediaPlaybackRequiresUserGesture());
        sb.append('}');
        return sb.toString();
    }

    private void updateInterceptEnabled() {
        nativeSetIntercept(mHandle, clientInterceptsRequests(mClient));
    }

    private static boolean clientInterceptsRequests(WebViewClient client) {
        // Interception is enabled when the concrete client overrides either
        // shouldInterceptRequest form (Tauri/Cordova clients always do).
        Class<?> c = client.getClass();
        while (c != null && c != WebViewClient.class) {
            for (Method m : c.getDeclaredMethods()) {
                if (m.getName().equals("shouldInterceptRequest")) return true;
            }
            c = c.getSuperclass();
        }
        return false;
    }

    private static class RequestImpl implements WebResourceRequest {
        private final Uri mUrl;
        private final String mMethod;
        private final Map<String, String> mHeaders;
        private final boolean mMainFrame;

        RequestImpl(String url, String method, Map<String, String> headers, boolean mainFrame) {
            mUrl = Uri.parse(url);
            mMethod = method;
            mHeaders = headers;
            mMainFrame = mainFrame;
        }

        @Override public Uri getUrl() { return mUrl; }
        @Override public String getMethod() { return mMethod; }
        @Override public Map<String, String> getRequestHeaders() { return mHeaders; }
        @Override public boolean isForMainFrame() { return mMainFrame; }
        @Override public boolean isRedirect() { return false; }
        @Override public boolean hasGesture() { return false; }
    }

    private static class PermissionRequestImpl extends PermissionRequest {
        private final Uri mOrigin;
        private final String[] mResources;
        private boolean mAnswered = false;

        PermissionRequestImpl(String origin, String[] resources) {
            mOrigin = Uri.parse(origin);
            mResources = resources;
        }

        @Override public Uri getOrigin() { return mOrigin; }
        @Override public String[] getResources() { return mResources; }
        @Override public void grant(String[] resources) { mAnswered = true; }
        @Override public void deny() { mAnswered = true; }
    }

    // --------------------------------------------------------------- natives

    private static native void nativeCreate(long handle, String configJson);
    private static native void nativeDestroy(long handle);
    private static native void nativeApplySettings(long handle, String configJson);
    private static native void nativeLoadUrl(long handle, String url);
    private static native void nativeLoadData(long handle, String data, String mimeType, String baseUrl);
    private static native void nativeEval(long handle, long evalId, String script);
    private static native void nativeSetFrame(long handle, int x, int y, int width, int height);
    private static native void nativeSetVisible(long handle, boolean visible);
    private static native void nativeGoBack(long handle);
    private static native void nativeGoForward(long handle);
    private static native void nativeReload(long handle);
    private static native void nativeStopLoading(long handle);
    private static native void nativeFlushStorage(long handle);
    private static native boolean nativeCanGoBack(long handle);
    private static native boolean nativeCanGoForward(long handle);
    private static native String nativeGetUrl(long handle);
    private static native void nativeSetIntercept(long handle, boolean enabled);
    private static native String nativePollEvent(long handle);
    private static native void nativeRespond(long requestId, boolean pass, int status, String mimeType, byte[] body);
    private static native byte[] nativeInterceptBody(long requestId);
    private static native String nativeInterceptInfo(long requestId);
}
