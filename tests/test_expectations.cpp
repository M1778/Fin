#include <gtest/gtest.h>

#include <iostream>
#include <gtest/gtest-spi.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <ostream>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "Corpus.hpp"

namespace fs = std::filesystem;
using namespace fin::testing;

// --- Corpus discovery -------------------------------------------------------
//
// These three tests are the guard the old suite lacked. GetFinFiles() ran during
// static initialisation and probed relative paths, so a wrong working directory
// registered zero parameterised tests and the run reported success.

TEST(CorpusDiscovery, SamplesDirectoryExists) {
    ASSERT_TRUE(fs::exists(samplesDir()))
        << "sample corpus missing at " << samplesDir();
}

TEST(CorpusDiscovery, DiscoveryIsNotEmpty) {
    ASSERT_FALSE(sampleFiles().empty())
        << "zero .fin samples discovered under " << samplesDir()
        << ". Zero-sample discovery is a hard failure: the suite would otherwise "
           "register no sample tests and report success.";
}

TEST(CorpusDiscovery, EveryFinFileInTheTestTreeIsEitherASampleOrExplicitlyScratch) {
    const auto scratch = scratchFinFiles();
    std::set<std::string> allowed(scratch.begin(), scratch.end());

    std::vector<std::string> unaccounted;
    for (const auto& rel : allFinFilesInTestTree()) {
        if (rel.rfind("samples/", 0) == 0) continue;   // a sample
        if (allowed.count(rel)) continue;              // explicitly scratch
        unaccounted.push_back(rel);
    }

    EXPECT_TRUE(unaccounted.empty())
        << "a .fin file in the tests tree is neither a sample nor named in "
           "scratchFinFiles(). Silently globbing it in, or silently leaving it "
           "out, is the ADR 0008 failure. Rule on it: "
        << [&] {
               std::string s;
               for (const auto& u : unaccounted) s += "\n  " + u;
               return s;
           }();

    // The named scratch files must actually exist, so a stale exclusion cannot
    // sit in the list unnoticed.
    for (const auto& rel : scratch) {
        EXPECT_TRUE(fs::exists(fs::path(testsDir()) / rel))
            << "scratchFinFiles() names " << rel << ", which does not exist";
    }
}

TEST(CorpusDiscovery, EverySampleHasLfLineEndings) {
    for (const auto& path : sampleFiles()) {
        std::string src = readWholeFile(path);
        EXPECT_EQ(src.find('\r'), std::string::npos)
            << path << " contains a CR. Column numbers in an `error` expectation "
                       "are only meaningful once line endings are settled.";
    }
}

// --- The expectation runner -------------------------------------------------

