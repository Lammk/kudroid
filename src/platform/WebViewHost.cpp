// Core-side state for the WebView host bridge (see WebViewHost.h).
//
// This file is deliberately platform-free: the iOS shell registers real
// WKWebView callbacks through kudroid_set_webview_host(), and on headless
// builds (Linux CI, tests) everything still works — loads are recorded,
// events queue, interception waiters complete — so the guest-side logic in
// LibCore and framework/android/webkit can be exercised without a device.
#include "kudroid/platform/WebViewHost.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace {

struct InterceptWaiter {
    int64_t handle = 0;
    std::string method;
    std::string url;
    std::string headersJson;
    std::vector<uint8_t> requestBody;
    bool mainFrame = false;

    bool done = false;
    bool pass = false;
    int32_t status = 0;
    std::string mime;
    std::vector<uint8_t> body;
    std::condition_variable cv;
};

struct WebViewState {
    std::string configJson;
    bool interceptEnabled = false;
    bool canGoBack = false;
    bool canGoForward = false;
    std::string currentUrl;
    std::deque<std::string> events;
};

std::mutex gMutex;
KuDroidWebViewHost gHost{};
bool gHasHost = false;
std::unordered_map<int64_t, std::shared_ptr<WebViewState>> gViews;
std::unordered_map<int64_t, std::shared_ptr<InterceptWaiter>> gWaiters;
int64_t gNextRequestId = 1;

std::shared_ptr<WebViewState> Find(int64_t handle) {
    auto it = gViews.find(handle);
    return it == gViews.end() ? nullptr : it->second;
}

void PushEventLocked(const std::shared_ptr<WebViewState>& state, int32_t type,
                     const std::string& payload) {
    if (!state) return;
    // Bound the queue: a guest that stopped polling must not grow memory
    // without limit. Page/progress events are droppable; the cap is generous
    // enough that a live guest never reaches it.
    if (state->events.size() >= 1024) state->events.pop_front();
    state->events.push_back(std::to_string(type) + "\t" + payload);
}

}  // namespace

extern "C" void kudroid_set_webview_host(const KuDroidWebViewHost* host) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (host) {
        gHost = *host;
        gHasHost = true;
    } else {
        gHost = KuDroidWebViewHost{};
        gHasHost = false;
    }
}

extern "C" void kudroid_webview_host_event(int64_t handle, int32_t type,
                                           const char* payload) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto state = Find(handle);
    if (!state) return;
    const std::string body = payload ? payload : "";
    if (type == kKuWebViewEventNavState) {
        // Keep nav state queryable synchronously (canGoBack is a sync API).
        // Payload: "<canGoBack>\t<canGoForward>\t<url>".
        const auto t1 = body.find('\t');
        const auto t2 = t1 == std::string::npos ? t1 : body.find('\t', t1 + 1);
        if (t1 != std::string::npos) {
            state->canGoBack = body.substr(0, t1) == "1";
            if (t2 != std::string::npos) {
                state->canGoForward = body.substr(t1 + 1, t2 - t1 - 1) == "1";
                state->currentUrl = body.substr(t2 + 1);
            }
        }
    }
    PushEventLocked(state, type, body);
}

extern "C" void kudroid_webview_eval_result(int64_t handle, int64_t evalId,
                                            const char* jsonResultUtf8) {
    const std::string payload = std::to_string(evalId) + "\t" +
                                (jsonResultUtf8 ? jsonResultUtf8 : "null");
    kudroid_webview_host_event(handle, kKuWebViewEventEvalResult, payload.c_str());
}

extern "C" int64_t kudroid_webview_begin_intercept(int64_t handle, const char* method,
                                                   const char* url,
                                                   const char* headersJson,
                                                   const uint8_t* body,
                                                   int32_t bodyLen,
                                                   int32_t isMainFrame) {
    auto waiter = std::make_shared<InterceptWaiter>();
    waiter->handle = handle;
    waiter->method = method ? method : "GET";
    waiter->url = url ? url : "";
    waiter->headersJson = headersJson ? headersJson : "{}";
    if (body && bodyLen > 0) waiter->requestBody.assign(body, body + bodyLen);
    waiter->mainFrame = isMainFrame != 0;

    int64_t requestId;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto state = Find(handle);
        if (!state || !state->interceptEnabled) return 0;
        requestId = gNextRequestId++;
        gWaiters[requestId] = waiter;
        const std::string payload = std::to_string(requestId) + "\t" + waiter->method +
                                    "\t" + waiter->url + "\t" +
                                    (waiter->mainFrame ? "1" : "0");
        PushEventLocked(state, kKuWebViewEventIntercept, payload);
    }
    return requestId;
}

extern "C" int32_t kudroid_webview_await_response(int64_t requestId, int32_t timeoutMs,
                                                  int32_t* outStatus, char* outMime,
                                                  int32_t outMimeCap, uint8_t* outBody,
                                                  int32_t* bodyLen) {
    std::shared_ptr<InterceptWaiter> waiter;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gWaiters.find(requestId);
        if (it == gWaiters.end()) return 0;
        waiter = it->second;
    }

    bool completed;
    {
        std::unique_lock<std::mutex> lock(gMutex);
        completed = waiter->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                        [&] { return waiter->done; });
    }

    if (!completed || waiter->pass) {
        std::lock_guard<std::mutex> lock(gMutex);
        gWaiters.erase(requestId);
        return 0;
    }

    if (outStatus) *outStatus = waiter->status;
    if (outMime && outMimeCap > 0) {
        std::strncpy(outMime, waiter->mime.c_str(), static_cast<size_t>(outMimeCap) - 1);
        outMime[outMimeCap - 1] = '\0';
    }
    if (bodyLen) {
        const int32_t need = static_cast<int32_t>(waiter->body.size());
        if (outBody && *bodyLen >= need) {
            if (need > 0) std::memcpy(outBody, waiter->body.data(), waiter->body.size());
            *bodyLen = need;
        } else {
            // Too small: report the needed size and keep the (completed)
            // waiter so the caller can retry with a bigger buffer.
            *bodyLen = need;
            return 2;
        }
    }
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gWaiters.erase(requestId);
    }
    return 1;
}

