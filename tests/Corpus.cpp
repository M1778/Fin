#include "Corpus.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <process.h>
#define FIN_GETPID _getpid
#else
#include <sys/wait.h>
#include <unistd.h>
#define FIN_GETPID getpid
#endif

#ifndef FIN_TESTS_DIR
#error "FIN_TESTS_DIR must be defined by the build; the harness must not depend on the working directory"
#endif
#ifndef FIN_SAMPLES_DIR
#error "FIN_SAMPLES_DIR must be defined by the build"
#endif
#ifndef FINC_BINARY
#error "FINC_BINARY must be defined by the build"
#endif

namespace fs = std::filesystem;

namespace fin::testing {

std::string testsDir()   { return FIN_TESTS_DIR; }
std::string samplesDir() { return FIN_SAMPLES_DIR; }
std::string fincBinary() { return FINC_BINARY; }

std::string readWholeFile(const std::string& path) {
    std::ifstream t(path, std::ios::binary);
    if (!t.is_open()) return "";
    std::stringstream buffer;
    buffer << t.rdbuf();
    return buffer.str();
}

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Reads a double-quoted string starting at `i`, honouring backslash escapes.
bool readQuoted(const std::string& s, size_t i, std::string& out, size_t& end) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        if (s[i] == '\\' && i + 1 < s.size()) { out += s[i + 1]; i += 2; continue; }
        if (s[i] == '"') { end = i + 1; return true; }
        out += s[i++];
    }
    return false;
}

} // namespace

SampleAnnotation parseAnnotation(const std::string& path, const std::string& source) {
    SampleAnnotation a;
    a.path = path;

    std::istringstream in(source);
    std::string line;
    int lineNo = 0;
    bool sawLabel = false;

    while (std::getline(in, line)) {
        ++lineNo;
        std::string t = trim(line);
        if (t.rfind("//@", 0) != 0) continue;

        std::string rest = trim(t.substr(3));
        if (rest.empty()) {
            a.error = "empty //@ directive on line " + std::to_string(lineNo);
            return a;
        }

        // The label.
        if (rest == "normative" || rest == "aspirational") {
            if (sawLabel) {
                a.error = "a second authority label on line " + std::to_string(lineNo);
                return a;
            }
            sawLabel = true;
            a.authority = rest == "normative" ? Authority::Normative : Authority::Aspirational;
            continue;
        }

        Expectation e;
        e.sourceLine = lineNo;

        if (rest == "ok") {
            e.kind = ExpectationKind::Ok;
            a.expectations.push_back(e);
            continue;
        }

        if (rest.rfind("unimplemented", 0) == 0) {
            std::string tail = trim(rest.substr(std::string("unimplemented").size()));
            std::string reason;
            size_t end = 0;
            if (!readQuoted(tail, 0, reason, end)) {
                a.error = "line " + std::to_string(lineNo) +
                          ": `unimplemented` needs a quoted reason";
                return a;
            }
            e.kind = ExpectationKind::Unimplemented;
            e.text = reason;
            a.expectations.push_back(e);
            continue;
        }

        if (rest.rfind("error", 0) == 0) {
            std::string tail = trim(rest.substr(std::string("error").size()));
            size_t colon = tail.find(':');
            size_t space = tail.find_first_of(" \t");
            if (colon == std::string::npos || space == std::string::npos || colon > space) {
                a.error = "line " + std::to_string(lineNo) +
                          ": `error` needs <line>:<col> \"<msg>\"";
                return a;
            }
            try {
                e.line = std::stoi(tail.substr(0, colon));
                e.column = std::stoi(tail.substr(colon + 1, space - colon - 1));
            } catch (...) {
                a.error = "line " + std::to_string(lineNo) + ": `error` position is not numeric";
                return a;
            }
            std::string msg;
            size_t end = 0;
            std::string q = trim(tail.substr(space));
            if (!readQuoted(q, 0, msg, end)) {
                a.error = "line " + std::to_string(lineNo) +
                          ": `error` needs a quoted message";
                return a;
            }
            e.kind = ExpectationKind::Error;
            e.text = msg;
            a.expectations.push_back(e);
            continue;
        }

        // An unrecognised directive is loud. A typo in an expectation must not
        // be the same thing as an absent expectation.
        a.error = "line " + std::to_string(lineNo) + ": unrecognised //@ directive `" + rest +
                  "` (expected `normative`, `aspirational`, `ok`, "
                  "`error <line>:<col> \"<msg>\"` or `unimplemented \"<reason>\"`)";
        return a;
    }

    if (a.expectations.empty()) {
        a.error = "no //@ expectation. A sample that says nothing about what should happen "
                  "is indistinguishable from an untested one (ADR 0008).";
        return a;
    }

    bool hasOk = false, hasFail = false;
    for (const auto& e : a.expectations) {
        if (e.kind == ExpectationKind::Ok) hasOk = true;
        else hasFail = true;
    }
    if (hasOk && hasFail) {
        a.error = "`ok` cannot be combined with `error` or `unimplemented`";
        return a;
    }
    if (hasOk && a.expectations.size() > 1) {
        a.error = "more than one `ok` expectation";
        return a;
    }

    return a;
}

