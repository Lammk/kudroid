#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Host bridge for android.webkit.WebView — the "hybrid app" surface (Tauri,
// Cordova, Capacitor, and plain WebView apps). The C++ core owns per-WebView
// state, the host->guest event queue, and request-interception waiters; the
// iOS shell owns the actual WKWebView objects and registers its callbacks
// through this vtable, exactly like KuDroidSensorHost does for sensors.
//
// Threading: every function here is callable from any thread. Host callbacks
// (create/load/eval/...) are invoked on the caller's thread; the shell is
// responsible for hopping to the main thread before touching WKWebView.
// Events pushed with kudroid_webview_host_event() are drained by the guest's
// Java side (nativePollEvent in LibCore) on the guest main thread, so guest
// callbacks (WebViewClient/WebChromeClient) always fire on that thread.
typedef struct KuDroidWebViewHost {
    // A guest WebView was created. `configJson` carries the settings the
    // guest asked for: {"ua":..., "js":bool, "domStorage":bool,
    // "mediaGesture":bool, "intercept":bool}.
    void (*create)(int64_t handle, const char* configJson);
    void (*destroy)(int64_t handle);
    void (*load_url)(int64_t handle, const char* url);
    // Load raw markup. `baseUrl` may be NULL.
    void (*load_data)(int64_t handle, const char* data, const char* mimeType,
                      const char* baseUrl);
    // Run JS; the shell must answer exactly once with
    // kudroid_webview_eval_result(handle, evalId, jsonResultUtf8).
    void (*eval_js)(int64_t handle, int64_t evalId, const char* script);
    // Guest view geometry in density-independent pixels (Android dp).
    void (*set_frame)(int64_t handle, int32_t x, int32_t y, int32_t width,
                      int32_t height);
    void (*set_visible)(int64_t handle, int32_t visible);
    void (*go_back)(int64_t handle);
    void (*go_forward)(int64_t handle);
    void (*reload)(int64_t handle);
    void (*stop_loading)(int64_t handle);
    // Persist cookies/storage for this handle where the platform allows it.
    void (*flush_storage)(int64_t handle);
    // Settings changed after creation; same JSON shape as create().
    void (*apply_settings)(int64_t handle, const char* configJson);
} KuDroidWebViewHost;

void kudroid_set_webview_host(const KuDroidWebViewHost* host);

// ---- Host (shell) -> core events -------------------------------------------
// Event type codes delivered to the guest Java poll loop. Payload format is
// documented in WebView.java (dispatchHostEvent): fields are separated by
// '\t', text is percent-escaped by the producer (see webview_escape).
enum KuDroidWebViewEvent {
    kKuWebViewEventPageStarted = 1,
    kKuWebViewEventPageFinished = 2,
    kKuWebViewEventTitle = 3,
    kKuWebViewEventProgress = 4,
    kKuWebViewEventEvalResult = 5,   // payload: "<evalId>\t<json>"
    kKuWebViewEventJsMessage = 6,    // payload: "<interfaceName>\t<json>"
    kKuWebViewEventIntercept = 7,    // payload: "<requestId>\t<method>\t<url>\t<isMainFrame>"
    kKuWebViewEventGeoPrompt = 8,    // payload: "<origin>"
    kKuWebViewEventConsole = 9,      // payload: "<level>\t<message>"
    kKuWebViewEventNavState = 10,    // payload: "<canGoBack 0/1>\t<canGoForward 0/1>\t<url>"
    kKuWebViewEventPermission = 11,  // payload: "<requestId>\t<resource csv>"
    kKuWebViewEventFileChooser = 12, // payload: "<requestId>\t<acceptTypes csv>\t<multiple 0/1>"
};

void kudroid_webview_host_event(int64_t handle, int32_t type, const char* payload);
void kudroid_webview_eval_result(int64_t handle, int64_t evalId, const char* jsonResultUtf8);

// ---- Request interception ---------------------------------------------------
// The shell calls begin_intercept when a load matches a guest-registered
// interception (custom scheme, or a URL the guest's WebViewClient wants to
// serve). It returns a request id > 0 and posts an Intercept event to the
// guest. The shell then blocks (on its own background thread, never the main
// thread) in kudroid_webview_await_response until the guest answers through
// the LibCore natives (nativeRespond / nativeRespondPass) or the timeout
// expires. A "pass" answer means: the guest declines; host should fall back
// to loading the URL itself (or fail the load, for non-network schemes).
int64_t kudroid_webview_begin_intercept(int64_t handle, const char* method,
                                        const char* url, const char* headersJson,
                                        const uint8_t* body, int32_t bodyLen,
                                        int32_t isMainFrame);
// Waits for the guest response. On success returns 1 and fills the outputs
// (body buffer is owned by the caller-provided storage: pass a buffer and
// its capacity in *bodyLen; if the body is larger, returns 2 and *bodyLen
// holds the required size so the caller can retry with a bigger buffer).
// Returns 0 on pass/timeout.
int32_t kudroid_webview_await_response(int64_t requestId, int32_t timeoutMs,
                                       int32_t* outStatus, char* outMime,
                                       int32_t outMimeCap, uint8_t* outBody,
                                       int32_t* bodyLen);

#ifdef __cplusplus
}  // extern "C"
#endif

// ---- Core-side API used by LibCore (C++) ------------------------------------
#ifdef __cplusplus
#include <string>
#include <vector>

namespace kudroid {
namespace webview {

// Creates/destroys core-side state for a handle and forwards to the host.
void Create(int64_t handle, const std::string& configJson);
void Destroy(int64_t handle);
bool Exists(int64_t handle);

void LoadUrl(int64_t handle, const std::string& url);
void LoadData(int64_t handle, const std::string& data, const std::string& mime,
              const std::string& baseUrl);
void EvalJs(int64_t handle, int64_t evalId, const std::string& script);
void SetFrame(int64_t handle, int32_t x, int32_t y, int32_t w, int32_t h);
void SetVisible(int64_t handle, bool visible);
void ApplySettings(int64_t handle, const std::string& configJson);
void GoBack(int64_t handle);
void GoForward(int64_t handle);
void Reload(int64_t handle);
void StopLoading(int64_t handle);
void FlushStorage(int64_t handle);

// Records whether the guest's WebViewClient wants interception at all, so
// the shell can ask before starting loads it cannot intercept natively.
void SetInterceptEnabled(int64_t handle, bool enabled);
bool InterceptEnabled(int64_t handle);

// Navigation state last reported by the host (NavState event).
bool CanGoBack(int64_t handle);
bool CanGoForward(int64_t handle);
std::string CurrentUrl(int64_t handle);

// Drains one pending host event for the guest poll loop. Returns "" when the
// queue is empty. Format: "<type>\t<payload>" with payload fields already
// tab-separated/escaped by the producer side (host_event / eval_result).
std::string PollEvent(int64_t handle);

// Guest answers an interception. `pass=true` releases the waiter as declined.
void Respond(int64_t requestId, bool pass, int32_t status, const std::string& mime,
             const std::vector<uint8_t>& body);
// Request body captured at begin_intercept (e.g. Tauri IPC POST payloads).
bool InterceptBody(int64_t requestId, std::vector<uint8_t>* out);
bool InterceptInfo(int64_t requestId, std::string* outMethod, std::string* outUrl,
                   std::string* outHeadersJson, bool* outMainFrame);

}  // namespace webview
}  // namespace kudroid
#endif
