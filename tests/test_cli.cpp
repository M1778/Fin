#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "Corpus.hpp"
#include "driver/SearchPaths.hpp"
#include "driver/Version.hpp"

#if defined(__unix__) || defined(__APPLE__)
#  include <cerrno>
#  include <cstdlib>
#  include <fcntl.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  define FIN_HAVE_PTY 1
#endif

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

const char* kUnset = "\x01unset";

// A temporary .fin file, so a contract test never depends on a sample.
class TempFin {
public:
    explicit TempFin(const std::string& contents, const std::string& tag = "t") {
        path_ = uniqueTempPath("fin_cli_" + tag, ".fin");
        std::ofstream f(path_, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    ~TempFin() { std::error_code ec; fs::remove(path_, ec); }
    std::string str() const { return path_.string(); }

private:
    fs::path path_;
};

#ifdef FIN_HAVE_PTY
struct PtyRun {
    int exitCode = -1;   // -1 also means "no pty could be allocated"
    std::string err;
};

// Runs finc with stderr attached to a pseudo-terminal. Without this the only
// stderr the suite ever sees is a redirected file, so `isatty` is always false
// and the NO_COLOR branch is never actually taken.
PtyRun runFincOnPtyStderr(const std::vector<std::string>& args,
                          const std::vector<std::pair<std::string, std::string>>& env) {
    PtyRun r;
    int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) return r;
    if (::grantpt(master) != 0 || ::unlockpt(master) != 0) { ::close(master); return r; }
    const char* name = ::ptsname(master);
    if (name == nullptr) { ::close(master); return r; }
    const std::string slavePath = name;

    const std::string bin = fincBinary();
    pid_t pid = ::fork();
    if (pid < 0) { ::close(master); return r; }
    if (pid == 0) {
        ::close(master);
        int slave = ::open(slavePath.c_str(), O_RDWR);
        if (slave < 0) _exit(127);
        ::dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO) ::close(slave);
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) { ::dup2(devnull, STDOUT_FILENO); ::close(devnull); }
        for (const auto& kv : env) {
            if (kv.second == std::string(kUnset)) ::unsetenv(kv.first.c_str());
            else ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
        }
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(bin.c_str()));
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        ::execv(bin.c_str(), argv.data());
        _exit(127);
    }

    char buf[4096];
    for (;;) {
        ssize_t n = ::read(master, buf, sizeof buf);
        if (n > 0) { r.err.append(buf, (size_t)n); continue; }
        if (n < 0 && errno == EINTR) continue;
        break;   // 0 is EOF; -1/EIO is the slave side closing
    }
    ::close(master);
    int status = 0;
    ::waitpid(pid, &status, 0);
    r.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}
#endif

// The reproducer from docs/baseline.md: an unlexable byte between two
// declarations. The parser never misses a token, so the parse succeeds; the
// lexer's catch-all is the only thing that saw the byte.
const char* kLexerErrorBetweenDecls =
    "fun a() <noret> {}\n"
    "\302\247\n"
    "fun main() <noret> {}\n";

} // namespace

// --- Exit codes -------------------------------------------------------------

TEST(MachineContract, ExitZeroOnASampleThatCompiles) {
    TempFin f("fun main() <noret> {}\n");
    auto r = runFinc({f.str()});
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(MachineContract, ExitOneOnADiagnostic) {
    TempFin f("fun main() <noret> { let ; }\n");
    auto r = runFinc({f.str()});
    EXPECT_EQ(r.exitCode, 1) << stripAnsi(r.err);
}

TEST(MachineContract, ExitTwoOnAMissingInputFile) {
    auto r = runFinc({"/nonexistent/definitely-not-here.fin"});
    EXPECT_EQ(r.exitCode, 2);
    EXPECT_NE(stripAnsi(r.err).find("could not read file"), std::string::npos);
}

TEST(MachineContract, ExitTwoOnNoInputFile) {
    auto r = runFinc({"--debug-ast"});
    EXPECT_EQ(r.exitCode, 2);
}

TEST(MachineContract, ExitTwoOnNoArgumentsAtAll) {
    auto r = runFinc({});
    EXPECT_EQ(r.exitCode, 2);
    EXPECT_EQ(r.out, "") << "usage on the failure path belongs on stderr";
}

// --- Strict argv ------------------------------------------------------------

TEST(MachineContract, UnknownFlagIsAnError) {
    TempFin f("fun main() <noret> {}\n");
    auto r = runFinc({f.str(), "--bogus"});
    EXPECT_EQ(r.exitCode, 2) << stripAnsi(r.err);
    EXPECT_NE(stripAnsi(r.err).find("unknown option"), std::string::npos);
}

TEST(MachineContract, ASecondPositionalIsAnError) {
    TempFin a("fun main() <noret> {}\n", "a");
    TempFin b("fun other() <noret> {}\n", "b");
    auto r = runFinc({a.str(), b.str()});
    EXPECT_EQ(r.exitCode, 2) << stripAnsi(r.err);
    EXPECT_NE(stripAnsi(r.err).find("second input file"), std::string::npos);
}

TEST(MachineContract, TheBaselineArgvReproducerNoLongerCompilesTheWrongFile) {
    // `finc basic.fin --bogus -o prog` used to discard --bogus and compile
    // `prog`, because main.cpp:39 had no else branch.
    TempFin f("fun main() <noret> {}\n");
    auto r = runFinc({f.str(), "--bogus", "-o", "prog"});
    EXPECT_EQ(r.exitCode, 2);
    EXPECT_EQ(stripAnsi(r.err).find("prog"), std::string::npos)
        << "the operand of -o must not become the input file";
}

TEST(MachineContract, DashOProducesTheNamedExecutable) {
    // This test used to be DashOIsAcceptedAndIgnored, and asserted the opposite:
    // `-o` was stored and never honoured because runCodeGen returned true without
    // emitting anything. Wave 5's first artifact is what changed it. What the CLI
    // owes is narrow and is all that is checked here -- the file appears, at the
    // path asked for, and the exit code still means what ADR 0009 says. Whether
    // the program *runs* is Soundness_Codegen's, in tests/test_codegen.cpp.
    TempFin f("fun main() <noret> {}\n");
    const std::string target = uniqueTempPath("fin_o_flag_target");
    auto r = runFinc({f.str(), "-o", target});
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
    EXPECT_TRUE(fs::exists(target)) << "-o must name the artifact it produces";
    std::error_code ec;
    fs::remove(target, ec);
}

TEST(MachineContract, DashOWithoutAnOperandIsAUsageError) {
    TempFin f("fun main() <noret> {}\n");
    auto r = runFinc({f.str(), "-o"});
    EXPECT_EQ(r.exitCode, 2);
}

TEST(MachineContract, IncludeWithoutAnOperandIsAUsageError) {
    TempFin f("fun main() <noret> {}\n");
    auto r = runFinc({f.str(), "-I"});
    EXPECT_EQ(r.exitCode, 2);
}

TEST(MachineContract, AnUnknownDiagnosticsFormatIsAUsageError) {
    TempFin f("fun main() <noret> {}\n");
    auto r = runFinc({f.str(), "--diagnostics=yaml"});
    EXPECT_EQ(r.exitCode, 2);
}

TEST(MachineContract, HelpGoesToStdoutAndExitsZero) {
    auto r = runFinc({"--help"});
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_NE(r.out.find("Usage: finc"), std::string::npos);
    EXPECT_EQ(r.err, "");
}

// --- --version --------------------------------------------------------------

TEST(MachineContract, VersionIsParseable) {
    auto r = runFinc({"--version"});
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_EQ(r.err, "");

    std::smatch m;
    const std::string out = r.out;
    static const std::regex re(R"(^finc (\d+)\.(\d+)\.(\d+) \(contract (\d+)\)\n$)");
    ASSERT_TRUE(std::regex_match(out, m, re))
        << "`finc --version` must print exactly `finc <semver> (contract <int>)`, got: " << out;
    EXPECT_EQ(m[0].str(),
              std::string("finc ") + fin::kFincVersion + " (contract " +
              std::to_string(fin::kFincContractVersion) + ")\n");
}

// --- Streams ----------------------------------------------------------------

TEST(MachineContract, DiagnosticsGoToStderrAndStdoutStaysEmpty) {
    TempFin f("fun main() <noret> { let ; }\n");
    auto r = runFinc({f.str()});
    EXPECT_EQ(r.out, "") << "stdout is reserved";
    EXPECT_NE(stripAnsi(r.err).find("error:"), std::string::npos);
}

TEST(MachineContract, SuccessChatterDoesNotTouchStdout) {
    TempFin f("fun main() <noret> {}\n");
    auto r = runFinc({f.str()});
    EXPECT_EQ(r.out, "");
    EXPECT_NE(stripAnsi(r.err).find("Build Successful."), std::string::npos);
}

// --- The `Build Successful.` reproducer ------------------------------------

TEST(MachineContract, ALexerErrorBetweenDeclarationsFailsTheBuild) {
    TempFin f(kLexerErrorBetweenDecls);
    auto r = runFinc({f.str()});
    const std::string err = stripAnsi(r.err);

    EXPECT_NE(r.exitCode, 0)
        << "an unlexable byte between two declarations must not exit 0.\nstderr:\n" << err;
    EXPECT_EQ(r.exitCode, 1) << err;
    EXPECT_EQ(err.find("Build Successful."), std::string::npos)
        << "`Build Successful.` printed with errors on screen:\n" << err;
    EXPECT_NE(err.find("error:"), std::string::npos) << err;
    EXPECT_NE(err.find("unrecognised byte"), std::string::npos)
        << "the lexer's catch-all must reach the DiagnosticEngine:\n" << err;
}

TEST(MachineContract, ALexerErrorIsCountedNotJustPrinted) {
    TempFin f(kLexerErrorBetweenDecls);
    auto r = runFinc({f.str(), "--diagnostics=json"});
    EXPECT_NE(r.err.find("\"kind\":\"diagnostic\""), std::string::npos) << r.err;
    EXPECT_EQ(r.err.find("\"errors\":0"), std::string::npos)
        << "the lexer error must be counted in the summary:\n" << r.err;
}

// --- Empty versus missing ---------------------------------------------------

TEST(MachineContract, AnEmptyFileCompiles) {
    TempFin f("");
    auto r = runFinc({f.str()});
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(MachineContract, AnEmptyFileIsDistinguishedFromAMissingOne) {
    TempFin empty("");
    auto a = runFinc({empty.str()});
    auto b = runFinc({"/nonexistent/definitely-not-here.fin"});
    EXPECT_EQ(a.exitCode, 0);
    EXPECT_EQ(b.exitCode, 2);
}

TEST(MachineContract, AFileOfOnlyACommentCompiles) {
    TempFin f("// nothing here\n");
    auto r = runFinc({f.str()});
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(MachineContract, ADirectoryIsNotReadableAsSource) {
    auto r = runFinc({samplesDir()});
    EXPECT_EQ(r.exitCode, 2);
}

// --- Colour -----------------------------------------------------------------

TEST(MachineContract, NoAnsiWhenStderrIsNotATty) {
    // runFinc redirects stderr to a file, so this is the non-tty path.
    TempFin f("fun main() <noret> { let ; }\n");
    auto r = runFinc({f.str()}, {{"NO_COLOR", kUnset}});
    EXPECT_EQ(r.err.find('\x1b'), std::string::npos)
        << "captured output must carry no ANSI escapes:\n" << r.err;
}

// Precedence is --color=<mode>, then NO_COLOR, then isatty. An explicit flag on
// the command line is the most local instruction there is, and a machine
// consumer never passes one.
TEST(MachineContract, ColorAlwaysOverridesNoColor) {
    TempFin f("fun main() <noret> { let ; }\n");
    auto r = runFinc({f.str(), "--color=always"}, {{"NO_COLOR", "1"}});
    EXPECT_NE(r.err.find('\x1b'), std::string::npos)
        << "an explicit --color=always must beat NO_COLOR:\n" << r.err;
}

#ifdef FIN_HAVE_PTY
TEST(MachineContract, AnsiOnATtyByDefault) {
    TempFin f("fun main() <noret> { let ; }\n");
    auto r = runFincOnPtyStderr({f.str()}, {{"NO_COLOR", kUnset}});
    ASSERT_NE(r.exitCode, -1) << "no pty available; the colour default is untested";
    EXPECT_EQ(r.exitCode, 1);
    EXPECT_NE(r.err.find('\x1b'), std::string::npos)
        << "a terminal should get colour:\n" << r.err;
}

TEST(MachineContract, NoColorSuppressesAnsiOnATty) {
    TempFin f("fun main() <noret> { let ; }\n");
    auto r = runFincOnPtyStderr({f.str()}, {{"NO_COLOR", "1"}});
    ASSERT_NE(r.exitCode, -1) << "no pty available; NO_COLOR is untested";
    EXPECT_NE(r.err.find("error:"), std::string::npos) << "expected a diagnostic";
    EXPECT_EQ(r.err.find('\x1b'), std::string::npos)
        << "NO_COLOR must suppress colour even on a terminal:\n" << r.err;
}

TEST(MachineContract, NoColorWithAnEmptyValueStillSuppressesAnsiOnATty) {
    // no-color.org: the variable's presence is the signal, not its value.
    TempFin f("fun main() <noret> { let ; }\n");
    auto r = runFincOnPtyStderr({f.str()}, {{"NO_COLOR", ""}});
    ASSERT_NE(r.exitCode, -1) << "no pty available; NO_COLOR is untested";
    EXPECT_EQ(r.err.find('\x1b'), std::string::npos) << r.err;
}

TEST(MachineContract, ColorNeverSuppressesAnsiOnATty) {
    TempFin f("fun main() <noret> { let ; }\n");
    auto r = runFincOnPtyStderr({f.str(), "--color=never"}, {{"NO_COLOR", kUnset}});
    ASSERT_NE(r.exitCode, -1) << "no pty available; --color=never is untested";
    EXPECT_EQ(r.err.find('\x1b'), std::string::npos) << r.err;
}

TEST(MachineContract, JsonModeEmitsNoAnsiEvenOnATty) {
    // The consumer's parser must not have to strip escapes.
    TempFin f("fun main() <noret> { let ; }\n");
    auto r = runFincOnPtyStderr({f.str(), "--diagnostics=json"}, {{"NO_COLOR", kUnset}});
    ASSERT_NE(r.exitCode, -1) << "no pty available; JSON-on-a-tty is untested";
    EXPECT_NE(r.err.find("\"kind\":\"diagnostic\""), std::string::npos) << r.err;
    EXPECT_EQ(r.err.find('\x1b'), std::string::npos)
        << "JSON mode must never emit ANSI:\n" << r.err;
}
#endif

TEST(MachineContract, ColorAlwaysDoesEmitAnsi) {
    TempFin f("fun main() <noret> { let ; }\n");
    auto r = runFinc({f.str(), "--color=always"}, {{"NO_COLOR", kUnset}});
    EXPECT_NE(r.err.find('\x1b'), std::string::npos)
        << "--color=always must override the non-tty default";
}

// --- --diagnostics=json -----------------------------------------------------

namespace {

std::vector<std::string> jsonLines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) nl = s.size();
        std::string line = s.substr(start, nl - start);
        if (!line.empty()) out.push_back(line);
        start = nl + 1;
    }
    return out;
}

} // namespace

