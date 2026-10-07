// fintest — minimal end-to-end runner for `*_test.fin` files.
//
// Usage: fintest [--timeout Ns] [roots...]  (default root: ./fintest/)
//        fintest [--timeout Ns] --file <path>  (single file, for per-file ctest registration)
//
// For each discovered test: compile with `finc --color=never <file> -o <tmp>`
// in a fresh per-test temp dir, then run the result with a waitpid deadline
// (default 10s, `--timeout Ns` overrides). A test passes iff the compile exits
// 0 with empty stdout and the run exits 0. Results go to stdout as TAP v13.
//
// Outcome classes (one TAP rendering each, no second format): pass;
// compile-fail (finc exit 1/2/3 + first diagnostic lines); run-fail (a
// `file:line: assertion failed[: msg]` line in the run output, extracted);
// timeout (`timeout after Ns`); crashed (`crashed (signal N)`). A finc crash
// is a test failure (`compiler crashed (signal N)`), never runner-internal:
// exit 3 is fintest's own breakage only. Crash decoding uses WIFEXITED /
// WIFSIGNALED on waitpid status directly — std::system's 128+signal encoding
// is indistinguishable from orderly rejection (tests/test_expectations.cpp).
//
// Isolation: each test gets its own temp dir (pid + test-stem + atomic
// counter, cf. tests/Corpus.cpp uniqueTempPath — the measured fix for
// `ctest -j8` temp collisions); the dir is always removed afterwards, even on
// failure — diagnostics go to TAP `#` lines, so a rerun reproduces the
// failure and /tmp never fills under -j8.
// Timeout: enforced by a waitpid deadline in runWithCapture, not by an
// external `timeout(1)` — so no 124/139 exit-code guessing: a kill reports
// `timeout after Ns` and fails the test (exit 1), and a child killed by a
// signal reports the true WTERMSIG. The duration policy (`--timeout Ns`,
// default 10s) is FT-3's; this file only classifies what the wait observes.
// Compile stdout: ADR 0009 reserves finc stdout, so stdout noise on a
// zero-exit compile fails the test (exit 1), not the runner — exit 3 is
// fintest's own breakage (missing finc, no tmpdir), finc misbehavior fails
// the test it was invoked for.
//
// Exits: 0 all pass (N>=1), 1 test failure, 2 caller error, 3 tool broken.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
// windows.h defines min/max macros that break std::min below; NOMINMAX
// suppresses them (cf. src/utils/Process.cpp, which includes windows.h).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>  // _getpid for the temp-file prefix below
#define FINTEST_GETPID _getpid
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define FINTEST_GETPID getpid
#endif

#ifndef FINC_BINARY
#error "FINC_BINARY must be defined by the build (tools/fintest/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

namespace {

constexpr int kDefaultRunTimeoutSeconds = 10;

// Cap on dumped output lines per failed test: the header names the class,
// the first lines carry the evidence, the rest is a count, not a flood.
constexpr size_t kMaxDetailLines = 10;

#ifdef _WIN32
// Microsoft CRT command-line decoding: backslashes before a quote and before
// the closing quote double, each literal quote is backslash-escaped, and the
// whole argument is wrapped in double quotes (adapted from the quote helper in
// src/utils/Process.cpp — fintest links nothing, so the helper lives here
// rather than dragging fin_core into the runner). The old helper wrapped in
// POSIX single quotes, which cmd.exe does not recognise as quoting.
std::string argvQuote(const std::string& arg) {
    std::string out = "\"";
    size_t slashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            ++slashes;
            continue;
        }
        out.append(slashes * (c == '"' ? 2 : 1), '\\');
        slashes = 0;
        if (c == '"') out += '\\';
        out += c;
    }
    out.append(slashes * 2, '\\');
    return out + '"';
}
#endif

std::string readWholeFile(const std::string& path) {
    std::ifstream t(path, std::ios::binary);
    if (!t.is_open()) return "";
    std::ostringstream buffer;
    buffer << t.rdbuf();
    return buffer.str();
}

// Port of tests/Corpus.cpp stripAnsi: with --color=never there should be no
// escapes, but the stdout-empty check reads the stripped text so a stray
// escape cannot hide noise as empty.
std::string stripAnsi(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && s[i] != 'm') ++i;
            continue;
        }
        out += s[i];
    }
    return out;
}

