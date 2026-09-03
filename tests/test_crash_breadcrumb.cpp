// test_crash_breadcrumb.cpp — the one line that must survive a handler that never returns.
//
// Why this exists. ULTRAKILL took SIGABRT on UnityMain with a guest handler installed,
// and the run produced NO kudroid_crash.log at all: il2cpp's SIGABRT handler siglongjmps
// out to its own recovery point, so every line of reporting below the dispatch call was
// skipped. What reached the log was `fatal-signal sig=6 fault_addr=0x1da7b81dc si_code=0`
// — and for an abort() the fault address is merely the pc of abort inside libsystem, so
// that line named the signal and nothing about who raised it. The abort message the guest
// had already handed to android_set_abort_message was sitting in memory, written nowhere.
//
// So the breadcrumb is not a nicety here, it is the only report for a whole class of
// crash, and these tests drive the real crashHandler through a real signal to check it
// carries what a reader needs.
//
// What this cannot check on a Linux x86-64 host: the pc/lr fields, which are extracted
// under `#if defined(__aarch64__)`. They are asserted absent here rather than ignored, so
// this file states plainly which half is covered — the last two rounds each shipped an
// Apple-only path that had never executed anywhere.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
void kudroid_set_log_dir(const char* dir);
void kudroid_store_abort_message(const char* msg);
void kudroid_persistent_breadcrumb(const char* line);
}

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

std::string g_logDir;
std::string g_tmpDir;

