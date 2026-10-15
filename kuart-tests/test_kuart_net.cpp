// test_kuart_net.cpp - java.net over real host sockets, run through the real framework.dex:
// address parsing, TCP client and server, UDP, timeouts and refusals. Peers are plain POSIX
// sockets on loopback, so a failure here is in the Java classes or the KuNet natives.
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

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

using kudroid::kuart::DexArray;
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

void Check(bool condition, const char* message) {
    ++g_checks;
    std::printf("%s %s\n", condition ? "  OK  " : "  FAIL", message);
    if (!condition) ++g_failures;
}

struct Result {
    bool ok = false;            // found and ran without throwing
    DexValue v;
    std::string exception;      // descriptor of what was thrown, if anything
};

Result Run(DexMethod* method, std::vector<DexValue> args) {
    Result r;
    if (method == nullptr) return r;
    g_interp->ClearPendingException();
    const DexValue value = g_interp->Execute(method, args.data(), args.size());
    if (g_interp->HasPendingException()) {
        DexObject* ex = g_interp->pending_exception();
        r.exception = ex != nullptr && ex->clazz != nullptr ? ex->clazz->descriptor : "?";
        g_interp->ClearPendingException();
        return r;
    }
    r.ok = true;
    r.v = value;
    return r;
}

Result Stat(const char* descriptor, const char* name, const char* sig, std::vector<DexValue> args) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub || !g_interp->EnsureInitialized(klass)) return {};
    DexMethod* method = klass->FindDirectMethod(name, sig);
    if (method == nullptr) method = klass->FindVirtualMethod(name, sig);
    return Run(method, std::move(args));
}

Result Virt(DexObject* receiver, const char* name, const char* sig, std::vector<DexValue> args = {}) {
    if (receiver == nullptr) return {};
    DexMethod* method = receiver->clazz->FindVirtualMethod(name, sig);
    if (method == nullptr) method = receiver->clazz->FindDirectMethod(name, sig);
    args.insert(args.begin(), DexValue::Ref(receiver));
    return Run(method, std::move(args));
}

Result New(const char* descriptor, const char* sig, std::vector<DexValue> args) {
    Result r;
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub || !g_interp->EnsureInitialized(klass)) return r;
    DexObject* object = g_linker->AllocObject(klass);
    DexMethod* ctor = klass->FindDirectMethod("<init>", sig);
    if (object == nullptr || ctor == nullptr) return r;
    args.insert(args.begin(), DexValue::Ref(object));
    r = Run(ctor, std::move(args));
    r.v = DexValue::Ref(object);
    return r;
}

DexValue Str(const char* s) { return DexValue::Ref(g_linker->NewString(s)); }

DexArray* NewBytes(int n) { return g_linker->AllocArray(g_linker->FindClass("[B"), n); }

DexArray* BytesOf(const std::string& s) {
    DexArray* a = NewBytes(static_cast<int>(s.size()));
    std::memcpy(a->Data(), s.data(), s.size());
    return a;
}

std::string StringOf(DexObject* o) {
    return o == nullptr ? "<null>" : std::string(static_cast<DexString*>(o)->utf8);
}

std::string Text(DexObject* receiver, const char* name) {
    Result r = Virt(receiver, name, "()Ljava/lang/String;");
    return r.ok ? StringOf(r.v.l) : "<error " + r.exception + ">";
}

// A listening loopback TCP socket on an ephemeral port.
struct Listener {
    int fd = -1;
    int port = 0;
    Listener() {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        listen(fd, 4);
        socklen_t len = sizeof(a);
        getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
        port = ntohs(a.sin_port);
    }
    ~Listener() { if (fd >= 0) close(fd); }
};

DexObject* LoopbackSocketAddress(int port) {
    DexArray* ip = BytesOf(std::string("\x7f\x00\x00\x01", 4));
    Result addr = Stat("Ljava/net/InetAddress;", "getByAddress", "([B)Ljava/net/InetAddress;",
                       {DexValue::Ref(ip)});
    Result sa = New("Ljava/net/InetSocketAddress;", "(Ljava/net/InetAddress;I)V",
                    {addr.v, DexValue::Int(port)});
    return sa.v.l;
}