namespace {

struct Sample {
    std::string path;
};

// Without this gtest prints "32-byte object <F0-CA ...>" in ctest's test list.
void PrintTo(const Sample& s, std::ostream* os) { *os << s.path; }

std::vector<Sample> samples() {
    std::vector<Sample> out;
    for (const auto& p : sampleFiles()) out.push_back(Sample{p});
    if (out.empty()) {
        // A sentinel so the suite fails loudly instead of registering nothing.
        out.push_back(Sample{"<<NO SAMPLES DISCOVERED>>"});
    }
    return out;
}

// Pulls `file:line:col` out of the human renderer's `--> ` line.
struct Position { int line = 0; int column = 0; };

std::vector<Position> parsePositions(const std::string& stderrText) {
    std::vector<Position> out;
    static const std::regex re(R"(-->\s+\S*?:(\d+):(\d+))");
    auto begin = std::sregex_iterator(stderrText.begin(), stderrText.end(), re);
    for (auto it = begin; it != std::sregex_iterator(); ++it) {
        out.push_back(Position{std::stoi((*it)[1].str()), std::stoi((*it)[2].str())});
    }
    return out;
}

// How many diagnostics were printed. Anchored at the start of a line because the
// source line echoed under a caret can itself contain "error:" -- an unanchored
// count read `stdlib/error.fin`, whose whole subject is error handling, six high.
size_t errorCount(const std::string& stripped) {
    size_t n = 0;
    for (size_t i = 0; i < stripped.size();) {
        size_t eol = stripped.find('\n', i);
        if (eol == std::string::npos) eol = stripped.size();
        if (stripped.compare(i, 7, "error: ") == 0) ++n;
        i = eol + 1;
    }
    return n;
}

// The whole comparison, extracted so the tests below can drive it over a file
// they wrote themselves and prove it fails when it should.
void checkSampleAgainstFinc(const std::string& path) {
    const std::string source = readWholeFile(path);
    ASSERT_FALSE(source.empty() && fs::file_size(path) > 0)
        << "could not read " << path;

    SampleAnnotation ann = parseAnnotation(path, source);

    // An unannotated or malformed sample is a harness failure, never a skip.
    ASSERT_TRUE(ann.error.empty()) << path << ": " << ann.error;
    EXPECT_NE(ann.authority, Authority::Unlabelled)
        << path << " carries no `//@ normative` or `//@ aspirational` label. "
                   "The label is advisory, but every sample states one.";

    FincRun run = runFinc({path, "--color=never"});
    const std::string err = stripAnsi(run.err);

    // stdout is reserved on every path, whatever the outcome.
    EXPECT_EQ(run.out, "")
        << path << " produced output on stdout, which is reserved (ADR 0009):\n" << run.out;

    // The contract is a closed set of four codes (ADR 0009), and this is checked
    // before the expectation is consulted because no `//@` line can license a
    // code outside it. It is checked here, in the per-sample test, and not only
    // in the corpus-wide sweep in test_cli.cpp, so that the sample which breaks
    // the contract is the test that goes red.
    //
    // This assertion is the reason the suite was green while nine samples
    // segfaulted. `runFinc` goes through `std::system`, so a child killed by a
    // signal arrives as the shell's `128 + signal` -- 139 for SIGSEGV -- and the
    // only check on a failing sample's status was `EXPECT_NE(exitCode, 0)`,
    // which 139 satisfies. A crash was therefore indistinguishable from an
    // orderly rejection, and `//@ unimplemented` silently ratified it.
    EXPECT_TRUE(run.exitCode >= 0 && run.exitCode <= 3)
        << path << " exited " << run.exitCode << ", which is not one of the four codes the "
           "machine contract defines: 0 accepted, 1 diagnostics, 2 usage, 3 internal error "
           "(ADR 0009).\n"
        << (run.exitCode >= 128
                ? "A code at or above 128 is a shell reporting `128 + signal`, so the "
                  "compiler was killed rather than having returned: signal "
                      + std::to_string(run.exitCode - 128) + ".\n"
                : "")
        << "stderr:\n" << err;

    // Exit 1 means diagnostics and diagnostics mean exit 1 -- both directions, over
    // every sample. This is the contract ADR 0009 states, and until now nothing
    // checked it: `expectsOk` below asserts exit 0 and separately asserts no
    // diagnostic, but a *failing* sample was only required to be non-zero, so a
    // sample that printed nothing and exited 1, or printed six diagnostics and
    // exited 3, satisfied every assertion in the file.
    //
    // Stated as a biconditional and not as two independent one-way implications
    // because the two failures it catches are opposite mistakes: a pass that sets
    // the failure flag without reporting anything (silent rejection -- the user
    // sees an empty failure), and a pass that reports without setting it (a
    // diagnostic the exit code denies, which is what a build script reads).
    //
    // Exit 2 is reachable in principle -- `finc /tmp/nosuch.fin` prints
    // `error: could not read file` and exits 2 -- but never from here: every sample
    // is a readable file passed as the one positional argument. So 2 is a failure of
    // this assertion rather than an exemption from it, and the message says so.
    const size_t diagnostics = errorCount(err);
    if (diagnostics > 0) {
        EXPECT_EQ(run.exitCode, 1)
            << path << " printed " << diagnostics
            << " diagnostic(s) but exited " << run.exitCode
            << ". A diagnostic means exit 1 (ADR 0009); 2 is reserved for usage errors, "
               "which a readable sample file cannot provoke.\nstderr:\n" << err;
    } else {
        EXPECT_NE(run.exitCode, 1)
            << path << " exited 1 without printing a diagnostic. Exit 1 means "
               "diagnostics were reported (ADR 0009), so this is a silent failure: "
               "whatever rejected the sample set the failure flag without saying why.";
    }

    const bool expectsOk = ann.expectations.front().kind == ExpectationKind::Ok;

    if (expectsOk) {
        EXPECT_EQ(run.exitCode, 0)
            << path << " carries `//@ ok` but finc exited " << run.exitCode
            << ".\nstderr:\n" << err;
        EXPECT_EQ(err.find("error:"), std::string::npos)
            << path << " carries `//@ ok` but finc reported a diagnostic:\n" << err;
        return;
    }

    // Every non-`ok` expectation says the compile must fail. An
    // `unimplemented` expectation whose construct has started working fails
    // here, which is the point: the harness demands attention rather than
    // leaving a stale exception behind (ADR 0008).
    EXPECT_NE(run.exitCode, 0)
        << path << " was expected to fail but finc exited 0.\n"
        << "If the construct now works, promote the expectation to `//@ ok`.\n"
        << "Expectation: " << ann.expectations.front().text;

    for (const auto& e : ann.expectations) {
        if (e.kind != ExpectationKind::Error) continue;
        EXPECT_NE(err.find(e.text), std::string::npos)
            << path << ":" << e.sourceLine << " expected the message \"" << e.text
            << "\"\nstderr:\n" << err;
        bool found = false;
        for (const auto& p : parsePositions(err)) {
            if (p.line == e.line && p.column == e.column) { found = true; break; }
        }
        EXPECT_TRUE(found)
            << path << ":" << e.sourceLine << " expected a diagnostic at " << e.line
            << ":" << e.column << "\nstderr:\n" << err;
    }
}

} // namespace

