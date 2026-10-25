// Host-side tests for the hybrid-app bridges: WebViewHost (event queue,
// navigation state, interception waiter) and LocationHost (fix queues,
// last-known). These exercise the exact core logic the guest Java classes
// drive through LibCore natives, without a device or a host shell.
#include "kudroid/platform/WebViewHost.h"
#include "kudroid/platform/LocationHost.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static void test_webview_events() {
    kudroid::webview::Create(101, "{\"ua\":\"test\"}");
    CHECK(kudroid::webview::Exists(101));
    CHECK(kudroid::webview::PollEvent(101).empty());

    kudroid_webview_host_event(101, kKuWebViewEventPageStarted, "https://example.test/");
    kudroid_webview_host_event(101, kKuWebViewEventTitle, "Example");
    kudroid_webview_eval_result(101, 7, "\"ok\"");

    CHECK(kudroid::webview::PollEvent(101) == "1\thttps://example.test/");
    CHECK(kudroid::webview::PollEvent(101) == "3\tExample");
    CHECK(kudroid::webview::PollEvent(101) == "5\t7\t\"ok\"");
    CHECK(kudroid::webview::PollEvent(101).empty());

    // NavState updates the synchronous getters and still queues the event.
    kudroid_webview_host_event(101, kKuWebViewEventNavState, "1\t0\thttps://example.test/a");
    CHECK(kudroid::webview::CanGoBack(101));
    CHECK(!kudroid::webview::CanGoForward(101));
    CHECK(kudroid::webview::CurrentUrl(101) == "https://example.test/a");
    CHECK(kudroid::webview::PollEvent(101) == "10\t1\t0\thttps://example.test/a");

    kudroid::webview::Destroy(101);
    CHECK(!kudroid::webview::Exists(101));
    std::printf("webview events ok\n");
}

static void test_webview_intercept_roundtrip() {
    kudroid::webview::Create(202, "{}");
    // No interception until the guest enables it.
    CHECK(kudroid_webview_begin_intercept(202, "GET", "http://ipc.localhost/x",
                                          "{}", nullptr, 0, 0) == 0);
    kudroid::webview::SetInterceptEnabled(202, true);

    const char* payload = "{\"cmd\":1}";
    std::thread responder([payload] {
        // Guest side: drain the intercept event, read the body, respond.
        std::string ev;
        for (int i = 0; i < 200; ++i) {
            ev = kudroid::webview::PollEvent(202);
            if (!ev.empty()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(ev.rfind("7\t", 0) == 0);
        // Parse request id out of the payload.
        const auto t1 = ev.find('\t', 2);
        const int64_t requestId = std::stoll(ev.substr(2, t1 - 2));
        std::vector<uint8_t> body;
        CHECK(kudroid::webview::InterceptBody(requestId, &body));
        CHECK(body.size() == std::strlen(payload));
        CHECK(std::memcmp(body.data(), payload, body.size()) == 0);
        const std::vector<uint8_t> response = {'{', '"', 'o', 'k', '"', '}'};
        kudroid::webview::Respond(requestId, false, 200, "application/json", response);
    });

    const int64_t requestId = kudroid_webview_begin_intercept(
        202, "POST", "http://ipc.localhost/invoke", "{}",
        reinterpret_cast<const uint8_t*>(payload),
        static_cast<int32_t>(std::strlen(payload)), 0);
    CHECK(requestId > 0);

    int32_t status = 0;
    char mime[128] = {};
    uint8_t small[4];
    int32_t bodyLen = sizeof(small);
    // First await: body does not fit -> rc 2 with required size, waiter kept.
    int32_t rc = kudroid_webview_await_response(requestId, 5000, &status, mime,
                                                sizeof(mime), small, &bodyLen);
    CHECK(rc == 2);
    CHECK(bodyLen == 6);
    std::vector<uint8_t> buf(static_cast<size_t>(bodyLen));
    rc = kudroid_webview_await_response(requestId, 5000, &status, mime, sizeof(mime),
                                        buf.data(), &bodyLen);
    CHECK(rc == 1);
    CHECK(status == 200);
    CHECK(std::strcmp(mime, "application/json") == 0);
    CHECK(std::memcmp(buf.data(), "{\"ok\"}", 6) == 0);
    responder.join();
    kudroid::webview::Destroy(202);
    std::printf("webview intercept roundtrip ok\n");
}

static void test_webview_intercept_pass_and_timeout() {
    kudroid::webview::Create(303, "{}");
    kudroid::webview::SetInterceptEnabled(303, true);

    const int64_t id1 = kudroid_webview_begin_intercept(303, "GET", "http://asset.localhost/a",
                                                        "{}", nullptr, 0, 0);
    CHECK(id1 > 0);
    kudroid::webview::Respond(id1, true, 0, "", {});
    int32_t len = 0;
    CHECK(kudroid_webview_await_response(id1, 1000, nullptr, nullptr, 0, nullptr, &len) == 0);

    const int64_t id2 = kudroid_webview_begin_intercept(303, "GET", "http://asset.localhost/b",
                                                        "{}", nullptr, 0, 0);
    CHECK(id2 > 0);
    CHECK(kudroid_webview_await_response(id2, 50, nullptr, nullptr, 0, nullptr, &len) == 0);
    kudroid::webview::Destroy(303);
    std::printf("webview intercept pass/timeout ok\n");
}

static void test_location() {
    kudroid::location::Fix fix;
    CHECK(!kudroid::location::LastKnown(&fix));

    kudroid::location::Start(11, 0, 0);
    kudroid_location_inject_fix(11, 21.0278, 105.8342, 12.5f, 30.0, 1.5f, 90.0f,
                                1700000000000LL);
    CHECK(kudroid::location::LastKnown(&fix));
    CHECK(fix.latitude == 21.0278);
    CHECK(kudroid::location::PollFix(11, &fix));
    CHECK(fix.longitude == 105.8342);
    CHECK(fix.timeMs == 1700000000000LL);
    CHECK(!kudroid::location::PollFix(11, &fix));

    const std::string encoded = kudroid::location::Encode(fix);
    // Round-trip: 7 pipe-separated fields that parse back to the same fix.
    int pipes = 0;
    for (char c : encoded) if (c == '|') ++pipes;
    CHECK(pipes == 6);
    CHECK(std::stod(encoded.substr(0, encoded.find('|'))) == 21.0278);

    kudroid_location_set_services_enabled(1);
    CHECK(kudroid::location::ServicesEnabled());
    kudroid::location::Stop(11);
    std::printf("location ok\n");
}

int main() {
    test_webview_events();
    test_webview_intercept_roundtrip();
    test_webview_intercept_pass_and_timeout();
    test_location();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_hybrid_hosts: all ok\n");
    return 0;
}
