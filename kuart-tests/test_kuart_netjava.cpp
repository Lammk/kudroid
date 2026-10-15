// test_kuart_netjava.cpp - java.net HTTP(S) clients through the real framework.dex. The Java
// scenarios live in kuart-tests/java; this harness supplies the servers they talk to: a plain
// HTTP server on loopback and, where OpenSSL is available, TLS servers with generated
// certificates (a trusted one, a self-signed one and one for the wrong host name).
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zlib.h>

#include <signal.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef KUDROID_HAS_OPENSSL
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

namespace {

using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexJniEnv;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexObject;
using kudroid::kuart::DexString;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

int g_checks = 0;
int g_failures = 0;
DexClassLinker* g_linker = nullptr;
Interpreter* g_interp = nullptr;

// ── a byte stream the server speaks over: a socket, or a TLS session ───────────────────────

struct Stream {
    virtual ~Stream() {}
    virtual ssize_t read(void* buf, size_t len) = 0;
    virtual ssize_t write(const void* buf, size_t len) = 0;
};

struct PlainStream : Stream {
    int fd;
    explicit PlainStream(int f) : fd(f) {}
    ssize_t read(void* buf, size_t len) override { return ::read(fd, buf, len); }
    ssize_t write(const void* buf, size_t len) override { return ::write(fd, buf, len); }
};

std::string Gzip(const std::string& in) {
    z_stream z{};
    deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
    std::string out(in.size() + 64, '\0');
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    z.avail_in = static_cast<uInt>(in.size());
    z.next_out = reinterpret_cast<Bytef*>(&out[0]);
    z.avail_out = static_cast<uInt>(out.size());
    deflate(&z, Z_FINISH);
    out.resize(z.total_out);
    deflateEnd(&z);
    return out;
}

std::atomic<int> g_connections{0};

struct Request {
    std::string method, target, body;
    std::map<std::string, std::string> headers;  // lower-cased names
};

bool ReadRequest(Stream& s, std::string& pending, Request* req) {
    auto fill = [&]() {
        char buf[4096];
        const ssize_t n = s.read(buf, sizeof(buf));
        if (n <= 0) return false;
        pending.append(buf, static_cast<size_t>(n));
        return true;
    };
    size_t end;
    while ((end = pending.find("\r\n\r\n")) == std::string::npos) {
        if (!fill()) return false;
    }
    std::istringstream head(pending.substr(0, end));
    std::string line;
    std::getline(head, line);
    std::istringstream first(line);
    std::string version;
    first >> req->method >> req->target >> version;
    while (std::getline(head, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        for (char& c : name) c = static_cast<char>(tolower(c));
        size_t v = colon + 1;
        while (v < line.size() && line[v] == ' ') ++v;
        req->headers[name] = line.substr(v);
    }
    pending.erase(0, end + 4);
    size_t want = 0;
    if (req->headers.count("content-length")) want = std::stoul(req->headers["content-length"]);
    while (pending.size() < want) {
        if (!fill()) return false;
    }
    req->body = pending.substr(0, want);
    pending.erase(0, want);
    return true;
}

void SendResponse(Stream& s, const std::string& status, const std::string& headers,
                  const std::string& body, bool includeLength = true) {
    std::string out = "HTTP/1.1 " + status + "\r\n" + headers;
    if (includeLength) out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    out += "\r\n" + body;
    s.write(out.data(), out.size());
}

// Serves one connection until the client closes it, so keep-alive can be observed.
void Serve(std::unique_ptr<Stream> stream) {
    std::string pending;
    for (;;) {
        Request req;
        if (!ReadRequest(*stream, pending, &req)) return;
        const std::string& t = req.target;
        if (t == "/hello") {
            if (req.method == "HEAD") {
                SendResponse(*stream, "200 OK", "Content-Type: text/plain\r\nContent-Length: 11\r\n", "", false);
            } else {
                SendResponse(*stream, "200 OK", "Content-Type: text/plain\r\n", "hello world");
            }
        } else if (t == "/chunked") {
            std::string h = "Transfer-Encoding: chunked\r\n";
            const std::string body = "3\r\nabc\r\n5\r\ndefgh\r\n0\r\n\r\n";
            SendResponse(*stream, "200 OK", h, body, false);
        } else if (t == "/gzip") {
            SendResponse(*stream, "200 OK", "Content-Encoding: gzip\r\n", Gzip("compressed text"));
        } else if (t == "/redirect") {
            SendResponse(*stream, "302 Found", "Location: /hello\r\n", "");
        } else if (t == "/redirect-post") {
            SendResponse(*stream, "303 See Other", "Location: /echo-method\r\n", "");
        } else if (t == "/echo-method") {
            SendResponse(*stream, "200 OK", "", req.method);
        } else if (t == "/echo") {
            std::string h = "X-Method: " + req.method + "\r\nX-Content-Type: " +
                            req.headers["content-type"] + "\r\nX-Length: " +
                            req.headers["content-length"] + "\r\n";
            SendResponse(*stream, "200 OK", h, req.body);
        } else if (t == "/headers") {
            std::string body;
            for (const auto& kv : req.headers) body += kv.first + ": " + kv.second + "\n";
            SendResponse(*stream, "200 OK", "", body);
        } else if (t == "/notfound") {
            SendResponse(*stream, "404 Not Found", "", "nope");
        } else if (t == "/boom") {
            SendResponse(*stream, "500 Internal Server Error", "", "error body");
        } else if (t.rfind("/query", 0) == 0) {
            SendResponse(*stream, "200 OK", "", t);
        } else if (t == "/close") {
            SendResponse(*stream, "200 OK", "Connection: close\r\n", "until-close", false);
            return;
        } else if (t == "/slow") {
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            SendResponse(*stream, "200 OK", "", "late");
        } else if (t == "/count") {
            SendResponse(*stream, "200 OK", "", std::to_string(g_connections.load()));
        } else if (t == "/setcookie") {
            SendResponse(*stream, "200 OK", "Set-Cookie: sid=abc; Path=/\r\n", "ok");
        } else if (t == "/needcookie") {
            SendResponse(*stream, "200 OK", "", req.headers["cookie"]);
        } else {
            SendResponse(*stream, "404 Not Found", "", "unknown");
        }
        if (req.headers["connection"] == "close") return;
    }
}

using Wrap = std::function<std::unique_ptr<Stream>(int)>;

struct Server {
    int fd = -1;
    int port = 0;
    std::atomic<bool> stop{false};
    std::thread thread;

    explicit Server(Wrap wrap) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        listen(fd, 16);
        socklen_t len = sizeof(a);
        getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
        port = ntohs(a.sin_port);
        thread = std::thread([this, wrap] {
            while (!stop) {
                int c = accept(fd, nullptr, nullptr);
                if (c < 0) return;
                ++g_connections;
                std::thread([c, wrap] {
                    std::unique_ptr<Stream> s = wrap(c);
                    if (s) Serve(std::move(s));
                    close(c);
                }).detach();
            }
        });
    }

    ~Server() {
        stop = true;
        shutdown(fd, SHUT_RDWR);
        close(fd);
        if (thread.joinable()) thread.join();
    }
};

#ifdef KUDROID_HAS_OPENSSL

struct TlsStream : Stream {
    SSL* ssl;
    explicit TlsStream(SSL* s) : ssl(s) {}
    ~TlsStream() override {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    ssize_t read(void* buf, size_t len) override {
        const int n = SSL_read(ssl, buf, static_cast<int>(len));
        return n > 0 ? n : -1;
    }
    ssize_t write(const void* buf, size_t len) override {
        const int n = SSL_write(ssl, buf, static_cast<int>(len));
        return n > 0 ? n : -1;
    }
};

struct Identity {
    EVP_PKEY* key = nullptr;
    X509* cert = nullptr;
};

Identity MakeIdentity(const std::string& cn, const std::string& san, bool ca, const Identity* issuer) {
    Identity id;
    id.key = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
    id.cert = X509_new();
    X509_set_version(id.cert, 2);
    static long serial = 100;
    ASN1_INTEGER_set(X509_get_serialNumber(id.cert), ++serial);
    X509_gmtime_adj(X509_getm_notBefore(id.cert), -3600);
    X509_gmtime_adj(X509_getm_notAfter(id.cert), 3600L * 24 * 30);
    X509_set_pubkey(id.cert, id.key);
    X509_NAME* name = X509_get_subject_name(id.cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1, 0);
    X509_set_issuer_name(id.cert, issuer ? X509_get_subject_name(issuer->cert) : name);
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, issuer ? issuer->cert : id.cert, id.cert, nullptr, nullptr, 0);
    auto add = [&](int nid, const char* value) {
        X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value);
        if (ext != nullptr) {
            X509_add_ext(id.cert, ext, -1);
            X509_EXTENSION_free(ext);
        }
    };
    add(NID_basic_constraints, ca ? "critical,CA:TRUE" : "critical,CA:FALSE");
    if (!san.empty()) add(NID_subject_alt_name, san.c_str());
    X509_sign(id.cert, issuer ? issuer->key : id.key, EVP_sha256());
    return id;
}

