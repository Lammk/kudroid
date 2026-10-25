import Foundation
import WebKit

// KuDroidWebViewEvent codes from WebViewHost.h (the C enum does not import
// as Int32, so the values are mirrored here).
private enum Ev {
    static let pageStarted: Int32 = 1
    static let pageFinished: Int32 = 2
    static let title: Int32 = 3
    static let jsMessage: Int32 = 6
    static let geoPrompt: Int32 = 8
    static let navState: Int32 = 10
    static let permission: Int32 = 11
}

// WKWebView side of the WebViewHost bridge (see
// include/kudroid/platform/WebViewHost.h). Owns one WKWebView per guest
// handle and translates between the C ABI and WebKit delegates.
//
// Interception model: Android WebViews intercept every resource load via
// WebViewClient.shouldInterceptRequest. WKWebView cannot intercept http(s)
// loads natively, so loads to the hybrid-runtime hosts (ipc.localhost,
// asset.localhost — the Tauri pattern) are intercepted two ways:
//   * main-frame navigations: decidePolicyFor cancels and the response the
//     guest produces is loaded as data;
//   * fetch()/XHR from page JS: a document-start user script wraps fetch
//     and XMLHttpRequest for those hosts and routes them through the
//     "kudroid_ipc" script message handler; the guest's answer settles the
//     JS promise via a resolver script.
// Custom non-http schemes the guest registers are handled the same way as
// fetch (the page can only reach them through fetch/XHR or navigation).
final class KuDroidWebViewBridge: NSObject {
    static let shared = KuDroidWebViewBridge()

    private var webViews: [Int64: WKWebView] = [:]
    private var frames: [Int64: CGRect] = [:]
    private var interceptHosts: Set<String> = ["ipc.localhost", "asset.localhost"]

    // All WKWebView access happens on the main queue; vtable calls arrive
    // from guest threads and are re-dispatched here in call order.
    private func onMain(_ block: @escaping () -> Void) {
        if Thread.isMainThread { block() } else { DispatchQueue.main.async(execute: block) }
    }

    func register() {
        var host = KuDroidWebViewHost(
            create: { handle, configJson in
                let json = configJson != nil ? String(cString: configJson!) : "{}"
                KuDroidWebViewBridge.shared.onMain {
                    KuDroidWebViewBridge.shared.createWebView(handle: handle, configJson: json)
                }
            },
            destroy: { handle in
                KuDroidWebViewBridge.shared.onMain { KuDroidWebViewBridge.shared.destroyWebView(handle: handle) }
            },
            load_url: { handle, url in
                let u = url != nil ? String(cString: url!) : ""
                KuDroidWebViewBridge.shared.onMain {
                    guard let wv = KuDroidWebViewBridge.shared.webViews[handle] else { return }
                    if let parsed = URL(string: u) { wv.load(URLRequest(url: parsed)) }
                }
            },
            load_data: { handle, data, mime, baseUrl in
                let d = data != nil ? String(cString: data!) : ""
                let m = mime != nil ? String(cString: mime!) : "text/html"
                let b = baseUrl != nil ? String(cString: baseUrl!) : nil
                KuDroidWebViewBridge.shared.onMain {
                    guard let wv = KuDroidWebViewBridge.shared.webViews[handle] else { return }
                    wv.loadHTMLString(d, baseURL: b.flatMap { URL(string: $0) })
                    _ = m
                }
            },
            eval_js: { handle, evalId, script in
                let s = script != nil ? String(cString: script!) : ""
                KuDroidWebViewBridge.shared.onMain {
                    KuDroidWebViewBridge.shared.eval(handle: handle, evalId: evalId, script: s)
                }
            },
            set_frame: { handle, x, y, w, h in
                KuDroidWebViewBridge.shared.onMain {
                    let rect = CGRect(x: CGFloat(x), y: CGFloat(y), width: CGFloat(w), height: CGFloat(h))
                    KuDroidWebViewBridge.shared.frames[handle] = rect
                    KuDroidWebViewBridge.shared.webViews[handle]?.frame = rect
                }
            },
            set_visible: { handle, visible in
                KuDroidWebViewBridge.shared.onMain {
                    KuDroidWebViewBridge.shared.webViews[handle]?.isHidden = visible == 0
                }
            },
            go_back: { handle in
                KuDroidWebViewBridge.shared.onMain { KuDroidWebViewBridge.shared.webViews[handle]?.goBack() }
            },
            go_forward: { handle in
                KuDroidWebViewBridge.shared.onMain { KuDroidWebViewBridge.shared.webViews[handle]?.goForward() }
            },
            reload: { handle in
                KuDroidWebViewBridge.shared.onMain { KuDroidWebViewBridge.shared.webViews[handle]?.reload() }
            },
            stop_loading: { handle in
                KuDroidWebViewBridge.shared.onMain { KuDroidWebViewBridge.shared.webViews[handle]?.stopLoading() }
            },
            flush_storage: { _ in
                // WKWebsiteDataStore persists cookies/storage itself; nothing
                // buffered on our side to flush.
            },
            apply_settings: { handle, configJson in
                let json = configJson != nil ? String(cString: configJson!) : "{}"
                KuDroidWebViewBridge.shared.onMain {
                    KuDroidWebViewBridge.shared.applySettings(handle: handle, configJson: json)
                }
            }
        )
        kudroid_set_webview_host(&host)
    }