TEST(JsonDiagnostics, EveryLineIsAJsonObjectAndTheLastIsTheSummary) {
    TempFin f("fun main() <noret> { let ; }\n");
    auto r = runFinc({f.str(), "--diagnostics=json"});
    ASSERT_EQ(r.out, "") << "stdout is reserved even in JSON mode";

    auto lines = jsonLines(r.err);
    ASSERT_GE(lines.size(), 2u) << r.err;
    for (const auto& l : lines) {
        EXPECT_EQ(l.front(), '{') << l;
        EXPECT_EQ(l.back(), '}') << l;
        EXPECT_NE(l.find("\"kind\":"), std::string::npos) << l;
    }
    EXPECT_NE(lines.front().find("\"kind\":\"diagnostic\""), std::string::npos) << lines.front();
    EXPECT_NE(lines.back().find("\"kind\":\"summary\""), std::string::npos) << lines.back();
}

TEST(JsonDiagnostics, NoNonJsonByteReachesStderr) {
    TempFin f("fun main() <noret> { let ; }\n");
    auto r = runFinc({f.str(), "--diagnostics=json", "--debug-ast", "--debug-sema"});
    for (const auto& l : jsonLines(r.err)) {
        EXPECT_EQ(l.front(), '{') << "progress chatter leaked into the JSON stream: " << l;
    }
    EXPECT_EQ(r.err.find('\x1b'), std::string::npos) << "ANSI escapes in the JSON stream";
}

TEST(JsonDiagnostics, ASuccessfulRunStillEmitsASummary) {
    TempFin f("fun main() <noret> {}\n");
    auto r = runFinc({f.str(), "--diagnostics=json"});
    auto lines = jsonLines(r.err);
    ASSERT_EQ(lines.size(), 1u) << r.err;
    EXPECT_NE(lines[0].find("\"kind\":\"summary\""), std::string::npos) << lines[0];
    EXPECT_NE(lines[0].find("\"errors\":0"), std::string::npos) << lines[0];
    EXPECT_NE(lines[0].find("\"exitCode\":0"), std::string::npos) << lines[0];
    EXPECT_NE(lines[0].find("\"status\":\"ok\""), std::string::npos) << lines[0];
}

TEST(JsonDiagnostics, TheSummaryCarriesTheExitCodeOnAUsageFailure) {
    auto r = runFinc({"/nonexistent/definitely-not-here.fin", "--diagnostics=json"});
    auto lines = jsonLines(r.err);
    ASSERT_GE(lines.size(), 1u) << r.err;
    EXPECT_NE(lines.back().find("\"exitCode\":2"), std::string::npos) << lines.back();
    EXPECT_NE(lines.back().find("\"status\":\"failed\""), std::string::npos) << lines.back();
}

TEST(JsonDiagnostics, ADiagnosticCarriesTheReservedAttributionKey) {
    // Wave 4 lets a library inject code at an event point, so a diagnostic can
    // name a source location the user never wrote. The key is reserved now
    // because `finn` ships against this schema; it is null for every diagnostic
    // the compiler raises on its own, which today is all of them.
    TempFin f("fun main() <noret> { let ; }\n");
    auto r = runFinc({f.str(), "--diagnostics=json"});
    auto lines = jsonLines(r.err);
    ASSERT_GE(lines.size(), 2u) << r.err;
    EXPECT_NE(lines.front().find("\"attribution\":null"), std::string::npos)
        << "the attribution key must be present and null:\n" << lines.front();
}

TEST(JsonDiagnostics, ADiagnosticCarriesCodeAsNullAndAPosition) {
    TempFin f("fun main() <noret> { let ; }\n");
    auto r = runFinc({f.str(), "--diagnostics=json"});
    auto lines = jsonLines(r.err);
    ASSERT_GE(lines.size(), 2u) << r.err;
    const std::string& d = lines.front();
    EXPECT_NE(d.find("\"code\":null"), std::string::npos) << d;
    EXPECT_NE(d.find("\"severity\":\"error\""), std::string::npos) << d;
    EXPECT_NE(d.find("\"line\":1"), std::string::npos) << d;
    EXPECT_NE(d.find("\"column\":"), std::string::npos) << d;
    EXPECT_NE(d.find("\"file\":"), std::string::npos) << d;
}

TEST(JsonDiagnostics, AMessageContainingAQuoteIsEscaped) {
    TempFin f("fun main() <noret> { let ; }\n");
    auto r = runFinc({f.str(), "--diagnostics=json"});
    // A bare, unescaped double quote inside the message would break the line
    // into invalid JSON; check the object still closes with a brace.
    for (const auto& l : jsonLines(r.err)) {
        EXPECT_EQ(l.back(), '}') << l;
    }
}

// --- Library search paths ---------------------------------------------------
//
// `import "mylib";` gives the compiler no path, so the only way it can resolve
// is a search path (tests/samples/importing.fin:9). Every test here unsets
// FIN_LIBS explicitly: an inherited value would make the negative cases pass
// for the wrong reason.

namespace {

// A directory holding one importable module, plus a source that imports it.
class TempLib {
public:
    explicit TempLib(const std::string& dirName = "libs") {
        root_ = uniqueTempPath("fin_lib", "_" + dirName);
        std::error_code ec;
        fs::create_directories(root_, ec);
        std::ofstream(root_ / "mylib.fin", std::ios::binary) << "// an empty lib\n";
    }
    ~TempLib() { std::error_code ec; fs::remove_all(root_, ec); }
    std::string dir() const { return root_.string(); }

private:
    fs::path root_;
};

const char* kImportsMyLib = "import \"mylib\";\nfun main() <noret> {}\n";

std::vector<std::pair<std::string, std::string>> noFinLibs() {
    return {{"FIN_LIBS", kUnset}};
}

} // namespace

TEST(LibraryPaths, AModuleOffEverySearchPathIsNotFound) {
    TempLib lib;
    TempFin f(kImportsMyLib, "nolib");
    auto r = runFinc({f.str()}, noFinLibs());
    EXPECT_EQ(r.exitCode, 1) << stripAnsi(r.err);
    EXPECT_NE(stripAnsi(r.err).find("module not found"), std::string::npos);
}

