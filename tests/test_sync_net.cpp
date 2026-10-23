// Run-level pins for the two library slices this lane owns: stdlib/sync
// (issue #56) and the completed blocking-socket surface in stdlib/networking
// (issue #58).
//
// The corpus already runs both samples through the agreement harnesses
// (Stage/Stage3/OptLevels compile AND run them on every compiler; that is
// where byte-determinism across the self-host stages is held). These pins own
// the behavior the samples cannot carry: a mutex held across two real
// threads, a closed port naming its refusal, and an HTTP fetch against a
// peer this test serverises itself.
//
// Each Fin program is compiled with the suite's finc binary (the reference
// compiler; the captures these programs need are a reference-compiler
// capability, recorded in lib/std/sync.fin's header) and run as a child.

#include <gtest/gtest.h>

#include <cstdio>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "Corpus.hpp"

#include "utils/Process.hpp"

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

// ---------------------------------------------------------------------------
// A temp source file + build + fate-aware run. The runner decodes waitpid
// directly (same shape as test_opt_agreement.cpp's, for the same reason:
// `runProcess` folds every signal into -1, and SIGABRT -- `blame` -- is a
// fate two of these pins assert on).
//
// ---------------------------------------------------------------------------

struct Proc {
    bool exited = false;
    int exitCode = -1;
    bool signaled = false;  // WIFSIGNALED -- termSig valid (WTERMSIG)
    int termSig = 0;
    std::string out;
    std::string err;
};

// Runs `bin` as a child, capturing fate and streams.
Proc runBinary(const std::string& bin) {
    const fs::path outPath = uniqueTempPath("syncnet_out");
    const fs::path errPath = uniqueTempPath("syncnet_err");
    Proc p;
#ifdef _WIN32
    p.exitCode = fin::runProcess({bin}, outPath.string(), errPath.string());
    p.exited = p.exitCode != -1;
#else
    const pid_t pid = ::fork();
    if (pid < 0) {
        p.err = "fork failed";
        return p;
    }
    if (pid == 0) {
        FILE* of = fopen(outPath.c_str(), "w");
        FILE* ef = fopen(errPath.c_str(), "w");
        if (of != nullptr) {
            dup2(fileno(of), STDOUT_FILENO);
            fclose(of);
        }
        if (ef != nullptr) {
            dup2(fileno(ef), STDERR_FILENO);
            fclose(ef);
        }
        char* argv[2];
        argv[0] = const_cast<char*>(bin.c_str());
        argv[1] = nullptr;
        execvp(bin.c_str(), argv);
        _exit(127);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
    }
    if (WIFEXITED(status)) {
        p.exited = true;
        p.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        p.signaled = true;
        p.termSig = WTERMSIG(status);
    }

#endif
    p.out = readProcessOutput(outPath.string());
    p.err = stripAnsi(readProcessOutput(errPath.string()));
    std::error_code ec;
    fs::remove(outPath, ec);
    fs::remove(errPath, ec);
    return p;
}

// Writes `source` to a temp file, compiles it with the suite's finc binary,
// runs the artifact, cleans up, and answers the fate. A build failure
// arrives as a Proc with the compiler's stderr prepended to `.err` and a
// `-1` exit code, so an EXPECT on the run outcome prints the true reason.
Proc buildRun(const std::string& source, const std::string& prefix) {
    const fs::path src = uniqueTempPath(prefix, ".fin");
    const fs::path exe = uniqueTempPath(prefix, "_exe");
    std::ofstream file(src);
    file << source;
    file.close();
    const FincRun built = runFinc({src.string(), "-o", exe.string()});
    if (built.exitCode != 0 || !fs::exists(exe)) {
        std::error_code ec;
        fs::remove(src, ec);
        Proc failed;
        failed.err = "compile failed: " + stripAnsi(built.err);
        return failed;
    }
    Proc p = runBinary(exe.string());
    std::error_code ec;
    fs::remove(src, ec);
    fs::remove(exe, ec);
    return p;
}

// ---------------------------------------------------------------------------
// A loopback peer. Set up is
// done on the calling thread -- bind/listen before any child exists, so a
// client's connect cannot race the listen -- and `serveOne` runs afterwards
// on a peer thread: read up to `want` request bytes, answer once,
// half-close. Every call is select()-bounded so a client that never arrives
// cannot hang the test process beyond 15 seconds.
// ---------------------------------------------------------------------------