// Folded into the temp prefix so a file leaked by a crash says which test
// leaked it (cf. tests/Corpus.cpp); uniqueness still comes from pid+counter.
std::string sanitizeTag(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (std::isalnum(static_cast<unsigned char>(c)) == 0) c = '_';
    if (out.empty()) out = "notest";
    if (out.size() > 32) out.resize(32);
    return out;
}

// pid + atomic counter: safe under parallel runners (cf. tests/Corpus.cpp);
// pass the sanitised test stem inside the prefix for leak attribution.
std::string uniqueTempPath(const std::string& prefix, const std::string& suffix = "") {
    static std::atomic<int> counter{0};
    const std::string name = prefix + "_" + std::to_string(FINTEST_GETPID()) + "_" +
                             std::to_string(counter.fetch_add(1)) + suffix;
    return (fs::temp_directory_path() / name).string();
}

// Separators the child spawn accepts. CMake bakes FINC_BINARY with forward
// slashes and ctest passes test paths the same way (`D:/a/Fin/Fin/...`), and
// a quoted forward-slash image path fails the Windows launch with `The
// filename, directory name, or volume label syntax is incorrect.` Every
// spawned path goes through this before argv construction. On POSIX
// make_preferred is identity, so Linux TAP is byte-identical by construction.
std::string nativePath(const std::string& p) {
    return fs::path(p).make_preferred().string();
}

// TAP diagnostics: every line a `#` comment on stdout. Bounded: the first
// kMaxDetailLines lines verbatim, then one count line — the single TAP
// format for every outcome class, not a second (e.g. summary) format.
void emitHashLines(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    if (lines.empty()) {
        std::cout << "# (no output)\n";
        return;
    }
    const size_t shown = std::min(lines.size(), kMaxDetailLines);
    for (size_t i = 0; i < shown; ++i) std::cout << "# " << lines[i] << "\n";
    if (lines.size() > shown)
        std::cout << "# ... (truncated after " << shown << " of " << lines.size()
                  << " lines)\n";
}

// First output line naming a failed blame (`path:line: assertion failed[: msg]`,
// cf. Soundness_Codegen/AFailedBlameNamesAFileAndALine). Its presence is what
// separates a run-fail from a crash: a blame abort dies by signal too.
std::string firstAssertionLine(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("assertion failed") != std::string::npos) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }
    }
    return "";
}

struct Proc {
    bool launched = false;  // false: fork/exec itself failed (tool broken)
    // Why not, when !launched: the Win32 code or errno at the failing call.
    // A bare `launched=false` once hid every Windows spawn failure behind
    // `compile failed (exit 1)` with an empty body; the call sites print this.
    std::string launchError;
    bool exited = false;    // WIFEXITED — exitCode valid
    int exitCode = -1;
    bool signaled = false;  // WIFSIGNALED — termSig valid (WTERMSIG)
    int termSig = 0;
    bool timedOut = false;  // killed past the deadline (run only)
    std::string out;
    std::string err;
};

#ifndef _WIN32

