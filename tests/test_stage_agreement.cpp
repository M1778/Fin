// Run agreement between the C++ compiler (build/finc) and the self-hosted
// stage-2 compiler (build/finc_stage2).
//
// The harness gap this closes: `ctest` stayed green while the two compilers
// DIVERGED at runtime. `tests/samples/const.fin` is the case that bit us --
// the C++ build aborted (SIGABRT, assertion at :85, empty stdout) while the
// stage build printed and exited 0 -- and only a manual sweep caught it,
// because every automated suite checked at most one compiler's artifact.
// Expectation tests (`test_expectations.cpp`) are compile-only; codegen tests
// (`test_codegen.cpp`) build and run but only through build/finc.
//
// So for each runnable normative-ok sample this suite builds AND runs with
// both compilers and requires the same fate: same build outcome, same run
// exit-or-signal, byte-identical run stdout, byte-identical run stderr.
// Anything else is a divergence and fails, unless the sample is named in the
// allowlist below with an owner and a reason.
//
// Scope (deliberate, all three are covered elsewhere):
// - Expected-error / unimplemented samples: compile-only, owned by the
//   expectation runner. They never reach the run comparison here.
// - `deeptest4` (struct `==`): held ruling ADR 0036, declared-only and never
//   synthesized, so neither compiler produces a runnable. Explicit skip.
// - Aspirational samples (e.g. `deeptest2`): not normative; out of scope.
// - A sample the reference (C++) build refuses (no `main`, or a reference
//   refusal): not runnable by definition, skipped. A C++ crash would skip
//   here but is already red in MatchesCompilerBehaviour's exit-code contract.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
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
// Allowlist. One line per entry: when the owner closes the gap, deleting the
// line is the whole change. A line that stops matching fails the suite (stale
// entry), so entries cannot rot.
// ---------------------------------------------------------------------------

struct AllowEntry {
    const char* sample;  // stem without `.fin`
    const char* kind;    // "stage-build-refusal" or "stderr-wording"
    const char* owner;   // who deletes the line when the gap closes
    const char* reason;
};

// complex/interfaces refuse in the stage with a monomorphization diagnostic.
// deeptest1/implements_block/lambdas did too (stage emitted `add ptr`) until
// the stage side was fixed; their lines were deleted. Each remaining entry
// must go away with a one-line deletion.
const AllowEntry kAllowlist[] = {
};

const AllowEntry* lookupAllow(const std::string& stem, const std::string& kind) {
    for (const auto& e : kAllowlist)
        if (stem == e.sample && kind == e.kind) return &e;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Fate-aware process runner. `fin::runProcess` reports every signal death as
// -1, which would conflate SIGABRT with SIGSEGV -- the same class of gap the
// 128+signal encoding once caused in the expectation runner. Agreement needs
// the real fate, so this decodes waitpid status directly (cf. fintest's
// runWithCapture, which exists for the same reason). Windows has no
// fork/waitpid: there it degrades to runProcess and signals are not
// distinguished (documented, not silent).
// ---------------------------------------------------------------------------

struct Proc {
    bool exited = false;    // WIFEXITED -- exitCode valid
    int exitCode = -1;
    bool signaled = false;  // WIFSIGNALED -- termSig valid (WTERMSIG)
    int termSig = 0;
    bool launched = false;
    std::string out;
    std::string err;
};

Proc spawnCapture(const std::string& bin, const std::vector<std::string>& args) {
    Proc p;
#ifdef _WIN32
    const fs::path outPath = uniqueTempPath("stageagree_out");
    const fs::path errPath = uniqueTempPath("stageagree_err");
    std::vector<std::string> cmd{bin};
    cmd.insert(cmd.end(), args.begin(), args.end());
    const int rc = fin::runProcess(cmd, outPath.string(), errPath.string());
    p.launched = rc >= -1;
    if (rc >= 0) {
        p.exited = true;
        p.exitCode = rc;
    }
    p.out = readProcessOutput(outPath.string());
    p.err = readProcessOutput(errPath.string());
    std::error_code ec;
    fs::remove(outPath, ec);
    fs::remove(errPath, ec);
    return p;
#else
    const fs::path outPath = uniqueTempPath("stageagree_out");
    const fs::path errPath = uniqueTempPath("stageagree_err");
    const pid_t pid = fork();
    if (pid < 0) return p;
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
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(bin.c_str()));
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(bin.c_str(), argv.data());
        _exit(127);
    }
    p.launched = true;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
    }
    if (WIFEXITED(status)) {
        p.exited = true;
        p.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        p.signaled = true;
        p.termSig = WTERMSIG(status);
    }
    p.out = readProcessOutput(outPath.string());
    p.err = readProcessOutput(errPath.string());
    std::error_code ec;
    fs::remove(outPath, ec);
    fs::remove(errPath, ec);
    return p;