class SampleExpectation : public ::testing::TestWithParam<Sample> {};

TEST_P(SampleExpectation, MatchesCompilerBehaviour) {
    const std::string path = GetParam().path;
    ASSERT_NE(path, "<<NO SAMPLES DISCOVERED>>")
        << "no samples were discovered under " << samplesDir();
    checkSampleAgainstFinc(path);
}

// --- The runner is not vacuous ---------------------------------------------
//
// Fifty passing tests mean nothing unless the comparison can fail. These drive
// checkSampleAgainstFinc over files the test wrote and intercept the failures it
// produces. They are also nearly all the coverage of the `//@ error` form, which
// exactly one sample uses — `undefined_behavior.fin`. Every other failing sample
// fails because a construct is unbuilt, not because the sample is invalid Fin,
// which is why the corpus is 16 `//@ ok`, 33 `//@ unimplemented` and that one.

namespace {

// A .fin file written by a test, annotated however the test likes.
class TempSample {
public:
    explicit TempSample(const std::string& contents) {
        path_ = uniqueTempPath("fin_exp", ".fin");
        std::ofstream f(path_, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    ~TempSample() { std::error_code ec; fs::remove(path_, ec); }
    std::string str() const { return path_.string(); }

private:
    fs::path path_;
};

struct Captured {
    int count = 0;
    std::string text;
};

// Intercepts the assertion failures a body produces instead of failing the test.
Captured capture(const std::function<void()>& body) {
    ::testing::TestPartResultArray results;
    {
        ::testing::ScopedFakeTestPartResultReporter reporter(
            ::testing::ScopedFakeTestPartResultReporter::INTERCEPT_ONLY_CURRENT_THREAD,
            &results);
        body();
    }
    Captured out;
    for (int i = 0; i < results.size(); ++i) {
        const ::testing::TestPartResult& r = results.GetTestPartResult(i);
        if (r.failed()) {
            ++out.count;
            out.text += r.message();
            out.text += "\n";
        }
    }
    return out;
}

size_t countOccurrences(const std::string& haystack, const std::string& needle) {
    size_t n = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size())) {
        ++n;
    }
    return n;
}

const char* kBadLet = "fun main() <noret> { let ; }\n";

} // namespace

TEST(ExpectationRunner, AnErrorExpectationMatchesMessageAndPosition) {
    TempSample s(std::string("//@ normative\n")
                 + "//@ error 3:26 \"unexpected SEMICOLON\"\n"
                 + kBadLet);
    auto f = capture([&] { checkSampleAgainstFinc(s.str()); });
    EXPECT_EQ(f.count, 0) << "a correct `//@ error` expectation must pass:\n" << f.text;
}