// Direct fork/exec/waitpid with stdout/stderr captured to temp files.
// timeoutSecs > 0 arms a CLOCK_MONOTONIC deadline: past it the child's
// process group is SIGKILLed and timedOut is set. 0 means wait unbounded
// (compile step — no deadline policy there). Status is decoded with
// WIFEXITED/WIFSIGNALED directly, never via a shell's 128+signal code.
Proc runWithCapture(const std::vector<std::string>& argv, int timeoutSecs) {
    Proc p;
    const std::string outPath = uniqueTempPath("fintest_out");
    const std::string errPath = uniqueTempPath("fintest_err");

    std::vector<char*> cargs;
    cargs.reserve(argv.size() + 1);
    for (const auto& a : argv) cargs.push_back(const_cast<char*>(a.c_str()));
    cargs.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        p.launchError = "cannot fork: errno " + std::to_string(errno);
        return p;
    }
    if (pid == 0) {
        setpgid(0, 0);
        const int ofd =
            open(outPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        const int efd =
            open(errPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (ofd < 0 || efd < 0) _exit(127);
        dup2(ofd, STDOUT_FILENO);
        dup2(efd, STDERR_FILENO);
        if (ofd != STDOUT_FILENO) close(ofd);
        if (efd != STDERR_FILENO) close(efd);
        execvp(cargs[0], cargs.data());
        _exit(127);
    }
    p.launched = true;

    struct timespec deadline{0, 0};
    if (timeoutSecs > 0) {
        struct timespec now{0, 0};
        clock_gettime(CLOCK_MONOTONIC, &now);
        deadline.tv_sec = now.tv_sec + timeoutSecs;
        deadline.tv_nsec = now.tv_nsec;
    }
    bool killed = false;
    int status = 0;
    for (;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) break;
        struct timespec now{0, 0};
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (timeoutSecs > 0 &&
            (now.tv_sec > deadline.tv_sec ||
             (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec))) {
            if (!killed) {
                kill(-pid, SIGKILL);  // own process group from setpgid above
                killed = true;
                p.timedOut = true;
            }
            r = waitpid(pid, &status, 0);  // reap the kill
            if (r == pid) break;
        }
        struct timespec nap{0, 10 * 1000 * 1000};
        nanosleep(&nap, nullptr);
    }
    if (WIFEXITED(status)) {
        p.exited = true;
        p.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        p.signaled = true;
        p.termSig = WTERMSIG(status);
    }
    p.out = readWholeFile(outPath);
    p.err = readWholeFile(errPath);
    std::error_code ec;
    fs::remove(outPath, ec);
    fs::remove(errPath, ec);
    return p;
}

#else  // _WIN32: no fork/signal decoding; exit codes only, crash classes degrade.