#endif
}

std::string stageBinary() {
#ifdef FINC_STAGE2_BINARY
    std::string s = FINC_STAGE2_BINARY;
#ifdef _WIN32
    if (!s.empty() && !fs::exists(s) && fs::exists(s + ".exe")) s += ".exe";
#endif
    return s;
#else
    return "";
#endif
}

// The stage compiler resolves `package::std` imports through the working
// directory (no binary-relative lib fallback like C++ finc has), so a stage
// `-o` build from anywhere but the repo root dies with `module not found`
// even though the compilers agree. Both builds and both runs below must see
// the same tree, so the test body runs them all from the repo root and
// restores the previous directory on exit (ctest workers start elsewhere,
// and a full-binary run shares the process with later suites).
class ScopedRepoRoot {
public:
    ScopedRepoRoot() {
        std::error_code ec;
        prev_ = fs::current_path(ec);
        if (ec) {
            ok_ = false;
            return;
        }
        fs::current_path(fs::path(testsDir()).parent_path(), ec);
        if (ec) ok_ = false;
    }
    ~ScopedRepoRoot() {
        if (!prev_.empty()) {
            std::error_code ec;
            fs::current_path(prev_, ec);
        }
    }
    bool ok() const { return ok_; }

private:
    fs::path prev_;
    bool ok_ = true;
};

// The stage links every `-o` build through the fixed path
// /tmp/finc_stage_tmp.o (finc/driver.fin): two stage builds at once
// cross-link, and one sample's exe runs another sample's program (observed:
// preprocessor's exe printed functions' output under ctest -j8). That is the
// stage owner's bug to fix; until then stage builds serialize here on a lock
// file. flock releases on process death, so a killed worker cannot wedge the
// suite. Windows has no flock: there the guard is absent and parallel stage
// builds stay racy (documented, not silent).
class StageBuildLock {
public:
    StageBuildLock() {
#ifndef _WIN32
        lockPath_ = (fs::temp_directory_path() / "finc_stage_build.lock").string();
        fd_ = ::open(lockPath_.c_str(), O_CREAT | O_RDWR, 0600);
        if (fd_ >= 0) ::flock(fd_, LOCK_EX);
#endif
    }
    ~StageBuildLock() {
#ifndef _WIN32
        if (fd_ >= 0) {
            ::flock(fd_, LOCK_UN);
            ::close(fd_);
        }
#endif
    }

private:
#ifndef _WIN32
    std::string lockPath_;
    int fd_ = -1;
#endif
};

// ---------------------------------------------------------------------------
// Pure comparison logic. Kept free of I/O so the unit tests below can drive
// the exact pre-fix shapes (notably const's abort-vs-print) without invoking
// either compiler.
// ---------------------------------------------------------------------------

struct RunAgreement {
    bool fateMatch = false;    // same exit code, or same signal
    bool stdoutMatch = false;  // byte-identical stdout
    bool stderrMatch = false;  // byte-identical stderr (modulo allowlist)
    bool agrees() const { return fateMatch && stdoutMatch && stderrMatch; }
};