TEST(LibraryPaths, TheEnvironmentVariableIsSearched) {
    TempLib lib;
    TempFin f(kImportsMyLib, "env");
    auto r = runFinc({f.str()}, {{"FIN_LIBS", lib.dir()}});
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(LibraryPaths, TheFlagIsSearched) {
    TempLib lib;
    TempFin f(kImportsMyLib, "flag");
    auto r = runFinc({f.str(), "--fin-libs", lib.dir()}, noFinLibs());
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(LibraryPaths, TheFlagIsAlsoAcceptedGlued) {
    TempLib lib;
    TempFin f(kImportsMyLib, "glued");
    auto r = runFinc({f.str(), "--fin-libs=" + lib.dir()}, noFinLibs());
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(LibraryPaths, AListIsSplitOnTheSeparator) {
    TempLib lib;
    TempFin f(kImportsMyLib, "split");
    const std::string list = std::string("/nonexistent/first") +
                             fin::kSearchPathSeparator + lib.dir();
    auto r = runFinc({f.str(), "--fin-libs=" + list}, noFinLibs());
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

// Not a regression test for the hardcoded `':'`: on a POSIX host the old code
// split on `':'`, which is what POSIX wants, so this passed before the fix too.
// It guards the opposite mistake — someone hardcoding `';'` — and the actual
// regression assertion is in SearchPaths.TheSeparatorIsThePlatforms below.
TEST(LibraryPaths, TheOtherPlatformsSeparatorIsJustACharacterInAPath) {
    TempLib lib("od;d");
    TempFin f(kImportsMyLib, "othersep");
    auto r = runFinc({f.str(), "--fin-libs=" + lib.dir()}, noFinLibs());
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(LibraryPaths, TheFlagIsRepeatable) {
    TempLib lib;
    TempFin f(kImportsMyLib, "repeat");
    auto r = runFinc({f.str(), "--fin-libs=/nonexistent/first",
                      "--fin-libs=" + lib.dir()}, noFinLibs());
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(LibraryPaths, TheFlagReplacesTheEnvironmentRatherThanExtendingIt) {
    TempLib lib;
    TempFin f(kImportsMyLib, "replace");
    auto r = runFinc({f.str(), "--fin-libs=/nonexistent/only"},
                     {{"FIN_LIBS", lib.dir()}});
    EXPECT_EQ(r.exitCode, 1) << stripAnsi(r.err);
}

TEST(LibraryPaths, AnEmptyFlagValueMeansNoLibraryPaths) {
    TempLib lib;
    TempFin f(kImportsMyLib, "emptyflag");
    auto r = runFinc({f.str(), "--fin-libs="}, {{"FIN_LIBS", lib.dir()}});
    EXPECT_EQ(r.exitCode, 1) << stripAnsi(r.err);
}

// An empty `FIN_LIBS` is not the same statement as an empty `--fin-libs=`.
//
// The flag is `finn` pinning what a build compiles against, so `--fin-libs=` means "pin
// it to nothing" and the bundled library is suppressed -- that is the test above, and it
// stays. But `FIN_LIBS=` in a shell is how POSIX spells *unset*: `export FIN_LIBS=` in a
// profile, a CI job that writes the variable before it has a value, `env FIN_LIBS= finc`
// in a script that means to clear it. Treating that as "pinned to nothing" took the
// standard library away from anyone who had ever cleared the variable, and then told them
// `no library search paths were given; pass --fin-libs or set FIN_LIBS` -- advice to set
// the thing they had set. `getenv` cannot distinguish empty from unset in any useful way,
// so the compiler has to decide, and an empty environment variable means unset here.
TEST(LibraryPaths, AnEmptyEnvironmentVariableIsUnsetAndDoesNotSuppressTheBundle) {
    TempFin f("import { Error } from error::std;\nfun main() <noret> {}\n", "emptyenv");

    // The control: with FIN_LIBS truly absent the bundled library resolves. If this
    // fails the bundle is missing and the rest of the test proves nothing.
    const auto unset = runFinc({f.str()}, noFinLibs());
    EXPECT_EQ(unset.exitCode, 0) << "the bundled library has to resolve for this test to "
                                    "mean anything\n" << stripAnsi(unset.err);

    const auto empty = runFinc({f.str()}, {{"FIN_LIBS", ""}});
    EXPECT_EQ(empty.exitCode, 0)
        << "an empty FIN_LIBS is unset, not \"pinned to no libraries\"\n"
        << stripAnsi(empty.err);
    EXPECT_EQ(empty.err.find("module not found"), std::string::npos) << stripAnsi(empty.err);

    // And the flag still means what it means, with an empty environment underneath it:
    // the two spellings have to stay distinguishable.
    const auto flag = runFinc({f.str(), "--fin-libs="}, {{"FIN_LIBS", ""}});
    EXPECT_EQ(flag.exitCode, 1)
        << "--fin-libs= is an explicit pin to nothing and must still suppress the bundle\n"
        << stripAnsi(flag.err);
}

TEST(LibraryPaths, AnEmptyEntryIsDroppedAndDoesNotBecomeTheWorkingDirectory) {
    TempFin f(kImportsMyLib, "emptyentry");
    const std::string justSeparators(3, fin::kSearchPathSeparator);
    auto r = runFinc({f.str(), "--fin-libs=" + justSeparators}, noFinLibs());
    EXPECT_EQ(r.exitCode, 1) << stripAnsi(r.err);
}

TEST(LibraryPaths, TheFlagWithoutAnOperandIsAUsageError) {
    auto r = runFinc({"--fin-libs"}, noFinLibs());
    EXPECT_EQ(r.exitCode, 2);
    EXPECT_NE(stripAnsi(r.err).find("--fin-libs"), std::string::npos);
}

TEST(LibraryPaths, TheFlagIsAdvertisedInHelp) {
    auto r = runFinc({"--help"}, noFinLibs());
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_NE(r.out.find("--fin-libs"), std::string::npos);
}

// --- The separator itself ---------------------------------------------------
//
// `FIN_LIBS` was split on a hardcoded `':'`, which on Windows turns `C:\libs`
// into a relative `C` and a rootless `\libs`. That defect is invisible from a
// POSIX host, where a colon really is the separator, so a test that runs finc
// cannot catch it here. These call the splitter directly: the Windows branch is
// the assertion the bug fails, and it executes on the Windows runners in CI.

TEST(SearchPaths, TheSeparatorIsThePlatforms) {
    using V = std::vector<std::string>;
#if defined(_WIN32)
    EXPECT_EQ(fin::splitSearchPaths("C:\\a;C:\\b"), (V{"C:\\a", "C:\\b"}));
    EXPECT_EQ(fin::splitSearchPaths("C:\\libs"), (V{"C:\\libs"}));
#else
    EXPECT_EQ(fin::splitSearchPaths("/a:/b"), (V{"/a", "/b"}));
    EXPECT_EQ(fin::splitSearchPaths("/a;b"), (V{"/a;b"}));
#endif
}

TEST(SearchPaths, EmptyEntriesAreDropped) {
    using V = std::vector<std::string>;
    const char sep = fin::kSearchPathSeparator;
    const std::string a = "/a", b = "/b";
    EXPECT_EQ(fin::splitSearchPaths(""), V{});
    EXPECT_EQ(fin::splitSearchPaths(std::string(1, sep)), V{});
    EXPECT_EQ(fin::splitSearchPaths(std::string(4, sep)), V{});
    EXPECT_EQ(fin::splitSearchPaths(sep + a), (V{a}));
    EXPECT_EQ(fin::splitSearchPaths(a + sep), (V{a}));
    EXPECT_EQ(fin::splitSearchPaths(a + sep + sep + b), (V{a, b}));
}

TEST(SearchPaths, OrderIsPreserved) {
    using V = std::vector<std::string>;
    const char sep = fin::kSearchPathSeparator;
    EXPECT_EQ(fin::splitSearchPaths(std::string("/1") + sep + "/2" + sep + "/3"),
              (V{"/1", "/2", "/3"}));
}

TEST(SearchPaths, APathContainingASpaceSurvivesIntact) {
    using V = std::vector<std::string>;
    EXPECT_EQ(fin::splitSearchPaths("/a dir/with spaces"), (V{"/a dir/with spaces"}));
}

// --- Argv errors in JSON mode -----------------------------------------------
//
// A mistake in the command line has to be reported in the format the command
// line asked for. It was not: every argv-level usage error printed plain text
// even under `--diagnostics=json`, which breaks ADR 0009's promise that no
// non-JSON byte reaches stderr in JSON mode — and breaks it for the argv error a
// programmatic caller is most likely to hit, a `finn` built against a newer
// `finc` passing a flag this one does not have.

TEST(JsonArgvErrors, AnUnknownFlagIsReportedAsJson) {
    auto r = runFinc({"--diagnostics=json", "--bogus"});
    EXPECT_EQ(r.exitCode, 2);
    EXPECT_EQ(r.out, "") << "stdout is reserved";
    auto lines = jsonLines(r.err);
    ASSERT_EQ(lines.size(), 2u) << r.err;
    for (const auto& l : lines) {
        EXPECT_EQ(l.front(), '{') << "non-JSON byte on stderr in JSON mode: " << l;
        EXPECT_EQ(l.back(), '}') << l;
    }
    EXPECT_NE(lines.front().find("\"kind\":\"diagnostic\""), std::string::npos) << lines.front();
    EXPECT_NE(lines.front().find("--bogus"), std::string::npos) << lines.front();
    EXPECT_NE(lines.back().find("\"kind\":\"summary\""), std::string::npos) << lines.back();
    EXPECT_NE(lines.back().find("\"exitCode\":2"), std::string::npos) << lines.back();
}

// The flag can be written after the mistake it has to render, so the format is
// read in a pre-pass over the whole command line rather than as it is reached.
TEST(JsonArgvErrors, TheFormatIsHonouredWhenWrittenAfterTheMistake) {
    auto r = runFinc({"--bogus", "--diagnostics=json"});
    EXPECT_EQ(r.exitCode, 2);
    auto lines = jsonLines(r.err);
    ASSERT_EQ(lines.size(), 2u) << r.err;
    EXPECT_EQ(lines.front().front(), '{') << r.err;
}

TEST(JsonArgvErrors, ASecondPositionalIsReportedAsJson) {
    TempFin a("fun main() <noret> {}\n", "posA");
    TempFin b("fun main() <noret> {}\n", "posB");
    auto r = runFinc({"--diagnostics=json", a.str(), b.str()});
    EXPECT_EQ(r.exitCode, 2);
    auto lines = jsonLines(r.err);
    ASSERT_EQ(lines.size(), 2u) << r.err;
    EXPECT_EQ(lines.front().front(), '{') << r.err;
}

TEST(JsonArgvErrors, AMissingOperandIsReportedAsJson) {
    auto r = runFinc({"--diagnostics=json", "--fin-libs"}, noFinLibs());
    EXPECT_EQ(r.exitCode, 2);
    auto lines = jsonLines(r.err);
    ASSERT_EQ(lines.size(), 2u) << r.err;
    EXPECT_EQ(lines.front().front(), '{') << r.err;
}

// `file` is null and `line` is 0 rather than pointing anywhere: the diagnostic
// is about the invocation, not about a place in a file. `file` was already
// emitted through the nullable path, so this costs the schema nothing.
TEST(JsonArgvErrors, AnArgvDiagnosticHasNoSourceLocation) {
    auto r = runFinc({"--diagnostics=json", "--bogus"});
    auto lines = jsonLines(r.err);
    ASSERT_GE(lines.size(), 1u) << r.err;
    EXPECT_NE(lines.front().find("\"file\":null"), std::string::npos) << lines.front();
    EXPECT_NE(lines.front().find("\"line\":0"), std::string::npos) << lines.front();
    EXPECT_NE(lines.front().find("\"code\":null"), std::string::npos) << lines.front();
    EXPECT_NE(lines.front().find("\"attribution\":null"), std::string::npos) << lines.front();
}

TEST(JsonArgvErrors, TheUsageNoteTravelsInTheHelpKeyRatherThanAsALooseLine) {
    auto r = runFinc({"--diagnostics=json", "--bogus"});
    auto lines = jsonLines(r.err);
    ASSERT_GE(lines.size(), 1u) << r.err;
    EXPECT_NE(lines.front().find("\"help\":\""), std::string::npos)
        << "the note has to be a key, or it is a non-JSON line:\n" << lines.front();
}

// A format value the compiler does not know cannot be honoured, so the error
// about it is reported in whatever format the pre-pass did settle.
TEST(JsonArgvErrors, AnUnhonourableFormatValueIsReportedInHuman) {
    TempFin f("fun main() <noret> {}\n", "badfmt");
    auto r = runFinc({f.str(), "--diagnostics=xml"});
    EXPECT_EQ(r.exitCode, 2);
    EXPECT_NE(stripAnsi(r.err).find("unknown diagnostics format 'xml'"), std::string::npos)
        << r.err;
}

TEST(JsonArgvErrors, ColorIsAlsoHonouredForAnArgvError) {
    auto r = runFinc({"--color=never", "--bogus"});
    EXPECT_EQ(r.exitCode, 2);
    EXPECT_EQ(r.err.find('\x1b'), std::string::npos) << "ANSI escape despite --color=never";
}

TEST(JsonArgvErrors, NoAnsiEscapeReachesTheJsonStreamOnAnArgvError) {
    auto r = runFinc({"--diagnostics=json", "--color=always", "--bogus"});
    EXPECT_EQ(r.err.find('\x1b'), std::string::npos)
        << "JSON mode must win over --color=always";
}

// --- Where the bundled standard library is looked for ------------------------
//
// `configureLoader` used to hardcode `tests/samples/stdlib`: a path belonging to
// whichever checkout produced the binary, resolved against the working
// directory. These bind the rule that replaced it. The property the old code
// lacked, and the one most of these assert from a different angle, is that the
// answer depends on where the binary *is* and not on where it was *run from*.

namespace {

// A throwaway directory tree, so the layout rule meets a real filesystem rather
// than string manipulation.
class TempTree {
public:
    TempTree() {
        root_ = uniqueTempPath("fin_tree");
        std::error_code ec;
        fs::create_directories(root_, ec);
    }
    ~TempTree() { std::error_code ec; fs::remove_all(root_, ec); }

    // Creates a directory under the tree and returns its path.
    fs::path dir(const std::string& rel) {
        std::error_code ec;
        fs::create_directories(root_ / rel, ec);
        return root_ / rel;
    }
    // Creates an empty file under the tree, parents included, and returns it.
    fs::path file(const std::string& rel) {
        std::error_code ec;
        fs::create_directories((root_ / rel).parent_path(), ec);
        std::ofstream f(root_ / rel, std::ios::binary);
        return root_ / rel;
    }
    const fs::path& root() const { return root_; }

private:
    fs::path root_;
};

} // namespace

// The layout a release archive unpacks to: `bin/finc` beside `lib/std`.
TEST(BundledLibraries, FindsLibStdBesideTheBinarysParent) {
    TempTree tree;
    const fs::path exe = tree.file("bin/finc");
    const fs::path lib = tree.dir("lib/std");

    const auto paths = fin::bundledLibraryPathsFor(exe.string());
    ASSERT_EQ(paths.size(), 1u) << "expected exactly the bundled stdlib";
    EXPECT_EQ(fs::path(paths[0]), lib);
}

// The same rule, unchanged, has to find a build tree — `finc` in `build/` beside
// the source `lib/std`. One rule covering both layouts is the reason it is
// written as "the binary's grandparent" rather than as "the install prefix":
// hardcoding `bin` here would pass the test above and fail this one.
TEST(BundledLibraries, TheSameRuleFindsABuildTree) {
    TempTree tree;
    const fs::path exe = tree.file("build/finc");
    const fs::path lib = tree.dir("lib/std");

    const auto paths = fin::bundledLibraryPathsFor(exe.string());
    ASSERT_EQ(paths.size(), 1u);
    EXPECT_EQ(fs::path(paths[0]), lib);
}

// Today's reality for every build in this repository: there is no `lib/std` yet.
// A search path that cannot be read is not reported, so that any future account
// of where the compiler looks does not name a directory it never opened.
TEST(BundledLibraries, NothingWhenLibStdIsAbsent) {
    TempTree tree;
    const fs::path exe = tree.file("bin/finc");
    EXPECT_TRUE(fin::bundledLibraryPathsFor(exe.string()).empty());
}

TEST(BundledLibraries, NothingWhenLibStdIsAFileRatherThanADirectory) {
    TempTree tree;
    const fs::path exe = tree.file("bin/finc");
    tree.file("lib/std");
    EXPECT_TRUE(fin::bundledLibraryPathsFor(exe.string()).empty());
}

// The branch that fires where the platform will not say where the binary is —
// a hardened container with no readable `/proc/self/exe`. Without the guard,
// `path("").parent_path().parent_path()` is empty and the rule would offer
// `lib/std` as a *relative* path, quietly reintroducing the working-directory
// dependence this whole change removes.
//
// The empty result therefore has to be produced by the guard and not by the
// filesystem merely happening to be bare — as first written, this test passed
// with the guard deleted. So it stages the directory the ungarded rule would
// find.
TEST(BundledLibraries, NothingWhenThePlatformWillNotSayWhereTheBinaryIs) {
    std::error_code ec;
    const fs::path libDir = fs::current_path() / "lib";
    const bool libExisted = fs::exists(libDir, ec);
    const bool stdExisted = fs::exists(libDir / "std", ec);
    if (!stdExisted) fs::create_directories(libDir / "std", ec);
    ASSERT_TRUE(fs::is_directory(libDir / "std", ec))
        << "could not stage " << (libDir / "std");

    const auto paths = fin::bundledLibraryPathsFor("");

    // Removed before asserting, so a failure does not leave the tree dirty, and
    // only what this test created is removed.
    if (!stdExisted) {
        fs::remove_all(libDir / "std", ec);
        if (!libExisted) fs::remove(libDir, ec);
    }

    EXPECT_TRUE(paths.empty())
        << "with no executable path known the rule fell back to a relative "
           "lib/std and found " << (paths.empty() ? std::string("nothing") : paths[0]);
}

// The regression assertion. What was wrong with `tests/samples/stdlib` was not
// only that it named this checkout: it was relative, so it meant a different
// directory for every working directory finc was invoked from.
TEST(BundledLibraries, ThePathIsAbsoluteWheneverTheBinaryPathIs) {
    TempTree tree;
    const fs::path exe = tree.file("bin/finc");
    tree.dir("lib/std");

    const auto paths = fin::bundledLibraryPathsFor(exe.string());
    ASSERT_EQ(paths.size(), 1u);
    EXPECT_TRUE(fs::path(paths[0]).is_absolute())
        << "a relative search path resolves against the working directory: " << paths[0];
}

// `executablePath()` is the one part of this that cannot be tested through a
// temporary tree, because it asks the platform about the process that is asking.
// So this asserts the properties every caller depends on, and deliberately not
// the file's name: an earlier version required the name to contain `fin_tests`,
// which made the test fail whenever the suite was linked under another name and
// reported that as a defect in code it does not touch.
//
// What stays untested is identity — that the path names *this* program rather
// than some other real executable. Nothing portable knows a process's own file
// independently of the call under test, so asserting it would only restate the
// implementation.
TEST(ExecutablePath, DescribesARealFile) {
    const std::string exe = fin::executablePath();
    ASSERT_FALSE(exe.empty()) << "the platform would not say where this binary is";
    EXPECT_TRUE(fs::path(exe).is_absolute())
        << "a relative answer would defeat the whole point of asking: " << exe;

    std::error_code ec;
    ASSERT_TRUE(fs::exists(exe, ec)) << exe;
    EXPECT_TRUE(fs::is_regular_file(exe, ec)) << exe;
    EXPECT_GT(fs::file_size(exe, ec), 0u) << exe;
}

// --- One missing module, one diagnostic -------------------------------------
//
// `ModuleLoader.hpp:23` states the invariant these tests hold it to: "one bad
// import produces one diagnostic in one format". It does not. A single
// unresolvable import produced three errors, and the JSON summary said
// `"errors":3` — which is the number `finn` shows a user who has one typo.
//
// The cause is that only *successes* are remembered. `loadModule` resolves,
// and on failure reports and returns `nullptr` (ModuleLoader.cpp:115-119)
// *before* the `moduleCache` check on the line after it, so nothing records
// that this import already failed. `loadModule` is called once by the macro
// expander (ExpanderDecls.cpp:21) and again by the analyzer, and each call
// re-resolves and re-reports. The expander returns silently on `nullptr`
// (`if (!moduleScope) return;`) precisely because it trusts the loader to have
// reported, so the loader cannot simply go quiet instead.
//
// All three import spellings behaved identically before the fix -- `import
// "mylib";`, `import mylib;`, and `import { x } from mylib;` each produced two
// location-less "module not found" errors plus the analyzer's located "Failed
// to load module". The third is `src/semantics/`, which this wave does not
// own; it is recorded as a wave-3 item. These tests therefore assert what the
// loader is responsible for: it reports once.

namespace {

size_t countOccurrences(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return 0;
    size_t n = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size())) {
        ++n;
    }
    return n;
}

// Diagnostic objects only, so the summary's own text cannot be counted as one.
std::vector<std::string> jsonDiagnostics(const std::string& err) {
    std::vector<std::string> out;
    for (const auto& l : jsonLines(err)) {
        if (l.find("\"kind\":\"diagnostic\"") != std::string::npos) out.push_back(l);
    }
    return out;
}

} // namespace

TEST(Soundness_ModuleDiagnostics, OneUnresolvableImportIsReportedOnceByTheLoader) {
    TempFin f(kImportsMyLib, "dupe");
    auto r = runFinc({f.str(), "--fin-libs=/nonexistent/only"}, noFinLibs());
    ASSERT_EQ(r.exitCode, 1) << stripAnsi(r.err);
    EXPECT_EQ(countOccurrences(stripAnsi(r.err), "module not found: mylib"), 1u)
        << "the loader reported the same unresolvable import more than once; it "
           "is called by both the macro expander and the analyzer, and a failed "
           "resolution has to be remembered the way a successful one is.\n"
        << stripAnsi(r.err);
}

// Deliberately not a fix for the count: it holds the count honest wherever the
// deduplication lives. Suppressing a repeat inside the emitter would leave
// `errorCount` at the pre-suppression number, and a summary that disagrees with
// the stream it summarises is worse than a duplicate -- a consumer reading only
// the summary and a consumer reading the objects would report different things.
TEST(Soundness_ModuleDiagnostics, TheSummaryCountAgreesWithTheDiagnosticsEmitted) {
    TempFin f(kImportsMyLib, "count");
    auto r = runFinc({f.str(), "--diagnostics=json", "--fin-libs=/nonexistent/only"},
                     noFinLibs());
    ASSERT_EQ(r.exitCode, 1) << r.err;

    const auto diags = jsonDiagnostics(r.err);
    size_t errors = 0;
    for (const auto& d : diags) {
        if (d.find("\"severity\":\"error\"") != std::string::npos) ++errors;
    }

    const auto lines = jsonLines(r.err);
    ASSERT_FALSE(lines.empty()) << r.err;
    const std::string& summary = lines.back();
    ASSERT_NE(summary.find("\"kind\":\"summary\""), std::string::npos) << summary;
    EXPECT_NE(summary.find("\"errors\":" + std::to_string(errors)), std::string::npos)
        << "the summary's error count and the number of error objects on the "
           "stream have to be the same number.\nsummary: " << summary
        << "\nerror objects: " << errors;
}

// The message a user actually hits when `lib/std` is missing or `--fin-libs` is
// wrong. `addSearchPath` (ModuleLoader.cpp:28) keeps a directory only if it
// already exists, so a path that is simply misspelled is discarded without a
// word and the failure is indistinguishable from a module that genuinely is not
// there. A warning would be the wrong instrument: ADR 0009 has exit `0` imply
// zero diagnostics, so warning about a bogus entry on a compile that otherwise
// succeeds would break the contract. Naming the places searched inside the
// failure costs nothing on success and puts the typo next to the error it caused.
TEST(Soundness_ModuleDiagnostics, TheFailureNamesWhereItLooked) {
    TempFin f(kImportsMyLib, "where");
    auto r = runFinc({f.str(), "--fin-libs=/nonexistent/only"}, noFinLibs());
    ASSERT_EQ(r.exitCode, 1) << stripAnsi(r.err);
    EXPECT_NE(stripAnsi(r.err).find("/nonexistent/only"), std::string::npos)
        << "a search path that does not exist was dropped silently, so the error "
           "cannot say where it looked and a misspelled --fin-libs looks exactly "
           "like a missing module.\n"
        << stripAnsi(r.err);
}

// A real search path is listed too, so the test above cannot be satisfied by
// echoing the flag's operand back without consulting the loader.
TEST(Soundness_ModuleDiagnostics, TheFailureNamesASearchPathThatDoesExist) {
    TempLib lib;  // exists, but holds mylib.fin -- so import a different module
    TempFin f("import \"absent_module\";\nfun main() <noret> {}\n", "wherereal");
    auto r = runFinc({f.str(), "--fin-libs=" + lib.dir()}, noFinLibs());
    ASSERT_EQ(r.exitCode, 1) << stripAnsi(r.err);
    EXPECT_NE(stripAnsi(r.err).find(lib.dir()), std::string::npos)
        << "the searched-paths list omitted a directory that was searched.\n"
        << stripAnsi(r.err);
}

// Pins a shape rather than a bug, because which way it should go is the owner's
// call. ADR 0009:86 documents two shapes: a located diagnostic (`file` set,
// `line` >= 1) and an argv diagnostic (`file` null, `line` 0, "about the
// invocation, not about a place in a file"). The loader's is a third -- `file`
// set and `line` 0 -- a diagnostic that claims a file and names no place in it,
// which renders as `mylib.fin:0:0` in any consumer that formats what it is
// given. `reportError(const std::string&)` (DiagnosticEngine.cpp:333) sets
// `d.file = filename` and leaves `d.line` at its default; the loader has no
// location to give it, because `loadModule` takes the import as a string and
// the `ImportModule` node that carries the location stays with the caller.
//
// If the ruling is that the shape is legal, ADR 0009 gains a sentence and this
// test is renamed into Soundness_. If the ruling is that it is not, threading
// the location into `loadModule` touches `src/macros/`, which this wave does
// not own -- then this inverts to assert `line >= 1`.
TEST(KnownDefect_ModuleDiagnostics, ALocationlessErrorStillNamesAFile) {
    TempFin f(kImportsMyLib, "shape");
    auto r = runFinc({f.str(), "--diagnostics=json", "--fin-libs=/nonexistent/only"},
                     noFinLibs());
    ASSERT_EQ(r.exitCode, 1) << r.err;

    bool sawFileWithoutAPlace = false;
    for (const auto& d : jsonDiagnostics(r.err)) {
        if (d.find("module not found") == std::string::npos) continue;
        if (d.find("\"file\":null") == std::string::npos &&
            d.find("\"line\":0") != std::string::npos) {
            sawFileWithoutAPlace = true;
        }
    }
    EXPECT_TRUE(sawFileWithoutAPlace)
        << "good news if this fails: the loader's diagnostic now names a place "
           "in the file it blames, or stopped naming the file. Invert this test "
           "into Soundness_ModuleDiagnostics asserting whichever it is.\n"
        << r.err;
}

// The deduplication above must not swallow a location. Two import sites naming
// the same missing module get one "where I looked" message between them -- the
// search is the same search, and saying so twice adds nothing -- but two located
// errors, one per site, because each `import` line is a place the user has to go
// and fix. Over-deduplicating to one message per module is the obvious next
// simplification and it would silently lose the second site.
TEST(Soundness_ModuleDiagnostics, EveryImportSiteKeepsItsOwnLocatedError) {
    TempFin f("import \"mylib\";\nimport \"mylib\";\nfun main() <noret> {}\n", "sites");
    auto r = runFinc({f.str(), "--fin-libs=/nonexistent/only"}, noFinLibs());
    ASSERT_EQ(r.exitCode, 1) << stripAnsi(r.err);
    const std::string err = stripAnsi(r.err);

    EXPECT_EQ(countOccurrences(err, "module not found: mylib"), 1u)
        << "one failed search, one explanation of it.\n" << err;
    EXPECT_EQ(countOccurrences(err, "Failed to load module 'mylib'"), 2u)
        << "each import site is a separate place to fix and needs its own "
           "located diagnostic; deduplication must key on the search, not on "
           "the module name alone.\n" << err;
}

// A file import and a package import of the same name search different places
// -- `resolvePath` CASE B tries the importing file's own directory first and
// CASE A does not -- so both are explained, and the two help strings differ.
// This is deliberate rather than a leak in the dedup key: suppressing the
// second would claim the one search that ran covered both.
TEST(Soundness_ModuleDiagnostics, TheTwoSearchKindsAreExplainedSeparately) {
    TempFin f("import \"mylib\";\nimport { a } from mylib;\nfun main() <noret> {}\n",
              "kinds");
    auto r = runFinc({f.str(), "--diagnostics=json", "--fin-libs=/nonexistent/only"},
                     noFinLibs());
    ASSERT_EQ(r.exitCode, 1) << r.err;

    std::vector<std::string> helps;
    for (const auto& d : jsonDiagnostics(r.err)) {
        if (d.find("module not found: mylib") == std::string::npos) continue;
        const size_t at = d.find("\"help\":");
        ASSERT_NE(at, std::string::npos) << d;
        helps.push_back(d.substr(at));
    }
    ASSERT_EQ(helps.size(), 2u)
        << "expected the file search and the package search to be explained "
           "once each\n" << r.err;
    EXPECT_NE(helps[0], helps[1])
        << "two messages carrying the same help are a duplicate after all; "
           "either the searches differ and the help should show it, or they do "
           "not and one message should be suppressed.\n" << r.err;
}

// --- An imported module's diagnostics point into the imported module ---------
//
// Every diagnostic raised inside an imported module named a line that does not
// exist, and printed a blank source snippet under it. `lib.fin` with an error on
// line 2, compiled directly, reported `lib.fin:2:26` and showed the line. The
// same file reached through `import "lib";` from a two-line `main.fin` reported
// `./lib.fin:4:26` -- and `6:26` the next time it was parsed.
//
// The arithmetic is the whole diagnosis: 2 lines of `main.fin` plus 2 is 4. The
// lexer's location is a file-scope `loc` in `lexer.l:13`, and
// `fin::reset_lexer_location()` (`lexer.l:34`, declared in `lexer.hpp:14`) puts
// it back to 1:1. `Driver.cpp:203` calls it for the file named on the command
// line; `ModuleLoader.cpp` never called it before `yy_scan_string`, so every
// module continued counting from wherever the previous parse stopped. The column
// was always right, which is what pointed at a line counter rather than at
// location tracking in general.
//
// This is not a corner case: it is every diagnostic in every imported module, so
// it is every multi-file project. No sample covers it, because a sample is one
// file -- which is why nothing in the corpus could have caught it and why these
// tests are here rather than in the corpus.

namespace {

// A library directory plus a source that imports it, both written by the caller.
class TempModule {
public:
    TempModule(const std::string& moduleName, const std::string& moduleBody) {
        root_ = uniqueTempPath("fin_mod", "_" + moduleName);
        std::error_code ec;
        fs::create_directories(root_, ec);
        std::ofstream(root_ / (moduleName + ".fin"), std::ios::binary) << moduleBody;
    }
    ~TempModule() { std::error_code ec; fs::remove_all(root_, ec); }
    std::string dir() const { return root_.string(); }

private:
    fs::path root_;
};

// `lib.fin` line 1 is fine, line 2 has one undefined call. Kept to two lines so
// the wrong answer and the right answer cannot coincide.
const char* kLibWithErrorOnLineTwo =
    "fun good() <int> { return 1; }\n"
    "fun bad() <int> { return undefined_thing(); }\n";

const char* kImportsLib = "import \"lib\";\nfun main() <noret> {}\n";

} // namespace

TEST(Soundness_ModuleDiagnostics, AnImportedModulesErrorPointsAtItsOwnLine) {
    TempModule lib("lib", kLibWithErrorOnLineTwo);
    TempFin f(kImportsLib, "modline");
    auto r = runFinc({f.str(), "--fin-libs=" + lib.dir()}, noFinLibs());
    ASSERT_EQ(r.exitCode, 1) << stripAnsi(r.err);
    const std::string err = stripAnsi(r.err);

    EXPECT_NE(err.find("lib.fin:2:"), std::string::npos)
        << "the error is on line 2 of lib.fin.\n" << err;
    EXPECT_EQ(err.find("lib.fin:4:"), std::string::npos)
        << "line 4 is main.fin's two lines plus two: the lexer's line counter "
           "carried over from the previous parse instead of being reset.\n"
        << err;
}

// The snippet was blank because the renderer looked up a line number the module
// does not have. Asserted separately from the number: a fix that corrected the
// number but still rendered nothing would leave the diagnostic just as unusable,
// and only this test would say so.
TEST(Soundness_ModuleDiagnostics, AnImportedModulesErrorShowsItsSourceLine) {
    TempModule lib("lib", kLibWithErrorOnLineTwo);
    TempFin f(kImportsLib, "modsnip");
    auto r = runFinc({f.str(), "--fin-libs=" + lib.dir()}, noFinLibs());
    ASSERT_EQ(r.exitCode, 1) << stripAnsi(r.err);
    EXPECT_NE(stripAnsi(r.err).find("return undefined_thing()"), std::string::npos)
        << "the caret line pointed at a line the module does not have, so there "
           "was no source text to print under it.\n"
        << stripAnsi(r.err);
}

// Compiling the module directly and reaching it through an import have to agree
// about where the error is. This is the assertion that cannot be satisfied by
// hardcoding an offset, because it compares the compiler against itself.
TEST(Soundness_ModuleDiagnostics, ImportedAndDirectAgreeOnTheLine) {
    TempModule lib("lib", kLibWithErrorOnLineTwo);
    TempFin f(kImportsLib, "modagree");

    auto viaImport = runFinc({f.str(), "--fin-libs=" + lib.dir()}, noFinLibs());
    auto direct = runFinc({lib.dir() + "/lib.fin"}, noFinLibs());
    ASSERT_EQ(viaImport.exitCode, 1) << stripAnsi(viaImport.err);
    ASSERT_EQ(direct.exitCode, 1) << stripAnsi(direct.err);

    EXPECT_NE(stripAnsi(direct.err).find("lib.fin:2:"), std::string::npos)
        << "control: compiled directly, the line is right.\n" << stripAnsi(direct.err);
    EXPECT_NE(stripAnsi(viaImport.err).find("lib.fin:2:"), std::string::npos)
        << "reached through an import, it has to be the same line.\n"
        << stripAnsi(viaImport.err);
}

// A module that fails is not cached -- `moduleCache` takes only successes -- so
// each pass that asked re-parsed it, and re-parsing is what advanced the line
// counter a second time. One two-file import cycle produced 21 errors this way.
// The line number is the visible symptom; the repeated work is the cause, and a
// count is the only thing that holds it down.
TEST(Soundness_ModuleDiagnostics, AFailedModuleIsAnalysedOnceNotOncePerPass) {
    TempModule lib("lib", kLibWithErrorOnLineTwo);
    TempFin f(kImportsLib, "modonce");
    auto r = runFinc({f.str(), "--fin-libs=" + lib.dir()}, noFinLibs());
    ASSERT_EQ(r.exitCode, 1) << stripAnsi(r.err);
    const std::string err = stripAnsi(r.err);

    EXPECT_EQ(countOccurrences(err, "Undefined function or type 'undefined_thing'"), 1u)
        << "the module's own error was reported once per pass that loaded it.\n" << err;
    EXPECT_EQ(countOccurrences(err, "semantic errors in module"), 1u)
        << "and so was the loader's summary of it.\n" << err;
}

// The cycle case, which is how this was found. It must terminate, must not be
// silent, and must not bury the answer -- an import cycle is one fact about the
// program and 21 errors is not a way to state one fact.
TEST(Soundness_ModuleDiagnostics, AnImportCycleIsReportedOnceAndTerminates) {
    const fs::path root = uniqueTempPath("fin_cycle", "_libs");
    std::error_code ec;
    fs::create_directories(root, ec);

    // The module names carry the unique directory's stem rather than being "a" and "b".
    // The entry file lives in the system temp directory, and ModuleLoader consults the
    // *importing file's own directory* before any search path (resolvePath CASE B step
    // 1), so a stray /tmp/a.fin shadows the pinned library and this test measures a
    // syntax error in somebody else's leftover probe instead of a cycle. That is not
    // hypothetical -- it happened, which is also what
    // KnownDefect_LibraryPaths.AFileBesideTheSourceShadowsAPinnedLibrary is about.
    const std::string tag = root.filename().string();
    const std::string modA = "cyc_a_" + tag;
    const std::string modB = "cyc_b_" + tag;
    std::ofstream(root / (modA + ".fin"), std::ios::binary)
        << "import \"" << modB << "\";\nfun fa() <int> { return 1; }\n";
    std::ofstream(root / (modB + ".fin"), std::ios::binary)
        << "import \"" << modA << "\";\nfun fb() <int> { return 2; }\n";

    TempFin f("import \"" + modA + "\";\nfun main() <noret> {}\n", "cycle");
    auto r = runFinc({f.str(), "--fin-libs=" + root.string()}, noFinLibs());
    const std::string err = stripAnsi(r.err);
    fs::remove_all(root, ec);

    EXPECT_EQ(r.exitCode, 1) << "a cycle is an error and has to be one\n" << err;
    EXPECT_NE(err.find("circular dependency"), std::string::npos) << err;
    EXPECT_LE(countOccurrences(err, "circular dependency detected"), 1u)
        << "one cycle, one statement of it.\n" << err;
}

// A file beside the source shadows a library the build pinned.
//
// Driver.cpp goes to real trouble for hermeticity: as soon as `--fin-libs` or `FIN_LIBS`
// names a path it drops the working directory from the search list, because "leaving it
// in would let a file in the project shadow a module the build pinned", and it says
// outright that `finn` "cannot fix that from its side at any price". The reasoning is
// right and the working directory is duly gone -- but the *importing file's own
// directory* is still consulted, and it is consulted first: ModuleLoader::resolvePath
// CASE B step 1 checks `rootBasePath / rawImport` before it looks at a single search
// path. So `import "collection";` next to a stray collection.fin picks up the stray, and
// the pin is worth nothing.
//
// Two readings, and the corpus does not settle which is right, so this is booked rather
// than fixed. A quoted import is overloaded: `import "tests/samples/macros.fin";`
// (importing.fin:7) is a path relative to the project and has to resolve against the
// source, while `import "somelib";` (extern_as.fin:9) is emphatically a *library* lookup
// with no path at all -- "HOW DOES THE COMPILER KNOW WHERE TO IMPORT THIS LIBRARY". One
// spelling, two meanings, and only the second one should honour a pin. A plausible rule
// is that a name with no separator and no `.fin` is a library lookup and skips the
// source directory, but that is a language decision and it is the owner's.
//
// It has already cost something: Soundness_ModuleDiagnostics.AnImportCycleIsReportedOnce-
// AndTerminates went red because a probe file left in /tmp shadowed the library it had
// pinned, and it took a while to see that the test was right and the leftover was not.
//
// The inversion is Soundness_LibraryPaths.APinnedLibraryIsNotShadowedByASiblingFile.
TEST(KnownDefect_LibraryPaths, AFileBesideTheSourceShadowsAPinnedLibrary) {
    // The library the build pins, and a decoy of the same name beside the source. The
    // two differ in a way the diagnostic can see: only the decoy has an error in it.
    TempLib lib;  // writes <dir>/mylib.fin, empty and valid

    const fs::path decoy = fs::path(uniqueTempPath("fin_shadow", "_src"));
    std::error_code ec;
    fs::create_directories(decoy, ec);
    std::ofstream(decoy / "mylib.fin", std::ios::binary)
        << "fun shadowed() <int> { return nope_from_the_decoy; }\n";
    const fs::path src = decoy / "app.fin";
    std::ofstream(src, std::ios::binary) << kImportsMyLib;

    const auto r = runFinc({src.string(), "--fin-libs=" + lib.dir()}, noFinLibs());
    const std::string err = stripAnsi(r.err);
    fs::remove_all(decoy, ec);

    EXPECT_EQ(r.exitCode, 1)
        << "when this starts exiting 0 the pin is being honoured -- invert and rename\n"
        << err;
    EXPECT_NE(err.find("nope_from_the_decoy"), std::string::npos)
        << "the decoy beside the source was loaded instead of the pinned library.\n"
           "If this stops matching, resolvePath stopped preferring the source directory\n"
           "and the pin now wins, which is the fix this test is waiting for.\n"
        << err;
}

// --- Pinning a library against a same-named file next to the source ---
//
// `finn` passes --fin-libs to pin the environment a build compiles against, and
// Driver.cpp:83-93 goes to real trouble for that promise: it drops the working
// directory from the search list as soon as any library path is named, because
// "leaving it in would let a file in the project shadow a module the build
// pinned". That reasoning is sound and the code does what it says.
//
// It is also not sufficient. ModuleLoader::resolvePath CASE B step 1 resolves a
// quoted import against `rootBasePath` -- the *importing file's* directory --
// before it ever consults the search paths, and it does so unconditionally. In a
// real project every source file has the project's other files as siblings, so
// the hole the driver closed against the working directory stays open against the
// directory the source itself lives in. Dropping "." was necessary, not enough.
//
// Package imports (CASE A) consult only searchPaths and never rootBasePath, so
// the two spellings of the same import disagree: with one pinned `mylib` and one
// same-named sibling, `from mylib` gets the pinned one and `from "mylib"` gets
// the sibling. That contrast is the evidence the quoted form is the odd one out
// rather than the whole design being cwd-flavoured, and it is why the package
// case below is Soundness while the quoted case is a KnownDefect.
//
// The shadowing is silent: the build succeeds against the wrong module. A warning
// would be the obvious answer and the contract forbids it -- ADR 0009 has exit 0
// imply zero diagnostics, and a shadowed import compiles fine -- which is the
// same wall the bogus-search-path finding hit.
//
// Whether the fix is "a bare quoted name is a library lookup and skips step 1,
// while a quoted *path* keeps it" is a language decision, not a loader detail:
// the corpus documents both meanings for the one syntax. importing.fin calls
// `import "tests/samples/macros.fin"` "a normal file import" and says of
// `import "somelib"` that it has no path so the compiler must find it through
// FIN_LIBS/--fin-libs. A rule keyed on whether the string looks like a path
// reads straight off those two lines and closes the hole exactly. It is left for
// the owner, because it changes how every import in every project resolves.

namespace {

// One module name, two definitions that differ in return type, so the type
// checker reports which file won rather than us having to guess.
const char* kPinnedReturnsInt   = "fun libfn() <int> { return 7; }\n";
const char* kPinnedReturnsFloat = "fun libfn() <float> { return 7.0; }\n";
const char* kSiblingReturnsFloat = "fun libfn() <float> { return 0.5; }\n";

// A project directory holding one source file, optionally beside a same-named
// module that would shadow the pinned library, plus a separate library directory
// to point --fin-libs at. The source's own directory is what does the shadowing,
// so nothing here depends on the working directory.
class TempPinnedProject {
public:
    TempPinnedProject(const std::string& appBody,
                      const std::string& pinnedBody,
                      const std::string& siblingBody = "") {
        proj_ = uniqueTempPath("fin_pin", "_proj");
        libs_ = uniqueTempPath("fin_pin", "_libs");
        std::error_code ec;
        fs::create_directories(proj_, ec);
        fs::create_directories(libs_, ec);
        std::ofstream(proj_ / "app.fin", std::ios::binary) << appBody;
        std::ofstream(libs_ / "mylib.fin", std::ios::binary) << pinnedBody;
        if (!siblingBody.empty()) {
            std::ofstream(proj_ / "mylib.fin", std::ios::binary) << siblingBody;
        }
    }
    ~TempPinnedProject() {
        std::error_code ec;
        fs::remove_all(proj_, ec);
        fs::remove_all(libs_, ec);
    }
    std::string app() const { return (proj_ / "app.fin").string(); }
    std::string libs() const { return libs_.string(); }

private:
    fs::path proj_;
    fs::path libs_;
};

const char* kAppPackageImport = "import { libfn } from mylib;\n"
                                "fun main() <noret> { let x <int> = libfn(); }\n";
const char* kAppQuotedImport  = "import { libfn } from \"mylib\";\n"
                                "fun main() <noret> { let x <int> = libfn(); }\n";

} // namespace

// The control that keeps the two tests below honest. `let x <int> = libfn()`
// only reports which module won if the return type is actually checked, so this
// pins a pinned-library type error being *reachable*. If this test ever goes
// green, the others stop meaning anything and start passing for free.
TEST(Soundness_PinnedLibraries, TheReturnTypeOfAPinnedLibrarysFunctionIsChecked) {
    TempPinnedProject p(kAppPackageImport, kPinnedReturnsFloat);
    auto r = runFinc({p.app(), "--fin-libs=" + p.libs()}, noFinLibs());
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(r.exitCode, 1) << "int = float() has to be caught, or the two "
                                "shadowing tests below prove nothing\n" << err;
    EXPECT_NE(err.find("Type mismatch"), std::string::npos) << err;
}

// The invariant `finn` depends on: what --fin-libs names is what gets compiled
// against, whatever files happen to sit next to the source.
TEST(Soundness_PinnedLibraries, APackageImportIsNotShadowedByASiblingFile) {
    TempPinnedProject p(kAppPackageImport, kPinnedReturnsInt, kSiblingReturnsFloat);
    auto r = runFinc({p.app(), "--fin-libs=" + p.libs()}, noFinLibs());
    EXPECT_EQ(r.exitCode, 0)
        << "the pinned mylib returns int and the sibling returns float, so a "
           "type mismatch here means the sibling won and the pin was defeated\n"
        << stripAnsi(r.err);
}

// Separates precedence from reachability. Without this, the KnownDefect below
// would also be satisfied by a quoted import that simply never reaches a search
// path -- a different defect with a different fix.
TEST(Soundness_PinnedLibraries, AQuotedImportReachesAPinnedLibraryWhenNothingShadowsIt) {
    TempPinnedProject p(kAppQuotedImport, kPinnedReturnsInt);
    auto r = runFinc({p.app(), "--fin-libs=" + p.libs()}, noFinLibs());
    EXPECT_EQ(r.exitCode, 0)
        << "a quoted import must still find a pinned library (resolvePath step 3)\n"
        << stripAnsi(r.err);
}

// Asserts the defect. resolvePath CASE B step 1 tries the importing file's own
// directory before any search path, so the float sibling wins over the int
// library the build pinned, silently.
//
// When the owner rules on the library-lookup-versus-file-path question and the
// quoted form stops consulting rootBasePath for a bare name, this test fails --
// which is the good news. Invert it to EXPECT_EQ(r.exitCode, 0) with the message
// from APackageImportIsNotShadowedByASiblingFile and rename it into
// Soundness_PinnedLibraries.AQuotedImportIsNotShadowedByASiblingFile. Do not
// relax it, and do not delete it: the two spellings agreeing is the property.
TEST(KnownDefect_PinnedLibraries, AQuotedImportIsShadowedByASiblingFile) {
    TempPinnedProject p(kAppQuotedImport, kPinnedReturnsInt, kSiblingReturnsFloat);
    auto r = runFinc({p.app(), "--fin-libs=" + p.libs()}, noFinLibs());
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(r.exitCode, 1)
        << "TODAY'S BEHAVIOUR, not the desired one: a same-named sibling file "
           "beats the pinned library for a quoted import\n" << err;
    EXPECT_NE(err.find("Type mismatch"), std::string::npos)
        << "the float sibling won over the int library\n" << err;
}

namespace {

// An app plus a directory of library modules reachable through `--fin-libs`. The
// pinned-project helper above writes exactly one library file named `mylib.fin`;
// these tests need two modules that import each other, so this takes the set.
class TempLibSet {
public:
    TempLibSet(const std::string& appBody,
               const std::vector<std::pair<std::string, std::string>>& libFiles) {
        proj_ = uniqueTempPath("fin_libset", "_proj");
        libs_ = uniqueTempPath("fin_libset", "_libs");
        std::error_code ec;
        fs::create_directories(proj_, ec);
        fs::create_directories(libs_, ec);
        std::ofstream(proj_ / "app.fin", std::ios::binary) << appBody;
        for (const auto& f : libFiles)
            std::ofstream(libs_ / f.first, std::ios::binary) << f.second;
    }
    ~TempLibSet() {
        std::error_code ec;
        fs::remove_all(proj_, ec);
        fs::remove_all(libs_, ec);
    }
    std::string app() const { return (proj_ / "app.fin").string(); }
    std::string libs() const { return libs_.string(); }

private:
    fs::path proj_;
    fs::path libs_;
};

// One module, two kinds of declaration, one of them not public. `inner` exists so
// that what `m` re-exports can be told apart from what `m` declares.
const std::vector<std::pair<std::string, std::string>> kStarLibs = {
    {"inner.fin", "pub fun inner_fn() <int> { return 7; }\n"
                  "pub struct InnerType { pub v <int>, }\n"},
    {"m.fin",     "import { inner_fn } from inner;\n"
                  "pub fun m_fn() <int> { return 1; }\n"
                  "fun m_private() <int> { return 2; }\n"
                  "pub struct MType { pub v <int>, }\n"},
};

FincRun runStar(const std::string& appBody) {
    TempLibSet p(appBody, kStarLibs);
    return runFinc({p.app(), "--fin-libs=" + p.libs()}, noFinLibs());
}

} // namespace

// The control. `import *` is what the three tests below are about, so a named
// import of the same symbol has to work first: without this, a red `import *`
// test is equally consistent with the search path being wrong, the module not
// parsing, or the helper writing files somewhere the compiler never looks.
TEST(Soundness_Imports, ANamedImportBindsTheSymbolItNames) {
    const auto r = runStar("import { m_fn } from m;\n"
                           "fun main() <noret> { let x <int> = m_fn(); }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

// `import * from somelib;` -- tests/samples/importing.fin:11, whose comment reads
// "this will import ALL symbols from the somelib library".
//
// It did not import any. `visit(ImportModule&)` looked `*` up in the module's
// scope as though it were an identifier and reported `Module 'm' does not export
// '*'` (Analyzer_Decl.cpp:475) -- wrong twice over: `*` is not a name a module
// could export, and the module in question exports everything the import wanted.
// The user then got a second, misleading diagnostic for every use of a symbol the
// import was supposed to bind, so one unimplemented form produced a cascade that
// pointed away from its own cause.
//
// Split from the type case below on purpose: `Scope` keeps values in `symbols`
// and types in `types`, and copying one map is a complete-looking fix that leaves
// the other broken.
TEST(Soundness_Imports, ImportStarBindsEveryValueTheModuleDeclares) {
    const auto r = runStar("import * from m;\n"
                           "fun main() <noret> { let x <int> = m_fn(); }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(Soundness_Imports, ImportStarBindsEveryTypeTheModuleDeclares) {
    const auto r = runStar("import * from m;\n"
                           "fun main() <noret> { let x <MType>; }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

// The cascade, asserted separately from the two tests above because it is the part
// a user actually reports: not "the feature is missing" but "the compiler says my
// symbol is undefined". A fix that bound the symbols would take this with it; a
// fix that only silenced the `does not export '*'` message would not.
TEST(Soundness_Imports, ImportStarDoesNotLeaveItsSymbolsUndefined) {
    const auto r = runStar("import * from m;\n"
                           "fun main() <noret> { let x <int> = m_fn(); }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("Undefined function or type 'm_fn'"), std::string::npos)
        << "the star import failed and then blamed the symbol it should have bound\n"
        << err;
    EXPECT_EQ(err.find("does not export '*'"), std::string::npos)
        << "`*` was looked up as if it were a symbol name\n" << err;
}

// Explicit beats wildcard. The importer declares its own `MType` before the star
// import, and the module has one too; `x.w` exists only on the importer's. Without
// this, "copy the module's scope into mine" is a one-line fix that silently lets a
// library rename a type out from under the file importing it.
//
// This test was written after the fix rather than before it, so it has never been
// red for the right reason -- before the fix `import *` bound nothing at all, which
// is not the same as binding the right thing. Proven by mutation instead: dropping
// the `if (!currentScope->resolveType(...))` guard in Analyzer_Decl.cpp fails it.
TEST(Soundness_Imports, ImportStarDoesNotShadowTheImportersOwnDeclaration) {
    const auto r = runStar("struct MType { pub w <string>, }\n"
                           "import * from m;\n"
                           "fun main() <noret> { let x <MType>; x.w = \"hi\"; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("has no member 'w'"), std::string::npos)
        << "the star import replaced the importer's own MType with the module's\n" << err;
    EXPECT_EQ(r.exitCode, 0) << err;
}

// A module that cannot be found must be reported as missing, not as a module that
// declines to export `*`. This one passed before `import *` was implemented and
// must keep passing after: it is the case where the old message was almost right,
// and the risk of the fix is that the module-load failure stops being reported at
// all once `*` no longer goes through the symbol lookup.
TEST(Soundness_Imports, ImportStarFromAMissingModuleReportsTheModule) {
    const auto r = runStar("import * from nosuchmod;\n"
                           "fun main() <noret> {}\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_NE(r.exitCode, 0);
    EXPECT_NE(err.find("module not found: nosuchmod"), std::string::npos) << err;
}

// Two holes in the same function, both real, neither fixable without a ruling.
// They are here so that the ruling is made against a measurement instead of a
// memory, and so that whichever way it goes, something fails when the behaviour
// changes.

// `import { X } from m::ns` names a namespace. The parser keeps it --
// `module_path` in parser.y:776 splits `m::ns` into the module and the namespace
// tail, and `ImportModule::namespace_path` holds it -- and
// `visit(ImportModule&)` never reads the field. So the namespace is not merely
// unchecked, it is discarded: `from m::nonexistent` and `from m::a::b::c` both
// compile clean, and so would a namespace no module has ever declared.
//
// Not fixable as it stands. Enforcing it means knowing which namespace each
// symbol was declared in, and `namespace std { ... }` currently has no effect on
// anything -- dropping the block from a module changes no import. That is the
// ruling: what does a namespace do to a module's symbol table, and is naming the
// wrong one an error or a no-op? Twelve stdlib samples open with `namespace std`
// and eleven corpus imports name `::std`, so the answer is load-bearing for the
// standard library and should be settled before it is written, not after.
TEST(KnownDefect_Imports, AnImportIgnoresTheNamespaceItNames) {
    for (const char* ns : {"m::nonexistent", "m::a::b::c"}) {
        const auto r = runStar("import { m_fn } from " + std::string(ns) + ";\n"
                               "fun main() <noret> { let x <int> = m_fn(); }\n");
        EXPECT_EQ(r.exitCode, 0)
            << "`from " << ns << "` was rejected -- if that is the fix landing, invert "
               "this test rather than relaxing it\n" << stripAnsi(r.err);
    }
}

// `pub` is not enforced on an import. `m_private` is declared without it and
// imports fine, and the diagnostic for a name a module really does not have says
// "does not export", which is a promise the code does not keep: it means "does
// not declare". The AST carries the fact -- `is_public` on every declaration node
// -- and `Symbol` (Scope.hpp:12) has nowhere to put it, so it is dropped when the
// module's scope is built. Types are worse off: `Scope::types` is a bare name-to-
// type map with no visibility at all.
//
// Blocked on a ruling, and the corpus is why. tests/samples/structs.fin:3 declares
// `struct Vector3` with no `pub`, and tests/samples/importing.fin:3 imports it with
// the comment "This is allowed and ONLY imports the symbol (Vector3)". So either
// `pub` is required to export and that sample is wrong, or a quoted-path file
// import is exempt and only library imports are gated, or `pub` is advisory. Under
// ADR 0008 the first of those changes sample code and needs a ratified decision;
// the other two change the compiler. Nothing here picks one.
TEST(KnownDefect_Imports, ANonPublicSymbolCanBeImportedByName) {
    const auto r = runStar("import { m_private } from m;\n"
                           "fun main() <noret> { let x <int> = m_private(); }\n");
    EXPECT_EQ(r.exitCode, 0)
        << "a non-public symbol was refused. If visibility now has a meaning, this test "
           "is the record of when it did not -- invert it, do not delete it.\n"
        << stripAnsi(r.err);
}

// --- Two stated invariants, swept out of the comments and checked ------------
//
// Neither of these found a defect. They are here because both claims are made in
// a comment, both are relied on by something that ships, and neither had a test
// -- so on this platform nothing would notice either one breaking.
//
// DiagnosticEngine.cpp:194 says "Tolerate a stray CR so a column never counts
// one", and the claim spans two components: splitLines() strips the CR from the
// stored snippet, while the column itself comes from the lexer. release.yml
// builds Windows archives, where CRLF is what a source file has by default, and
// the whole suite runs on LF -- so dropping that one pop_back() would put a
// carriage return in every snippet and misalign every caret for Windows users
// while CI stayed green.
//
// DiagnosticEngine.cpp:102 says a key whose value is absent "ships as JSON null
// rather than being omitted, so a consumer never has to distinguish 'missing
// key' from 'no value'". `finn` is that consumer. The tests already here check
// individual keys on one diagnostic shape; the claim is the stronger one that
// every shape carries the same keys, and there are three shapes today (located;
// argv, with file null and line 0; and the loader's reportError path, with file
// set and line 0 -- see KnownDefect_ModuleDiagnostics.ALocationlessErrorStillNamesAFile).

namespace {

// Top-level keys of a flat JSON object, in order. Every object in this stream is
// flat and no message in these tests contains the sequence `":`, which is what
// keeps a naive scan honest here.
std::string jsonKeys(const std::string& obj) {
    static const std::regex key(R"RX("([A-Za-z]+)":)RX");
    std::string out;
    for (auto it = std::sregex_iterator(obj.begin(), obj.end(), key);
         it != std::sregex_iterator(); ++it) {
        if (!out.empty()) out += ",";
        out += (*it)[1].str();
    }
    return out;
}

// The `"line":...` through `"endColumn":N` slice of a diagnostic: the whole
// position, with the file path left out so two temp files can be compared.
std::string positionSlice(const std::string& obj) {
    const size_t b = obj.find("\"line\":");
    const size_t e = obj.find(',', obj.find("\"endColumn\":"));
    if (b == std::string::npos || e == std::string::npos) return "";
    return obj.substr(b, e - b);
}

const char* kErrorOnLineTwoBody = "fun main() <noret> {\n"
                                  "    let x <int> = nope();\n"
                                  "}\n";

std::string withCrlf(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\n') out += '\r';
        out += c;
    }
    return out;
}

} // namespace

// Compares the compiler against itself on the same source in two encodings, so
// it cannot be satisfied by hardcoding a position.
TEST(Soundness_SourceEncoding, CrlfAndLfSourcesReportTheSamePosition) {
    TempFin lf(kErrorOnLineTwoBody, "lf");
    TempFin crlf(withCrlf(kErrorOnLineTwoBody), "crlf");
    auto a = runFinc({lf.str(), "--diagnostics=json"}, noFinLibs());
    auto b = runFinc({crlf.str(), "--diagnostics=json"}, noFinLibs());
    auto da = jsonDiagnostics(a.err);
    auto db = jsonDiagnostics(b.err);
    ASSERT_EQ(da.size(), 1u) << a.err;
    ASSERT_EQ(db.size(), 1u) << b.err;
    const std::string pa = positionSlice(da[0]);
    EXPECT_FALSE(pa.empty()) << da[0];
    EXPECT_EQ(pa, positionSlice(db[0]))
        << "the same source in CRLF and LF must land on the same line and column\n"
        << "  lf:   " << da[0] << "\n  crlf: " << db[0];
}

// The snippet half of the same claim. A CR reaching the rendered line is what
// puts `^M` in a Windows user's diagnostic and shifts the caret.
TEST(Soundness_SourceEncoding, NoCarriageReturnReachesARenderedDiagnostic) {
    TempFin crlf(withCrlf(kErrorOnLineTwoBody), "crlf2");
    auto r = runFinc({crlf.str()}, noFinLibs());
    EXPECT_EQ(r.exitCode, 1) << stripAnsi(r.err);
    EXPECT_EQ(r.err.find('\r'), std::string::npos)
        << "splitLines() must strip the CR before the snippet is rendered\n"
        << stripAnsi(r.err);
    EXPECT_NE(stripAnsi(r.err).find("let x <int> = nope();"), std::string::npos)
        << "the snippet still has to be there, or the test above passes vacuously\n"
        << stripAnsi(r.err);
}

// All three of today's diagnostic shapes, compared against each other rather
// than against a written-down list, so a key added later is required to appear
// everywhere instead of making this test stale.
TEST(Soundness_JsonSchema, EveryDiagnosticShapeCarriesTheSameKeys) {
    TempFin located("fun main() <noret> { let ; }\n", "loc");
    TempFin missingModule("import { z } from nosuchmodule;\nfun main() <noret> {}\n", "mod");

    std::vector<std::pair<std::string, std::string>> shapes;  // label -> keys
    auto collect = [&](const char* label, const FincRun& r) {
        for (const auto& d : jsonDiagnostics(r.err)) {
            shapes.emplace_back(label, jsonKeys(d));
        }
    };
    collect("located", runFinc({located.str(), "--diagnostics=json"}, noFinLibs()));
    collect("argv", runFinc({"--diagnostics=json", "--bogus"}, noFinLibs()));
    collect("loader", runFinc({missingModule.str(), "--diagnostics=json"}, noFinLibs()));

    ASSERT_GE(shapes.size(), 3u) << "all three shapes have to actually be produced";
    EXPECT_NE(shapes[0].second.find("attribution"), std::string::npos)
        << "sanity: keys were parsed at all, got " << shapes[0].second;
    for (const auto& s : shapes) {
        EXPECT_EQ(s.second, shapes[0].second)
            << "shape '" << s.first << "' carries a different key set than '"
            << shapes[0].first << "'; a consumer would have to tell a missing key "
               "from a null one";
    }
}

// --- Import depth, and why depth 2 was not enough ---------------------------
//
// ModuleLoader.cpp:207 justifies calling reset_lexer_location() before each
// module's parse with a claim about control flow: "Safe to do here because a
// parse is never interrupted by another parse: a module is parsed at step 5 and
// only expanded, which is what can recurse, at step 6." That is the load-bearing
// half. If a nested load could begin while an outer parse was in progress, the
// reset would renumber the outer file's remaining lines from 1 and the fix would
// have traded one wrong-location bug for a subtler one.
//
// AnImportedModulesErrorPointsAtItsOwnLine covers depth 2, which cannot tell the
// two apart -- at depth 2 there is only ever one parse to get wrong. This runs a
// three-deep chain with a diagnostic in every file, each on that file's own last
// line, so a location that carried over from another parse lands past the end of
// its file and renders a blank snippet rather than merely being off by a little.
// Measured before it was written: all three are correct today.
//
// It is the recursion claim this pins, not the reset -- a refactor that moved
// import expansion into the parse, which is how plenty of compilers do it, would
// break this while leaving the reset itself untouched.

namespace {

// Each file's own error sits on its last line, and the three lengths differ, so
// no two files could accidentally agree on a line number.
struct TempChain {
    TempChain() {
        root = uniqueTempPath("fin_chain", "_libs");
        std::error_code ec;
        fs::create_directories(root, ec);
        std::ofstream(root / "b.fin", std::ios::binary)
            << "fun b1() <int> { return 1; }\n"
               "fun b2() <int> { return 2; }\n"
               "fun b3() <int> { return 3; }\n"
               "fun bBad() <int> { return b_undefined(); }\n";   // line 4
        std::ofstream(root / "a.fin", std::ios::binary)
            << "import { b1 } from \"b\";\n"
               "fun a1() <int> { return 1; }\n"
               "fun aBad() <int> { return a_undefined(); }\n";   // line 3
        std::ofstream(root / "main.fin", std::ios::binary)
            << "import { a1 } from \"a\";\n"
               "fun main() <noret> { m_undefined(); }\n";        // line 2
    }
    ~TempChain() { std::error_code ec; fs::remove_all(root, ec); }
    std::string libs() const { return root.string(); }
    std::string main() const { return (root / "main.fin").string(); }
    fs::path root;
};

} // namespace

TEST(Soundness_ModuleDiagnostics, AThreeDeepChainKeepsEveryFilesOwnLines) {
    TempChain c;
    auto r = runFinc({c.main(), "--fin-libs=" + c.libs()}, noFinLibs());
    const std::string err = stripAnsi(r.err);
    ASSERT_EQ(r.exitCode, 1) << err;

    // Each file's diagnostic on its own line, at the column its own text puts it.
    EXPECT_NE(err.find("b.fin:4:27"), std::string::npos) << err;
    EXPECT_NE(err.find("a.fin:3:27"), std::string::npos) << err;
    EXPECT_NE(err.find("main.fin:2:22"), std::string::npos) << err;

    // And each rendered snippet is that file's line, not a blank -- which is how
    // a carried-over location shows itself when it lands past end of file.
    EXPECT_NE(err.find("return b_undefined()"), std::string::npos)
        << "b.fin's snippet is missing, so its location is out of range\n" << err;
    EXPECT_NE(err.find("return a_undefined()"), std::string::npos)
        << "a.fin's snippet is missing, so its location is out of range\n" << err;
    EXPECT_NE(err.find("m_undefined()"), std::string::npos) << err;

    // The import sites keep their own locations too, one per depth.
    EXPECT_EQ(countOccurrences(err, "Failed to load module"), 2u)
        << "one per import site: a.fin importing b, main.fin importing a\n" << err;
}

namespace {

// `yy_scan_string(s)` is defined as `yy_scan_bytes(s, strlen(s))` (build/lexer.cpp:2465),
// so handing the lexer `source.c_str()` ends the translation unit at the first embedded
// NUL even though the std::string read from disk holds the whole file. The two spellings
// below differ only in whether a NUL sits between the halves, and the second half cannot
// compile -- so if the compiler disagrees about them, the NUL is what it took for the end
// of the input.
const char* kCompilingHalf = "fun ok_half() <int> { return 1; }\n";
const char* kBrokenHalf    = "fun broken_half() <int> { let ; }\n";

std::string halvesSplitBy(const std::string& separator) {
    return std::string(kCompilingHalf) + separator + kBrokenHalf;
}

const std::string kNul(1, '\0');

// A module beside the file that imports it, so the quoted import resolves without any
// library path being involved.
struct TempModuleProject {
    explicit TempModuleProject(const std::string& moduleBody) {
        root = uniqueTempPath("fin_nulmod", "_proj");
        std::error_code ec;
        fs::create_directories(root, ec);
        std::ofstream m(root / "nulmod.fin", std::ios::binary);
        m.write(moduleBody.data(), (std::streamsize)moduleBody.size());
        std::ofstream(root / "main.fin", std::ios::binary)
            << "import { ok_half } from \"nulmod\";\n"
               "fun main() <noret> { let x <int> = ok_half(); }\n";
    }
    ~TempModuleProject() { std::error_code ec; fs::remove_all(root, ec); }
    std::string main() const { return (root / "main.fin").string(); }
    fs::path root;
};

} // namespace

TEST(Soundness_SourceEncoding, SourceAfterANulByteIsNotDiscarded) {
    TempFin joined(halvesSplitBy(""), "nul_ctl");
    const auto control = runFinc({joined.str()}, noFinLibs());
    ASSERT_NE(control.exitCode, 0)
        << "the second half has to be rejected on its own, or this test proves nothing.\n"
        << control.err;

    TempFin split(halvesSplitBy(kNul), "nul_split");
    const auto r = runFinc({split.str()}, noFinLibs());
    EXPECT_NE(r.exitCode, 0)
        << "everything after a NUL byte was discarded: the same source without the NUL is "
           "rejected, so the compiler compiled a prefix of the file and reported success "
           "for the whole of it.\n"
        << r.err;
}

TEST(Soundness_SourceEncoding, SourceAfterANulByteInAnImportedModuleIsNotDiscarded) {
    // ModuleLoader.cpp:211 is a second call site with the same mistake, so a fix applied
    // to the driver alone would leave an imported module silently truncated.
    TempModuleProject control(halvesSplitBy(""));
    const auto c = runFinc({control.main()}, noFinLibs());
    ASSERT_NE(c.exitCode, 0)
        << "a module whose second half cannot parse has to be rejected, or this test "
           "proves nothing.\n"
        << c.err;

    TempModuleProject split(halvesSplitBy(kNul));
    const auto r = runFinc({split.main()}, noFinLibs());
    EXPECT_NE(r.exitCode, 0)
        << "an imported module was truncated at its first NUL byte and the build was "
           "reported successful.\n"
        << r.err;
}

namespace {

// N modules that cannot parse, plus a main that imports every one of them and carries an
// error of its own so that no shape has zero diagnostics.
struct TempBrokenModules {
    explicit TempBrokenModules(int count) {
        root = uniqueTempPath("fin_undercount", "_proj");
        std::error_code ec;
        fs::create_directories(root, ec);
        std::string mainBody;
        for (int i = 1; i <= count; ++i) {
            const std::string n = std::to_string(i);
            std::ofstream(root / ("brk" + n + ".fin"), std::ios::binary)
                << "fun broken" << n << "() <int> { let ; }\n";
            mainBody += "import { broken" + n + " } from \"brk" + n + "\";\n";
        }
        mainBody += "fun main() <noret> { main_undefined(); }\n";
        std::ofstream(root / "main.fin", std::ios::binary) << mainBody;
    }
    ~TempBrokenModules() { std::error_code ec; fs::remove_all(root, ec); }
    std::string main() const { return (root / "main.fin").string(); }
    fs::path root;
};

size_t countErrorObjects(const std::string& err) {
    size_t n = 0;
    for (const auto& d : jsonDiagnostics(err))
        if (d.find("\"severity\":\"error\"") != std::string::npos) ++n;
    return n;
}

long summaryErrorCount(const std::string& err) {
    const auto lines = jsonLines(err);
    if (lines.empty()) return -1;
    static const std::regex rx(R"RX("errors":([0-9]+))RX");
    std::smatch m;
    if (!std::regex_search(lines.back(), m, rx)) return -1;
    return std::stol(m[1].str());
}

} // namespace

TEST(Soundness_JsonSchema, TheSummaryCountsDiagnosticsThatModulesReported) {
    // ModuleLoader.cpp:196 gives each module its own DiagnosticEngine, so that a module's
    // diagnostics can point into the module's own source. That engine counts errors into
    // itself and is then destroyed, while everything it printed has already gone to the
    // shared stream -- so the summary undercounts by exactly the number of diagnostics the
    // modules reported. Sweeping 0, 1 and 2 broken modules is what stops a constant from
    // passing this: the gap has to stay at zero as the module count grows.
    for (int broken : {0, 1, 2}) {
        TempBrokenModules p(broken);
        const auto r = runFinc({p.main(), "--diagnostics=json"}, noFinLibs());
        ASSERT_NE(r.exitCode, 0) << "with " << broken << " broken module(s):\n" << r.err;

        const long emitted = (long)countErrorObjects(r.err);
        const long counted = summaryErrorCount(r.err);
        EXPECT_EQ(emitted, counted)
            << "with " << broken << " broken module(s) the summary claims " << counted
            << " errors while " << emitted << " error objects reached the stream. A second "
               "program reading the stream and a second program reading the summary would "
               "disagree about how many errors this build had.\n"
            << r.err;
    }
}

TEST(Soundness_SearchPaths, AnExplicitlyEmptyLibraryFlagOverridesTheEnvironment) {
    // A build tool has to be able to force a hermetic build from a shell that happens to
    // have FIN_LIBS set, so `--fin-libs=` has to mean "no library paths" rather than
    // "unspecified". The two runs differ only in the flag.
    TempPinnedProject p(kAppQuotedImport, kPinnedReturnsInt);

    const auto viaEnv = runFinc({p.app()}, {{"FIN_LIBS", p.libs()}});
    ASSERT_EQ(viaEnv.exitCode, 0)
        << "FIN_LIBS has to reach the library at all, or the override below proves nothing.\n"
        << viaEnv.err;

    const auto overridden = runFinc({"--fin-libs=", p.app()}, {{"FIN_LIBS", p.libs()}});
    EXPECT_NE(overridden.exitCode, 0)
        << "`--fin-libs=` was treated as unspecified rather than as an empty library set, "
           "so FIN_LIBS still reached the library and the build was not hermetic.\n"
        << overridden.err;
}

namespace {

// The rendered snippet is the source line itself, so every byte of the source is a byte
// the compiler writes to a terminal. A control byte is not inert there: CR returns the
// cursor to column 0, so text after it overwrites the diagnostic that was just printed,
// and ESC begins a sequence the terminal executes. Neither is the compiler's to emit.
const char* kInteriorCr =
    "fun main() <noret> { let ; }\rerror: nothing wrong here, move along\n";
const char* kEscapeInSource =
    "fun main() <noret> { \x1b[31m let ; }\n";

// A control byte placed *before* the error it precedes, so that rendering it as anything
// other than one byte shifts the rest of the line out from under the caret.
const char* kControlByteBeforeError = "fun main() <noret> {\x01 let ; }\n";

// Reads the character the last caret points at, off the rendered line directly above it.
// Both lines carry the same gutter, so the caret's offset within its own line indexes
// straight into the snippet and no gutter width has to be assumed. '\0' when the shape
// is not there.
char characterUnderLastCaret(const std::string& err) {
    const size_t caret = err.rfind('^');
    if (caret == std::string::npos) return '\0';
    const size_t caretLineBegin = err.rfind('\n', caret);
    if (caretLineBegin == std::string::npos || caretLineBegin == 0) return '\0';
    const size_t offset = caret - caretLineBegin - 1;
    const size_t snippetBegin = err.rfind('\n', caretLineBegin - 1);
    if (snippetBegin == std::string::npos) return '\0';
    const std::string snippet =
        err.substr(snippetBegin + 1, caretLineBegin - snippetBegin - 1);
    return offset < snippet.size() ? snippet[offset] : '\0';
}

} // namespace

TEST(Soundness_SourceEncoding, AnInteriorCarriageReturnCannotOverwriteARenderedDiagnostic) {
    // `splitLines` strips a CR at the end of a line, which is the CRLF case and is already
    // covered. An interior CR survives that strip, and it is the dangerous one: the source
    // decides what the terminal displays for the rest of the line.
    TempFin f(kInteriorCr, "cr_mid");
    const auto r = runFinc({f.str()}, noFinLibs());
    ASSERT_NE(r.exitCode, 0) << r.err;
    ASSERT_NE(r.err.find("fun main()"), std::string::npos)
        << "the snippet has to be rendered at all, or this test proves nothing.\n" << r.err;

    EXPECT_EQ(r.err.find('\r'), std::string::npos)
        << "a carriage return from the source reached the rendered diagnostic. Everything "
           "the source wrote after it would overwrite the line the compiler printed, so a "
           "file can choose what its own diagnostics appear to say.\n"
        << r.err;
}

TEST(Soundness_SourceEncoding, AnEscapeByteFromSourceCannotReachTheTerminal) {
    // NO_COLOR is set so the compiler emits no escape of its own: any ESC on the stream
    // came out of the source file.
    TempFin f(kEscapeInSource, "esc");
    const auto r = runFinc({f.str()}, {{"FIN_LIBS", kUnset}, {"NO_COLOR", "1"}});
    ASSERT_NE(r.exitCode, 0) << r.err;
    ASSERT_NE(r.err.find("fun main()"), std::string::npos)
        << "the snippet has to be rendered at all, or this test proves nothing.\n" << r.err;

    EXPECT_EQ(r.err.find('\x1b'), std::string::npos)
        << "an escape byte from the source reached the rendered diagnostic, so a .fin file "
           "can run terminal control sequences on whoever compiles it.\n"
        << r.err;
}

TEST(Soundness_SourceEncoding, TheCaretStillPointsAtTheCharacterItIsAbout) {
    // The caret is written as `column - 1` spaces (`printContext`), so it does not move
    // with the line's content -- which makes the real risk the opposite one: rendering a
    // control byte as more or fewer than one byte shifts the line out from under a caret
    // that stayed where it was. Reading the character from under the caret is what catches
    // that. Asserting the caret's column cannot, because that column is where it came
    // from: an earlier version of this test did exactly that and a mutant that deleted the
    // byte outright still passed it.
    TempFin f(kControlByteBeforeError, "caret_shift");
    const auto r = runFinc({f.str()}, {{"FIN_LIBS", kUnset}, {"NO_COLOR", "1"}});
    ASSERT_NE(r.exitCode, 0) << r.err;

    EXPECT_EQ(characterUnderLastCaret(r.err), ';')
        << "the last diagnostic is about the `;`, so the caret has to land on it. The line "
           "above the caret is the rendered source, so a control byte earlier in that line "
           "rendered as anything but one byte leaves the caret marking the wrong character.\n"
        << r.err;
}

TEST(Soundness_SourceEncoding, TheUnrecognisedByteMessageNamesTheByteItRejected) {
    // `reportLexerError` builds a std::string from a `const char*`, which ends at a NUL --
    // so the one byte whose name matters most was reported as ''. The ESC case is the
    // control: it shows the naming works, which is what makes the NUL case a defect rather
    // than a missing feature.
    TempFin esc(kEscapeInSource, "name_esc");
    const auto e = runFinc({esc.str()}, {{"FIN_LIBS", kUnset}, {"NO_COLOR", "1"}});
    ASSERT_NE(e.err.find("unrecognised byte in source: '\\x1b'"), std::string::npos)
        << "the control case has to name its byte, or this test proves nothing.\n" << e.err;

    TempFin nul(std::string("fun main() <noret> { ") + kNul + " let ; }\n", "name_nul");
    const auto r = runFinc({nul.str()}, {{"FIN_LIBS", kUnset}, {"NO_COLOR", "1"}});
    EXPECT_NE(r.err.find("unrecognised byte in source: '\\x00'"), std::string::npos)
        << "a rejected NUL byte was not named in the message that rejected it.\n" << r.err;
}

namespace {

// A module beside its importer, whose readability the test controls.
struct TempReadableModule {
    TempReadableModule(const std::string& moduleBody) {
        root = uniqueTempPath("fin_unread", "_proj");
        std::error_code ec;
        fs::create_directories(root, ec);
        std::ofstream(root / "m.fin", std::ios::binary) << moduleBody;
        std::ofstream(root / "main.fin", std::ios::binary)
            << "import { exported } from \"m\";\n"
               "fun main() <noret> { let x <int> = exported(); }\n";
    }
    ~TempReadableModule() {
        std::error_code ec;
        fs::permissions(root / "m.fin", fs::perms::owner_all, ec);  // so remove_all can
        fs::remove_all(root, ec);
    }
    bool makeUnreadable() {
        std::error_code ec;
        fs::permissions(root / "m.fin", fs::perms::none, ec);
        if (ec) return false;
        std::ifstream probe(root / "m.fin");
        return !probe.is_open();   // false when running as a user permissions cannot stop
    }
    std::string main() const { return (root / "main.fin").string(); }
    std::string module() const { return (root / "m.fin").string(); }
    fs::path root;
};

} // namespace

TEST(Soundness_ModuleDiagnostics, AnUnreadableModuleIsNotReportedAsAMissingExport) {
    // `ModuleLoader::readFile` returned "" for a file it could not read, which is exactly
    // what it returns for a file that is empty -- so a module the compiler was not allowed
    // to open was reported as a module that does not export the name. The reader is then
    // sent looking for a typo in an export list they cannot see. The driver already tells
    // these apart for the file named on the command line, which is what makes this a defect
    // and not a missing feature: the two runs below must not produce the same diagnostic.
    TempReadableModule empty("");
    const auto onEmpty = runFinc({empty.main()}, noFinLibs());
    ASSERT_NE(onEmpty.exitCode, 0) << onEmpty.err;
    ASSERT_NE(onEmpty.err.find("does not export"), std::string::npos)
        << "an empty module is the case that genuinely has no exports, and it has to say so, "
           "or this test cannot tell the two apart.\n"
        << onEmpty.err;

    TempReadableModule unreadable("fun exported() <int> { return 1; }\n");
    if (!unreadable.makeUnreadable())
        GTEST_SKIP() << "cannot make a file unreadable as this user";

    const auto r = runFinc({unreadable.main()}, noFinLibs());
    EXPECT_NE(r.exitCode, 0) << r.err;
    EXPECT_EQ(r.err.find("does not export"), std::string::npos)
        << "a module that could not be read was reported as a module that does not export "
           "the name, which is what an empty module reports -- so the one thing the reader "
           "needs to know, that the file was never opened, is the one thing not said.\n"
        << r.err;
}

namespace {

// A broken module and a main that imports it under whatever spellings are given.
// The point of the shape is that the module is one file no matter how many ways it
// is named, so the diagnostics about it must not depend on how many names are used.
struct TempAliasedModule {
    TempAliasedModule(const std::vector<std::string>& spellings,
                      const std::string& moduleName = "m") {
        root = uniqueTempPath("fin_alias", "_proj");
        std::error_code ec;
        fs::create_directories(root / "sub", ec);
        std::ofstream(root / (moduleName + ".fin"), std::ios::binary)
            << "fun helper() <int> { return 1; }\n"
               "fun broken_in_module() <int> { let ; }\n";
        std::ofstream main(root / "main.fin", std::ios::binary);
        for (const auto& s : spellings)
            main << "import { helper } from \"" << s << "\";\n";
        main << "fun main() <noret> { let x <int> = helper(); }\n";
    }
    ~TempAliasedModule() { std::error_code ec; fs::remove_all(root, ec); }
    std::string main() const { return (root / "main.fin").string(); }
    bool linkAs(const std::string& linkName, const std::string& target) {
        std::error_code ec;
        fs::create_symlink(target, root / linkName, ec);
        return !ec;
    }
    fs::path root;
};

// Diagnostics attributed to any file other than the importing one -- that is, the
// ones about the module. Identified by exclusion rather than by naming the module,
// because the whole question under test is how many spellings one module arrives
// under, so no single spelling can be assumed to catch it.
size_t countDiagnosticsNotAbout(const std::string& err, const std::string& importerNeedle) {
    size_t n = 0;
    for (const auto& d : jsonDiagnostics(err)) {
        const size_t f = d.find("\"file\":\"");
        if (f == std::string::npos) continue;
        const size_t begin = f + 8;
        const size_t end = d.find('"', begin);
        if (end == std::string::npos) continue;
        if (d.substr(begin, end - begin).find(importerNeedle) == std::string::npos) ++n;
    }
    return n;
}

} // namespace

TEST(Soundness_ModuleIdentity, OneModuleImportedUnderTwoSpellingsIsStillOneModule) {
    // `resolvePath` concatenated the base directory onto the import text and returned
    // the result unnormalised, so `"m"` became `./m.fin` and `"./m"` became `././m.fin`.
    // Those are two keys in `moduleCache` for one file on disk, so the module was parsed
    // twice and every diagnostic in it was reported twice -- under two different `file`
    // values, which also splits one file into two for anything grouping by file.
    //
    // Asserted against the compiler's own behaviour on the single-import case rather than
    // against a fixed count: importing a module a second time under another spelling must
    // not change what the compiler says about that module at all.
    TempAliasedModule once({"m"});
    const auto baseline = runFinc({once.main(), "--diagnostics=json"}, noFinLibs());
    const size_t expected = countDiagnosticsNotAbout(baseline.err, "main.fin");
    ASSERT_GT(expected, 0u) << "the module has an error in it, so it must be diagnosed at "
                               "least once, or this test compares nothing\n" << baseline.err;

    TempAliasedModule twice({"m", "./m"});
    const auto r = runFinc({twice.main(), "--diagnostics=json"}, noFinLibs());
    EXPECT_EQ(countDiagnosticsNotAbout(r.err, "main.fin"), expected)
        << "naming one module two ways doubled its diagnostics: the cache is keyed on the "
           "unnormalised path text, so two spellings are two modules.\n" << r.err;
}

TEST(Soundness_ModuleIdentity, AModuleReachedThroughASymlinkIsNotASecondModule) {
    // Split from the test above deliberately. Normalising the path lexically fixes `./m`
    // versus `m` and leaves this case untouched, so one plausible fix for that defect
    // would leave this one green. A symlink is a second name for one file, and parsing
    // the file twice defines its symbols twice regardless of which name was used.
    TempAliasedModule once({"real"}, "real");
    const auto baseline = runFinc({once.main(), "--diagnostics=json"}, noFinLibs());
    const size_t expected = countDiagnosticsNotAbout(baseline.err, "main.fin");
    ASSERT_GT(expected, 0u) << baseline.err;

    TempAliasedModule twice({"real", "alias"}, "real");
    if (!twice.linkAs("alias.fin", "real.fin"))
        GTEST_SKIP() << "cannot create a symlink here";

    const auto r = runFinc({twice.main(), "--diagnostics=json"}, noFinLibs());
    EXPECT_EQ(countDiagnosticsNotAbout(r.err, "main.fin"), expected)
        << "the module was parsed once through its real name and once through the symlink, "
           "so one file on disk became two modules.\n" << r.err;
}

namespace {

struct Misattributed {
    std::string sample;
    long line = 0;
    std::string message;
};

// Diagnostics the compiler attributes to a line that is a `//@` expectation comment
// rather than program text. A comment cannot contain a semantic error, so every hit is
// a diagnostic whose location is wrong -- and pointing at a `//@` line is the worst
// possible wrong answer, because those lines are this suite's own expectations.
//
// Only diagnostics whose `file` is the sample being compiled are considered. A
// diagnostic about an imported module carries that module's path and its own line
// numbering, so checking it against the sample's line 1 would invent misattributions.
// The census, plus how many diagnostics reached the `//@` test at all. The count is
// not decoration: once the misattribution is fixed, "the census is empty" is the
// assertion, and an empty census is exactly what a broken detector also returns. The
// count is the difference between the two.
struct AttributionCensus {
    std::vector<Misattributed> misattributed;
    size_t considered = 0;
};

AttributionCensus diagnosticsOnExpectationLines() {
    AttributionCensus out;
    for (const auto& path : sampleFiles()) {
        std::vector<std::string> lines;
        {
            std::istringstream src(readWholeFile(path));
            for (std::string l; std::getline(src, l); ) lines.push_back(l);
        }
        const auto r = runFinc({path, "--diagnostics=json"});
        for (const auto& d : jsonDiagnostics(r.err)) {
            std::smatch m;
            if (!std::regex_search(d, m, std::regex(R"RX("file":"([^"]*)","line":([0-9]+))RX")))
                continue;
            if (m[1].str() != path) continue;
            const long line = std::stol(m[2].str());
            if (line < 1 || (size_t)line > lines.size()) continue;
            std::string text = lines[(size_t)line - 1];
            text.erase(0, text.find_first_not_of(" \t"));
            ++out.considered;
            if (text.rfind("//@", 0) != 0) continue;
            std::smatch msg;
            out.misattributed.push_back(Misattributed{
                path, line,
                std::regex_search(d, msg, std::regex(R"RX("message":"([^"]*)")RX"))
                    ? msg[1].str() : std::string("<unparsed>")});
        }
    }
    return out;
}

std::string censusByFile(const std::vector<Misattributed>& all) {
    std::map<std::string, size_t> perFile;
    for (const auto& m : all) ++perFile[m.sample];
    std::string out;
    for (const auto& kv : perFile)
        out += "    " + kv.first + ": " + std::to_string(kv.second) + "\n";
    return out;
}

} // namespace

TEST(Soundness_DiagnosticAttribution, NoDiagnosticPointsAtAnExpectationComment) {
    // A `//@` comment cannot contain a semantic error, so a diagnostic attributed to one
    // is a diagnostic whose location is wrong -- and it is the worst wrong answer
    // available, because those lines are this suite's own expectations. The compiler used
    // to produce 430 of them across 21 samples: any type that failed to resolve was
    // reported at 1:1, because eight of `base_type`'s fifteen productions in parser.y
    // never called setLoc, and line 1 of every sample is its `//@` label. `Undefined type
    // 'Any'` in stdlib/operators.fin was reported at 1:1 while `Any` is written at 6:15.
    //
    // This was KnownDefect_DiagnosticAttribution.DiagnosticsAreAttributedToExpectationComments,
    // asserting the census was non-empty, until the locations landed: 430 -> 5 -> 0. The
    // five were two further causes the first fix missed -- `new`-expression types and
    // top-level function declarations -- found by re-running this census, not by reading
    // the grammar again, which is the argument for keeping the census corpus-wide rather
    // than replacing it with per-production unit tests. Those live in
    // Soundness_DiagnosticLocation in test_soundness.cpp; this one holds the whole corpus.
    const auto census = diagnosticsOnExpectationLines();

    // Two guards, because "the census is empty" is also what a detector that finds
    // nothing returns, and a vacuous pass here would hide a return of the defect.
    //
    // First: the detector reached the `//@` test at all. This counts every diagnostic the
    // corpus emits about the file being compiled -- 322 when this floor was first set,
    // and 98 the day positional member access landed -- so it falls as the compiler
    // improves. When it approaches the floor, lower the floor deliberately and say so
    // here; do not delete the check, and do not let it reach zero unnoticed. Proven to
    // bind: pointing the detector at a path that does not exist takes `considered` to 0
    // and fails here, which is the shape a silently broken detector has.
    //
    // 100 -> 50 for that reason: the corpus is down to 98 located diagnostics about
    // itself, and the floor was one unit away from failing for the right reason.
    EXPECT_GT(census.considered, 50u)
        << "the corpus emitted almost no located diagnostics about its own files, so the "
           "assertion below would pass without measuring anything. Fix the detector (or "
           "lower this floor on purpose) before trusting an empty census.";

    // Second: line 1 of every sample is still a `//@` expectation, so the historical
    // failure mode -- everything rendered at 1:1 -- would still be caught. If samples
    // ever stop carrying their expectation on line 1, this test stops being a net and
    // needs rewriting rather than quietly passing.
    for (const auto& path : sampleFiles()) {
        std::istringstream src(readWholeFile(path));
        std::string first;
        std::getline(src, first);
        first.erase(0, first.find_first_not_of(" \t"));
        ASSERT_EQ(first.rfind("//@", 0), 0u)
            << path << " does not carry its expectation on line 1, so a diagnostic "
                       "misattributed to 1:1 would no longer be detected here.";
    }

    // And the over-detection guard from the KnownDefect era, kept as-is: a run in which
    // every sample is dirty means the line lookup is broken, not that the corpus rotted.
    std::set<std::string> dirty;
    for (const auto& m : census.misattributed) dirty.insert(m.sample);
    ASSERT_LT(dirty.size(), sampleFiles().size())
        << "every single sample reported a misattributed diagnostic, which means this "
           "detector is miscounting rather than detecting.";

    EXPECT_TRUE(census.misattributed.empty())
        << census.misattributed.size() << " diagnostics were attributed to `//@` "
           "expectation lines across " << dirty.size() << " samples, out of "
        << census.considered << " examined. A node reached the analyzer without a "
           "location: find the production in parser.y that built it and call setLoc, "
           "then add the case to Soundness_DiagnosticLocation.\n"
        << censusByFile(census.misattributed);
}

namespace {

// A root file with one error of its own, plus a module. Whether that module imports the
// root back is the variable; everything else is held constant, so the two runs differ
// only in whether the root file participates in a cycle.
struct TempRootCycleProject {
    explicit TempRootCycleProject(bool moduleImportsRootBack) {
        root = uniqueTempPath("fin_rootcyc", "_proj");
        std::error_code ec;
        fs::create_directories(root, ec);
        std::ofstream(root / "main.fin", std::ios::binary)
            << "import { b } from \"b\";\n"
               "fun a() <int> { return 1; }\n"
               "fun uses_undefined() <int> { return absent_from_every_scope; }\n"
               "fun main() <noret> { let x <int> = b(); }\n";
        std::ofstream m(root / "b.fin", std::ios::binary);
        if (moduleImportsRootBack) m << "import { a } from \"main\";\n";
        m << "fun b() <int> { return 2; }\n";
    }
    ~TempRootCycleProject() { std::error_code ec; fs::remove_all(root, ec); }
    std::string main() const { return (root / "main.fin").string(); }
    fs::path root;
};

// How many times one exact message is emitted at one exact position in one exact file.
size_t timesEmittedAt(const std::string& err, const std::string& messageNeedle) {
    std::set<std::string> positions;
    size_t hits = 0;
    for (const auto& d : jsonDiagnostics(err)) {
        if (d.find(messageNeedle) == std::string::npos) continue;
        std::smatch m;
        if (!std::regex_search(
                d, m, std::regex(R"RX("file":"([^"]*)","line":([0-9]+),"column":([0-9]+))RX")))
            continue;
        ++hits;
        positions.insert(m[1].str() + ":" + m[2].str() + ":" + m[3].str());
    }
    // Collapsing by position first: what matters is one message arriving twice for one
    // spot in one file, not the same message legitimately arising in two places.
    return positions.size() == 1 ? hits : 0;
}

} // namespace

TEST(Soundness_ModuleIdentity, TheRootFileIsNotAnalysedTwiceWhenItIsInAnImportCycle) {
    // The file named on the command line is compiled by the driver, not by `loadModule`,
    // so it never went onto the loading stack. An import of it from inside one of its own
    // imports therefore looked like a fresh module: the root file was parsed, expanded and
    // analysed a second time, and every diagnostic in it was reported twice at the same
    // line and column. Measured at 11 diagnostics emitted for 8 distinct ones.
    //
    // The cycle is reported either way -- that part worked. What did not is that being in
    // a cycle changed how many times the root's own errors were printed, and a consumer
    // reading the stream sees one mistake as two.
    //
    // Asserted against the compiler's own acyclic behaviour rather than against "no
    // duplicates anywhere": a node whose parser production never called setLoc still
    // renders at 1:1, and there are productions like that left -- the corpus no longer
    // reaches any of them (Soundness_DiagnosticAttribution) but the compiler is not free
    // of them. So two genuinely distinct errors can still share a position, and a blanket
    // no-duplicates rule would convict the wrong defect.
    TempRootCycleProject acyclic(false);
    const auto baseline = runFinc({acyclic.main(), "--diagnostics=json"}, noFinLibs());
    const size_t expected = timesEmittedAt(baseline.err, "absent_from_every_scope");
    ASSERT_EQ(expected, 1u)
        << "the root's own error must be reported exactly once when nothing is cyclic, or "
           "this test has no baseline to compare against\n" << baseline.err;

    TempRootCycleProject cyclic(true);
    const auto r = runFinc({cyclic.main(), "--diagnostics=json"}, noFinLibs());
    EXPECT_EQ(timesEmittedAt(r.err, "absent_from_every_scope"), expected)
        << "a module importing the root file back caused the root to be loaded again as a "
           "module, so its own diagnostics were emitted twice at the same position.\n"
        << r.err;
}

// ---------------------------------------------------------------------------
// No sample may terminate the compiler by signal.
//
// The machine contract defines four exit codes and no fifth (ADR 0009). A
// segfault is not one of them, and it is not a rejection either: a compiler
// killed mid-analysis has not decided anything about the program it was given,
// so a build system reading only the status would treat the crash as "this file
// has errors" and a `//@ unimplemented` expectation would treat it as
// "unimplemented, as documented". Both readings are wrong in the same
// direction, and both hide it.
//
// This sweep exists alongside the per-sample check in test_expectations.cpp
// because the per-sample runner invokes finc exactly one way -- no FIN_LIBS --
// and four further samples crash only when the standard library is resolvable,
// which is the configuration a real user compiles in. A contract that holds in
// the harness's own invocation and nowhere else is not a contract.
// ---------------------------------------------------------------------------
namespace {

// `runFinc` shells out through `std::system`, so a child killed by a signal is
// reported by the shell as `128 + signal` and cannot be told apart from a
// process that genuinely returned that number. It does not need to be: the
// contract admits four codes, so anything else is a violation whichever way it
// arose, and the number still says which signal when it was one.
std::string describeExit(int code) {
    if (code >= 0 && code <= 3) return "";
    if (code >= 128) {
        return "killed by signal " + std::to_string(code - 128)
             + (code == 139 ? " (SIGSEGV)" : code == 134 ? " (SIGABRT)" : "");
    }
    return "exited " + std::to_string(code) + ", which the contract does not define";
}

std::string sweepExitCodes(const std::vector<std::pair<std::string, std::string>>& env) {
    std::string offenders;
    for (const auto& path : sampleFiles()) {
        const FincRun run = runFinc({path, "--color=never"}, env);
        const std::string bad = describeExit(run.exitCode);
        if (!bad.empty()) offenders += "  " + path + ": " + bad + "\n";
    }
    return offenders;
}

} // namespace

TEST(Soundness_MachineContract, NoSampleTerminatesTheCompilerBySignal) {
    // As the corpus runner invokes it.
    const std::string bare = sweepExitCodes({{"FIN_LIBS", "\x01unset"}});
    EXPECT_TRUE(bare.empty())
        << "these samples did not exit with one of the four documented codes (ADR 0009):\n"
        << bare
        << "A crash is never a diagnosis. Whatever the sample's `//@` expectation says, the "
           "compiler must reach a verdict and report it.";

    // As a user compiles, with the standard library resolvable. Swept separately
    // because a module that loads changes which analysis runs, so this
    // configuration reaches code the bare one never does.
    const std::string withLibs = sweepExitCodes({{"FIN_LIBS", samplesDir() + "/stdlib"}});
    EXPECT_TRUE(withLibs.empty())
        << "these samples did not exit with one of the four documented codes (ADR 0009) when "
           "the standard library was resolvable:\n"
        << withLibs
        << "The per-sample corpus runner does not set FIN_LIBS, so it cannot see these.";
}