#ifdef _WIN32
using SocketHandle = SOCKET;
using SocketLength = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
using SocketLength = socklen_t;
constexpr SocketHandle kInvalidSocket = -1;
#endif

class SocketRuntime {
public:
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#endif
    }
    ~SocketRuntime() {
#ifdef _WIN32
        if (ok_) WSACleanup();
#endif
    }
    bool ok() const { return ok_; }
private:
    bool ok_ = true;
};

void closeSocket(SocketHandle fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

class LoopbackPeer {
public:
    // A real listener on an ephemeral loopback port, open before any child.
    LoopbackPeer() {
        if (!runtime_.ok()) return;
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ == kInvalidSocket) {
            return;
        }
        int one = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            closeSocket(fd_);
            fd_ = kInvalidSocket;
            return;
        }
        SocketLength len = sizeof(addr);
        getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        if (listen(fd_, 8) != 0) {
            closeSocket(fd_);
            fd_ = kInvalidSocket;
        }
    }
    ~LoopbackPeer() {
        if (fd_ != kInvalidSocket) {
            closeSocket(fd_);
        }
    }
    LoopbackPeer(const LoopbackPeer&) = delete;
    LoopbackPeer& operator=(const LoopbackPeer&) = delete;

    bool ok() const { return fd_ != kInvalidSocket; }
    int port() const { return port_; }

    // One connection: reads until at least `want` request bytes arrived
    // (bounded), answers with `answer`, half-closes, closes. Answers what
    // it saw.
    std::string serveOne(int want, const std::string& answer) const {
        std::string got;
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd_, &rfds);
        timeval tv{};
        tv.tv_sec = 15;
        if (::select(static_cast<int>(fd_ + 1), &rfds, nullptr, nullptr, &tv) <= 0) {
            return "<accept timeout>";
        }
        sockaddr_in peer{};
        SocketLength plen = sizeof(peer);
        const SocketHandle conn = ::accept(fd_, reinterpret_cast<sockaddr*>(&peer), &plen);
        if (conn == kInvalidSocket) {
            return "<accept failed>";
        }
#ifdef _WIN32
        DWORD rv = 15000;
#else
        timeval rv{};
        rv.tv_sec = 15;
#endif
        setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rv), sizeof(rv));
        while (static_cast<int>(got.size()) < want) {
            char buf[64];
            const auto n = ::recv(conn, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            got.append(buf, static_cast<size_t>(n));
        }
        ::send(conn, answer.data(), static_cast<int>(answer.size()), 0);
#ifdef _WIN32
        ::shutdown(conn, SD_SEND);
#else
        ::shutdown(conn, SHUT_WR);
#endif
        closeSocket(conn);
        return got;
    }

private:
    SocketRuntime runtime_;
    SocketHandle fd_ = kInvalidSocket;
    int port_ = 0;
};

}  // namespace

// ---------------------------------------------------------------------------
// Sync (issue #56).
// ---------------------------------------------------------------------------

TEST(Soundness_SyncNet, SyncSampleRunsEndToEnd) {
    // The bundled sample, run as a child: spawn, blocking recv, join. "7" is
    // the answer either thread ordering gives -- recv waits until a value is
    // queued, so a nonblocking implementation would print -1 and the pinned
    // stdout would name the lost value instead.
    const fs::path src = fs::path(samplesDir()) / "stdlib" / "sync.fin";
    const fs::path exe = uniqueTempPath("sync_sample", "_exe");
    const FincRun built = runFinc({src.string(), "-o", exe.string()});
    ASSERT_EQ(built.exitCode, 0) << stripAnsi(built.err);
    const Proc p = runBinary(exe.string());
    std::error_code ec;
    fs::remove(exe, ec);
    ASSERT_TRUE(p.exited) << "the sample died on a signal; stderr: " << p.err;
    EXPECT_EQ(p.exitCode, 0) << "stderr: " << p.err;
    EXPECT_EQ(p.out, "7\n") << "stderr: " << p.err;
}