// `//@ error` means "at least this diagnostic", not "exactly these". Pinned
// here because until now that was accidental — nothing asserted it either way —
// and it has two consequences worth being deliberate about.
//
// First, an `//@ error` sample cannot license anything. It cannot be read as
// evidence that the rest of the file is legal Fin, because the runner never
// claims the annotated diagnostic was the only one. That is what makes
// `undefined_behavior.fin` silent on whether `if (0)` is a legal condition,
// despite being normative and writing it twice — see KnownDefect_Conditions.
//
// Second, and this is the gap: an `//@ error` sample cannot *detect* a new
// unrelated error appearing. Measured, not supposed — enabling the commented-out
// condition check at Analyzer_Stmt.cpp:34 makes `undefined_behavior.fin` emit
// two extra diagnostics, and its test goes on passing, because the annotated one
// is still in the output. A spurious diagnostic is a real defect that misleads a
// user, and today no sample can catch one.
//
// Making `//@ error` exhaustive would close that, and the cost is now measured
// rather than guessed: one sample uses the form, it emits two errors against one
// expectation, so the whole migration is one added `//@ error` line. The reason
// it is not done here is that it changes what the specification format means,
// which is ADR 0008's authority model and the owner's ruling, not the harness's
// to take unilaterally. Recorded in docs/plan.md.
TEST(ExpectationRunner, AnUnrelatedExtraErrorIsTolerated) {
    TempSample s(std::string("//@ normative\n")
                 + "//@ error 3:36 \"expected 'bool', got 'int'\"\n"
                 + "fun main() <void> { let a <bool> = 1; let b <bool> = \"s\"; }\n");
    // Asserted, not assumed: without this the test would go on passing if the
    // second diagnostic ever stopped being emitted, at which point it would be
    // asserting tolerance of an extra error while no extra error existed.
    const std::string direct = stripAnsi(runFinc({s.str(), "--color=never"}).err);
    ASSERT_EQ(countOccurrences(direct, "error:"), 2u)
        << "this test needs a file that really does produce two errors:\n" << direct;

    auto f = capture([&] { checkSampleAgainstFinc(s.str()); });
    EXPECT_EQ(f.count, 0)
        << "the second error, at 3:54, is unannotated and must not fail the "
           "sample under `at least` semantics. If this test starts failing, "
           "`//@ error` became exhaustive — which is a decision, so check it was "
           "made on purpose before relaxing this:\n" << f.text;
}

TEST(ExpectationRunner, AWrongPositionIsCaught) {
    TempSample s(std::string("//@ normative\n")
                 + "//@ error 9:9 \"unexpected SEMICOLON\"\n"
                 + kBadLet);
    auto f = capture([&] { checkSampleAgainstFinc(s.str()); });
    EXPECT_EQ(f.count, 1) << f.text;
    EXPECT_NE(f.text.find("expected a diagnostic at 9:9"), std::string::npos) << f.text;
}

TEST(ExpectationRunner, AWrongMessageIsCaught) {
    TempSample s(std::string("//@ normative\n")
                 + "//@ error 3:26 \"a message finc never prints\"\n"
                 + kBadLet);
    auto f = capture([&] { checkSampleAgainstFinc(s.str()); });
    EXPECT_EQ(f.count, 1) << f.text;
    EXPECT_NE(f.text.find("expected the message"), std::string::npos) << f.text;
}

TEST(ExpectationRunner, AnOkExpectationOnAFailingFileIsCaught) {
    TempSample s(std::string("//@ normative\n//@ ok\n") + kBadLet);
    auto f = capture([&] { checkSampleAgainstFinc(s.str()); });
    EXPECT_GT(f.count, 0) << "`//@ ok` on a file finc rejects must fail";
    EXPECT_NE(f.text.find("`//@ ok`"), std::string::npos) << f.text;
}

TEST(ExpectationRunner, AnUnimplementedExpectationThatStartedWorkingIsCaught) {
    // The stale-exception guard: once a construct works, the harness demands the
    // expectation be promoted rather than leaving the exception behind.
    TempSample s("//@ normative\n//@ unimplemented \"nothing at all\"\n"
                 "fun main() <noret> {}\n");
    auto f = capture([&] { checkSampleAgainstFinc(s.str()); });
    EXPECT_EQ(f.count, 1) << f.text;
    EXPECT_NE(f.text.find("promote the expectation to `//@ ok`"), std::string::npos)
        << f.text;
}

TEST(ExpectationRunner, AnUnannotatedFileIsAFailureNotASkip) {
    TempSample s(kBadLet);
    auto f = capture([&] { checkSampleAgainstFinc(s.str()); });
    EXPECT_GT(f.count, 0) << "a sample with no expectation must fail the harness";
}