std::string Pem(X509* cert) {
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, cert);
    char* data = nullptr;
    const long n = BIO_get_mem_data(bio, &data);
    std::string out(data, static_cast<size_t>(n));
    BIO_free(bio);
    return out;
}

// A TLS server context presenting `leaf` followed by `chainExtra`.
SSL_CTX* ServerContext(const Identity& leaf, const Identity* chainExtra) {
    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX_use_certificate(ctx, leaf.cert);
    SSL_CTX_use_PrivateKey(ctx, leaf.key);
    if (chainExtra != nullptr) {
        X509_up_ref(chainExtra->cert);
        SSL_CTX_add_extra_chain_cert(ctx, chainExtra->cert);
    }
    return ctx;
}

Wrap TlsWrap(SSL_CTX* ctx) {
    return [ctx](int fd) -> std::unique_ptr<Stream> {
        SSL* ssl = SSL_new(ctx);
        SSL_set_fd(ssl, fd);
        if (SSL_accept(ssl) <= 0) {
            SSL_free(ssl);
            return nullptr;
        }
        return std::unique_ptr<Stream>(new TlsStream(ssl));
    };
}

#endif  // KUDROID_HAS_OPENSSL

// ── running the Java side ──────────────────────────────────────────────────────────────────

DexValue Str(const std::string& s) { return DexValue::Ref(g_linker->NewString(s.c_str())); }