void test_addresses() {
    std::printf("[net] InetAddress parsing and resolution\n");
    auto by_name = [](const char* host) {
        return Stat("Ljava/net/InetAddress;", "getByName", "(Ljava/lang/String;)Ljava/net/InetAddress;",
                    {Str(host)});
    };
    Result r = by_name("127.0.0.1");
    Check(r.ok && Text(r.v.l, "getHostAddress") == "127.0.0.1", "an IPv4 literal keeps its text");
    Check(r.ok && Virt(r.v.l, "isLoopbackAddress", "()Z").v.i == 1, "127.0.0.1 is loopback");

    r = by_name("::1");
    Check(r.ok && Text(r.v.l, "getHostAddress") == "0:0:0:0:0:0:0:1", "::1 expands to eight groups");
    r = by_name("2001:db8::ff00:42:8329");
    Check(r.ok && Text(r.v.l, "getHostAddress") == "2001:db8:0:0:0:ff00:42:8329",
          "a compressed IPv6 literal expands");
    r = by_name("[fe80::1]");
    Check(r.ok && Text(r.v.l, "getHostAddress") == "fe80:0:0:0:0:0:0:1", "brackets are accepted");
    r = by_name("::ffff:10.1.2.3");
    Check(r.ok && r.v.l->clazz == g_linker->FindClass("Ljava/net/Inet4Address;") &&
              Text(r.v.l, "getHostAddress") == "10.1.2.3",
          "a v4-mapped IPv6 literal becomes an Inet4Address");
    r = by_name("localhost");
    Check(r.ok && Virt(r.v.l, "isLoopbackAddress", "()Z").v.i == 1, "localhost resolves to loopback");
    r = by_name("no-such-host.invalid");
    Check(!r.ok && r.exception == "Ljava/net/UnknownHostException;",
          "an unknown name throws UnknownHostException");

    DexArray* ten = BytesOf(std::string("\x0a\x00\x00\x05", 4));
    r = Stat("Ljava/net/InetAddress;", "getByAddress", "([B)Ljava/net/InetAddress;",
             {DexValue::Ref(ten)});
    Check(r.ok && Text(r.v.l, "toString") == "/10.0.0.5", "toString of an unnamed address");
    Check(r.ok && Virt(r.v.l, "isSiteLocalAddress", "()Z").v.i == 1, "10.0.0.5 is site-local");
    DexArray* bad = BytesOf("abc");
    r = Stat("Ljava/net/InetAddress;", "getByAddress", "([B)Ljava/net/InetAddress;",
             {DexValue::Ref(bad)});
    Check(!r.ok && r.exception == "Ljava/net/UnknownHostException;", "a 3-byte address is rejected");
}

void test_tcp_client() {
    std::printf("[net] Socket as a TCP client\n");
    Listener server;
    std::string seen;
    std::thread peer([&] {
        int c = accept(server.fd, nullptr, nullptr);
        char buf[16] = {};
        ssize_t n = read(c, buf, sizeof(buf));
        for (ssize_t i = 0; i < n; ++i) buf[i] = static_cast<char>(toupper(buf[i]));
        seen.assign(buf, n > 0 ? static_cast<size_t>(n) : 0);
        write(c, buf, n > 0 ? static_cast<size_t>(n) : 0);
        close(c);
    });

    Result sock = New("Ljava/net/Socket;", "()V", {});
    Result conn = Virt(sock.v.l, "connect", "(Ljava/net/SocketAddress;I)V",
                       {DexValue::Ref(LoopbackSocketAddress(server.port)), DexValue::Int(3000)});
    Check(conn.ok, "connect to a loopback listener succeeds");
    Check(Virt(sock.v.l, "isConnected", "()Z").v.i == 1, "isConnected after connect");
    Check(Virt(sock.v.l, "getPort", "()I").v.i == server.port, "getPort is the remote port");
    Check(Virt(sock.v.l, "getLocalPort", "()I").v.i > 0, "getLocalPort is assigned");
    Check(Text(Virt(sock.v.l, "getInetAddress", "()Ljava/net/InetAddress;").v.l, "getHostAddress") ==
              "127.0.0.1",
          "getInetAddress is the peer");
    Check(Virt(sock.v.l, "setTcpNoDelay", "(Z)V", {DexValue::Int(1)}).ok &&
              Virt(sock.v.l, "getTcpNoDelay", "()Z").v.i == 1,
          "TCP_NODELAY round-trips");
    Check(Virt(sock.v.l, "setSoTimeout", "(I)V", {DexValue::Int(2000)}).ok &&
              Virt(sock.v.l, "getSoTimeout", "()I").v.i == 2000,
          "SO_TIMEOUT round-trips");

    Result out = Virt(sock.v.l, "getOutputStream", "()Ljava/io/OutputStream;");
    DexArray* hello = BytesOf("hello");
    Check(Virt(out.v.l, "write", "([B)V", {DexValue::Ref(hello)}).ok, "write 5 bytes");
    Result in = Virt(sock.v.l, "getInputStream", "()Ljava/io/InputStream;");
    DexArray* buf = NewBytes(16);
    Result n = Virt(in.v.l, "read", "([BII)I", {DexValue::Ref(buf), DexValue::Int(0), DexValue::Int(16)});
    Check(n.ok && n.v.i == 5 && std::memcmp(buf->Data(), "HELLO", 5) == 0, "read the echoed bytes");
    Result eof = Virt(in.v.l, "read", "([BII)I", {DexValue::Ref(buf), DexValue::Int(0), DexValue::Int(16)});
    Check(eof.ok && eof.v.i == -1, "end of stream reads -1");
    peer.join();
    Check(seen == "HELLO", "the peer received what Java wrote");

    Check(Virt(sock.v.l, "close", "()V").ok && Virt(sock.v.l, "isClosed", "()Z").v.i == 1, "close");
    Result after = Virt(in.v.l, "read", "([BII)I", {DexValue::Ref(buf), DexValue::Int(0), DexValue::Int(16)});
    Check(!after.ok && after.exception == "Ljava/net/SocketException;",
          "reading a closed socket throws SocketException");
}