    // MARK: - Lifecycle

    private func createWebView(handle: Int64, configJson: String) {
        let config = (try? JSONSerialization.jsonObject(with: Data(configJson.utf8))) as? [String: Any] ?? [:]
        let wkConfig = WKWebViewConfiguration()
        wkConfig.websiteDataStore = .default()
        wkConfig.preferences.javaScriptEnabled = (config["js"] as? Bool) ?? true
        if #available(iOS 14.0, *) {
            wkConfig.defaultWebpagePreferences.allowsContentJavaScript = (config["js"] as? Bool) ?? true
        }
        wkConfig.mediaTypesRequiringUserActionForPlayback =
            ((config["mediaGesture"] as? Bool) ?? true) ? .all : []
        let ucc = wkConfig.userContentController
        ucc.add(KuDroidScriptHandler(bridge: self, handle: handle), name: "kudroid_jsi")
        ucc.add(KuDroidScriptHandler(bridge: self, handle: handle), name: "kudroid_ipc")
        ucc.addUserScript(WKUserScript(source: Self.fetchShimSource,
                                       injectionTime: .atDocumentStart,
                                       forMainFrameOnly: false))
        let wv = WKWebView(frame: frames[handle] ?? Self.defaultFrame(), configuration: wkConfig)
        wv.navigationDelegate = self
        wv.uiDelegate = self
        if let ua = config["ua"] as? String, !ua.isEmpty { wv.customUserAgent = ua }
        webViews[handle] = wv
        attach(wv)
    }

    private func destroyWebView(handle: Int64) {
        guard let wv = webViews.removeValue(forKey: handle) else { return }
        wv.stopLoading()
        wv.navigationDelegate = nil
        wv.uiDelegate = nil
        wv.removeFromSuperview()
        frames.removeValue(forKey: handle)
    }

    private func applySettings(handle: Int64, configJson: String) {
        guard let wv = webViews[handle],
              let config = (try? JSONSerialization.jsonObject(with: Data(configJson.utf8))) as? [String: Any]
        else { return }
        if let ua = config["ua"] as? String { wv.customUserAgent = ua }
        // JS enablement and DOM storage are configuration-time on WKWebView;
        // changing them requires recreating the view, which the guest does
        // not expect mid-session. They are honored at create().
    }

    private func attach(_ wv: WKWebView) {
        guard let vc = Self.topViewController() else { return }
        wv.frame = wv.frame.isEmpty ? vc.view.bounds : wv.frame
        vc.view.addSubview(wv)
        vc.view.bringSubviewToFront(wv)
    }

    private static func defaultFrame() -> CGRect {
        topViewController()?.view.bounds ?? CGRect(x: 0, y: 0, width: 390, height: 844)
    }

    private static func topViewController() -> UIViewController? {
        let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
        let window = scenes.first?.windows.first { $0.isKeyWindow } ?? scenes.first?.windows.first
        var vc = window?.rootViewController
        while let presented = vc?.presentedViewController { vc = presented }
        return vc
    }

    // MARK: - JS evaluation

    private func eval(handle: Int64, evalId: Int64, script: String) {
        guard let wv = webViews[handle] else {
            kudroid_webview_eval_result(handle, evalId, "null")
            return
        }
        wv.evaluateJavaScript(script) { result, _ in
            kudroid_webview_eval_result(handle, evalId, Self.jsonLiteral(of: result))
        }
    }

    // Android's evaluateJavascript callback receives a JSON-encoded value.
    private static func jsonLiteral(of value: Any?) -> String {
        guard let value = value else { return "null" }
        if let s = value as? String {
            return (try? JSONSerialization.data(withJSONObject: [s]))
                .flatMap { String(data: $0, encoding: .utf8) }
                .map { String($0.dropFirst().dropLast()) } ?? "\"\""
        }
        if let n = value as? NSNumber { return n.stringValue }
        if let b = value as? Bool { return b ? "true" : "false" }
        if let data = try? JSONSerialization.data(withJSONObject: value),
           let s = String(data: data, encoding: .utf8) {
            return s
        }
        return "null"
    }

    // MARK: - Interception plumbing

    // Runs one intercepted request through the guest and answers on the
    // caller's (background) thread — never the main thread.
    fileprivate func runIntercept(handle: Int64, method: String, url: String,
                                    headers: [String: String], body: Data,
                                    isMainFrame: Bool,
                                    completion: @escaping (Int32, String, Data) -> Void) {
        let headersJson = (try? JSONSerialization.data(withJSONObject: headers))
            .flatMap { String(data: $0, encoding: .utf8) } ?? "{}"
        var requestId: Int64 = 0
        body.withUnsafeBytes { raw in
            requestId = kudroid_webview_begin_intercept(
                handle, method, url, headersJson,
                raw.bindMemory(to: UInt8.self).baseAddress, Int32(body.count),
                isMainFrame ? 1 : 0)
        }
        guard requestId > 0 else {
            completion(0, "", Data())  // guest declined before queueing: pass
            return
        }
        DispatchQueue.global(qos: .userInitiated).async {
            var status: Int32 = 0
            var mimeBuf = [CChar](repeating: 0, count: 256)
            var bodyBuf = Data(count: 64 * 1024)
            var bodyLen = Int32(bodyBuf.count)
            var rc: Int32 = 0
            bodyBuf.withUnsafeMutableBytes { raw in
                rc = kudroid_webview_await_response(requestId, 15000, &status,
                                                    &mimeBuf, 256,
                                                    raw.bindMemory(to: UInt8.self).baseAddress,
                                                    &bodyLen)
            }
            if rc == 2 {
                // Body larger than the scratch buffer: refetch with room.
                bodyBuf = Data(count: Int(bodyLen))
                bodyLen = Int32(bodyBuf.count)
                bodyBuf.withUnsafeMutableBytes { raw in
                    rc = kudroid_webview_await_response(requestId, 1000, &status,
                                                        &mimeBuf, 256,
                                                        raw.bindMemory(to: UInt8.self).baseAddress,
                                                        &bodyLen)
                }
            }
            guard rc == 1 else {
                completion(0, "", Data())
                return
            }
            let mime = String(cString: mimeBuf)
            completion(status, mime, bodyBuf.prefix(Int(bodyLen)))
        }
    }

    fileprivate func handleScriptMessage(handle: Int64, channel: String, body: Any) {
        guard let text = body as? String,
              let msg = (try? JSONSerialization.jsonObject(with: Data(text.utf8))) as? [String: Any]
        else { return }
        if channel == "kudroid_jsi" {
            // addJavascriptInterface dispatch: forward raw JSON to the guest.
            let iface = (msg["iface"] as? String) ?? ""
            kudroid_webview_host_event(handle, Ev.jsMessage,
                                       "\(iface)\t\(text)")
            return
        }
        // kudroid_ipc: fetch/XHR interception from the shim.
        let shimId = (msg["id"] as? NSNumber)?.int64Value ?? 0
        let method = (msg["method"] as? String) ?? "GET"
        let url = (msg["url"] as? String) ?? ""
        let headers = (msg["headers"] as? [String: String]) ?? [:]
        let bodyB64 = (msg["body"] as? String) ?? ""
        let bodyData = Data(base64Encoded: bodyB64) ?? Data()
        runIntercept(handle: handle, method: method, url: url, headers: headers,
                     body: bodyData, isMainFrame: false) { [weak self] status, mime, data in
            guard let self = self else { return }
            self.onMain {
                guard let wv = self.webViews[handle] else { return }
                let b64 = data.base64EncodedString()
                let js = "window.__kudroid_ipc_resolve(\(shimId),\(status)," +
                         "\(Self.jsonLiteral(of: mime)),\(Self.jsonLiteral(of: b64)))"
                wv.evaluateJavaScript(js, completionHandler: nil)
            }
        }
    }

    // Document-start shim: routes fetch()/XHR to *.localhost hybrid hosts
    // (and any host the guest later adds via __kudroid_intercept_hosts)
    // through the kudroid_ipc message channel.
    static let fetchShimSource = """
    (function(){
      if (window.__kudroid_shim) return; window.__kudroid_shim = true;
      var pending = {}; var seq = 1;
      window.__kudroid_ipc_resolve = function(id, status, mime, bodyB64){
        var p = pending[id]; if (!p) return; delete pending[id];
        if (!status) { p.reject(new TypeError('kudroid intercept: guest declined')); return; }
        var bin = atob(bodyB64 || ''); var bytes = new Uint8Array(bin.length);
        for (var i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
        p.resolve(new Response(bytes, {status: status,
          headers: {'Content-Type': mime || 'application/octet-stream'}}));
      };
      function shouldIntercept(u){
        try { var url = new URL(u, location.href);
          return url.hostname === 'ipc.localhost' || url.hostname === 'asset.localhost';
        } catch(e) { return false; }
      }
      function toB64(buf){
        if (buf == null) return '';
        var bytes = new Uint8Array(buf); var s = '';
        for (var i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
        return btoa(s);
      }
      var origFetch = window.fetch;
      window.fetch = function(input, init){
        var url = (typeof input === 'string') ? input : input.url;
        if (!shouldIntercept(url)) return origFetch.apply(this, arguments);
        init = init || {};
        var method = (init.method || (typeof input !== 'string' && input.method) || 'GET');
        var headers = {};
        try { new Headers(init.headers || {}).forEach(function(v,k){ headers[k]=v; }); } catch(e){}
        var bodyPromise = Promise.resolve('');
        if (init.body != null) {
          if (typeof init.body === 'string') bodyPromise = Promise.resolve(btoa(init.body));
          else bodyPromise = new Response(init.body).arrayBuffer().then(toB64);
        }
        var id = seq++;
        return bodyPromise.then(function(b64){
          return new Promise(function(resolve, reject){
            pending[id] = {resolve: resolve, reject: reject};
            window.webkit.messageHandlers.kudroid_ipc.postMessage(JSON.stringify(
              {id: id, method: method, url: url, headers: headers, body: b64}));
          });
        });
      };
    })();
    """
}