std::string read_breadcrumbs() {
    std::ifstream in(g_logDir + "/native_breadcrumbs.log");
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The breadcrumb line for a given prefix, or empty. Breadcrumbs are one record per line
// prefixed with t_ns=, so a line is the unit to assert about.
std::string find_line(const std::string& haystack, const std::string& needle) {
    size_t pos = 0;
    while (pos < haystack.size()) {
        const size_t eol = haystack.find('\n', pos);
        const std::string line = haystack.substr(pos, eol == std::string::npos ? std::string::npos
                                                                               : eol - pos);
        if (line.find(needle) != std::string::npos) return line;
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
    return {};
}

// ─── the field names a reader greps for ──────────────────────────────────────

// `sig=` is already taken. native-enter and native-exit print the Java type signature
// under that name, and one captured run had 76 lines using it that way against this
// line's single one — so grepping `sig=` for a signal number returns almost entirely the
// wrong records, with the one that matters buried. This is about being able to find the
// line at all.
void test_the_signal_number_is_not_called_sig() {
    std::printf("[fields] the signal number is signo=, because sig= means a Java signature\n");

    // A native-enter breadcrumb, written the way NativeCallTelemetry writes it, so the
    // collision is demonstrated rather than described.
    kudroid_persistent_breadcrumb(
        "native-enter class=Lbitter/jnibridge/JNIBridge; method=invoke "
        "sig=(JLjava/lang/Class;Ljava/lang/reflect/Method;[Ljava/lang/Object;)Ljava/lang/Object; "
        "args=4 vm_depth=1");

    const std::string log = read_breadcrumbs();
    const std::string fatal = find_line(log, "fatal-signal");
    Check(!fatal.empty(), "the fatal-signal line is in the breadcrumb file");
    if (fatal.empty()) return;

    Check(fatal.find("signo=") != std::string::npos,
          "it carries signo=, which no other record uses");

    // The collision itself: `sig=` must not appear on this line, or a grep for signals
    // cannot distinguish it from a Java signature.
    Check(fatal.find("sig=") == fatal.find("signo=") ||
              fatal.find(" sig=") == std::string::npos,
          "and no bare sig= field that a Java-signature grep would also match");

    // And the other direction: the Java line is still found by its own name, so renaming
    // one field did not make the other unfindable.
    Check(find_line(log, "native-enter").find("sig=(") != std::string::npos,
          "the Java signature record still uses sig= and is unaffected");
}

// The numeric thread id, because every other breadcrumb prints it and a name alone cannot
// be joined to them. The captured run had `thread=UnityMain` on this line and
// `thread_id=3453262` on the watchdog line, the stall reports and the thread samples: the
// records described the same thread and nothing could match them automatically.
void test_the_thread_is_identified_the_same_way_as_every_other_record() {
    std::printf("[fields] the faulting thread carries thread_id=, like the other records\n");
    const std::string fatal = find_line(read_breadcrumbs(), "fatal-signal");
    Check(!fatal.empty(), "the fatal-signal line is present");
    if (fatal.empty()) return;

    Check(fatal.find("thread_id=") != std::string::npos,
          "thread_id= is present, so the line joins to the watchdog and stall reports");

    // Not zero: a placeholder would satisfy the grep and answer nothing.
    const size_t at = fatal.find("thread_id=");
    const std::string rest = fatal.substr(at + std::strlen("thread_id="));
    const unsigned long long tid = std::strtoull(rest.c_str(), nullptr, 10);
    Check(tid != 0ULL, "and it is a real thread id rather than 0");

    Check(fatal.find("thread=") != std::string::npos,
          "the human-readable name is still there too");
}

// ─── the guest's own statement of what went wrong ────────────────────────────

// android_set_abort_message is how a bionic guest says why it is aborting, and KuDroid
// already captures it. It used to be printed only inside kudroid_crash.log — the file a
// non-returning handler never reaches — so for the crash that actually happened it was
// captured and then discarded.
void test_the_abort_message_reaches_the_breadcrumb() {
    std::printf("[abort] the guest's abort message is on the line, not only in the crash log\n");
    const std::string fatal = find_line(read_breadcrumbs(), "fatal-signal");
    Check(!fatal.empty(), "the fatal-signal line is present");
    if (fatal.empty()) return;

    Check(fatal.find("abort_message=") != std::string::npos,
          "abort_message= is on the breadcrumb line");
    Check(fatal.find("test abort: sem_wait failed in a handshake") != std::string::npos,
          "and it is the text the guest actually set");
    Check(fatal.find("abort_message=\"") != std::string::npos,
          "quoted, so a message containing spaces stays one field");
}

// ─── what this host cannot execute ───────────────────────────────────────────

// Stated as a check rather than left implicit. pc/lr are extracted under
// `#if defined(__aarch64__)`, so on x86-64 they are absent by design — and an assertion
// that says so is what keeps "absent because the platform differs" from being confused
// with "absent because it broke".
void test_pc_and_lr_are_arm64_only_and_this_host_is_not() {
    std::printf("[coverage] pc/lr are arm64-only; this host's coverage is stated, not assumed\n");
    const std::string fatal = find_line(read_breadcrumbs(), "fatal-signal");
    if (fatal.empty()) { Check(false, "the fatal-signal line is present"); return; }

#if defined(__aarch64__) || defined(__arm64__)
    Check(fatal.find("pc=") != std::string::npos, "pc= is present on arm64");
    Check(fatal.find("lr=") != std::string::npos, "lr= is present on arm64");
#else
    Check(fatal.find("pc=") == std::string::npos,
          "pc= is absent on a non-arm64 host, as the #if says — NOT covered here");
    std::printf("  NOTE  pc/lr extraction and guest-module attribution are exercised only\n");
    std::printf("        by the arm64 build; this host cannot execute that branch.\n");
#endif
}

// ─── driving the real handler ────────────────────────────────────────────────

// Raise a genuine signal on a spawned thread and let the real crashHandler run.
//
// A spawned thread, not the main one: crashHandler re-raises with SIG_DFL on the main
// thread (correct — the process is finished) and parks a background thread forever so the
// launcher UI survives. Parking is what makes this testable at all: the breadcrumb has
// been written by then, and the test's main thread can read it.
//
// The thread is deliberately never joined. It is parked with every signal blocked and will
// not return; process exit is what reaps it.
void raise_a_real_signal_on_a_background_thread() {
    std::atomic<bool> raised{false};
    std::thread([&raised] {
        pthread_setname_np(pthread_self(), "TestFaultThread");
        raised = true;
        // SIGABRT, because that is the signal the captured crash used and the one whose
        // reporting path a non-returning guest handler skips.
        ::raise(SIGABRT);
    }).detach();

    while (!raised.load()) std::this_thread::yield();
    // The handler writes the breadcrumb first of all, but it is another thread: give it
    // room to get there. Polling the file is what ends the wait, not the sleep.
    for (int i = 0; i < 200; ++i) {
        if (!find_line(read_breadcrumbs(), "fatal-signal").empty()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

}  // namespace

int main() {
    std::printf("=== crash breadcrumb ===\n");

    // A private log directory. kudroid_set_log_dir also installs the crash handlers,
    // which is what makes the raise below reach crashHandler.
    char tmpl[] = "/tmp/kudroid_crumb_XXXXXX";
    const char* dir = ::mkdtemp(tmpl);
    if (dir == nullptr) {
        std::printf("  FAIL could not create a temporary log directory\n");
        return 1;
    }
    g_tmpDir = dir;
    g_logDir = std::string(dir) + "/logs";
    kudroid_set_log_dir(dir);

    // What a bionic guest does on its way to abort(). Set before the signal, exactly as
    // the real sequence has it.
    kudroid_store_abort_message("test abort: sem_wait failed in a handshake");

    raise_a_real_signal_on_a_background_thread();

    test_the_signal_number_is_not_called_sig();
    test_the_thread_is_identified_the_same_way_as_every_other_record();
    test_the_abort_message_reaches_the_breadcrumb();
    test_pc_and_lr_are_arm64_only_and_this_host_is_not();

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);

    // Remove the temporary directory. Files first, since rmdir needs it empty; the set is
    // known and small, and unlink on an absent file failing is fine.
    for (const char* name : {"native_breadcrumbs.log", "kudroid_version.txt", "classes.log",
                             "kudroid_crash.log", "stderr.log", "kudroid_android_logs.txt"}) {
        ::unlink((g_logDir + "/" + name).c_str());
    }
    ::rmdir(g_logDir.c_str());
    ::rmdir(g_tmpDir.c_str());

    // The parked thread is still there by design; _exit avoids running static destructors
    // while a thread sits inside the crash handler holding an unknown amount of state.
    std::fflush(stdout);
    ::_exit(g_failures == 0 ? 0 : 1);
}
