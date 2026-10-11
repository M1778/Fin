// Run agreement between the C++ compiler (build/finc) and the self-hosted
// stage compilers (build/finc_stage2, build/finc_stage3).
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
// And for each normative error / unimplemented sample (issue #76) this suite
// compares the compile refusal compile-only: same exit code, plus the pinned
// location and message for `//@ error`, or byte-identical refusal text for
// `//@ unimplemented`. A stage that compiles clean what the reference refuses
// -- or refuses at a different place -- is red exactly like a run divergence.
//
// Scope (deliberate, all three are covered elsewhere):
// - `deeptest4` (struct `==`): held ruling ADR 0036, declared-only and never
//   synthesized, so neither compiler produces a runnable. Explicit skip.
// - Aspirational samples (e.g. `deeptest2`): not normative; out of scope.
// - A sample the reference (C++) build refuses (no `main`, or a reference
//   refusal): not runnable by definition, skipped. A C++ crash would skip
//   here but is already red in MatchesCompilerBehaviour's exit-code contract.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
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
// the stage side was fixed; their lines were deleted, and literal_struct's
// went the same way (the stage lowers the $struct-seeded return now).
// Each remaining entry must go away with a one-line deletion.
const AllowEntry kAllowlist[] = {
    // block_scope_use_after's 7:5 diagnostic: C++ pins `Undefined variable
    // 'b'`, the stage reports `use of undeclared identifier`. Same place, same
    // exit code, different wording -- the message-text half of the error check
    // below is waived for this stem, the position half is not.
    {"block_scope_use_after", "stderr-wording", "EC1/#76",
     "stage words the 7:5 refusal `use of undeclared identifier`, C++ pins `Undefined variable 'b'`"},
    // Sentinel: a deduced-size array with `= {}` has size 0, which is
    // ill-formed (MSVC rejects it with C2466; GCC/Clang accept it as an
    // extension). The empty-string entry never matches a real (stem, kind)
    // pair -- stems are sample file stems, kinds are "stage-build-refusal"
    // or "stderr-wording" -- so lookup semantics stay "empty allowlist".
    // It is inert beside real entries; delete it with the last real entry.
    {"", "", "", ""},
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
// distinguished (documented, not silent) -- an abort arrives as a nonzero
// exit code, so abort agreement there compares codes, not signals.
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
    // Windows has no signals through this API: every termination -- clean
    // exit or abort -- arrives as an exit code, and only -1 (launch failure)
    // is not one. Abort codes are large unsigned values (0xC0000409 for a
    // failed abort, access violations likewise) that read negative as int;
    // treating only rc == -1 as unlaunched keeps them comparable by code, so
    // two sides aborting the same way agree and different deaths disagree.
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

// Issue #35: the stage-3 compiler (the compiler building itself twice over).
// A stage2 miscompile that reproduces itself is invisible to
// reference-vs-stage2; comparing stage3 output closes that hole. Resolved the
// same way as stage2: baked path, loud skip when absent, never silent.
std::string stage3Binary() {
#ifdef FINC_STAGE3_BINARY
    std::string s = FINC_STAGE3_BINARY;
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

// simple_pointers frees a stack address: undefined behavior, and the C
// library's response varies by version and environment (SIGABRT with
// `free(): invalid pointer` on one glibc, `munmap_chunk(): invalid pointer`
// on another, SIGSEGV with no text at all elsewhere). The sample's contract
// is only that the program dies abnormally with no output, so for this stem
// agreement means both sides signaled with byte-identical (empty) stdout;
// stderr is libc's and is not compared. Scoped to this stem on purpose:
// everywhere else a stage segfault where C++ aborts is a miscompile, not a
// libc difference, and exact fate+stderr agreement is what catches it.
bool isAbortClassAgreement(const std::string& stem) {
    return stem == "simple_pointers";
}

RunAgreement checkRunAgreement(const std::string& stem, const Proc& cpp, const Proc& stage) {
    RunAgreement a;
    const bool abortClass = isAbortClassAgreement(stem) && cpp.signaled && stage.signaled;
    a.fateMatch = (cpp.exited && stage.exited && cpp.exitCode == stage.exitCode) ||
                  (cpp.signaled && stage.signaled &&
                   (cpp.termSig == stage.termSig || abortClass));
    a.stdoutMatch = cpp.out == stage.out;
    a.stderrMatch = (abortClass && a.stdoutMatch && cpp.out.empty())
                        ? true
                        : normalizedStderr(stem, cpp.err) == normalizedStderr(stem, stage.err);
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

// ---------------------------------------------------------------------------
// Diagnostic agreement (issue #76). Pure: the per-sample tests below only feed
// it compile-only exit codes and stripped stderrs.
// ---------------------------------------------------------------------------

enum class DiagVerdict {
    Agree,             // same fate, pinned diagnostic / refusal text agrees
    FateDivergence,    // different compile exit codes
    MessageDivergence, // same fate, diagnostic disagrees (or pin unmet)
    ToleratedWording,  // allowlisted stderr-wording divergence
    StaleAllowlist,    // allowlisted but now identical -- delete the line
};

// The expectation runner's `-->` shape (`--> path:line:col`); duplicated here
// because that parser lives in test_expectations.cpp's anonymous namespace.
bool stderrHasPosition(const std::string& err, int line, int column) {
    static const std::regex re(R"(-->\s+\S*?:(\d+):(\d+))");
    auto begin = std::sregex_iterator(err.begin(), err.end(), re);
    for (auto it = begin; it != std::sregex_iterator(); ++it) {
        if (std::stoi((*it)[1].str()) == line && std::stoi((*it)[2].str()) == column) return true;
    }
    return false;
}

DiagVerdict checkDiagAgreement(const std::string& stem, ExpectationKind kind, const Expectation* pin,
                               int cppExit, const std::string& cppErr, int stageExit,
                               const std::string& stageErr) {
    const bool allow = lookupAllow(stem, "stderr-wording") != nullptr;
    if (cppExit != stageExit) return DiagVerdict::FateDivergence;
    if (kind == ExpectationKind::Error) {
        // Both accept: nothing to compare; the pin is the expectation suite's.
        if (cppExit == 0) return DiagVerdict::Agree;
        const bool pos = pin != nullptr && stderrHasPosition(cppErr, pin->line, pin->column) &&
                         stderrHasPosition(stageErr, pin->line, pin->column);
        const bool msg = pin != nullptr && cppErr.find(pin->text) != std::string::npos &&
                         stageErr.find(pin->text) != std::string::npos;
        if (pos && msg) return allow ? DiagVerdict::StaleAllowlist : DiagVerdict::Agree;
        if (pos && allow) return DiagVerdict::ToleratedWording;
        return DiagVerdict::MessageDivergence;
    }
    // Unimplemented: a deliberate refusal, so the refusal *text* is compared,
    // not just the exit code. importing.fin proves the both-accept shape: from
    // the repo root both compilers exit 0 with identical stderr, which agrees.
    const bool same = normalizedStderr(stem, cppErr) == normalizedStderr(stem, stageErr);
    if (same) return allow ? DiagVerdict::StaleAllowlist : DiagVerdict::Agree;
    return allow ? DiagVerdict::ToleratedWording : DiagVerdict::MessageDivergence;
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

// Every normative `//@ error` / `//@ unimplemented` sample: the diagnostic
// candidates. Aspirational samples stay out (same authority rule as above),
// and malformed annotations belong to the expectation suite, not this one.
struct DiagSample {
    std::string path;
    ExpectationKind kind = ExpectationKind::Error;
    Expectation pin;  // the single expectation; message+position for Error
};
void PrintTo(const DiagSample& s, std::ostream* os) { *os << s.path; }

std::vector<DiagSample> diagnosticSamples() {
    std::vector<DiagSample> out;
    for (const auto& p : sampleFiles()) {
        SampleAnnotation ann = parseAnnotation(p, readWholeFile(p));
        if (!ann.error.empty()) continue;
        if (ann.authority != Authority::Normative) continue;
        if (ann.expectations.size() != 1) continue;
        const Expectation& e = ann.expectations.front();
        if (e.kind != ExpectationKind::Error && e.kind != ExpectationKind::Unimplemented) continue;
        out.push_back(DiagSample{p, e.kind, e});
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

TEST(StageAgreementLogic, SimplePointersAgreesOnAbortClass) {
    // simple_pointers frees a stack address: the libc response varies
    // (SIGABRT with `free(): invalid pointer` here, `munmap_chunk` there,
    // SIGSEGV with no text elsewhere), so agreement is abort-class scoped
    // to this stem: both signaled, stdout byte-identical (empty), stderr
    // uncompared. The identical pair still agrees everywhere.
    Proc cpp, stage;
    cpp.signaled = true;
    cpp.termSig = 6;
    cpp.err = "free(): invalid pointer\n";
    stage.signaled = true;
    stage.termSig = 6;
    stage.err = "free(): invalid pointer\n";
    EXPECT_TRUE(checkRunAgreement("simple_pointers", cpp, stage).agrees());
    EXPECT_TRUE(checkRunAgreement("loops", cpp, stage).agrees());
    // The CI shape: C++ segfaults silently, stage aborts with munmap text.
    // Agrees under simple_pointers, and must NOT agree anywhere else (a
    // stage segfault where C++ aborts is a miscompile outside this stem).
    Proc segv, munmap;
    segv.signaled = true;
    segv.termSig = 11;
    segv.err = "";
    munmap.signaled = true;
    munmap.termSig = 6;
    munmap.err = "munmap_chunk(): invalid pointer\n";
    EXPECT_TRUE(checkRunAgreement("simple_pointers", segv, munmap).agrees());
    const RunAgreement other = checkRunAgreement("loops", segv, munmap);
    EXPECT_TRUE(other.stdoutMatch);
    EXPECT_FALSE(other.agrees());
    // Windows shape: no signals through runProcess, so aborts arrive as
    // exit codes (0xC0000409 read as int). Same code both sides agrees by
    // exit equality; different abort codes disagree.
    Proc wabort, wabort2;
    wabort.exited = true;
    wabort.exitCode = -1073740791;
    wabort2.exited = true;
    wabort2.exitCode = -1073740791;
    EXPECT_TRUE(checkRunAgreement("simple_pointers", wabort, wabort2).agrees());
    wabort2.exitCode = -1073741819;
    EXPECT_FALSE(checkRunAgreement("simple_pointers", wabort, wabort2).agrees());
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
// Diagnostic agreement, pinned without either compiler. The first two are the
// issue-#76 catch-proof: a stage that drops a pinned diagnostic (exits 0, or
// exits 1 without the message) must be a divergence, and refusing at a
// different place must be one too.
// ---------------------------------------------------------------------------

TEST(StageAgreementLogic, StageDroppingAPinnedDiagnosticDiverges) {
    // undefined_behavior.fin's pin. Reference refuses; stage compiles clean.
    Expectation pin;
    pin.kind = ExpectationKind::Error;
    pin.line = 3;
    pin.column = 1;
    pin.text = "Function 'add' is missing a return statement on some paths";
    const std::string err =
        "error: Function 'add' is missing a return statement on some paths\n"
        "   --> undefined_behavior.fin:3:1\n";
    EXPECT_EQ(checkDiagAgreement("undefined_behavior", ExpectationKind::Error, &pin, 1, err, 0, ""),
              DiagVerdict::FateDivergence);
    // Same fate, but the stage diagnostic names something else entirely.
    const std::string other =
        "error: something else went wrong\n"
        "   --> undefined_behavior.fin:9:4\n";
    EXPECT_EQ(checkDiagAgreement("undefined_behavior", ExpectationKind::Error, &pin, 1, err, 1, other),
              DiagVerdict::MessageDivergence);
}

TEST(StageAgreementLogic, StageRefusingAtADifferentPlaceDiverges) {
    // Message matches, position does not: still red.
    Expectation pin;
    pin.kind = ExpectationKind::Error;
    pin.line = 11;
    pin.column = 16;
    pin.text = "field 'v' has union type 'Number'";
    const std::string cpp =
        "error: field 'v' has union type 'Number'\n"
        "   --> union_pointer_map.fin:11:16\n";
    const std::string stage =
        "error: field 'v' has union type 'Number'\n"
        "   --> union_pointer_map.fin:11:20\n";
    EXPECT_EQ(checkDiagAgreement("union_pointer_map", ExpectationKind::Error, &pin, 1, cpp, 1, stage),
              DiagVerdict::MessageDivergence);
}

TEST(StageAgreementLogic, MatchingErrorRefusalsAgreeWithoutByteExactStderr) {
    // union_pointer_map's live shape: same message and position, but the caret
    // span differs (`^^^ here` vs `^ here`), so agreement must NOT be
    // byte-exact here.
    Expectation pin;
    pin.kind = ExpectationKind::Error;
    pin.line = 11;
    pin.column = 16;
    pin.text = "field 'v' has union type 'Number'";
    const std::string cpp =
        "error: field 'v' has union type 'Number'\n"
        "   --> union_pointer_map.fin:11:16\n"
        "    |                ^^^^^^^^^^^ here\n";
    const std::string stage =
        "error: field 'v' has union type 'Number'\n"
        "   --> union_pointer_map.fin:11:16\n"
        "    |                ^ here\n";
    EXPECT_EQ(checkDiagAgreement("union_pointer_map", ExpectationKind::Error, &pin, 1, cpp, 1, stage),
              DiagVerdict::Agree);
}

TEST(StageAgreementLogic, AllowlistedWordingWaivesMessageButNotPosition) {
    // block_scope_use_after's live shape: same 7:5, different message.
    Expectation pin;
    pin.kind = ExpectationKind::Error;
    pin.line = 7;
    pin.column = 5;
    pin.text = "Undefined variable 'b'";
    const std::string cpp =
        "error: Undefined variable 'b'\n"
        "   --> block_scope_use_after.fin:7:5\n";
    const std::string stage =
        "error: use of undeclared identifier\n"
        "   --> block_scope_use_after.fin:7:5\n";
    EXPECT_EQ(checkDiagAgreement("block_scope_use_after", ExpectationKind::Error, &pin, 1, cpp, 1,
                                 stage),
              DiagVerdict::ToleratedWording);
    // A different place is red even allowlisted: the waiver covers wording.
    const std::string elsewhere =
        "error: use of undeclared identifier\n"
        "   --> block_scope_use_after.fin:7:9\n";
    EXPECT_EQ(checkDiagAgreement("block_scope_use_after", ExpectationKind::Error, &pin, 1, cpp, 1,
                                 elsewhere),
              DiagVerdict::MessageDivergence);
    // And an allowlist that stops matching is stale, like any other entry.
    EXPECT_EQ(checkDiagAgreement("block_scope_use_after", ExpectationKind::Error, &pin, 1, cpp, 1,
                                 cpp),
              DiagVerdict::StaleAllowlist);
}

TEST(StageAgreementLogic, UnimplementedComparesRefusalText) {
    const std::string refusal =
        "error: Undefined type 'Strict'\n"
        "   --> stdlib/stdio.fin:49:22\n";
    EXPECT_EQ(checkDiagAgreement("stdio", ExpectationKind::Unimplemented, nullptr, 1, refusal, 1,
                                 refusal),
              DiagVerdict::Agree);
    const std::string other =
        "error: Undefined type 'SomethingElse'\n"
        "   --> stdlib/stdio.fin:49:22\n";
    EXPECT_EQ(checkDiagAgreement("stdio", ExpectationKind::Unimplemented, nullptr, 1, refusal, 1,
                                 other),
              DiagVerdict::MessageDivergence);
    // importing.fin's live shape: from the repo root both compilers accept, so
    // both-accept with identical stderr agrees; the pin stays the expectation
    // suite's business.
    const std::string warning =
        "warning: unused variable 'y' is never read\n"
        "   --> importing.fin:23:5\n";
    EXPECT_EQ(checkDiagAgreement("importing", ExpectationKind::Unimplemented, nullptr, 0, warning, 0,
                                 warning),
              DiagVerdict::Agree);
    EXPECT_EQ(checkDiagAgreement("importing", ExpectationKind::Unimplemented, nullptr, 0, warning, 1,
                                 refusal),
              DiagVerdict::FateDivergence);
}

// ---------------------------------------------------------------------------
// The per-sample run agreement, through the reference compiler and one stage
// compiler. StageAgreement covers stage2; Stage3Agreement (issue #35) mirrors
// it over stage3. Both share checkAgreementForStage below; only the stage
// binary differs, so a stage3 divergence fails exactly like a stage2 one.
// ---------------------------------------------------------------------------

class StageAgreement : public ::testing::TestWithParam<Sample> {};
class Stage3Agreement : public ::testing::TestWithParam<Sample> {};

void checkAgreementForStage(const std::string& path, const std::string& stem,
                            const std::string& stageBin, const std::string& exeTag) {
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
        const std::string firstErr = cppBuild.err.substr(0, cppBuild.err.find('\n'));
        GTEST_SKIP() << "reference (C++) build refuses " << stem << " (exit " << cppBuild.exitCode
                     << "); not runnable; first stderr line: " << firstErr;
    }

    // Stage build, serialized: the stage cross-links concurrent `-o` builds
    // through its fixed /tmp object (see StageBuildLock).
    const fs::path stageExe = uniqueTempPath(exeTag);
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

TEST_P(StageAgreement, RunAgreesAcrossCompilers) {
    const std::string path = GetParam().path;
    const std::string stem = fs::path(path).stem().string();

    const std::string stageBin = stageBinary();
    if (stageBin.empty() || !fs::exists(stageBin)) {
        GTEST_SKIP() << "stage compiler not found at FINC_STAGE2_BINARY=" << stageBin
                     << "; run-agreement needs build/finc_stage2 next to build/finc";
    }
    checkAgreementForStage(path, stem, stageBin, "stageagree_stage");
}

TEST_P(Stage3Agreement, RunAgreesAcrossStage3) {
    const std::string path = GetParam().path;
    const std::string stem = fs::path(path).stem().string();

    const std::string stageBin = stage3Binary();
    if (stageBin.empty() || !fs::exists(stageBin)) {
        GTEST_SKIP() << "stage3 compiler not found at FINC_STAGE3_BINARY=" << stageBin
                     << "; stage3-agreement needs build/finc_stage3 next to build/finc";
    }
    checkAgreementForStage(path, stem, stageBin, "stageagree_stage3");
}

INSTANTIATE_TEST_SUITE_P(
    Corpus,
    StageAgreement,
    ::testing::ValuesIn(agreementSamples()),
    [](const testing::TestParamInfo<Sample>& info) { return testNameForSample(info.param.path); });

INSTANTIATE_TEST_SUITE_P(
    Corpus,
    Stage3Agreement,
    ::testing::ValuesIn(agreementSamples()),
    [](const testing::TestParamInfo<Sample>& info) { return testNameForSample(info.param.path); });

// ---------------------------------------------------------------------------
// The per-sample diagnostic agreement (issue #76): compile-only, both
// compilers from the repo root, exit code plus pinned diagnostic or refusal
// text. StageDiagAgreement covers stage2; Stage3DiagAgreement mirrors it over
// stage3. GTEST_SKIP only for absent binaries: a refusal on either side is the
// expected shape here, never a skip.
// ---------------------------------------------------------------------------

class StageDiagAgreement : public ::testing::TestWithParam<DiagSample> {};
class Stage3DiagAgreement : public ::testing::TestWithParam<DiagSample> {};

// One exit-code encoding for both compilers: a crash is 128+signal (the shell
// convention the expectation runner documents), so two sides dying the same
// way agree and different deaths disagree. -1 is unlaunched, never a fate.
int diagExitCode(const Proc& p) {
    if (p.exited) return p.exitCode;
    if (p.signaled) return 128 + p.termSig;
    return -1;
}

void checkDiagAgreementForStage(const DiagSample& sample, const std::string& stageBin) {
    const std::string stem = fs::path(sample.path).stem().string();

    // Same tree for both compilers (stage resolves packages via CWD).
    ScopedRepoRoot repoRoot;
    if (!repoRoot.ok()) FAIL() << "cannot chdir to the repo root; stage imports would misresolve";

    // Compile-only on both sides; serialized like the `-o` builds above.
    const Proc cpp = spawnCapture(fincBinary(), {sample.path, "--color=never"});
    Proc stageBuild;
    {
        StageBuildLock lock;
        stageBuild = spawnCapture(stageBin, {sample.path, "--color=never"});
    }
    const Expectation* pin = sample.kind == ExpectationKind::Error ? &sample.pin : nullptr;
    switch (checkDiagAgreement(stem, sample.kind, pin, diagExitCode(cpp), stripAnsi(cpp.err),
                               diagExitCode(stageBuild), stripAnsi(stageBuild.err))) {
        case DiagVerdict::Agree:
            break;
        case DiagVerdict::FateDivergence:
            FAIL() << stem << ": compile fate diverges: C++ " << procFate(cpp) << " vs stage "
                   << procFate(stageBuild) << "\nC++ stderr:\n"
                   << cpp.err << "stage stderr:\n"
                   << stageBuild.err;
            break;
        case DiagVerdict::MessageDivergence:
            FAIL() << stem << ": diagnostic diverges (both " << procFate(cpp) << ")\nC++ stderr:\n"
                   << cpp.err << "stage stderr:\n"
                   << stageBuild.err;
            break;
        case DiagVerdict::ToleratedWording: {
            const AllowEntry* e = lookupAllow(stem, "stderr-wording");
            ::testing::Test::RecordProperty("allowlisted", e->reason);
            break;
        }
        case DiagVerdict::StaleAllowlist:
            FAIL() << stem << ": diagnostics now agree -- delete the one allowlist line";
            break;
    }
}

TEST_P(StageDiagAgreement, DiagAgreesAcrossCompilers) {
    const std::string stageBin = stageBinary();
    if (stageBin.empty() || !fs::exists(stageBin)) {
        GTEST_SKIP() << "stage compiler not found at FINC_STAGE2_BINARY=" << stageBin
                     << "; diag-agreement needs build/finc_stage2 next to build/finc";
    }
    checkDiagAgreementForStage(GetParam(), stageBin);
}

TEST_P(Stage3DiagAgreement, DiagAgreesAcrossStage3) {
    const std::string stageBin = stage3Binary();
    if (stageBin.empty() || !fs::exists(stageBin)) {
        GTEST_SKIP() << "stage3 compiler not found at FINC_STAGE3_BINARY=" << stageBin
                     << "; stage3 diag-agreement needs build/finc_stage3 next to build/finc";
    }
    checkDiagAgreementForStage(GetParam(), stageBin);
}

INSTANTIATE_TEST_SUITE_P(
    Corpus,
    StageDiagAgreement,
    ::testing::ValuesIn(diagnosticSamples()),
    [](const testing::TestParamInfo<DiagSample>& info) { return testNameForSample(info.param.path); });

INSTANTIATE_TEST_SUITE_P(
    Corpus,
    Stage3DiagAgreement,
    ::testing::ValuesIn(diagnosticSamples()),
    [](const testing::TestParamInfo<DiagSample>& info) { return testNameForSample(info.param.path); });
