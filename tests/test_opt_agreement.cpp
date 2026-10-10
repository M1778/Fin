// Optimizer-agreement between `finc -O0/-O2/-O3` and between the C++
// compiler and the self-hosted stage at each level.
//
// The gap this closes: nothing in `ctest` compared optimized artifacts
// against each other. A prior audit swept all runnable normative-ok samples
// through finc O0/O2/O3 and finc-vs-stage at each level (40/40 agreement)
// with throwaway scripts; this suite is that sweep standing. Optimization
// must not change observable behavior: same build outcome, byte-identical
// build stderr, same run fate, byte-identical run stdout/stderr.
//
// Two parametrized suites share one checker:
// - `OptLevels`: finc at -O0 vs -O2 vs -O3 (O0 is the reference).
// - `OptStage`: stage2 vs finc at each of -O0/-O2/-O3 (the stage honors -O,
//   so every level is compared, not just the default).
//
// Routing (same as StageAgreement):
// - Expected-error / unimplemented / aspirational samples: out of scope,
//   owned by the expectation runner.
// - `deeptest4` (struct `==`, ADR 0036): explicit skip.
// - A sample the reference (finc -O0) build refuses (no `main`, or a
//   reference refusal): not runnable, skipped with the first stderr line.
// - The stage binary missing: skipped with a reason (Linux-only bootstrap).
//
// Abort samples (`enums`, `nullifier`, `simple_pointers`) abort at runtime
// (SIGABRT, empty stdout). Fate and stdout compare exactly; stderr is under
// the same abort-class rule StageAgreement uses for `simple_pointers`: when
// both sides signaled with matching (empty) stdout, stderr -- the libc or
// blame rendering -- is not compared. Scoped to these stems on purpose:
// everywhere else an exact fate+stderr match is what catches a miscompile.

#include <gtest/gtest.h>