TEST(ExpectationRunner, AnUnlabelledFileIsAFailure) {
    TempSample s(std::string("//@ error 2:26 \"unexpected SEMICOLON\"\n") + kBadLet);
    auto f = capture([&] { checkSampleAgainstFinc(s.str()); });
    EXPECT_GT(f.count, 0) << f.text;
    EXPECT_NE(f.text.find("carries no `//@ normative`"), std::string::npos) << f.text;
}

INSTANTIATE_TEST_SUITE_P(
    Corpus,
    SampleExpectation,
    ::testing::ValuesIn(samples()),
    [](const testing::TestParamInfo<Sample>& info) {
        return testNameForSample(info.param.path);
    }
);

// --- The annotation format itself ------------------------------------------

TEST(ExpectationParser, RejectsAFileWithNoExpectation) {
    auto a = parseAnnotation("x.fin", "//@ normative\nfun main() <noret> {}\n");
    EXPECT_FALSE(a.error.empty());
    EXPECT_NE(a.error.find("no //@ expectation"), std::string::npos);
}

TEST(ExpectationParser, RejectsAnUnrecognisedDirective) {
    auto a = parseAnnotation("x.fin", "//@ normative\n//@ okay\n");
    EXPECT_FALSE(a.error.empty());
    EXPECT_NE(a.error.find("unrecognised"), std::string::npos);
}

TEST(ExpectationParser, ReadsOk) {
    auto a = parseAnnotation("x.fin", "//@ normative\n//@ ok\n");
    ASSERT_TRUE(a.error.empty()) << a.error;
    EXPECT_EQ(a.authority, Authority::Normative);
    ASSERT_EQ(a.expectations.size(), 1u);
    EXPECT_EQ(a.expectations[0].kind, ExpectationKind::Ok);
}

TEST(ExpectationParser, ReadsUnimplementedReason) {
    auto a = parseAnnotation("x.fin", "//@ aspirational\n//@ unimplemented \"block comments\"\n");
    ASSERT_TRUE(a.error.empty()) << a.error;
    EXPECT_EQ(a.authority, Authority::Aspirational);
    ASSERT_EQ(a.expectations.size(), 1u);
    EXPECT_EQ(a.expectations[0].kind, ExpectationKind::Unimplemented);
    EXPECT_EQ(a.expectations[0].text, "block comments");
}

TEST(ExpectationParser, ReadsErrorPositionAndMessage) {
    auto a = parseAnnotation("x.fin", "//@ normative\n//@ error 19:24 \"unexpected RPAREN\"\n");
    ASSERT_TRUE(a.error.empty()) << a.error;
    ASSERT_EQ(a.expectations.size(), 1u);
    EXPECT_EQ(a.expectations[0].kind, ExpectationKind::Error);
    EXPECT_EQ(a.expectations[0].line, 19);
    EXPECT_EQ(a.expectations[0].column, 24);
    EXPECT_EQ(a.expectations[0].text, "unexpected RPAREN");
}

TEST(ExpectationParser, ReadsAnEscapedQuoteInsideAMessage) {
    auto a = parseAnnotation("x.fin", "//@ normative\n//@ unimplemented \"Type \\\"T\\\" has no methods\"\n");
    ASSERT_TRUE(a.error.empty()) << a.error;
    ASSERT_EQ(a.expectations.size(), 1u);
    EXPECT_EQ(a.expectations[0].text, "Type \"T\" has no methods");
}

TEST(ExpectationParser, RejectsErrorWithoutAQuotedMessage) {
    auto a = parseAnnotation("x.fin", "//@ normative\n//@ error 3:4\n");
    EXPECT_FALSE(a.error.empty());
}

TEST(ExpectationParser, RejectsOkCombinedWithUnimplemented) {
    auto a = parseAnnotation("x.fin", "//@ normative\n//@ ok\n//@ unimplemented \"x\"\n");
    EXPECT_FALSE(a.error.empty());
}

TEST(ExpectationParser, AllowsAnExpectationToDisagreeWithItsLabel) {
    // ADR 0008: an `ok` inside an aspirational file is how a construct gets
    // promoted without relabelling the file.
    auto a = parseAnnotation("x.fin", "//@ aspirational\n//@ ok\n");
    ASSERT_TRUE(a.error.empty()) << a.error;
    EXPECT_EQ(a.authority, Authority::Aspirational);
}

