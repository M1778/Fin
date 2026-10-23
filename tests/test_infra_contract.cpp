#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "Corpus.hpp"

// Registered for the stdlib/infra wave lanes (issues #51/#56+#58/#81+#87+#88).
// Each upstream lane adds its own TESTs here.

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

// A temporary .fin file, so a contract test never depends on a sample.
class TempFin {
public:
    explicit TempFin(const std::string& contents, const std::string& tag = "t") {
        path_ = uniqueTempPath("fin_infra_" + tag, ".fin");
        std::ofstream f(path_, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    ~TempFin() { std::error_code ec; fs::remove(path_, ec); }
    std::string str() const { return path_.string(); }

private:
    fs::path path_;
};

// --diagnostics=json is one JSON object per stderr line, so every assertion
// below runs per line rather than over the stream.
std::vector<std::string> splitLines(const std::string& s) {
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

// A documented key is present, whatever value it carries. Substring rather
// than a JSON parser: this is a conformance threshold, not a reimplementation
// of the consumer — the full parse stays `finn`'s job (issue #87).
bool hasKey(const std::string& line, const char* key) {
    return line.find("\"" + std::string(key) + "\":") != std::string::npos;
}

std::string valueOf(const std::string& line, const char* key) {
    std::string needle = "\"" + std::string(key) + "\":\"";
    size_t v = line.find(needle);
    if (v == std::string::npos) return "";
    size_t start = v + needle.size();
    size_t end = line.find('"', start);
    if (end == std::string::npos) return "";
    return line.substr(start, end - start);
}

const char* const kDiagnosticKeys[] = {
    "kind", "severity", "code", "message", "file", "line", "column",
    "endLine", "endColumn", "help", "attribution",
};
const char* const kSummaryKeys[] = { "kind", "errors", "warnings", "exitCode", "status" };

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

} // namespace

// --- Conformance (docs/finc-interface-contract.md, contract 3) --------------

TEST(ContractConformance, ExitCodesMatchTheDocumentedTable) {
    // 0 = compiled, zero diagnostics; 1 = rejected source; 2 = bad command
    // line or unreadable input. Exit 3 (a compiler failure carrying its own
    // diagnostic) is not reachable from ordinary argv today; the contract doc
    // verifies that path by injection rather than by a CI run, so the table
    // tests the codes a consumer can rely on without provoking a bug.

    // 0: compiled, zero diagnostics
    TempFin clean("fun main() <noret> {}\n");
    auto ok = runFinc({clean.str(), "--fin-libs=", "--diagnostics=json"});
    ASSERT_EQ(ok.exitCode, 0) << ok.err;
    auto okLines = splitLines(ok.err);
    ASSERT_EQ(okLines.size(), 1u) << ok.err;
    EXPECT_NE(okLines.back().find("\"exitCode\":0"), std::string::npos) << okLines.back();
    EXPECT_NE(okLines.back().find("\"status\":\"ok\""), std::string::npos) << okLines.back();

    // 1: rejected source
    TempFin bad("fun main() <noret> { let ; }\n");
    auto rejected = runFinc({bad.str(), "--fin-libs=", "--diagnostics=json"});
    ASSERT_EQ(rejected.exitCode, 1) << rejected.err;
    EXPECT_NE(splitLines(rejected.err).back().find("\"exitCode\":1"), std::string::npos)
        << rejected.err;

    // 2: a bad command line, and input that could not be read
    auto flag = runFinc({clean.str(), "--fin-libs=", "--diagnostics=json", "--no-such-flag"});
    ASSERT_EQ(flag.exitCode, 2) << flag.err;
    EXPECT_NE(splitLines(flag.err).back().find("\"exitCode\":2"), std::string::npos)
        << flag.err;

    auto unreadable =
        runFinc({"/nonexistent/fin-no-such-file.fin", "--fin-libs=", "--diagnostics=json"});
    ASSERT_EQ(unreadable.exitCode, 2) << unreadable.err;
    auto unreadableLines = splitLines(unreadable.err);
    ASSERT_GE(unreadableLines.size(), 2u) << unreadable.err;
    EXPECT_NE(unreadableLines.front().find("\"file\":null"), std::string::npos)
        << "an invocation error is about the invocation, not a place in a file: "
        << unreadableLines.front();
    EXPECT_NE(unreadableLines.front().find("\"line\":0"), std::string::npos)
        << unreadableLines.front();
}

TEST(ContractConformance, EveryDiagnosticCarriesTheDocumentedSchema) {
    // One error and, in real runs, a warning or note, so the stream is not a
    // single shape. If a key below goes missing, `finn`'s parser breaks while
    // both suites still pass — that is the drift this file exists to catch.
    TempFin bad("fun main() <noret> { let ; }\n");
    auto r = runFinc({bad.str(), "--fin-libs=", "--diagnostics=json"});
    ASSERT_EQ(r.out, "") << "stdout is reserved on every contract path";
    ASSERT_EQ(r.exitCode, 1) << r.err;

    auto lines = splitLines(r.err);
    ASSERT_GE(lines.size(), 2u) << r.err;
    for (size_t i = 0; i + 1 < lines.size(); ++i) {
        const std::string& line = lines[i];
        EXPECT_EQ(line.front(), '{') << line;
        EXPECT_EQ(line.back(), '}') << line;
        EXPECT_EQ(valueOf(line, "kind"), "diagnostic") << line;
        for (const char* key : kDiagnosticKeys) {
            EXPECT_TRUE(hasKey(line, key)) << "missing key `" << key << "` in " << line;
        }
        const std::string severity = valueOf(line, "severity");
        EXPECT_TRUE(severity == "error" || severity == "warning" || severity == "note")
            << "severity `" << severity << "` is outside the documented set: " << line;
        // Reserved for wave 4: present, and null for every diagnostic finc
        // raises on its own, which today is all of them.
        EXPECT_NE(line.find("\"attribution\":null"), std::string::npos) << line;
    }
    const std::string& summary = lines.back();
    EXPECT_EQ(valueOf(summary, "kind"), "summary") << summary;
    for (const char* key : kSummaryKeys) {
        EXPECT_TRUE(hasKey(summary, key)) << "missing key `" << key << "` in " << summary;
    }
}

TEST(ContractConformance, StdoutIsReservedOnEveryContractPath) {
    TempFin clean("fun main() <noret> {}\n");
    TempFin bad("fun main() <noret> { let ; }\n");
    std::vector<std::vector<std::string>> argvs = {
        {clean.str(), "--fin-libs=", "--diagnostics=json"},
        {clean.str(), "--fin-libs=", "--diagnostics=json", "--no-such-flag"},
        {bad.str(), "--fin-libs=", "--diagnostics=json"},
        {"/nonexistent/fin-no-such-file.fin", "--fin-libs=", "--diagnostics=json"},
    };
    for (const auto& argv : argvs) {
        std::string joined;
        for (const auto& a : argv) joined += " " + a;
        auto r = runFinc(argv);
        EXPECT_EQ(r.out, "") << "stdout leaked on:" << joined;
    }
}

TEST(ContractConformance, ExecutableLinkWithJsonDiagnosticsIsRefused) {
    // The contract-3 point: `-o` (without `-c`) together with `--diagnostics=json`
    // is refused by name with exit 2, and the refusal is JSONL like any other
    // command-line error. Under contract 2 the linked executable's external C
    // driver could write its own output into the one stream a consumer must be
    // able to parse line by line.
    TempFin clean("fun main() <noret> {}\n");
    std::string obj = uniqueTempPath("fin_infra_refusal", ".bin");
    auto r = runFinc({clean.str(), "--fin-libs=", "-o", obj, "--diagnostics=json"});
    ASSERT_EQ(r.exitCode, 2) << r.err;
    auto lines = splitLines(r.err);
    ASSERT_GE(lines.size(), 2u) << r.err;
    const std::string& d = lines.front();
    EXPECT_EQ(valueOf(d, "kind"), "diagnostic") << d;
    EXPECT_NE(d.find("-o"), std::string::npos) << "the refusal names the flag: " << d;
    EXPECT_NE(d.find("--diagnostics=json"), std::string::npos) << d;
    // A refusal builds nothing.
    std::error_code ec;
    EXPECT_FALSE(fs::exists(obj, ec)) << "a refused -o must not leave its artifact";
    fs::remove(obj, ec);
}

// --- Contract corpus (tests/contract-corpus) --------------------------------

// The corpus is the one source both repos share: these tests prove the
// checked-in expectations still describe the real compiler, and `finn` replays
// the same files against its own parser. Absolute machine paths never enter
// the corpus — argv fixtures carry the @DIR@ placeholder for the case
// directory, substituted on the way in and normalized out of the output.

namespace {

struct CorpusCase {
    std::string dir;
    std::string name;
};

std::vector<CorpusCase> corpusCases() {
    const fs::path root = fs::path(testsDir()) / "contract-corpus";
    std::vector<CorpusCase> out;
    if (fs::exists(root)) {
        for (const auto& e : fs::directory_iterator(root)) {
            if (e.is_directory()) {
                out.push_back({e.path().generic_string(),
                               e.path().filename().string()});
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const CorpusCase& a, const CorpusCase& b) {
        return a.name < b.name;
    });
    return out;
}

std::string readFileText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string normalizeCorpusOutput(const std::string& output, const std::string& dir) {
    const std::string separator = R"((/|\\\\))";
    std::string pattern;
    for (size_t i = 0; i < dir.size(); ++i) {
        const unsigned char c = dir[i];
        if (c == '/' || c == '\\') {
            pattern += separator;
        } else if (i == 0 && dir.size() > 1 && dir[1] == ':' && std::isalpha(c)) {
            pattern += '[';
            pattern += static_cast<char>(std::tolower(c));
            pattern += static_cast<char>(std::toupper(c));
            pattern += ']';
        } else {
            if (std::string(R"(\.^$|()[]{}*+?)").find(c) != std::string::npos)
                pattern += '\\';
            pattern += c;
        }
    }
    return std::regex_replace(replaceAll(output, "\r", ""),
                              std::regex("(" + pattern + "|@DIR@)" + separator),
                              "@DIR@/");
}

} // namespace

TEST(ContractCorpus, CasePathsReplayAcrossPlatformSpellings) {
    const std::string expected =
        R"({"file":"@DIR@/input.fin","message":"could not read file: @DIR@/absent.fin","help":"keep \\n and \"quotes\"; D:/other\\file"})" "\n";
    const std::vector<std::pair<std::string, std::string>> spellings = {
        {"/a/Fin/tests/case", "/a/Fin/tests/case/"},
        {"D:/a/Fin/tests/case", R"(D:/a/Fin/tests\\case/)"},
        {R"(D:\a\Fin\tests\case)", R"(d:/a\\Fin/tests\\case\\)"},
        {"d:/a/Fin/tests/case", R"(D:\\a\\Fin\\tests\\case\\)"},
        {"D:/a/Fin/tests/case", R"(@DIR@\\)"},
    };
    for (const auto& [dir, prefix] : spellings) {
        SCOPED_TRACE(prefix);
        const std::string actual = replaceAll(expected, "@DIR@/", prefix);
        EXPECT_EQ(normalizeCorpusOutput(actual, dir), expected);
    }
}

TEST(ContractCorpus, EveryCaseReplaysAgainstTheRealCompiler) {
    auto cases = corpusCases();
    ASSERT_FALSE(cases.empty()) << "no contract-corpus cases under " << testsDir();
    for (const auto& c : cases) {
        fs::path dir(c.dir);
        const std::string argvText = readFileText(dir / "argv");
        ASSERT_FALSE(argvText.empty()) << c.name << ": no argv fixture";
        std::vector<std::string> args;
        for (const auto& arg : splitLines(argvText)) {
            args.push_back(replaceAll(arg, "@DIR@", c.dir));
        }
        EXPECT_FALSE(args.empty()) << c.name << ": empty argv fixture";

        SCOPED_TRACE(c.name);
        auto r = runFinc(args);
        EXPECT_EQ(r.out, "") << "stdout is reserved";
        const std::string actual = normalizeCorpusOutput(r.err, c.dir);
        const std::string expected =
            normalizeCorpusOutput(readFileText(dir / "expected.jsonl"), c.dir);
        EXPECT_EQ(actual, expected)
            << c.name << " diverged from the corpus. If only the message text moved,"
            << " regenerate with tests/tools/contract_corpus_regen.sh; if the schema"
            << " moved, that is a contract change (docs/finc-interface-contract.md).";
        ASSERT_FALSE(splitLines(actual).empty()) << c.name << ": not JSONL";
    }
}

TEST(ContractCorpus, EveryExpectedFileEndsInASummaryCarryingAnExitCode) {
    for (const auto& c : corpusCases()) {
        const std::string expected = readFileText(fs::path(c.dir) / "expected.jsonl");
        ASSERT_FALSE(expected.empty()) << c.name << ": empty expected.jsonl";
        auto lines = splitLines(expected);
        ASSERT_FALSE(lines.empty()) << c.name << ": expected.jsonl has no lines";
        for (const auto& line : lines) {
            EXPECT_EQ(line.front(), '{') << c.name << ": " << line;
            EXPECT_EQ(line.back(), '}') << c.name << ": " << line;
        }
        // The summary is how a consumer tells "finished" from "died before
        // reporting"; every corpus case must carry one.
        EXPECT_NE(lines.back().find("\"kind\":\"summary\""), std::string::npos)
            << c.name << ": corpus run does not end in a summary";
        EXPECT_NE(lines.back().find("\"exitCode\":"), std::string::npos)
            << c.name << ": summary does not carry exitCode";
    }
}

TEST(ContractCorpus, TheCorpusCoversTheDocumentedExitCodes) {
    // 0/1/2 must each have a concrete case so `finn`'s replay sees every shape
    // the table promises; 3 is carriage-by-diagnostic, verified by injection.
    bool has0 = false, has1 = false, has2 = false;
    for (const auto& c : corpusCases()) {
        auto lines = splitLines(readFileText(fs::path(c.dir) / "expected.jsonl"));
        ASSERT_FALSE(lines.empty()) << c.name << ": empty expected.jsonl";
        const std::string& summary = lines.back();
        if (summary.find("\"exitCode\":0") != std::string::npos) has0 = true;
        if (summary.find("\"exitCode\":1") != std::string::npos) has1 = true;
        if (summary.find("\"exitCode\":2") != std::string::npos) has2 = true;
    }
    EXPECT_TRUE(has0) << "the corpus does not cover exit 0";
    EXPECT_TRUE(has1) << "the corpus does not cover exit 1";
    EXPECT_TRUE(has2) << "the corpus does not cover exit 2";
}