#include <filesystem>
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
// Fate-aware process runner. Same shape as StageAgreement's: `fin::runProcess`
// reports every signal death as -1, conflating SIGABRT with SIGSEGV, so this
// decodes waitpid status directly. Windows degrades to runProcess (signals
// undistinguished; documented, not silent).
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
    const fs::path outPath = uniqueTempPath("optagree_out");
    const fs::path errPath = uniqueTempPath("optagree_err");
    std::vector<std::string> cmd{bin};
    cmd.insert(cmd.end(), args.begin(), args.end());
    const int rc = fin::runProcess(cmd, outPath.string(), errPath.string());
    // Windows has no signals through this API: every termination -- clean
    // exit or abort -- arrives as an exit code, and only -1 (launch failure)
    // is not one. Abort codes are large unsigned values (0xC0000409 for a
    // failed abort) that read negative as int; treating only rc == -1 as
    // unlaunched keeps them comparable by code.
    p.launched = rc != -1;
    if (rc != -1) {
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
    const fs::path outPath = uniqueTempPath("optagree_out");
    const fs::path errPath = uniqueTempPath("optagree_err");
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

// The stage mirrors C++ diagnostics (warnings included), so build stderr
// compares byte-for-byte; errors and the build outcome line are part of
// that comparison, and any divergence fails.

// The stage compiler resolves `package::std` imports through the working
// directory (no binary-relative lib fallback like C++ finc has), so every
// stage build below runs from the repo root, restoring the previous
// directory on exit (cf. StageAgreement's ScopedRepoRoot).
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
// cross-link. Serializes stage builds on a lock file; flock releases on
// process death, so a killed worker cannot wedge the suite. Windows has no
// flock: there the guard is absent and parallel stage builds stay racy
// (documented, not silent).
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
// the shapes without invoking either compiler.
// ---------------------------------------------------------------------------

struct LevelAgreement {
    bool buildExitMatch = false;    // same build exit code
    bool buildStderrMatch = false;  // byte-identical build stderr
    bool fateMatch = false;         // same exit code, or same signal
    bool stdoutMatch = false;       // byte-identical run stdout
    bool stderrMatch = false;       // byte-identical run stderr (modulo abort class)
    bool agrees() const {
        return buildExitMatch && buildStderrMatch && fateMatch && stdoutMatch && stderrMatch;
    }
};

// The abort-class stems: programs whose contract is only that they die
// abnormally with no output. `simple_pointers` frees a stack address (libc's
// response varies by version and environment); `enums` and `nullifier` abort
// on a Fin blame (erased payload, absent denullify). For these stems stderr
// is the runtime's rendering, not the program's output, so it is not compared
// once both sides signaled with matching (empty) stdout. Stdout stays exact,
// and so does fate outside the abort class; inside it, abnormal death agrees
// across fatal signals (a level that exits 0 where the reference aborts is
// still a miscompile). Mirrors StageAgreement's abort-class rule.
bool isOptAbortClass(const std::string& stem) {
    return stem == "enums" || stem == "nullifier" || stem == "simple_pointers";
}

LevelAgreement checkLevelAgreement(const std::string& stem, const FincRun& baseBuild,
                                   const Proc& baseRun, const FincRun& otherBuild,
                                   const Proc& otherRun) {
    LevelAgreement a;
    a.buildExitMatch = baseBuild.exitCode == otherBuild.exitCode;
    a.buildStderrMatch = baseBuild.err == otherBuild.err;
    // Abort-class stems agree on abnormal death regardless of which fatal
    // signal a given libc delivers (SIGABRT with text here, SIGSEGV silent
    // there): fate is "both signaled", exactly as StageAgreement's
    // abort-class rule. Everywhere else the signal stays exact.
    const bool abortDeath = isOptAbortClass(stem) && baseRun.signaled && otherRun.signaled;
    a.fateMatch = (baseRun.exited && otherRun.exited && baseRun.exitCode == otherRun.exitCode) ||
                  (baseRun.signaled && otherRun.signaled &&
                   (baseRun.termSig == otherRun.termSig || abortDeath));
    a.stdoutMatch = baseRun.out == otherRun.out;
    const bool abortClass = isOptAbortClass(stem) && baseRun.signaled && otherRun.signaled;
    a.stderrMatch = (abortClass && a.stdoutMatch && baseRun.out.empty())
                        ? true
                        : baseRun.err == otherRun.err;
    return a;
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
// candidates. Derived from the corpus at discovery time -- no hardcoded list
// to drift -- with the same filter StageAgreement enumerates. Aspirational,
// error and unimplemented samples are out of scope by construction, and
// malformed annotations belong to the expectation suite, not this one.
struct Sample {
    std::string path;
};
void PrintTo(const Sample& s, std::ostream* os) { *os << s.path; }

std::vector<Sample> optAgreementSamples() {
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

const char* kOptLevels[] = {"-O0", "-O2", "-O3"};

}  // namespace

// ---------------------------------------------------------------------------
// The logic, pinned without either compiler.
// ---------------------------------------------------------------------------

TEST(OptAgreementLogic, IdenticalLevelsAgree) {
    FincRun build;
    build.exitCode = 0;
    build.err = "Build Successful.\n";
    Proc run;
    run.exited = true;
    run.exitCode = 0;
    run.out = "0: 1, 1: 2, ";
    EXPECT_TRUE(checkLevelAgreement("loops", build, run, build, run).agrees());
}

TEST(OptAgreementLogic, OptDriftDivergesOnItsOwnAxis) {
    FincRun build;
    build.exitCode = 0;
    build.err = "Build Successful.\n";
    Proc run;
    run.exited = true;
    run.out = "same\n";
    Proc drifted = run;
    drifted.out = "optimized\n";
    const LevelAgreement a = checkLevelAgreement("loops", build, run, build, drifted);
    EXPECT_TRUE(a.buildExitMatch);
    EXPECT_TRUE(a.buildStderrMatch);
    EXPECT_TRUE(a.fateMatch);
    EXPECT_FALSE(a.stdoutMatch);
    EXPECT_FALSE(a.agrees());
    FincRun warnBuild = build;
    warnBuild.err = "Build Successful.\nwarning: something\n";
    EXPECT_FALSE(checkLevelAgreement("loops", build, run, warnBuild, run).buildStderrMatch);
}

TEST(OptAgreementLogic, AbortClassSkipsStderrAndToleratesSignal) {
    // enums aborts on a Fin blame; the rendering is the runtime's, not the
    // program's, so differing stderr still agrees -- but only when both sides
    // signaled with matching empty stdout.
    FincRun build;
    build.exitCode = 0;
    auto aborted = [](const std::string& err) {
        Proc p;
        p.signaled = true;
        p.termSig = 6;
        p.err = err;
        return p;
    };
    for (const std::string stem : {"enums", "nullifier", "simple_pointers"}) {
        const LevelAgreement a =
            checkLevelAgreement(stem, build, aborted("first rendering\n"), build,
                                aborted("second rendering\n"));
        EXPECT_TRUE(a.fateMatch) << stem;
        EXPECT_TRUE(a.stdoutMatch) << stem;
        EXPECT_TRUE(a.stderrMatch) << stem;
        EXPECT_TRUE(a.agrees()) << stem;
    }
    // A level that exits 0 where the reference aborts is a miscompile, even on
    // an abort-class stem.
    Proc exited;
    exited.exited = true;
    exited.exitCode = 0;
    EXPECT_FALSE(checkLevelAgreement("enums", build, aborted("x\n"), build, exited).fateMatch);
    // Cross-signal abnormal death agrees inside the abort class (SIGABRT with
    // text on one libc, SIGSEGV silent on another): same rule as
    // StageAgreement, whose CI libc segfaults where local aborts.
    Proc segv;
    segv.signaled = true;
    segv.termSig = 11;
    EXPECT_TRUE(checkLevelAgreement("nullifier", build, aborted("x\n"), build, segv).agrees());
    // Windows shape: no signals through runProcess, aborts arrive as exit
    // codes. Same code both sides agrees by exit equality; different abort
    // codes disagree.
    Proc wabort, wabort2;
    wabort.exited = true;
    wabort.exitCode = -1073740791;
    wabort2.exited = true;
    wabort2.exitCode = -1073740791;
    EXPECT_TRUE(checkLevelAgreement("nullifier", build, wabort, build, wabort2).agrees());
    wabort2.exitCode = -1073741819;
    EXPECT_FALSE(checkLevelAgreement("nullifier", build, wabort, build, wabort2).agrees());
    // And outside the abort-class stems the same stderr difference fails.
    const LevelAgreement other =
        checkLevelAgreement("loops", build, aborted("first rendering\n"), build,
                            aborted("second rendering\n"));
    EXPECT_TRUE(other.stdoutMatch);
    EXPECT_FALSE(other.stderrMatch);
    EXPECT_FALSE(other.agrees());
}

// ---------------------------------------------------------------------------
// finc -O0 vs -O2 vs -O3. O0 is the reference: a sample it refuses is not
// runnable (same routing as StageAgreement); a level that refuses what O0
// builds is a divergence and fails.
// ---------------------------------------------------------------------------

class OptLevels : public ::testing::TestWithParam<Sample> {};

TEST_P(OptLevels, SameBuildAndRunAcrossOptLevels) {
    const std::string path = GetParam().path;
    const std::string stem = fs::path(path).stem().string();

    // Held ruling ADR 0036: struct `==` is declared-only, never synthesized.
    // The reference refuses it; compile-only, already covered.
    if (stem == "deeptest4") {
        GTEST_SKIP() << "held ruling (ADR 0036): struct `==` is refused by design; out of scope";
    }

    ScopedRepoRoot repoRoot;
    if (!repoRoot.ok()) FAIL() << "cannot chdir to the repo root";

    const fs::path o0Exe = uniqueTempPath("optlev_o0exe");
    const FincRun baseBuild = runFinc({path, "--color=never", "-O0", "-o", o0Exe.string()});
    if (baseBuild.exitCode != 0 || !fs::exists(o0Exe)) {
        removeIfExists(o0Exe);
        const std::string firstErr = baseBuild.err.substr(0, baseBuild.err.find('\n'));
        GTEST_SKIP() << "reference (finc -O0) build refuses " << stem << " (exit "
                     << baseBuild.exitCode << "); not runnable; first stderr line: " << firstErr;
    }

    const Proc baseRun = spawnCapture(o0Exe.string(), {});
    for (const char* level : {"-O2", "-O3"}) {
        const fs::path exe = uniqueTempPath(std::string("optlev_") + (level + 1));
        const FincRun build = runFinc({path, "--color=never", level, "-o", exe.string()});
        EXPECT_EQ(build.exitCode, baseBuild.exitCode)
            << stem << ": " << level << " build exit " << build.exitCode << " vs -O0 "
            << baseBuild.exitCode << ":\n" << build.err;
        EXPECT_EQ(build.err, baseBuild.err)
            << stem << ": " << level << " build stderr diverges:\n-O0 stderr:\n"
            << baseBuild.err << level << " stderr:\n" << build.err;
        if (build.exitCode != 0 || !fs::exists(exe)) {
            removeIfExists(exe);
            continue;  // divergence already recorded above; nothing to run
        }
        const Proc run = spawnCapture(exe.string(), {});
        const LevelAgreement a = checkLevelAgreement(stem, baseBuild, baseRun, build, run);
        EXPECT_TRUE(a.fateMatch) << stem << ": " << level << " run fate diverges: -O0 "
                                 << procFate(baseRun) << " vs " << level << " " << procFate(run)
                                 << "\n-O0 stderr:\n" << baseRun.err << level << " stderr:\n"
                                 << run.err;
        EXPECT_TRUE(a.stdoutMatch) << stem << ": " << level << " run stdout diverges ("
                                   << baseRun.out.size() << " vs " << run.out.size() << " bytes)";
        EXPECT_TRUE(a.stderrMatch) << stem << ": " << level << " run stderr diverges:\n-O0 stderr:\n"
                                   << baseRun.err << level << " stderr:\n" << run.err;
        removeIfExists(exe);
    }
    removeIfExists(o0Exe);
}

INSTANTIATE_TEST_SUITE_P(
    Corpus,
    OptLevels,
    ::testing::ValuesIn(optAgreementSamples()),
    [](const testing::TestParamInfo<Sample>& info) { return testNameForSample(info.param.path); });

// ---------------------------------------------------------------------------
// stage2 vs finc at each of -O0/-O2/-O3. The reference at each level is finc
// at that level: a sample finc refuses there is not runnable; a stage refusal
// where finc builds is an unlisted divergence and fails.
// ---------------------------------------------------------------------------

class OptStage : public ::testing::TestWithParam<Sample> {};

bool optStageToleratedRefusal(const std::string& stem) {
    (void)stem;
    return false;
}

TEST_P(OptStage, StageAgreesWithFincAtEachOptLevel) {
    const std::string path = GetParam().path;
    const std::string stem = fs::path(path).stem().string();

    const std::string stageBin = stageBinary();
    if (stageBin.empty() || !fs::exists(stageBin)) {
        GTEST_SKIP() << "stage compiler not found at FINC_STAGE2_BINARY=" << stageBin
                     << "; run-agreement needs build/finc_stage2 next to build/finc";
    }
    if (stem == "deeptest4") {
        GTEST_SKIP() << "held ruling (ADR 0036): struct `==` is refused by design; out of scope";
    }

    // Same tree for both compilers (stage resolves packages via CWD).
    ScopedRepoRoot repoRoot;
    if (!repoRoot.ok()) FAIL() << "cannot chdir to the repo root; stage imports would misresolve";

    for (const char* level : kOptLevels) {
        const fs::path fincExe = uniqueTempPath(std::string("optstg_finc_") + (level + 1));
        const FincRun fincBuild = runFinc({path, "--color=never", level, "-o", fincExe.string()});
        if (fincBuild.exitCode != 0 || !fs::exists(fincExe)) {
            removeIfExists(fincExe);
            const std::string firstErr = fincBuild.err.substr(0, fincBuild.err.find('\n'));
            GTEST_SKIP() << "reference (finc " << level << ") build refuses " << stem << " (exit "
                         << fincBuild.exitCode << "); not runnable; first stderr line: " << firstErr;
        }
        const fs::path stageExe = uniqueTempPath(std::string("optstg_stage_") + (level + 1));
        Proc stageBuild;
        {
            StageBuildLock lock;
            stageBuild = spawnCapture(stageBin, {path, "--color=never", level, "-o",
                                                stageExe.string()});
        }
        const bool stageBuilt =
            stageBuild.exited && stageBuild.exitCode == 0 && fs::exists(stageExe);
        if (!stageBuilt && optStageToleratedRefusal(stem)) {
            // Tolerated IFF an orderly refusal (exit 1 with a diagnostic): a
            // stage crash is a new shape, not the listed one.
            EXPECT_TRUE(stageBuild.exited && stageBuild.exitCode == 1)
                << stem << " " << level << ": allowlisted as a build refusal but the stage "
                << procFate(stageBuild) << ":\n" << stageBuild.err;
            ::testing::Test::RecordProperty("allowlisted", "ADR 0049 hash_word leaf (stage-half mirror pending)");
            removeIfExists(fincExe);
            removeIfExists(stageExe);
            continue;
        }
        if (stageBuilt && optStageToleratedRefusal(stem)) {
            removeIfExists(fincExe);
            removeIfExists(stageExe);
            FAIL() << stem << " " << level << ": stage now builds it -- delete it from optStageToleratedRefusal";
        }
        EXPECT_TRUE(stageBuild.exited && stageBuild.exitCode == fincBuild.exitCode)
            << stem << " " << level << ": stage build " << procFate(stageBuild)
            << " vs finc exit " << fincBuild.exitCode << ":\n" << stageBuild.err;
        EXPECT_TRUE(stageBuild.err == fincBuild.err)
            << stem << " " << level << ": stage build stderr diverges:\nfinc stderr:\n"
            << fincBuild.err << "stage stderr:\n" << stageBuild.err;
        if (stageBuilt) {
            const Proc fincRun = spawnCapture(fincExe.string(), {});
            const Proc stageRun = spawnCapture(stageExe.string(), {});
            FincRun stageBuildAsRun;
            stageBuildAsRun.exitCode = stageBuild.exitCode;
            stageBuildAsRun.err = stageBuild.err;
            const LevelAgreement a =
                checkLevelAgreement(stem, fincBuild, fincRun, stageBuildAsRun, stageRun);
            EXPECT_TRUE(a.fateMatch)
                << stem << " " << level << ": run fate diverges: finc " << procFate(fincRun)
                << " vs stage " << procFate(stageRun);
            EXPECT_TRUE(a.stdoutMatch)
                << stem << " " << level << ": run stdout diverges (" << fincRun.out.size() << " vs "
                << stageRun.out.size() << " bytes)";
            EXPECT_TRUE(a.stderrMatch)
                << stem << " " << level << ": run stderr diverges:\nfinc stderr:\n"
                << fincRun.err << "stage stderr:\n" << stageRun.err;
        }
        removeIfExists(fincExe);
        removeIfExists(stageExe);
    }
}

INSTANTIATE_TEST_SUITE_P(
    Corpus,
    OptStage,
    ::testing::ValuesIn(optAgreementSamples()),
    [](const testing::TestParamInfo<Sample>& info) { return testNameForSample(info.param.path); });