namespace kudroid {
namespace webview {

void Create(int64_t handle, const std::string& configJson) {
    KuDroidWebViewHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto state = std::make_shared<WebViewState>();
        state->configJson = configJson;
        gViews[handle] = state;
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.create) host.create(handle, configJson.c_str());
}

void Destroy(int64_t handle) {
    KuDroidWebViewHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gViews.erase(handle);
        // Release any waiters belonging to this view as "pass" so shell
        // threads blocked in await_response do not hang on a dead view.
        for (auto& pair : gWaiters) {
            if (pair.second->handle == handle && !pair.second->done) {
                pair.second->done = true;
                pair.second->pass = true;
                pair.second->cv.notify_all();
            }
        }
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.destroy) host.destroy(handle);
}

bool Exists(int64_t handle) {
    std::lock_guard<std::mutex> lock(gMutex);
    return Find(handle) != nullptr;
}

void LoadUrl(int64_t handle, const std::string& url) {
    KuDroidWebViewHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (auto state = Find(handle)) state->currentUrl = url;
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.load_url) host.load_url(handle, url.c_str());
}

void LoadData(int64_t handle, const std::string& data, const std::string& mime,
              const std::string& baseUrl) {
    KuDroidWebViewHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.load_data) {
        host.load_data(handle, data.c_str(), mime.c_str(),
                       baseUrl.empty() ? nullptr : baseUrl.c_str());
    }
}

void EvalJs(int64_t handle, int64_t evalId, const std::string& script) {
    KuDroidWebViewHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.eval_js) {
        host.eval_js(handle, evalId, script.c_str());
    } else {
        // No host (headless): answer immediately with null so guest
        // ValueCallbacks do not hang forever.
        kudroid_webview_eval_result(handle, evalId, "null");
    }
}

void SetFrame(int64_t handle, int32_t x, int32_t y, int32_t w, int32_t h) {
    KuDroidWebViewHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.set_frame) host.set_frame(handle, x, y, w, h);
}

void SetVisible(int64_t handle, bool visible) {
    KuDroidWebViewHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.set_visible) host.set_visible(handle, visible ? 1 : 0);
}

void ApplySettings(int64_t handle, const std::string& configJson) {
    KuDroidWebViewHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (auto state = Find(handle)) state->configJson = configJson;
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.apply_settings) host.apply_settings(handle, configJson.c_str());
}

void GoBack(int64_t handle) {
    KuDroidWebViewHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.go_back) host.go_back(handle);
}

void GoForward(int64_t handle) {
    KuDroidWebViewHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.go_forward) host.go_forward(handle);
}

void Reload(int64_t handle) {
    KuDroidWebViewHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.reload) host.reload(handle);
}

void StopLoading(int64_t handle) {
    KuDroidWebViewHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.stop_loading) host.stop_loading(handle);
}

void FlushStorage(int64_t handle) {
    KuDroidWebViewHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.flush_storage) host.flush_storage(handle);
}

void SetInterceptEnabled(int64_t handle, bool enabled) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (auto state = Find(handle)) state->interceptEnabled = enabled;
}

bool InterceptEnabled(int64_t handle) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto state = Find(handle);
    return state && state->interceptEnabled;
}

bool CanGoBack(int64_t handle) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto state = Find(handle);
    return state && state->canGoBack;
}

bool CanGoForward(int64_t handle) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto state = Find(handle);
    return state && state->canGoForward;
}

std::string CurrentUrl(int64_t handle) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto state = Find(handle);
    return state ? state->currentUrl : std::string();
}

std::string PollEvent(int64_t handle) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto state = Find(handle);
    if (!state || state->events.empty()) return std::string();
    std::string event = state->events.front();
    state->events.pop_front();
    return event;
}

void Respond(int64_t requestId, bool pass, int32_t status, const std::string& mime,
             const std::vector<uint8_t>& body) {
    std::shared_ptr<InterceptWaiter> waiter;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gWaiters.find(requestId);
        if (it == gWaiters.end()) return;
        waiter = it->second;
        waiter->pass = pass;
        waiter->status = status;
        waiter->mime = mime;
        waiter->body = body;
        waiter->done = true;
    }
    waiter->cv.notify_all();
}

bool InterceptBody(int64_t requestId, std::vector<uint8_t>* out) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gWaiters.find(requestId);
    if (it == gWaiters.end()) return false;
    if (out) *out = it->second->requestBody;
    return true;
}

bool InterceptInfo(int64_t requestId, std::string* outMethod, std::string* outUrl,
                   std::string* outHeadersJson, bool* outMainFrame) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gWaiters.find(requestId);
    if (it == gWaiters.end()) return false;
    if (outMethod) *outMethod = it->second->method;
    if (outUrl) *outUrl = it->second->url;
    if (outHeadersJson) *outHeadersJson = it->second->headersJson;
    if (outMainFrame) *outMainFrame = it->second->mainFrame;
    return true;
}

}  // namespace webview
}  // namespace kudroid