std::string normalizedStderr(const std::string& stem, const std::string& err) {
    // No wording entries remain: bare-T monomorphization converged the one
    // glibc spelling (simple_pointers aborts `free(): invalid pointer` on
    // both sides), so stderr compares byte-exact for every sample.
    (void)stem;
    return err;
}

RunAgreement checkRunAgreement(const std::string& stem, const Proc& cpp, const Proc& stage) {
    RunAgreement a;
    a.fateMatch = (cpp.exited && stage.exited && cpp.exitCode == stage.exitCode) ||
                  (cpp.signaled && stage.signaled && cpp.termSig == stage.termSig);
    a.stdoutMatch = cpp.out == stage.out;
    a.stderrMatch = normalizedStderr(stem, cpp.err) == normalizedStderr(stem, stage.err);
    return a;
}

enum class BuildVerdict {
    BothBuild,            // run comparison decides
    ReferenceRefuses,     // C++ refuses: not runnable, skip (no-main, reference refusal)
    ToleratedRefusal,     // C++ builds, stage refuses, sample allowlisted
    UnlistedDivergence,   // C++ builds, stage refuses, nobody allowlisted it
    StaleAllowlist,       // stage builds a sample the allowlist says it refuses
};

BuildVerdict checkBuildAgreement(const std::string& stem, bool cppBuilt, bool stageBuilt) {
    if (!cppBuilt) return BuildVerdict::ReferenceRefuses;
    if (stageBuilt) {
        return lookupAllow(stem, "stage-build-refusal") != nullptr ? BuildVerdict::StaleAllowlist
                                                                   : BuildVerdict::BothBuild;
    }
    return lookupAllow(stem, "stage-build-refusal") != nullptr ? BuildVerdict::ToleratedRefusal
                                                               : BuildVerdict::UnlistedDivergence;
}

std::string procFate(const Proc& p) {
    if (p.exited) return "exit " + std::to_string(p.exitCode);
    if (p.signaled) return "signal " + std::to_string(p.termSig);
    return "unlaunched";
}

void removeIfExists(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec);
}

// Every `.fin` sample whose annotation is normative `//@ ok`: the runnable
// candidates. Aspirational-ok (deeptest2), error and unimplemented samples are
// out of scope by construction, and malformed annotations belong to the
// expectation suite, not this one.
struct Sample {
    std::string path;
};
void PrintTo(const Sample& s, std::ostream* os) { *os << s.path; }