// --- Census: the corpus's shape, so a drift in it is visible ---------------

namespace {

struct CensusTally {
    int samples = 0;
    int normative = 0, aspirational = 0;
    int expectations = 0;
    int ok = 0, unimplemented = 0, error = 0;
    std::vector<std::string> faults;   // unparseable, or carrying no expectation
};

// Walks the corpus once and asserts nothing. Both tests below call it, so an
// EXPECT in here would attribute one corpus fault to every test that walks the
// corpus — which is how "a sample regressed" and "a sample is malformed" come to
// look like the same failure. Faults are collected and adjudicated by the test
// that is about them.
CensusTally census() {
    CensusTally t;
    for (const auto& p : sampleFiles()) {
        auto a = parseAnnotation(p, readWholeFile(p));
        if (!a.error.empty()) {
            // ADR 0008: an unparseable annotation is a fault in the harness, not
            // a skip. Skipped here only so the rest of the walk still counts.
            t.faults.push_back(p + ": " + a.error);
            continue;
        }
        t.samples++;
        if (a.authority == Authority::Normative) t.normative++;
        if (a.authority == Authority::Aspirational) t.aspirational++;
        if (a.expectations.empty()) {
            t.faults.push_back(p + ": carries no expectation, so it passes by default");
        }
        for (const auto& e : a.expectations) {
            t.expectations++;
            if (e.kind == ExpectationKind::Ok) t.ok++;
            if (e.kind == ExpectationKind::Unimplemented) t.unimplemented++;
            if (e.kind == ExpectationKind::Error) t.error++;
        }
    }
    return t;
}

} // namespace

TEST(Census, EverySampleIsAnnotatedAndClassified) {
    const auto t = census();

    for (const auto& f : t.faults) ADD_FAILURE() << f;

    EXPECT_EQ(t.samples, 50)
        << "the corpus is 50 samples. Sample code changes only by a ratified "
           "language decision (ADR 0008), so a different count is either such a "
           "decision — update this — or a file globbed in by accident";

    // Every sample is exactly one of the two authorities, because a sample with
    // neither carries no weight and one with both cannot be adjudicated.
    EXPECT_EQ(t.normative + t.aspirational, t.samples);

    // Every expectation is one of the three kinds. This is what catches a fourth
    // kind being added to the parser and then silently ignored by the runner —
    // the count is not asserted, the classification is.
    EXPECT_EQ(t.ok + t.unimplemented + t.error, t.expectations);

    ::testing::Test::RecordProperty("normative", t.normative);
    ::testing::Test::RecordProperty("aspirational", t.aspirational);
    ::testing::Test::RecordProperty("ok", t.ok);
    ::testing::Test::RecordProperty("unimplemented", t.unimplemented);
    ::testing::Test::RecordProperty("error", t.error);
}

// The floor below is deliberately a floor and not an equality, and the previous
// version of this test is the argument for it: it asserted `ok == 11` with a note
// saying the number would be updated deliberately. Wave 2's whole job is to move
// samples from `unimplemented` to `ok`, so that assertion was designed to fail on
// every unit of progress — and it did, in a harness file the agent making the
// progress does not own. An equality here does not protect a property; it taxes
// the work.
//
// A floor protects the property that actually matters: a sample that has reached
// `ok` must not go back. Forgetting to raise it after progress is harmless, which
// is the asymmetry that makes it safe to leave slightly stale — the opposite of
// the equality, where forgetting turned a green suite red.
TEST(Census, ThePassingSampleCountNeverFalls) {
    // Raise this when the number goes up; never lower it. Lowering it is the
    // review question "which capability did we lose?", and that question is the
    // only reason this test exists.
    constexpr int kFloor = 17;

    const auto t = census();
    EXPECT_GE(t.ok, kFloor)
        << "a sample that used to be `//@ ok` no longer is. Either the compiler "
           "lost a capability — find it — or an expectation was downgraded, which "
           "needs a reason recorded, not a lower floor here.";

    if (t.ok > kFloor) {
        // Not a failure. Progress is reported so the floor gets raised on purpose
        // rather than discovered later by someone debugging a regression.
        ::testing::Test::RecordProperty("ok_above_floor", t.ok - kFloor);
        std::cerr << "[  NOTE    ] " << t.ok << " samples pass, floor is " << kFloor
                  << " — raise kFloor in tests/test_expectations.cpp\n";
    }
}