const std::vector<std::string>& scratchFinFiles() {
    // Ruled on rather than left ambiguous: `addits/plor.fin` is scratch, not a
    // sample. It is referenced by nothing in the tree, it is four lines long,
    // line 3 has no terminator, and its `<fn() int>` type form appears nowhere
    // else in the corpus. Excluding it by name means a future .fin file dropped
    // outside tests/samples fails the discovery test instead of being silently
    // ignored.
    static const std::vector<std::string> v = { "addits/plor.fin" };
    return v;
}

std::vector<std::string> allFinFilesInTestTree() {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(testsDir(), ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".fin") continue;
        out.push_back(fs::relative(entry.path(), testsDir()).generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

const std::vector<std::string>& sampleFiles() {
    // Built once, from an absolute compile-time path. The old GetFinFiles()
    // probed "samples" then "tests/samples" relative to the working directory
    // during static initialisation, so a wrong cwd registered zero tests and the
    // suite reported success.
    static const std::vector<std::string> files = [] {
        std::vector<std::string> out;
        std::error_code ec;
        const std::string dir = samplesDir();
        if (!fs::exists(dir, ec)) {
            std::cerr << "\n"
                      << "================================================================\n"
                      << "FATAL: sample corpus not found at " << dir << "\n"
                      << "The expectation runner has nothing to run. This is a harness\n"
                      << "failure, not an empty test suite (ADR 0008).\n"
                      << "================================================================\n";
            std::cerr.flush();
            return out;
        }
        for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".fin") continue;
            out.push_back(entry.path().generic_string());
        }
        std::sort(out.begin(), out.end());
        if (out.empty()) {
            std::cerr << "\n"
                      << "================================================================\n"
                      << "FATAL: zero .fin samples discovered under " << dir << "\n"
                      << "Zero-sample discovery is a hard failure: a suite that registers\n"
                      << "no sample tests otherwise reports success (ADR 0008).\n"
                      << "================================================================\n";
            std::cerr.flush();
        }
        return out;
    }();
    return files;
}

std::string testNameForSample(const std::string& path) {
    std::string name = path;
    const std::string marker = "samples/";
    size_t pos = name.rfind(marker);
    if (pos != std::string::npos) name = name.substr(pos + marker.size());
    if (name.size() > 4 && name.substr(name.size() - 4) == ".fin") {
        name = name.substr(0, name.size() - 4);
    }
    std::replace_if(name.begin(), name.end(),
                    [](char c) { return !std::isalnum((unsigned char)c); }, '_');
    return name;
}

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

namespace {

std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

} // namespace

std::string uniqueTempPath(const std::string& prefix, const std::string& suffix) {
    // Atomic because gtest can be built to run tests on more than one thread,
    // and a torn counter here reintroduces exactly the collision this function
    // exists to remove.
    static std::atomic<int> counter{0};

    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    // A TEST_P's name contains '/' — "MatchesCompilerBehaviour/0" — which would
    // silently turn the file name into a path through a directory that does not
    // exist, so every non-alphanumeric character is folded away. The name is
    // included only so that a file leaked by a crashed test says which test
    // leaked it; uniqueness comes from the pid and the counter.
    std::string test = "notest";
    if (info != nullptr && info->name() != nullptr) {
        test.assign(info->name());
        for (char& c : test) {
            if (std::isalnum(static_cast<unsigned char>(c)) == 0) c = '_';
        }
    }

    const std::string name = prefix + "_" + test + "_" + std::to_string(FIN_GETPID()) +
                             "_" + std::to_string(counter.fetch_add(1)) + suffix;
    return (fs::temp_directory_path() / name).string();
}

FincRun runFinc(const std::vector<std::string>& args,
                const std::vector<std::pair<std::string, std::string>>& env) {
    FincRun r;

    fs::path outPath = uniqueTempPath("finc_out");
    fs::path errPath = uniqueTempPath("finc_err");

    std::string cmd;
    // `env -u` so NO_COLOR can be *removed* as well as set: an inherited
    // NO_COLOR would otherwise make the colour test vacuous.
    cmd += "env";
    for (const auto& kv : env) {
        if (kv.second == "\x01unset") cmd += " -u " + kv.first;
        else cmd += " " + kv.first + "=" + shellQuote(kv.second);
    }
    cmd += " " + shellQuote(fincBinary());
    for (const auto& a : args) cmd += " " + shellQuote(a);
    cmd += " > " + shellQuote(outPath.string());
    cmd += " 2> " + shellQuote(errPath.string());

    int status = std::system(cmd.c_str());
#ifdef WIFEXITED
    r.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#else
    r.exitCode = status;
#endif

    r.out = readWholeFile(outPath.string());
    r.err = readWholeFile(errPath.string());

    std::error_code ec;
    fs::remove(outPath, ec);
    fs::remove(errPath, ec);
    return r;
}

}