TEST(Soundness_SyncNet, MutexCountsAcrossThreads) {
    // Two threads, one guarded counter, one answer -- 2000 only if the mutex
    // actually excluded the increments across two real threads. Both workers
    // increment the same heap `Mutex`, so neither can run on its own copy.
    // The `new Mutex::<int>{...}` spelling is the one the reference compiler
    // accepts: the same literal without `::<int>` infers `Mutex<T>` and
    // refuses ("expected '&Mutex<int>', got '&Mutex<T>'"); the stages accept
    // both spellings. The pin takes the portable one.
    const std::string src = R"(import { Mutex, Thread } from sync::std;

let g_m <&Mutex<int>>;

fun bump() <noret> {
    let i <int> = 0;
    while (i < 1000) {
        let p <&int> = g_m.lock();
        *p = *p + 1;
        g_m.unlock();
        i = i + 1;
    }
}

fun main() <noret> {
    let m <Mutex<int>> = Mutex::<int>::create(0);
    g_m = new Mutex::<int>{m: m.m, v: m.v};
    let a <Thread> = Thread::spawn(() <noret> => { bump(); });
    let b <Thread> = Thread::spawn(() <noret> => { bump(); });
    a.join();
    b.join();
    let p <&int> = g_m.lock();
    printf("%d\n", *p);
    g_m.unlock();
}
)";
    const Proc p = buildRun(src, "sync_mutex");
    EXPECT_EQ(p.out, "2000\n") << "rc: " << p.exitCode << ", stderr: " << p.err;
}

TEST(Soundness_SyncNet, ThreadRunsCapturedBody) {
    const Proc p = buildRun(R"(import { Thread } from sync::std;
fun main() <noret> {
    let answer <int> = 42;
    let thread <Thread> = Thread::spawn(() <noret> => { printf("%d\n", answer); });
    answer = 99;
    thread.join();
}
)", "sync_capture");
    ASSERT_TRUE(p.exited) << p.err;
    EXPECT_EQ(p.exitCode, 0) << p.err;
    EXPECT_EQ(p.out, "42\n") << p.err;
}

TEST(Soundness_SyncNet, ChannelKeepsFifoOrderAndRefusesWhenFull) {
    // One thread, so FIFO order and the bounded capacity are the only things
    // under test: two accepted sends, a third refused by capacity, and two
    // dequeues in oldest-first order. The cross-thread side is the sample
    // pin's job -- and its blocking `recv` is proven there: the worker has
    // not sent when main blocks on recv on every ordering.
    const std::string src = R"(import { Channel } from sync::std;
import { Outcome } from outcome::std;

fun main() <noret> {
    let c <Channel<int>> = Channel::<int>::create(2);
    let f1 <bool> = c.send(1);
    let f2 <bool> = c.send(2);
    let full <bool> = c.send(3);
    let r1 <Outcome<int>> = c.recv();
    let r2 <Outcome<int>> = c.recv();
    printf("%d %d %d %d %d\n", f1, f2, full, r1.unwrap_or(-1), r2.unwrap_or(-1));
}
)";
    const Proc p = buildRun(src, "sync_chan");
    EXPECT_EQ(p.out, "1 1 0 1 2\n") << "rc: " << p.exitCode << ", stderr: " << p.err;
}

// ---------------------------------------------------------------------------
// Networking (issue #58).
// ---------------------------------------------------------------------------

TEST(Soundness_SyncNet, TcpWrappersRunAnEcho) {
    // bind/listen on the test thread before the child exists, so the child's
    // connect cannot race the listen; the peer thread answers one connection.
    LoopbackPeer peer;
    ASSERT_TRUE(peer.ok()) << "the test peer could not bind loopback";
    const std::string src =
        "import { TcpStream } from networking::std;\n"
        "\n"
        "fun main() <noret> {\n"
        "    let s <TcpStream> = TcpStream::connect(\"127.0.0.1\", " +
        std::to_string(peer.port()) +
        ");\n"
        "    let wrote <int> = s.write(\"ping\", 4);\n"
        "    let got <[char]> = s.read(16);\n"
        "    printf(\"%d %c%c%c%c\\n\", wrote, got[0], got[1], got[2], got[3]);\n"
        "    s.close();\n"
        "}\n";
    const fs::path srcPath = uniqueTempPath("net_echo", ".fin");
    {
        std::ofstream file(srcPath);
        file << src;
    }
    const fs::path exe = uniqueTempPath("net_echo", "_exe");
    const FincRun built = runFinc({srcPath.string(), "-o", exe.string()});
    ASSERT_EQ(built.exitCode, 0) << stripAnsi(built.err);
    std::string seen;
    std::thread t([&peer, &seen]() { seen = peer.serveOne(4, "pong"); });
    const Proc p = runBinary(exe.string());
    t.join();
    std::error_code ec;
    fs::remove(srcPath, ec);
    fs::remove(exe, ec);
    EXPECT_EQ(seen, "ping") << "the peer read: " << seen;
    EXPECT_EQ(p.out, "4 pong\n") << "rc: " << p.exitCode << ", stderr: " << p.err;
}