void test_connect_failures() {
    std::printf("[net] connect refusals and read timeouts\n");
    int dead_port;
    {
        Listener gone;
        dead_port = gone.port;
    }  // closed: nothing listens here now
    Result sock = New("Ljava/net/Socket;", "()V", {});
    Result conn = Virt(sock.v.l, "connect", "(Ljava/net/SocketAddress;I)V",
                       {DexValue::Ref(LoopbackSocketAddress(dead_port)), DexValue::Int(2000)});
    Check(!conn.ok && conn.exception == "Ljava/net/ConnectException;",
          "connecting to a closed port throws ConnectException");

    Listener silent;
    std::thread peer([&] {
        int c = accept(silent.fd, nullptr, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        close(c);
    });
    Result s2 = New("Ljava/net/Socket;", "()V", {});
    Virt(s2.v.l, "connect", "(Ljava/net/SocketAddress;I)V",
         {DexValue::Ref(LoopbackSocketAddress(silent.port)), DexValue::Int(2000)});
    Virt(s2.v.l, "setSoTimeout", "(I)V", {DexValue::Int(200)});
    Result in = Virt(s2.v.l, "getInputStream", "()Ljava/io/InputStream;");
    DexArray* buf = NewBytes(8);
    const auto t0 = std::chrono::steady_clock::now();
    Result n = Virt(in.v.l, "read", "([BII)I", {DexValue::Ref(buf), DexValue::Int(0), DexValue::Int(8)});
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();
    Check(!n.ok && n.exception == "Ljava/net/SocketTimeoutException;",
          "a silent peer makes read throw SocketTimeoutException");
    Check(ms >= 150 && ms < 650, "the timeout fires near SO_TIMEOUT, not at connection close");
    Virt(s2.v.l, "close", "()V");
    peer.join();
}

void test_server_socket() {
    std::printf("[net] ServerSocket\n");
    Result server = New("Ljava/net/ServerSocket;", "(I)V", {DexValue::Int(0)});
    Check(server.ok, "ServerSocket(0) binds");
    const int port = Virt(server.v.l, "getLocalPort", "()I").v.i;
    Check(port > 0, "the ephemeral port is reported");

    Virt(server.v.l, "setSoTimeout", "(I)V", {DexValue::Int(200)});
    Result timeout = Virt(server.v.l, "accept", "()Ljava/net/Socket;");
    Check(!timeout.ok && timeout.exception == "Ljava/net/SocketTimeoutException;",
          "accept with no client times out");
    Virt(server.v.l, "setSoTimeout", "(I)V", {DexValue::Int(3000)});

    std::string reply;
    std::thread client([&] {
        int c = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<uint16_t>(port));
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(c, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
            write(c, "ping", 4);
            char buf[8] = {};
            ssize_t n = read(c, buf, sizeof(buf));
            reply.assign(buf, n > 0 ? static_cast<size_t>(n) : 0);
        }
        close(c);
    });
    Result accepted = Virt(server.v.l, "accept", "()Ljava/net/Socket;");
    Check(accepted.ok && accepted.v.l != nullptr, "accept returns the client's socket");
    Check(Text(Virt(accepted.v.l, "getInetAddress", "()Ljava/net/InetAddress;").v.l,
               "getHostAddress") == "127.0.0.1",
          "an IPv4 client appears as 127.0.0.1 even on a dual-stack listener");
    Result in = Virt(accepted.v.l, "getInputStream", "()Ljava/io/InputStream;");
    DexArray* buf = NewBytes(8);
    Result n = Virt(in.v.l, "read", "([BII)I", {DexValue::Ref(buf), DexValue::Int(0), DexValue::Int(8)});
    Check(n.ok && n.v.i == 4 && std::memcmp(buf->Data(), "ping", 4) == 0, "server read the request");
    Result out = Virt(accepted.v.l, "getOutputStream", "()Ljava/io/OutputStream;");
    Virt(out.v.l, "write", "([B)V", {DexValue::Ref(BytesOf("pong"))});
    Virt(accepted.v.l, "close", "()V");
    client.join();
    Check(reply == "pong", "the client received the server's reply");
    Check(Virt(server.v.l, "close", "()V").ok && Virt(server.v.l, "isClosed", "()Z").v.i == 1,
          "ServerSocket closes");
}