void RunJava(const char* cls, const char* sig, std::vector<DexValue> args) {
    DexClass* klass = g_linker->FindClass(cls);
    if (klass == nullptr || !g_interp->EnsureInitialized(klass)) {
        std::printf("  FAIL class %s not found\n", cls);
        ++g_checks;
        ++g_failures;
        return;
    }
    DexMethod* method = klass->FindDirectMethod("run", sig);
    if (method == nullptr) {
        std::printf("  FAIL %s.run%s not found\n", cls, sig);
        ++g_checks;
        ++g_failures;
        return;
    }
    g_interp->ClearPendingException();
    const DexValue v = g_interp->Execute(method, args.data(), args.size());
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s threw %s\n", cls, g_interp->DescribePendingException().c_str());
        g_interp->ClearPendingException();
        ++g_checks;
        ++g_failures;
        return;
    }
    const std::string report = v.l != nullptr ? static_cast<DexString*>(v.l)->utf8 : "";
    std::istringstream lines(report);
    std::string line;
    while (std::getline(lines, line)) {
        ++g_checks;
        const bool ok = line.rfind("OK", 0) == 0;
        if (!ok) ++g_failures;
        std::printf("  %s %s\n", ok ? "OK  " : "FAIL", line.substr(line.find(' ') + 1).c_str());
    }
}

std::vector<uint8_t> ReadFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
    // A peer that closed early must not kill the harness.
    signal(SIGPIPE, SIG_IGN);
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::printf("=== KuART java.net HTTP and TLS ===\n");
    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex", &error)) {
        std::printf("  FAIL AddDexFile(framework.dex): %s\n", error.c_str());
        return 1;
    }
    const std::vector<uint8_t> testDex = ReadFile(KUART_JAVA_TEST_DEX);
    if (testDex.empty() || !linker.AddDexFile(testDex.data(), testDex.size(), "tests.dex", &error)) {
        std::printf("  FAIL AddDexFile(tests.dex): %s\n", error.c_str());
        return 1;
    }
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    interp.set_instruction_limit(8000ull * 1000ull * 1000ull);
    g_linker = &linker;
    g_interp = &interp;

    {
        std::printf("[http] plain HTTP client\n");
        Server http([](int fd) { return std::unique_ptr<Stream>(new PlainStream(fd)); });
        RunJava("LHttpTests;", "(I)Ljava/lang/String;", {DexValue::Int(http.port)});
    }

#ifdef KUDROID_HAS_OPENSSL
    {
        std::printf("[tls] TLS client\n");
        Identity ca = MakeIdentity("Test Root CA", "", true, nullptr);
        Identity leaf = MakeIdentity("localhost", "DNS:localhost,IP:127.0.0.1", false, &ca);
        Identity selfSigned = MakeIdentity("localhost", "DNS:localhost,IP:127.0.0.1", true, nullptr);
        Identity wrongName = MakeIdentity("other.example", "", false, &ca);

        const std::string caPem = Pem(ca.cert);
        const char* caPath = "/tmp/kuart_tls_test_ca.pem";
        std::ofstream(caPath) << caPem;
        setenv("KUDROID_TLS_CAFILE", caPath, 1);

        SSL_CTX* goodCtx = ServerContext(leaf, nullptr);
        SSL_CTX* selfCtx = ServerContext(selfSigned, nullptr);
        SSL_CTX* wrongCtx = ServerContext(wrongName, nullptr);
        Server good(TlsWrap(goodCtx));
        Server self(TlsWrap(selfCtx));
        Server wrong(TlsWrap(wrongCtx));
        RunJava("LTlsTests;", "(IIILjava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                {DexValue::Int(good.port), DexValue::Int(self.port), DexValue::Int(wrong.port),
                 Str(caPem), Str(Pem(selfSigned.cert))});
    }
#else
    std::printf("[tls] skipped: built without OpenSSL\n");
#endif

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