TEST(Soundness_SyncNet, AcceptedStreamSurvivesListenerClose) {
    const Proc p = buildRun(R"(import { TcpStream, TcpListener } from networking::std;
fun main() <noret> {
    let listener <TcpListener> = TcpListener::bind(0, 4);
    let client <TcpStream> = TcpStream::connect("127.0.0.1", listener.local_port());
    let accepted <TcpStream> = listener.accept();
    blame listener.close();
    let replacement <TcpListener> = TcpListener::bind(0, 4);
    blame !listener.close();
    blame replacement.local_port() > 0;
    blame replacement.close();
    blame client.write("ping", 4) == 4;
    let got <[char]> = accepted.read(4);
    blame got[0] == 'p' && got[3] == 'g';
    delete got;
    blame accepted.close();
    blame !accepted.close();
    blame client.close();
    blame !client.close();
    printf("closed\n");
}
)", "net_listener_lifetime");
    ASSERT_TRUE(p.exited) << p.err;
    EXPECT_EQ(p.exitCode, 0) << p.err;
    EXPECT_EQ(p.out, "closed\n") << p.err;
}

TEST(Soundness_SyncNet, ConnectRefusalNamesTheFailure) {
    // A port we close on the spot: nothing listens on it from here on, so
    // connect refuses immediately rather than hanging. The ruling's
    // contract is that refusal is `blame`, and the message names the call.
    SocketRuntime runtime;
    ASSERT_TRUE(runtime.ok());
    const SocketHandle fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(fd, kInvalidSocket);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ASSERT_EQ(bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    SocketLength len = sizeof(addr);
    ASSERT_EQ(getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len), 0);
    const int port = ntohs(addr.sin_port);
    closeSocket(fd);
    const std::string src =
        "import { TcpStream } from networking::std;\n"
        "\n"
        "fun main() <noret> {\n"
        "    let s <TcpStream> = TcpStream::connect(\"127.0.0.1\", " +
        std::to_string(port) +
        ");\n"
        "    s.close();\n"
        "}\n";
    const Proc p = buildRun(src, "net_refuse");
#ifdef _WIN32
    EXPECT_TRUE(p.exited);
    EXPECT_NE(p.exitCode, 0) << p.err;
#else
    EXPECT_TRUE(p.signaled) << "a refused connect must blame (SIGABRT), rc: "
                            << p.exitCode << ", stderr: " << p.err;
    EXPECT_EQ(p.termSig, SIGABRT) << "refusal is a blame abort like every libc-ruled failure";
#endif
    EXPECT_TRUE(p.err.find("cannot connect") != std::string::npos)
        << "blamed with: " << p.err;
}

TEST(Soundness_SyncNet, HttpGetServesAgainstATestPeer) {
    // The stretch demo against a peer that reads the whole request and
    // answers one fixed response: the pinned child output is the whole wire,
    // headers included, byte for byte.
    LoopbackPeer peer;
    ASSERT_TRUE(peer.ok());
    const std::string src =
        "import { http_get } from networking::std;\n"
        "\n"
        "fun main() <int> {\n"
        "    let body <string> = http_get(\"127.0.0.1\", " +
        std::to_string(peer.port()) +
        ", \"/hi\");\n"
        "    printf(\"%s\", body);\n"
        "    return 0;\n"
        "}\n";
    const fs::path srcPath = uniqueTempPath("net_http", ".fin");
    {
        std::ofstream file(srcPath);
        file << src;
    }
    const fs::path exe = uniqueTempPath("net_http", "_exe");
    const FincRun built = runFinc({srcPath.string(), "-o", exe.string()});
    ASSERT_EQ(built.exitCode, 0) << stripAnsi(built.err);
    const std::string response = "HTTP/1.0 200 OK\r\n\r\nhello";
    std::string seen;
    std::thread t([&peer, &seen, &response]() {
        seen = peer.serveOne(37, response);
    });
    const Proc p = runBinary(exe.string());
    t.join();
    std::error_code ec;
    fs::remove(srcPath, ec);
    fs::remove(exe, ec);
    EXPECT_TRUE(seen.find("GET /hi HTTP/1.0") != std::string::npos)
        << "the peer read: " << seen;
    EXPECT_TRUE(seen.find("Host: 127.0.0.1") != std::string::npos)
        << "the peer read: " << seen;
    EXPECT_EQ(p.out, response) << "rc: " << p.exitCode << ", stderr: " << p.err;
}