// No shell: the old code built a POSIX single-quoted string and ran it through
// std::system, which on Windows goes through cmd.exe — single quotes quote
// nothing there, so every spawn failed with `The filename, directory name, or
// volume label syntax is incorrect.` before any Fin code ran (wave4-selfhost
// run 37566508425). Arguments now go to CreateProcess literally (cf.
// fin::runProcess on Windows), stdout/stderr to temp files through inherited
// handles instead of `>` redirection, and timeoutSecs arms a
// WaitForSingleObject deadline mirroring the POSIX waitpid deadline above —
// past it the child is terminated and timedOut is set. 0 means wait unbounded
// (compile step — no deadline policy there).
Proc runWithCapture(const std::vector<std::string>& argv, int timeoutSecs) {
    Proc p;
    if (argv.empty()) {
        p.launchError = "cannot launch: no argv";
        return p;
    }
    const std::string outPath = nativePath(uniqueTempPath("fintest_out"));
    const std::string errPath = nativePath(uniqueTempPath("fintest_err"));
    // CreateProcess may modify the command line, so it needs a mutable buffer.
    // Every element goes through nativePath: the finc image arrives with
    // forward slashes (FINC_BINARY) and so do the test and -o paths.
    std::string cmd;
    for (const auto& a : argv) {
        if (!cmd.empty()) cmd += ' ';
        cmd += argvQuote(nativePath(a));
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE hOut = CreateFileA(outPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD outCode = (hOut == INVALID_HANDLE_VALUE) ? GetLastError() : 0;
    HANDLE hErr = CreateFileA(errPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD errCode = (hErr == INVALID_HANDLE_VALUE) ? GetLastError() : 0;
    if (hOut == INVALID_HANDLE_VALUE || hErr == INVALID_HANDLE_VALUE) {
        const bool outBad = (hOut == INVALID_HANDLE_VALUE);
        p.launchError = "cannot create temp file " + (outBad ? outPath : errPath) +
                        ": Win32 " + std::to_string(outBad ? outCode : errCode);
        if (hOut != INVALID_HANDLE_VALUE) CloseHandle(hOut);
        if (hErr != INVALID_HANDLE_VALUE) CloseHandle(hErr);
        return p;
    }
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hOut;
    si.hStdError = hErr;
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                                        0, nullptr, nullptr, &si, &pi);
    CloseHandle(hOut);
    CloseHandle(hErr);
    if (!started) {
        p.launchError = "cannot launch " + nativePath(argv[0]) + ": Win32 " +
                        std::to_string(GetLastError());
        return p;
    }
    p.launched = true;
    if (timeoutSecs <= 0) {
        WaitForSingleObject(pi.hProcess, INFINITE);
    } else {
        const ULONGLONG deadline =
            GetTickCount64() + static_cast<ULONGLONG>(timeoutSecs) * 1000u;
        for (;;) {
            const DWORD r = WaitForSingleObject(pi.hProcess, 10);
            if (r != WAIT_TIMEOUT) break;
            if (GetTickCount64() >= deadline) {
                TerminateProcess(pi.hProcess, 1);
                p.timedOut = true;
                WaitForSingleObject(pi.hProcess, INFINITE);
                break;
            }
        }
    }
    DWORD status = 0;
    if (GetExitCodeProcess(pi.hProcess, &status)) {
        p.exited = true;
        p.exitCode = static_cast<int>(status);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    p.out = readWholeFile(outPath);
    p.err = readWholeFile(errPath);
    std::error_code ec;
    fs::remove(outPath, ec);
    fs::remove(errPath, ec);
    return p;
}

#endif

// The corpus under tests/ is owned by the gtest harness (ADR 0008); fintest
// never scans it, and a root or file inside it is a caller error, not a skip.
bool underTestsDir(const fs::path& p) {
    for (const auto& part : p.lexically_normal())
        if (part == "tests") return true;
    return false;
}

bool hasTestSuffix(const fs::path& p) {
    const std::string name = p.filename().string();
    constexpr const char* kSuffix = "_test.fin";
    const size_t n = 9;  // strlen("_test.fin")
    return name.size() > n && name.compare(name.size() - n, n, kSuffix) == 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> roots;
    std::string onlyFile;
    int runTimeoutSeconds = kDefaultRunTimeoutSeconds;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--timeout" || a.rfind("--timeout=", 0) == 0) {
            std::string val;
            if (a == "--timeout") {
                if (++i >= argc) {
                    std::cout << "TAP version 13\n1..0\n";
                    std::cout << "# fintest: caller error: --timeout needs seconds\n";
                    return 2;
                }
                val = argv[i];
            } else {
                val = a.substr(std::string("--timeout=").size());
            }
            char* end = nullptr;
            const long v = std::strtol(val.c_str(), &end, 10);
            if (end == val.c_str() || *end != '\0' || v <= 0 || v > 86400) {
                std::cout << "TAP version 13\n1..0\n";
                std::cout << "# fintest: caller error: --timeout needs a positive "
                             "integer (seconds): "
                          << val << "\n";
                return 2;
            }
            runTimeoutSeconds = static_cast<int>(v);
        } else if (a == "--file") {
            if (++i >= argc || !onlyFile.empty()) {
                std::cout << "TAP version 13\n1..0\n";
                std::cout << "# fintest: caller error: --file takes exactly one path\n";
                return 2;
            }
            onlyFile = argv[i];
        } else {
            roots.push_back(a);
        }
    }
    if (!onlyFile.empty() && !roots.empty()) {
        std::cout << "TAP version 13\n1..0\n";
        std::cout << "# fintest: caller error: --file takes no roots\n";
        return 2;
    }

    std::cout << "TAP version 13\n";

    std::vector<std::string> files;
    if (!onlyFile.empty()) {
        std::error_code ec;
        if (!fs::is_regular_file(onlyFile, ec) || underTestsDir(fs::path(onlyFile))) {
            std::cout << "1..0\n";
            std::cout << "# fintest: caller error: bad --file path: " << onlyFile << "\n";
            return 2;
        }
        files.push_back(fs::path(onlyFile).generic_string());
    } else {
    if (roots.empty()) roots.push_back("./fintest/");
    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::exists(root, ec)) {
            std::cout << "1..0\n";
            std::cout << "# fintest: caller error: root not found: " << root << "\n";
            return 2;
        }
        if (underTestsDir(fs::path(root))) {
            std::cout << "1..0\n";
            std::cout << "# fintest: caller error: refusing to scan tests/: " << root
                      << "\n";
            return 2;
        }
        fs::recursive_directory_iterator it(root, ec);
        if (ec) {
            std::cout << "1..0\n";
            std::cout << "# fintest: caller error: cannot scan root: " << root << "\n";
            return 2;
        }
        const fs::recursive_directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec) || ec) continue;
            const fs::path p = it->path();
            if (p.extension() != ".fin") continue;
            if (!hasTestSuffix(p)) continue;
            if (underTestsDir(p)) {
                std::cout << "1..0\n";
                std::cout << "# fintest: caller error: refusing to scan tests/: "
                          << p.generic_string() << "\n";
                return 2;
            }
            files.push_back(p.generic_string());
        }
    }
    }  // else (root scan)
    std::sort(files.begin(), files.end());

    if (files.empty()) {
        std::cout << "1..0\n";
        std::cout << "# fintest: caller error: zero *_test.fin files discovered\n";
        return 2;
    }

    const std::string finc = FINC_BINARY;
    {
        std::error_code ec;
        if (!fs::exists(finc, ec)) {
            std::cout << "1..0\n";
            std::cout << "# fintest: tool broken: finc not found at " << finc << "\n";
            return 3;
        }
    }

    std::cout << "1.." << files.size() << "\n";
    int failures = 0;
    int n = 0;
    for (const auto& f : files) {
        ++n;
        const std::string dir =
            uniqueTempPath("fintest_" + sanitizeTag(fs::path(f).stem().string()));
        std::error_code ec;
        if (!fs::create_directories(dir, ec)) {
            std::cout << "not ok " << n << " " << f << "\n";
            std::cout << "# fintest: tool broken: cannot create temp dir\n";
            return 3;
        }
        const std::string exe = (fs::path(dir) / "test").string();
        const Proc c = runWithCapture(
            {finc, "--color=never", f, "-o", exe}, 0 /* no deadline */);
        bool pass = false;
        std::string header;
        std::string body;
        if (!c.launched) {
            std::cout << "not ok " << n << " " << f << "\n";
            std::cout << "# fintest: tool broken: "
                      << (c.launchError.empty() ? "cannot launch finc" : c.launchError)
                      << "\n";
            return 3;
        } else if (c.signaled) {
            // A finc crash is a test failure, never runner-internal: exit 1
            // overall, counted below like any other failing test.
            header = "compiler crashed (signal " + std::to_string(c.termSig) + ")";
            body = c.err + c.out;
        } else if (c.exitCode != 0) {
            header = "compile failed (exit " + std::to_string(c.exitCode) + ")";
            body = c.err + c.out;
        } else if (!stripAnsi(c.out).empty()) {
            // Class is fail, not tool-broken: exit 3 is fintest's own breakage; finc misbehavior fails the test it was invoked for.
            header = "compile produced stdout (ADR 0009: stdout reserved)";
            body = c.err + c.out;
        } else {
            std::error_code exeEc;
            if (!fs::exists(exe, exeEc) || exeEc) {
                header = "compile exit 0 but no executable";
                body = c.err + c.out;
            } else {
                const Proc r = runWithCapture({exe}, runTimeoutSeconds);
                if (!r.launched) {
                    std::cout << "not ok " << n << " " << f << "\n";
                    std::cout << "# fintest: tool broken: "
                              << (r.launchError.empty() ? "cannot launch test"
                                                        : r.launchError)
                              << "\n";
                    return 3;
                } else if (r.timedOut) {
                    header = "timeout after " +
                             std::to_string(runTimeoutSeconds) + "s";
                    body = r.out + r.err;
                } else if (r.exited && r.exitCode == 0) {
                    pass = true;
                } else {
                    const std::string blame =
                        firstAssertionLine(r.err + "\n" + r.out);
                    if (!blame.empty()) {
                        header = r.signaled
                                     ? "run failed (signal " +
                                           std::to_string(r.termSig) + ")"
                                     : "run failed (exit " +
                                           std::to_string(r.exitCode) + ")";
                        body = blame;
                    } else if (r.signaled) {
                        header =
                            "crashed (signal " + std::to_string(r.termSig) + ")";
                        body = r.out + r.err;
                    } else {
                        header =
                            "run failed (exit " + std::to_string(r.exitCode) + ")";
                        body = r.out + r.err;
                    }
                }
            }
        }
        if (pass) {
            std::cout << "ok " << n << " " << f << "\n";
        } else {
            ++failures;
            std::cout << "not ok " << n << " " << f << "\n";
            std::cout << "# " << header << "\n";
            if (!body.empty()) emitHashLines(body);
        }
        // Always removed, even on failure: diagnostics already went to TAP `#`
        // lines above, so a rerun reproduces the failure without /tmp litter.
        fs::remove_all(dir, ec);
    }
    return failures == 0 ? 0 : 1;
}