std::vector<Sample> agreementSamples() {
    std::vector<Sample> out;
    for (const auto& p : sampleFiles()) {
        SampleAnnotation ann = parseAnnotation(p, readWholeFile(p));
        if (!ann.error.empty()) continue;
        if (ann.authority != Authority::Normative) continue;
        if (ann.expectations.size() != 1) continue;
        if (ann.expectations.front().kind != ExpectationKind::Ok) continue;
        out.push_back(Sample{p});
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// The logic, pinned without either compiler. These are the catch-proof: the
// first test replays the pre-fix const shape the manual sweep found (C++
// aborts, stage prints) and asserts the checker calls it a divergence on both
// the fate and the stdout assertion.
// ---------------------------------------------------------------------------

TEST(StageAgreementLogic, PreFixConstShapeDivergesOnFateAndStdout) {
    // Pre-fix const: C++ aborted (SIGABRT, assertion, empty stdout) while the
    // stage build printed its rptr snapshot and exited 0. Both halves of the
    // run comparison must fire on that input -- fate first, then stdout.
    Proc cpp, stage;
    cpp.signaled = true;
    cpp.termSig = 6;
    cpp.err = "tests/samples/const.fin:85: assertion failed\n";
    stage.exited = true;
    stage.exitCode = 0;
    stage.out = "0: 1, 1: 2, 2: 3, 3: 4, ";
    const RunAgreement a = checkRunAgreement("const", cpp, stage);
    EXPECT_FALSE(a.fateMatch) << "SIGABRT vs exit 0 must fail the fate assertion";
    EXPECT_FALSE(a.stdoutMatch) << "empty vs printed must fail the stdout assertion";
    EXPECT_FALSE(a.agrees());
}

TEST(StageAgreementLogic, PostFixConstShapeAgrees) {
    // Post-fix const (sibling SN1's rptr.restrict redesign): both print the
    // same bytes and exit 0. This is the EXPECTED-PASS state the suite below
    // holds; if the tree regresses, the per-sample test reports it.
    Proc cpp, stage;
    cpp.exited = true;
    cpp.exitCode = 0;
    cpp.out = "0: 1, 1: 2, 2: 3, 3: 4, ";
    stage = cpp;
    const RunAgreement a = checkRunAgreement("const", cpp, stage);
    EXPECT_TRUE(a.agrees()) << "fate " << procFate(cpp) << " vs " << procFate(stage);
}

TEST(StageAgreementLogic, SimplePointersStderrAgreesByteExact) {
    // Bare-T monomorphization converged the glibc spelling: both sides abort
    // with `free(): invalid pointer`, so no normalization remains and the
    // identical pair agrees under every stem.
    Proc cpp, stage;
    cpp.signaled = true;
    cpp.termSig = 6;
    cpp.err = "free(): invalid pointer\n";
    stage.signaled = true;
    stage.termSig = 6;
    stage.err = "free(): invalid pointer\n";
    EXPECT_TRUE(checkRunAgreement("simple_pointers", cpp, stage).agrees());
    EXPECT_TRUE(checkRunAgreement("loops", cpp, stage).agrees());
}

TEST(StageAgreementLogic, BuildVerdictsRouteEveryShape) {
    EXPECT_EQ(checkBuildAgreement("loops", true, true), BuildVerdict::BothBuild);
    // Neither builds (deeptest4's held ruling, or no `main`): not runnable.
    EXPECT_EQ(checkBuildAgreement("deeptest4", false, false), BuildVerdict::ReferenceRefuses);
    EXPECT_EQ(checkBuildAgreement("macros", false, false), BuildVerdict::ReferenceRefuses);
    // A refusal nobody listed is the failure this suite exists for.
    EXPECT_EQ(checkBuildAgreement("loops", true, false), BuildVerdict::UnlistedDivergence);
    // MM1 fixed the stage: former refusals now build like the reference.
    EXPECT_EQ(checkBuildAgreement("complex", true, true), BuildVerdict::BothBuild);
    EXPECT_EQ(checkBuildAgreement("interfaces", true, true), BuildVerdict::BothBuild);
    EXPECT_EQ(checkBuildAgreement("loops", true, true), BuildVerdict::BothBuild);
}

// ---------------------------------------------------------------------------
// The per-sample run agreement, through both real compilers.
// ---------------------------------------------------------------------------

class StageAgreement : public ::testing::TestWithParam<Sample> {};

TEST_P(StageAgreement, RunAgreesAcrossCompilers) {
    const std::string path = GetParam().path;
    const std::string stem = fs::path(path).stem().string();

    const std::string stageBin = stageBinary();
    if (stageBin.empty() || !fs::exists(stageBin)) {
        GTEST_SKIP() << "stage compiler not found at FINC_STAGE2_BINARY=" << stageBin
                     << "; run-agreement needs build/finc_stage2 next to build/finc";
    }

    // Held ruling ADR 0036: struct `==` is declared-only, never synthesized.
    // Neither compiler produces a runnable; compile-only, already covered.
    if (stem == "deeptest4") {
        GTEST_SKIP() << "held ruling (ADR 0036): struct `==` is refused by design; out of scope";
    }

    // Same tree for both compilers (stage resolves packages via CWD).
    ScopedRepoRoot repoRoot;
    if (!repoRoot.ok()) FAIL() << "cannot chdir to the repo root; stage imports would misresolve";

    // Reference build (C++). A refusal here means not runnable: no `main`, or
    // a reference-side refusal. A C++ crash would also land here, but that is
    // already red in MatchesCompilerBehaviour's exit-code contract.
    const fs::path cppExe = uniqueTempPath("stageagree_cpp");
    const FincRun cppBuild = runFinc({path, "--color=never", "-o", cppExe.string()});
    const bool cppBuilt = cppBuild.exitCode == 0 && fs::exists(cppExe);
    if (!cppBuilt) {
        removeIfExists(cppExe);
        GTEST_SKIP() << "reference (C++) build refuses " << stem << " (exit " << cppBuild.exitCode
                     << "); not runnable";
    }

    // Stage build, serialized: the stage cross-links concurrent `-o` builds
    // through its fixed /tmp object (see StageBuildLock).
    const fs::path stageExe = uniqueTempPath("stageagree_stage");
    Proc stageBuild;
    {
        StageBuildLock lock;
        stageBuild = spawnCapture(stageBin, {path, "-o", stageExe.string()});
    }
    const bool stageBuilt = stageBuild.exited && stageBuild.exitCode == 0 && fs::exists(stageExe);

    switch (checkBuildAgreement(stem, cppBuilt, stageBuilt)) {
        case BuildVerdict::ReferenceRefuses:
            removeIfExists(cppExe);
            removeIfExists(stageExe);
            GTEST_SKIP() << "reference refuses; not runnable";
            break;
        case BuildVerdict::ToleratedRefusal: {
            const AllowEntry* e = lookupAllow(stem, "stage-build-refusal");
            // The allowlist covers a *refusal* (orderly exit 1 with a
            // diagnostic). A stage crash is a new shape, not the listed one.
            EXPECT_TRUE(stageBuild.exited && stageBuild.exitCode == 1)
                << stem << ": allowlisted as a build refusal but the stage " << procFate(stageBuild)
                << ":\n"
                << stageBuild.err;
            ::testing::Test::RecordProperty("allowlisted", e->reason);
            removeIfExists(cppExe);
            removeIfExists(stageExe);
            break;
        }
        case BuildVerdict::StaleAllowlist:
            removeIfExists(cppExe);
            removeIfExists(stageExe);
            FAIL() << stem << ": stage now builds it -- delete the one allowlist line";
            break;
        case BuildVerdict::UnlistedDivergence:
            removeIfExists(cppExe);
            removeIfExists(stageExe);
            FAIL() << "unlisted build divergence on " << stem << ": C++ builds (exit 0) but the stage "
                   << procFate(stageBuild) << ":\n" << stageBuild.err;
            break;
        case BuildVerdict::BothBuild:
            break;
    }
    if (!stageBuilt) return;  // verdict already recorded above (tolerated)

    // Both built: run both, compare fate + stdout + stderr.
    const Proc cpp = spawnCapture(cppExe.string(), {});
    const Proc stg = spawnCapture(stageExe.string(), {});
    const RunAgreement a = checkRunAgreement(stem, cpp, stg);
    EXPECT_TRUE(a.fateMatch) << stem << ": run fate diverges: C++ " << procFate(cpp) << " vs stage "
                             << procFate(stg) << "\nC++ stderr:\n"
                             << cpp.err << "stage stderr:\n"
                             << stg.err;
    EXPECT_TRUE(a.stdoutMatch) << stem << ": run stdout diverges (" << cpp.out.size() << " vs "
                               << stg.out.size() << " bytes)\nC++ stdout:\n"
                               << cpp.out << "stage stdout:\n"
                               << stg.out;
    EXPECT_TRUE(a.stderrMatch) << stem << ": run stderr diverges\nC++ stderr:\n"
                               << cpp.err << "stage stderr:\n"
                               << stg.err;
    removeIfExists(cppExe);
    removeIfExists(stageExe);
}

INSTANTIATE_TEST_SUITE_P(
    Corpus,
    StageAgreement,
    ::testing::ValuesIn(agreementSamples()),
    [](const testing::TestParamInfo<Sample>& info) { return testNameForSample(info.param.path); });