void test_udp() {
    std::printf("[net] DatagramSocket\n");
    Result sock = New("Ljava/net/DatagramSocket;", "(I)V", {DexValue::Int(0)});
    Check(sock.ok, "DatagramSocket(0) binds");
    const int port = Virt(sock.v.l, "getLocalPort", "()I").v.i;
    Check(port > 0, "the ephemeral UDP port is reported");

    int peer = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in me{};
    me.sin_family = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(peer, reinterpret_cast<sockaddr*>(&me), sizeof(me));
    socklen_t mlen = sizeof(me);
    getsockname(peer, reinterpret_cast<sockaddr*>(&me), &mlen);
    const int peerPort = ntohs(me.sin_port);

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(static_cast<uint16_t>(port));
    dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(peer, "dgram", 5, 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));

    DexArray* data = NewBytes(32);
    Result packet = New("Ljava/net/DatagramPacket;", "([BI)V", {DexValue::Ref(data), DexValue::Int(32)});
    Virt(sock.v.l, "setSoTimeout", "(I)V", {DexValue::Int(3000)});
    Result got = Virt(sock.v.l, "receive", "(Ljava/net/DatagramPacket;)V", {DexValue::Ref(packet.v.l)});
    Check(got.ok, "receive returns the datagram");
    Check(Virt(packet.v.l, "getLength", "()I").v.i == 5 && std::memcmp(data->Data(), "dgram", 5) == 0,
          "length and payload match");
    Check(Virt(packet.v.l, "getPort", "()I").v.i == peerPort, "the sender's port is recorded");

    Result reply = New("Ljava/net/DatagramPacket;", "([BILjava/net/InetAddress;I)V",
                       {DexValue::Ref(BytesOf("echo")), DexValue::Int(4),
                        Virt(packet.v.l, "getAddress", "()Ljava/net/InetAddress;").v,
                        DexValue::Int(peerPort)});
    Check(Virt(sock.v.l, "send", "(Ljava/net/DatagramPacket;)V", {DexValue::Ref(reply.v.l)}).ok,
          "send the reply");
    char buf[16] = {};
    timeval tv{2, 0};
    setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ssize_t n = recv(peer, buf, sizeof(buf), 0);
    Check(n == 4 && std::memcmp(buf, "echo", 4) == 0, "the peer received the reply");

    // A packet reused after a short datagram still has its full capacity.
    sendto(peer, "this is a longer datagram", 25, 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    Result again = Virt(sock.v.l, "receive", "(Ljava/net/DatagramPacket;)V", {DexValue::Ref(packet.v.l)});
    Check(again.ok && Virt(packet.v.l, "getLength", "()I").v.i == 25,
          "a reused packet is not truncated to the previous length");

    Virt(sock.v.l, "setSoTimeout", "(I)V", {DexValue::Int(150)});
    Result none = Virt(sock.v.l, "receive", "(Ljava/net/DatagramPacket;)V", {DexValue::Ref(packet.v.l)});
    Check(!none.ok && none.exception == "Ljava/net/SocketTimeoutException;",
          "receive with nothing pending times out");
    Virt(sock.v.l, "close", "()V");
    close(peer);
}

}  // namespace

int main() {
    std::printf("=== KuART java.net ===\n");
    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex", &error)) {
        std::printf("  FAIL AddDexFile(framework.dex): %s\n", error.c_str());
        return 1;
    }
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    interp.set_instruction_limit(4000ull * 1000ull * 1000ull);
    g_linker = &linker;
    g_interp = &interp;

    test_addresses();
    test_tcp_client();
    test_connect_failures();
    test_server_socket();
    test_udp();

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