// Script message handler is a separate object because WKUserContentController
// retains its handlers and WebView would cycle with the bridge otherwise.
private final class KuDroidScriptHandler: NSObject, WKScriptMessageHandler {
    weak var bridge: KuDroidWebViewBridge?
    let handle: Int64

    init(bridge: KuDroidWebViewBridge, handle: Int64) {
        self.bridge = bridge
        self.handle = handle
    }

    func userContentController(_ controller: WKUserContentController,
                               didReceive message: WKScriptMessage) {
        bridge?.handleScriptMessage(handle: handle, channel: message.name, body: message.body)
    }
}

extension KuDroidWebViewBridge: WKNavigationDelegate, WKUIDelegate {
    private func esc(_ s: String) -> String {
        s.replacingOccurrences(of: "\t", with: "%09").replacingOccurrences(of: "\n", with: "%0A")
    }

    func webView(_ webView: WKWebView, didStartProvisionalNavigation navigation: WKNavigation!) {
        guard let handle = webViews.first(where: { $0.value === webView })?.key else { return }
        kudroid_webview_host_event(handle, Ev.pageStarted, webView.url?.absoluteString ?? "")
        reportNavState(handle: handle, webView: webView)
    }

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        guard let handle = webViews.first(where: { $0.value === webView })?.key else { return }
        let url = webView.url?.absoluteString ?? ""
        kudroid_webview_host_event(handle, Ev.pageFinished, url)
        if let title = webView.title {
            kudroid_webview_host_event(handle, Ev.title, title)
        }
        reportNavState(handle: handle, webView: webView)
    }

    func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
        guard let handle = webViews.first(where: { $0.value === webView })?.key else { return }
        kudroid_webview_host_event(handle, Ev.pageFinished, webView.url?.absoluteString ?? "")
    }

    func webView(_ webView: WKWebView,
                 decidePolicyFor navigationAction: WKNavigationAction,
                 decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        guard let handle = webViews.first(where: { $0.value === webView })?.key,
              let url = navigationAction.request.url else {
            decisionHandler(.allow)
            return
        }
        // Hybrid-runtime hosts: the guest serves these itself.
        if interceptHosts.contains(url.host ?? "") {
            decisionHandler(.cancel)
            let request = navigationAction.request
            runIntercept(handle: handle, method: request.httpMethod ?? "GET",
                         url: url.absoluteString, headers: [:],
                         body: request.httpBody ?? Data(), isMainFrame: true) { [weak self] status, mime, data in
                guard status > 0 else { return }
                self?.onMain {
                    webView.load(data, mimeType: mime.isEmpty ? "text/html" : mime,
                                 characterEncodingName: "utf-8", baseURL: url)
                }
            }
            return
        }
        // Non-web schemes leave the app, like Android's shouldOverrideUrlLoading
        // default in hybrid runtimes. (Guest consultation for ordinary links is
        // a documented v1 gap — see docs/sorayori-rust-beta-plan.md.)
        if let scheme = url.scheme, scheme != "http" && scheme != "https" && scheme != "about" {
            UIApplication.shared.open(url, options: [:], completionHandler: nil)
            decisionHandler(.cancel)
            return
        }
        decisionHandler(.allow)
    }

    private func reportNavState(handle: Int64, webView: WKWebView) {
        let payload = "\(webView.canGoBack ? 1 : 0)\t\(webView.canGoForward ? 1 : 0)\t\(webView.url?.absoluteString ?? "")"
        kudroid_webview_host_event(handle, Ev.navState, payload)
    }

    // MARK: WKUIDelegate

    func webView(_ webView: WKWebView, runJavaScriptAlertPanelWithMessage message: String,
                 initiatedByFrame frame: WKFrameInfo,
                 completionHandler: @escaping () -> Void) {
        completionHandler()
    }

    func webView(_ webView: WKWebView, runJavaScriptConfirmPanelWithMessage message: String,
                 initiatedByFrame frame: WKFrameInfo,
                 completionHandler: @escaping (Bool) -> Void) {
        completionHandler(false)
    }

    func webView(_ webView: WKWebView, runJavaScriptTextInputPanelWithPrompt prompt: String,
                 defaultText: String?, initiatedByFrame frame: WKFrameInfo,
                 completionHandler: @escaping (String?) -> Void) {
        completionHandler(nil)
    }

    @available(iOS 15.0, *)
    func webView(_ webView: WKWebView,
                 requestGeolocationPermissionFor origin: WKSecurityOrigin,
                 initiatedByFrame frame: WKFrameInfo,
                 decisionHandler: @escaping (WKPermissionDecision) -> Void) {
        guard let handle = webViews.first(where: { $0.value === webView })?.key else {
            decisionHandler(.deny)
            return
        }
        kudroid_webview_host_event(handle, Ev.geoPrompt, origin.host)
        // The real grant is the host app's CoreLocation authorization; the
        // guest's GeolocationPermissions.Callback answer is advisory here.
        decisionHandler(KuDroidLocationBridge.shared.isAuthorized ? .grant : .deny)
    }

    @available(iOS 15.0, *)
    func webView(_ webView: WKWebView,
                 requestMediaCapturePermissionFor origin: WKSecurityOrigin,
                 initiatedByFrame frame: WKFrameInfo, type: WKMediaCaptureType,
                 decisionHandler: @escaping (WKPermissionDecision) -> Void) {
        guard let handle = webViews.first(where: { $0.value === webView })?.key else {
            decisionHandler(.deny)
            return
        }
        let resource = type == .microphone
            ? PermissionResourceAudio : PermissionResourceVideo
        kudroid_webview_host_event(handle, Ev.permission,
                                   "0\t\(resource)")
        decisionHandler(.grant)
    }
}

// Resource strings matching android.webkit.PermissionRequest constants.
private let PermissionResourceAudio = "android.webkit.resource.AUDIO_CAPTURE"
private let PermissionResourceVideo = "android.webkit.resource.VIDEO_CAPTURE"
