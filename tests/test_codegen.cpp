#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Corpus.hpp"
#include "types/Layout.hpp"
#include "types/PrimitiveType.hpp"
#include "types/StructType.hpp"
#include "utils/Process.hpp"

#ifdef FIN_TESTS_HAVE_BACKEND
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#endif

// Wave 5, from the first artifact onwards.
//
// The exit criterion docs/plan.md sets for this wave is one shell line --
// `finc hello.fin -o hello && ./hello` -- and until it holds nothing in the front
// end has ever been executed. Every other suite in this repository asserts what
// finc *says*; this one asserts what the program finc produced *does*, which is
// the only check that catches a lowering that type-checks and computes the wrong
// answer.
//
// So each test here compiles a string to a real executable, runs it, and asserts
// on its stdout and its exit status. `printf` is declared the way the corpus
// declares it -- `@define printf(fmt: string, ...) <noret>;`, tests/samples/
// functions.fin:3 -- so observing a value costs no invented syntax.
//
// The suite convention is the one test_soundness.cpp documents: Soundness_* must
// always pass, KnownDefect_* asserts what is wrong today and a failure is good
// news. Codegen gets a third obligation the front end does not have, and it is
// the reason UnsupportedConstructs exists below: a construct the backend cannot
// lower must be *refused*, never skipped. A silently dropped statement is a
// miscompile, and a miscompile is worse than an unimplemented feature by exactly
// the margin that makes it hard to find.

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

// The compile and the run, together, because for a backend test neither half is
// evidence alone: a compile that succeeds and produces a binary that crashes is
// the failure this suite exists to catch, and a run that is never reached would
// otherwise read as a pass.
struct Built {
    int compileExit = -1;
    std::string compileErr;
    bool ran = false;
    int runExit = -1;
    std::string out;

    // The path the program was compiled from. Kept because a failed `blame` prints
    // it, so a test that pins the location whole needs the same spelling the driver
    // was handed -- and the harness writes to a unique temporary, so that spelling
    // is not a constant any assertion could hold.
    std::string srcPath;

    // The message an assertion should print. Both halves, always: which one is
    // interesting depends on where it went wrong.
    std::string why() const {
        return "compile exit " + std::to_string(compileExit) + "\n" + compileErr +
               (ran ? "\nrun exit " + std::to_string(runExit) + ", stdout:\n" + out
                    : "\n(never ran)");
    }
};

Built build(const std::string& code) {
    Built b;
    fs::path src = uniqueTempPath("fin_cg", ".fin");
    fs::path exe = uniqueTempPath("fin_cg_exe");
    b.srcPath = src.string();
    {
        std::ofstream f(src, std::ios::binary);
        f.write(code.data(), (std::streamsize)code.size());
    }

    const FincRun c = runFinc({src.string(), "-o", exe.string()});
    b.compileExit = c.exitCode;
    b.compileErr = stripAnsi(c.err);

    if (b.compileExit == 0 && fs::exists(exe)) {
        fs::path outPath = uniqueTempPath("fin_cg_out");
        b.runExit = fin::runProcess({exe.string()}, outPath.string(), outPath.string());
        b.ran = true;
        b.out = readProcessOutput(outPath.string());
        std::error_code ec;
        fs::remove(outPath, ec);
    }

    std::error_code ec;
    fs::remove(src, ec);
    fs::remove(exe, ec);
    return b;
}

// tests/samples/functions.fin:3 verbatim.
const char* const kPrintf = "@define printf(fmt: string, ...) <noret>;\n";

}  // namespace

#ifndef FIN_TESTS_HAVE_BACKEND
// A build configured without the backend still has to compile this file, so the
// tests exist and say why they did not run rather than vanishing from the count.
//
// The trailing declaration is what makes that true, and it was missing until wave
// 4 step 6 found it: `TEST(s, n) { skip; }` followed by the test's own `{ ... }`
// leaves a compound statement at namespace scope, which is not C++ -- so this
// file did not compile at all with FIN_WITH_LLVM=OFF, and the switch nobody
// exercises is exactly the switch that breaks. The body becomes the body of an
// uncalled function instead, which keeps it *compiled* in an OFF build: a test
// body that stops compiling should be a build error either way, not something a
// configuration hides. A body that needs LLVM's own headers cannot live behind
// this macro at all -- see the DataLayout tests at the end of the file.
#define BACKEND_TEST(suite, name)                                          \
    TEST(suite, name) { GTEST_SKIP() << "built with FIN_WITH_LLVM=OFF"; }  \
    [[maybe_unused]] static void backend_body_##suite##_##name()
#else
#define BACKEND_TEST(suite, name) TEST(suite, name)
#endif

// ---------------------------------------------------------------------------
// The exit criterion.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, HelloWorldRunsAndPrints) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"Hello, World!\\n\");\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "Hello, World!\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEmptyMainExitsZero) {
    const Built b = build("fun main() <noret> { }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, MainsIntReturnIsTheProcessExitStatus) {
    // `fun main() <int>` type-checks today, and a process exit status is the one
    // observable a program has without a library. 7 rather than 0 so that "it
    // exited" and "it exited with what main returned" are different results.
    const Built b = build("fun main() <int> { return 7; }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 7) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AProgramWithNoMainSaysSoRatherThanFailingToLink) {
    // `-o` is a request for an executable and an executable needs an entry point.
    // Before this the object was emitted and handed to `cc`, which reported
    // "undefined reference to `main`" from inside Scrt1.o -- a C diagnostic about a
    // C file, for a Fin program, followed by finc's own help blaming the C toolchain
    // and suggesting FIN_CC. Four corpus samples reach it (macros.fin, macros2.fin,
    // macro_definitions.fin, stdlib/networking.fin), and every
    // one of them is a file with no main rather than a broken toolchain.
    const Built b = build(std::string(kPrintf) +
        "fun helper() <void> {\n"
        "    printf(\"never called\\n\");\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("main"), std::string::npos) << b.why();
    // Specifically not the linker's answer, and specifically not finc's help about
    // the C toolchain: the program is what is wrong here.
    EXPECT_EQ(b.compileErr.find("undefined reference"), std::string::npos) << b.why();
    EXPECT_EQ(b.compileErr.find("FIN_CC"), std::string::npos) << b.why();
}

// ---------------------------------------------------------------------------
// `-c`, which is the other thing a backend is for: an object file and no link.
//
// finc had exactly two modes -- check, and build-an-executable -- and a Fin file
// that is a library fits neither. Ten of the corpus's front-end-clean samples have
// no `main` (macros.fin, stdlib/networking.fin, struct_methods.fin and the rest),
// which under `-o` is now a diagnostic and under no flag at all is never handed to
// the backend, so nothing measured whether their declarations lower. `-c` is the
// mode that asks. It is also what a build system needs before it can compile two
// files and link them once, which is the shape `finn` will want.
//
// The object's name follows cc: `-c -o <path>` puts it at <path>, and `-c` alone
// puts <stem>.o in the working directory.
// ---------------------------------------------------------------------------

namespace {

// A compile that stops at the object. Returns the object's path (empty if the
// compile failed) so a test can link it, size it, or check it is absent.
struct Compiled {
    int exitCode = -1;
    std::string err;
    fs::path object;

    std::string why() const {
        return "exit " + std::to_string(exitCode) + ", object " + object.string() + "\n" + err;
    }
};

// The backend's own account of what it lowered, which is the only way from out
// here to see a name that never becomes a symbol. An llvm::StructType's name is
// not in the object file at all -- LLVM types are structural, and the name is
// there for the IR reader -- so a test that wants to check `#[llvm_name]` on a
// struct has to ask the compiler what it called the type.
std::string codegenTrace(const std::string& code) {
    fs::path srcPath = uniqueTempPath("fin_tr", ".fin");
    fs::path obj = uniqueTempPath("fin_tr", ".o");
    {
        std::ofstream f(srcPath, std::ios::binary);
        f.write(code.data(), (std::streamsize)code.size());
    }
    const FincRun r = runFinc({srcPath.string(), "-c", "-o", obj.string(),
                               "--debug-codegen"});
    std::error_code ec;
    fs::remove(srcPath, ec);
    fs::remove(obj, ec);
    return stripAnsi(r.err);
}

// How many times `needle` appears in `haystack`. Used where the interesting fact is
// a count and not a presence: a template instantiated twice at one type has to be
// emitted *once*, and a test that only looked for the name would pass either way.
size_t occurrences(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return 0;
    size_t n = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size())) {
        ++n;
    }
    return n;
}

Compiled compileOnly(const std::string& code, const fs::path& objectPath = {}) {
    Compiled c;
    fs::path srcPath = uniqueTempPath("fin_c", ".fin");
    {
        std::ofstream f(srcPath, std::ios::binary);
        f.write(code.data(), (std::streamsize)code.size());
    }
    std::vector<std::string> args{srcPath.string(), "-c"};
    if (!objectPath.empty()) { args.push_back("-o"); args.push_back(objectPath.string()); }

    const FincRun r = runFinc(args);
    c.exitCode = r.exitCode;
    c.err = stripAnsi(r.err);
    c.object = objectPath.empty() ? fs::path(srcPath.stem().string() + ".o") : objectPath;

    std::error_code ec;
    fs::remove(srcPath, ec);
    return c;
}

}  // namespace

BACKEND_TEST(Soundness_Codegen, ACompileOnlyBuildEmitsAnObjectForALibrary) {
    // No `main`, which under `-o` is a diagnostic and here is simply not relevant:
    // nothing is being linked, so nothing needs an entry point.
    const fs::path obj = uniqueTempPath("fin_obj", ".o");
    const Compiled c = compileOnly(std::string(kPrintf) +
        "fun twice(n: int) <int> { return n * 2; }\n", obj);
    EXPECT_EQ(c.exitCode, 0) << c.why();
    EXPECT_EQ(c.err.find("main"), std::string::npos) << c.why();
    ASSERT_TRUE(fs::exists(obj)) << c.why();
    EXPECT_GT(fs::file_size(obj), 0u) << c.why();
    std::error_code ec;
    fs::remove(obj, ec);
}

BACKEND_TEST(Soundness_Codegen, ACompileOnlyBuildNamesTheObjectAfterTheInput) {
    // `-c` with no `-o`: <stem>.o in the working directory, which is cc's rule and
    // the one a Makefile already assumes.
    const Compiled c = compileOnly("fun twice(n: int) <int> { return n * 2; }\n");
    EXPECT_EQ(c.exitCode, 0) << c.why();
    ASSERT_TRUE(fs::exists(c.object)) << c.why();
    EXPECT_GT(fs::file_size(c.object), 0u) << c.why();
    std::error_code ec;
    fs::remove(c.object, ec);
}

#if defined(__unix__) || defined(__APPLE__)
namespace {

// The port of the link step to fin::linkCommand dropped the libraries the
// self-host compiler needs at link time: `cc obj -o out` links a plain sample
// but a program that calls the LLVM C API fails with `undefined reference to
// LLVMBuildCall2`. The pre-port Driver::runLinker always appended
// `-Wl,--as-needed -lLLVM-<major> -lm` unless FIN_LDFLAGS overrode it; the two
// tests below pin both halves of that contract on the argv itself.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        const char* cur = std::getenv(name);
        had_ = cur != nullptr;
        if (had_) old_ = cur;
        if (value != nullptr) ::setenv(name, value, 1);
        else ::unsetenv(name);
    }
    ~ScopedEnv() {
        if (had_) ::setenv(name_.c_str(), old_.c_str(), 1);
        else ::unsetenv(name_.c_str());
    }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    std::string name_;
    std::string old_;
    bool had_ = false;
};

std::string expectedLlvmLib() {
#ifdef FIN_LLVM_MAJOR
#define FIN_TEST_STRINGIFY_HELPER(x) #x
#define FIN_TEST_STRINGIFY(x) FIN_TEST_STRINGIFY_HELPER(x)
    return "-lLLVM-" FIN_TEST_STRINGIFY(FIN_LLVM_MAJOR);
#undef FIN_TEST_STRINGIFY
#undef FIN_TEST_STRINGIFY_HELPER
#else
    return "-lLLVM-22";
#endif
}

}  // namespace

TEST(LinkCommand, AppendsLlvmLibrariesAfterTheOutput) {
    ScopedEnv cc("FIN_CC", nullptr);
    ScopedEnv ldflags("FIN_LDFLAGS", nullptr);
    const std::vector<std::string> expected{"cc", "a.o", "-o", "a.out",
                                            "-Wl,--as-needed", expectedLlvmLib(), "-lm"};
    EXPECT_EQ(fin::linkCommand({"a.o"}, "a.out"), expected);
}

TEST(LinkCommand, FinLdflagsReplacesTheDefaultLibraries) {
    ScopedEnv cc("FIN_CC", nullptr);
    ScopedEnv ldflags("FIN_LDFLAGS", "-L/opt/custom -lcustom");
    const std::vector<std::string> expected{"cc", "a.o", "-o",  "a.out",
                                            "-L/opt/custom", "-lcustom"};
    EXPECT_EQ(fin::linkCommand({"a.o"}, "a.out"), expected);
}
#endif

BACKEND_TEST(Soundness_Codegen, TwoObjectsFromCompileOnlyLinkIntoAProgram) {
    // The whole point, and the only test that proves the object is a real one: a
    // library compiled alone, a main compiled alone, linked by cc, run. It also
    // proves the names are what the other file thinks they are -- Fin does not
    // mangle, so `twice` in one object is the `twice` the other one calls.
    const fs::path libObj = uniqueTempPath("fin_lib", ".o");
    const fs::path mainObj = uniqueTempPath("fin_main", ".o");
    const fs::path exe = uniqueTempPath("fin_linked");

    const Compiled lib = compileOnly("fun twice(n: int) <int> { return n * 2; }\n", libObj);
    ASSERT_EQ(lib.exitCode, 0) << lib.why();
    const Compiled mainPart = compileOnly(std::string(kPrintf) +
        "@define twice(n: int) <int>;\n"
        "fun main() <noret> { printf(\"%d\\n\", twice(21)); }\n", mainObj);
    ASSERT_EQ(mainPart.exitCode, 0) << mainPart.why();

    const fs::path outPath = uniqueTempPath("fin_linked_out");
    ASSERT_EQ(fin::runProcess(fin::linkCommand(
        {libObj.string(), mainObj.string()}, exe.string())), 0);

    EXPECT_EQ(fin::runProcess({exe.string()}, outPath.string(), outPath.string()), 0);
    EXPECT_EQ(readProcessOutput(outPath.string()), "42\n");

    std::error_code ec;
    for (const fs::path& p : {libObj, mainObj, exe, outPath}) fs::remove(p, ec);
}

BACKEND_TEST(Soundness_Codegen, ACompileOnlyBuildStillRefusesWhatItCannotLower) {
    // `-c` is not a way around the refusals, and a failed compile leaves no object
    // -- a stale one is a link that succeeds against yesterday's code.
    const fs::path obj = uniqueTempPath("fin_obj_bad", ".o");
    const Compiled c = compileOnly(
        "fun bad() <noret> { m1778; }\n"
        "fun make() <int> { return 1; }\n", obj);
    EXPECT_NE(c.exitCode, 0) << c.why();
    EXPECT_NE(c.err.find("codegen"), std::string::npos) << c.why();
    EXPECT_FALSE(fs::exists(obj)) << c.why();
    std::error_code ec;
    fs::remove(obj, ec);
}

BACKEND_TEST(Soundness_Codegen, ACompileOnlyBuildOfABrokenProgramStopsAtTheDiagnostic) {
    // The front end still runs first: `-c` reaches the backend only for a program
    // that checked clean.
    const fs::path obj = uniqueTempPath("fin_obj_sema", ".o");
    const Compiled c = compileOnly("fun main() <noret> { let x <int> = \"no\"; }\n", obj);
    EXPECT_NE(c.exitCode, 0) << c.why();
    EXPECT_FALSE(fs::exists(obj)) << c.why();
    std::error_code ec;
    fs::remove(obj, ec);
}

BACKEND_TEST(Soundness_Codegen, ACheckWithNoOutputPathNeedsNoMain) {
    // `finc x.fin` is a check and not a build (Driver::runCodeGen's first line), so a
    // library -- which is what a corpus file with no main is -- still checks clean.
    // This is what keeps the diagnostic above from turning every such sample into a
    // corpus failure.
    const fs::path src = uniqueTempPath("fin_nomain", ".fin");
    {
        std::ofstream f(src, std::ios::binary);
        const std::string code = "fun helper() <void> { }\n";
        f.write(code.data(), (std::streamsize)code.size());
    }
    const FincRun c = runFinc({src.string()});
    EXPECT_EQ(c.exitCode, 0) << stripAnsi(c.err);
    std::error_code ec;
    fs::remove(src, ec);
}

// ---------------------------------------------------------------------------
// The two backend options, which the backend honoured before anything could ask
// for them: `optLevel` and `debugCodegen` were fields set by nobody.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AnOptimisedBuildRunsTheSameProgram) {
    // -O2 runs LLVM's own pipeline at that level over the module. What is asserted
    // is the only thing an optimisation level may change: nothing observable. The
    // arithmetic is written so that a folded program and an unfolded one both have to
    // print 30.
    const fs::path src = uniqueTempPath("fin_opt", ".fin");
    const fs::path exe = uniqueTempPath("fin_opt_exe");
    {
        std::ofstream f(src, std::ios::binary);
        const std::string code = std::string(kPrintf) +
            "fun triple(n: int) <int> { return n * 3; }\n"
            "fun main() <noret> {\n"
            "    let total <int> = 0;\n"
            "    for (i: int = 1; i <= 4; i++) { total = total + triple(i); }\n"
            "    printf(\"%d\\n\", total);\n"
            "}\n";
        f.write(code.data(), (std::streamsize)code.size());
    }
    const FincRun c = runFinc({src.string(), "-O2", "-o", exe.string()});
    EXPECT_EQ(c.exitCode, 0) << stripAnsi(c.err);
    ASSERT_TRUE(fs::exists(exe)) << stripAnsi(c.err);

    const fs::path outPath = uniqueTempPath("fin_opt_out");
    EXPECT_EQ(fin::runProcess({exe.string()}, outPath.string(), outPath.string()), 0);
    EXPECT_EQ(readProcessOutput(outPath.string()), "30\n");

    std::error_code ec;
    fs::remove(src, ec);
    fs::remove(exe, ec);
    fs::remove(outPath, ec);
}

BACKEND_TEST(Soundness_Codegen, AnUnknownOptimisationLevelIsAUsageError) {
    // Spelled out rather than parsed as a number: `-O9` means nothing, and a flag
    // that silently means something else is the failure mode a programmatic caller
    // cannot see. Exit 2 is Usage (ADR 0009).
    const FincRun c = runFinc({"-O9", "nonexistent.fin"});
    EXPECT_EQ(c.exitCode, 2) << stripAnsi(c.err);
    EXPECT_NE(stripAnsi(c.err).find("-O9"), std::string::npos) << stripAnsi(c.err);
}

BACKEND_TEST(Soundness_Codegen, DebugCodegenSaysWhatItLowered) {
    const fs::path src = uniqueTempPath("fin_dbg", ".fin");
    const fs::path exe = uniqueTempPath("fin_dbg_exe");
    {
        std::ofstream f(src, std::ios::binary);
        const std::string code = "fun main() <noret> { }\n";
        f.write(code.data(), (std::streamsize)code.size());
    }
    const FincRun c = runFinc({src.string(), "--debug-codegen", "-o", exe.string()});
    EXPECT_EQ(c.exitCode, 0) << stripAnsi(c.err);
    const std::string err = stripAnsi(c.err);
    EXPECT_NE(err.find("[codegen]"), std::string::npos) << err;
    // The link command too, which is what makes a FIN_CC problem diagnosable.
    EXPECT_NE(err.find("-o"), std::string::npos) << err;
    std::error_code ec;
    fs::remove(src, ec);
    fs::remove(exe, ec);
}

// ---------------------------------------------------------------------------
// Values computed at run time. Each of these would pass on a backend that
// printed a constant, so each prints something the front end could not have
// folded and the expected text is written out in full.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, ArithmeticIsComputed) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <int> = 2;\n"
        "    let b <int> = 3;\n"
        "    printf(\"%d %d %d %d %d\\n\", a + b, a - b, a * b, b / a, b % a);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5 -1 6 1 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, PrecedenceSurvivesLowering) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", 2 + 3 * 4);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "14\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ALocalIsStoredAndReloaded) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    x = x + 41;\n"
        "    printf(\"%d\\n\", x);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ACompoundAssignmentUpdatesInPlace) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <int> = 10;\n"
        "    x += 5;\n"
        "    x -= 3;\n"
        "    x *= 2;\n"
        "    printf(\"%d\\n\", x);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "24\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFunctionCallReturnsItsValue) {
    const Built b = build(std::string(kPrintf) +
        "fun add(a: int, b: int) <int> { return a + b; }\n"
        "fun main() <noret> { printf(\"%d\\n\", add(20, 22)); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ACallAboveItsDeclarationLinks) {
    // The front-end half of this is 6f48a89. A hoisted name has to reach the
    // backend as well, or the artifact fails to link on a program that checks.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> { printf(\"%d\\n\", later()); }\n"
        "fun later() <int> { return 9; }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnIfChoosesOneBranch) {
    const Built b = build(std::string(kPrintf) +
        "fun sign(n: int) <int> {\n"
        "    if (n > 0) { return 1; } else { if (n < 0) { return 0 - 1; } }\n"
        "    return 0;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d %d\\n\", sign(5), sign(0 - 5), sign(0));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 -1 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWhileLoopIterates) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let i <int> = 0;\n"
        "    let sum <int> = 0;\n"
        "    while (i < 5) { sum += i; i += 1; }\n"
        "    printf(\"%d %d\\n\", i, sum);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5 10\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, RecursionWorks) {
    const Built b = build(std::string(kPrintf) +
        "fun fact(n: int) <int> {\n"
        "    if (n <= 1) { return 1; }\n"
        "    return n * fact(n - 1);\n"
        "}\n"
        "fun main() <noret> { printf(\"%d\\n\", fact(10)); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3628800\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, MutualRecursionLinks) {
    const Built b = build(std::string(kPrintf) +
        "fun even(n: int) <int> { if (n == 0) { return 1; } return odd(n - 1); }\n"
        "fun odd(n: int) <int> { if (n == 0) { return 0; } return even(n - 1); }\n"
        "fun main() <noret> { printf(\"%d %d\\n\", even(10), even(7)); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ComparisonsAndLogicalOperators) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let t <bool> = true;\n"
        "    let f <bool> = false;\n"
        "    printf(\"%d %d %d %d\\n\", 1 < 2, 2 <= 2, t && f, t || f);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 1 0 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, LogicalAndShortCircuits) {
    // Not a micro-optimisation: `f() && g()` that evaluates both sides is a
    // different program, and the only way to see it is a side effect.
    const Built b = build(std::string(kPrintf) +
        "fun no() <bool> { printf(\"no\\n\"); return false; }\n"
        "fun yes() <bool> { printf(\"yes\\n\"); return true; }\n"
        "fun main() <noret> {\n"
        "    if (no() && yes()) { printf(\"both\\n\"); }\n"
        "    if (yes() || no()) { printf(\"first\\n\"); }\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "no\nyes\nfirst\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, UnaryMinusAndNot) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let n <int> = 5;\n"
        "    let t <bool> = true;\n"
        "    printf(\"%d %d\\n\", -n, !t);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "-5 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, FloatsAreDoublesAtTheVarargBoundary) {
    // A C variadic promotes a float argument to double, so a backend that passed
    // an f32 would print garbage rather than fail. That is exactly the class of
    // bug only a run catches -- and it needs one of each width, because promoting
    // the double a second time would be just as wrong.
    //
    // The double comes from a cast rather than from `let y <double> = 2.25;`,
    // which the analyzer refuses today: a bare float literal is `float` and there
    // is no implicit widening to `double`. Whether that is right is the same open
    // question as the integer conversions (KnownDefect_IntegerConstants), and it
    // is not this test's to settle -- so the test is written in the language as it
    // is, and the cast is doing nothing at run time here beyond naming the width.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <float> = 1.5;\n"
        "    let y <double> = cast<double>(2.25);\n"
        "    printf(\"%.2f %.2f\\n\", x, y);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1.50 2.25\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, StringsAreNulTerminatedData) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let s <string> = \"world\";\n"
        "    printf(\"hello %s\\n\", s);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "hello world\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEscapeIsLoweredOnce) {
    // `Literal::value` is the lexeme: lexer.l:308 hands `yytext` through with its
    // quotes and its backslashes intact, and the parser stores it unchanged. So
    // the backend is what strips and decodes -- exactly once. Decoding twice, or
    // not at all, both produce a program that prints a backslash and an `n`.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> { printf(\"a\\tb\\n\"); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "a\tb\n") << b.why();
}

// ---------------------------------------------------------------------------
// The obligation that is specific to a backend: refuse, never skip.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AnUnloweredConstructIsRefused) {
    const Built b = build(
        "fun main() <noret> { m1778; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ARefusalNamesTheLine) {
    // A refusal with no location is a refusal the reader has to go looking for,
    // and in a file of any size that is most of the cost of the error. The
    // construct here is below line 1 so that a location the backend simply left
    // default would not pass by accident.
    //
    // It used to be `i++`, then a generic struct, then an interface, which is exactly
    // the trap AnUnloweredConstructIsRefused warns about one screen above: each of
    // those lowered in turn, and each time this test went from asserting a located
    // refusal to asserting nothing -- failing rather than passing vacuously only
    // because it checks the exit code too. The construct is a class for the same
    // reason that one uses it: a class is a struct, inheritance, a vtable and a
    // destructor at once, so it is the furthest thing in this file from being lowered.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let i <int> = 1;\n"
        "    m1778;\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find(".fin:3:"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASpecialCallRefusalNamesTheCallee) {
    // Wave-4 slice 1a stops at parse + check: lowering `@`-calls is a later
    // slice, so the backend refuses with a diagnostic naming the construct
    // rather than silently dropping the call.
    const fs::path obj = uniqueTempPath("fin_obj_special", ".o");
    const Compiled c = compileOnly(
        "@special(pub) sp(v: int) <int> { return v; }\n"
        "fun main() <noret> { const x <int> = @sp(1); }\n", obj);
    EXPECT_NE(c.exitCode, 0) << c.why();
    EXPECT_NE(c.err.find("the compile-time call '@sp'"), std::string::npos) << c.why();
    EXPECT_FALSE(fs::exists(obj)) << c.why();
    std::error_code ec;
    fs::remove(obj, ec);
}

// ---------------------------------------------------------------------------
// Where a variable's refusal is reported.
//
// The suite is Soundness_DiagnosticLocation, whose other cases live in
// test_soundness.cpp; these three belong here because only the backend refuses a
// written type that the analyzer accepted, so only a codegen probe can ask where
// that refusal lands.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_DiagnosticLocation, AVariablesRefusalIsLocatedAtTheDeclaration) {
    // A plain `let` inside a function had no location at all, so its refusal was
    // reported at 1:1 -- and for every sample in tests/samples that is the `//@`
    // expectation comment, which is the one line in the file that is not code. The
    // reader was sent to a comment to find a type written twelve lines below it.
    //
    // The cause was in the grammar rather than in the backend: a `let` is reachable
    // both as a `variable_declaration` statement and as a `declaration_body`, the
    // two paths carry six duplicated productions each, and only the statement copies
    // called setLoc (src/parser/parser.y). `object` is the type because it is
    // refused for a reason that is not waiting on anything -- there is no
    // representation for a value whose type is unknown at compile time -- so
    // this test cannot quietly stop testing a location the way
    // ARefusalNamesTheLine above did three times. (`any` served here until it
    // mapped as an opaque blob; `object` is its still-unmapped sibling.)
    const Built b = build(
        "fun main() <noret> {\n"
        "    let i <int> = 1;\n"
        "    let v <int{7}>;\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a variable of type 'int{7}'"), std::string::npos) << b.why();
    EXPECT_NE(b.compileErr.find(".fin:3:5"), std::string::npos) << b.why();
    EXPECT_EQ(b.compileErr.find(".fin:1:1"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_DiagnosticLocation, AnAttributedVariablesRefusalIsLocatedToo) {
    // The case that already worked, kept beside the one that did not, because it is
    // what made the bug hard to see: `annotated_declaration` sets a location over the
    // attribute list and the declaration together, so an attributed `let` was located
    // by its wrapper and the identical bare `let` was not. If a later change to the
    // grammar takes the location back off `declaration_body`, this test keeps passing
    // and the one above fails -- which is the pair that says where to look.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let i <int> = 1;\n"
        "    #[slaveof($Fin)] let v <int{7}>;\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find(".fin:3:5"), std::string::npos) << b.why();
    EXPECT_EQ(b.compileErr.find(".fin:1:1"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_DiagnosticLocation, AGlobalsRefusalIsLocatedAtItsOwnLine) {
    // A module-scope `let` goes through the same six `declaration_body` productions,
    // and 1:1 is a plausible answer there for the wrong reason: a global on line 1 of
    // a file really is at 1:1, so a test that put it there would pass with no location
    // at all. It is on line 2 for that reason.
    const Built b = build(
        "let i <int> = 1;\n"
        "pub let v <int{7}>;\n"
        "fun main() <noret> { }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a global of type 'int{7}'"), std::string::npos) << b.why();
    EXPECT_NE(b.compileErr.find(".fin:2:1"), std::string::npos) << b.why();
}

// ---------------------------------------------------------------------------
// Every refusal, not just the first.
//
// `finc -c` used to stop at the first construct it could not lower, which made a
// per-sample refusal a depth-1 probe: all 16 refused corpus samples reported exactly
// one refusal each, so the chain behind each one was invisible and a queue ordered by
// "what does this sample refuse" was ordered by an artefact of the walk. The front end
// has never worked that way -- stdlib/stdio.fin reports 23 diagnostics at once.
//
// This is additive to diagnostics and cannot change what is emitted, because the
// compile already fails and no object is written either way. It is emphatically not
// permission to continue *past* a construct as though it lowered: `failed_` still
// stops the unit it was raised in, and the only thing that changes is where the walk
// is allowed to pick up again.
//
// Where it may pick up again is the one design decision here, and it is answered by
// what consumes what. run() is a pipeline of phases -- enums, structs, signatures,
// globals, method bodies, then the top-level statements -- and a later phase reads
// what an earlier one built. Siblings *within* a phase do not: one struct's
// declaration is not an input to the next struct's, and one function's body is not an
// input to another's. So the walk resumes across siblings inside a phase and still
// halts between phases, which makes a cascade impossible by construction rather than
// by a filter applied afterwards.

BACKEND_TEST(Soundness_Codegen, TwoIndependentUnloweredDeclarationsAreBothReported) {
    // The whole point, at the coarsest grain that has it: a refused declaration and a
    // refused statement in a different function share nothing, so reporting one and
    // stopping hides a whole unit of work from anyone reading the output.
    //
    // The first probe was a `foreach`, which lowers now. A generic `fn` type replaces it
    // for the reason the two cascade tests below already give: it is a *declaration*
    // that refuses, which is what this test's name is about, and it refuses on a type
    // this file maps rather than on a feature that might land next week.
    const Built b = build(
        "fun f() <noret> { let a <fn<T>(m: T) -> T>; }\n"
        "fun g(v: int) <noret> { m1778; }\n"
        "fun main() <noret> { let i <int> = 1; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_EQ(occurrences(b.compileErr, "codegen: "), 2u) << b.why();
    EXPECT_NE(b.compileErr.find("'m1778'"), std::string::npos) << b.why();
    EXPECT_NE(b.compileErr.find("a variable of type 'fn<...>(T) -> T'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoUnloweredFunctionBodiesAreBothReported) {
    // One body is not an input to another, so each reports its own first refusal.
    // Both constructs are statements rather than declarations, so what is being crossed
    // here is a function boundary and not just a top-level one.
    //
    // `m1778` is the second statement, and it used to be a `blame`. `blame`'s assert
    // form lowers now, so it stopped being a refusal at all and this test went green for
    // the wrong reason -- it was counting two and finding one. `m1778` replaces it
    // because it is the same *shape* of refusal, which is what this test is actually
    // about: an expression statement that declares no name, so nothing after it can be a
    // cascade. ADR 0001 fixes its meaning ("not implemented"), so it is a construct that
    // will never stop being refused, which makes it a stabler probe than any feature.
    //
    // The first statement was a `foreach` until `foreach` lowered, and a key lookup into
    // a prototype replaces it on the same reasoning: an expression statement, declaring
    // nothing, whose refusal is a ruling waiting on an answer (what equality over an
    // arbitrary key type means) rather than a feature about to land.
    const Built b = build(
        "fun f() <noret> { m1778; }\n"
        "fun g(v: int) <noret> { m1778; }\n"
        "fun main() <noret> { let i <int> = 1; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_EQ(occurrences(b.compileErr, "codegen: "), 2u) << b.why();
    EXPECT_NE(b.compileErr.find("'m1778'"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, CollectingRefusalsStillWritesNoObject) {
    // The founding rule is unchanged and this is the assertion that says so. Reporting
    // more is only worth anything if "the compile failed" still means "there is no
    // artifact": a stale or partial object is a link against code that was refused.
    const fs::path obj = uniqueTempPath("fin_obj_multi", ".o");
    const Compiled c = compileOnly(
        "fun f() <noret> { m1778; }\n"
        "fun g(v: int) <noret> { m1778; }\n"
        "fun main() <noret> { let i <int> = 1; }\n", obj);
    EXPECT_NE(c.exitCode, 0) << c.why();
    EXPECT_EQ(occurrences(c.err, "codegen: "), 2u) << c.why();
    EXPECT_FALSE(fs::exists(obj)) << c.why();
    std::error_code ec;
    fs::remove(obj, ec);
}

BACKEND_TEST(Soundness_Codegen, EachCollectedRefusalStillNamesItsOwnLine) {
    // A list of refusals is only usable if each one still points at its own construct.
    // The two here are eight lines apart, so a location that was reused or left default
    // would show up as the same line twice.
    const Built b = build(
        "fun f() <noret> { m1778; }\n"
        "fun g(v: int) <noret> { m1778; }\n"
        "fun main() <noret> { let i <int> = 1; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find(".fin:1:"), std::string::npos) << b.why();
    EXPECT_NE(b.compileErr.find(".fin:2:"), std::string::npos) << b.why();
}

// Statements inside one body, which is where the corpus actually keeps its chains: a
// declaration boundary is too coarse to see past, because a sample writes most of its
// program inside `main`. Two statements in a row are not independent the way two
// function bodies are -- the second may read what the first declared -- so this is the
// boundary where a cascade is possible and has to be ruled out rather than assumed
// away.
//
// The rule is narrow and it is about names. The only thing a refused statement can
// leave for a later one to trip over is a name with no storage behind it: a refused
// `m1778` or a refused expression statement declares nothing. So a refused
// variable declaration poisons its name, a read of a poisoned name stops that statement
// without reporting anything, and every other refusal in the block is its own finding.
// A suppressed statement is not silently accepted -- it is not lowered either, and the
// compile still fails -- it is simply not reported as a separate discovery, because it
// is not one.

BACKEND_TEST(Soundness_Codegen, TwoIndependentUnloweredStatementsInOneBodyAreBothReported) {
    // Neither statement reads anything the other declares, so both are findings.
    // The declaration above them lowers, which is what keeps this case free of any
    // poisoned name and separates it from the test below.
    //
    // `m1778` was a `blame` until `blame`'s assert form lowered, and the statement above
    // it was a `foreach` until `foreach` lowered; see
    // TwoUnloweredFunctionBodiesAreBothReported for why each replacement was chosen.
    const Built b = build(
        "fun f(v: int) <noret> {\n"
        "    m1778;\n"
        "    m1778;\n"
        "}\n"
        "fun main() <noret> { let i <int> = 1; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_EQ(occurrences(b.compileErr, "codegen: "), 2u) << b.why();
    EXPECT_NE(b.compileErr.find("'m1778'"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ARefusedDeclarationDoesNotCascadeIntoItsReaders) {
    // The former fixture used `[int]` as the refused declaration. That construct now
    // lowers, so the test's old argument is preserved as the replacement: use a
    // prototype, whose backend representation is still deliberately refused.
    const Built b = build(
        "fun f() <noret> {\n"
        "    let a <fn<T>(m: T) -> T>;\n"
        "    let b <auto> = a;\n"
        "    let c <auto> = a;\n"
        "}\n"
        "fun main() <noret> { let i <int> = 1; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_EQ(occurrences(b.compileErr, "codegen: "), 1u) << b.why();
    EXPECT_NE(b.compileErr.find("a variable of type 'fn<...>(T) -> T'"), std::string::npos) << b.why();
    EXPECT_EQ(b.compileErr.find("the name 'a'"), std::string::npos)
        << "a reader of the refused name was reported as a finding of its own\n" << b.why();
}

BACKEND_TEST(Soundness_Codegen, SuppressingACascadeDoesNotSuppressAnUnrelatedRefusal) {
    // The dynamic-array declaration used by the original fixture now lowers. Preserve
    // the cascade argument with a construct still refused by the backend.
    const Built b = build(
        "fun f(v: int) <noret> {\n"
        "    let a <fn<T>(m: T) -> T>;\n"
        "    let b <auto> = a;\n"
        "    m1778;\n"
        "}\n"
        "fun main() <noret> { let i <int> = 1; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_EQ(occurrences(b.compileErr, "codegen: "), 2u) << b.why();
    EXPECT_NE(b.compileErr.find("a variable of type 'fn<...>(T) -> T'"), std::string::npos) << b.why();
    EXPECT_NE(b.compileErr.find("'m1778'"), std::string::npos) << b.why();
    EXPECT_EQ(b.compileErr.find("the name 'a'"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, WhatWasNotExaminedIsSaidInTheTrace) {
    // Suppression makes the list of refusals a lower bound, and a lower bound that does
    // not say so is a number someone will plan against. It is said on the trace rather
    // than in the diagnostics because a suppressed statement is not a diagnostic --
    // reporting it as one is the noise this mechanism exists to avoid -- and
    // `--debug-codegen` is the channel that already exists for what the backend did.
    const std::string trace = codegenTrace(
        "fun f() <noret> {\n"
        "    let a <fn<T>(m: T) -> T>;\n"
        "    let b <auto> = a;\n"
        "}\n"
        "fun main() <noret> { let i <int> = 1; }\n");
    EXPECT_NE(trace.find("not examined"), std::string::npos) << trace;
    EXPECT_NE(trace.find("'a'"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, WithoutDashOFincStillOnlyChecks) {
    // Every other suite here, and tests/tools/corpus_snapshot.sh, invoke `finc F`
    // for its diagnostics. That invocation must stay a check: no artifact, and no
    // codegen refusal for a program the backend cannot lower yet.
    fs::path src = uniqueTempPath("fin_cg_check", ".fin");
    {
        std::ofstream f(src, std::ios::binary);
        f << "struct S { pub v <int>, }\nfun main() <noret> { let s <S> = S{v: 1}; }\n";
    }
    const FincRun r = runFinc({src.string()});
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
    EXPECT_FALSE(fs::exists("a.out"));
    std::error_code ec;
    fs::remove(src, ec);
}

BACKEND_TEST(Soundness_Codegen, ARejectedProgramProducesNoArtifact) {
    fs::path src = uniqueTempPath("fin_cg_bad", ".fin");
    fs::path exe = uniqueTempPath("fin_cg_bad_exe");
    {
        std::ofstream f(src, std::ios::binary);
        f << "fun main() <noret> { let x <int> = nosuchthing(); }\n";
    }
    const FincRun r = runFinc({src.string(), "-o", exe.string()});
    EXPECT_EQ(r.exitCode, 1) << stripAnsi(r.err);
    EXPECT_FALSE(fs::exists(exe));
    std::error_code ec;
    fs::remove(src, ec);
    fs::remove(exe, ec);
}


// ---------------------------------------------------------------------------
// Fixed arrays.
//
// A fixed array is the second aggregate to lower, and what unblocked it is the
// same thing that unblocked structs: a number the backend is not allowed to guess.
// `[int, 4]` and `[int, 8]` used to be one semantic type, so an alloca here would
// have been a guess at how much stack to reserve -- and a guessed size is a
// program that runs and writes past what it reserved. The extent is part of the
// type now and the layout pass measures it (ADR 0015's second moment), so this
// file hands LLVM an [N x T] and lets LLVM place it, exactly as it hands over a
// struct's field list.
//
// A *dynamic* `[T]` is deliberately still refused, and it is not the same feature
// with a number missing. How a `[T]` is represented -- a pointer and a length side
// by side, a header word ahead of the elements, something else -- is an undecided
// ruling that decides what `array.length` compiles to inside a callee that was
// handed one (arrays.fin's `sort(array: &[T])` is the corpus's own case). Refused
// rather than guessed at, for the reason Layout.cpp gives at the same fork.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AFixedArrayIsIndexedAtRunTime) {
    // The loop is what makes this a test of a GEP rather than of constant folding:
    // the index is a variable the front end cannot have read.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 3]> = [10, 20, 30];\n"
        "    for (i: int = 0; i < 3; i++) { printf(\"%d \", a[i]); }\n"
        "    printf(\"\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "10 20 30 \n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, EachElementIsItsOwnSlot) {
    // Three distinct values read back in one printf: a lowering that stored every
    // element at the same offset, or read every index from element 0, prints the
    // same number three times and passes AFixedArrayIsIndexedAtRunTime's shape
    // only if the values happen to agree.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 3]> = [10, 20, 30];\n"
        "    printf(\"%d %d %d\\n\", a[0], a[1], a[2]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "10 20 30\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnElementIsAssignedThroughItsIndex) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 3]> = [1, 2, 3];\n"
        "    a[1] = 99;\n"
        "    printf(\"%d %d %d\\n\", a[0], a[1], a[2]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 99 3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAssignmentThroughARunTimeIndexWritesOneElement) {
    // The write half of the GEP, with an index the front end could not fold, and an
    // assertion that it wrote *one* element rather than smearing across the array.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 4]> = [0, 0, 0, 0];\n"
        "    for (i: int = 0; i < 4; i++) { a[i] = i * i; }\n"
        "    printf(\"%d %d %d %d\\n\", a[0], a[1], a[2], a[3]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0 1 4 9\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayWithNoInitialiserIsZeroed) {
    // The same answer a scalar local with no initialiser gets, and for the same
    // reason: undefined stack contents is the one answer that cannot be tested.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 3]>;\n"
        "    printf(\"%d %d %d\\n\", a[0], a[1], a[2]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0 0 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArraysLengthIsAConstant) {
    // `.length` on a fixed array is the extent, known here and folded to it. The
    // analyzer types it as `int` (Soundness_Members.ALengthIsAnIntAndNotAnother-
    // IntegerWidth), so `%d` reads it.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 7]>;\n"
        "    printf(\"%d\\n\", a.length);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ALengthIsUsableInArithmetic) {
    // Not just printable: `i < a.length - 1` is how arrays.fin:16 writes its loop
    // bound, so the constant has to be an ordinary int value and not a special form.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 5]> = [1, 2, 3, 4, 5];\n"
        "    let total <int> = 0;\n"
        "    for (i: int = 0; i < a.length; i++) { total = total + a[i]; }\n"
        "    printf(\"%d %d\\n\", total, a.length - 1);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "15 4\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayOfAWiderElementStrides) {
    // The stride is the element's size, not a word. An array of `long` read with an
    // int-sized stride returns halves of neighbouring elements, which is exactly the
    // failure a hardcoded stride produces and is invisible with i32 elements.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[long, 3]> = [4294967296, 8589934592, 12884901888];\n"
        "    printf(\"%lld %lld %lld\\n\", a[0], a[1], a[2]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4294967296 8589934592 12884901888\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayOfFloatsRoundTrips) {
    // `float` rather than `double` because a float literal does not widen to
    // `double` in the front end today -- `[double, 3] = [1.5, ...]` reports
    // `expected 'double', got 'float'`, which is a booked front-end gap and not a
    // property of arrays. The elements still cross the vararg boundary as doubles,
    // which is what promoteVararg is for.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[float, 3]> = [1.5, 2.5, 3.5];\n"
        "    printf(\"%.1f %.1f %.1f\\n\", a[0], a[1], a[2]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1.5 2.5 3.5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayOfStructsIndexesAndReadsAField) {
    // Two aggregates composed: the GEP lands on an element and the struct GEP lands
    // on a field of it. Getting the stride wrong here reads one struct's field at
    // another's offset, and both are well-typed ints.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> }\n"
        "fun main() <noret> {\n"
        "    let ps <[P, 2]> = [P { a: 1, b: 2 }, P { a: 3, b: 4 }];\n"
        "    printf(\"%d %d %d %d\\n\", ps[0].a, ps[0].b, ps[1].a, ps[1].b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2 3 4\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayFieldOfAStructIsAddressed) {
    // The other composition order. A struct containing an array is what the layout
    // pass's pointer-map repeat exists for, and its field GEP has to land on the
    // start of the array rather than on a word.
    const Built b = build(std::string(kPrintf) +
        "struct Row { n <int>, cells <[int, 3]> }\n"
        "fun main() <noret> {\n"
        "    let r <Row> = Row { n: 9, cells: [7, 8, 9] };\n"
        "    printf(\"%d %d %d %d\\n\", r.n, r.cells[0], r.cells[1], r.cells[2]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9 7 8 9\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnElementOfAStructFieldIsAssigned) {
    const Built b = build(std::string(kPrintf) +
        "struct Row { cells <[int, 3]> }\n"
        "fun main() <noret> {\n"
        "    let r <Row> = Row { cells: [1, 2, 3] };\n"
        "    r.cells[2] = 42;\n"
        "    printf(\"%d %d %d\\n\", r.cells[0], r.cells[1], r.cells[2]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2 42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedArrayIndexesInBothDimensions) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let g <[[int, 2], 2]> = [[1, 2], [3, 4]];\n"
        "    printf(\"%d %d %d %d\\n\", g[0][0], g[0][1], g[1][0], g[1][1]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2 3 4\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayIsCopiedByValueIntoAVariable) {
    // Fin has not ruled on whether an array assigned to another variable aliases or
    // copies, and an LLVM array value is a value -- so this lowers as a copy, which
    // is the same answer a struct gets. Asserted rather than assumed: if the ruling
    // lands the other way this test is the one that has to change, and a silent
    // aliasing lowering would be a program that mutates a variable nobody assigned.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 3]> = [1, 2, 3];\n"
        "    let c <[int, 3]> = a;\n"
        "    c[0] = 99;\n"
        "    printf(\"%d %d\\n\", a[0], c[0]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 99\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayCrossesAFinToFinCallByValue) {
    // Same rule as the struct parameter, and the same reason it is only a Fin-to-Fin
    // boundary: the platform ABI decides how an aggregate is passed and clang
    // implements that classification, so an array on an `@define` refuses below.
    const Built b = build(std::string(kPrintf) +
        "fun total(a: [int, 3]) <int> { return a[0] + a[1] + a[2]; }\n"
        "fun main() <noret> {\n"
        "    let a <[int, 3]> = [1, 2, 3];\n"
        "    printf(\"%d\\n\", total(a));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "6\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayIsReturnedByValue) {
    const Built b = build(std::string(kPrintf) +
        "fun make() <[int, 3]> { return [4, 5, 6]; }\n"
        "fun main() <noret> {\n"
        "    let a <[int, 3]> = make();\n"
        "    printf(\"%d %d %d\\n\", a[0], a[1], a[2]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4 5 6\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AZeroLengthArrayLowers) {
    // `[T, 0]` is a legal type of zero bytes, and it is genuinely zero where an
    // empty struct is one byte: an empty struct spends its byte so that two of its
    // values cannot share an address, and `[T, 0]` needs no such byte because it
    // has an element type and therefore a stride to distinguish by. Nothing may
    // index it, which the front end enforces
    // (Soundness_ArrayBounds.AZeroLengthArrayHasNoElementZero).
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 0]> = [];\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADynamicArrayIsAvaPairOfPointerAndLength) {
    // Not a fixed array with a number missing. How a `[T]` is represented is now
    // settled by ADR 0025: `{ptr, len}`, with the pointer first and an `int` length
    // second. The literal below exercises allocation, stores and runtime indexing;
    // the sample suite exercises the same pair through a reference and `.length`.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int]> = [1, 2, 3];\n"
        "    printf(\"%d %d\\n\", a.length, a[1]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "3 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoDynamicArraysOfOneElementTypeAreTheSameType) {
    // The pair is a *literal* struct type, which LLVM uniques by its element types, so
    // every mapping of `[int]` is one llvm::StructType. It was built with
    // `StructType::create` and so was a fresh named type per call -- and nothing
    // noticed, because a `[T]` that is only ever declared, indexed and `.length`-ed
    // never has one compared against another.
    //
    // Passing one and returning one are exactly the comparisons: `convert` shortcuts on
    // `from.type.llvmType == to.llvmType`, which was false for two `[int]`s, and the
    // walk fell through every remaining case to the bottom and reported `this
    // conversion is not lowered yet` -- a refusal about the representation ADR 0025
    // had already decided, at a caret on the call rather than on anything wrong.
    //
    // Both directions in one test so they cannot drift apart: a parameter is a
    // conversion into a callee's type and a return is a conversion into the caller's,
    // and a fix that unified only one of them would leave the other refusing.
    const Built b = build(std::string(kPrintf) +
        "fun take(a: [int]) <int> { return a.length; }\n"
        "fun give() <[int]> { let a <[int]> = [1, 2, 3, 4]; return a; }\n"
        "fun main() <noret> {\n"
        "    let a <[int]> = [1, 2, 3];\n"
        "    printf(\"%d %d\\n\", take(a), give().length);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3 4\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayAllocationIsAPairAndNotAPointerToAFixedArray) {
    // `new [T, n]{}` is the one allocation whose result is not a pointer. The analyzer
    // types it `[T]` (`self._arr = new [T, amount]{}` at stdlib/collection.fin:54
    // stores it into a `[T]` field), so the backend must produce the pair.
    //
    // A run-time extent, deliberately: it is what the corpus writes -- stdio.fin
    // allocates `new [char, nbytes + self.stream_length]` -- and it is what cannot be
    // served by mapping the written type, because `mapArray` needs a constant to build
    // an `[N x T]`. A literal extent is the trap in the other direction: `new [int, 3]`
    // maps to a fixed `[3 x i32]`, which would make the result a `&[int, 3]` and not
    // the `[int]` the analyzer said. So the extent is never read as a type here.
    //
    // The contents are asserted zero, and that is a decision and not an observation:
    // `{}` is the empty initialiser written at every corpus allocation site, and
    // undefined contents is the one answer no test can pin.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let n <int> = 4;\n"
        "    let a <[int]> = new [int, n]{};\n"
        "    printf(\"%d %d\\n\", a.length, a[3]);\n"
        "    a[3] = 7;\n"
        "    printf(\"%d\\n\", a[3]);\n"
        "    delete a;\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4 0\n7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayAllocationsExtentMayBeAnyIntegerAndAnExpression) {
    // Both of the corpus's own allocations write a `ulong` count -- stdio.fin:109
    // declares `read(nbytes: ulong = -1)` and :112 allocates
    // `new [char, nbytes - self.pointer]` -- and the analyzer accepts any integer on
    // purpose (Soundness_ArrayExtent.AnAllocationsExtentMayBeAnyIntegerType). So the
    // backend converts rather than assuming an `int`, and it converts *twice* from one
    // value: widened for the byte multiply, narrowed for the length word, each carrying
    // the source's signedness. A `ulong` sign-extended into the byte count is a
    // negative number handed to malloc, which then fails for a reason that has nothing
    // to do with the program.
    //
    // The expression form is the other half: an extent is an expression, not a name,
    // and `n + 3` is emitted here rather than read as part of a type.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let n <ulong> = 5;\n"
        "    let a <[char]> = new [char, n]{};\n"
        "    let m <int> = 2;\n"
        "    let c <[int]> = new [int, m + 3]{};\n"
        "    printf(\"%d %d\\n\", a.length, c.length);\n"
        "    delete a;\n"
        "    delete c;\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5 5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAllocatedArrayOfStructsStridesByTheWholeElement) {
    // The stride comes from `getTypeAllocSize` and not `getSizeOf`: an element in an
    // array occupies its size *plus* its tail padding, and a struct is where the two
    // differ. Reading element 0 back after writing element 1 is what catches a stride
    // that is too small -- the write would land inside element 0 and the program would
    // still run.
    const Built b = build(std::string(kPrintf) +
        "struct P { x <int>, y <int>, }\n"
        "fun main() <noret> {\n"
        "    let a <[P]> = new [P, 2]{};\n"
        "    a[1].x = 9;\n"
        "    printf(\"%d %d %d\\n\", a.length, a[1].x, a[0].x);\n"
        "    delete a;\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2 9 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAllocatedArrayStoredInAFieldKeepsItsLength) {
    // `stdlib/collection.fin` is this shape: `_arr <[T]>` at :51, allocated at :54 and
    // freed at :46. The length has to survive being stored into an enclosing aggregate
    // and loaded back out, which is what a `{ptr, len}` pair buys and a bare data
    // pointer cannot.
    const Built b = build(std::string(kPrintf) +
        "struct C { _arr <[int]>, }\n"
        "fun main() <noret> {\n"
        "    let n <int> = 3;\n"
        "    let c <C> = C{_arr: new [int, n]{}};\n"
        "    c._arr[0] = 5;\n"
        "    printf(\"%d %d\\n\", c._arr.length, c._arr[0]);\n"
        "    delete c._arr;\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3 5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TheLengthOfADynamicArrayWithNoAddressIsRead) {
    // A `[T]` that never had a home. `give()` returns a pair in a register and
    // `mk().xs` is a field extracted out of one, and neither has an address to load a
    // length word through -- which is a normal answer from `baseAddress`, not a
    // refusal.
    //
    // The path used to ask for the address twice: once to learn the object was an
    // array, and again inside the dynamic branch to load from. The second ask returned
    // nullopt, the branch returned with no value produced, and the caller reported
    // whatever *it* was in the middle of -- `this conversion is not lowered yet`
    // pointing at line 1, or `an array passed to a C variadic`. Both name something
    // other than the length, which is the part that made it worth a test rather than a
    // one-line fix: a missing value propagates as a refusal about the wrong construct.
    const Built b = build(std::string(kPrintf) +
        "struct S { xs <[int]>, }\n"
        "fun give() <[int]> { let a <[int]> = [1, 2, 3, 4]; return a; }\n"
        "fun mk() <S> { return S{xs: [1, 2]}; }\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d\\n\", give().length, mk().xs.length);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayAllocationWithNoExtentIsRefused) {
    // `new [int]` parses. There is no count, and zero would be a guess rather than an
    // answer -- so it is refused, and named as `new`'s own refusal rather than as
    // something about the element type, which is fine.
    const Built b = build(
        "fun main() <noret> { let a <[int]> = new [int]{}; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("no extent"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayOnAnExternBoundaryIsRefused) {
    // The struct rule at the second aggregate. A C function's parameter of array
    // type is a pointer by C's own decay rule, and passing an LLVM [3 x i32] by
    // value would link cleanly and pass garbage.
    const Built b = build(
        "@define take(a: [int, 3]) <noret>;\n"
        "fun main() <noret> { let a <[int, 3]> = [1, 2, 3]; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayPassedToACVariadicIsRefused) {
    // printf's `...` gives the analyzer nothing to check against, so this reaches
    // the backend well-typed. What va_arg reads is not an aggregate.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 3]> = [1, 2, 3];\n"
        "    printf(\"%d\\n\", a);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayLiteralWithTooFewElementsCannotReachTheBackend) {
    // The front end refuses this (`expected '[int, 3]', got '[int, 2]'`), which is
    // what the extent being part of the type bought. Asserted here because the
    // backend's alloca trusts it: an array literal shorter than its type would be a
    // store off the end of the slot.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let a <[int, 3]> = [1, 2];\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
}

// ---------------------------------------------------------------------------
// A prototype is two arrays side by side.
//
// `prototype<K, V>` lowers as `{ [K], [V] }` -- the keys in one dynamic array, the
// values in another, index by index. That is a derivation and not a choice:
// tests/samples/stdlib/prototypes.fin is normative and says `prtp.0` is the keys and
// `prtp.1` the values, the analyzer already types those two as `[K]` and `[V]`, and a
// dynamic `[T]` is ADR 0025's `{ptr, len}`, which lowers today. Choosing anything else
// would make `.0` *build* an array at a size only a run time knows.
//
// What is here is storage, construction and the two projections. Key *lookup* is not:
// `a[10]` needs an equality over an arbitrary key type and a search over the keys, which
// is prototype access (tests/samples/prototype_test.fin's own note) and a unit of its
// own. It refuses rather than answering with element 0.

BACKEND_TEST(Soundness_Codegen, APrototypeLiteralKeepsItsKeysAndValuesInWrittenOrder) {
    // The pairing is the data structure -- key i belongs to value i -- so this asserts
    // the *order*, not merely that three of each arrived. A build that sorted the keys,
    // or that filled the values array from the keys' expressions, passes a length check
    // and fails this one.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <{int, float}> = { 30: 1.5, 10: 2.5, 20: 3.5 };\n"
        "    printf(\"%d %d %d\\n\", p.0[0], p.0[1], p.0[2]);\n"
        "    printf(\"%.1f %.1f %.1f\\n\", p.1[0], p.1[1], p.1[2]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "30 10 20\n1.5 2.5 3.5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, BothHalvesOfAPrototypeHaveTheSameLength) {
    // `.length` through the projection, which is what makes the two arrays real arrays
    // rather than two pointers: the length word is in each pair, and reading it is the
    // same code path `a.length` on an `[int]` takes.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <{int, float}> = { 1: 1.0, 2: 2.0, 3: 3.0, 4: 4.0 };\n"
        "    printf(\"%d %d\\n\", p.0.length, p.1.length);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4 4\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeHalfIsADynamicArrayAndAssignsToOne) {
    // `let k <[int]> = p.0;` -- the projection's type has to *be* the `[K]` the analyzer
    // says it is, not merely a pair that happens to have the same shape. The assignment
    // is what checks it: convert() compares llvm types, and a half built by any route
    // other than the one mapArray uses would be a different llvm::StructType here.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <{int, float}> = { 7: 1.5, 8: 2.5 };\n"
        "    let k <[int]> = p.0;\n"
        "    let v <[float]> = p.1;\n"
        "    printf(\"%d %.1f %d\\n\", k[1], v[1], k.length);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8 2.5 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeIsPassedToAFunctionAndReturnedFromOne) {
    // The pair-of-pairs by value across both boundaries. A `{ { ptr, i32 }, { ptr, i32 } }`
    // is an aggregate, and the reason this is a test rather than an assumption is that
    // the extern boundary refuses aggregates for a reason (AnArrayOnAnExternBoundaryIsRefused);
    // a Fin-to-Fin call does not, and the two must not be confused.
    const Built b = build(std::string(kPrintf) +
        "fun mk() <{int, float}> {\n"
        "    let q <{int, float}> = { 5: 1.5, 6: 2.5 };\n"
        "    return q;\n"
        "}\n"
        "fun firstKey(p: {int, float}) <int> { return p.0[0]; }\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d\\n\", firstKey(mk()), mk().1.length);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeWithNoHomeIsProjectedFromTheValue) {
    // `mk().0` -- a prototype returned by a call is a value with no address, so the half
    // comes out of the register with extractvalue rather than through a GEP. Paired with
    // the test above deliberately: that one reads through a variable, this one never has
    // one, and the two paths in visit(MemberAccess&) are separate code.
    const Built b = build(std::string(kPrintf) +
        "fun mk() <{int, float}> {\n"
        "    let q <{int, float}> = { 11: 1.5, 22: 2.5 };\n"
        "    return q;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let k <[int]> = mk().0;\n"
        "    printf(\"%d %d\\n\", k[1], mk().0.length);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "22 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeIsAStructFieldAndCopiedWithTheStruct) {
    // The field is the pair-of-pairs inline, so a struct holding one is stored and read
    // like any other struct. The copy is a pointer copy of each half's buffer -- which
    // is exactly what a dynamic `[T]` field already does
    // (a struct with an `[int]` field lowers today) -- and is asserted rather than
    // assumed because a reader will ask.
    const Built b = build(std::string(kPrintf) +
        "struct Bag {\n"
        "    data <{int, float}>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Bag> = Bag { data: { 9: 1.5, 8: 2.5 } };\n"
        "    let c <Bag> = b;\n"
        "    printf(\"%d %d %.1f\\n\", c.data.0[0], c.data.0.length, c.data.1[1]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9 2 2.5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeWithOneEntryLowers) {
    // The single-entry case, because one is the length every off-by-one gets right for
    // the wrong reason and the one a malloc of `sizeof(T) * 1` cannot distinguish from
    // a malloc of `sizeof(T)`.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <{int, float}> = { 42: 4.5 };\n"
        "    printf(\"%d %.1f %d\\n\", p.0[0], p.1[0], p.0.length);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42 4.5 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADeclaredPrototypeWithNoInitialiserLowers) {
    // Storage alone: the slot exists and the program compiles. Nothing is read out of
    // it, because what an uninitialised prototype's halves *contain* is the same open
    // question an uninitialised `[int]`'s pointer is, and reading one would be asserting
    // an answer to it.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let p <{int, float}>;\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeVariableIsCopiedWhenAssignedFromAnother) {
    // `let q <{int, float}> = p;` -- the pair-of-pairs is a value, so this is a copy of
    // two pointers and two lengths. Both names then read the same buffers, which is the
    // same aliasing a dynamic `[T]` copy has and is ADR 0025's, not a new decision.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <{int, float}> = { 3: 1.5, 4: 2.5 };\n"
        "    let q <{int, float}> = p;\n"
        "    printf(\"%d %.1f\\n\", q.0[1], q.1[0]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4 1.5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AKeyIsWrittenThroughTheProjection) {
    // `p.0[1] = 99` -- the projection is an lvalue because the half is addressed, and
    // the store lands in the heap buffer the literal allocated. This is the write half
    // of the same address path the read uses, and the two disagreeing would mean a
    // read-only view of something the program can name.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <{int, float}> = { 3: 1.5, 4: 2.5 };\n"
        "    p.0[1] = 99;\n"
        "    p.1[0] = 7.5;\n"
        "    printf(\"%d %.1f\\n\", p.0[1], p.1[0]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "99 7.5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedLiteralInsideAPrototypeGetsTheHalfsElementType) {
    // `{ 1: [7, 8] }` -- the value half is an `[[int]]`, so each value expression is an
    // array literal and needs the element type as its own hint. Without that it refused
    // with "an array literal with no declared type" in a program whose author wrote no
    // array declaration to be missing.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <{int, [int]}> = { 1: [7, 8], 2: [9, 10] };\n"
        "    printf(\"%d %d %d\\n\", p.0[1], p.1[0][1], p.1[1].length);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2 8 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeIsAHalfOfAPrototype) {
    // Recursion through the mapper, which is the property that makes the representation
    // compositional rather than a special case for scalars: the inner prototype is a
    // value like any other and its `{ [K], [V] }` sits in the outer values' buffer.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <{int, {int, float}}> = { 1: { 5: 1.5 } };\n"
        "    printf(\"%d %d\\n\", p.0[0], p.1[0].0[0]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeLiteralWithNoDeclaredTypeIsRefused) {
    // The negative that pairs with every test above. `{ 10: 1.5 }` is not
    // `prototype<int, float>` by inspection -- the front end may have typed those
    // constants against a `<{long, double}>` this file cannot see -- so reading the key
    // type off the first key is how the two passes come to disagree about a stride.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let p <auto> = { 10: 1.5 };\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a prototype literal with no declared type"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeLookupUsesTheKeyNotTheArrayIndex) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <{int, int}> = { 10: 1, 20: 2 };\n"
        "    printf(\"%d %d\\n\", p[20], p[10]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AKeyLookupOnAPrototypeUsesTheKey) {
    const Built b = build(
        "fun main() <noret> {\n"
        "    let p <{int, float}> = { 10: 1.5 };\n"
        "    let v <float> = p[10];\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMissingKeyBlamesRatherThanReturningASentinel) {
    // ADR 0028: an absent key is never a value. A generic `V` has no sentinel that is
    // not also a legal value, so the read fails loudly at the line that wrote it.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let p <{int, int}> = { 10: 1 };\n"
        "    let v <int> = p[11];\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_NE(b.runExit, 0) << b.why();
    EXPECT_NE(b.out.find(":3: Fin blames this lookup because the key is not in the prototype"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeStoreKeepsInsertionOrderAndUpdatesInPlace)  {
    // Two facts in one program, because they are the same invariant seen twice: key i
    // belongs to value i, and a store to a key already there rewrites that value rather
    // than appending a second entry with the same key.
    const Built b = build(
        "@define printf(fmt: string, ...) <noret>;\n"
        "fun main() <noret> {\n"
        "    let p <{int, int}> = { 10: 1 };\n"
        "    p[20] = 2;\n"
        "    p[10] = 7;\n"
        "    printf(\"%d %d %d %d\\n\", p[10], p[20], p.0[0], p.1[1]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7 2 10 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AKeyStoreOnAPrototypeUpdatesAndInserts) {
    // The write half of the same question, and the harder one: `p[11] = 2.5` on a key
    // that is not there has to *grow* both buffers, which is an allocator policy nothing
    // in the corpus rules on. Refused at the assignment rather than at the index, which
    // is why it is its own test: the two go through different code.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let p <{int, float}> = { 10: 1.5 };\n"
        "    p[11] = 2.5;\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeOfAnArityOtherThanTwoIsRefused) {
    // `{int}` and `{int, float, char}` both parse and both reach here. Which of "the
    // value is `any`", "it is a set" and "it is an error" Fin means is unruled, and the
    // representation is two arrays -- so a third half has nowhere to go and a missing one
    // has no type. Refused with the arity in the spelling, so the reader sees what the
    // compiler read.
    const Built one = build("fun main() <noret> { let p <{int}>; }\n");
    EXPECT_NE(one.compileExit, 0) << one.why();
    EXPECT_NE(one.compileErr.find("prototype<int>"), std::string::npos) << one.why();

    const Built three = build("fun main() <noret> { let p <{int, float, char}>; }\n");
    EXPECT_NE(three.compileExit, 0) << three.why();
    EXPECT_NE(three.compileErr.find("prototype<int, float, char>"),
              std::string::npos) << three.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeWithAnObjectHalfDeclaresStorage) {
    // `{object, object}` is what tests/samples/prototype_test.fin:40 writes.
    // Now that `object` maps as an opaque `any` blob, storage is declared cleanly.
    const Built b = build("fun main() <noret> { let p <{object, object}>; }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeWithAnAnyHalfDeclaresStorage) {
    // Was the second half of APrototypeWithAnErasedHalfIsRefused: `{int, any}`
    // refused with it until halves of `any` mapped. Storage needs no boxing
    // rule -- only element reads and writes do, and those refuse downstream
    // (AStoreIntoAnAnyHalfIsRefused) -- so the declaration lowers.
    const Built b = build("fun main() <noret> { let p <{int, any}>; }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeOnAnExternBoundaryIsRefused) {
    // The array rule at the third aggregate. A `{ { ptr, i32 }, { ptr, i32 } }` passed by
    // value to a C function would link cleanly and pass something C never agreed to --
    // there is no C type this is the ABI of, because ADR 0025's pair is Fin's own.
    const Built b = build(
        "@define take(p: {int, float}) <noret>;\n"
        "fun main() <noret> { let p <{int, float}> = { 1: 1.0 }; take(p); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypePassedToACVariadicIsRefused) {
    // printf's `...` gives the analyzer nothing to check against, so this reaches the
    // backend well-typed. What va_arg reads is not an aggregate.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <{int, float}> = { 1: 1.0 };\n"
        "    printf(\"%d\\n\", p);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeGlobalWithALiteralInitialiserIsRefused) {
    // A global's initialiser has to be an LLVM constant, and a prototype literal is a
    // malloc and two stores. That is the same refusal a global `[int]` literal gets and
    // not a prototype rule: where a run-time initialiser for a global runs -- a
    // module-init function, and in what order across modules -- is an open question.
    // The declaration *without* one lowers, which is the pair that says so.
    const Built b = build("pub let g <{int, float}> = { 1: 1.0 };\n"
                          "fun main() <noret> {}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();

    const Built bare = build("pub let g <{int, float}>;\n"
                             "fun main() <noret> {}\n");
    EXPECT_EQ(bare.compileExit, 0) << bare.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeRemovesByKeyAndKeepsTheOrder) {
    // `a.rm("b")` from tests/samples/prototype_test.fin:24, which calls it "the functional
    // way" -- the same operation `delete &a[10]` spells manually. This test used to assert
    // the opposite (that the front end refused every prototype method) and was correct
    // when the analyzer had no method list; ADR 0028's initial API is now checked in
    // SemanticAnalyzer::checkPrototypeMethod and lowered in emitPrototypeMethod, so the
    // refusal it protected has moved to an unknown *name* -- kept below, because that is
    // the part that mattered: a name must never reach the `.0`/`.1` positional path and
    // GEP by a position parsed out of nothing.
    //
    // Three facts here. The removal shifts rather than swaps with the last entry, so 1
    // and 3 stay in the order they were written -- insertion order is the data structure,
    // not an accident of how the last hole was filled. The removal answers `true`, and
    // removing the same key twice answers `false` without failing, because an absent key
    // is not an error to remove. And both halves shrink together: the length after is the
    // one length, read back off `.0`.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <{int, int}> = { 1: 10, 2: 20, 3: 30 };\n"
        "    printf(\"%d %d\\n\", p.rm(2), p.rm(2));\n"
        "    printf(\"%d %d %d %d\\n\", p.0.length, p.1.length, p.0[0], p.0[1]);\n"
        "    printf(\"%d %d\\n\", p[1], p[3]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 0\n2 2 1 3\n10 30\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, DeleteThroughAPrototypeSubscriptRemovesTheEntry) {
    // `delete &a[10]` -- tests/samples/prototype_test.fin:23, whose comment calls it "the
    // manual way" against `a.rm("b")` on 24 as "the functional way". Two spellings of one
    // operation, so this is a removal and not a `free`: a value slot's address points
    // into the values buffer, and handing that to libc would free a block it never
    // allocated. Asserted by what is left afterwards rather than by exit 0, because a
    // `free` of an interior pointer aborts on some allocators and passes on others.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <{int, int}> = { 1: 10, 2: 20, 3: 30 };\n"
        "    delete &p[2];\n"
        "    printf(\"%d %d %d\\n\", p.0.length, p.0[0], p.0[1]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2 1 3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, DeleteThroughAPrototypeReachesItWhereverItLives) {
    // The subject is found the same way every other prototype operation finds one, so a
    // field of a struct and an element of an array both work. `delete &a[i]` on an
    // *array* is still the `free` it always was -- the third build -- which is the pair
    // that says the new path is matched on the prototype and not on the spelling.
    const Built b = build(std::string(kPrintf) +
        "struct Box { pub t <{string, int}> }\n"
        "fun main() <noret> {\n"
        "    let b <Box> = Box { t: { \"a\": 1, \"b\": 2 } };\n"
        "    delete &b.t[\"a\"];\n"
        "    let ps <[{int, int}]> = [{ 1: 10, 2: 20 }];\n"
        "    let i <int> = 0;\n"
        "    delete &ps[i][1];\n"
        "    printf(\"%d %d %d\\n\", b.t.0.length, b.t.contains(\"b\"), ps[0].0.length);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 1 1\n") << b.why();

    const Built twice = build(
        "fun bump(c: [int]) <int> { c[0] = c[0] + 1; return 0; }\n"
        "fun main() <noret> {\n"
        "    let calls <[int]> = [0];\n"
        "    let ps <[{int, int}]> = [{ 1: 10 }];\n"
        "    delete &ps[bump(calls)][1];\n"
        "}\n");
    EXPECT_NE(twice.compileExit, 0) << twice.why();
    EXPECT_NE(twice.compileErr.find("cannot be evaluated twice"), std::string::npos)
        << twice.why();

    const Built array = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int]> = new [int, 3];\n"
        "    delete &a[0];\n"
        "    printf(\"freed\\n\");\n"
        "}\n");
    ASSERT_TRUE(array.ran) << array.why();
    EXPECT_EQ(array.out, "freed\n") << array.why();
}

BACKEND_TEST(Soundness_Codegen, TheAddressOfAPrototypeValueStaysRefused) {
    // `&p[1]` on its own. The slot is interior to the values buffer and an appending
    // store reallocs that buffer, so the pointer would dangle with nothing to tell its
    // holder. `delete &p[1]` is lowered as a whole statement precisely so this can stay
    // refused; if this ever starts compiling, the two have come apart.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let p <{int, int}> = { 1: 10 };\n"
        "    let q <&int> = &p[1];\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeGetAndContainsAnswerTheSameSearch) {
    // `get` is `p[k]` under another name -- one shared scan (emitPrototypeScan) underlies
    // the subscript, `get`, `contains` and `remove` precisely so the four cannot come to
    // disagree about which key is present. `contains` is the one that answers about a
    // missing key without failing; `get` on the same key blames (below).
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <{int, int}> = { 10: 1, 20: 2 };\n"
        "    printf(\"%d %d %d %d\\n\", p.get(20), p.get(10), p.contains(10), p.contains(11));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2 1 1 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeGetBlamesAMissingKey) {
    // ADR 0028 says `get` fails at run time when the key is absent and `try_get` is the
    // non-throwing spelling. Same blame as the subscript, because it is the same lookup.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let p <{int, int}> = { 10: 1 };\n"
        "    let v <int> = p.get(11);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_NE(b.runExit, 0) << b.why();
    EXPECT_NE(b.out.find(":3: Fin blames this lookup because the key is not in the prototype"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeMethodNameOutsideTheApiIsRefused) {
    // What APrototypesMethodsAreRefusedByTheFrontEnd was really protecting: the API is a
    // closed set, so a name that is not in it is a diagnostic and not a position. The
    // message names the set, because the reader of it is looking for the right spelling.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let p <{int, float}> = { 1: 1.0 };\n"
        "    p.nope(1);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("has no method 'nope'"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeTryGetIsRefusedUntilOptionsAreLowered) {
    // `try_get` type-checks -- its result is `V?` -- and does not lower, because a
    // nullable local does not lower at all yet. Refused rather than answered with the
    // `get` lowering, which would blame on the one call that asked safely.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let p <{int, int}> = { 1: 1 };\n"
        "    p.try_get(1);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("try_get"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeReadingMethodTakesAValueReceiver) {
    // `mk().get(2)` -- a prototype that never had a home. The reading methods take one
    // because a search reads the same answer out of a copy as out of the original;
    // `remove` writes a shorter length back, so on a value it refuses rather than editing
    // a table nobody can name.
    const Built b = build(std::string(kPrintf) +
        "fun mk() <{int, int}> {\n"
        "    let p <{int, int}> = { 1: 10, 2: 20 };\n"
        "    return p;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d %d\\n\", mk().get(2), mk().contains(1), mk().contains(9));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "20 1 0\n") << b.why();

    const Built write = build(
        "fun mk() <{int, int}> { let p <{int, int}> = { 1: 10 }; return p; }\n"
        "fun main() <noret> { mk().rm(1); }\n");
    EXPECT_NE(write.compileExit, 0) << write.why();
    EXPECT_NE(write.compileErr.find("with no address"), std::string::npos) << write.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeMethodEmitsItsReceiverOnce) {
    // `p[next()].get(...)` cannot be written -- a prototype of prototypes needs a
    // prototype key -- so the receiver with a side effect is an array element. The
    // regression this pins is real and was live in the first draft: the prototype path
    // asked for an address, and when the name turned out not to be a prototype method the
    // struct path asked for one again, emitting the index expression twice.
    const Built b = build(std::string(kPrintf) +
        "fun bump(c: [int]) <int> { c[0] = c[0] + 1; return 0; }\n"
        "fun main() <noret> {\n"
        "    let calls <[int]> = [0];\n"
        "    let ps <[{int, int}]> = [{ 1: 10 }];\n"
        "    printf(\"%d %d\\n\", ps[bump(calls)].get(1), calls[0]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "10 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStringKeyComparesItsBytesNotItsAddress) {
    // lib/std/memory.fin:32 writes `info["MemoryCardModel"] = ...` and reads it back
    // elsewhere; two identical literals are not required to be the same pointer, and the
    // second one here is a `strdup` of the first, so it certainly is not. Raw-byte or
    // by-address key equality passes the first assertion and fails this one.
    const Built b = build(std::string(kPrintf) +
        "@define strdup(s: string) <string>;\n"
        "fun main() <noret> {\n"
        "    let p <{string, int}> = { \"alpha\": 1 };\n"
        "    p[\"beta\"] = 2;\n"
        "    let copy <string> = strdup(\"beta\");\n"
        "    printf(\"%d %d %d\\n\", p[\"alpha\"], p.get(copy), p.contains(\"gamma\"));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructKeyComparesFieldByField) {
    // Derived structural equality, recursively: a struct key is equal when every field
    // is, which is not the same as its bytes being equal -- padding between the fields is
    // undefined, so a memcmp here would answer from uninitialised memory.
    const Built b = build(std::string(kPrintf) +
        "struct Point { x <int>, y <int> }\n"
        "fun main() <noret> {\n"
        "    let p <{Point, int}> = { Point { x: 1, y: 2 }: 12 };\n"
        "    p[Point { x: 3, y: 4 }] = 34;\n"
        "    let probe <Point> = Point { x: 3, y: 4 };\n"
        "    printf(\"%d %d %d\\n\", p[Point { x: 1, y: 2 }], p.get(probe),\n"
        "           p.contains(Point { x: 1, y: 4 }));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "12 34 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADynamicArrayKeyIsRefusedRatherThanComparedByPointer) {
    // `{[int], [{int, string}]}` at tests/samples/prototype_test.fin:41, which is booked
    // unimplemented. Structural equality of a dynamic array needs a run-time loop over a
    // length; comparing the two pointers instead would be a silent answer to a different
    // question, so the key is refused with the reason.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let p <{[int], int}>;\n"
        "    let k <[int]> = [1];\n"
        "    let v <int> = p[k];\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("dynamic array"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APositionPastAPrototypesTwoHalvesIsRefused) {
    // `p.2`. The front end reports it (`has no member '2'`), and the backend's own path
    // checks the position against 2 as well rather than against the LLVM struct's arity,
    // so the two agree without depending on each other. A GEP at index 2 of a two-field
    // struct is out of bounds.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let p <{int, float}> = { 1: 1.0 };\n"
        "    let v <int> = p.2;\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
}

// ---------------------------------------------------------------------------
// `foreach` walks an array by index.
//
// A counter of the loop's own, a bound read once before the first iteration, an
// indexed load into a slot, and an increment `continue` reaches. That is
// `visit(ForLoop&)`'s shape, which is the point: `foreach` is the spelling that cannot
// get the bound wrong, where `for (i: int = 0; i < a.length - 1; i++)`
// (tests/samples/loops.fin:14) demonstrably can.
//
// What is iterable is an array and nothing else, and that is a ruling recorded in the
// library rather than a gap here: `lib/std/collection.fin`:59 and `lib/std/hashmap.fin`
// :316 both say there is no iteration protocol and that index-based iteration is what
// those types support. So a struct, a prototype, a string or an integer is refused with
// that named -- not skipped, and not silently walked as though it had a length.
//
// The binding is a *copy* of the element. loops.fin:20 is `blame element == a[idx]`,
// which fixes the element at `idx` as the element the loop binds and the index as
// counting from 0 in step with it; a copy is also what makes the one-binding and
// two-binding spellings the same loop, and the front end refuses an assignment to the
// binding anyway (`Cannot assign to immutable variable 'e'`), so nothing can observe
// a write through it.
//
// The binding's written type has to *be* the element type. Nothing before the backend
// checks it -- the analyzer defines both bindings from what was written and never asks
// the iterable what it yields (KnownDefect_Foreach.ABindingTypeIsNeverCheckedAgainst
// TheIterable) -- so `foreach (e <string> in a)` over an `[int]` arrives here as a
// well-typed program, and converting it would read four bytes of an integer as a
// pointer. Refusing is the only answer that does not invent a front-end rule here.

BACKEND_TEST(Soundness_Codegen, AForeachOverAFixedArrayBindsEveryElementInOrder) {
    // loops.fin:24 verbatim in shape, over the same `[int, 5]` the sample declares.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 5]> = [1, 2, 3, 4, 5];\n"
        "    foreach (element <int> in a) {\n"
        "        printf(\"%d \", element);\n"
        "    }\n"
        "    printf(\"\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2 3 4 5 \n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AForeachIndexCountsFromZeroInStepWithTheElement) {
    // loops.fin:19-21's own claim, measured: `element == a[idx]` at every step. The
    // sample writes it as a `blame`, which aborts on a mismatch; this prints both so a
    // failure says which pair disagreed rather than only that one did.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 5]> = [10, 20, 30, 40, 50];\n"
        "    foreach(idx <int>, element <int> in a) {\n"
        "        printf(\"%d:%d \", idx, element);\n"
        "        blame element == a[idx];\n"
        "    }\n"
        "    printf(\"\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0:10 1:20 2:30 3:40 4:50 \n") << b.why();
    EXPECT_EQ(b.runExit, 0) << "the sample's own `blame element == a[idx]` fired\n"
                            << b.why();
}

BACKEND_TEST(Soundness_Codegen, AForeachOverADynamicArrayReadsItsRunTimeLength) {
    // The bound is the pair's length word rather than a constant, which is the whole
    // difference between the two array kinds here. Two arrays of different lengths in
    // one program, so a bound taken from the wrong one would show up as the wrong count.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int]> = [7, 8, 9];\n"
        "    let b <[int]> = [1, 2];\n"
        "    foreach (e <int> in a) { printf(\"%d \", e); }\n"
        "    foreach (e <int> in b) { printf(\"%d \", e); }\n"
        "    printf(\"\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7 8 9 1 2 \n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AForeachOverAnEmptyArrayRunsItsBodyNoTimes) {
    // Zero is the boundary the condition has to get right, and it is a boundary both
    // array kinds have: a `[int, 0]` and a dynamic array built with a zero extent. A
    // do-while shape -- body first, test after -- would run each of these once.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let z <[int, 0]> = [];\n"
        "    foreach (e <int> in z) { printf(\"fixed\"); }\n"
        "    let n <int> = 0;\n"
        "    let d <[int]> = new [int, n]{};\n"
        "    foreach (e <int> in d) { printf(\"dynamic\"); }\n"
        "    printf(\"none\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "none\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, EveryForeachSpellingLowersToTheSameLoop) {
    // The grammar has four (parser.y, `foreach_loop`): parenthesised or not, one binding
    // or two. Soundness_Foreach.TheElementBindingIsDefinedInEveryForm holds the front
    // end to all four, and a lowering that read `index_name` from only the productions
    // that set it -- or that only ever saw the parenthesised spelling, which is the one
    // the corpus writes -- would pass every other test here.
    for (const char* body : {"foreach (e <int> in a) { printf(\"%d\", e); }",
                             "foreach e <int> in a { printf(\"%d\", e); }",
                             "foreach (i <int>, e <int> in a) { printf(\"%d\", e); }",
                             "foreach i <int>, e <int> in a { printf(\"%d\", e); }"}) {
        const Built b = build(std::string(kPrintf) +
            "fun main() <noret> {\n"
            "    let a <[int, 3]> = [4, 5, 6];\n"
            "    " + body + "\n"
            "    printf(\"\\n\");\n"
            "}\n");
        ASSERT_TRUE(b.ran) << body << "\n" << b.why();
        EXPECT_EQ(b.out, "456\n") << body << "\n" << b.why();
    }
}

BACKEND_TEST(Soundness_Codegen, AForeachBindsACopyAndNotTheElementItself) {
    // The array is written *through the index* inside the loop and the binding keeps the
    // value it was given, which is what says the binding is a copy loaded once per
    // iteration rather than an alias of the slot. It also pins the direction of travel:
    // clearing `a[i]` as the loop passes it would change what a later iteration reads if
    // the loop walked backwards, and the printed values would not be the written ones.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 4]> = [10, 20, 30, 40];\n"
        "    foreach (i <int>, e <int> in a) {\n"
        "        a[i] = 0;\n"
        "        printf(\"%d \", e);\n"
        "    }\n"
        "    printf(\"| %d %d\\n\", a[0], a[3]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "10 20 30 40 | 0 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, BreakAndContinueInsideAForeachReachItsOwnTargets) {
    // `continue` goes to the increment and not to the condition -- a `continue` that
    // skipped the step would hang here, which is the one failure this file cannot let
    // through, because a hung test is not a failed one. `break` leaves the loop rather
    // than the enclosing one.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 5]> = [1, 2, 3, 4, 5];\n"
        "    foreach (i <int>, e <int> in a) {\n"
        "        if (i == 1) { continue; }\n"
        "        if (e == 4) { break; }\n"
        "        printf(\"%d \", e);\n"
        "    }\n"
        "    printf(\"\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 3 \n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedForeachKeepsItsOwnCounterAndBinding) {
    // Two loops over one array, the inner one inside the outer's body. A shared counter
    // slot -- one alloca reused, or a member rather than a local -- would run the outer
    // loop once, because the inner would leave the counter at the end.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 2]> = [1, 2];\n"
        "    foreach (x <int> in a) {\n"
        "        foreach (y <int> in a) { printf(\"%d%d \", x, y); }\n"
        "    }\n"
        "    printf(\"\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "11 12 21 22 \n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AForeachBindingIsScopedToTheLoop) {
    // The binding is the loop's name and the enclosing body's variable of the same name
    // is untouched, which is what `pushScope`/`popScope` around the whole statement buys.
    // A binding registered in the *enclosing* scope would leave `e` holding 7 here.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let e <int> = 99;\n"
        "    let a <[int, 3]> = [5, 6, 7];\n"
        "    foreach (e <int> in a) { printf(\"%d \", e); }\n"
        "    printf(\"| %d\\n\", e);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5 6 7 | 99\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AForeachIndexTakesAnyIntegerWidth) {
    // The counter is an `int` -- the type `.length` answers with -- and the binding is
    // whatever integer was written, converted from it. Any width holds a position; a
    // `char` index over a three-element array is the narrowest case the corpus makes
    // reachable, and a conversion that sign-extended the wrong way or truncated the
    // wrong end would show up here rather than in a wide one.
    //
    // `int{8}` is here rather than in AForeachIndexBindingThatIsNotAnIntegerIsRefused
    // because it is the same eight bits `char` names, and a width that held a position
    // when spelled one way and not the other would be two types.
    for (const char* type : {"int", "long", "uint", "ulong", "char",
                             "int{8}", "uint{16}", "int{64}"}) {
        const Built b = build(std::string(kPrintf) +
            "fun main() <noret> {\n"
            "    let a <[int, 3]> = [4, 5, 6];\n"
            "    foreach (i <" + std::string(type) + ">, e <int> in a) {\n"
            "        printf(\"%d\", e);\n"
            "    }\n"
            "    printf(\"\\n\");\n"
            "}\n");
        ASSERT_TRUE(b.ran) << type << "\n" << b.why();
        EXPECT_EQ(b.out, "456\n") << type << "\n" << b.why();
    }
}

BACKEND_TEST(Soundness_Codegen, AForeachWalksAnArrayWhereverItLives) {
    // The iterable is reached through `baseAddress`, which is the same door `a[i]` and
    // `a.length` use -- so every home an array has is an iterable one, and none of them
    // needed a case of its own. A global, a parameter, a struct field, and a pointer to
    // an array (deeptest3.fin:111's rule: the base is dereferenced first), in one program
    // so that a regression in any of them fails a single test.
    const Built b = build(std::string(kPrintf) +
        "struct Bag { xs <[int]> }\n"
        "let g <[int, 2]> = [1, 2];\n"
        "fun sum(xs: [int]) <int> {\n"
        "    let t <int> = 0;\n"
        "    foreach (e <int> in xs) { t = t + e; }\n"
        "    return t;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    foreach (e <int> in g) { printf(\"%d\", e); }\n"
        "    printf(\" %d\", sum([3, 4]));\n"
        "    let b <Bag> = Bag { xs: [5, 6] };\n"
        "    foreach (e <int> in b.xs) { printf(\" %d\", e); }\n"
        "    let a <[int, 2]> = [7, 8];\n"
        "    let p <&[int, 2]> = &a;\n"
        "    foreach (e <int> in p) { printf(\" %d\", e); }\n"
        "    printf(\"\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "12 7 5 6 7 8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AForeachElementCanBeAnyTypeAnArrayHolds) {
    // The element is loaded and stored by its own type, so a struct element is a struct
    // copy and a float element is a float -- neither is a case in the loop. A nested
    // array is the one worth naming: the binding's type is `[int, 2]`, and iterating the
    // binding is then an ordinary `foreach` over an array that lives in a frame slot.
    const Built b = build(std::string(kPrintf) +
        "struct P { x <int>, y <int> }\n"
        "fun main() <noret> {\n"
        "    let ps <[P, 2]> = [P{x: 1, y: 2}, P{x: 3, y: 4}];\n"
        "    foreach (p <P> in ps) { printf(\"%d%d \", p.x, p.y); }\n"
        "    let fs <[float, 2]> = [1.5, 2.5];\n"
        "    foreach (f <float> in fs) { printf(\"%.1f \", f); }\n"
        "    let rows <[[int, 2], 2]> = [[1, 2], [3, 4]];\n"
        "    foreach (row <[int, 2]> in rows) {\n"
        "        foreach (e <int> in row) { printf(\"%d\", e); }\n"
        "    }\n"
        "    printf(\"\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "12 34 1.5 2.5 1234\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AForeachWalksAPrototypesHalf) {
    // `p.0` is a `[K]` and iterating it is iterating a dynamic array -- no prototype case
    // in the loop, which is what makes the two units one. The keys come back in written
    // order, which is what APrototypeLiteralKeepsItsKeysAndValuesInWrittenOrder holds.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <{int, float}> = { 30: 1.5, 10: 2.5 };\n"
        "    foreach (k <int> in p.0) { printf(\"%d \", k); }\n"
        "    foreach (v <float> in p.1) { printf(\"%.1f \", v); }\n"
        "    printf(\"\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "30 10 1.5 2.5 \n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AReturnOutOfAForeachBodyLeavesTheFunction) {
    // The body is walked with the ordinary statement visitor, so a `return` inside it
    // terminates its block and the loop must not then append a branch to the step --
    // which would be an LLVM error, not a wrong answer. `terminated()` is what checks it,
    // and this is the case that reaches it.
    const Built b = build(std::string(kPrintf) +
        "fun first_over(a: [int], n: int) <int> {\n"
        "    foreach (e <int> in a) { if (e > n) { return e; } }\n"
        "    return -1;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d\\n\", first_over([1, 5, 9], 4), first_over([1, 2], 7));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5 -1\n") << b.why();
}

// The refusals. Each names the question it is waiting on, and each is paired with the
// nearest thing that lowers, so what the refusal is *about* is the difference between
// the two programs and not the whole of either.

BACKEND_TEST(Soundness_Codegen, AForeachOverSomethingThatIsNotAnArrayIsRefused) {
    // The ruling, not a gap: there is no iteration protocol in Fin
    // (lib/std/collection.fin:59), so nothing but an array says what it yields. The front
    // end accepts every one of these -- it never asks the iterable anything, which is
    // KnownDefect_Foreach.TheIterableIsNeverCheckedForBeingIterable -- so each arrives
    // here as a well-typed program and the backend is where it stops.
    //
    // A prototype is the one worth naming: it *has* two arrays inside it, so walking one
    // half or the other would be a choice this file is not entitled to make. `p.0` is how
    // a program says which, and AForeachWalksAPrototypesHalf measures it.
    for (const char* code : {
             "fun main() <noret> { foreach (e <int> in 5) { } }\n",
             "fun main() <noret> { foreach (e <int> in true) { } }\n",
             "fun main() <noret> { let s <string> = \"ab\";"
             " foreach (e <char> in s) { } }\n",
             "struct S { a <int> }\n"
             "fun main() <noret> { let s <S> = S { a: 1 };"
             " foreach (e <int> in s) { } }\n",
             "fun main() <noret> { let p <{int, int}> = { 1: 2 };"
             " foreach (e <int> in p) { } }\n",
             "fun main() <noret> { let x <int> = 1; let p <&int> = &x;"
             " foreach (e <int> in p) { } }\n"}) {
        const Built b = build(code);
        EXPECT_NE(b.compileExit, 0) << code << "\n" << b.why();
        EXPECT_NE(b.compileErr.find("not an array"), std::string::npos)
            << code << "\n" << b.why();
    }
}

BACKEND_TEST(Soundness_Codegen, AForeachOverAnArrayWithNoHomeIsRefused) {
    // Said apart from "not an array", because it sends a reader somewhere else: this one
    // *is* an array and the gap is that a temporary has no address to index into --
    // LLVM's extractvalue takes a constant index, so an array that is only a value cannot
    // be walked at all. The same gap `give()[0]` and `mk().xs[0]` have.
    //
    // Paired with the array named by a variable, one line apart, which lowers.
    const Built refused = build(std::string(kPrintf) +
        "struct Box { xs <[int, 2]> }\n"
        "fun mk() <Box> { return Box { xs: [1, 2] }; }\n"
        "fun main() <noret> { foreach (e <int> in mk().xs) { printf(\"%d\", e); } }\n");
    EXPECT_NE(refused.compileExit, 0) << refused.why();
    EXPECT_NE(refused.compileErr.find("no home"), std::string::npos) << refused.why();

    const Built lowered = build(std::string(kPrintf) +
        "struct Box { xs <[int, 2]> }\n"
        "fun mk() <Box> { return Box { xs: [1, 2] }; }\n"
        "fun main() <noret> {\n"
        "    let b <Box> = mk();\n"
        "    foreach (e <int> in b.xs) { printf(\"%d\", e); }\n"
        "    printf(\"\\n\");\n"
        "}\n");
    ASSERT_TRUE(lowered.ran) << lowered.why();
    EXPECT_EQ(lowered.out, "12\n") << lowered.why();
}

BACKEND_TEST(Soundness_Codegen, AForeachBindingOfAnotherTypeThanTheElementIsRefused) {
    // Nothing before the backend checks this, so refusing it here is what stands between
    // `foreach (e <string> in a)` over an `[int]` and a program that reads four bytes of
    // an integer as a pointer. Not converted, even where a conversion exists: `int` into
    // `long` widens fine as an assignment, but a binding that silently widened would make
    // `e` a different value from `a[i]` and loops.fin:20 asserts they are the same.
    for (const char* code : {
             "fun main() <noret> { let a <[int, 3]> = [1, 2, 3];"
             " foreach (e <string> in a) { } }\n",
             "fun main() <noret> { let a <[int, 3]> = [1, 2, 3];"
             " foreach (e <long> in a) { } }\n",
             "fun main() <noret> { let a <[float, 2]> = [1.5, 2.5];"
             " foreach (e <int> in a) { } }\n",
             "struct A { a <int> }\n"
             "struct B { a <int> }\n"
             "fun main() <noret> { let xs <[A, 1]> = [A{a: 1}];"
             " foreach (e <B> in xs) { } }\n"}) {
        const Built b = build(code);
        EXPECT_NE(b.compileExit, 0) << code << "\n" << b.why();
        EXPECT_NE(b.compileErr.find("elements of another type"), std::string::npos)
            << code << "\n" << b.why();
    }
}

BACKEND_TEST(Soundness_Codegen, AForeachBindingOfATypeWithNoRepresentationIsRefused) {
    // A binding whose written type this file cannot map at all is refused by its type
    // rather than by the comparison above it, so the message names the type. `<auto>` is
    // the reachable case and it is a real one: a `let` infers from its initialiser and a
    // binding has none, so there is nothing to infer from -- inferring the element type
    // would be a front-end rule, and the front end has not made it.
    const Built b = build(
        "fun main() <noret> { let a <[int, 3]> = [1, 2, 3];"
        " foreach (e <auto> in a) { } }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a 'foreach' binding of type 'auto'"), std::string::npos)
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, AForeachIndexBindingThatIsNotAnIntegerIsRefused) {
    // What the index is handed is a position, and only an integer holds one as the number
    // the body compares against an index (`a[idx]`, loops.fin:20). A `float` would arrive
    // as 0.0, 1.0, ... and compare equal against an `int` index by conversion, which is a
    // rule this file would have invented; a `bool` would be true for every element but
    // the first. Refused rather than converted, and paired with the widths that do work
    // in AForeachIndexTakesAnyIntegerWidth -- `int{8}` moved to that list when the
    // width became real, and `int{7}` is here in its place because a width this
    // compiler cannot represent holds no position either.
    for (const char* type : {"float", "double", "bool", "string", "int{7}"}) {
        const Built b = build(
            "fun main() <noret> {\n"
            "    let a <[int, 3]> = [1, 2, 3];\n"
            "    foreach (i <" + std::string(type) + ">, e <int> in a) { }\n"
            "}\n");
        EXPECT_NE(b.compileExit, 0) << type << "\n" << b.why();
        EXPECT_NE(b.compileErr.find("index binding"), std::string::npos)
            << type << "\n" << b.why();
    }
}

BACKEND_TEST(Soundness_Codegen, AForeachOutsideAFunctionIsRefused) {
    // A loop at module scope has no frame to put its counter in, and there is no
    // module initialiser to run it in either -- the same answer every statement outside a
    // function gets here. Paired with the identical loop inside `main`.
    const Built refused = build(
        "let a <[int, 2]> = [1, 2];\n"
        "foreach (e <int> in a) { }\n"
        "fun main() <noret> { }\n");
    EXPECT_NE(refused.compileExit, 0) << refused.why();
    EXPECT_NE(refused.compileErr.find("outside a function"), std::string::npos)
        << refused.why();

    const Built lowered = build(std::string(kPrintf) +
        "let a <[int, 2]> = [1, 2];\n"
        "fun main() <noret> { foreach (e <int> in a) { printf(\"%d\", e); } }\n");
    ASSERT_TRUE(lowered.ran) << lowered.why();
    EXPECT_EQ(lowered.out, "12") << lowered.why();
}

// ---------------------------------------------------------------------------
// An enum without payloads is an integer.
//
// Which integer is the ruling this slice makes: `int`-wide and signed, because the
// analyzer checks every written member value against `int`
// (Analyzer_Decl.cpp, visit(EnumDeclaration&)) and every corpus enum numbers its
// members with small non-negative literals. It is also what C does, which matters
// at an `@define` boundary.
//
// A member *with* a payload is refused. `Result { Ok <T>, Err <U> }` is a tagged
// union whose layout -- where the tag sits, whether the payloads overlap, what the
// alignment of the whole is -- is an owner ruling and not a detail to be picked
// here, and picking one would be an ABI other passes would then have to match.

BACKEND_TEST(Soundness_Codegen, AFieldlessEnumMemberIsAConstant) {
    // arrays_enums.fin:3-6 and :17 verbatim in shape: a written zero, and the member
    // read by its bare name rather than through the enum.
    const Built b = build(std::string(kPrintf) +
        "enum Status {\n"
        "    OK = 0,\n"
        "    ERROR\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <Status> = OK;\n"
        "    printf(\"%d\\n\", s);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnUnnumberedMemberFollowsTheOneBeforeIt) {
    const Built b = build(std::string(kPrintf) +
        "enum Status {\n"
        "    OK = 0,\n"
        "    ERROR\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <Status> = ERROR;\n"
        "    printf(\"%d\\n\", s);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, NumberingResumesFromTheLastWrittenValue) {
    // operators.fin:6-9: `State { Alive = 1, Dead }`. Dead is 2 and not 1, which is
    // the difference between counting from the member before it and counting
    // positions.
    const Built b = build(std::string(kPrintf) +
        "enum State {\n"
        "    Alive = 1,\n"
        "    Dead\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d\\n\", Alive, Dead);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumWithNoWrittenValuesCountsFromZero) {
    // extern_as.fin:35-37: `MyEnum { A, B, C }`.
    const Built b = build(std::string(kPrintf) +
        "enum MyEnum { A, B, C }\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d %d\\n\", A, B, C);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0 1 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AQualifiedMemberIsTheSameConstant) {
    // extern_as.fin:44-45 writes both spellings of the same member two lines apart.
    const Built b = build(std::string(kPrintf) +
        "enum MyEnum { A, B, C }\n"
        "fun main() <noret> {\n"
        "    let a <MyEnum> = B;\n"
        "    let b <MyEnum> = MyEnum::B;\n"
        "    printf(\"%d %d\\n\", a, b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWrittenValueMayBeNegative) {
    // The values are signed, so the reader is not the unsigned extent reader with a
    // sign bolted on: `-1` is a UnaryOp over a Literal and has to come out as -1
    // rather than as a very large unsigned number.
    const Built b = build(std::string(kPrintf) +
        "enum Sign { Neg = -1, Zero, Pos }\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d %d\\n\", Neg, Zero, Pos);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "-1 0 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoMembersMayShareAValue) {
    // Nothing in the language says the values are distinct, and C's do not have to
    // be. This is here so that a later uniqueness check is a deliberate change.
    const Built b = build(std::string(kPrintf) +
        "enum E { A = 3, B = 3 }\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d\\n\", A, B);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3 3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumValueIsComparedByItsNumber) {
    const Built b = build(std::string(kPrintf) +
        "enum Status { OK = 0, ERROR }\n"
        "fun main() <noret> {\n"
        "    let s <Status> = ERROR;\n"
        "    if (s == ERROR) { printf(\"yes\\n\"); }\n"
        "    if (s == OK) { printf(\"no\\n\"); }\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "yes\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumVariableIsReassigned) {
    const Built b = build(std::string(kPrintf) +
        "enum Status { OK = 0, ERROR }\n"
        "fun main() <noret> {\n"
        "    let s <Status> = OK;\n"
        "    s = ERROR;\n"
        "    printf(\"%d\\n\", s);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumCrossesAFinToFinCallAndComesBack) {
    const Built b = build(std::string(kPrintf) +
        "enum Status { OK = 0, ERROR }\n"
        "fun echo(s: Status) <Status> { return s; }\n"
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", echo(ERROR));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumFieldOfAStructRoundTrips) {
    // The enum has to be registered before the structs are, because a field of enum
    // type needs its representation to exist -- the same ordering a field of struct
    // type needs.
    const Built b = build(std::string(kPrintf) +
        "enum Status { OK = 0, ERROR }\n"
        "struct Reply { st <Status>, n <int> }\n"
        "fun main() <noret> {\n"
        "    let r <Reply> = Reply { st: ERROR, n: 7 };\n"
        "    printf(\"%d %d\\n\", r.st, r.n);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayOfEnumsIndexes) {
    const Built b = build(std::string(kPrintf) +
        "enum Status { OK = 0, ERROR }\n"
        "fun main() <noret> {\n"
        "    let a <[Status, 3]> = [OK, ERROR, OK];\n"
        "    printf(\"%d %d %d\\n\", a[0], a[1], a[2]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0 1 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ALocalOutranksAnEnumMemberOfTheSameName) {
    // The analyzer defines an enumerator in the scope the enum was declared in, so a
    // local of that name shadows it. If the backend looked the name up in its enum
    // table first, this would print 5's member value instead of 5.
    const Built b = build(std::string(kPrintf) +
        "enum Status { OK = 0, ERROR }\n"
        "fun main() <noret> {\n"
        "    let OK <int> = 5;\n"
        "    printf(\"%d\\n\", OK);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumDeclaredBelowItsUseLowers) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", Late);\n"
        "}\n"
        "enum E { Early = 4, Late }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericEnumInstantiatesPerArgumentList) {
    // The flip of the refusal that used to be pinned here. The instantiation
    // machinery the struct path has -- mangled names, substitutions,
    // per-argument-list layout, members resolved through the arguments the
    // analyzer recorded -- now covers enums (instantiateEnumGeneric). Two
    // instantiations of one template are two types with two payloads; both
    // construct, and both read back by position.
    const Built b = build(std::string(kPrintf) +
        "enum Res <T> { Ok <T>, Err <T> }\n"
        "fun main() <noret> {\n"
        "    let a <Res<int>> = Ok(7);\n"
        "    let l <Res<long>> = Ok(21);\n"
        "    printf(\"%d %lld\\n\", a.0, l.0);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7 21\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericFunctionInfersThroughAnEnumArgument) {
    // The call-boundary mirror of the struct path: `unbox(b: Box<T>)` infers T
    // from the argument's instantiation, and `get(r: Res<T>)` must infer it
    // from the enum's -- the instantiation already knows what its parameters
    // became (instantiateEnumGeneric), so the inner binding is read off it
    // rather than re-derived. Used to refuse `a call to 'get' whose type
    // argument 'T' no argument mentions`.
    const Built b = build(std::string(kPrintf) +
        "enum Res <T> { Ok <T>, Err <T> }\n"
        "fun get<T>(r: Res<T>) <T> {\n"
        "    return r.0;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let r <Res<int>> = Ok(10);\n"
        "    printf(\"%d\\n\", get(r));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "10\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumMethodRunsTheTagIdiom) {
    // lib/std/stdio.fin's `unwrap` shape, end to end: a generic enum, an
    // `implements`-block method taking the enum as its first *parameter*
    // (Soundness_EnumMethodReceiver), the `getkeyid(enum_) == keyidof(Ok)`
    // dispatch, and `enum_.0` for the payload. All three halves used to refuse:
    // a method on an enum had no lowering, and the two tag intrinsics lowered
    // as calls to `#[llvm_name]` symbols nothing defines.
    const std::string decls =
        "fun getkeyid(value: any) <int>;\n"
        "fun keyidof(enum_member: $enum_member) <int>;\n"
        "enum Res <T> { Ok <T>, Err <T> }\n"
        "interface ResApi <T> {\n"
        "    fun unwrap(enum_: Res<T>) <T>;\n"
        "  }\n"
        "Res<T> implements <ResApi> {\n"
        "    pub fun unwrap(enum_: Res<T>) <T> {\n"
        "        if (getkeyid(enum_) == keyidof(Ok)) {\n"
        "            return enum_.0;\n"
        "          }\n"
        "        return enum_.0;\n"
        "      }\n"
        "  }\n";
    const Built b = build(decls +
        "fun main() <noret> {\n"
        "    let a <Res<int>> = Ok(7);\n"
        "    let e <Res<int>> = Err(3);\n"
        "    printf(\"%d %d\\n\", a.unwrap(), e.unwrap());\n"
        "  }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7 3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumWithPayloadsLowersAsTaggedUnion) {
    // ADR 0041: An enum with member payloads lowers as a tagged union with 32-bit discriminant.
    const Built b = build(
        "enum R { Ok(int), Err }\n"
        "fun main() <noret> { }\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumPayloadConstructorConstructs) {
    const Built b = build(
        "enum Color {\n"
        "    RGB(uint{8}, uint{8}, uint{8}),\n"
        "    RGBA(uint{8}, uint{8}, uint{8}, uint{8})\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let c <Color> = Color::RGB(100, 200, 50);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumPayloadMemberIsReadByPosition) {
    const Built b = build(
        "@define printf(fmt: string, ...) <noret>;\n"
        "enum Color {\n"
        "    RGB(uint{8}, uint{8}, uint{8}),\n"
        "    RGBA(uint{8}, uint{8}, uint{8}, uint{8})\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let c <Color> = Color::RGB(100, 200, 50);\n"
        "    if (c.0 == 100) {\n"
        "        printf(\"OK\\n\");\n"
        "    }\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "OK\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumPayloadMemberCanBeMutatedByPosition) {
    const Built b = build(
        "@define printf(fmt: string, ...) <noret>;\n"
        "enum Color {\n"
        "    RGB(uint{8}, uint{8}, uint{8})\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let c <Color> = Color::RGB(100, 200, 50);\n"
        "    c.0 = 250;\n"
        "    if (c.0 == 250) {\n"
        "        printf(\"MUTATED\\n\");\n"
        "    }\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "MUTATED\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumPayloadMemberWithZeroArgsConstructsAndDiscriminates) {
    const Built b = build(
        "@define printf(fmt: string, ...) <noret>;\n"
        "enum R {\n"
        "    Ok(int),\n"
        "    Err\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let r <R> = R::Err;\n"
        "    let ok <R> = R::Ok(42);\n"
        "    if (r != ok) {\n"
        "        printf(\"DIFFERENT\\n\");\n"
        "    }\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "DIFFERENT\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumPayloadCrossesAFunctionCall) {
    const Built b = build(
        "@define printf(fmt: string, ...) <noret>;\n"
        "enum Opt {\n"
        "    Some(int),\n"
        "    None\n"
        "}\n"
        "fun inspect(o: Opt) <int> {\n"
        "    return o.0;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <Opt> = Opt::Some(999);\n"
        "    if (inspect(s) == 999) {\n"
        "        printf(\"ROUNDTRIP\\n\");\n"
        "    }\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ROUNDTRIP\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericEnumInstantiatesWithFieldlessMembers) {
    // The flip of the refusal that used to be pinned here: `enum E<T> { A, B }`
    // is a template whose members carry no payload, and a member of it is its tag
    // -- the same i32 a member of a non-generic fieldless enum is, since the
    // member order fixes the numbers and no argument can change them. The
    // *instantiation* is what a program names, and one nothing names is not
    // emitted at all (the same evidence `struct M <T> {}` gives, blame_assert.fin:19).
    const Built b = build(
        "enum E<T> { A, B }\n"
        "fun main() <noret> { }\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumMemberValueThatIsNotConstantIsRefused) {
    // Not a program error the analyzer let through -- it checks the value against
    // `int` and `1 + 1` is an int -- so the backend is the first pass that needs the
    // *number* and the first that can say it does not have one. Reading it as
    // anything (least of all as the position) would number the member silently
    // wrong.
    const Built b = build(
        "enum E { A = 1 + 1, B }\n"
        "fun main() <noret> { }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("value"), std::string::npos) << b.why();
}

// ---------------------------------------------------------------------------
// A global, which is a variable whose home is the object file rather than a frame.
//
// `tests/samples/variables.fin:6-11` writes four of them and calls module scope out
// as its own thing: a `const` "cannot be reassigned", a `let` "can be changed from
// outside of program". So the lowering is an `llvm::GlobalVariable` with external
// linkage -- Fin does not mangle, so the name in the object is the name in the
// source, and that is what makes `extern` and `@define` able to reach one.
//
// The initialiser must be a *constant*. Not a limitation of this pass so much as a
// question it is not allowed to answer: code that runs before `main` runs at some
// point in some order relative to every other module's, and picking one here would
// be inventing the initialisation-order rule. `= f()` is refused, which is C's
// answer and not C++'s.
//
// A `const` becomes an LLVM constant global, which is what lets it fold into the
// code that reads it. The analyzer already refuses assigning one, so the two agree.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AGlobalIsReadFromAFunction) {
    // tests/samples/extern_as.fin:6 (`const myglobv <int> = 10;`).
    const Built b = build(std::string(kPrintf) +
        "const K <int> = 10;\n"
        "let Counter <int> = 3;\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d\\n\", K, Counter);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "10 3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGlobalWithNoInitialiserIsZero) {
    // The same answer a local with no initialiser gets, and here it is also what the
    // object file does anyway: a global with no value lives in .bss.
    const Built b = build(std::string(kPrintf) +
        "let Counter <int>;\n"
        "fun main() <noret> { printf(\"%d\\n\", Counter); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGlobalIsAssignedAndKeepsItsValueAcrossCalls) {
    // The point of a global: one home, and every function sees the same one. A
    // counter bumped by a callee and read by the caller is the smallest program that
    // cannot be done with locals.
    const Built b = build(std::string(kPrintf) +
        "let Counter <int> = 0;\n"
        "fun bump() <noret> { Counter = Counter + 1; }\n"
        "fun main() <noret> {\n"
        "    bump();\n"
        "    bump();\n"
        "    bump();\n"
        "    printf(\"%d\\n\", Counter);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ACompoundAssignmentToAGlobalReadsAndWritesTheSameHome) {
    const Built b = build(std::string(kPrintf) +
        "let Counter <int> = 10;\n"
        "fun main() <noret> {\n"
        "    Counter += 5;\n"
        "    Counter++;\n"
        "    printf(\"%d\\n\", Counter);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "16\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ALocalOutranksAGlobalOfTheSameName) {
    // The scope the analyzer resolved it in, and the same rule the enumerators
    // already follow (ALocalOutranksAnEnumMemberOfTheSameName).
    const Built b = build(std::string(kPrintf) +
        "let G <int> = 1;\n"
        "fun main() <noret> {\n"
        "    let G <int> = 2;\n"
        "    printf(\"%d\\n\", G);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGlobalOfEachScalarTypeRoundTrips) {
    // No `<double>` here: a float literal does not widen to double in the front end
    // yet (a booked gap), which is a front-end limit and not a global one -- the
    // `%f` promotion still exercises float, since printf's varargs widen it.
    const Built b = build(std::string(kPrintf) +
        "const F <float> = 3.5;\n"
        "const B <bool> = true;\n"
        "const S <string> = \"hi\";\n"
        "const L <long> = 9000000000;\n"
        "fun main() <noret> {\n"
        "    printf(\"%.2f %d %s %lld\\n\", F, B, S, L);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3.50 1 hi 9000000000\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGlobalWithAnInferredTypeTakesItsInitialisers) {
    // tests/samples/variables.fin:7 (`const MAX_FILE_SIZE <auto> = 1000;`).
    const Built b = build(std::string(kPrintf) +
        "const MAX <auto> = 1000;\n"
        "fun main() <noret> { printf(\"%d\\n\", MAX); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1000\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGlobalArrayIsIndexedAndAssigned) {
    const Built b = build(std::string(kPrintf) +
        "let Cells <[int, 3]> = [7, 8, 9];\n"
        "fun main() <noret> {\n"
        "    Cells[1] = 42;\n"
        "    printf(\"%d %d %d\\n\", Cells[0], Cells[1], Cells[2]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7 42 9\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGlobalStructHasItsFieldsReadAndWritten) {
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> = 5 }\n"
        "let Origin <P> = P { a: 1 };\n"
        "fun main() <noret> {\n"
        "    Origin.a = 9;\n"
        "    printf(\"%d %d\\n\", Origin.a, Origin.b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9 5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGlobalOfEnumTypeIsItsMembersNumber) {
    const Built b = build(std::string(kPrintf) +
        "enum E { A = 1, B }\n"
        "const G <E> = B;\n"
        "fun main() <noret> { printf(\"%d\\n\", G); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGlobalsInitialiserMayBeAConstantExpression) {
    // Folded, not run: `2 + 3 * 4` is a number by the time the object is written.
    const Built b = build(std::string(kPrintf) +
        "const N <int> = 2 + 3 * 4;\n"
        "const M <int> = sizeof(long);\n"
        "fun main() <noret> { printf(\"%d %d\\n\", N, M); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "14 8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGlobalInitialisedByACallIsRefused) {
    // When it would run, and in what order against every other module's, is the
    // initialisation-order rule -- and that is a decision rather than a pass. C
    // refuses this too; C++ does not, and pays for it.
    const Built b = build(std::string(kPrintf) +
        "fun one() <int> { return 1; }\n"
        "let G <int> = one();\n"
        "fun main() <noret> { printf(\"%d\\n\", G); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGlobalWithAnAttributeIsRefused) {
    // `#[slaveof($Fin)]` (variables.fin:35) is a lifetime instruction, and a global
    // already outlives everything -- but an attribute this file does not read may be
    // one that changes where the variable lives. The same rule as a struct's.
    // The one exception is flag-form `#[export]` (ADR 0033), accepted in
    // AnExportOnAGlobalIsVacuous below: it is import-visibility, not a storage
    // question.
    const Built b = build(std::string(kPrintf) +
        "#[slaveof($Fin)]\n"
        "const G <int> = 1;\n"
        "fun main() <noret> { printf(\"%d\\n\", G); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnExportOnAGlobalIsVacuous) {
    // math.fin writes `#[export]` above its `pub const`s (`PI`, `E`, `PI_F`),
    // and the module has to compile standalone with `-c`. ADR 0033: export is
    // who may name the declaration from an import -- the analyzer's decision,
    // already made -- and it asks nothing about where the storage lives, so the
    // global lands in its ordinary section with the ordinary initializer. The
    // value is what pins that: a build that relocated or dropped the storage
    // would not print the constant.
    const Built b = build(std::string(kPrintf) +
        "#[export]\n"
        "pub const G <int> = 42;\n"
        "fun main() <noret> { printf(\"%d\\n\", G); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGlobalDeclaredBelowItsUseLowers) {
    const Built b = build(std::string(kPrintf) +
        "fun get() <int> { return G; }\n"
        "let G <int> = 7;\n"
        "fun main() <noret> { printf(\"%d\\n\", get()); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

// ---------------------------------------------------------------------------
// `sizeof`, which is a number the layout has already decided.
//
// The grammar takes a type and only a type -- `sizeof(1 + 1)` is a syntax error and
// `sizeof(a)` parses as the type `a` -- so there is no expression to evaluate and no
// question about evaluating one twice. The number comes from the module's own
// DataLayout, which is the same table the emitted GEPs and allocas use, so a
// `sizeof` can never disagree with the code that indexes the thing it measured.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, SizeofAScalarIsItsWidth) {
    // The widths src/types/Layout.hpp declares: `int` 4 and `long` 8 because the
    // corpus writes `%d` and `%lld`, `bool` a byte in storage though an i1 in a
    // register, `char` 1.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d %d %d %d %d %d\\n\", sizeof(char), sizeof(bool), sizeof(int),\n"
        "           sizeof(long), sizeof(float), sizeof(double));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 1 4 8 4 8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, SizeofAStringIsAPointer) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", sizeof(string));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, SizeofAStructIsItsLaidOutSizeAndNotItsFieldsAdded) {
    // deeptest1.fin:35 is the corpus site (`sizeof(Vector2)`). `Padded` is here
    // because a size that adds the fields up gets 9 and the answer is 16: the
    // padding and the tail padding are both the layout's, not this expression's.
    const Built b = build(std::string(kPrintf) +
        "struct Pair { a <int>, b <int> }\n"
        "struct Padded { a <char>, b <long> }\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d\\n\", sizeof(Pair), sizeof(Padded));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8 16\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, SizeofAFixedArrayIsItsElementsStride) {
    // Not element size times count in general -- an array of a padded struct strides
    // by the padded size -- which is why this asks LLVM rather than multiplying.
    const Built b = build(std::string(kPrintf) +
        "struct Padded { a <char>, b <long> }\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d %d\\n\", sizeof([int, 4]), sizeof([Padded, 2]),\n"
        "           sizeof([[int, 2], 3]));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "16 32 24\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, SizeofIsAConstantAndNotACall) {
    // Usable where a constant is: as an array extent's neighbour, in arithmetic, and
    // as an argument. If it were emitted as anything else this would still pass --
    // what it guards is that it is an ordinary int value with no statement behind it.
    const Built b = build(std::string(kPrintf) +
        "fun twice(n: int) <int> { return n * 2; }\n"
        "fun main() <noret> {\n"
        "    let n <int> = sizeof(long) * 3 - 1;\n"
        "    printf(\"%d %d\\n\", n, twice(sizeof(int)));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "23 8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, SizeofAVoidIsRefused) {
    // The front end accepts it -- `void` is a type name and sizeof takes a type --
    // so the backend is the first pass that has to answer, and 0 would be an answer
    // to a question that has none.
    const Built b = build("fun main() <noret> { let n <int> = sizeof(void); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("sizeof"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, SizeofADynamicArrayIsThePairAndNotAPointer) {
    // Was SizeofATypeWithNoRepresentationIsRefused, whose argument was: "A dynamic
    // `[T]`: how one is represented is undecided, so its size is the same undecided
    // thing rather than a pointer's width guessed here." That was right while the
    // representation was open, and ADR 0025 has since decided it -- so the size is no
    // longer a guess and the refusal is no longer honest.
    //
    // The number is what makes this worth asserting rather than merely flipping. On a
    // 64-bit target the pair is 8 bytes of pointer, 4 of length, and 4 of tail padding
    // to the pointer's alignment: 16. The old comment's feared wrong answer -- "a
    // pointer's width" -- is 8, so this assertion is precisely what tells a correct
    // lowering from the one that treats a `[T]` as its data pointer alone.
    //
    // It is also a cross-check between the two passes: this reads the backend's number
    // through `sizeof`, and Soundness_Layout.ADynamicArrayHasAPointerAndLengthLayout
    // computes the same one in LayoutEngine, which is the pass a collector asks. Two
    // tables that agree today are two tables that disagree after one edit, and the
    // disagreement would be an ABI split in which every program still runs.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> { printf(\"%d\\n\", sizeof([int])); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "16\n") << b.why();
}

// ---------------------------------------------------------------------------
// The layout table and the backend are the same table.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Structs.
//
// The layout pass (6b908c0) computes offsets and LLVM's own DataLayout agrees
// with it, which settles where a field *is*. These settle that the emitted code
// reads and writes it there -- a different question, and the one that produces a
// program that runs and prints the wrong number rather than a program that fails
// to build.
//
// Every test here prints, because a struct is the first construct in this
// language whose lowering can be wrong in a way that type-checks: a field read at
// the wrong offset is still a well-typed int. The value printed is chosen so that
// reading the neighbouring field, or the same field of the other struct, gives a
// different answer.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AStructFieldIsReadBack) {
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 3, b: 4 };\n"
        "    printf(\"%d %d\\n\", p.a, p.b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3 4\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructFieldIsAssignedInPlace) {
    // The write has to land in the slot the read comes from. Both fields are
    // printed because writing `a` at `b`'s offset shows up as `b` changing.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 3, b: 4 };\n"
        "    p.a = 10;\n"
        "    printf(\"%d %d\\n\", p.a, p.b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "10 4\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructFieldTakesACompoundAssignment) {
    // `p.a += 5` reads and writes one place, and the address must be computed once
    // for both halves -- not once for the load and again for the store, which is
    // the same answer here and a different one as soon as the object expression
    // has a side effect.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 10, b: 100 };\n"
        "    p.a += 5;\n"
        "    p.b -= 1;\n"
        "    printf(\"%d %d\\n\", p.a, p.b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "15 99\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructLiteralFollowsDeclarationOrderNotWrittenOrder) {
    // The literal names its fields, so the order they are written in is the
    // author's convenience and the order they are stored in is the declaration's.
    // An emitter that walked the literal and stored to offset 0, 1, 2 in the order
    // it read them would pass every other test in this file and fail this one.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int>, c <int> }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { c: 3, a: 1, b: 2 };\n"
        "    printf(\"%d %d %d\\n\", p.a, p.b, p.c);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2 3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructIsCopiedWhenAssigned) {
    // A Fin struct is a value. `q = p` copies, so writing through `q` must not be
    // visible through `p` -- the failure being one slot aliased by two names,
    // which is what lowering a struct as a pointer would give.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 1, b: 2 };\n"
        "    let q <P> = p;\n"
        "    q.a = 99;\n"
        "    printf(\"%d %d\\n\", p.a, q.a);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 99\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructIsPassedByValue) {
    // The callee gets a copy: its write is not the caller's. Same rule as the
    // assignment above, at the one boundary where getting it wrong is invisible
    // inside either function.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> }\n"
        "fun bump(p: P) <int> {\n"
        "    p.a = 50;\n"
        "    return p.a + p.b;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 1, b: 2 };\n"
        "    let n <int> = bump(p);\n"
        "    printf(\"%d %d\\n\", n, p.a);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "52 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructIsReturnedByValue) {
    // A struct built in a callee's frame and returned has to survive the frame
    // going away. Returning the address of the callee's alloca compiles, links,
    // and reads freed stack.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> }\n"
        "fun make(x: int) <P> { return P { a: x, b: x + 1 }; }\n"
        "fun main() <noret> {\n"
        "    let q <P> = make(5);\n"
        "    printf(\"%d %d\\n\", q.a, q.b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5 6\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AReturnedStructSurvivesAnInterveningCall) {
    // The same question as above, asked in the way that actually catches it: a
    // second call reuses the stack the first one returned from, so a struct
    // returned by address is intact right up until anything else runs.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> }\n"
        "fun make(x: int) <P> { return P { a: x, b: x + 1 }; }\n"
        "fun noise(x: int) <int> { let junk <P> = P { a: 777, b: 888 }; return junk.a + x; }\n"
        "fun main() <noret> {\n"
        "    let q <P> = make(5);\n"
        "    let n <int> = noise(1);\n"
        "    printf(\"%d %d %d\\n\", q.a, q.b, n);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5 6 778\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedStructFieldIsReadBack) {
    // `o.i.v` is two GEPs, and the second one is relative to the first. Getting
    // that wrong reads from the outer struct's base.
    const Built b = build(std::string(kPrintf) +
        "struct In { v <int>, w <int> }\n"
        "struct Out { pad <int>, i <In>, tail <int> }\n"
        "fun main() <noret> {\n"
        "    let o <Out> = Out { pad: 9, i: In { v: 1, w: 2 }, tail: 8 };\n"
        "    printf(\"%d %d %d %d\\n\", o.pad, o.i.v, o.i.w, o.tail);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9 1 2 8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedStructIsAssignedThrough) {
    const Built b = build(std::string(kPrintf) +
        "struct In { v <int>, w <int> }\n"
        "struct Out { pad <int>, i <In>, tail <int> }\n"
        "fun main() <noret> {\n"
        "    let o <Out> = Out { pad: 9, i: In { v: 1, w: 2 }, tail: 8 };\n"
        "    o.i.w = 20;\n"
        "    printf(\"%d %d %d %d\\n\", o.pad, o.i.v, o.i.w, o.tail);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9 1 20 8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedStructIsCopiedWhole) {
    // Assigning the inner struct out and writing to the copy: the original must
    // not move. A whole-aggregate load and store, not a field-by-field one.
    const Built b = build(std::string(kPrintf) +
        "struct In { v <int>, w <int> }\n"
        "struct Out { i <In> }\n"
        "fun main() <noret> {\n"
        "    let o <Out> = Out { i: In { v: 1, w: 2 } };\n"
        "    let copy <In> = o.i;\n"
        "    copy.v = 77;\n"
        "    printf(\"%d %d\\n\", o.i.v, copy.v);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 77\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, EveryScalarFieldWidthRoundTrips) {
    // One struct holding one field of each lowered width, all written and all read
    // back. This is where a field whose *size* is right and whose *offset* is off
    // by the padding shows up, because a narrow field followed by a wide one is
    // exactly where the padding is.
    //
    // `d` is written through cast<double> because a bare float literal types as
    // `float` and there is no implicit widening -- a booked gap, not a lowering
    // question. printf reads %d for the narrow integers because a C variadic
    // promotes them, which is the same boundary FloatsAreDoublesAtTheVarargBoundary
    // covers.
    const Built b = build(std::string(kPrintf) +
        "struct M {\n"
        "    c <char>,\n"
        "    s <short>,\n"
        "    i <int>,\n"
        "    l <long>,\n"
        "    f <float>,\n"
        "    d <double>,\n"
        "    str <string>,\n"
        "    bo <bool>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let m <M> = M {\n"
        "        c: 7, s: 300, i: 70000, l: 5000000000,\n"
        "        f: 1.5, d: cast<double>(2.25), str: \"hi\", bo: true\n"
        "    };\n"
        "    printf(\"%d %d %d %lld %.2f %.2f %s %d\\n\",\n"
        "           m.c, m.s, m.i, m.l, m.f, m.d, m.str, m.bo);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7 300 70000 5000000000 1.50 2.25 hi 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APaddedStructKeepsItsFieldsApart) {
    // char, long, char: the shape with padding both after the first field and at
    // the end. Writing each field must not disturb the others, which is the
    // observable form of "the offsets are the ones the layout pass computed".
    const Built b = build(std::string(kPrintf) +
        "struct Pad { a <char>, big <long>, z <char> }\n"
        "fun main() <noret> {\n"
        "    let p <Pad> = Pad { a: 1, big: 5000000000, z: 2 };\n"
        "    p.big = 4000000000;\n"
        "    printf(\"%d %lld %d\\n\", p.a, p.big, p.z);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 4000000000 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnOmittedFieldIsZeroed) {
    // The front end admits a literal that names some of the fields
    // (`P { a: 1 }` with `b` unmentioned). The backend has to put *something*
    // there, and zero is the same answer a local with no initialiser already
    // gets -- undefined stack contents being the one answer that cannot be
    // tested. Zero is the answer for a field with *no* default; a field that
    // declares one gets it instead (AnOmittedFieldTakesItsDeclaredDefault).
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 1 };\n"
        "    printf(\"%d %d\\n\", p.a, p.b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructWithNoInitialiserIsZeroed) {
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> }\n"
        "fun main() <noret> {\n"
        "    let p <P>;\n"
        "    printf(\"%d %d\\n\", p.a, p.b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoStructsWithTheSameFieldNameUseTheirOwnOffsets) {
    // `v` is at offset 8 in A and offset 0 in B. A field index looked up by name
    // in one table shared across struct types gives the same answer for both, and
    // reads eight bytes past the start of a B.
    const Built b = build(std::string(kPrintf) +
        "struct A { pad <long>, v <int> }\n"
        "struct B { v <int>, pad <long> }\n"
        "fun main() <noret> {\n"
        "    let a <A> = A { pad: 1, v: 2 };\n"
        "    let bb <B> = B { v: 3, pad: 4 };\n"
        "    printf(\"%d %d\\n\", a.v, bb.v);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2 3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructDeclaredBelowItsUseLowers) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 41, b: 1 };\n"
        "    printf(\"%d\\n\", p.a + p.b);\n"
        "}\n"
        "struct P { a <int>, b <int> }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructIsAParameterAndAReturnTogether) {
    // The shape a program actually writes: take one, return a different one.
    const Built b = build(std::string(kPrintf) +
        "struct Point { x <int>, y <int> }\n"
        "fun shift(p: Point, by: int) <Point> {\n"
        "    return Point { x: p.x + by, y: p.y + by };\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point { x: 1, y: 2 };\n"
        "    let q <Point> = shift(p, 10);\n"
        "    printf(\"%d %d %d %d\\n\", p.x, p.y, q.x, q.y);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2 11 12\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructRoundTripsThroughAConditional) {
    // A struct assigned in one arm of an `if` and read after it: the slot has to
    // be the same one on both paths, which is what allocating per-declaration
    // rather than per-assignment gives.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> }\n"
        "fun pick(n: int) <P> {\n"
        "    let p <P> = P { a: 0, b: 0 };\n"
        "    if (n > 0) { p = P { a: 1, b: 2 }; } else { p = P { a: 3, b: 4 }; }\n"
        "    return p;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let hi <P> = pick(1);\n"
        "    let lo <P> = pick(-1);\n"
        "    printf(\"%d %d %d %d\\n\", hi.a, hi.b, lo.a, lo.b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2 3 4\n") << b.why();
}

// The refusals that stay refusals. A struct lowering that quietly accepted any of
// these would be worse than one that refused them, because the front end has
// already said the program is well-typed.

BACKEND_TEST(Soundness_Codegen, AMethodCallOnAStructCallsIt) {
    // Was AMethodCallOnAStructIsRefused, inverted by the method unit at the bottom
    // of this file. `pub fun get()` writes no `self` and reads one, which is the
    // injected receiver -- struct_methods.fin:10 says the compiler supplies it
    // either way.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>\n"
        "    pub fun get() <int> { return self.a; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 1 };\n"
        "    let x <int> = p.get();\n"
        "    printf(\"%d\\n\", x);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructPassedToACVariadicIsRefused) {
    // printf's parameter is `...`, so the analyzer has nothing to check the
    // argument against and accepts it. The aggregate the backend would pass is not
    // what C's va_arg reads, so this is the one refusal in the struct set that
    // stands between a program that builds and a program that prints nonsense.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int> }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 1 };\n"
        "    printf(\"%d\\n\", p);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructOnAnExternBoundaryIsRefused) {
    // A Fin-to-Fin call agrees with itself about how a struct is passed because
    // both halves are emitted here. A C function does not: the platform ABI decides
    // per struct whether it arrives in registers or behind a pointer, and that
    // classification is clang's work, not LLVM's. Emitting the aggregate would link
    // and pass garbage.
    const Built b = build(
        "struct P { a <int>, b <int> }\n"
        "@define take(p: P) <noret>;\n"
        "fun main() <noret> { let p <P> = P { a: 1, b: 2 }; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

// ---------------------------------------------------------------------------
// A field default, which is a value the struct wrote once and every literal that
// omits the field inherits.
//
// Three rules, and the middle one is the one with teeth:
//
//   * a field the literal omits takes its default; with no default it is zero
//     (AnOmittedFieldIsZeroed), which is the same answer an uninitialised local
//     gets;
//   * the default is an *expression*, evaluated at each literal that omits the
//     field and not at all at a literal that writes it -- so a default that calls
//     something calls it once per instantiation, which is C++'s rule and the only
//     one under which `= now()` means anything;
//   * it is evaluated in the *struct's* scope and not the literal's. The analyzer
//     already resolved its names there (`x <int> = q` is "Undefined variable 'q'"
//     even with a `q` in scope at every use), and the backend has to agree: a
//     local at the use site must not be able to capture a name the declaration
//     resolved to something else.
//
// Order: what the literal writes evaluates in the order written, and the defaults
// fill in afterwards in declaration order. The defaults are not in the literal's
// text, so no order interleaves them with it -- putting them after is the only
// choice that does not run invisible code between two visible lines.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AnOmittedFieldTakesItsDeclaredDefault) {
    // tests/samples/deeptest1.fin:9 (`y <int> = 10`) is the corpus's first one.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> = 5 }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 1 };\n"
        "    printf(\"%d %d\\n\", p.a, p.b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWrittenValueBeatsTheDefault) {
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> = 5 }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 1, b: 2 };\n"
        "    printf(\"%d %d\\n\", p.a, p.b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ALiteralMayWriteNothingAtAll) {
    // `P { }` with every field defaulted, which is the shape a struct of options
    // is for. The empty literal parses and has to mean "all of them".
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int> = 1, b <int> = 2 }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { };\n"
        "    printf(\"%d %d\\n\", p.a, p.b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMixOfDefaultedAndBareFieldsFillsInBoth) {
    // The two rules in one literal: `b` gets its default, `a` gets zero.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> = 5 }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { };\n"
        "    printf(\"%d %d\\n\", p.a, p.b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0 5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADefaultMayBeAnExpression) {
    // Not just a literal. 14 rather than 20, so precedence is being read and not
    // the digits.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int> = 2 + 3 * 4 }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { };\n"
        "    printf(\"%d\\n\", p.a);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "14\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADefaultOfAStringFieldIsThatString) {
    const Built b = build(std::string(kPrintf) +
        "struct P { s <string> = \"hi\" }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { };\n"
        "    printf(\"%s\\n\", p.s);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "hi\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADefaultOfAnArrayFieldIsThatArray) {
    const Built b = build(std::string(kPrintf) +
        "struct P { cells <[int, 3]> = [7, 8, 9] }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { };\n"
        "    printf(\"%d %d %d\\n\", p.cells[0], p.cells[1], p.cells[2]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7 8 9\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADefaultOfAStructFieldIsThatStruct) {
    // A default that is itself a literal, and so runs the same fill-in one level
    // down: `Q { }` inside `P`'s default takes Q's own default.
    const Built b = build(std::string(kPrintf) +
        "struct Q { n <int> = 3 }\n"
        "struct P { q <Q> = Q { } }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { };\n"
        "    printf(\"%d\\n\", p.q.n);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADefaultRunsOncePerInstantiation) {
    // The default is an expression and not a stored constant, so a call in one runs
    // every time a literal omits the field. Two literals, two ticks.
    const Built b = build(std::string(kPrintf) +
        "fun tick() <int> { printf(\"tick \"); return 1; }\n"
        "struct P { a <int> = tick() }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { };\n"
        "    let q <P> = P { };\n"
        "    printf(\"| %d %d\\n\", p.a, q.a);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "tick tick | 1 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADefaultIsNotEvaluatedWhenTheFieldIsWritten) {
    // The other half of the same rule, and the half that would be a silent bug:
    // evaluating a default whose value is then overwritten still runs its effects.
    const Built b = build(std::string(kPrintf) +
        "fun tick() <int> { printf(\"tick \"); return 1; }\n"
        "struct P { a <int> = tick() }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 5 };\n"
        "    printf(\"| %d\\n\", p.a);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "| 5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TheWrittenValuesRunBeforeTheDefaults) {
    // The declared order is a, b; the literal writes only b. `b` runs first because
    // it is what the literal says, and `a`'s default fills in after -- see the
    // section note for why the invisible code goes last.
    const Built b = build(std::string(kPrintf) +
        "fun say(m: string) <int> { printf(\"%s\", m); return 1; }\n"
        "struct P { a <int> = say(\"a\"), b <int> }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { b: say(\"b\") };\n"
        "    printf(\"|\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ba|\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADefaultResolvesInTheStructsScopeAndNotTheUseSite) {
    // The one that could go wrong quietly. `A` in P's default is the enumerator the
    // declaration resolved it to; the `A` in scope at the literal is a different
    // thing holding a different value. Printing both is the only way to tell them
    // apart, and the backend must not read the local.
    const Built b = build(std::string(kPrintf) +
        "enum E { A = 1, B }\n"
        "struct P { x <E> = A }\n"
        "fun main() <noret> {\n"
        "    let A <E> = B;\n"
        "    let p <P> = P { };\n"
        "    printf(\"%d %d\\n\", p.x, A);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADefaultThatNamesAnotherFieldIsRefused) {
    // `y <int> = x` type-checks -- the analyzer has the members in scope while it
    // checks the defaults -- and means nothing: x's default, or x's written value,
    // or x at some point during a fill-in whose order is not the reader's. Refused
    // rather than given one of the three.
    const Built b = build(std::string(kPrintf) +
        "struct P { x <int> = 1, y <int> = x }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { };\n"
        "    printf(\"%d\\n\", p.y);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADefaultOfNullForAScalarIsZero) {
    // Was ADefaultOfNullForANonNullableFieldIsRefused. tests/samples/deeptest4.fin:6
    // writes `integer <int> = null`, the sample is normative, and the front end
    // takes it -- so the backend saying "no null int" was the compiler
    // disagreeing with the language, not caution. A `null` default on a scalar
    // is zero: exactly what omitting the field produces (the literal starts
    // from null and inserts what is written), so spelling the nothing changes
    // nothing. Pointers keep the null itself; anything else still refuses.
    const Built b = build(std::string(kPrintf) +
        "struct P { x <int> = null }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { };\n"
        "    printf(\"%d\\n\", p.x);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADefaultedFieldStillCrossesACall) {
    // The filled-in value is in the struct and not in the literal's shadow: it
    // survives being passed and returned.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> = 5 }\n"
        "fun sum(p: P) <int> { return p.a + p.b; }\n"
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", sum(P { a: 1 }));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "6\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnIncompleteStructIsRefused) {
    // `struct S;` names a type whose size nothing knows (stdlib/stdio.fin:42 writes
    // one). The analyzer admits a variable of it; the backend cannot allocate it and
    // must not pick a size.
    const Built b = build(
        "struct S;\n"
        "fun main() <noret> { let s <S>; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEmptyStructIsOneByteSoItsValuesHaveDistinctAddresses) {
    // Was AnEmptyStructIsRefused, which said the choice between LLVM's zero bytes and
    // C's one byte was a rule the language had not made. It has been made, and it is
    // C's -- so this test now asserts the consequence that decided it rather than the
    // refusal.
    //
    // The consequence is object identity. At zero bytes nothing stops two separately
    // declared values from being placed at one address, and `&a != &b` then reads
    // false for two variables the program has every reason to believe are two. One
    // byte is what C spends to make that impossible, C++ inherits it, and finc is a
    // C++ program that interops with C++ -- so an empty Fin struct crossing that
    // boundary has to be the size the other side already believes it is.
    //
    // Asserting the addresses rather than a `size_of`: the byte exists for the sake of
    // distinctness, and distinctness is the thing a reader of this test needs to see
    // held. A size assertion would pass just as well on a padding byte introduced for
    // some unrelated reason.
    const Built b = build(std::string(kPrintf) +
        "struct S { }\n"
        "fun main() <noret> {\n"
        "    let a <S>;\n"
        "    let b <S>;\n"
        "    let pa <&S> = &a;\n"
        "    let pb <&S> = &b;\n"
        "    if (pa != pb) { printf(\"distinct\\n\"); } else { printf(\"same\\n\"); }\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "distinct\n") << b.why();
}

// ---------------------------------------------------------------------------
// Struct inheritance: the base's fields splice in at offset 0.
//
// The owner's ruling, and src/types/Layout.cpp:428-451 already computed it for the
// collector before the backend could emit it -- so this unit is the backend agreeing
// with a number the type layer had already fixed, which is the safer direction. The
// two are checked against each other by
// Soundness_Layout.AnInheritedFieldComesBeforeTheOnesDeclaredHere over there and by
// the sizes asserted here.
//
// Every test prints or asserts a value. A wrong splice is not a build failure: it is
// a well-typed read at the wrong offset, which is exactly the failure mode the
// structs section above exists to catch.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// `try` / `catch`: the try block runs, the catch block does not exist at run time.
//
// Ruled 2026-08-28. Nothing in Fin raises anything a `catch` could receive --
// `blame`'s assert form prints and aborts, and its raise form is still refused -- so a
// handler for an event that cannot occur is honestly lowered as nothing. `try` becomes
// its block, and the block is a scope like any other.
//
// These tests are new because there were none: the refusal was never asserted anywhere,
// which is why lowering it broke nothing and also why nothing would have noticed if it
// had been lowered wrongly. The day a raise form lowers, ATryBlockRunsAndItsCatchDoesNot
// is what fails, and that failure is the signal to build a real mechanism.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, ATryBlockRunsAndItsCatchDoesNot) {
    // readonly.fin:48-52 in miniature. Both halves asserted in one test, because
    // "the try ran" and "the catch did not" are the two things that can go wrong
    // independently: emitting neither would silently drop the guarded statement, which
    // is the miscompile this file exists to prevent.
    const Built b = build(std::string(kPrintf) +
        "struct Error { message <string> }\n"
        "fun main() <noret> {\n"
        "    let n <int> = 1;\n"
        "    try {\n"
        "        n = 5;\n"
        "        printf(\"try ran\\n\");\n"
        "    } catch (Error as err) {\n"
        "        printf(\"catch ran\\n\");\n"
        "    }\n"
        "    printf(\"n=%d\\n\", n);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "try ran\nn=5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ATryBlocksSideEffectsSurviveIt) {
    // The guarded statement is not merely *reached*, its effect outlives the block --
    // `n = 5` above is read after the `try` closes. Separate from the test above
    // because a lowering that emitted the try block into a scope it then discarded
    // would print "try ran" and still report n=1.
    const Built b = build(std::string(kPrintf) +
        "struct Error { message <string> }\n"
        "fun main() <noret> {\n"
        "    let total <int> = 0;\n"
        "    try { total = total + 7; } catch (Error as err) { total = 100; }\n"
        "    try { total = total + 3; } catch (Error as err) { total = 200; }\n"
        "    printf(\"%d\\n\", total);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "10\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnUnlowerableStatementInsideATryIsStillRefused) {
    // `try` is a scope, not a suppression. A construct the backend cannot lower is
    // refused wherever it is written, and writing it inside a `try` must not turn the
    // refusal off -- that would be the "refuse, never skip" rule with a hole in it.
    const Built b = build(
        "struct Error { message <string> }\n"
        "fun main() <noret> {\n"
        "    try { m1778; } catch (Error as err) { }\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("'m1778'"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ACatchBlockIsStillAnalysedEvenThoughItIsNotEmitted) {
    // The front end walks the catch body and type-checks it (Analyzer_Stmt.cpp:148-156),
    // so skipping *code generation* for it does not make it an unchecked region. This
    // is the test that says the two passes disagree on purpose rather than by accident:
    // an undefined name in there is still a diagnostic, and it comes from the analyzer.
    const Built b = build(
        "struct Error { message <string> }\n"
        "fun main() <noret> {\n"
        "    try { } catch (Error as err) { let x <int> = nosuchname; }\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.compileErr.find("codegen:"), std::string::npos)
        << "the catch body's fault must come from the front end, not from the backend\n"
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInheritedFieldIsReadAndWrittenThroughTheDerivedStruct) {
    // The base's two fields, then the derived one's, and each distinct so that reading
    // a neighbour gives a different answer. The write half matters as much as the read:
    // an inherited field is a real slot, not a copy, so assigning through the derived
    // struct has to land in it.
    const Built b = build(std::string(kPrintf) +
        "struct Base { a <int>, b <int> }\n"
        "struct Derived: <Base> { c <int> }\n"
        "fun main() <noret> {\n"
        "    let d <Derived> = Derived{ a: 1, b: 2, c: 3 };\n"
        "    printf(\"%d %d %d\\n\", d.a, d.b, d.c);\n"
        "    d.a = 10;\n"
        "    d.c = 30;\n"
        "    printf(\"%d %d %d\\n\", d.a, d.b, d.c);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2 3\n10 2 30\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADerivedStructIsItsBasePlusItsOwnFields) {
    // The size is what says the splice happened rather than the fields being ignored.
    // Base is two ints; Derived must be three, not one -- a lowering that dropped the
    // inherited fields would report 4 here and every field of every derived struct in
    // the program would be at the wrong offset, in a program that still runs.
    const Built b = build(std::string(kPrintf) +
        "struct Base { a <int>, b <int> }\n"
        "struct Derived: <Base> { c <int> }\n"
        "fun main() <noret> { printf(\"%d %d\\n\", sizeof(Base), sizeof(Derived)); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8 12\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnImplementedInterfaceAddsNoFieldsToTheStruct) {
    // `parents` holds base structs and implemented interfaces together (parser.y puts
    // `struct S : <I>` and `struct S : <Base>` in the same vector), and this is the
    // half that has to contribute nothing. A slot reserved for an interface would move
    // every field after it for something with no run-time existence -- so the size
    // here is the plain struct's, and `x` is still at offset 0.
    //
    // The two corpus sites this is for: `struct ChangableSomehow: <UnchanableString>`
    // (readonly.fin:34) and `struct HashMap<T, U> : <Index, IndexAssign>`
    // (stdlib/hashmap.fin:15). Neither has a base struct at all.
    const Built b = build(std::string(kPrintf) +
        "interface I { pub fun f(self: &Self) <int>; }\n"
        "struct S: <I> {\n"
        "    x <int>\n"
        "    fun f() <int> { return self.x; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{ x: 7 };\n"
        "    printf(\"%d %d\\n\", sizeof(S), s.x);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4 7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ABaseClassSplicesLikeABaseStruct) {
    const Built b = build(std::string(kPrintf) +
        "class Base { a <int> }\n"
        "struct Derived: <Base> { c <int> }\n"
        "fun main() <noret> { let d <Derived> = Derived{ a: 1, c: 2 }; printf(\"%d %d\\n\", d.a, d.c); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "1 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AClassLowersLikeAStruct) {
    const Built b = build(std::string(kPrintf) +
        "class C { a <int> }\n"
        "fun main() <noret> { let c <C> = C { a: 1 }; printf(\"%d\\n\", c.a); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "1\n") << b.why();
}

// ---------------------------------------------------------------------------
// A method a struct inherits is callable through it.
//
// The other half of the splice above. The base's fields are already at the offsets the
// base's own body indexes them at, so the derived pointer *is* a valid pointer to the
// base and the base's function is called with it unchanged -- no thunk, no second body,
// no upcast instruction. What is refused is the case where that is not true: a second
// base's fields begin after the first's, so its methods would read the first base's
// fields, which is a wrong value rather than a missing feature.
//
// Every test prints a value, for the reason the splice tests do: calling the wrong
// function or reading the wrong offset both compile and both run.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AnInheritedMethodIsCalledThroughTheDerivedStruct) {
    // deeptest2.fin:67 in miniature: `Student : <Person>` calling what `Person`
    // declares. The method reads `self.a`, so a receiver that was not the derived
    // object -- or was it at the wrong offset -- prints something other than 3.
    const Built b = build(std::string(kPrintf) +
        "struct Base {\n"
        "    a <int>\n"
        "    fun get_a() <int> { return self.a; }\n"
        "}\n"
        "struct Derived: <Base> { b <int> }\n"
        "fun main() <noret> {\n"
        "    let d <Derived> = Derived{ a: 3, b: 4 };\n"
        "    printf(\"%d %d\\n\", d.get_a(), d.b);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3 4\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInheritedMethodWritesThroughTheDerivedObject) {
    // The write half, which the read half cannot catch: a receiver copied to a
    // temporary would let `set_a` run, return, and change nothing -- and the call would
    // still compile. Asserted by reading the field back through the derived struct.
    const Built b = build(std::string(kPrintf) +
        "struct Base {\n"
        "    a <int>\n"
        "    fun set_a(n: int) <noret> { self.a = n; }\n"
        "}\n"
        "struct Derived: <Base> { b <int> }\n"
        "fun main() <noret> {\n"
        "    let d <Derived> = Derived{ a: 1, b: 2 };\n"
        "    d.set_a(9);\n"
        "    printf(\"%d %d\\n\", d.a, d.b);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnOverrideWinsOverTheMethodItOverrides) {
    // deeptest2.fin:78 writes `Student.to_string` over `Person.to_string` and calls the
    // override "we can also override parents methods". The lookup is breadth-first for
    // this: the derived struct's own method is found a level before the base's, so
    // `who()` is 2 and not 1. A depth-first walk would print 1 and still run.
    const Built b = build(std::string(kPrintf) +
        "struct A {\n"
        "    a <int>\n"
        "    fun who() <int> { return 1; }\n"
        "}\n"
        "struct B: <A> {\n"
        "    b <int>\n"
        "    fun who() <int> { return 2; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let x <B> = B{ a: 5, b: 6 };\n"
        "    printf(\"%d\\n\", x.who());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodIsInheritedThroughTwoLevels) {
    // `C : <B>` and `B : <A>`, with the method on `A` and no redeclaration between.
    // The walk is transitive because the layout is: A's fields are at offset 0 of B,
    // which are at offset 0 of C. `deep()` multiplies the field it reads, so a read
    // from the wrong slot cannot come out as 50 by accident.
    const Built b = build(std::string(kPrintf) +
        "struct A {\n"
        "    a <int>\n"
        "    fun deep() <int> { return self.a * 10; }\n"
        "}\n"
        "struct B: <A> { b <int> }\n"
        "struct C: <B> { c <int> }\n"
        "fun main() <noret> {\n"
        "    let x <C> = C{ a: 5, b: 6, c: 7 };\n"
        "    printf(\"%d %d\\n\", x.deep(), x.c);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "50 7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInheritedStaticMethodIsCalledThroughTheDerivedType) {
    // `Derived::tag()` where `tag` is the base's static. There is no receiver, so there
    // is no layout question to ask -- which is why this is inherited unconditionally
    // where an instance method is not.
    const Built b = build(std::string(kPrintf) +
        "struct Base {\n"
        "    a <int>\n"
        "    static fun tag() <int> { return 42; }\n"
        "}\n"
        "struct Derived: <Base> { b <int> }\n"
        "fun main() <noret> { printf(\"%d\\n\", Derived::tag()); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInheritedOperatorIsAppliedThroughTheDerivedStruct) {
    // An operator is a method with a spelled name, so it is inherited by the same walk
    // over the same table. Before this it refused as "an undeclared operator '+' on
    // struct 'W'", which was true of the derived struct and not of the program.
    const Built b = build(std::string(kPrintf) +
        "struct V {\n"
        "    x <int>\n"
        "    operator +(o: int) <int> { return self.x + o; }\n"
        "}\n"
        "struct W: <V> { y <int> }\n"
        "fun main() <noret> {\n"
        "    let a <W> = W{ x: 1, y: 9 };\n"
        "    printf(\"%d\\n\", a + 41);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodOfASecondBaseIsRefusedRatherThanMisread) {
    // The refusal this unit is bounded by. `Q`'s fields start after `P`'s in `Both`, and
    // `Q.get_q` GEPs at the index `q` has in `Q` -- which in a `Both` is `p`. Calling it
    // would print 1 for a field holding 2, in a program that compiles and runs, so the
    // offsets are compared and the call is refused when they disagree. What a two-base
    // object should look like is deeptest2.fin:83's open question.
    const Built b = build(std::string(kPrintf) +
        "struct P { p <int> }\n"
        "struct Q {\n"
        "    q <int>\n"
        "    fun get_q() <int> { return self.q; }\n"
        "}\n"
        "struct Both: <P, Q> { z <int> }\n"
        "fun main() <noret> {\n"
        "    let x <Both> = Both{ p: 1, q: 2, z: 3 };\n"
        "    printf(\"%d\\n\", x.get_q());\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("inherited from 'Q'"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodInheritedFromTwoBasesIsRefusedRatherThanChosen) {
    // Which `f()` `x.f()` means is a language question, and answering it by which base
    // was written first would answer it silently. The same refusal declareStructs makes
    // for a second inherited *field* of one name, one level along.
    const Built b = build(std::string(kPrintf) +
        "struct P {\n"
        "    p <int>\n"
        "    fun f() <int> { return 1; }\n"
        "}\n"
        "struct Q {\n"
        "    q <int>\n"
        "    fun f() <int> { return 2; }\n"
        "}\n"
        "struct Both: <P, Q> { z <int> }\n"
        "fun main() <noret> {\n"
        "    let x <Both> = Both{ p: 1, q: 2, z: 3 };\n"
        "    printf(\"%d\\n\", x.f());\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("inherits one from 'P' and one from 'Q'"),
              std::string::npos) << b.why();
}

// ---------------------------------------------------------------------------
// An `implements` block writes members of a struct declared somewhere else.
// ---------------------------------------------------------------------------
// `MyStruct implements <GetVal<int>> { pub fun get_val() <int> {...} }`
// (implements_block.fin:13) is the same method the struct could have written in its
// own body, and this backend treats it as exactly that: one `Struct.method` symbol,
// the same receiver pointer, the same weak linkage, the same deferred body. So the
// tests below assert a *value* rather than a compile -- a block whose members were
// collected under the wrong name, or declared twice, or bound to a copy of the
// receiver, all compile and all run.
//
// What the interface named in the header contributes is nothing: it adds no fields
// (AnImplementedInterfaceAddsNoFieldsToTheStruct, above) and no check here -- what a
// struct owes an interface is the analyzer's question. A block this file could not
// consume is refused by name, and the two tests at the end fix which those are.

BACKEND_TEST(Soundness_Codegen, AMethodFromAnImplementsBlockIsCallable) {
    const Built b = build(std::string(kPrintf) +
        "interface GetVal<T> {\n"
        "    pub fun get_val() <T>;\n"
        "}\n"
        "struct MyStruct {\n"
        "    val <int>\n"
        "}\n"
        "MyStruct implements <GetVal<int>> {\n"
        "    pub fun get_val() <int> {\n"
        "        return self.val;\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <MyStruct> = MyStruct { val: 42 };\n"
        "    printf(\"%d\\n\", s.get_val());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodFromAnImplementsBlockWritesThroughTheReceiver) {
    // The receiver is the object's address and not a copy of it, which is the half of
    // the convention a read-only method cannot witness: a `self` spilled to a
    // temporary compiles, runs, and prints the old value.
    const Built b = build(std::string(kPrintf) +
        "interface Settable {\n"
        "    pub fun set(n: int) <noret>;\n"
        "}\n"
        "struct Cell { v <int> }\n"
        "Cell implements <Settable> {\n"
        "    pub fun set(n: int) <noret> { self.v = n; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let c <Cell> = Cell { v: 1 };\n"
        "    c.set(9);\n"
        "    printf(\"%d\\n\", c.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnOperatorFromAnImplementsBlockIsApplied) {
    // implements_block.fin:28 verbatim in shape, and the value is what makes it a
    // test: an operator bound to the wrong struct's symbol would still compile.
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    y <int>\n"
        "}\n"
        "interface Addable<T> {\n"
        "    pub operator + (other: <T>) <T>;\n"
        "}\n"
        "Point implements <Addable<Point>> {\n"
        "    pub operator + (other: <Point>) <Point> {\n"
        "        return Point { x: self.x + other.x, y: self.y + other.y };\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p1 <Point> = Point { x: 1, y: 2 };\n"
        "    let p2 <Point> = Point { x: 3, y: 4 };\n"
        "    let p3 <Point> = p1 + p2;\n"
        "    printf(\"%d %d\\n\", p3.x, p3.y);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4 6\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStaticMethodFromAnImplementsBlockIsCallable) {
    // `Struct::name()` -- no receiver, so nothing about the pointer to assert. What
    // this fixes is that a block's static goes into the same `Struct.name` table the
    // `::` path already reads, rather than needing a second lookup.
    const Built b = build(std::string(kPrintf) +
        "interface Tagged {\n"
        "    pub fun tag() <int>;\n"
        "}\n"
        "struct S { a <int> }\n"
        "S implements <Tagged> {\n"
        "    pub fun tag() <int> { return self.a; }\n"
        "    pub static fun made() <int> { return 7; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", S::made());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AConstructorFromAnImplementsBlockRuns) {
    // `Collection<T> implements <NoLengthCollection> { Collection() {...} }`
    // (stdlib/collection.fin:103) is this shape. The value is what says the
    // constructor ran at all: the caller zeroes the storage first, so a constructor
    // that was declared and never called prints 0 and still compiles.
    const Built b = build(std::string(kPrintf) +
        "interface Makeable {\n"
        "    pub fun get() <int>;\n"
        "}\n"
        "struct S { a <int> }\n"
        "S implements <Makeable> {\n"
        "    S() { self.a = 8; }\n"
        "    pub fun get() <int> { return self.a; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S> = S();\n"
        "    printf(\"%d\\n\", s.get());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodFromAnImplementsBlockSatisfiesTheInterfacesVtable) {
    // The block's method reached through the interface rather than through the
    // struct: `hear(d)` converts a `Dog` to a `{data, vtable}` pair (ADR 0019) and
    // calls slot 0. Before the block's methods were declared with the struct's, that
    // slot held a null pointer -- a call through one is a jump to address zero, which
    // is a crash and not a diagnostic.
    const Built b = build(std::string(kPrintf) +
        "interface Speaker {\n"
        "    pub fun speak() <int>;\n"
        "}\n"
        "struct Dog { n <int> }\n"
        "Dog implements <Speaker> {\n"
        "    pub fun speak() <int> { return self.n * 2; }\n"
        "}\n"
        "fun hear(s: Speaker) <int> { return s.speak(); }\n"
        "fun main() <noret> {\n"
        "    let d <Dog> = Dog { n: 21 };\n"
        "    printf(\"%d\\n\", hear(d));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnImplementsBlockOnATemplateIsDeclaredPerInstantiation) {
    // `Result<T, U> implements <IResult>` (stdlib/typing.fin:27) is written on the
    // template, and a method of a template has no signature until something says what
    // T is. Two instantiations, two bodies, two representations -- one `int` and one
    // `char` -- because a block's method is monomorphised on the same terms as one
    // written in the body (ADR 0002).
    const Built b = build(std::string(kPrintf) +
        "interface Tagged {\n"
        "    pub fun tag() <int>;\n"
        "}\n"
        "struct S<T> { a <T> }\n"
        "S<T> implements <Tagged> {\n"
        "    pub fun tag() <int> { return 1; }\n"
        "    pub fun get() <T> { return self.a; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <S<int>> = S::<int>{ a: 6 };\n"
        "    let b <S<char>> = S::<char>{ a: 65 };\n"
        "    printf(\"%d %d %d\\n\", a.get(), cast<int>(b.get()), a.tag());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "6 65 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnOverwriterImplementsBlockAddsItsMethodsToo) {
    // `@implements Collection<T> { ... }` (stdlib/collection.fin:93) -- the form that
    // names no interface, whose own comment reads "overwrites or adds
    // methods/operators". Adding is what this backend does with it, on the same terms
    // as the interface-named form: a method of the target, in the target's table.
    const Built b = build(std::string(kPrintf) +
        "struct S<T> { a <T> }\n"
        "@implements S<T> {\n"
        "    pub fun get() <T> { return self.a; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S<int>> = S::<int>{ a: 6 };\n"
        "    printf(\"%d\\n\", s.get());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "6\n") << b.why();
}

// Wave-4 @implements query lowering, stage B: concrete named pairs lower to
// ConstantInt i1 through the normative predicate (`StructType::implements`,
// owner ruling 4) -- presence alone is not conformance. A field of the wrong
// type, a missing operator, or a missing destructor answers false, never a
// guessed true.
BACKEND_TEST(Soundness_Codegen, ImplementsQueryRefusesAFieldOfTheWrongType) {
    const Built b = build(std::string(kPrintf) +
        "interface HasX {\n"
        "    pub x <int>,\n"
        "    pub fun m() <int>;\n"
        "}\n"
        "struct WrongType {\n"
        "    x <string>,\n"
        "    pub fun m() <int> { return 1; }\n"
        "}\n"
        "struct Right {\n"
        "    x <int>,\n"
        "    pub fun m() <int> { return 1; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let r1 <bool> = @implements(WrongType, HasX);\n"
        "    if (r1 == true) {\n"
        "        printf(\"wrong-true\\n\");\n"
        "    } else {\n"
        "        printf(\"wrong-false\\n\");\n"
        "    }\n"
        "    let r2 <bool> = @implements(Right, HasX);\n"
        "    if (r2 == true) {\n"
        "        printf(\"right-true\\n\");\n"
        "    } else {\n"
        "        printf(\"right-false\\n\");\n"
        "    }\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "wrong-false\nright-true\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ImplementsQueryRefusesAMissingOperator) {
    const Built b = build(std::string(kPrintf) +
        "interface HasAdd {\n"
        "    pub operator + (other: <int>) <int>;\n"
        "}\n"
        "struct NoAdd { x <int> }\n"
        "struct HasIt {\n"
        "    x <int>,\n"
        "    pub operator + (other: <int>) <int> { return self.x + other; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let r1 <bool> = @implements(NoAdd, HasAdd);\n"
        "    if (r1 == true) {\n"
        "        printf(\"noadd-true\\n\");\n"
        "    } else {\n"
        "        printf(\"noadd-false\\n\");\n"
        "    }\n"
        "    let r2 <bool> = @implements(HasIt, HasAdd);\n"
        "    if (r2 == true) {\n"
        "        printf(\"hasit-true\\n\");\n"
        "    } else {\n"
        "        printf(\"hasit-false\\n\");\n"
        "    }\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "noadd-false\nhasit-true\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ImplementsQueryRefusesAMissingDestructor) {
    const Built b = build(std::string(kPrintf) +
        "interface HasDtor {\n"
        "    ~Self();\n"
        "}\n"
        "struct NoDtor { x <int> }\n"
        "struct YesDtor {\n"
        "    x <int>,\n"
        "    ~YesDtor() { }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let r1 <bool> = @implements(NoDtor, HasDtor);\n"
        "    if (r1 == true) {\n"
        "        printf(\"nodtor-true\\n\");\n"
        "    } else {\n"
        "        printf(\"nodtor-false\\n\");\n"
        "    }\n"
        "    let r2 <bool> = @implements(YesDtor, HasDtor);\n"
        "    if (r2 == true) {\n"
        "        printf(\"yesdtor-true\\n\");\n"
        "    } else {\n"
        "        printf(\"yesdtor-false\\n\");\n"
        "    }\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "nodtor-false\nyesdtor-true\n") << b.why();
}

// Wave-4 @implements query lowering, stage C: the `$struct`/`$interface`
// param shape (literal_interface.fin:6) plus `interface {...}` literals flow
// through the tid-pair chain -- anonymous interfaces carry full tids and
// InterfaceInfo (owner ruling 2), never a silent false. Foldable named pairs
// still take the Stage-B static fold above and never reach the chain.
BACKEND_TEST(Soundness_Codegen, ImplementsQueryOverLiteralsAnswersTrue) {
    // `compatible()` verbatim from literal_interface.fin:5-11, called with
    // literals whose shapes agree.
    const Built b = build(std::string(kPrintf) +
        "fun compatible(iface: $interface, struct_: $struct) <bool> {\n"
        "    if (@implements(struct_, iface) == true) {\n"
        "        return true;\n"
        "      } else {\n"
        "          return false;\n"
        "        }\n"
        "  }\n"
        "fun main() <noret> {\n"
        "    let s <$struct> = struct {\n"
        "        pub x <int>,\n"
        "        pub fun m() <int> { return 1; }\n"
        "      };\n"
        "    let i <$interface> = interface {\n"
        "        pub x <int>,\n"
        "        pub fun m() <int>;\n"
        "      };\n"
        "    let r <bool> = compatible(i, s);\n"
        "    if (r == true) {\n"
        "        printf(\"compat-true\\n\");\n"
        "    } else {\n"
        "        printf(\"compat-false\\n\");\n"
        "    }\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "compat-true\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ImplementsQueryOverLiteralsAnswersFalse) {
    // The same chain saying no: the struct lacks the interface's method, so
    // no tid pair matches and the answer is false -- the honest negative,
    // not an over-matching chain.
    const Built b = build(std::string(kPrintf) +
        "fun compatible(iface: $interface, struct_: $struct) <bool> {\n"
        "    if (@implements(struct_, iface) == true) {\n"
        "        return true;\n"
        "      } else {\n"
        "          return false;\n"
        "        }\n"
        "  }\n"
        "fun main() <noret> {\n"
        "    let s <$struct> = struct {\n"
        "        pub x <int>,\n"
        "      };\n"
        "    let i <$interface> = interface {\n"
        "        pub x <int>,\n"
        "        pub fun m() <int>;\n"
        "      };\n"
        "    let r <bool> = compatible(i, s);\n"
        "    if (r == true) {\n"
        "        printf(\"compat-true\\n\");\n"
        "    } else {\n"
        "        printf(\"compat-false\\n\");\n"
        "    }\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "compat-false\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, PickInterfaceTidFlowsIntoCompatible) {
    // literal_interface.fin:18-27 verbatim: `pick_interface` returns an
    // anonymous interface literal selected by a runtime enum compare, and
    // the returned tid flows into `compatible`. First matches, Second does
    // not -- the tid dataflow made observable.
    const Built b = build(std::string(kPrintf) +
        "fun compatible(iface: $interface, struct_: $struct) <bool> {\n"
        "    if (@implements(struct_, iface) == true) {\n"
        "        return true;\n"
        "      } else {\n"
        "          return false;\n"
        "        }\n"
        "  }\n"
        "enum IFaceOptions {\n"
        "    First = 1,\n"
        "    Second = 2,\n"
        "  }\n"
        "fun pick_interface(option: IFaceOptions) <$interface> {\n"
        "    if (option == IFaceOptions::First) {\n"
        "        return interface {\n"
        "            pub picked_first <bool> = true;\n"
        "            pub fun say_hello() <noret>;\n"
        "          };\n"
        "      } else {\n"
        "          return interface { pub picked_second <bool> = true; };\n"
        "        }\n"
        "  }\n"
        "fun main() <noret> {\n"
        "    let has_first <$struct> = struct {\n"
        "        pub picked_first <bool> = true,\n"
        "        pub fun say_hello() <noret> {\n"
        "          }\n"
        "      };\n"
        "    let i1 <$interface> = pick_interface(IFaceOptions::First);\n"
        "    let r1 <bool> = compatible(i1, has_first);\n"
        "    if (r1 == true) {\n"
        "        printf(\"first-true\\n\");\n"
        "    } else {\n"
        "        printf(\"first-false\\n\");\n"
        "    }\n"
        "    let i2 <$interface> = pick_interface(IFaceOptions::Second);\n"
        "    let r2 <bool> = compatible(i2, has_first);\n"
        "    if (r2 == true) {\n"
        "        printf(\"second-true\\n\");\n"
        "    } else {\n"
        "        printf(\"second-false\\n\");\n"
        "    }\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "first-true\nsecond-false\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodInABlockOfANameTheStructDeclaresIsRefused) {
    // Two definitions of one `S.f` symbol, and `declareFunction` keeps the first --
    // so the block's body would silently not be the one that runs. The same refusal a
    // second method written inside the body gets, and it has to span the two places a
    // method may be written or the check is only half a check.
    const Built b = build(std::string(kPrintf) +
        "interface I { pub fun f() <int>; }\n"
        "struct S {\n"
        "    a <int>,\n"
        "    fun f() <int> { return 1; }\n"
        "}\n"
        "S implements <I> {\n"
        "    pub fun f() <int> { return 2; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S> = S { a: 0 };\n"
        "    printf(\"%d\\n\", s.f());\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a second method 'f' on struct 'S'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AConstructorInABlockBesideTheStructsOwnIsRefused) {
    // One constructor symbol per struct, matching the analyzer's `constructors[0]`
    // rule -- so two of them are two definitions of `S.constructor` however they are
    // spread over the file, and which one `S()` meant is overload resolution nobody
    // has written.
    const Built b = build(std::string(kPrintf) +
        "interface I { pub fun f() <int>; }\n"
        "struct S {\n"
        "    a <int>,\n"
        "    S() { self.a = 1; }\n"
        "}\n"
        "S implements <I> {\n"
        "    S() { self.a = 2; }\n"
        "    pub fun f() <int> { return self.a; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S> = S();\n"
        "    printf(\"%d\\n\", s.f());\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("constructor overloads on struct 'S'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnImplementsBlockOnAnEnumIsRefused) {
    // `Result<T, U> implements <IResult>` on an *enum* (stdlib/typing.fin:27). An enum
    // lowers to an integer here: it has no StructInfo to hang a method on, no address
    // to be a receiver, and the analyzer's rule for one -- the first parameter is the
    // receiver when its type is the enum -- is a second calling convention. Refused
    // by name rather than half consumed.
    const Built b = build(std::string(kPrintf) +
        "enum E { A, B }\n"
        "interface I { pub fun f() <int>; }\n"
        "E implements <I> {\n"
        "    pub fun f(e: E) <int> { return 1; }\n"
        "}\n"
        "fun main() <noret> { printf(\"%d\\n\", 1); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("an implements block on 'E'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASingleMemberOverwriteIsRefused) {
    // `@implements Result<T, E>::unwrap = fun(...) {...}` (enums.fin:25) supplies a
    // *value* for one named member rather than a declaration, and a value has no
    // signature to declare a function from. Refused whole: consuming half of it would
    // mean a struct whose method table depends on which form the writer used.
    const Built b = build(std::string(kPrintf) +
        "struct S<T> { a <T> }\n"
        "@implements S<T>::g = fun(s: S<T>) <int> { return 1; }\n"
        "fun main() <noret> {\n"
        "    let s <S<int>> = S::<int>{ a: 1 };\n"
        "    printf(\"%d\\n\", s.a);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("overwriting the member 'g'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnUndeclaredOperatorOnAStructIsRefused) {
    // Was AnOperatorOnAStructComparesMemberwise, which asserted memberwise `p == q`
    // synthesis. Overturned by owner decision under ADR 0036 (equality is declared,
    // not synthesized): `==` with no declared `operator ==` is refused in codegen.
    // Inverted rather than deleted, because the refusal is the contract now.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int> }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 1 };\n"
        "    let q <P> = P { a: 1 };\n"
        "    if (p == q) { printf(\"same\\n\"); }\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("an undeclared operator '==' on struct 'P'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnUndeclaredOperatorOnAGenericInstantiationIsRefused) {
    // The generic-struct-typed shape of the same contract: `Box<int>` is a
    // struct like `P` above, so `==` with no declared `operator ==` refuses
    // naming the instance rather than the template (ADR 0036).
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> { val <T> }\n"
        "fun main() <noret> {\n"
        "    let a <Box<int>> = Box::<int>{ val: 1 };\n"
        "    let b <Box<int>> = Box::<int>{ val: 1 };\n"
        "    if (a == b) { printf(\"same\\n\"); }\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("an undeclared operator '==' on struct 'Box<int>'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnUndeclaredOperatorOnACollectionOfDataIsRefused) {
    // The `Collection<Data>` shape: two parameters bound, one of them a
    // struct. A field-wise synthesis would have to decide what equality over
    // `Data` means, which is exactly what ADR 0036 withholds, so this
    // refuses naming the instantiation.
    const Built b = build(std::string(kPrintf) +
        "struct Data { x <int> }\n"
        "struct Collection<T> { v <T> }\n"
        "fun main() <noret> {\n"
        "    let a <Collection<Data>> = Collection::<Data>{ v: Data{ x: 1 } };\n"
        "    let b <Collection<Data>> = Collection::<Data>{ v: Data{ x: 1 } };\n"
        "    if (a == b) { printf(\"same\\n\"); }\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("an undeclared operator '==' on struct 'Collection<Data>'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnUndeclaredOperatorThroughAnAliasIsRefused) {
    // The alias shape: `Alias` names `Box<int>`, so the refusal names the
    // instance the alias denotes rather than the spelling the program wrote.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> { val <T> }\n"
        "type Alias = Box<int>;\n"
        "fun main() <noret> {\n"
        "    let a <Alias> = Box::<int>{ val: 1 };\n"
        "    let b <Alias> = Box::<int>{ val: 1 };\n"
        "    if (a == b) { printf(\"same\\n\"); }\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("an undeclared operator '==' on struct 'Box<int>'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ABareGenericParamComparisonInstantiatedAtAStructIsRefused) {
    // The bare-generic-param shape: `a == b` with `a, b: T` lowers once per
    // binding, so at `int` it is an integer comparison and at `Data` it is
    // the struct contract above. The struct binding refuses rather than
    // reusing the scalar lowering.
    const Built b = build(std::string(kPrintf) +
        "struct Data { x <int> }\n"
        "fun eq<T>(a: T, b: T) <bool> { return a == b; }\n"
        "fun main() <noret> {\n"
        "    let a <Data> = Data{ x: 1 };\n"
        "    let b <Data> = Data{ x: 2 };\n"
        "    let r <bool> = eq(a, b);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("an undeclared operator '==' on struct 'Data'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnUndeclaredInequalityOnAGenericInstantiationIsRefused) {
    // The `!=` half of the generic shape: a distinct token with the same
    // contract, so it names `!=` rather than sharing `==`'s refusal.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> { val <T> }\n"
        "fun main() <noret> {\n"
        "    let a <Box<int>> = Box::<int>{ val: 1 };\n"
        "    let b <Box<int>> = Box::<int>{ val: 1 };\n"
        "    if (a != b) { printf(\"different\\n\"); }\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("an undeclared operator '!=' on struct 'Box<int>'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructAsAConditionIsRefused) {
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int> }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 1 };\n"
        "    if (p) { printf(\"truthy\\n\"); }\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

// Was AGenericStructIsRefused, which asserted that `struct Box<T> { v <T> }` fails
// the build on its own. It no longer does -- a template nobody instantiates lowers
// to nothing, which is Soundness_Codegen.AGenericStructNobodyInstantiatesLowers-
// ToNothing. Inverted rather than deleted, because the *declaration* is still where
// some refusals belong: the ones that no type argument could fix. A `class` is a
// value or a reference and Fin has not said which (lowerableStruct), and no `Box<T>`
// at any T changes that, so it is refused where it is written rather than at each
// use.
BACKEND_TEST(Soundness_Codegen, AGenericClassLowersAtItsUse) {
    const Built b = build(std::string(kPrintf) +
        "class Box<T> { v <T> }\n"
        "fun main() <noret> { let b <Box<int>> = Box::<int> { v: 7 }; printf(\"%d\\n\", b.v); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

// These two cannot use BACKEND_TEST: their bodies name llvm::DataLayout and
// llvm::Type directly, and an OFF build has no LLVM headers to compile them
// against. So the guard is a real #ifdef, with stubs below so the count does not
// move -- the same reason BACKEND_TEST skips rather than disappearing.
#ifdef FIN_TESTS_HAVE_BACKEND

TEST(Soundness_Codegen, TheLayoutTableAgreesWithLLVM) {
    // The one test in this repository that asks a third party whether finc is
    // right, and the reason it exists is the failure mode it guards.
    //
    // Two components compute the size of an `int`: src/types/Layout.hpp, which the
    // layout pass and every `compiler.layout.*` query read, and LLVM, which lays
    // out the struct types the backend builds. If those two disagree by one byte,
    // nothing fails to compile: the front end reports one offset for a field, the
    // emitted code reads another, and the program runs and prints garbage. No
    // suite that only asserts what finc *says*, and none that only asserts what
    // the produced program *does* for the cases someone thought to write, can find
    // that. So the table is checked against LLVM's own DataLayout for the real
    // target, name by name.
    //
    // The widths are shared rather than duplicated now (CodeGen_LLVM.cpp's byName
    // reads scalarByName), which makes a disagreement much harder to introduce --
    // but "harder" is not "impossible": a wrong *alignment* in the table would
    // survive sharing untouched, since the table declares alignment and LLVM
    // derives it from the target. That is exactly the case this catches. `long` is
    // 8-aligned on x86-64 and 4-aligned on i686, and the table's maxScalarAlign
    // knob is the only thing that knows.
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    // A parsed `llvm::Triple`, for the reason generateObject uses one: from LLVM 21
    // these three entry points take the triple and not the string it came from. The
    // string is kept for the failure message, which has to name the target a reader
    // would recognise.
    const llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
    const std::string tripleName = triple.str();
    std::string lookupError;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, lookupError);
    ASSERT_NE(target, nullptr) << lookupError;
    llvm::TargetOptions options;
    std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
        triple, "generic", "", options, llvm::Reloc::PIC_));
    ASSERT_NE(machine, nullptr) << "no TargetMachine for " << tripleName;
    const llvm::DataLayout dataLayout = machine->createDataLayout();

    llvm::LLVMContext ctx;
    const fin::TargetLayout finTarget;

    // Every name the table answers for. Listed literally rather than iterated,
    // because a table that could enumerate itself could also forget an entry and
    // still pass.
    const char* const names[] = {
        "bool",  "char",  "byte",   "short", "ushort", "int",   "uint",
        "long",  "ulong", "int8",   "uint8", "int16",  "uint16", "int32",
        "uint32", "int64", "uint64", "float", "double", "string",
        // GV1 alias completeness: the stage checker knows these (checker.fin),
        // so the one table must too -- this test then holds each to LLVM.
        "u8",   "i8",   "u16",  "i16",  "u32",   "i32",   "u64",  "i64",
        "f32",  "f64",  "usize", "isize", "size_t",
    };

    for (const char* name : names) {
        auto info = fin::scalarByName(name);
        ASSERT_TRUE(info.has_value()) << name << " is missing from the table";
        ASSERT_NE(info->kind, fin::ScalarKind::Void) << name;

        llvm::Type* llvmType = nullptr;
        switch (info->kind) {
            case fin::ScalarKind::Bool:
            case fin::ScalarKind::Int:
                llvmType = llvm::Type::getIntNTy(ctx, info->bits);
                break;
            case fin::ScalarKind::Float:
                llvmType = info->bits == 32 ? llvm::Type::getFloatTy(ctx)
                                            : llvm::Type::getDoubleTy(ctx);
                break;
            case fin::ScalarKind::Pointer:
                llvmType = llvm::PointerType::getUnqual(ctx);
                break;
            case fin::ScalarKind::Void:
                break;
        }
        ASSERT_NE(llvmType, nullptr) << name;

        EXPECT_EQ(fin::sizeOfScalar(*info, finTarget),
                  dataLayout.getTypeAllocSize(llvmType).getFixedValue())
            << name << ": the layout pass and LLVM disagree about its size";
        EXPECT_EQ(fin::alignOfScalar(*info, finTarget),
                  dataLayout.getABITypeAlign(llvmType).value())
            << name << ": the layout pass and LLVM disagree about its alignment";
    }
}

TEST(Soundness_Codegen, AStructsLayoutMatchesWhatLLVMWouldChoose) {
    // The same check one level up: given the field types in declaration order,
    // finc's own offsets and size must be what LLVM computes for the equivalent
    // literal struct. This is the check that says `alignUp` and the padding rule
    // are the C rule and not merely a self-consistent invention -- and it is what
    // the next unit, struct lowering, depends on being true, since that unit will
    // build exactly these LLVM struct types and GEP into them by field index.
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    const llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
    std::string lookupError;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, lookupError);
    ASSERT_NE(target, nullptr) << lookupError;
    llvm::TargetOptions options;
    std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
        triple, "generic", "", options, llvm::Reloc::PIC_));
    ASSERT_NE(machine, nullptr);
    const llvm::DataLayout dataLayout = machine->createDataLayout();
    llvm::LLVMContext ctx;
    const fin::TargetLayout finTarget;
    fin::LayoutEngine engine;

    // Orders chosen so that padding lands in a different place in each: leading,
    // trailing, interior, and none.
    const std::vector<std::vector<const char*>> shapes = {
        {"char", "long"},
        {"long", "char"},
        {"char", "int", "char"},
        {"int", "int"},
        {"char", "char", "char"},
        {"double", "char", "short"},
        {"string", "char"},
        {"bool", "bool", "long"},
    };

    for (const auto& shape : shapes) {
        auto s = std::make_shared<fin::StructType>("S");
        std::vector<llvm::Type*> members;
        std::string label;
        for (size_t i = 0; i < shape.size(); ++i) {
            s->defineField("f" + std::to_string(i),
                           std::make_shared<fin::PrimitiveType>(shape[i]), true);
            auto info = fin::scalarByName(shape[i]);
            ASSERT_TRUE(info.has_value());
            switch (info->kind) {
                case fin::ScalarKind::Bool:
                case fin::ScalarKind::Int:
                    members.push_back(llvm::Type::getIntNTy(ctx, info->bits));
                    break;
                case fin::ScalarKind::Float:
                    members.push_back(info->bits == 32 ? llvm::Type::getFloatTy(ctx)
                                                       : llvm::Type::getDoubleTy(ctx));
                    break;
                case fin::ScalarKind::Pointer:
                    members.push_back(llvm::PointerType::getUnqual(ctx));
                    break;
                case fin::ScalarKind::Void:
                    break;
            }
            label += std::string(i ? ", " : "") + shape[i];
        }
        auto result = engine.layoutOf(s);
        ASSERT_TRUE(result.ok()) << label << ": " << result.refusal;

        // isPacked false: finc inserts padding, so the comparison must be against
        // the padded LLVM struct. Comparing against a packed one would "pass" by
        // agreeing that there is no padding anywhere.
        llvm::StructType* llvmStruct = llvm::StructType::get(ctx, members, false);
        const llvm::StructLayout* llvmLayout = dataLayout.getStructLayout(llvmStruct);

        EXPECT_EQ(result.layout.size, llvmLayout->getSizeInBytes()) << "{" << label << "}";
        EXPECT_EQ(result.layout.align, llvmLayout->getAlignment().value()) << "{" << label << "}";
        ASSERT_EQ(result.layout.fields.size(), shape.size()) << "{" << label << "}";
        for (size_t i = 0; i < shape.size(); ++i) {
            EXPECT_EQ(result.layout.fields[i].offset, llvmLayout->getElementOffset(i))
                << "{" << label << "} field " << i;
        }
    }
}

#else

TEST(Soundness_Codegen, TheLayoutTableAgreesWithLLVM) {
    GTEST_SKIP() << "built with FIN_WITH_LLVM=OFF";
}
TEST(Soundness_Codegen, AStructsLayoutMatchesWhatLLVMWouldChoose) {
    GTEST_SKIP() << "built with FIN_WITH_LLVM=OFF";
}

#endif  // FIN_TESTS_HAVE_BACKEND

// ---------------------------------------------------------------------------
// `++` and `--`.
//
// This operator was refused outright, and the reason was not the lowering: the AST
// built the same node for `i++` and `++i`, so there was nothing to read to decide
// which value the expression has. Soundness_OperatorPosition is the other half of
// this unit -- `is_postfix` is a fact about the source now -- and these are the
// tests that the fact is *used*, which is only visible when the value is read.
//
// Every increment in the corpus is a statement or a `for` step, where the two are
// the same instruction sequence. So a wrong guess would have compiled all eight of
// them correctly and been wrong on the first program that wrote `let n = i++;`.

BACKEND_TEST(Soundness_Codegen, APostfixIncrementYieldsTheOldValue) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let i <int> = 5;\n"
        "    let j <int> = i++;\n"
        "    printf(\"%d %d\\n\", i, j);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "6 5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrefixIncrementYieldsTheNewValue) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let i <int> = 5;\n"
        "    let j <int> = ++i;\n"
        "    printf(\"%d %d\\n\", i, j);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "6 6\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APostfixDecrementYieldsTheOldValue) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let i <int> = 5;\n"
        "    let j <int> = i--;\n"
        "    printf(\"%d %d\\n\", i, j);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "4 5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrefixDecrementYieldsTheNewValue) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let i <int> = 5;\n"
        "    let j <int> = --i;\n"
        "    printf(\"%d %d\\n\", i, j);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "4 4\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnIncrementInStatementPositionAdvancesTheVariable) {
    // The spelling the corpus uses eight times out of eight, in both positions,
    // where the two must be indistinguishable.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let i <int> = 0;\n"
        "    i++;\n"
        "    ++i;\n"
        "    i--;\n"
        "    printf(\"%d\\n\", i);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AForLoopStepIncrements) {
    // `for (i : int = 0; i <= 10; i++)` is loops.fin:8 -- the whole reason this
    // operator blocks more of the corpus than its size suggests.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    for (i : int = 0; i < 4; i++) {\n"
        "        printf(\"%d\", i);\n"
        "    }\n"
        "    printf(\"\\n\");\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "0123\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnIncrementOnAStructFieldWritesBack) {
    // `self.length++` (stdlib/collection.fin:22) is the other shape the corpus
    // writes, and it goes through the same address path a field assignment does --
    // so the read and the write are one GEP rather than two.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 1, b: 2 };\n"
        "    let old <int> = p.a++;\n"
        "    p.b--;\n"
        "    printf(\"%d %d %d\\n\", p.a, p.b, old);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "2 1 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnIncrementOnANestedFieldWritesBack) {
    const Built b = build(std::string(kPrintf) +
        "struct Inner { v <int> }\n"
        "struct Outer { i <Inner>, tail <int> }\n"
        "fun main() <noret> {\n"
        "    let o <Outer> = Outer { i: Inner { v: 7 }, tail: 8 };\n"
        "    o.i.v++;\n"
        "    printf(\"%d %d\\n\", o.i.v, o.tail);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "8 8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnIncrementOnAFloatAddsOne) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let f <float> = 1.5;\n"
        "    f++;\n"
        "    printf(\"%.2f\\n\", cast<double>(f));\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "2.50\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnIncrementReadsAndWritesOneAddress) {
    // `i++` must not evaluate its target twice. There is no expression this backend
    // admits as a target whose evaluation has a side effect, so the way to observe
    // it is arithmetic: a second load between the load and the store would read a
    // value the first increment had already written.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let i <int> = 0;\n"
        "    let sum <int> = i++ + i++ + i++;\n"
        "    printf(\"%d %d\\n\", sum, i);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "3 3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnIncrementOnAPointerIsRefused) {
    // Whether `p++` advances by one element or one byte is an owner ruling, and
    // both lower cleanly -- the one that is wrong is an out-of-bounds read with
    // nothing to report it. The analyzer refuses this first; the backend's guard is
    // here so the answer does not depend on which layer runs.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let i <int> = 1;\n"
        "    let p <&int> = &i;\n"
        "    p++;\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
}

// ---------------------------------------------------------------------------
// Pointers.
//
// tests/samples/deeptest3.fin is the whole specification, and it is unusually
// explicit for this corpus -- it states the rules in prose beside the code:
//
//   `&` takes an address (:23 `swap(&x, &y)`, :52 `&numbers[1]`, :109 `&my_array`,
//   :126 `&p`), `*` reads or writes through one (:7 `let temp <int> = *a`, :10
//   `*a = *b`, :134 `**pp = 500`), and a pointer compares against `null` (:64).
//
//   ":39 Access members via pointer (Fin automatically handles -> logic with .)"
//   -- there is no `->` in Fin. `p.hp` on a `<&Player>` reads the field the
//   pointer points at, and `head.next.value` (:98, with the sample's own note
//   that C would write `head->next->value`) chains it.
//
//   ":111 In Fin, indexing a pointer to an array works just like indexing the
//   array. The compiler knows to dereference the base first" -- `ptr_to_arr[0]`
//   on a `<&[int, 3]>` is element 0 of the array, not of a pointer.
//
//   ":35 Allocate on Heap" / ":44 then frees memory" -- `new` and `delete`. Which
//   is malloc and free: ADR 0003 says a memory *strategy* (ownership,
//   refcounting, a collector) is a library written against the component API, and
//   a library needs a substrate to be written against. This is the substrate, and
//   nothing else in the corpus offers itself as one.
//
// A pointer is one machine word whatever it points at (LLVM has had one opaque
// `ptr` since 15), so the pointee is a fact this file's own type table carries
// rather than something recoverable from the IR. Every test below that reads
// through a pointer is a test that it carried the right one: an `&char` that had
// lost its pointee would load four bytes from a one-byte slot and still compile.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, APointerParameterIsWrittenThroughByTheCallee) {
    // tests/samples/deeptest3.fin:5-26, the sample's first exercise: two `&int`
    // parameters, three dereferences, and a caller that sees both writes.
    const Built b = build(std::string(kPrintf) +
        "fun swap(a: &int, b: &int) <noret> {\n"
        "    let temp <int> = *a;\n"
        "    *a = *b;\n"
        "    *b = temp;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let x <int> = 10;\n"
        "    let y <int> = 20;\n"
        "    swap(&x, &y);\n"
        "    printf(\"%d %d\\n\", x, y);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "20 10\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADereferenceReadsThroughAPointer) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <int> = 7;\n"
        "    let p <&int> = &x;\n"
        "    printf(\"%d\\n\", *p);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAssignmentThroughAPointerReachesTheOriginal) {
    // The half of `*p` that is not a read. If `*p = 5` stored into a copy the
    // program would run and print 1, which is why this is a separate test from the
    // read: one address, two directions.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let p <&int> = &x;\n"
        "    *p = 5;\n"
        "    printf(\"%d\\n\", x);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ACompoundAssignmentThroughAPointerReadsAndWritesOneAddress) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <int> = 10;\n"
        "    let p <&int> = &x;\n"
        "    *p += 5;\n"
        "    printf(\"%d\\n\", x);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "15\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TheAddressOfAnArrayElementPointsAtThatElement) {
    // tests/samples/deeptest3.fin:48-60. `&numbers[1]` is the address of one
    // element and not of the array: a pointer to the array would read 10 here, and
    // the write at the end would land on element 0.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let numbers <[int, 3]> = [10, 20, 30];\n"
        "    let ptr <&int> = &numbers[1];\n"
        "    printf(\"%d\\n\", *ptr);\n"
        "    *ptr = 99;\n"
        "    printf(\"%d %d %d\\n\", numbers[0], numbers[1], numbers[2]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "20\n10 99 30\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APointerToAWholeArrayIsIndexedThroughIt) {
    // tests/samples/deeptest3.fin:105-117 and its note at :111. Both halves: the
    // index goes through the pointer to the element, and an explicit `*` gives the
    // whole array back as a value that can be copied into an `[int, 3]`.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let my_array <[int, 3]> = [10, 20, 30];\n"
        "    let ptr_to_arr <&[int, 3]> = &my_array;\n"
        "    printf(\"%d %d\\n\", ptr_to_arr[0], ptr_to_arr[2]);\n"
        "    let copy_of_arr <[int, 3]> = *ptr_to_arr;\n"
        "    printf(\"%d\\n\", copy_of_arr[1]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "10 30\n20\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWriteThroughAnArrayPointerReachesTheArray) {
    // The copy at the end of the sample's version hides this: if `ptr_to_arr[0]`
    // read through a *copy* of the array, everything above still prints the same
    // numbers and this prints 10.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 3]> = [10, 20, 30];\n"
        "    let p <&[int, 3]> = &a;\n"
        "    p[0] = 77;\n"
        "    printf(\"%d %d\\n\", a[0], p[0]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "77 77\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APointerToAPointerIsDereferencedTwice) {
    // tests/samples/deeptest3.fin:119-137, including the sample's note at :129
    // that `<&(&int)>` is admitted as the same type as `<&&int>` -- so both
    // spellings are declared here and both are dereferenced twice.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <int> = 10;\n"
        "    let p <&int> = &x;\n"
        "    let pp <&&int> = &p;\n"
        "    let pp_3 <&(&int)> = &p;\n"
        "    **pp = 500;\n"
        "    printf(\"%d %d %d\\n\", x, **pp, **pp_3);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "500 500 500\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, NullComparesEqualToNullAndAnAddressDoesNot) {
    // tests/samples/deeptest3.fin:62-76. `null` reaches the callee as an argument,
    // which is the case that has no declared type at the literal to take a
    // representation from -- it is one word of zeroes whatever it was going to
    // point at.
    const Built b = build(std::string(kPrintf) +
        "fun print_if_exists(val_ptr: &int) <noret> {\n"
        "    if (val_ptr == null) {\n"
        "        printf(\"No value provided.\\n\");\n"
        "    } else {\n"
        "        printf(\"Value is: %d\\n\", *val_ptr);\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <int> = 500;\n"
        "    print_if_exists(&a);\n"
        "    print_if_exists(null);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "Value is: 500\nNo value provided.\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoPointersToTheSameObjectCompareEqual) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let y <int> = 1;\n"
        "    let p <&int> = &x;\n"
        "    let q <&int> = &x;\n"
        "    let r <&int> = &y;\n"
        "    if (p == q) { printf(\"same\\n\"); }\n"
        "    if (p != r) { printf(\"different\\n\"); }\n"
        "    if (p != null) { printf(\"live\\n\"); }\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    // `p != r` and not `x != y`: two objects with equal contents are two
    // addresses, so a pointer comparison that compared pointees would print
    // nothing here and be wrong in a way `p == q` cannot catch.
    EXPECT_EQ(b.out, "same\ndifferent\nlive\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFieldIsReadAndWrittenThroughAPointerWithADot) {
    // tests/samples/deeptest3.fin:34-46 and the note at :39: `.` on a pointer is
    // what C spells `->`. The write comes first so that the read cannot be
    // answered from the literal.
    const Built b = build(std::string(kPrintf) +
        "struct Player { pub hp <int>, pub score <int> }\n"
        "fun main() <noret> {\n"
        "    let p <&Player> = new Player{hp: 100, score: 0};\n"
        "    p.score = 50;\n"
        "    printf(\"Player HP: %d, Score: %d\\n\", p.hp, p.score);\n"
        "    delete p;\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "Player HP: 100, Score: 50\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFieldIsWrittenThroughAPointerToALocal) {
    // The same rule with the pointee on the stack, so that the field write is
    // observable through the *original* and not only through the pointer.
    const Built b = build(std::string(kPrintf) +
        "struct P { pub a <int>, pub b <int> }\n"
        "fun bump(q: &P) <noret> { q.a = q.a + 1; }\n"
        "fun main() <noret> {\n"
        "    let v <P> = P{a: 1, b: 2};\n"
        "    bump(&v);\n"
        "    printf(\"%d %d\\n\", v.a, v.b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructIsCopiedOutOfAPointer) {
    // `*p` on a pointer to a struct is the struct by value, so the copy does not
    // change when the original does.
    const Built b = build(std::string(kPrintf) +
        "struct P { pub a <int>, pub b <int> }\n"
        "fun main() <noret> {\n"
        "    let v <P> = P{a: 1, b: 2};\n"
        "    let p <&P> = &v;\n"
        "    let copy <P> = *p;\n"
        "    p.a = 9;\n"
        "    printf(\"%d %d %d\\n\", v.a, copy.a, copy.b);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9 1 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AChainOfPointerFieldsReachesTheFarValue) {
    // tests/samples/deeptest3.fin:83-103. `head.next.value` with both links on the
    // heap: two auto-dereferences in one expression, and the sample's own note
    // that C would need `head->next->value`.
    const Built b = build(std::string(kPrintf) +
        "struct Node { pub value <int>, pub next <&Node> = null }\n"
        "fun main() <noret> {\n"
        "    let head <&Node> = new Node{value: 1};\n"
        "    let second <&Node> = new Node{value: 2};\n"
        "    head.next = second;\n"
        "    printf(\"Head: %d\\n\", head.value);\n"
        "    printf(\"Next: %d\\n\", head.next.value);\n"
        "    delete second;\n"
        "    delete head;\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "Head: 1\nNext: 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AThreeLinkChainReachesTheLastValue) {
    // Two links is the shortest chain that can be wrong; three is the shortest
    // that can be wrong in a way two cannot -- a `.next` that resolved against the
    // first node rather than the one it was handed prints 2 here and 2 there.
    const Built b = build(std::string(kPrintf) +
        "struct Node { pub value <int>, pub next <&Node> = null }\n"
        "fun main() <noret> {\n"
        "    let c <&Node> = new Node{value: 3};\n"
        "    let b <&Node> = new Node{value: 2};\n"
        "    let a <&Node> = new Node{value: 1};\n"
        "    b.next = c;\n"
        "    a.next = b;\n"
        "    printf(\"%d %d %d\\n\", a.value, a.next.value, a.next.next.value);\n"
        "    delete c;\n"
        "    delete b;\n"
        "    delete a;\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2 3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASelfReferentialPointerFieldDefaultsToNull) {
    // tests/samples/deeptest3.fin:78-81 (`pub next <&Node> = null // Default to
    // null`). Two rules at once: a struct may hold a pointer to itself -- which is
    // the case that needs the pointee's *body* not to exist yet when the field is
    // mapped -- and the default is the null pointer.
    const Built b = build(std::string(kPrintf) +
        "struct Node { pub value <int>, pub next <&Node> = null }\n"
        "fun main() <noret> {\n"
        "    let n <Node> = Node{value: 7};\n"
        "    if (n.next == null) { printf(\"null %d\\n\", n.value); } else { printf(\"set\\n\"); }\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "null 7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APointerFieldInAStructRoundTrips) {
    const Built b = build(std::string(kPrintf) +
        "struct Holder { pub p <&int> }\n"
        "fun main() <noret> {\n"
        "    let x <int> = 41;\n"
        "    let h <Holder> = Holder{p: &x};\n"
        "    *h.p = 42;\n"
        "    printf(\"%d %d\\n\", x, *h.p);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42 42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrayOfPointersIsIndexedAndDereferenced) {
    // `[&int, 2]` is two words, and each is a pointer to somewhere else. The
    // element type being a pointer is what makes the stride a word rather than an
    // int -- `*ps[1]` reading 1 would mean the stride was four bytes.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let y <int> = 2;\n"
        "    let ps <[&int, 2]> = [&x, &y];\n"
        "    *ps[1] = 20;\n"
        "    printf(\"%d %d %d\\n\", *ps[0], *ps[1], y);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 20 20\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APointerToACharAddressesOneByte) {
    // The pointee decides the width of the load and the store. An `&char` that had
    // been read as an `&int` would write four bytes into a one-byte slot, which on
    // this stack frame is the neighbouring variable -- so `keep` is here to be
    // overwritten if that happens.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let keep <int> = 1234;\n"
        "    let c <char> = 'A';\n"
        "    let p <&char> = &c;\n"
        "    *p = 'B';\n"
        "    printf(\"%d %d %d\\n\", c, *p, keep);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "66 66 1234\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APointerToAFloatAddressesAFloat) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let f <float> = 1.5;\n"
        "    let p <&float> = &f;\n"
        "    *p = 2.5;\n"
        "    printf(\"%.2f %.2f\\n\", f, *p);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2.50 2.50\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APointerToABoolAddressesABool) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let t <bool> = true;\n"
        "    let p <&bool> = &t;\n"
        "    *p = false;\n"
        "    if (*p) { printf(\"yes\\n\"); } else { printf(\"no %d\\n\", t); }\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "no 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, NewOfAScalarStoresItsArgument) {
    // tests/samples/variables.fin:28 (`let m <&int> = new int(5);`). `new int(5)`
    // is an `&int` and not an `int`: the analyzer types it that way, and the
    // declaration it is written into says so.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <&int> = new int(5);\n"
        "    printf(\"%d\\n\", *p);\n"
        "    *p = 6;\n"
        "    printf(\"%d\\n\", *p);\n"
        "    delete p;\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n6\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, NewOfAStructRunsItsFieldDefaults) {
    // `new` builds the same value a literal does, defaults included -- it is the
    // same struct literal with a different home. A `new` that memset the
    // allocation instead would print 0 for `hp`.
    const Built b = build(std::string(kPrintf) +
        "struct P { pub hp <int> = 100, pub score <int> }\n"
        "fun main() <noret> {\n"
        "    let p <&P> = new P{score: 3};\n"
        "    printf(\"%d %d\\n\", p.hp, p.score);\n"
        "    delete p;\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "100 3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, NewOfAStructWithNothingWrittenIsZeroed) {
    const Built b = build(std::string(kPrintf) +
        "struct P { pub a <int>, pub b <int> }\n"
        "fun main() <noret> {\n"
        "    let p <&P> = new P{};\n"
        "    printf(\"%d %d\\n\", p.a, p.b);\n"
        "    delete p;\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    // Zero and not whatever malloc handed back: an allocation this file does not
    // write is still a value the program can read, and `new P{}` says every field
    // is defaulted rather than that none of them are.
    EXPECT_EQ(b.out, "0 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, NewAndDeleteRoundTripManyTimes) {
    // What `delete` actually has to get right is not observable in one iteration:
    // a `free` of the wrong pointer, or of a pointer never returned by `malloc`,
    // is a corrupted heap and glibc aborts on it. A thousand round trips also
    // means a `delete` that freed nothing would have to be caught by the allocator
    // rather than by the test, so the total is printed as well.
    const Built b = build(std::string(kPrintf) +
        "struct P { pub a <int> }\n"
        "fun main() <noret> {\n"
        "    let total <int> = 0;\n"
        "    for (let i <int> = 0; i < 1000; i++) {\n"
        "        let p <&P> = new P{a: i};\n"
        "        total = total + p.a;\n"
        "        delete p;\n"
        "    }\n"
        "    printf(\"%d\\n\", total);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "499500\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APointerReturnedFromACallIsUsedByTheCaller) {
    const Built b = build(std::string(kPrintf) +
        "struct P { pub hp <int> }\n"
        "fun make(h: int) <&P> { return new P{hp: h}; }\n"
        "fun main() <noret> {\n"
        "    let p <&P> = make(3);\n"
        "    printf(\"%d\\n\", p.hp);\n"
        "    delete p;\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGlobalsAddressCrossesAFunctionBoundary) {
    // A global's home is in the object file rather than a frame, and everything
    // downstream of an address treats the two alike -- which this checks by
    // writing through the returned pointer and reading the global back by name.
    const Built b = build(std::string(kPrintf) +
        "let G <int> = 7;\n"
        "fun get() <&int> { return &G; }\n"
        "fun main() <noret> {\n"
        "    let p <&int> = get();\n"
        "    *p = 11;\n"
        "    printf(\"%d %d\\n\", G, *get());\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "11 11\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGlobalPointerHoldsAnAddressAcrossCalls) {
    const Built b = build(std::string(kPrintf) +
        "let G <int> = 7;\n"
        "let GP <&int> = null;\n"
        "fun point() <noret> { GP = &G; }\n"
        "fun main() <noret> {\n"
        "    if (GP == null) { printf(\"start null\\n\"); }\n"
        "    point();\n"
        "    *GP = 12;\n"
        "    printf(\"%d\\n\", G);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "start null\n12\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, SizeofAPointerIsThePointerWidth) {
    // One word whatever it points at, and the same word for a pointer to a
    // pointer. Read from the module's own DataLayout like every other sizeof, so
    // this is 8 because the target says so rather than because this file does.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d %d %d\\n\", sizeof(&int), sizeof(&&int), sizeof(&[int, 64]));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8 8 8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TheAddressOfAValueWithNoHomeIsRefused) {
    // `&make()` has nothing to take the address of. Putting the returned value in
    // a fresh slot and pointing at that would compile and would answer a lifetime
    // question nobody has asked -- how long the slot lives, and what the pointer
    // means after that.
    const Built b = build(std::string(kPrintf) +
        "fun make() <int> { return 5; }\n"
        "fun main() <noret> {\n"
        "    let p <&int> = &make();\n"
        "    printf(\"%d\\n\", *p);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("address"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, TheAddressOfAStringLiteralIsACellHoldingIt) {
    // tests/samples/variables.fin:11, `let Complex <&string> = &"Hello world";`.
    //
    // Was TheAddressOfAStringLiteralIsRefused, and its argument was that the question
    // had two answers the corpus could not separate: a `string` is already a pointer to
    // bytes, so `&"..."` is either **that same pointer** -- making `&string` and
    // `string` one representation and `*Complex` a *char* -- or **the address of a cell
    // holding it**, making `*Complex` the string. Both compiled. variables.fin:11 is
    // the only `&"..."` and the only `&string` in tests/samples/ and lib/std/, and
    // nothing reads `Complex`, so no measurement could decide it.
    //
    // Ruled by the owner 2026-08-28: **the cell.** So `*Complex` is the string, and
    // `&string` behaves like `&T` for every other T.
    //
    // READ THIS BEFORE CHANGING THE ASSERTION. `%s` on `*G` is only meaningful under
    // the reading that was chosen -- under the other one `*G` is a char and `%s` is
    // wrong. That is precisely the circularity that sank the first attempt at this
    // lowering: a test written in the semantics it is trying to establish passes for
    // that reason alone. What makes this test evidence rather than an assumption is
    // that the semantics came from the ruling and the test came after; it verifies the
    // implementation against the decision, not the decision against itself.
    const Built b = build(std::string(kPrintf) +
        "let G <&string> = &\"Hello world\";\n"
        "fun main() <noret> { printf(\"%s\\n\", *G); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "Hello world\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoAddressesOfEqualLiteralsAreDistinct) {
    // A fresh holder per occurrence, not one per distinct value. LLVM may merge the
    // literal's character *data* with an identical literal's -- that data is
    // `constant`, so sharing it is invisible -- but the holder is not constant, because
    // `let G <&string>` is mutable.
    //
    // So a write through one `&"Hello world"` must not reach another `&"Hello world"`
    // written elsewhere in the program. This is the test that fails if the holder is
    // ever keyed by the literal's text, which is the natural-looking optimisation and
    // the wrong one.
    const Built b = build(std::string(kPrintf) +
        "let G <&string> = &\"Hello world\";\n"
        "let H <&string> = &\"Hello world\";\n"
        "fun main() <noret> {\n"
        "    *H = \"replaced\";\n"
        "    printf(\"%s %s\\n\", *H, *G);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "replaced Hello world\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TheAddressOfACallIsStillRefused) {
    // The other half of the fork, and the control on the test above: lowering
    // `&"literal"` must not have lowered `&expression` generally. `&make()` has no home
    // and no forced answer -- a fresh slot answers "how long does it live" by picking
    // one, and unlike a literal every candidate is a real choice with a program that
    // can tell them apart. A literal's value exists before the program starts, which is
    // what makes static storage forced rather than chosen there.
    const Built b = build(std::string(kPrintf) +
        "fun make() <int> { return 5; }\n"
        "fun main() <noret> {\n"
        "    let p <&int> = &make();\n"
        "    printf(\"%d\\n\", *p);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("no home"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASlaveofAttributeKeepsItsAllocationAlive) {
    // tests/samples/variables.fin:27 and :35. `#[slaveof(z)]` ties a local's storage to
    // another variable's lifetime and `#[slaveof($Fin)]` asks for "until the program
    // exits". For this destructor-less storage both lower to nothing -- because
    // nothing in this backend frees a heap allocation implicitly (ADR 0003:
    // memory management is a library), so an allocation nobody `delete`s
    // already outlives every scope. That is the narrowed remainder of the
    // 2026-08-28 no-op ruling: wave-5 slice 4 revisited it, and struct-typed
    // ties now delay destruction (the test below), while this spelling keeps
    // the old lowering.
    //
    // The assertion is the *consequence*: `m` is allocated inside a
    // block, `z` outlives that block, and the read through `z` afterwards must give 5.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let z <&int>;\n"
        "    {\n"
        "        #[slaveof(z)]\n"
        "        let m <&int> = new int(5);\n"
        "        z = m;\n"
        "    }\n"
        "    printf(\"%d\\n\", *z);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASlaveofTiedStructDestroysAtItsMastersExit) {
    // Wave-5 slice 4: the real rule. `m` is a struct-typed local declared in
    // an inner block and tied to the outer `z`, so its destructor must not
    // run when the inner block ends but when `z`'s scope does -- at function
    // fallthrough here. Lowered to nothing before this slice (the 2026-08-28
    // no-op ruling), so this goes red against "inner\ndtor 7\nouter\n".
    const Built b = build(std::string(kPrintf) +
        "struct Track {\n"
        "    v <int>,\n"
        "    ~Track() { printf(\"dtor %d\\n\", self.v); }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let z <int> = 0;\n"
        "    {\n"
        "        #[slaveof(z)]\n"
        "        let m <Track> = Track{ v: 7 };\n"
        "        printf(\"inner\\n\");\n"
        "    }\n"
        "    printf(\"outer\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "inner\nouter\ndtor 7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMovedTiedBindingStillSkipsItsDestructor) {
    // Slice-1 Maybe-skip stands at the extended point: `m` is moved-from on
    // this path, so deferring its destruction to `z`'s exit must not
    // resurrect it. `q` (moved-to) destroys at the inner exit, `z` at
    // function fallthrough; `m` never does. A guard -- green before this
    // slice (the skip engaged at the inner exit) and green after.
    const Built b = build(std::string(kPrintf) +
        "#[protocol(move_or_copy)]\n"
        "@special mymove(s: $struct) <quote> {}\n"
        "struct Track {\n"
        "    v <int>,\n"
        "    ~Track() { printf(\"dtor %d\\n\", self.v); }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let z <Track> = Track{ v: 0 };\n"
        "    {\n"
        "        #[slaveof(z)]\n"
        "        let m <Track> = Track{ v: 1 };\n"
        "        let q <Track> = @move(m);\n"
        "        printf(\"body\\n\");\n"
        "    }\n"
        "    printf(\"after\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "body\ndtor 1\nafter\ndtor 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASlaveofOnPlainDataLowersIdentically) {
    // The narrowed no-op half: destructor-less storage has no scope-exit
    // cleanup to delay, so tying it must not change the generated code at
    // all. Compiles both spellings to objects and compares the disassembled
    // text (unclaimed programs take the same path by construction -- no
    // attribute, no new code -- and the suite around this pins their
    // behaviour).
    const std::string plain =
        std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let z <int> = 1;\n"
        "    let m <int> = 2;\n"
        "    printf(\"%d\\n\", z + m);\n"
        "}\n";
    const std::string tied =
        std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let z <int> = 1;\n"
        "    #[slaveof(z)]\n"
        "    let m <int> = 2;\n"
        "    printf(\"%d\\n\", z + m);\n"
        "}\n";
    if (std::system("llvm-objdump --version > /dev/null 2>&1") != 0)
        GTEST_SKIP() << "llvm-objdump not on PATH";
    // `finc -c` writes <stem>.o beside the working directory: recover each
    // object by the stem of the source that produced it.
    auto objectFor = [](const std::string& code) {
        fs::path src = uniqueTempPath("fin_slaveof", ".fin");
        { std::ofstream f(src, std::ios::binary); f.write(code.data(), (std::streamsize)code.size()); }
        const FincRun c = runFinc({src.string(), "-c"});
        fs::path obj = fs::current_path() / (src.stem().string() + ".o");
        fs::remove(src);
        if (c.exitCode != 0) return fs::path{};
        return obj;
    };
    auto disassemble = [](const fs::path& obj) {
        // No shell: the object path reaches llvm-objdump as one argv element,
        // and merged stdout/stderr lands in a file the same as `2>&1` did.
        fs::path tmp = uniqueTempPath("fin_slaveof_dis");
        if (fin::runProcess({"llvm-objdump", "-d", "--no-show-raw-insn", obj.string()},
                            tmp.string(), tmp.string()) != 0)
            return std::string{};
        std::string out = readProcessOutput(tmp.string());
        std::error_code ec;
        fs::remove(tmp, ec);
        // Everything up to the line naming the object file itself (a leading
        // blank, then "<obj>: file format ...") is harness spelling, not
        // generated code; what follows it is.
        const size_t marker = out.find("file format");
        if (marker == std::string::npos) return out;
        const size_t eol = out.find('\n', marker);
        return eol == std::string::npos ? out : out.substr(eol + 1);
    };
    const fs::path plainObj = objectFor(plain);
    const fs::path tiedObj = objectFor(tied);
    ASSERT_FALSE(plainObj.empty()) << "plain variant failed to compile";
    ASSERT_FALSE(tiedObj.empty()) << "tied variant failed to compile";
    EXPECT_EQ(disassemble(plainObj), disassemble(tiedObj));
    std::error_code ec;
    fs::remove(plainObj, ec);
    fs::remove(tiedObj, ec);
}

BACKEND_TEST(Soundness_Codegen, ASlaveofAcrossALoopIsRefused) {
    // The deferral moves a destruction to the referent's scope exit, past a
    // back-edge the declaration's block does not dominate -- so the slot may
    // never have been constructed where it would be destroyed. Refused
    // naming the tie rather than lowered into invalid IR.
    const Built b = build(std::string(kPrintf) +
        "struct Track {\n"
        "    v <int>,\n"
        "    ~Track() { printf(\"dtor %d\\n\", self.v); }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let z <int> = 0;\n"
        "    let i <int> = 0;\n"
        "    while (i < 2) {\n"
        "        #[slaveof(z)]\n"
        "        let m <Track> = Track{ v: 9 };\n"
        "        printf(\"iter\\n\");\n"
        "        i = i + 1;\n"
        "    }\n"
        "    printf(\"done\\n\");\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("slaveof"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnUnreadAttributeOnAVariableIsStillRefused) {
    // `#[slaveof]` is exempt because it provably cannot change the generated code. That
    // exemption must not have become a general one: an attribute this file does not
    // read may be the one that decides where the variable lives, and accepting it is
    // claiming to have done what it asked.
    const Built b = build(
        "fun main() <noret> {\n"
        "    #[nosuchattribute]\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("nosuchattribute"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, IndexingAPointerToAScalarIsRefused) {
    // `p[0]` on a `&int` is pointer arithmetic, and whether a pointer strides by
    // an element or a byte is the same unmade ruling that refuses `p++`
    // (AnIncrementOnAPointerIsRefused). A pointer to an *array* is not this case
    // and is lowered: its extent is written down, so the index is into a known
    // shape rather than off the end of an unknown one.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let p <&int> = &x;\n"
        "    printf(\"%d\\n\", p[0]);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APointerAsAConditionIsRefused) {
    // `if (p)` needs "a pointer is true when it is not null" to be a rule, and
    // Fin's nullability rules are not settled -- the corpus writes `p == null`
    // every time it asks the question (deeptest3.fin:64).
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let p <&int> = &x;\n"
        "    if (p) { printf(\"y\\n\"); }\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
}

// ---------------------------------------------------------------------------
// Generic structs.
//
// tests/samples/struct_methods.fin:6 names the strategy itself, in a comment
// beside the declaration: "T is a generic and it will be a Monomorphization
// Generic type because its the default generic type we use".
//
// ADR 0002 says the same thing from the other side -- it carries two lowering
// decisions forward from pyprototype: "erasure is selected by the presence of an
// erasure-marker constraint on any one parameter, and an erased generic is
// represented as a raw pointer". A bare `<T>` carries no such constraint, so the
// default is the other branch, and these tests are that branch: one distinct
// type per distinct type argument, laid out as if the argument had been written
// in place of the parameter.
//
// The uses come from the corpus:
//
//   `struct Box<T> { val <T> }` with `let b <Box<int>> = Box::<int>{ val: 100 };`
//   (complex.fin:7,12) -- one parameter, one field, and a read of it.
//
//   `struct Result<T> { value <T>, is_error <bool> }` used at `Result<int>` and at
//   `Result<Result<int>>` (functions.fin:5,16) -- an instantiation is itself a
//   type argument, so the substitution has to nest.
//
//   `struct Vec2<T> { x <T> = 0, y <T> = 0 }` (letssee.fin:9-12) -- a parameter's
//   defaults are written once and have to typecheck against whatever T became.
//
//   `struct M <T> {}` (blame_assert.fin:19) -- declared, never instantiated. A
//   template nobody uses lowers to nothing at all, which is why the field checks
//   move from the declaration to the instantiation.
//
// The hazard the tests below are aimed at is sharing: if two instantiations
// collided in the table, `Box<bool>`'s field would be read at `Box<int>`'s width
// and every one of these programs would still compile.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AGenericStructIsMonomorphisedAtItsTypeArgument) {
    // tests/samples/complex.fin:7-18, less the module alias.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 100 };\n"
        "    if (b.val > 50) { printf(\"Big\\n\"); } else { printf(\"Small\\n\"); }\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "Big\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoInstantiationsOfOneGenericStructAreDistinctTypes) {
    // The test the whole unit exists to pass. `Box<char>` holds one byte and
    // `Box<long>` holds eight; if they shared a StructInfo the second store would
    // write eight bytes into the first's slot, and nothing in the type checker
    // would have anything to say about it. Printed together so a clobber shows up
    // as a wrong number rather than as a crash that might be anything.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let small <Box<char>> = Box::<char>{ val: 'A' };\n"
        "    let big <Box<long>> = Box::<long>{ val: 1234 };\n"
        "    let flag <Box<bool>> = Box::<bool>{ val: true };\n"
        "    printf(\"%d %lld %d\\n\", cast<int>(small.val), big.val,\n"
        "           cast<int>(flag.val));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "65 1234 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, RepeatedUsesOfOneInstantiationAreOneType) {
    // The other half of the same fact: `Box<int>` written three times is one type,
    // not three. An assignment between two of them proves it -- distinct
    // llvm::StructTypes with identical bodies would refuse the store, and two
    // *named* struct types are always distinct in LLVM however alike their bodies.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>\n"
        "}\n"
        "fun take(b: Box<int>) <int> { return b.val; }\n"
        "fun main() <noret> {\n"
        "    let a <Box<int>> = Box::<int>{ val: 7 };\n"
        "    let c <Box<int>> = a;\n"
        "    printf(\"%d %d\\n\", c.val, take(c));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7 7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericStructsFieldDefaultsRunAtEachInstantiation) {
    // tests/samples/letssee.fin:9-12 (`struct Vec2<T> { x <T> = 0, y <T> = 0 }`).
    // The default is one expression shared by every instantiation, so it is
    // evaluated once per literal that omits the field and against that
    // instantiation's field type.
    const Built b = build(std::string(kPrintf) +
        "struct Vec2<T> {\n"
        "    x <T> = 0,\n"
        "    y <T> = 0\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <Vec2<int>> = Vec2::<int>{ x: 3 };\n"
        "    let b <Vec2<int>> = Vec2::<int>{};\n"
        "    printf(\"%d %d %d %d\\n\", a.x, a.y, b.x, b.y);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3 0 0 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericStructIsAnArgumentToItself) {
    // tests/samples/functions.fin:16 (`let res <Result<Result<int>>>`). The
    // substitution has to nest: instantiating `Result<Result<int>>` needs
    // `Result<int>` to already be a type, and that one is discovered while mapping
    // the outer one's arguments rather than at a declaration.
    const Built b = build(std::string(kPrintf) +
        "struct Result<T> {\n"
        "    value <T>,\n"
        "    is_error <bool>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let inner <Result<int>> = Result::<int>{ value: 42, is_error: false };\n"
        "    let outer <Result<Result<int>>> =\n"
        "        Result::<Result<int>>{ value: inner, is_error: false };\n"
        "    printf(\"%d %d\\n\", outer.value.value,\n"
        "           cast<int>(outer.value.is_error));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoTypeParametersBindInWrittenOrder) {
    // `Pair<int, char>` and `Pair<char, int>` are different types, and the only
    // thing that tells them apart is position. A substitution keyed by name but
    // filled in the wrong order gives both the same layout and prints the same
    // two numbers for both.
    const Built b = build(std::string(kPrintf) +
        "struct Pair<A, B> {\n"
        "    first <A>,\n"
        "    second <B>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Pair<int, char>> = Pair::<int, char>{ first: 300, second: 'z' };\n"
        "    let q <Pair<char, int>> = Pair::<char, int>{ first: 'z', second: 300 };\n"
        "    printf(\"%d %d %d %d\\n\", p.first, cast<int>(p.second),\n"
        "           cast<int>(q.first), q.second);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "300 122 122 300\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ATypeParameterUnderAPointerIsSubstituted) {
    // deeptest3.fin:78's `struct Node { next <&Node> = null }` written generically.
    // Two things at once: the parameter is substituted through a decoration rather
    // than as a whole field type, and the instantiation refers to itself, so it
    // has to exist as an incomplete name before its own fields are mapped.
    const Built b = build(std::string(kPrintf) +
        "struct Node<T> {\n"
        "    value <T>,\n"
        "    next <&Node<T>> = null\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let tail <Node<int>> = Node::<int>{ value: 2 };\n"
        "    let head <Node<int>> = Node::<int>{ value: 1, next: &tail };\n"
        "    printf(\"%d %d\\n\", head.value, head.next.value);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ATypeParameterUnderAnArrayIsSubstituted) {
    // The other decoration: `[T, 3]` becomes `[char, 3]`, which is three bytes and
    // not three words. The trailing `guard` is there to be overwritten if the
    // element width came from the parameter instead of the argument.
    const Built b = build(std::string(kPrintf) +
        "struct Buf<T> {\n"
        "    items <[T, 3]>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Buf<char>> = Buf::<char>{ items: ['a', 'b', 'c'] };\n"
        "    let guard <int> = 4321;\n"
        "    printf(\"%d %d %d %d\\n\", cast<int>(b.items[0]),\n"
        "           cast<int>(b.items[2]), sizeof(Buf<char>), guard);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "97 99 3 4321\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInstantiationCrossesAFunctionBoundaryBothWays) {
    // A parameter and a return of the same instantiation, which is the shape
    // functions.fin:13-17 uses. The types are written in three separate places
    // here (the parameter, the return, the local) and all three have to resolve to
    // the one type or the call will not typecheck in LLVM.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>\n"
        "}\n"
        "fun bump(b: Box<int>) <Box<int>> {\n"
        "    return Box::<int>{ val: b.val + 1 };\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <Box<int>> = Box::<int>{ val: 5 };\n"
        "    let c <Box<int>> = bump(bump(a));\n"
        "    printf(\"%d\\n\", c.val);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, SizeofAnInstantiationIsTheSubstitutedSize) {
    // The layout question asked directly. `sizeof` reads the same DataLayout the
    // allocation and the GEPs read, so an instantiation whose fields were mapped
    // at the wrong width would disagree here first.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>\n"
        "}\n"
        "struct Pair<A, B> {\n"
        "    first <A>,\n"
        "    second <B>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d %d %d\\n\", sizeof(Box<char>), sizeof(Box<int>),\n"
        "           sizeof(Box<double>), sizeof(Pair<char, char>));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 4 8 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInstantiationIsWrittenThroughAPointer) {
    // The pointer unit meeting this one: `&Box<int>` is a pointer to the
    // instantiation, and `p.val = 9` GEPs through it into the substituted field.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 1 };\n"
        "    let p <&Box<int>> = &b;\n"
        "    p.val = 9;\n"
        "    printf(\"%d %d\\n\", b.val, p.val);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9 9\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInstantiationIsAnArrayElement) {
    // `[Box<int>, 2]` needs the instantiation's size before the array's, so this
    // is the ordering test: the element type has to be complete at the moment the
    // array asks how wide it is.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let boxes <[Box<int>, 2]> = [Box::<int>{ val: 4 }, Box::<int>{ val: 6 }];\n"
        "    printf(\"%d %d %d\\n\", boxes[0].val, boxes[1].val, boxes.length);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4 6 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, NewOfAnInstantiationAllocatesTheSubstitutedSize) {
    // `new Box::<T>{...}` allocates sizeof(the instantiation), not sizeof(the
    // template) -- the template has no size at all. Two widths so a fixed size
    // taken from the wrong one shows up.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <&Box<char>> = new Box::<char>{ val: 'Q' };\n"
        "    let q <&Box<long>> = new Box::<long>{ val: 999999 };\n"
        "    printf(\"%d %lld\\n\", cast<int>(p.val), q.val);\n"
        "    delete p;\n"
        "    delete q;\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "81 999999\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericStructNobodyInstantiatesLowersToNothing) {
    // tests/samples/blame_assert.fin:19 (`struct M <T> {}`). A template is not a
    // type and has no layout, so there is nothing to emit and nothing to refuse --
    // which is why the field checks belong at the instantiation. Note that this
    // one is also empty, and the two facts are independent: `M<int>` gets a byte
    // (see the next test) and this declaration still emits nothing, because what
    // is deferred here is the layout and not the emptiness.
    const Built b = build(std::string(kPrintf) +
        "struct M <T> {}\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEmptyGenericStructLowersWhereItIsInstantiated) {
    // Was AnEmptyGenericStructRefusesWhereItIsInstantiated. The deferral it described
    // is still the right shape -- `struct M<T> {}` has no layout until an argument is
    // named, which is why the test above it still passes with nothing emitted for the
    // template -- but the question the instantiation used to run into is answered now,
    // so `M<int>` gets the same one byte a non-generic empty struct gets.
    const Built b = build(std::string(kPrintf) +
        "struct M <T> {}\n"
        "fun main() <noret> {\n"
        "    let m <M<int>> = M::<int>{};\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericStructInstantiatedAtADynamicArrayCarriesThePair) {
    // Was AnInstantiationAtATypeThisFileCannotLowerIsRefused, and its argument was
    // that `Box<[int]>` is "a fine template at a type argument with no representation
    // yet: a dynamic `[T]` is the undecided one", so the refusal had to name the
    // *argument* rather than the template, because `Box<int>` beside it still worked.
    // ADR 0025 gave the argument a representation, so the refusal has nothing left to
    // report -- and the half of that argument worth keeping is the half about the
    // template, which is why this asserts through the field rather than merely
    // compiling.
    //
    // Reading `b.val.length` and `b.val[1]` is the point. A `[T]` inside a struct is
    // the case a `{ptr, len}` representation makes work and a bare-pointer one cannot:
    // the length is a field of the pair, so it survives being stored in and loaded
    // back out of an enclosing aggregate. `stdlib/collection.fin:51` (`_arr <[T]>`) is
    // the corpus's own version of this shape.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<[int]>> = Box::<[int]>{ val: [1, 2] };\n"
        "    printf(\"%d %d\\n\", b.val.length, b.val[1]);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericStructsMethodIsCalledOnAnInstantiation) {
    // Was AGenericStructsMethodsAreRefusedWhereverTheyAppear, which said in its own
    // comment that monomorphising the *type* must not be mistaken for having
    // monomorphised the methods. It is not mistaken for it any more: the instance's
    // methods are emitted where the instance is built, with T bound to what the
    // instance bound it to.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>\n"
        "    fun get(self: &Box<T>) <T> { return self.val; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 3 };\n"
        "    printf(\"%d\\n\", b.get());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3\n") << b.why();
}


// ---------------------------------------------------------------------------
// `#[llvm_name]` on a struct declaration
//
// struct_methods.fin:5 writes `#[llvm_name="general_point"]` above `struct Point<T>`
// and calls it, in its own comment, "a rust like attribute for compile time codegen
// manipulation (for specific statements like struct declarations)"; letssee.fin:8
// writes `#[llvm_name="vec2_f32"]` above `struct Vec2<T>`. The same attribute already
// works on an `@define`, where it binds a C symbol whose spelling differs from the Fin
// name (stdlib/stdio.fin:11), and this file already reads it there.
//
// On a struct it names something with much less riding on it. An llvm::StructType's
// name is metadata for whoever reads the IR: LLVM compares struct types structurally,
// nothing in the object file refers to a type by name, and two types asking for one
// name are uniqued by LLVM rather than merged. So honouring it cannot change what a
// program computes -- which is what most of the tests below assert, because the risk
// with a rename is not that it does too little but that it quietly does too much.
//
// Every other attribute stays refused, and for the reason the blanket refusal gave:
// an attribute this file cannot read may be one that changes the layout, and ignoring
// it is the failure mode that produces a working program with the wrong offsets.

BACKEND_TEST(Soundness_Codegen, AStructsLlvmNameNamesTheLlvmType) {
    const std::string trace = codegenTrace(
        "#[llvm_name=\"general_point\"]\n"
        "struct Point { x <int>, y <int> }\n"
        "fun use(p: Point) <int> { return p.x; }\n");
    EXPECT_NE(trace.find("general_point"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, AStructWithNoLlvmNameKeepsItsFinName) {
    // The other half of the previous test: the trace has to be able to tell the two
    // apart, or asserting on it proves nothing.
    const std::string trace = codegenTrace(
        "struct Point { x <int>, y <int> }\n"
        "fun use(p: Point) <int> { return p.x; }\n");
    EXPECT_NE(trace.find("declared struct Point"), std::string::npos) << trace;
    EXPECT_EQ(trace.find("general_point"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, AStructsLlvmNameDoesNotChangeWhatItComputes) {
    // struct_methods.fin's own struct, less the methods, which are a separate unit.
    const Built b = build(std::string(kPrintf) +
        "#[llvm_name=\"general_point\"]\n"
        "struct Point {\n"
        "    x <int>,\n"
        "    y <int> = 0\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point{ x: 7 };\n"
        "    p.y = 9;\n"
        "    printf(\"%d %d %d\\n\", p.x, p.y, cast<int>(sizeof(Point)));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7 9 8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructsLlvmNameIsNotASymbol) {
    // Why the rename is safe to honour at all. If the name reached the object file
    // the way an `@define`'s does, one name on a template with two instantiations
    // would be a duplicate-symbol link error; it does not, so it cannot be.
    const Built b = build(std::string(kPrintf) +
        "#[llvm_name=\"printf\"]\n"
        "struct S { v <int> }\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{ v: 4 };\n"
        "    printf(\"%d\\n\", s.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericStructsLlvmNameNamesEveryInstantiation) {
    // letssee.fin:8's shape: one `#[llvm_name="vec2_f32"]` above a template that
    // could be instantiated at anything. The attribute names the template, so both
    // instantiations ask for it and LLVM uniques the second -- which is visible in
    // the trace and is the reason the next test exists.
    const std::string trace = codegenTrace(
        "#[llvm_name=\"vec2_f32\"]\n"
        "struct Vec2<T> { x <T>, y <T> }\n"
        "fun use(a: Vec2<int>, b: Vec2<char>) <int> { return a.x; }\n");
    EXPECT_NE(trace.find("vec2_f32"), std::string::npos) << trace;
    EXPECT_NE(trace.find("Vec2<int>"), std::string::npos) << trace;
    EXPECT_NE(trace.find("Vec2<char>"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, TwoInstantiationsUnderOneLlvmNameKeepDistinctLayouts) {
    // The rename is a name and nothing else. `Vec2<char>` is two bytes where
    // `Vec2<int>` is eight whatever they are called, and a rename that collapsed
    // them into one type would be a miscompile that a name check would not catch.
    const Built b = build(std::string(kPrintf) +
        "#[llvm_name=\"vec2_f32\"]\n"
        "struct Vec2<T> { x <T>, y <T> }\n"
        "fun main() <noret> {\n"
        "    let a <Vec2<char>> = Vec2::<char>{ x: 'a', y: 'b' };\n"
        "    let b <Vec2<int>> = Vec2::<int>{ x: 300, y: 400 };\n"
        "    printf(\"%d %d %d %d %d\\n\", cast<int>(sizeof(Vec2<char>)),\n"
        "           cast<int>(sizeof(Vec2<int>)), cast<int>(a.y), b.x, b.y);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2 8 98 300 400\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAttributeThisFileDoesNotReadIsStillRefused) {
    // `#[future]` stands in for any attribute with no reader: what it would
    // exclude is unknown, so honouring `llvm_name` must not turn the attribute
    // check into "attributes are decoration". (`#[uncastable]` served here
    // until it gained a reader -- refusing casts -- at which point the fixture
    // moved to one that still has none.)
    const Built b = build(std::string(kPrintf) +
        "#[future]\n"
        "struct S { v <int> }\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{ v: 1 };\n"
        "    printf(\"%d\\n\", s.v);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("future"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnLlvmNameBesideAnUnreadAttributeIsStillRefused) {
    // `#[llvm_name="Error"] #[future]`: reading one of the two is not
    // permission to drop the other. (Was `#[uncastable]` second; it has a
    // reader now, so the fixture moved to one that does not.)
    const Built b = build(std::string(kPrintf) +
        "#[llvm_name=\"Error\"]\n"
        "#[future]\n"
        "struct S { v <int> }\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{ v: 1 };\n"
        "    printf(\"%d\\n\", s.v);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFlagFormLlvmNameIsRefused) {
    // `#[llvm_name]` with no value names nothing. Treating it as absent would be a
    // guess at what the writer meant, and the writer of the only three sites in the
    // corpus always wrote a value.
    const Built b = build(std::string(kPrintf) +
        "#[llvm_name]\n"
        "struct S { v <int> }\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{ v: 1 };\n"
        "    printf(\"%d\\n\", s.v);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
}


// ---------------------------------------------------------------------------
// A struct's own functions are functions
//
// Found by the `#[llvm_name]` unit above, which stopped refusing struct_methods.fin
// at line 5 -- and the sample then compiled clean to an object with no symbols in
// it at all, for a source that declares four functions. operators.fin was the same:
// two `operator` bodies, an object without them.
//
// Nothing miscomputed, because nothing can reach them: a method call is refused at
// the call and an operator on a struct has no lowering either. But "unreachable
// today" is the reasoning that produces a miscompile tomorrow, and this suite's rule
// is the one at the top of the file -- a construct the backend cannot lower must be
// refused, never skipped. A free function that is never called is still emitted; a
// method that is never called was not, and the source gave no sign.
//
// So the refusal moves to the declaration, where the reader can act on it, and it
// covers the three shapes that are function bodies: methods, operators and
// constructors. The destructor was already refused there, for the stronger reason
// that it also has to *run*.

BACKEND_TEST(Soundness_Codegen, AStructsMethodIsEmittedEvenIfNobodyCallsIt) {
    // Was AStructsMethodIsRefusedAtItsDeclaration, and the reason it was a refusal
    // is the reason it is now this test: the object used not to contain `get` at
    // all, for a source that declares it. A free function nobody calls is still
    // emitted, and a method is a function.
    //
    // `self: &Box` rather than `&Self`, because both spellings are the receiver and
    // struct_methods.fin:10 says so in as many words.
    const Built b = build(std::string(kPrintf) +
        "struct Box {\n"
        "    val <int>,\n"
        "    fun get(self: &Box) <int> { return self.val; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box> = Box{ val: 3 };\n"
        "    printf(\"%d\\n\", b.val);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3\n") << b.why();
    EXPECT_NE(codegenTrace(std::string(kPrintf) +
        "struct Box {\n"
        "    val <int>,\n"
        "    fun get(self: &Box) <int> { return self.val; }\n"
        "}\n").find("declared Box.get"), std::string::npos);
}

BACKEND_TEST(Soundness_Codegen, AStructsOperatorIsEmittedEvenIfNobodyWritesIt) {
    // operators.fin:19-21 verbatim in shape: an operator body on a struct whose `main`
    // never applies it. The same rule a method gets -- the object file contains the
    // functions the source declared, whether or not anything reaches them -- and here
    // it is the whole of what the normative sample asks for, because operators.fin's
    // `main` only prints an enum.
    const Built b = build(std::string(kPrintf) +
        "struct MyInt {\n"
        "    val <int>,\n"
        "    operator -(other: <int>) <int> {\n"
        "        return self.val + other;\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
    EXPECT_NE(codegenTrace(std::string(kPrintf) +
        "struct MyInt {\n"
        "    val <int>,\n"
        "    operator -(other: <int>) <int> { return self.val + other; }\n"
        "}\n").find("declared MyInt.operator-"), std::string::npos);
}

BACKEND_TEST(Soundness_Codegen, AStructsConstructorIsEmittedAndCallable) {
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    constructor(nx: int) { self.x = nx; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point(7);\n"
        "    printf(\"%d\\n\", p.x);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
    EXPECT_NE(codegenTrace(std::string(kPrintf) +
        "struct Point { x <int>, constructor(nx: int) { self.x = nx; } }\n").find("declared Point.constructor"), std::string::npos);
}

BACKEND_TEST(Soundness_Codegen, AConstructorWritesThroughTheCallersStorage) {
    // The calling convention, stated as a test rather than as a comment. The object is
    // the caller's: it allocates, passes the address as parameter 0, and the
    // constructor's stores land in it. A by-value return that forgot to copy back
    // passes the previous shape of this test and prints uninitialised memory, so the
    // assertion is on a field the constructor computes from another -- `y` cannot come
    // out right by accident.
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    y <int>,\n"
        "    constructor(nx: int) { self.x = nx; self.y = nx * 2; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point(7);\n"
        "    printf(\"%d %d\\n\", p.x, p.y);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7 14\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFieldNoConstructorAssignsIsZero) {
    // `z` is declared and never written, by the constructor or the call. Zero rather
    // than whatever the frame held -- the answer a local with no initialiser gets in
    // this file, and the one answer a test can pin at all.
    const Built b = build(std::string(kPrintf) +
        "struct Sparse {\n"
        "    a <int>,\n"
        "    z <int>,\n"
        "    constructor(n: int) { self.a = n; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <Sparse> = Sparse(4);\n"
        "    printf(\"%d %d\\n\", s.a, s.z);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AConstructorMayReturnAnAllocationOfItsOwnStruct) {
    // `return new S{...}` is what six of the fifteen constructors in the corpus and
    // lib/std write, lib/std/error.fin:66 among them. The constructor's emitted result
    // is void and the object is the caller's, so the returned pointer is read back and
    // copied into the caller's storage rather than returned -- which is the only
    // reading under which the value the body built is the value the caller sees.
    const Built b = build(std::string(kPrintf) +
        "struct Box {\n"
        "    v <int>,\n"
        "    constructor(n: int) { return new Box{v: n + 1}; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <Box> = Box(1);\n"
        "    printf(\"%d\\n\", a.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AConstructorMayReturnALiteralOfItsOwnStruct) {
    // The other half of the same rule, without the allocation. A returned aggregate is
    // stored through the receiver as it is; that the two forms agree is what makes the
    // load in the `new` case a representation detail rather than a second convention.
    const Built b = build(std::string(kPrintf) +
        "struct Plain {\n"
        "    v <int>,\n"
        "    constructor(n: int) { return Plain{v: n + 5}; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Plain> = Plain(1);\n"
        "    printf(\"%d\\n\", b.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "6\n") << b.why();
}

// ---------------------------------------------------------------------------
// A bare field name in a body with a receiver means `self`'s field.
//
// The analyzer resolves it that way (Analyzer_Expr.cpp's "Implicit Field
// Access"), so a backend that refuses the name disagrees with the front end
// about one program. deeptest2.fin:50-51 writes `delete &name;` in `~Person()`,
// which needs the address half; a read needs the value half.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, ABareFieldNameReadsThroughTheReceiver) {
    const Built b = build(std::string(kPrintf) +
        "struct Box {\n"
        "    val <int>,\n"
        "    fun get() <int> { return val; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box> = Box{ val: 3 };\n"
        "    printf(\"%d\\n\", b.get());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ABareFieldNameHasAnAddressThroughTheReceiver) {
    // `*(&val) = 9` is the address half without `delete`'s freeing semantics:
    // one address, computed once, stored through.
    const Built b = build(std::string(kPrintf) +
        "struct Box {\n"
        "    val <int>,\n"
        "    fun setthru() <noret> { *(&val) = 9; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box> = Box{ val: 3 };\n"
        "    b.setthru();\n"
        "    printf(\"%d\\n\", b.val);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADestructorBodyMayDeleteABareField) {
    // deeptest2.fin:49-52 verbatim in shape: `~Person()` deletes its fields by
    // their bare names. Nothing runs the destructor yet (no implicit scope-exit
    // rule), so this asserts the body lowers -- the symbol is emitted and the
    // file compiles -- rather than a value.
    const std::string prog = std::string(kPrintf) +
        "struct Person {\n"
        "    name <string>,\n"
        "    age <int>,\n"
        "    ~Person() {\n"
        "        delete &name;\n"
        "        delete &age;\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"ok\\n\");\n"
        "}\n";
    const Built b = build(prog);
    ASSERT_EQ(b.compileExit, 0) << b.why();
    EXPECT_NE(codegenTrace(prog).find("declared Person.destructor"),
              std::string::npos);
}

// ---------------------------------------------------------------------------
// `super::<Parent>::member` names the parent and reaches through `self`.
//
// deeptest2.fin:71-73 writes field stores and a method call in this form. The
// parent's fields sit at the offsets they have in the parent (declareStructs'
// splice, ADR 0029's sharing), so the qualifier selects an implementation
// without moving any bytes.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, ASuperQualifiedFieldStoreReachesThroughSelf) {
    const Built b = build(std::string(kPrintf) +
        "struct P {\n"
        "    p <int>\n"
        "}\n"
        "struct S : <P> {\n"
        "    fun setp(n: int) <noret> { super::<P>::p = n; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{ p: 1 };\n"
        "    s.setp(2);\n"
        "    printf(\"%d\\n\", s.p);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASuperQualifiedMethodCallUsesSelfAsReceiver) {
    const Built b = build(std::string(kPrintf) +
        "struct P {\n"
        "    p <int>,\n"
        "    fun get() <int> { return self.p; }\n"
        "}\n"
        "struct S : <P> {\n"
        "    fun getp() <int> { return super::<P>::get(); }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{ p: 7 };\n"
        "    printf(\"%d\\n\", s.getp());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASelfCallConstructsTheEnclosingStruct) {
    // deeptest2.fin:91-93 verbatim in shape: a static returning `Self()`.
    // `Self` is the enclosing struct, so this is the constructor call the
    // analyzer already selected -- spelled through the struct, not invented.
    const Built b = build(std::string(kPrintf) +
        "struct Box {\n"
        "    v <int>,\n"
        "    Box() { self.v = 1; }\n"
        "    static fun make() <Self> { return Self(); }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box> = Box::make();\n"
        "    printf(\"%d\\n\", b.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ABodilessInterfaceConstructorLowersToNothing) {
    // deeptest2.fin:104-108 verbatim in shape: `HasConstructor` requires a
    // constructor and a destructor without giving either a body. A requirement
    // is not code -- bodiless method requirements already emit nothing -- so
    // neither is a refusal. A body on either stays refused.
    const Built b = build(std::string(kPrintf) +
        "interface HasConstructor {\n"
        "    Self();\n"
        "    ~Self();\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

// ---------------------------------------------------------------------------
// `any` maps as an opaque blob; its values do not lower.
//
// `any` is `{i8*, i64}` (payload, typeid) for layout and signatures only --
// the plan-fixed shape, with no claim about what a value in it means. Boxing
// a value into one, converting either way, comparing, sizing, and calling
// through it all refuse: a blob holding nothing is storage, and storage that
// answers reads is a wrong answer rather than a missing feature.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AFunctionFieldOverAnyLaysOut) {
    // The HashMap.hasher shape (`hasher? <fn(any) -> int>`) minus the
    // nullable, whose discriminant is its own unruled question: a function
    // field whose parameter is `any` lays out, defaults to null, and is
    // never called here.
    const Built b = build(std::string(kPrintf) +
        "struct Holder {\n"
        "    h <fn(any) -> int> = null,\n"
        "    v <int>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let h <Holder> = Holder{ v: 5 };\n"
        "    printf(\"%d\\n\", h.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAssignmentIntoAnyBoxesScalar) {
    const Built b = build(
        "fun main() <noret> {\n"
        "    let x <any>;\n"
        "    x = 5;\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAnyEqualityComparesTypeidAndPayload) {
    // ADR 0034 amendment: `==`/`!=` on two `any` values compare typeid first,
    // then payload (strings by bytes) -- the same rule map keys use, so `==`
    // agrees with lookup. Other operators on `any` still refuse.
    const Built b = build(std::string(kPrintf) +
        "fun dup(s: string) <string> { return s; }\n"
        "fun main() <noret> {\n"
        "    let a <any> = 41;\n"
        "    let c <any> = 41;\n"
        "    let d <any> = 42;\n"
        "    let s <any> = \"hi\";\n"
        "    let e <any> = \"hi\";\n"
        "    let f <any> = dup(\"hi\");\n"
        "    if (a == c) { printf(\"eq\\n\"); }\n"
        "    if (a != d) { printf(\"ne\\n\"); }\n"
        "    if (a != s) { printf(\"tid\\n\"); }\n"
        "    if (e == f) { printf(\"seq\\n\"); }\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "eq\nne\ntid\nseq\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAnyEqualityAgainstAPlainValueBoxesIt) {
    // One side `any`, the other not: the plain side boxes, then the rule above
    // runs. `y == 41` is true, `y == 42` is not.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let y <any> = 41;\n"
        "    if (y == 41) { printf(\"eq\\n\"); }\n"
        "    if (y != 42) { printf(\"ne\\n\"); }\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "eq\nne\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAnyUnboxBlamesOnTypeMismatch) {
    // Unboxing names the held type exactly: an `int`-holding `any` cast to
    // `string` blames at run time instead of reinterpreting the bits.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let y <any> = 41;\n"
        "    let s <string> = cast<string>(y);\n"
        "    printf(\"%s\\n\", s);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_NE(b.runExit, 0) << b.why();
    EXPECT_NE(b.out.find("Fin blames"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAnyUnboxOfTheHeldTypeRuns) {
    // The matching unbox keeps working: int, string, bool and struct round-trip
    // through one `any` and read back their values.
    const Built b = build(std::string(kPrintf) +
        "struct Pt {\n"
        "    x <int>,\n"
        "    y <int>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let yi <any> = 41;\n"
        "    let ys <any> = \"hi\";\n"
        "    let yb <any> = true;\n"
        "    let p <Pt> = Pt { x: 3, y: 4 };\n"
        "    let yp <any> = p;\n"
        "    let q <Pt> = cast<Pt>(yp);\n"
        "    printf(\"%d %s %d %d\\n\", cast<int>(yi), cast<string>(ys),\n"
        "           cast<bool>(yb), q.x + q.y);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "41 hi 1 7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAnyUnboxTellsFunctionSignaturesApart) {
    // Function typeids carry the signature: boxing `fn(int) -> int` and
    // unboxing as `fn(int, int) -> int` blames rather than mis-calling.
    const Built b = build(std::string(kPrintf) +
        "fun target(x: int) <int> { return x + 1; }\n"
        "fun main() <noret> {\n"
        "    let f <any> = target;\n"
        "    let g <fn(int, int) -> int> = cast<fn(int, int) -> int>(f);\n"
        "    printf(\"%d\\n\", g(1, 2));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_NE(b.runExit, 0) << b.why();
    EXPECT_NE(b.out.find("Fin blames"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ACallThroughAnAnyParameterBoxesArgument) {
    const Built b = build(std::string(kPrintf) +
        "struct Holder {\n"
        "    h <fn(any) -> int> = null,\n"
        "    v <int>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let h <Holder> = Holder{ v: 1 };\n"
        "    let f <fn(any) -> int> = h.h;\n"
        "    let x <any> = 5;\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APrototypeWithAnAnyHalfTakesShape) {
    // The halves of a prototype are storage shapes, and storage of `any`
    // needs no boxing rule: only READS and WRITES of elements do, and those
    // refuse downstream (a conversion into `any`, below). This is the
    // parameter shape stdlib/prototypes.fin:10 writes, `prtp: {T, any}`.
    const Built b = build(std::string(kPrintf) +
        "fun first(prtp: {int, any}) <[int]> { return prtp.0; }\n"
        "fun main() <noret> {\n"
        "    let p <{int, any}>;\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStoreIntoAnAnyHalfBoxesValue) {
    const Built b = build(std::string(kPrintf) +
        "fun fill(p: {string, any}) <noret> { p[\"k\"] = 5; }\n"
        "fun main() <noret> {\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASizeofAnyIsRefused) {
    // The blob has 16 bytes, but the shared layout model answers "no layout"
    // for `any` until a lib/std declaration owns the representation -- and a
    // `sizeof` that disagreed with that model would be two passes with two
    // sizes for one type.
    const Built b = build(
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", sizeof(any));\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("the size of 'any'"), std::string::npos)
        << b.why();
}

// ---------------------------------------------------------------------------
// `== null` on a dynamic array or function value compares the buffer.
//
// A dynamic array is a `{ptr, len}` pair and a function value is a code
// pointer: in both cases null is a word the value can hold, so the comparison
// reads that word and nothing else. What it does *not* do is compare contents
// (two arrays) or order pointers (a claim about allocator order): those stay
// refused, as does any non-comparison operator on a pointer.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, ADynamicArrayComparedToNullReadsItsBuffer) {
    // An omitted dynamic-array field starts life zeroed, so its buffer is
    // null: the guard `if (self._arr == null)` in lib/std/collection.fin:96
    // is what this shape serves.
    const Built b = build(std::string(kPrintf) +
        "struct Wrap {\n"
        "    xs <[int]>,\n"
        "    n <int>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let w <Wrap> = Wrap{ n: 3 };\n"
        "    printf(\"%d %d %d\\n\", w.xs == null, w.xs != null, w.n);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 0 3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFunctionValueComparedToNullReadsItsAddress) {
    // `if (self.hasher == null)` in lib/std/hashmap.fin:144, plus the
    // non-null and reversed spellings, which take the same path.
    const Built b = build(std::string(kPrintf) +
        "fun dummy(x: int) <int> { return x + 1; }\n"
        "fun main() <noret> {\n"
        "    let f <fn(int) -> int> = null;\n"
        "    let g <fn(int) -> int> = dummy;\n"
        "    printf(\"%d %d %d %d\\n\", f == null, f != null, g == null, null == g);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 0 0 0\n") << b.why();
}

// ---------------------------------------------------------------------------
// Indexing through `operator []` / `[]=`: a subscript on a struct is a call.
//
// The analyzer already routes a subscript on a struct declaring the operator
// through it (and an assignment target visits the subscript first, so it
// needs the read operator declared too). The backend lowers each form to the
// operator call with the base's address as receiver: the assign form is a
// call, not a store, because there is no address that `a[k]` names.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AnIndexAssignLowersToTheAssignOperator) {
    // deeptest4.fin:13 verbatim in shape: `a["Hi"] = Data{...}`. Here with
    // integers, so the only new machinery is the call form itself.
    const Built b = build(std::string(kPrintf) +
        "struct Map {\n"
        "    v <int>,\n"
        "    operator [](key: <int>) <int> { return self.v; }\n"
        "    operator []=(key: <int>, value: <int>) <noret> { self.v = value; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let m <Map> = Map{ v: 1 };\n"
        "    m[9] = 2;\n"
        "    printf(\"%d\\n\", m.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnIndexReadLowersToTheReadOperator) {
    const Built b = build(std::string(kPrintf) +
        "struct Map {\n"
        "    v <int>,\n"
        "    operator [](key: <int>) <int> { return self.v + key; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let m <Map> = Map{ v: 10 };\n"
        "    printf(\"%d\\n\", m[5]);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "15\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ABareLiteralOfTheEnclosingTemplateUsesThatInstance) {
    // Self-family like `Self()` calls: a bare `Box{...}` inside `Box<T>`'s own
    // method is the enclosing instantiation, with its bindings -- not a use
    // the annotation-inference gap owns, which is about call sites with no
    // enclosing instance to answer from.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    v <T>,\n"
        "    fun dup() <Self> { return Box{ v: self.v }; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ v: 3 };\n"
        "    let c <Box<int>> = b.dup();\n"
        "    printf(\"%d\\n\", c.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ACastFromAPointerReadsItsAddressBits) {
    // What `cast<int>(key)` in lib/std/hashmap.fin:91 means: the address as an
    // integer, truncated to the target width. Asserted as self-equality rather
    // than a value, because addresses vary run to run and only equal pointers
    // must hash equal -- which is also the invariant `==` on strings keeps.
    const Built b = build(std::string(kPrintf) +
        "fun id_hash(s: string) <int> { return cast<int>(s); }\n"
        "fun main() <noret> {\n"
        "    let key <string> = \"abc\";\n"
        "    printf(\"%d %d\\n\", id_hash(key) == id_hash(key), cast<int>(7));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 7\n") << b.why();
}

// ---------------------------------------------------------------------------
// Postfix `?` (denullify): a checked read of a nullable function value.
//
// `x?` reads a nullable as its underlying type, failing if it is absent. A
// nullable `fn` is null when absent, so this emits the null check the
// spelling promises and blames on the failing edge -- the panic the analyzer
// books for the null case. A `?` on a non-nullable value is the analyzer's
// identity and emits nothing extra.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, ADenullifiedPresentFunctionPassesThrough) {
    // A present nullable reads through: the check passes and the value -- a
    // real function address -- calls.
    const Built b = build(std::string(kPrintf) +
        "fun dummy(x: int) <int> { return x * 2; }\n"
        "struct Holder {\n"
        "    h? <fn(int) -> int>,\n"
        "    v <int>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let h <Holder> = Holder{ v: 0 };\n"
        "    h.h = dummy;\n"
        "    let f <fn(int) -> int> = h.h?;\n"
        "    printf(\"%d\\n\", f(21));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADenullifiedAbsentFunctionBlames) {
    // An absent nullable fails loudly rather than calling null: the check
    // trips and the program blames with the read's location, then aborts.
    // (`find`, not equality: the shell may append its own signal report after
    // the program's bytes, and that epilogue is the shell's, not the test's.)
    const Built b = build(std::string(kPrintf) +
        "struct Holder {\n"
        "    h? <fn(int) -> int>,\n"
        "    v <int>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let h <Holder> = Holder{ v: 0 };\n"
        "    let f <fn(int) -> int> = h.h?;\n"
        "    printf(\"%d\\n\", f(1));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_NE(b.runExit, 0) << b.why();
    EXPECT_NE(b.out.find("denullify of an absent value"), std::string::npos)
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADenullifyOfNonNullableIsIdentity) {
    // The analyzer's rule for `?` on a value that was never nullable: the
    // read is that value, with no check emitted. `h` here is a plain field
    // defaulting null (not `h?`), so `h.h?` passes its null straight through.
    const Built b = build(std::string(kPrintf) +
        "fun dummy(x: int) <int> { return x * 2; }\n"
        "struct Holder {\n"
        "    h <fn(int) -> int> = null,\n"
        "    v <int>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let h <Holder> = Holder{ v: 0 };\n"
        "    h.h = dummy;\n"
        "    let f <fn(int) -> int> = h.h?;\n"
        "    printf(\"%d\\n\", f(21));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADenullifiedPresentPointerPassesThrough) {
    // ADR 0040: A present nullable pointer reads through: the check passes and
    // the pointer dereferences normally.
    const Built b = build(std::string(kPrintf) +
        "struct Holder {\n"
        "    p? <&int>,\n"
        "    v <int>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let x <int> = 99;\n"
        "    let h <Holder> = Holder{ v: 0 };\n"
        "    h.p = &x;\n"
        "    let ptr <&int> = h.p?;\n"
        "    printf(\"%d\\n\", *ptr);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "99\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADenullifiedAbsentPointerBlames) {
    // ADR 0040: An absent nullable pointer fails loudly: the check trips and
    // the program blames with "denullify of an absent value".
    const Built b = build(std::string(kPrintf) +
        "struct Holder {\n"
        "    p? <&int>,\n"
        "    v <int>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let h <Holder> = Holder{ v: 0 };\n"
        "    let ptr <&int> = h.p?;\n"
        "    printf(\"%d\\n\", *ptr);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_NE(b.runExit, 0) << b.why();
    EXPECT_NE(b.out.find("denullify of an absent value"), std::string::npos)
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANullableIntFieldAndDenullify) {
    // ADR 0040: A nullable scalar field starts null (0) and denullifies when present.
    const Built b = build(std::string(kPrintf) +
        "struct A {\n"
        "    pub b? <int>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <A> = A{};\n"
        "    printf(\"is_null=%d\\n\", a.b == null);\n"
        "    a.b = 42;\n"
        "    printf(\"val=%d is_null=%d\\n\", a.b?, a.b == null);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "is_null=1\nval=42 is_null=0\n") << b.why();
}

// --- ADR 0040 slices 2-3: tagged pairs, presence = tag ONLY -------------------
//
// Flipped expectation (overrules old zero-sentinel probes p1/p5/p6): a present
// zero is PRESENT (tag=1). `a.b=0` then `a.b==null` is 0 (false) and `a.b?`
// prints 0 and exits 0. Under the old zero-sentinel this read absent (1) and
// blamed.
BACKEND_TEST(Soundness_Codegen, APresentZeroIsNotNull) {
    const Built b = build(std::string(kPrintf) +
        "struct A {\n"
        "    pub b? <int>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <A> = A{};\n"
        "    a.b = 0;\n"
        "    printf(\"null=%d val=%d\\n\", a.b == null, a.b?);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "null=0 val=0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAbsentNullableIntDenullifyBlames) {
    const Built b = build(std::string(kPrintf) +
        "struct A {\n"
        "    pub b? <int>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <A> = A{};\n"
        "    printf(\"%d\\n\", a.b?);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_NE(b.runExit, 0) << b.why();
    EXPECT_NE(b.out.find("denullify of an absent value"), std::string::npos)
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoPresentZeroNullablesAreEqual) {
    // `==` on two nullables reads payload+tag: two present zeroes are equal,
    // absent vs present zero is not.
    const Built b = build(std::string(kPrintf) +
        "struct A {\n"
        "    pub b? <int>,\n"
        "    pub c? <int>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <A> = A{};\n"
        "    a.b = 0;\n"
        "    a.c = 0;\n"
        "    printf(\"%d\\n\", a.b == a.c);\n"
        "    let d <A> = A{};\n"
        "    printf(\"%d %d\\n\", a.b == d.b, a.b != d.b);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1\n0 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFunFalloffReturnsNull) {
    // `fun?` falling off the end returns null ({0,0}); the caller sees null.
    const Built b = build(std::string(kPrintf) +
        "struct A {\n"
        "    pub b <int>,\n"
        "}\n"
        "fun? make_A(n?: int) <A> {\n"
        "    if (n == null) {\n"
        "        return null;\n"
        "    }\n"
        "    if (n > 0) {\n"
        "        return A{ b: 7 };\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let x? <A> = make_A(-1);\n"
        "    printf(\"null=%d\\n\", x == null);\n"
        "    let y? <A> = make_A(1);\n"
        "    printf(\"null=%d val=%d\\n\", y == null, y?.b);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "null=1\nnull=0 val=7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnOmittedNullableParamIsAbsent) {
    // Omitted `n?` fills null; explicit 0 stays present.
    const Built b = build(std::string(kPrintf) +
        "fun is_null(n?: int) <int> {\n"
        "    if (n == null) { return 1; }\n"
        "    return 0;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d\\n\", is_null(), is_null(0));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANullableMixedStructReadsBack) {
    // M{int?,char,long?} 32B layout == emitted offsets: fields round-trip,
    // including a present zero long.
    const Built b = build(std::string(kPrintf) +
        "struct M {\n"
        "    pub a? <int>,\n"
        "    pub b <char>,\n"
        "    pub c? <long>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let m <M> = M{ b: 65 };\n"
        "    printf(\"an=%d bn=%d cn=%d\\n\", m.a == null, m.b == null, m.c == null);\n"
        "    m.a = 0;\n"
        "    m.c = 0;\n"
        "    printf(\"a=%d c=%lld an=%d cn=%d\\n\", m.a?, m.c?, m.a == null, m.c == null);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "an=1 bn=0 cn=1\na=0 c=0 an=0 cn=0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnImportedConcreteStructServesAsABase) {
    // ADR 0032's concrete half: a non-generic struct from a loaded module
    // registers its layout (never its methods or bodies) so a root struct can
    // inherit it -- the base's fields splice in at offset 0 exactly as for a
    // same-file base. Calls into the base's methods still refuse: no body is
    // emitted for them anywhere in this object.
    const fs::path dir = uniqueTempPath("fin_impbase", "");
    std::error_code ec;
    fs::create_directories(dir, ec);
    ASSERT_TRUE(fs::is_directory(dir)) << ec.message();
    {
        std::ofstream f(dir / "baselib.fin", std::ios::binary);
        f << "pub struct Base {\n"
             << "    x <int>,\n"
             << "}\n";
    }
    const fs::path src = uniqueTempPath("fin_impbase_root", ".fin");
    const fs::path exe = uniqueTempPath("fin_impbase_exe");
    {
        std::ofstream f(src, std::ios::binary);
        f << std::string(kPrintf)
          << "import { Base } from baselib;\n"
          << "struct Derived : <Base> {\n"
          << "    y <int>,\n"
          << "}\n"
          << "fun main() <noret> {\n"
          << "    let d <Derived> = Derived{ x: 1, y: 2 };\n"
          << "    printf(\"%d\\n\", d.x + d.y);\n"
          << "}\n";
    }
    const FincRun c =
        runFinc({src.string(), "-o", exe.string(), "-I", dir.string()});
    EXPECT_EQ(c.exitCode, 0) << stripAnsi(c.err);
    std::string out;
    if (c.exitCode == 0 && fs::exists(exe)) {
        const fs::path outPath = uniqueTempPath("fin_impbase_out");
        EXPECT_EQ(fin::runProcess({exe.string()}, outPath.string(), outPath.string()), 0);
        out = readProcessOutput(outPath.string());
        fs::remove(outPath, ec);
    }
    EXPECT_EQ(out, "3\n") << stripAnsi(c.err);
    fs::remove(src, ec);
    fs::remove(exe, ec);
    fs::remove_all(dir, ec);
}

BACKEND_TEST(Soundness_Codegen, AClassAttributeDoesNotChangeLayout) {
    // ADR 0026: a class lowers exactly as a struct. The attribute is accepted
    // and ignored for layout -- which is what lets a `#[class]` base from
    // another module (lib/std/error.fin's `Error`) splice its fields in.
    const Built b = build(std::string(kPrintf) +
        "#[class]\n"
        "struct C {\n"
        "    v <int>\n"
        "}\n"
        "struct D : <C> {\n"
        "    w <int>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let d <D> = D{ v: 1, w: 2 };\n"
        "    printf(\"%d\\n\", d.v + d.w);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3\n") << b.why();
}

// ---------------------------------------------------------------------------
// Meta-types map to opaque words; `resolve_type*` evaluate at compile time.
//
// A `$type` value only ever comes from the `resolve_type`/`resolve_arr_type`
// intrinsics (bodiless lib/std declarations): nothing else in the language
// produces one. Each of the four meta-types maps to its own named word type,
// so they stay distinct without claiming anything about what the word means;
// the intrinsics answer from the argument's static type, which is the only
// thing visible at compile time. The word's content is unobserved -- nothing
// compares, prints, or branches on one -- so numbering is per compilation.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AMetaTypeReturnLowers) {
    // tests/samples/stdlib/prototypes.fin:19-21 verbatim in shape: a function
    // returning `$type`. Nothing calls it here; the declaration lowering is
    // the gap (it refused before any body was read).
    const Built b = build(std::string(kPrintf) +
        "import { resolve_type } from types::std;\n"
        "pub fun typeof_it(v: int) <$type> { return resolve_type(v); }\n"
        "fun main() <noret> {\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AResolveTypeCallLowersToATypeId) {
    // The value is unobservable -- nothing compares or prints a `$type` --
    // so this pins that the call lowers rather than what number it yields.
    const std::string prog = std::string(kPrintf) +
        "import { resolve_type } from types::std;\n"
        "fun main() <noret> {\n"
        "    let t <$type> = resolve_type(5);\n"
        "    printf(\"ok\\n\");\n"
        "}\n";
    const Built first = build(prog);
    ASSERT_EQ(first.compileExit, 0) << first.why();
    ASSERT_TRUE(first.ran) << first.why();
    EXPECT_EQ(first.out, "ok\n") << first.why();
}

BACKEND_TEST(Soundness_Codegen, AllFourMetaTypesMapDistinctly) {
    // `$type`, `$struct`, `$interface` and `$enum_member` are four types, not
    // one (CONTEXT.md): a `$struct` parameter must not accept a `$type`
    // argument, in either direction, so each maps to its own word.
    const Built b = build(
        "fun a(t: $type) <$type> { return t; }\n"
        "fun b(t: $struct) <$struct> { return t; }\n"
        "fun c(t: $interface) <$interface> { return t; }\n"
        "fun d(t: $enum_member) <$enum_member> { return t; }\n"
        "fun main() <noret> {}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASizeofMetaTypeIsRefused) {
    // The word has 8 bytes, but the shared layout model answers "no layout"
    // for a meta-type until a declaration owns the representation -- and a
    // `sizeof` that disagreed with that model would be two passes with two
    // sizes for one type. Same rule as `sizeof(any)`.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", sizeof($type));\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("the size of '$type'"), std::string::npos)
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnExportOnAnAliasIsVacuous) {
    // stdlib/prototypes.fin:6 writes `#[export]` above `type listarray`. An
    // alias emits nothing -- no symbol, no storage -- so there is nothing to
    // rename or relink, and import-visibility (what `export` means, ADR 0033)
    // was already honoured by the analyzer resolving the import. Accepting it
    // claims nothing further. Any other attribute still refuses: `#[llvm_name]`
    // on an alias would be a naming request silently dropped.
    const Built b = build(std::string(kPrintf) +
        "#[export]\n"
        "type listarray = {any, any};\n"
        "fun main() <noret> {\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

// ---------------------------------------------------------------------------
// `#[uncastable]` excludes casts; `#[stderror]` marks the standard error.
//
// `uncastable` is a rule about the type, not about a single spelling: no
// cast to or from the type lowers, including same-type and
// generic/dynamic-mediated ones the analyzer otherwise admits (which is why
// the check lives on the cast expression rather than in conversions -- an
// implicit copy is not a cast). `stderror` marks the standard error class;
// it asks nothing of emission (like `export` on an alias), and is accepted
// as documented.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, ACastOfAnUncastableTypeIsRefused) {
    // Even same-type: `cast<Secret>(s)` is written as a cast, and the
    // attribute excludes casts, not just converting ones.
    const Built b = build(std::string(kPrintf) +
        "#[uncastable]\n"
        "struct Secret {\n"
        "    v <int>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <Secret> = Secret{ v: 1 };\n"
        "    let t <Secret> = cast<Secret>(s);\n"
        "    printf(\"%d\\n\", t.v);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a cast from uncastable 'Secret'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ACastIntoAnUncastableTypeIsRefused) {
    // Dynamic-mediated: the analyzer admits `cast<Secret>(a)` for `a: any`
    // (a cast overrides the checker), so the backend is what keeps the
    // attribute's promise. In a never-called body -- bodies lower eagerly,
    // so reachability is not the question.
    const Built b = build(
        "#[uncastable]\n"
        "struct Secret {\n"
        "    v <int>\n"
        "}\n"
        "fun f(a: any) <Secret> { return cast<Secret>(a); }\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a cast to uncastable 'Secret'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnUncastableStructStillLowers) {
    // The attribute excludes casts, not the struct: declaration, fields,
    // copies and reads all lower as for any struct.
    const Built b = build(std::string(kPrintf) +
        "#[uncastable]\n"
        "struct Secret {\n"
        "    v <int>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <Secret> = Secret{ v: 7 };\n"
        "    let t <Secret> = s;\n"
        "    printf(\"%d\\n\", t.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStderrorMarkIsAccepted) {
    const Built b = build(std::string(kPrintf) +
        "#[stderror]\n"
        "struct E {\n"
        "    v <int>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let e <E> = E{ v: 4 };\n"
        "    printf(\"%d\\n\", e.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASpecialDeclarationEmitsNothing) {
    // A `@special` runs at compile time; its declaration has no runtime
    // meaning, so emitting nothing for it is lowering it completely -- the
    // same state an interface declaration is in, and not the skip the
    // refusal rule forbids.
    const Built b = build(std::string(kPrintf) +
        "@special(pub) const_true() <bool> { return true; }\n"
        "fun main() <noret> {\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AScalarComparedToNullReadsZero) {
    // `err_code == null` in stdlib/error.fin:12: null converts to zero for
    // every scalar kind (the null-default rule), so the comparison reads
    // against zero rather than refusing. Only `==` and `!=`: ordering
    // against null is a claim about where nothing sits.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <int> = 5;\n"
        "    let z <int> = 0;\n"
        "    printf(\"%d %d %d\\n\", x == null, x != null, z == null);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0 1 1\n") << b.why();
}

// ---------------------------------------------------------------------------
// `delete` runs the destructor first, then frees.
//
// deeptest3.fin:44 documents it: "(Calls destructor if defined, then frees
// memory)". A struct with a destructor is no longer refused at its
// declaration, so a `delete` that skipped the call would be storage freed
// under a live cleanup rule -- the silent skip the refusal rule forbids.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, ADeleteRunsTheDestructorFirst) {
    const Built b = build(std::string(kPrintf) +
        "struct Track {\n"
        "    v <int>,\n"
        "    ~Track() { printf(\"dtor %d\\n\", self.v); }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <&Track> = new Track{ v: 3 };\n"
        "    delete p;\n"
        "    printf(\"after\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "dtor 3\nafter\n") << b.why();
}

// ---------------------------------------------------------------------------
// Composed destruction: a destructor call cleans fields and bases after it.
//
// ADR 0016: a field whose type has a destructor is cleaned from its parent's
// destructor, after the declared body (C++/D order); fields go in reverse
// declaration order and effective bases last. A parent that declares none
// still cleans its fields -- there is simply no body before them.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, ADeleteCleansFieldsInReverseAfterTheBody) {
    const Built b = build(std::string(kPrintf) +
        "struct Part {\n"
        "    id <int>,\n"
        "    ~Part() { printf(\"part %d\\n\", self.id); }\n"
        "}\n"
        "struct Mach {\n"
        "    first <Part>,\n"
        "    second <Part>,\n"
        "    ~Mach() { printf(\"mach\\n\"); }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let m <&Mach> = new Mach{ first: Part{ id: 1 }, second: Part{ id: 2 } };\n"
        "    delete m;\n"
        "    printf(\"done\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "mach\npart 2\npart 1\ndone\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADeleteCleansFieldsWithNoDeclaredBody) {
    const Built b = build(std::string(kPrintf) +
        "struct Inner {\n"
        "    v <int>,\n"
        "    ~Inner() { printf(\"inner %d\\n\", self.v); }\n"
        "}\n"
        "struct Wrap {\n"
        "    inn <Inner>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let w <&Wrap> = new Wrap{ inn: Inner{ v: 9 } };\n"
        "    delete w;\n"
        "    printf(\"done\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "inner 9\ndone\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADeleteCleansTheBaseAfterTheBody) {
    const Built b = build(std::string(kPrintf) +
        "struct Base {\n"
        "    v <int>,\n"
        "    ~Base() { printf(\"base\\n\"); }\n"
        "}\n"
        "struct Der : <Base> {\n"
        "    w <int>,\n"
        "    ~Der() { printf(\"der\\n\"); }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let d <&Der> = new Der{ v: 1, w: 2 };\n"
        "    delete d;\n"
        "    printf(\"done\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "der\nbase\ndone\n") << b.why();
}

// ---------------------------------------------------------------------------
// Scope-exit destruction: locals with destructors clean up when control
// leaves their scope (ADR 0030) -- at `return`, at block end, and on `break`
// and `continue` -- in reverse declaration order. There are no moves in the
// language (every binding copies), so every value destroys independently,
// exactly as C++ value semantics without move constructors.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, ALocalsCleanUpAtReturnInReverseOrder) {
    const Built b = build(std::string(kPrintf) +
        "struct Log {\n"
        "    id <int>,\n"
        "    ~Log() { printf(\"bye %d\\n\", self.id); }\n"
        "}\n"
        "fun work() <int> {\n"
        "    let a <Log> = Log{ id: 1 };\n"
        "    let b <Log> = Log{ id: 2 };\n"
        "    printf(\"body\\n\");\n"
        "    return 7;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", work());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "body\nbye 2\nbye 1\n7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ABlockLocalCleansUpAtBlockEnd) {
    const Built b = build(std::string(kPrintf) +
        "struct Log {\n"
        "    id <int>,\n"
        "    ~Log() { printf(\"bye %d\\n\", self.id); }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"a\\n\");\n"
        "    {\n"
        "        let t <Log> = Log{ id: 3 };\n"
        "        printf(\"b\\n\");\n"
        "    }\n"
        "    printf(\"c\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "a\nb\nbye 3\nc\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ALoopBodyCleansUpPerIterationAndOnBreak) {
    const Built b = build(std::string(kPrintf) +
        "struct Log {\n"
        "    id <int>,\n"
        "    ~Log() { printf(\"bye %d\\n\", self.id); }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    for (let i <int> = 0; i < 3; i++) {\n"
        "        let t <Log> = Log{ id: i };\n"
        "        if (i == 1) { break; }\n"
        "        printf(\"iter %d\\n\", i);\n"
        "    }\n"
        "    printf(\"end\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "iter 0\nbye 0\nbye 1\nend\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AParameterCleansUpAtReturn) {
    // Copies destroy independently (there are no moves): the parameter and
    // the caller's variable clean separately, each exactly once.
    const Built b = build(std::string(kPrintf) +
        "struct Log {\n"
        "    id <int>,\n"
        "    ~Log() { printf(\"bye %d\\n\", self.id); }\n"
        "}\n"
        "fun take(o: Log) <int> { return o.id; }\n"
        "fun main() <noret> {\n"
        "    let v <Log> = Log{ id: 9 };\n"
        "    printf(\"%d\\n\", take(v));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "bye 9\n9\nbye 9\n") << b.why();
}

// ---------------------------------------------------------------------------
// Moved-from bindings skip scope-exit destruction, but only with a
// `move_or_copy` claimant (wave-5 slice 1, ADR 0030's moved-from case).
// Without one every value still destroys independently -- the double destroy
// below is the legacy by-design output, pinned byte-identical. With one, a
// moved binding (Yes) and a conditionally-moved one (Maybe) both skip: leak
// beats corruption.
// ---------------------------------------------------------------------------

namespace {

// The well-formed claimant every moved-cleanup test below shares: an empty
// body (a replacement body is refused, never silently dropped) and the
// one-subject/quote contract slice 0 pins.
const char* const kMoveOrCopyClaimant =
    "#[protocol(move_or_copy)]\n"
    "@special mymove(s: $struct) <quote> {}\n";

const char* const kTrackStruct =
    "struct Track {\n"
    "    v <int>,\n"
    "    ~Track() { printf(\"dtor %d\\n\", self.v); }\n"
    "}\n";

}  // namespace

BACKEND_TEST(Soundness_Codegen, AMoveThenExitDestroysOnceWithClaimant) {
    const Built b = build(std::string(kPrintf) + kMoveOrCopyClaimant + kTrackStruct +
        "fun main() <noret> {\n"
        "    let a <Track> = Track{ v: 1 };\n"
        "    let b <Track> = @move(a);\n"
        "    printf(\"body\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    // `b` destroys at fallthrough; `a` was moved and skips. Exactly one.
    EXPECT_EQ(b.out, "body\ndtor 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMoveThenExitDestroysTwiceWithoutClaimant) {
    // ADR 0030 preserved by default: no claimant, no skip. The double destroy
    // is the legacy by-design output and must read byte-identical after the
    // slice (proved by IR diff on top of this run).
    const Built b = build(std::string(kPrintf) + kTrackStruct +
        "fun main() <noret> {\n"
        "    let a <Track> = Track{ v: 1 };\n"
        "    let b <Track> = @move(a);\n"
        "    printf(\"body\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "body\ndtor 1\ndtor 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ACopyThenExitDestroysEachWithClaimant) {
    // The claimant gates skipping, but nothing was moved: each binding
    // destroys, exactly as without one.
    const Built b = build(std::string(kPrintf) + kMoveOrCopyClaimant + kTrackStruct +
        "fun main() <noret> {\n"
        "    let a <Track> = Track{ v: 1 };\n"
        "    let b <Track> = a;\n"
        "    printf(\"body\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "body\ndtor 1\ndtor 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ABranchConditionalMoveSkipsWithoutDoubleFreeWithClaimant) {
    // The move runs on one path only, so `a` is Maybe past the join -- and
    // Maybe skips. The moved path destroys through `b`; the live path leaks
    // by owner decision (leak beats corruption). Either way no double-free:
    // exactly one destructor run across both calls.
    const Built b = build(std::string(kPrintf) + kMoveOrCopyClaimant + kTrackStruct +
        "fun pick(c: bool) <noret> {\n"
        "    let a <Track> = Track{ v: 1 };\n"
        "    if (c) {\n"
        "        let b <Track> = @move(a);\n"
        "        printf(\"moved\\n\");\n"
        "    } else {\n"
        "        printf(\"kept\\n\");\n"
        "    }\n"
        "    printf(\"after\\n\");\n"
        "}\n"
        "fun main() <noret> {\n"
        "    pick(true);\n"
        "    pick(false);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "moved\ndtor 1\nafter\nkept\nafter\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMoveThenRebindDestroysTheNewValueWithClaimant) {
    // Rebinding revives: the assignment makes `a` live again on this path,
    // so its new value destroys while the moved-from value does not.
    const Built b = build(std::string(kPrintf) + kMoveOrCopyClaimant + kTrackStruct +
        "fun main() <noret> {\n"
        "    let a <Track> = Track{ v: 1 };\n"
        "    let b <Track> = @move(a);\n"
        "    a = Track{ v: 2 };\n"
        "    printf(\"body\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    // Reverse declaration order: `b` (v 1) first, then the rebound `a` (v 2).
    EXPECT_EQ(b.out, "body\ndtor 1\ndtor 2\n") << b.why();
}

// ---------------------------------------------------------------------------
// The `destructor` claimant supplies generation (wave-5 slice 2, ADR 0014).
//
// Lookup order in emitDestructorCall: an explicit `~T()` first (it keeps
// precedence -- deeptest2.fin:46-52), then the claimant's generation, then
// ADR 0016's generated composition. Composition is preserving by default:
// the claimant occupies the declared-body slot and fields-then-bases still
// run after it, because a claimant that silently dropped field cleanup would
// leak exactly the way ADR 0016's rule exists to prevent. The claim shape
// (`#[protocol(destructor)]`, one `$struct` in, one `quote` out) carries no
// full-vs-partial channel, so there is no opt-out yet. The generation is
// uniform across claimed types (parameters do not bind -- the interpreter
// gap), so its quote names no `self`; every struct without a declared
// destructor in a claiming program gets it. Without a claimant nothing here
// reads anything new and every program lowers exactly as before.
// ---------------------------------------------------------------------------

namespace {

// The well-formed claimant every destructor-generation test below shares:
// one `return quote { ... };` printing a fixed marker (no `self`: the
// generation is uniform across subjects, slice 2's held line).
const char* const kDestructorClaimant =
    "#[protocol(destructor)]\n"
    "@special mydtor(s: $struct) <quote> { return quote { printf(\"claimant\\n\"); }; }\n";

}  // namespace

BACKEND_TEST(Soundness_Codegen, AClaimedStructWithNoDestructorRunsClaimantAtScopeExit) {
    const Built b = build(std::string(kPrintf) + kDestructorClaimant +
        "struct Plain {\n"
        "    v <int>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Plain> = Plain{ v: 1 };\n"
        "    printf(\"body\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "body\nclaimant\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnExplicitDestructorWinsOverTheClaimant) {
    // deeptest2.fin:46's rule ("you can overwrite it"): the declared body
    // runs and the claimant is not consulted for this type.
    const Built b = build(std::string(kPrintf) + kDestructorClaimant + kTrackStruct +
        "fun main() <noret> {\n"
        "    let a <Track> = Track{ v: 1 };\n"
        "    printf(\"body\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "body\ndtor 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AClaimantRunsBeforeFieldsInReverseOrder) {
    // Composition-preserving default: the claimant takes the declared-body
    // slot and ADR 0016's fields-then-bases still run after it, fields in
    // reverse declaration order.
    const Built b = build(std::string(kPrintf) + kDestructorClaimant +
        "struct Part {\n"
        "    id <int>,\n"
        "    ~Part() { printf(\"part %d\\n\", self.id); }\n"
        "}\n"
        "struct Mach {\n"
        "    first <Part>,\n"
        "    second <Part>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let m <Mach> = Mach{ first: Part{ id: 1 }, second: Part{ id: 2 } };\n"
        "    printf(\"body\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "body\nclaimant\npart 2\npart 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADeleteRunsTheClaimantBeforeFreeing) {
    // The delete path dispatches through the same emitDestructorCall, so a
    // claimed pointee type cleans through the claimant before storage goes.
    const Built b = build(std::string(kPrintf) + kDestructorClaimant +
        "struct Plain {\n"
        "    v <int>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <&Plain> = new Plain{ v: 3 };\n"
        "    delete p;\n"
        "    printf(\"after\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "claimant\nafter\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEmptyDestructorClaimantChangesNothing) {
    // An empty generation is a recorded claim with no custom cleanup:
    // composition still runs, and a type with nothing to compose cleans
    // nothing -- exactly as without the claim.
    const Built b = build(std::string(kPrintf) +
        "#[protocol(destructor)]\n"
        "@special mydtor(s: $struct) <quote> {}\n"
        "struct Plain {\n"
        "    v <int>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Plain> = Plain{ v: 1 };\n"
        "    printf(\"body\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "body\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AThreadedDestructorClaimantLowersAndRuns) {
    // Wave follow-up: the analyzer threads lets/bare-calls with the subject
    // param bound and the final return must be a quote; codegen mirrors by
    // evaluating the same straight-line shapes and splicing the final quote
    // as the generation body. Scope exit runs it.
    const Built b = build(std::string(kPrintf) +
        "fun id(v: int) <int> { return v; }\n"
        "@special mkq() <quote> { return quote { printf(\"threaded\\n\"); }; }\n"
        "#[protocol(destructor)]\n"
        "@special mydtor(s: $struct) <quote> {\n"
        "    let x <int> = id(7);\n"
        "    let t <$struct> = s;\n"
        "    mkq();\n"
        "    let q <quote> = mkq();\n"
        "    return q;\n"
        "}\n"
        "struct Plain {\n"
        "    v <int>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Plain> = Plain{ v: 1 };\n"
        "    printf(\"body\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "body\nthreaded\n") << b.why();
}

// ---------------------------------------------------------------------------
// The `deallocate` claimant substitutes the deallocation call (wave-5 slice 3,
// ADR 0014).
//
// The claimant replaces ONLY the `free`: the destructor above still runs
// first, so deeptest3.fin:44's order ("Calls destructor if defined, then
// frees memory") holds by construction and slices 1/2 stay orthogonal. `new`
// (the `malloc` site) is untouched. An empty claim records the slot and
// supplies no generation, so the default `free` stays -- exactly as without
// the claim. Without a claimant nothing here reads anything new.
// ---------------------------------------------------------------------------

namespace {

// The well-formed claimant every deallocate test below shares: one
// `return quote { ... };` printing a fixed marker (no `self`: the
// generation is uniform across subjects, slice 2's held line carried over).
const char* const kDeallocateClaimant =
    "#[protocol(deallocate)]\n"
    "@special myfree(s: $struct) <quote> { return quote { printf(\"claimant-free\\n\"); }; }\n";

}  // namespace

BACKEND_TEST(Soundness_Codegen, ADeallocateClaimantReplacesTheFree) {
    const Built b = build(std::string(kPrintf) + kDeallocateClaimant +
        "struct Plain {\n"
        "    v <int>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <&Plain> = new Plain{ v: 3 };\n"
        "    delete p;\n"
        "    printf(\"after\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "claimant-free\nafter\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADeleteRunsDestructorBeforeDeallocateClaimant) {
    // deeptest3.fin:44's order, with the claimant in the `free` slot: the
    // declared body runs first, then the claimant's marker.
    const Built b = build(std::string(kPrintf) + kDeallocateClaimant +
        "struct Track {\n"
        "    v <int>,\n"
        "    ~Track() { printf(\"dtor %d\\n\", self.v); }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <&Track> = new Track{ v: 3 };\n"
        "    delete p;\n"
        "    printf(\"after\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "dtor 3\nclaimant-free\nafter\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEmptyDeallocateClaimantChangesNothing) {
    // An empty generation is a recorded claim with no custom deallocation:
    // the default `free` stays, exactly as without the claim.
    const Built b = build(std::string(kPrintf) +
        "#[protocol(deallocate)]\n"
        "@special myfree(s: $struct) <quote> {}\n"
        "struct Plain {\n"
        "    v <int>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <&Plain> = new Plain{ v: 3 };\n"
        "    delete p;\n"
        "    printf(\"after\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "after\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AThreadedDeallocateClaimantLowersAndRuns) {
    // Wave follow-up, deallocate half: same straight-line threading as the
    // destructor claimant, spliced as the `free` replacement. The destructor
    // still runs first by construction; here there is none, so only the
    // threaded marker prints before `after`.
    const Built b = build(std::string(kPrintf) +
        "fun id(v: int) <int> { return v; }\n"
        "@special mkq() <quote> { return quote { printf(\"threaded-free\\n\"); }; }\n"
        "#[protocol(deallocate)]\n"
        "@special myfree(s: $struct) <quote> {\n"
        "    let x <int> = id(7);\n"
        "    let t <$struct> = s;\n"
        "    mkq();\n"
        "    let q <quote> = mkq();\n"
        "    return q;\n"
        "}\n"
        "struct Plain {\n"
        "    v <int>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <&Plain> = new Plain{ v: 3 };\n"
        "    delete p;\n"
        "    printf(\"after\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "threaded-free\nafter\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADoubleDeleteStillAbortsWithoutClaimant) {
    // No claimant, so both deletes lower to `free`: the second frees what the
    // first already freed, and glibc aborts. Pinned because the claimant
    // replaces the `free` -- the default path must keep doing what it does
    // today.
    const Built b = build(std::string(kPrintf) +
        "struct P { pub a <int> }\n"
        "fun main() <noret> {\n"
        "    let p <&P> = new P{a: 1};\n"
        "    delete p;\n"
        "    delete p;\n"
        "    printf(\"done\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_NE(b.runExit, 0) << b.why();
    EXPECT_NE(b.out.find("double free"), std::string::npos) << b.why();
}

// ---------------------------------------------------------------------------
// `cast<[char]>` on a string reads its bytes with a measured length.
//
// A `string` is NUL-terminated bytes by the convention everything that
// prints one already relies on, so the length is there to be measured
// (strlen) rather than invented. Narrow on both sides: bytes are chars, and
// a fixed extent has no static length to give -- `cast<[byte]>` and
// `cast<[char, 5]>` stay refused, as does a cast from any other pointer,
// whose target need not terminate.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, ACastFromAStringToACharArrayMeasuresIt) {
    // stdlib/stdio.fin:156 writes exactly this: `cast<[char]>("SomeData...")`
    // as an `Ok` payload.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let s <[char]> = cast<[char]>(\"hi\");\n"
        "    printf(\"%d %c %c\\n\", s.length, s[0], s[1]);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2 h i\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASelfFieldLowersToThisInstantiation) {
    // A field of `&Self` is this struct (struct_methods.fin:21 writes `<&Point>`
    // where `<&Self>` would do): bound during member mapping for templates and
    // plain structs alike, so the declaration lowers.
    const Built b = build(std::string(kPrintf) +
        "struct Node {\n"
        "    v <int>,\n"
        "    next <&Self>\n"
        "}\n"
        "struct Box<T> {\n"
        "    v <T>,\n"
        "    me <&Self>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let n <Node> = Node{ v: 1, next: null };\n"
        "    let b <Box<int>> = Box::<int>{ v: 3, me: null };\n"
        "    printf(\"%d %d\\n\", n.v, b.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 3\n") << b.why();
}

// ---------------------------------------------------------------------------
// A reference reads as its pointee where a value is expected (rvalues deref,
// lvalues do not): call arguments, comparisons, returns and plain reads all
// load through a single-level `&T`. Never through a nullable (narrow those
// first) and never twice (`&&T` keeps its explicit `*`).
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AReferenceArgumentDereferences) {
    const Built b = build(std::string(kPrintf) +
        "fun take(x: int) <int> { return x + 1; }\n"
        "fun main() <noret> {\n"
        "    let v <int> = 5;\n"
        "    printf(\"%d\\n\", take(&v));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "6\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AReferenceOperandDereferencesInComparison) {
    // const.fin:84 verbatim in shape: `ta2.value == 5` where the member reads
    // as `&int`.
    const Built b = build(std::string(kPrintf) +
        "fun same(a: &int, b: int) <bool> { return a == b; }\n"
        "fun main() <noret> {\n"
        "    let v <int> = 5;\n"
        "    if (same(&v, 5)) { printf(\"yes\\n\"); } else { printf(\"no\\n\"); }\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "yes\n") << b.why();
}

// ---------------------------------------------------------------------------
// Array equality: lengths, then elements.
//
// Two arrays compare by length first (no out-of-bounds read either way) and
// then element-wise through `==` itself, so nesting, `any` and struct
// elements all answer by the rules they already have. Only `==`/`!=`: ordering
// an array is unruled.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AFixedArrayComparesElementWise) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 3]> = [1, 2, 3];\n"
        "    let b <[int, 3]> = [1, 2, 3];\n"
        "    let c <[int, 3]> = [1, 2, 4];\n"
        "    printf(\"%d %d\\n\", a == b, a == c);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADynamicArrayComparesLengthThenElements) {
    // const.fin:102 verbatim in shape (there against a fixed literal): a
    // length mismatch decides without reading any element.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int]> = [1, 2, 3];\n"
        "    let b <[int]> = [1, 2];\n"
        "    let c <[int]> = [1, 2, 3];\n"
        "    printf(\"%d %d\\n\", a == b, a == c);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnElidedConstructorInstantiatesFromWhatInferenceFound) {
    // const.fin:80 in miniature: `rptr(5)` with `rptr<int>` on the left. The
    // analyzer infers T (from the annotation first, then the arguments --
    // b690f60) and records the spelling on the call; the backend instantiates
    // what was recorded, exactly as a written turbofish instantiates.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    constructor(v: T) { self.val = v; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box(5);\n"
        "    printf(\"%d\\n\", b.val);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnUninferrableConstructorStillNamesTheTemplate) {
    // Nothing says what T is -- no turbofish, no annotation seed (`auto`
    // takes none), no argument -- so there is nothing to record and the
    // refusal names the template rather than inventing an instantiation.
    const Built b = build(std::string(kPrintf) +
        "struct Wrap<T> {\n"
        "    val <T>,\n"
        "    constructor() {}\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let w <auto> = Wrap();\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("with no type arguments"), std::string::npos)
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnElidedLiteralInstantiatesFromTheEnclosingReturn) {
    // stdptr.fin's `weak()` in miniature: `wptr{...}` inside `rptr<T>` names
    // the bare template but means `wptr<T>`, and the declaration that says so
    // is the enclosing return type. The analyzer records the spelling; the
    // backend instantiates what was recorded.
    const Built b = build(std::string(kPrintf) +
        "struct Inner<T> {\n"
        "    v <T>,\n"
        "}\n"
        "struct Outer<T> {\n"
        "    x <T>,\n"
        "    fun make(self: &Self) <Inner<T>> {\n"
        "        return Inner{ v: self.x };\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let o <Outer<int>> = Outer{ x: 5 };\n"
        "    let i <Inner<int>> = o.make();\n"
        "    printf(\"%d\\n\", i.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnElidedLiteralInstantiatesFromTheAnnotation) {
    // The same elision at namespace scope, seeded by the declaration: the
    // annotation names the instantiation and the literal carries no turbofish
    // of its own.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box{ val: 7 };\n"
        "    printf(\"%d\\n\", b.val);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnElidedLiteralWithNoSourceStillNamesTheStruct) {
    // No turbofish, no declaration naming the instantiation (`auto` takes no
    // seed), and no field mentioning the parameter: inference records nothing
    // and the refusal names the struct rather than inventing an
    // instantiation. (A field that does mention it infers -- `Box{ val: 7 }`
    // under `auto` is `Box<int>` -- by the same rule a call's arguments do.)
    const Built b = build(std::string(kPrintf) +
        "struct Empty<T> {\n"
        "    tag <int>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let e <auto> = Empty{ tag: 1 };\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a literal of struct 'Empty'"), std::string::npos)
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADynamicArrayComparesAgainstABareLiteral) {
    // const.fin:102 in miniature: the literal carries no declaration, so the
    // backend cannot ask the hint what it is -- but the analyzer already
    // typed it (`[int, 4]`, fixed) and records the spelling. Lengths decide
    // without reading out of bounds either way.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int]> = [1, 2, 3];\n"
        "    printf(\"%d %d\\n\", a == [1, 2, 3], a == [1, 2]);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoBareLiteralsCompareByLengthThenElements) {
    // Neither side has a declaration; each carries what inference found.
    // Same extent compares element-wise, so equal lengths with a differing
    // element are unequal rather than unlowered.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d %d\\n\", [1, 2] == [1, 2], [1, 2] == [1, 3]);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEmptyBareLiteralStillHasNoType) {
    // `[]` states no elements, and `==` offers no hint, so there is nothing
    // to record and the refusal stays where inference leaves it: the front
    // end, which is the pass that knows no annotation was written.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int]> = [1, 2, 3];\n"
        "    printf(\"%d\\n\", a == []);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("Empty array literal cannot infer type"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnImportedGenericStructInstantiates) {
    // ADR 0032: the module loader keeps every loaded Program and the backend
    // registers templates out of them, so `Box::<int>` below instantiates the
    // `Box<T>` the import published -- the same instantiation a same-file
    // template gets (complex.fin:12). Emission stays root-only: the module's
    // own declarations never become symbols of this object.
    const fs::path dir = uniqueTempPath("fin_impmod", "");
    std::error_code ec;
    fs::create_directories(dir, ec);
    ASSERT_TRUE(fs::is_directory(dir)) << ec.message();
    {
        std::ofstream f(dir / "boxlib.fin", std::ios::binary);
        f << "pub struct Box<T> {\n"
             << "    val <T>,\n"
             << "}\n";
    }
    const fs::path src = uniqueTempPath("fin_impmod_root", ".fin");
    const fs::path exe = uniqueTempPath("fin_impmod_exe");
    {
        std::ofstream f(src, std::ios::binary);
        f << std::string(kPrintf)
          << "import { Box } from boxlib;\n"
          << "fun main() <noret> {\n"
          << "    let b <auto> = Box::<int>{ val: 3 };\n"
          << "    printf(\"%d\\n\", b.val);\n"
          << "}\n";
    }
    const FincRun c =
        runFinc({src.string(), "-o", exe.string(), "-I", dir.string()});
    EXPECT_EQ(c.exitCode, 0) << stripAnsi(c.err);
    std::string out;
    if (c.exitCode == 0 && fs::exists(exe)) {
        const fs::path outPath = uniqueTempPath("fin_impmod_out");
        EXPECT_EQ(fin::runProcess({exe.string()}, outPath.string(), outPath.string()), 0);
        out = readProcessOutput(outPath.string());
        fs::remove(outPath, ec);
    }
    EXPECT_EQ(out, "3\n") << stripAnsi(c.err);
    fs::remove(src, ec);
    fs::remove(exe, ec);
    fs::remove_all(dir, ec);
}

BACKEND_TEST(KnownDefect_Codegen, ConstructorOverloadsAreRefusedRatherThanResolved) {
    // The booked defect (docs/HANDOFF.md §7): the analyzer resolves `constructors[0]`
    // and no more. One symbol per struct is what this file declares to match it, so a
    // second `constructor` is refused *by name* at its declaration rather than silently
    // losing to the first -- a call that reached the wrong body would be a program that
    // quietly computes something else. The day overload resolution lands, this test is
    // the one that says so.
    const Built b = build(std::string(kPrintf) +
        "struct Two {\n"
        "    a <int>,\n"
        "    constructor(n: int) { self.a = n; }\n"
        "    constructor(n: int, m: int) { self.a = n + m; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let t <Two> = Two(1);\n"
        "    printf(\"%d\\n\", t.a);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("constructor overloads on struct 'Two'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructWithNoFunctionsOfItsOwnStillLowers) {
    // The boundary, from the other side. Refusing a declared function must not turn
    // into refusing every struct: structs.fin's Vector3 has three fields and nothing
    // else, and it is what most of this suite is built on. (Its fields are floats
    // there; ints here, because `cast<int>` of a float is a separate gap.)
    const Built b = build(std::string(kPrintf) +
        "struct Vector3 { x <int>, y <int>, z <int> }\n"
        "fun main() <noret> {\n"
        "    let v <Vector3> = Vector3{ x: 1, y: 2, z: 4 };\n"
        "    printf(\"%d\\n\", v.x + v.y + v.z);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericStructsMethodNobodyInstantiatesEmitsNothing) {
    // Was AGenericStructsMethodIsRefusedAtItsTemplate. A template's method is not a
    // skipped function, it is a function that does not exist yet: `fun get(self:
    // &Box<T>) <T>` has no LLVM signature until something says what T is, exactly as
    // a generic free function has none. So nothing is emitted and nothing is
    // refused -- which is also what makes struct_methods.fin, a whole sample of
    // methods on an uninstantiated `Point<T>`, compile.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun get(self: &Box<T>) <T> { return self.val; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
    // And specifically nothing, rather than one instance under the template's name.
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun get(self: &Box<T>) <T> { return self.val; }\n"
        "}\n");
    EXPECT_EQ(trace.find("declared Box"), std::string::npos) << trace;
}


// ---------------------------------------------------------------------------
// A constructor call on a generic struct: `HashMap::<string, Data>()`
//
// deeptest4.fin:11 is the corpus site and it was the whole of this file's turbofish
// refusal: `visit(FunctionCall&)` looked a called name up in `fnTemplates_` and never
// in `templates_`, so a *struct* template with its arguments written reached the blanket
// "a call with explicit generic arguments" and stopped there. Nothing else was missing.
// `instantiateGeneric` already maps the arguments, lays the instance out and declares
// its methods including its constructor, and the non-generic constructor path already
// allocates the object, zeroes it, passes its address as parameter 0 and loads the
// result back -- the two had simply never been introduced.
//
// The instantiation goes through `literalStructName`, which is the synthetic-TypeNode
// probe `Box::<int>{ val: 100 }` (complex.fin:12) already used. That is the point of
// these tests taken together: a call, a literal and a `let b <Box<int>>` annotation must
// reach *one* instance, because two instances of one layout are two LLVM types that are
// not assignable to each other, and the program that catches it is the one that assigns
// across the spellings rather than the one that merely compiles each.
//
// `deeptest4.fin` does not move on this unit, and that is measured rather than assumed:
// its `HashMap` is imported, a module's AST does not reach this pass (HANDOFF's
// imported-declaration gap), so the sample now refuses `a call to 'HashMap'` -- the same
// thing an imported *function* already says. The last test here is that refusal.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AGenericStructsConstructorIsCalledAtItsTypeArguments) {
    // The value, not the compile: a constructor's result is the caller's storage read
    // back out (parameter 0), so a path that allocated the wrong instance or forgot the
    // load would still exit 0 and print whatever the frame held.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T> = null,\n"
        "    Box(v: T) { return new Box{val: v}; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <auto> = Box::<int>(7);\n"
        "    let c <auto> = Box::<char>('z');\n"
        "    printf(\"%d %c\\n\", b.val, c.val);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    // Two instantiations, so a table that collided would read `c.val` at `int`'s width.
    EXPECT_EQ(b.out, "7 z\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericConstructorCallReachesTheSameInstanceAsALiteral) {
    // The assignment is the assertion. `fromLit = fromCall` type-checks in the analyzer
    // whatever this pass does; it *lowers* only if both spellings named one
    // llvm::StructType, and the parameter of `take(b: Box<int>)` is a third spelling
    // that has to agree with them.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T> = null,\n"
        "    Box(v: T) { return new Box{val: v}; }\n"
        "}\n"
        "fun take(b: Box<int>) <int> { return b.val; }\n"
        "fun main() <noret> {\n"
        "    let fromCall <Box<int>> = Box::<int>(7);\n"
        "    let fromLit <Box<int>> = Box::<int>{val: 8};\n"
        "    fromLit = fromCall;\n"
        "    printf(\"%d %d\\n\", take(Box::<int>(41)), fromLit.val);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "41 7\n") << b.why();
    // And one instance rather than two with the same layout under different names,
    // which is the failure the assignment above could not see if both were named
    // `Box<int>` in the trace and were different types underneath.
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T> = null,\n"
        "    Box(v: T) { return new Box{val: v}; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let fromCall <Box<int>> = Box::<int>(7);\n"
        "    let fromLit <Box<int>> = Box::<int>{val: 8};\n"
        "}\n");
    EXPECT_EQ(occurrences(trace, "instantiated struct Box<int>"), 1u) << trace;
}

BACKEND_TEST(Soundness_Codegen, TwoTypeArgumentsBindInDeclarationOrder) {
    // deeptest4.fin's shape -- two parameters, the second a struct -- with the map
    // declared here rather than imported. `T` and `U` are distinguishable in the output,
    // so a substitution built in the order inference happened to find them would print
    // the halves swapped or refuse the field.
    const Built b = build(std::string(kPrintf) +
        "struct Data { integer <int> = null, str <string> = null }\n"
        "struct Map<T, U> {\n"
        "    k <T> = null, v <U>,\n"
        "    Map(key: T, value: U) { return new Map{k: key, v: value}; }\n"
        "    pub fun __get(self: &Self, key: T) <U> { return self.v; }\n"
        "    pub fun __set(self: &Self, key: T, value: U) <noret> { self.v = value; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <auto> = Map::<string, Data>(\"Hi\", Data{integer: 10});\n"
        "    printf(\"%d\\n\", a.__get(\"Hi\").integer);\n"
        "    a.__set(\"x\", Data{integer: 20});\n"
        "    printf(\"%d\\n\", a.__get(\"x\").integer);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    // The second line is the one that matters twice over: `__set` stores through the
    // receiver, so the object the constructor call produced has to be the caller's
    // addressable storage and not a copy the write was discarded into.
    EXPECT_EQ(b.out, "10\n20\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericConstructorCallNestsAsItsOwnTypeArgument) {
    // `Box::<Box<int>>(inner)` -- functions.fin:16 nests `Result<Result<int>>` through
    // an annotation, and this is the same nesting reached through a call. The inner
    // instantiation has to exist before the outer one's layout can, which is what makes
    // this more than a spelling test.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T> = null,\n"
        "    Box(v: T) { return new Box{val: v}; }\n"
        "    pub fun get(self: &Self) <T> { return self.val; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let inner <auto> = Box::<int>(3);\n"
        "    let outer <Box<Box<int>>> = Box::<Box<int>>(inner);\n"
        "    let un <Box<int>> = outer.get();\n"
        "    printf(\"%d %d\\n\", inner.get(), un.get());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3 3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericConstructorCallResolvesTThroughTheActiveBinding) {
    // `Box::<T>(v)` written inside `Wrap<T>`'s own constructor. The `T` in the type
    // arguments is the *enclosing* instantiation's, so it has to be mapped in the scope
    // that is already bound rather than treated as a name to instantiate at -- the same
    // rule instantiateGeneric's step 1 records for a written `Node<T>`.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T> = null,\n"
        "    Box(v: T) { return new Box{val: v}; }\n"
        "}\n"
        "struct Wrap<T> {\n"
        "    inner <Box<T>>,\n"
        "    Wrap(v: T) { return new Wrap{inner: Box::<T>(v)}; }\n"
        "    pub fun peek(self: &Self) <T> { return self.inner.val; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let w <auto> = Wrap::<int>(11);\n"
        "    printf(\"%d\\n\", w.peek());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "11\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericConstructorLeavesUnwrittenFieldsAtTheirDefaults) {
    // `tag` is written by nobody: not by the call, not by the constructor's
    // `new Box{val: v}`. It reads 5 because buildStructValue runs the declared defaults
    // for the fields a literal left out, and the instantiation's fields carry the
    // template's default nodes. Asserted here because the defaults are the one part of a
    // constructor call that the receiver convention could silently skip.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T> = null,\n"
        "    tag <int> = 5,\n"
        "    Box(v: T) { return new Box{val: v}; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <auto> = Box::<int>(7);\n"
        "    printf(\"%d %d\\n\", b.val, b.tag);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7 5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnErasureMarkedGenericStructInstantiatesConstructorMonomorphically) {
    const Built b = build(std::string(kPrintf) +
        "struct M<T: Castable> {\n"
        "    v <int> = 0,\n"
        "    M(x: T) { return new M{v: 1}; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let m <auto> = M::<int>(1);\n"
        "    printf(\"%d\\n\", m.v);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericConstructorCallInfersFromArgumentsThenAnnotation) {
    // The booked gap the previous test here named has landed: b690f60 ruled
    // the sources -- annotation first, then arguments -- and what inference
    // finds is recorded on the call, so both spellings lower through one
    // path. What still refuses for want of an answer is a call with no source
    // at all (AnUninferrableConstructorStillNamesTheTemplate), and sources
    // that disagree refuse in the front end, at the argument.
    const char* const kBox =
        "struct Box<T> {\n"
        "    val <T> = null,\n"
        "    Box(v: T) { return new Box{val: v}; }\n"
        "}\n";
    const Built inferred = build(std::string(kPrintf) + kBox +
        "fun main() <noret> {\n"
        "    let b <auto> = Box(7);\n"
        "    printf(\"%d\\n\", b.val);\n"
        "}\n");
    ASSERT_EQ(inferred.compileExit, 0) << inferred.why();
    ASSERT_TRUE(inferred.ran) << inferred.why();
    EXPECT_EQ(inferred.out, "7\n") << inferred.why();

    const Built mismatch = build(std::string(kPrintf) + kBox +
        "fun main() <noret> { let b <Box<string>> = Box(7); }\n");
    EXPECT_NE(mismatch.compileExit, 0) << mismatch.why();
    EXPECT_NE(mismatch.compileErr.find("expected 'string', got 'int'"),
              std::string::npos) << mismatch.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericStructWithNoConstructorNamesTheInstance) {
    // No constructor is declared, so there is no symbol to call, and this is *not*
    // quietly turned into a zeroed default-construct: a constructor is the only thing
    // that runs a field's default here, so a synthesised one would hand back an object
    // whose `= null` fields were never written. `Box::<int>{}` is the spelling that
    // means the defaults. The refusal names `Box<int>` and not `Box`, because the
    // instance is what has no constructor -- the template has no symbols at all.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> { val <T> = null }\n"
        "fun main() <noret> { let b <auto> = Box::<int>(); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a call to 'Box<int>'"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ATurbofishOnANonGenericNameIsStillRefused) {
    // The blanket refusal, now conditioned on the name being one this file declares.
    // Kept rather than dropped for the reason it was written: a written type argument
    // that changed nothing would be a silent disagreement with whatever the writer
    // expected it to change. Refused in the front end (like a struct's
    // `P::<int>`), so `build` -- which checks first -- reports the check.
    const Built b = build(
        "fun plain(x: int) <int> { return x; }\n"
        "fun main() <noret> { let a <int> = plain::<int>(1); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("Generic count mismatch: 'plain' declares 0 parameter(s)"),
              std::string::npos) << b.why();
}


// ---------------------------------------------------------------------------
// `#[llvm_name]` on a function, and the attributes nobody was reading
//
// The same sweep that found the dropped method bodies found two more places where an
// attribute went nowhere. On an `@define` this file has always read `#[llvm_name]`,
// because that is how the corpus binds an extern to a C symbol spelled differently
// (stdlib/stdio.fin:11). On a `fun` it did not, and stdlib/memory.fin:8 writes
// `#[llvm_name="dealloc"]` above `fun dealloc` -- a definition that was being emitted
// under the Fin name while the attribute asked for another. Unlike the struct case
// this one is load-bearing: a function's name *is* its symbol, and getting it wrong is
// a link error at best and the wrong function at worst.
//
// An enum's attributes were not read either, and `#[llvm_name="Result"]`
// (stdlib/typing.fin:24) has nothing to name here: an enum lowers to an integer, and
// an integer type has no name to give. So that one is refused rather than honoured --
// accepting it would be claiming to have done something.

BACKEND_TEST(Soundness_Codegen, AFunctionsLlvmNameIsItsSymbol) {
    // Proved across a link, which is the only place a symbol name is observable: the
    // library defines `twice` under the name `fin_twice`, and the caller knows it only
    // by that name. If the attribute were ignored the link would fail undefined.
    const fs::path libObj = uniqueTempPath("fin_lib_rn", ".o");
    const fs::path mainObj = uniqueTempPath("fin_main_rn", ".o");
    const fs::path exe = uniqueTempPath("fin_linked_rn");

    const Compiled lib = compileOnly(
        "#[llvm_name=\"fin_twice\"]\n"
        "fun twice(n: int) <int> { return n * 2; }\n", libObj);
    ASSERT_EQ(lib.exitCode, 0) << lib.why();
    const Compiled mainPart = compileOnly(std::string(kPrintf) +
        "@define fin_twice(n: int) <int>;\n"
        "fun main() <noret> { printf(\"%d\\n\", fin_twice(21)); }\n", mainObj);
    ASSERT_EQ(mainPart.exitCode, 0) << mainPart.why();

    const fs::path outPath = uniqueTempPath("fin_linked_rn_out");
    ASSERT_EQ(fin::runProcess(fin::linkCommand(
        {libObj.string(), mainObj.string()}, exe.string())), 0);

    EXPECT_EQ(fin::runProcess({exe.string()}, outPath.string(), outPath.string()), 0);
    EXPECT_EQ(readProcessOutput(outPath.string()), "42\n");

    std::error_code ec;
    for (const fs::path& p : {libObj, mainObj, exe, outPath}) fs::remove(p, ec);
}

BACKEND_TEST(Soundness_Codegen, ARenamedFunctionIsStillCalledByItsFinName) {
    // The other half: `#[llvm_name]` renames the symbol and not the Fin name, exactly
    // as it does on an `@define`. A call site inside the same module writes `twice`.
    const Built b = build(std::string(kPrintf) +
        "#[llvm_name=\"fin_twice\"]\n"
        "fun twice(n: int) <int> { return n * 2; }\n"
        "fun main() <noret> { printf(\"%d\\n\", twice(4)); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAttributeThisFileDoesNotReadOnAFunctionIsRefused) {
    // `#[overwrite(printf)]` (stdlib/stdio.fin:35) says which of two definitions
    // wins, and this file does not arbitrate. It used to share this test with
    // `#[export]`, which moved to the acceptance side: ADR 0033 rules export
    // import-visibility only, and the standard library -- which writes
    // `#[export]` above plain `fun`s throughout (fs.fin, math.fin, path.fin) --
    // has to compile standalone with `-c`, where nothing strips the attribute
    // first. See AnExportOnAFunctionIsVacuous below.
    const Built b = build(std::string(kPrintf) +
        "#[overwrite(printf)]\n"
        "fun twice(n: int) <int> { return n * 2; }\n"
        "fun main() <noret> { printf(\"%d\\n\", twice(4)); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("overwrite"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnExportOnAFunctionIsVacuous) {
    // The other half of the split above. ADR 0033: export is who may name this
    // declaration from an import, which is the analyzer's decision and already
    // made before codegen runs. The symbol this object publishes is
    // `#[llvm_name]`'s question and nothing else, so there is nothing here to
    // honour beyond not dropping it -- the same reason the struct path accepts
    // the flag (lowerableStruct) and the alias path does (visit(TypeDefinition)).
    // The stdlib's `-c` audit is the case that asked; a lone program must build
    // to exactly the same bytes with and without the attribute, which the run
    // pins.
    const Built b = build(std::string(kPrintf) +
        "#[export]\n"
        "fun twice(n: int) <int> { return n * 2; }\n"
        "fun main() <noret> { printf(\"%d\\n\", twice(21)); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAttributeOnAnEnumIsRefused) {
    // An enum is an integer here and an integer type has no name, so there is nothing
    // for `#[llvm_name]` to rename -- honouring it would mean claiming to.
    const Built b = build(std::string(kPrintf) +
        "#[llvm_name=\"Result\"]\n"
        "enum State { Alive = 1, Dead }\n"
        "fun main() <noret> {\n"
        "    let s <State> = Alive;\n"
        "    printf(\"%d\\n\", cast<int>(s));\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("llvm_name"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnExportOnAnEnumIsVacuous) {
    // declareEnums used to refuse every attribute on an enum, `#[export]` included.
    // ADR 0033 gives the same answer it gives everywhere else: export is who may
    // name the declaration from an import, and an enum's members are folded
    // constants with no symbol to export. The refusal that named stdio.fin:23 as
    // its reason is the same line that stopped `lib/std/*.fin` compiling
    // standalone with `-c`, which is the case that asked for this.
    const Built b = build(std::string(kPrintf) +
        "#[export]\n"
        "enum Mode { Off, On }\n"
        "fun main() <noret> {\n"
        "    let m <Mode> = On;\n"
        "    printf(\"%d\\n\", cast<int>(m));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumWithNoAttributesStillLowers) {
    // operators.fin:6-9 and the boundary from the other side.
    const Built b = build(std::string(kPrintf) +
        "enum State { Alive = 1, Dead }\n"
        "fun main() <noret> {\n"
        "    let s <State> = Dead;\n"
        "    printf(\"%d\\n\", cast<int>(s));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2\n") << b.why();
}


// ---------------------------------------------------------------------------
// The last two places an attribute went unread
//
// A global's attributes were already refused and a local's were not, which is the
// wrong way round if either matters: variables.fin:27 writes `#[slaveof(z)]` on a
// *local* and its own comment says the attribute "makes the lifetime of variable
// declaration statement to the variable passed to slaveof" -- a rule about when the
// storage dies, which is generated code. A global one is easier to reason about
// (`#[slaveof($Fin)]`, :35, asks for what a global already does); the local is the one
// that actually changes something, and it was the one being dropped.
//
// THE `slaveof` HALF OF THAT WAS RULED ON, 2026-08-28, REVISITED IN WAVE-5
// SLICE 4, AND THE ARGUMENT ABOVE IS WHY IT COULD BE -- AND WHERE IT ENDED.
// It rested on "a rule about when the storage dies" -- and at the time
// nothing in this backend made storage die. Memory management is a library
// (ADR 0003), so a heap allocation is released only by an explicit `delete`:
// measured, an object built from a scope that allocates references `malloc`
// and not `free`. So an allocation nobody deletes already outlives every
// scope, which is what `slaveof(z)` asks for, and already lives until the
// program exits, which is what `slaveof($Fin)` asks for. Emitting nothing
// *satisfied* both rather than dropping them.
//
// Scope-exit destructors have since made storage die after all, so the ruling
// is narrowed to the storage it still holds for: destructor-less ties keep
// the old lowering, and struct-typed ties delay destruction to the referent's
// scope exit (`$Fin` pins to program exit).
//
// That is a narrow exemption and it is guarded from three sides:
// ASlaveofAttributeKeepsItsAllocationAlive asserts the pointer consequence (a
// read through a pointer whose scope has closed still gives 5),
// ASlaveofTiedStructDestroysAtItsMastersExit asserts the struct consequence
// (destruction waits for the referent), ASlaveofOnPlainDataLowersIdentically
// asserts the two lower to the same code, and
// AnUnreadAttributeOnAVariableIsStillRefused holds that no *other* attribute
// became acceptable. All live in the address-of section beside the other
// ruling of the same day.
//
// A struct member's were unread too, and that half stands. readonly.fin:19 writes
// `#[debug]` on a field, and a field attribute is one edit away from being a field
// *offset* attribute.

BACKEND_TEST(Soundness_Codegen, ASlaveofAttributeOnALocalIsAccepted) {
    // Was AnAttributeOnALocalIsRefused. Inverted rather than deleted: the same program,
    // now expected to build and run, which is what makes the pair of assertions a
    // record of where the boundary moved rather than of a test that vanished.
    //
    // `slaveof` on a local whose storage is not even heap-allocated is the weakest form
    // of the case -- there is nothing here for any lifetime rule to act on -- so if this
    // ever refuses again, the exemption has been narrowed further than the ruling.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let z <int> = 1;\n"
        "    #[slaveof(z)]\n"
        "    let m <int> = 2;\n"
        "    printf(\"%d\\n\", z + m);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ALocalWithNoAttributesStillLowers) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let z <int> = 1;\n"
        "    let m <int> = 2;\n"
        "    printf(\"%d\\n\", z + m);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnUnknownAttributeOnAStructMemberIsRefused) {
    const Built b = build(std::string(kPrintf) +
        "struct S {\n"
        "    #[layout]\n"
        "    v <int>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{ v: 5 };\n"
        "    printf(\"%d\\n\", s.v);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("layout"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnUnknownAttributeOnAGenericStructsMemberIsRefusedAtTheTemplate) {
    // At the declaration and not once per instantiation, for the reason the method
    // refusal gives: it will not become lowerable at `Box<int>`.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    #[layout]\n"
        "    val <T>\n"
        "}\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("layout"), std::string::npos) << b.why();
}

// --- a generic function ------------------------------------------------------
//
// Monomorphisation, the same strategy the struct templates already use and for the
// same stated reason: struct_methods.fin:6 calls `T` "a Monomorphization Generic
// type because its the default generic type we use", and ADR 0002 carries
// pyprototype's rule forward -- erasure is what an erasure-*marker* constraint
// selects, and a bare `<T>` has none. So one body per distinct binding, laid out as
// if the argument had been written in place of the parameter.
//
// What the corpus asks for:
//
//   `fun swap<T>(a: &T, b: &T) <noret>` called as `swap(&a, &b)` with no turbofish
//   (simple_pointers.fin:5,18) -- the binding is inferred from the arguments, and
//   through a pointer, so the parameter's written type is a decoration over `T`
//   rather than `T` itself.
//
//   `fun normal_generics<T>(a: T) <T>` (generics_interfaces.fin:12), whose own
//   comment says "Monomorphism" -- `T` in the return position, which is why the
//   old refusal landed on the signature rather than on the body.
//
//   `fun using_erasure_generics<T: Castable, U: Castable>` (generics_interfaces.fin:8)
//   -- the other branch, which ADR 0002 says is a raw pointer. Refused, by name,
//   because a monomorphised body for it would be a different program that happens to
//   agree on these arguments -- but refused at the *use* and not at the declaration,
//   on the same argument monomorphisation makes everywhere else here: a template is a
//   recipe, and the one in that sample is never called. Nothing is emitted for it and
//   nothing about it can be wrong, which is why that sample now reaches an object.
//
// The binding is resolved *here* and not read off the analyzer, which is what makes
// the turbofish work at all: a free function's turbofish binds nothing in
// Analyzer_Expr (booked), so the backend that trusted it would instantiate at
// whatever the argument happened to be.
//
// An instance is weak (linkonce_odr), not external. Two objects that each wrote the
// template and each instantiated it at `int` publish one symbol between them, which
// is the C++ template bargain: identical bodies, one copy, and the linker picks. An
// external instance would make the second object a duplicate-symbol error.

BACKEND_TEST(Soundness_Codegen, AGenericFunctionNobodyCallsLowersToNothing) {
    // The declaration is no longer where a generic function is refused, because a
    // template is not code -- it is a recipe, and monomorphisation means no
    // instantiation, no body. Inverted from the old signature refusal, which fired
    // on generics_interfaces.fin's uncalled `normal_generics`.
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "fun ident<T>(a: T) <T> { return a; }\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    EXPECT_EQ(trace.find("declared ident"), std::string::npos) << trace;
    EXPECT_EQ(trace.find("not lowered yet"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, AGenericFunctionsTypeArgumentIsInferredFromItsArgument) {
    const Built b = build(std::string(kPrintf) +
        "fun ident<T>(a: T) <T> { return a; }\n"
        "fun main() <noret> { printf(\"%d\\n\", ident(5)); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericFunctionsTurbofishBindsItsTypeArgument) {
    // Written, not inferred. Both spellings have to reach the same instance, and the
    // written one has to work even though the analyzer's own turbofish binds nothing.
    const Built b = build(std::string(kPrintf) +
        "fun ident<T>(a: T) <T> { return a; }\n"
        "fun main() <noret> { printf(\"%d\\n\", ident::<int>(5)); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoBindingsOfOneGenericFunctionAreTwoFunctions) {
    // The whole of monomorphisation in one test: `a + a` is an integer add in one
    // instance and a float add in the other, and a backend that emitted one body
    // would have to pick one of them and be wrong about the other.
    const Built b = build(std::string(kPrintf) +
        "fun twice<T>(a: T) <T> { return a + a; }\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %f\\n\", twice(21), twice(1.5));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42 3.000000\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoBindingsAreTwoNamesInTheTrace) {
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "fun twice<T>(a: T) <T> { return a + a; }\n"
        "fun main() <noret> { printf(\"%d %f\\n\", twice(21), twice(1.5)); }\n");
    EXPECT_NE(trace.find("declared twice<int>"), std::string::npos) << trace;
    EXPECT_NE(trace.find("declared twice<double>"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, OneBindingCalledTwiceIsEmittedOnce) {
    // The cache, which is what stops the second call from emitting a second body
    // under a name LLVM would then have to unique -- and a uniqued second copy is a
    // symbol nobody calls plus a program that is twice the size it should be.
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "fun ident<T>(a: T) <T> { return a; }\n"
        "fun main() <noret> { printf(\"%d %d\\n\", ident(3), ident(4)); }\n");
    EXPECT_EQ(occurrences(trace, "declared ident<int>"), 1u) << trace;
}

BACKEND_TEST(Soundness_Codegen, AGenericFunctionsPointerParameterSwapsThroughIt) {
    // simple_pointers.fin:5,18, less the `delete &temp` on line 10 -- `delete` is a
    // library call (ADR 0003) and not part of this. The binding is inferred through
    // the decoration: the argument is a `&int` and `T` is the `int` inside it.
    const Built b = build(std::string(kPrintf) +
        "fun swap<T>(a: &T, b: &T) <noret> {\n"
        "    let temp <T> = *b;\n"
        "    *b = *a;\n"
        "    *a = temp;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <int> = 10;\n"
        "    let b <int> = 20;\n"
        "    swap(&a, &b);\n"
        "    printf(\"a = %d, b = %d\\n\", a, b);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "a = 20, b = 10\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericFunctionBindsThroughAGenericStruct) {
    // `Box<T>` in a parameter position: the argument is a `Box<int>`, so `T` is what
    // that instantiation bound its own parameter to. Read off the instantiation's
    // substitution rather than re-derived, which is what keeps `Box<Colour>` from
    // binding `T` to `int`.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>\n"
        "}\n"
        "fun unbox<T>(b: Box<T>) <T> { return b.val; }\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 42 };\n"
        "    printf(\"%d\\n\", unbox(b));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFunctionBindingSpellsAStructTheWayAWrittenOneDoes) {
    // Why the instance is keyed on a Fin-style spelling of the representation and not
    // on an LLVM one. A binding inferred from an argument cannot recover the written
    // name, so it has to be re-derived -- and if it were derived as `i32`, the
    // `Box<T>` inside the instance's body would instantiate a *second* `Box<i32>`
    // with a layout identical to the `Box<int>` main already has. Two names for one
    // layout is two structs that are not assignable to each other.
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>\n"
        "}\n"
        "fun rebox<T>(v: T) <int> {\n"
        "    let b <Box<T>> = Box::<T>{ val: v };\n"
        "    return 1;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let w <Box<int>> = Box::<int>{ val: 1 };\n"
        "    printf(\"%d\\n\", rebox(w.val));\n"
        "}\n");
    EXPECT_EQ(occurrences(trace, "instantiated struct Box<int>"), 1u) << trace;
    EXPECT_EQ(trace.find("Box<i32>"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, AGenericFunctionCanRecurse) {
    // The instance has to be registered before its own body is emitted, or the
    // recursive call inside it asks for an instantiation that is already in progress
    // and starts a second one -- which does not terminate.
    const Built b = build(std::string(kPrintf) +
        "fun down<T>(n: T) <T> {\n"
        "    if (n <= 0) { return n; }\n"
        "    return down(n - 1);\n"
        "}\n"
        "fun main() <noret> { printf(\"%d\\n\", down(5)); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ARecursiveGenericFunctionIsEmittedOnce) {
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "fun down<T>(n: T) <T> {\n"
        "    if (n <= 0) { return n; }\n"
        "    return down(n - 1);\n"
        "}\n"
        "fun main() <noret> { printf(\"%d\\n\", down(5)); }\n");
    EXPECT_EQ(occurrences(trace, "declared down<int>"), 1u) << trace;
}

BACKEND_TEST(Soundness_Codegen, AGenericFunctionsInstanceIsOneSymbolAcrossTwoObjects) {
    // The linkage decision, which is only observable across a link. Both objects
    // wrote the template and both instantiated it at `int`; an external definition in
    // each would be a duplicate-symbol error, and there is no third place to put the
    // instance because neither object knows about the other.
    const fs::path libObj = uniqueTempPath("fin_lib_gi", ".o");
    const fs::path mainObj = uniqueTempPath("fin_main_gi", ".o");
    const fs::path exe = uniqueTempPath("fin_linked_gi");

    const Compiled lib = compileOnly(
        "fun ident<T>(a: T) <T> { return a; }\n"
        "fun libval() <int> { return ident(21); }\n", libObj);
    ASSERT_EQ(lib.exitCode, 0) << lib.why();
    const Compiled mainPart = compileOnly(std::string(kPrintf) +
        "@define libval() <int>;\n"
        "fun ident<T>(a: T) <T> { return a; }\n"
        "fun main() <noret> { printf(\"%d\\n\", ident(libval()) * 2); }\n", mainObj);
    ASSERT_EQ(mainPart.exitCode, 0) << mainPart.why();

    const fs::path outPath = uniqueTempPath("fin_linked_gi_out");
    ASSERT_EQ(fin::runProcess(fin::linkCommand(
        {libObj.string(), mainObj.string()}, exe.string())), 0);

    EXPECT_EQ(fin::runProcess({exe.string()}, outPath.string(), outPath.string()), 0);
    EXPECT_EQ(readProcessOutput(outPath.string()), "42\n");

    std::error_code ec;
    for (const fs::path& p : {libObj, mainObj, exe, outPath}) fs::remove(p, ec);
}

BACKEND_TEST(Soundness_Codegen, ATypeArgumentThatNoArgumentMentionsIsRefused) {
    // `T` appears in no parameter, so there is nothing to infer it from. Refused
    // naming the parameter, because the alternative is picking a type -- and a
    // function instantiated at a type the program never named is a function the
    // program did not write.
    const Built b = build(std::string(kPrintf) +
        "fun nothing<T>() <int> { return 1; }\n"
        "fun main() <noret> { printf(\"%d\\n\", nothing()); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("'T'"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ATypeArgumentThatNoArgumentMentionsTakesATurbofish) {
    // The other half: written, it is not inferred at all, so the same function is
    // fine. This pair is why the turbofish is read here rather than treated as
    // redundant with inference.
    const Built b = build(std::string(kPrintf) +
        "fun nothing<T>() <int> { return 1; }\n"
        "fun main() <noret> { printf(\"%d\\n\", nothing::<int>()); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnErasureMarkedGenericFunctionIsRefusedAtTheCall) {
    // generics_interfaces.fin:8. ADR 0002: an erasure-marker constraint on any one
    // parameter selects erasure, and an erased generic is a raw pointer. That is a
    // different representation, not a different spelling, so monomorphising it would
    // compile and be a different program -- one that happens to agree here and
    // disagree wherever the erased pointer is what the program is about.
    //
    // At the call, which is the half of this pair that used to be the declaration. The
    // message names the function so a reader with two templates knows which.
    const Built b = build(std::string(kPrintf) +
        "fun erased<T: Castable>(a: T) <int> { return cast<int>(a); }\n"
        "fun main() <noret> { printf(\"%d\\n\", erased(5)); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("the erasure marker 'Castable' on 'T' of the generic "
                                "function 'erased'"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnErasureMarkedGenericFunctionNobodyCallsLowersToNothing) {
    // The negative half, and the reason the refusal moved. A template is a recipe and
    // not code, so an uncalled one asks for no representation at all -- there is
    // nothing for ADR 0002's erasure rule to be about. This is exactly
    // AGenericFunctionNobodyCallsLowersToNothing's argument with a constraint added,
    // and generics_interfaces.fin is the sample that argument was costing: its only
    // blocker was an uncalled `using_erasure_generics`, and everything the file
    // actually does is monomorphic.
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "fun erased<T: Castable>(a: T) <int> { return cast<int>(a); }\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    EXPECT_EQ(trace.find("not lowered yet"), std::string::npos) << trace;
    EXPECT_EQ(trace.find("declared erased"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, AnErasureMarkedGenericStructInstantiatesMonomorphically) {
    const Built b = build(std::string(kPrintf) +
        "struct maybe<T: Castable> {\n"
        "    val <T>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let m <maybe<int>>;\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnErasureMarkedStructNobodyInstantiatesLowersToNothing) {
    // The struct's negative half, and the corpus has written this one down: `struct M
    // <T> {}` (blame_assert.fin:19) is a whole sample's worth of evidence that an
    // uninstantiated template is not an error, and a constraint on it does not change
    // what is emitted for it (nothing).
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "struct maybe<T: Castable> {\n"
        "    val <T>\n"
        "}\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    EXPECT_EQ(trace.find("not lowered yet"), std::string::npos) << trace;
    EXPECT_EQ(trace.find("instantiated struct maybe"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, AnErasureMarkedGenericMethodIsRefusedAtTheCall) {
    // The site the move exposed, and it had no check of any kind before: a method's
    // type parameters are not in the struct's `generic_params`, so the old
    // declaration-site predicate never saw them. `Box` is not generic and `peek` is,
    // so this reached declareFunction through instantiateGenericMethod, monomorphised
    // at the argument's type and *ran* -- printing 7, which is the silently different
    // program ADR 0002 names. Measured before the fix, so this is a regression test
    // for a real wrong answer rather than for a hypothetical one.
    const Built b = build(std::string(kPrintf) +
        "struct Box {\n"
        "    val <int>,\n"
        "    pub fun peek<U: Castable>(u: U) <int> { return self.val; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box>;\n"
        "    b.val = 7;\n"
        "    printf(\"%d\\n\", b.peek(3));\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("the erasure marker 'Castable' on 'U' of the generic "
                                "method 'peek' of struct 'Box'"), std::string::npos)
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnErasureMarkedMethodNobodyCallsLowersToNothing) {
    // The method's negative half, and it is not redundant with the function's: a
    // method of an *instantiated* struct is declared by instantiateGeneric's step 4,
    // which runs for `Box<int>` whether or not any call reaches the method. So this
    // asserts that step 4 does not declare a marked one -- a generic method is a
    // template within a template, and step 4 only registers it.
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    pub fun peek<U: Castable>(u: U) <int> { return cast<int>(self.val); }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>>;\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    EXPECT_NE(trace.find("instantiated struct Box<int>"), std::string::npos) << trace;
    EXPECT_EQ(trace.find("not lowered yet"), std::string::npos) << trace;
    EXPECT_EQ(trace.find("Box<int>.peek"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, AnErasureMarkedStructInstantiatesWhereverALayoutIsNeeded) {
    const char* const kTemplate =
        "struct maybe<T: Castable> {\n"
        "    val <T>\n"
        "}\n";

    const Built field = build(std::string(kPrintf) + kTemplate +
        "struct Holder {\n"
        "    m <maybe<int>>\n"
        "}\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    ASSERT_TRUE(field.ran) << field.why();
    EXPECT_EQ(field.out, "ok\n") << field.why();

    const Built param = build(std::string(kPrintf) + kTemplate +
        "fun take(m: maybe<int>) <int> { return 0; }\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    ASSERT_TRUE(param.ran) << param.why();
    EXPECT_EQ(param.out, "ok\n") << param.why();

    const Built nested = build(std::string(kPrintf) + kTemplate +
        "struct Box<T> {\n"
        "    v <T>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<maybe<int>>>;\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    ASSERT_TRUE(nested.ran) << nested.why();
    EXPECT_EQ(nested.out, "ok\n") << nested.why();
}

BACKEND_TEST(Soundness_Codegen, AnUncalledErasureTemplateDoesNotCostTheRestOfTheModule) {
    // The whole point of the move, as a value rather than as an absence: an object that
    // contains a marked template it never uses is a working object. This is
    // generics_interfaces.fin's shape -- an uncalled `<T: Castable, U: Castable>`
    // beside a monomorphic generic that *is* called -- and it asserts the answer the
    // called one computes, so a build that emitted the wrong body would fail here
    // rather than pass for compiling.
    const Built b = build(std::string(kPrintf) +
        "fun using_erasure_generics<T: Castable, U: Castable>(a: T, b: U) <int> {\n"
        "    return cast<int>(a) + cast<int>(b);\n"
        "}\n"
        "fun normal_generics<T>(a: T) <T> { return a; }\n"
        "fun main() <noret> { printf(\"%d\\n\", normal_generics(41) + 1); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, OneMarkedParameterOfTwoIsEnoughAndTheFirstIsNamed) {
    // ADR 0002 says "on any one parameter", so the mixed case is the rule's own
    // wording and not an edge: `<T, U: Castable>` is erased. The first marked one is
    // named rather than all of them, because fixing either of
    // generics_interfaces.fin:8's two alone fixes nothing -- and `T` here is *not*
    // marked, which is what makes this test say that the search is for a marker rather
    // than a look at parameter zero.
    const Built b = build(std::string(kPrintf) +
        "fun mixed<T, U: Castable>(a: T, b: U) <int> { return cast<int>(a); }\n"
        "fun main() <noret> { printf(\"%d\\n\", mixed(1, 2)); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("'Castable' on 'U'"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AConstraintThatIsNotTheErasureMarkerStillMonomorphises) {
    // A constraint set is a bound on what may be bound and not a representation
    // (ADR 0018), so the backend gets a concrete type either way and the constraint is
    // the analyzer's business. Refusing every constraint would have taken `sort<T:
    // Number>` (arrays.fin:9) and `number2str<T: Number>` (types.fin:106) with it --
    // written with `any` here because `type Number = int | float` is a type alias and
    // those are a unit of their own, so the corpus's own spelling cannot reach the
    // backend yet.
    const Built b = build(std::string(kPrintf) +
        "fun addup<T: any>(a: T) <T> { return a + a; }\n"
        "fun main() <noret> { printf(\"%d\\n\", addup(21)); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericFunctionsLlvmNameIsRefused) {
    // Honoured on a struct template, refused on a function one, and the difference is
    // the whole reason the struct rename was safe: a struct type's name reaches no
    // object file, so two instantiations asking for one name are uniqued. A function's
    // name *is* its symbol, so one name over two instances is either a duplicate
    // symbol or a silent `foo.1` that nobody can call.
    const Built b = build(std::string(kPrintf) +
        "#[llvm_name=\"fin_ident\"]\n"
        "fun ident<T>(a: T) <T> { return a; }\n"
        "fun main() <noret> { printf(\"%d\\n\", ident(5)); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("llvm_name"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEnumBindingSharesTheIntegerInstance) {
    // A fieldless enum *is* an `int` in this file, so `ident(Red)` and `ident(3)` are
    // one instance -- which is the right answer for a function (the emitted code is
    // identical) and deliberately not the one the struct table gives, where
    // `Box<Colour>` and `Box<int>` are two names for one layout so that a diagnostic
    // can say which the program wrote. The asymmetry is recorded here because it
    // looks like an inconsistency and is not.
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "enum Colour { Red = 1, Green }\n"
        "fun ident<T>(a: T) <T> { return a; }\n"
        "fun main() <noret> { printf(\"%d %d\\n\", ident(Red), ident(3)); }\n");
    EXPECT_EQ(occurrences(trace, "declared ident<int>"), 1u) << trace;
}

BACKEND_TEST(Soundness_Codegen, AGenericFunctionsBodyIsCheckedAtEachBinding) {
    // A template whose body is only lowerable for *some* arguments refuses at the
    // instantiation and not at the declaration, which is the opposite of where a
    // method or an attribute refuses. `cast<int>` of a float is not lowered yet, so
    // the `double` instance is the one that fails -- and it says so at the call.
    const Built b = build(std::string(kPrintf) +
        "fun asint<T>(a: T) <int> { return cast<int>(a); }\n"
        "fun main() <noret> { printf(\"%d\\n\", asint(1.5)); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("conversion"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericFunctionInstantiatedAtAStructPassesItByValue) {
    // A struct binding, which is the case where the instance's signature is not a
    // scalar and the aggregate has to survive being a parameter. Fin-to-Fin, so the
    // LLVM aggregate is the whole ABI and both sides are emitted here.
    const Built b = build(std::string(kPrintf) +
        "struct Point { x <int>, y <int> }\n"
        "fun first<T>(a: T) <T> { return a; }\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point{ x: 3, y: 4 };\n"
        "    let q <Point> = first(p);\n"
        "    printf(\"%d %d\\n\", q.x, q.y);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3 4\n") << b.why();
}

// ---------------------------------------------------------------------------
// A struct method
//
// tests/samples/struct_methods.fin is the whole of the specification, and it is
// four functions in one struct:
//
//   fun print_point(self: &Self) <noret>      -- the receiver written out
//   fun set_x<U>(new_x: U) <noret>            -- the receiver injected, plus a
//                                                method generic of its own
//   pub static fun default_point() <&Self>    -- no receiver, `Self` as a type
//   pub static fun default_point2() <&Point>  -- the same thing spelled with the
//                                                template's own name
//
// with :10's note settling the receiver: "the first parameter will be injected by
// compiler and it will be the struct itself (it has to be from its own type which
// can be specified with `Self` type or just {ClassName}) [NOTE: if the declared
// function doesn't have the static keyword and it doesn't specify the first argument
// as self compiler still automatically injects the self to the block of that
// function]".
//
// Four decisions, each of which some test below is the reason for.
//
// 1. The symbol is `Struct.method`, and for an instantiation `Struct<args>.method`.
//    Fin mangles nothing -- a free function publishes the name it was written with --
//    so this is the least mangling that keeps two structs' `get` apart, and a `.` is
//    a legal ELF symbol character. It is also already this file's convention: a
//    generic function instance publishes `ident<int>`, brackets and all.
//
// 2. The receiver is a pointer, always. `set_x` assigns to `self.x`, and a receiver
//    passed by value would make that method a no-op on a copy -- a silently
//    ineffective assignment, which is the failure this suite exists to refuse. So an
//    injected `self` is `&Self`, and a *written* one must be a pointer to its own
//    struct (`&Self`, or `&Point<T>`, or `&Box` -- all the same type) or it refuses.
//
// 3. `Self` is a binding and not a name lookup. Inside a method the mapper is handed
//    `Self -> this struct`, and for a template also `Point -> this instantiation`,
//    which is what makes :18's `<&Self>` and :21's `<&Point>` two spellings of one
//    type rather than one type and one refusal.
//
// 4. A method on a template is a template. It has no signature until the struct is
//    instantiated, so the instance's methods are emitted where the instance is
//    built, and a `Point<T>` nobody instantiates emits nothing at all. That is what
//    lets struct_methods.fin -- which has no `main` and instantiates nothing --
//    compile, and it is the same rule a generic free function follows.
//
// A method *generic* (`set_x<U>`) is one layer further: two substitutions, the
// struct's and the call's. Registered but not lowered, so a call to one refuses and
// a declaration nobody calls is not a refusal -- exactly as for the struct template
// that holds it.

BACKEND_TEST(Soundness_Codegen, AMethodWithAWrittenSelfIsCalledThroughIt) {
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    fun get(self: &Self) <int> { return self.x; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point{ x: 7 };\n"
        "    printf(\"%d\\n\", p.get());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodWithNoWrittenSelfStillHasOne) {
    // struct_methods.fin:14's spelling. The body names `self` and the parameter list
    // does not, and the analyzer defines it in the body's scope
    // (Analyzer_Decl.cpp's "[Magic] Injected implicit 'self'"); the backend has to
    // supply the same thing as an argument.
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    fun get() <int> { return self.x; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point{ x: 7 };\n"
        "    printf(\"%d\\n\", p.get());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInjectedSelfIsAReferenceSoAMethodCanAssignThroughIt) {
    // The reason the receiver is a pointer and not a copy. struct_methods.fin:15 is
    // `self.x = cast<T>(new_x);` inside a method that writes no `self`, and if the
    // injected receiver were by value that line would assign to a copy the caller
    // never sees -- a method that does nothing, with nothing in the source saying so.
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    fun bump() <noret> { self.x = self.x + 1; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point{ x: 7 };\n"
        "    p.bump();\n"
        "    printf(\"%d\\n\", p.x);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodsOwnArgumentsFollowTheReceiver) {
    // The receiver is argument 0 and is not one of the arguments the call site
    // writes, which is also what the analyzer's signature says (buildMethodSignature
    // drops a written `self`). Two parameters, so an off-by-one in either direction
    // shows up as a wrong number rather than as a crash.
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    fun combine(a: int, b: int) <int> { return self.x * 100 + a * 10 + b; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point{ x: 1 };\n"
        "    printf(\"%d\\n\", p.combine(2, 3));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "123\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodCallsAnotherMethodThroughSelf) {
    // `self` inside a method is a `&Point`, so `self.bump()` is the pointer-receiver
    // case one line down applied to the receiver itself.
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    fun bump() <noret> { self.x = self.x + 1; }\n"
        "    fun twice() <noret> { self.bump(); self.bump(); }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point{ x: 7 };\n"
        "    p.twice();\n"
        "    printf(\"%d\\n\", p.x);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodCallOnAPointerDerefsTheReceiverFirst) {
    // deeptest3.fin:39 -- "Access members via pointer (Fin automatically handles ->
    // logic with .)" -- and a method call is a `.`. The mutation is what proves the
    // receiver is the original and not a copy made to call through.
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    fun bump() <noret> { self.x = self.x + 1; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point{ x: 7 };\n"
        "    let q <&Point> = &p;\n"
        "    q.bump();\n"
        "    printf(\"%d\\n\", p.x);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodCallOnAFieldUsesThatFieldsAddress) {
    // The receiver is an expression and not only a variable: `o.inner.bump()` has to
    // mutate the field in place, and the GEP that reaches it is the one `.` already
    // builds.
    const Built b = build(std::string(kPrintf) +
        "struct Inner { v <int>, fun bump() <noret> { self.v = self.v + 1; } }\n"
        "struct Outer { inner <Inner> }\n"
        "fun main() <noret> {\n"
        "    let o <Outer> = Outer{ inner: Inner{ v: 1 } };\n"
        "    o.inner.bump();\n"
        "    printf(\"%d\\n\", o.inner.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodOnAnRvalueSpillsToStackTemporary) {
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    pub static fun make(v: int) <Point> { return Point{ x: v }; }\n"
        "    fun get(self: &Self) <int> { return self.x; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", Point::make(1).get());\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodChainEndingInGetEvaluatesOnce) {
    // A struct method named `get` collides with the prototype `get`: the call
    // used to emit its receiver twice (once for the prototype probe, once for
    // the struct path), so a mutating `add` ran twice (`1` -> `21`, base `21`)
    // instead of once (`11`, base `11`).
    const Built b = build(std::string(kPrintf) +
        "struct Acc {\n"
        "    v <int>,\n"
        "    fun add(self: &Self, n: int) <Self> {\n"
        "        self.v = self.v + n;\n"
        "        return *new Self{ v: self.v };\n"
        "    }\n"
        "    fun get(self: &Self) <int> { return self.v; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <Acc> = Acc{ v: 1 };\n"
        "    let r <int> = a.add(10).get();\n"
        "    printf(\"%d %d\\n\", r, a.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "11 11\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoStructsWithTheSameMethodNameKeepTheirOwn) {
    // The reason the symbol carries the struct. One name for both would be a
    // duplicate definition at best and the wrong body at worst.
    const Built b = build(std::string(kPrintf) +
        "struct A { v <int>, fun get() <int> { return self.v; } }\n"
        "struct B { v <int>, fun get() <int> { return self.v * 10; } }\n"
        "fun main() <noret> {\n"
        "    let a <A> = A{ v: 1 };\n"
        "    let b <B> = B{ v: 2 };\n"
        "    printf(\"%d %d\\n\", a.get(), b.get());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 20\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodCanCallAFreeFunctionDeclaredAfterIt) {
    // Ordering. Method bodies are emitted after every top-level prototype exists,
    // not while the struct table is being built -- a body emitted too early would
    // find no `helper` and refuse a call the program is entitled to make.
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    fun doubled() <int> { return helper(self.x); }\n"
        "}\n"
        "fun helper(n: int) <int> { return n * 2; }\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point{ x: 21 };\n"
        "    printf(\"%d\\n\", p.doubled());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodCanRecurse) {
    // The prototype has to be in the table before the body is emitted, which is the
    // same ordering an instantiated generic function needs and for the same reason.
    const Built b = build(std::string(kPrintf) +
        "struct Counter {\n"
        "    n <int>,\n"
        "    fun down(k: int) <int> { if (k <= 0) { return self.n; } return self.down(k - 1); }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let c <Counter> = Counter{ n: 5 };\n"
        "    printf(\"%d\\n\", c.down(3));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStaticMethodIsCalledOnTheTypeAndTakesNoReceiver) {
    // `pub static fun` -- struct_methods.fin:18,21. There is no object to pass, and
    // the call is written on the type.
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    pub static fun make(v: int) <Point> { return Point{ x: v }; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point::make(2);\n"
        "    printf(\"%d\\n\", p.x);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStaticMethodReturningSelfByReferenceAllocates) {
    // struct_methods.fin:18-19 exactly: `pub static fun default_point() <&Self>`
    // returning `new Self{x: 0}`, with the comment "we must return by reference since
    // we are allocating in memory with `new`". Two uses of the `Self` binding in one
    // declaration -- the return type and the type being allocated.
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    y <int> = 4,\n"
        "    pub static fun origin() <&Self> { return new Self{ x: 5 }; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <&Point> = Point::origin();\n"
        "    printf(\"%d %d\\n\", p.x, p.y);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5 4\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TheStructsOwnNameMeansTheSameAsSelf) {
    // struct_methods.fin:21-22's `default_point2`, whose whole point is that
    // "&Point instead of &Self is correct too". Both spellings in one struct, so
    // they have to agree about the type.
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    pub static fun a() <&Self> { return new Self{ x: 1 }; }\n"
        "    pub static fun b() <&Point> { return new Point{ x: 2 }; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <&Point> = Point::a();\n"
        "    let q <&Point> = Point::b();\n"
        "    printf(\"%d %d\\n\", p.x, q.x);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStaticMethodDoesNotTakeAnInjectedSelf) {
    // The other half of :10's note: the injection is for a method *without* the
    // static keyword. A static method that named `self` would be a front-end
    // question; what this checks is that the signature has no hidden first
    // parameter, which a wrong argument count would expose.
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    pub static fun two() <int> { return 2; }\n"
        "    fun get() <int> { return self.x; }\n"
        "}\n"
        "fun use() <int> { return Point::two(); }\n");
    EXPECT_NE(trace.find("declared Point.two"), std::string::npos) << trace;
    EXPECT_NE(trace.find("declared Point.get"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, AMethodsAttributeIsRefused) {
    // `#[export]` parses on a method and nothing here reads it. The same rule every
    // other attribute site in this file follows: an attribute that might decide
    // linkage or which of two definitions wins is refused rather than dropped.
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    #[export] fun get(self: &Self) <int> { return self.x; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point{ x: 1 };\n"
        "    printf(\"%d\\n\", p.get());\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("export"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWrittenSelfThatIsNotAPointerToItsStructIsRefused) {
    // The receiver's representation is one thing, so a written `self` has to be that
    // thing. `self: Self` is a copy, and a method that assigned through it would
    // silently discard the assignment; `self: int` is not the struct at all.
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    fun get(self: Point) <int> { return self.x; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point{ x: 1 };\n"
        "    printf(\"%d\\n\", p.get());\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("self"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericStructsMethodIsEmittedOncePerInstantiation) {
    // Two instantiations are two structs, so they are two methods -- and each is
    // emitted with the substitution its own instance was built from, which is what
    // makes `self.val` an i32 in one and an i8 in the other.
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun get(self: &Self) <T> { return self.val; }\n"
        "}\n"
        "fun use() <int> {\n"
        "    let a <Box<int>> = Box::<int>{ val: 1 };\n"
        "    let b <Box<char>> = Box::<char>{ val: 'x' };\n"
        "    return a.get();\n"
        "}\n");
    EXPECT_EQ(occurrences(trace, "declared Box<int>.get"), 1u) << trace;
    EXPECT_EQ(occurrences(trace, "declared Box<char>.get"), 1u) << trace;
}

BACKEND_TEST(Soundness_Codegen, AGenericStructsMethodAssignsThroughItsInstantiation) {
    // The struct's own substitution has to be live while the body is emitted: `v` is
    // a `T`, and T is whatever this instance bound it to.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun get(self: &Self) <T> { return self.val; }\n"
        "    fun set(v: T) <noret> { self.val = v; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 3 };\n"
        "    b.set(9);\n"
        "    printf(\"%d\\n\", b.get());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericStructsBareNameInAMethodIsItsOwnInstantiation) {
    // struct_methods.fin:21's spelling, inside a template: `<&Box>` and `new Box`
    // with no arguments written, both meaning this instantiation. Keyed on the
    // instance and not on the template, or `new Box` would build a second struct
    // named `Box<T>` with `Box<int>`'s layout -- which is the bug the generic
    // function unit found and named AFunctionBindingSpellsAStructTheWayAWrittenOneDoes.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    pub static fun zero() <&Box> { return new Box{ val: 0 }; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let z <&Box<int>> = Box::<int>::zero();\n"
        "    printf(\"%d\\n\", z.val);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericStructsMethodOnTwoInstantiationsKeepsThemApart) {
    // The strongest form of the same claim: one template, two instances, and the
    // answers have to come from the right layout. `char` and `int` differ in width,
    // so a body emitted under the wrong substitution reads the wrong number of bytes.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun get(self: &Self) <T> { return self.val; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <Box<int>> = Box::<int>{ val: 300 };\n"
        "    let b <Box<char>> = Box::<char>{ val: 'x' };\n"
        "    printf(\"%d %c\\n\", a.get(), b.get());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "300 x\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericStructsIntAndFloatMethodsKeepTheirBodies) {
    // The `Box<int>` + `Box<float>` mixing shape: one template, two method
    // bodies, and the arithmetic in each has to be its own type's. An engine
    // that shared one body would emit one add and be wrong about the other.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun twice(self: &Self) <T> { return self.val + self.val; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <Box<int>> = Box::<int>{ val: 21 };\n"
        "    let b <Box<float>> = Box::<float>{ val: 1.5 };\n"
        "    printf(\"%d %f\\n\", a.twice(), b.twice());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42 3.000000\n") << b.why();
}

// ---------------------------------------------------------------------------
// A `::` call whose target is a generic struct written without its type arguments:
// `Vec2::zero()` and `Vec2::from_angle(0.7854)` (tests/samples/letssee.fin:77, :59).
//
// Three of the corpus's `::` calls are spelled this way and all three were refused,
// because the target codegen has to map is the bare template and the answer to "which
// instantiation" is not in the call. It is in the annotation on the left of the line, or
// in the types of the arguments -- and the analyzer reads both, so the analyzer is where
// the question is answered and records it (StaticMethodCall::resolved_target). The
// backend maps that node instead of the written one when it is there, and maps the
// written one when it is not, which is what keeps every spelling that already worked on
// exactly the path it was on.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AStaticCallOnAGenericTargetTakesItsTypeArgumentFromTheAnnotation) {
    // letssee.fin:77, `let zero <Vec2<float>> = Vec2::zero();`. Nothing in the call names
    // a type: the annotation is the whole of the evidence, and an annotation is not
    // something this pass can see -- which is why the front end has to hand it over.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "pub:\n"
        "    v <T> = 0,\n"
        "    static fun zero() <&Self> { return new Self{}; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let z <&Box<int>> = Box::zero();\n"
        "    printf(\"%d\\n\", z.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedStaticCallOnAGenericTargetResolvesItsInnerCall) {
    // The inner `Box::zero()` names no type of its own; what its Self is comes from
    // the outer call's instantiation, which the annotation seeds. `clone` returns
    // its argument, so a wrong instantiation would read the default field wrong --
    // and `of` below pins the value so the read proves which instance it was.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "pub:\n"
        "    v <T> = 0,\n"
        "    static fun zero() <&Self> { return new Self{}; }\n"
        "    static fun clone(s: &Self) <&Self> { return s; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let z <&Box<int>> = Box::clone(Box::zero());\n"
        "    printf(\"%d\\n\", z.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AChainedCallOnAGenericTargetResolvesItsReceiver) {
    // `Box::zero().get()` with the hint on the whole chain: the receiver names
    // no type of its own, so what its Self is comes from the outer `get`
    // instantiation, which the annotation seeds. A template kept through the
    // receiver would refuse with `expected 'int', got 'T'` instead of lowering
    // `Box<int>` and reading its default field.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "pub:\n"
        "    v <T> = 0,\n"
        "    static fun zero() <&Self> { return new Self{}; }\n"
        "    fun get(self: &Self) <T> { return self.v; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let x <int> = Box::zero().get();\n"
        "    printf(\"%d\\n\", x);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStaticCallOnAGenericTargetInfersItsTypeArgumentFromAnArgument) {
    // letssee.fin:59's other half: the argument decides. `char` rather than `int` so the
    // answer is a width the wrong instantiation would get wrong -- 65 read out of an i32
    // field is still 65, and out of the wrong field is not.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "pub:\n"
        "    v <T> = 0,\n"
        "    static fun of(x: T) <&Self> { return new Self{v: x}; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <&Box<int>> = Box::of(7);\n"
        "    let c <&Box<char>> = Box::of('A');\n"
        "    printf(\"%d %c\\n\", a.v, c.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7 A\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStaticCallOnAGenericTargetInfersItsTypeArgumentFromASelfArgument) {
    // letssee.fin:73, `Vec2::normalize(scaled)` -- a static method whose parameter is
    // `&Self`, so the receiver arrives as an ordinary argument and the instantiation comes
    // from it. Asserted by mutation rather than by a return value: `bump` writes through
    // the pointer, so a call that lowered against some other instantiation would either
    // refuse or write at the wrong offset, and 42 is what says it did neither.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "pub:\n"
        "    v <T> = 0,\n"
        "    static fun of(x: T) <&Self> { return new Self{v: x}; }\n"
        "    static fun bump(s: &Self) <noret> { s.v = s.v + 1; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <&Box<int>> = Box::of(41);\n"
        "    Box::bump(a);\n"
        "    printf(\"%d\\n\", a.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStaticCallOnAGenericTargetInsideATemplateResolvesAtEachInstantiation) {
    // The claim that makes recording an answer on an AST node sound at all. Codegen does
    // not clone a template's body -- `Emitter::instantiateGeneric` emits the same nodes
    // once per instantiation, under a substitution -- so the one `Box::of(x)` here is read
    // twice, and a concrete type stamped on it would be right for at most one of those
    // readings. What is recorded instead is `Box<T>`: a parameter spelled as its own name,
    // which the mapper resolves through whichever substitution is live, exactly as it does
    // for the hand-written `Box<T>` on the line above it.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "pub:\n"
        "    v <T> = 0,\n"
        "    static fun of(x: T) <&Self> { return new Self{v: x}; }\n"
        "    fun get(self: &Self) <T> { return self.v; }\n"
        "}\n"
        "fun wrap<T>(x: T) <T> {\n"
        "    let b <&Box<T>> = Box::of(x);\n"
        "    return b.get();\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %c\\n\", wrap(7), wrap('A'));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7 A\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStaticCallOnAGenericTargetReachesAMethodOfTheInstantiationItResolved) {
    // Nested, and from inside a generic struct's own method: `Box<&Box<int>>` built by a
    // `::` call whose argument is itself a `&Box<int>`, and `Pair<A>::mk` calling
    // `Box::of(self.one)` where the `A` it resolves to is the struct's parameter rather
    // than a function's. Two ways for the recorded node to be read under a substitution
    // that is not the one it was recorded in, and both answers are still the right ones.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "pub:\n"
        "    v <T> = 0,\n"
        "    static fun of(x: T) <&Self> { return new Self{v: x}; }\n"
        "    fun get(self: &Self) <T> { return self.v; }\n"
        "}\n"
        "struct Pair<A> {\n"
        "pub:\n"
        "    one <A> = 0,\n"
        "    fun mk(self: &Self) <&Box<A>> { return Box::of(self.one); }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let inner <&Box<int>> = Box::of(3);\n"
        "    let outer <&Box<&Box<int>>> = Box::of(inner);\n"
        "    let back <&Box<int>> = outer.get();\n"
        "    let p <Pair<char>> = Pair::<char>{one: 'B'};\n"
        "    let made <&Box<char>> = p.mk();\n"
        "    printf(\"%d %c\\n\", back.get(), made.get());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3 B\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStaticCallOnAGenericTargetWithNothingToInferFromIsRefused) {
    // The negative half of the annotation test: the same call with its result discarded,
    // so there is no annotation, no argument, and nothing else in the program that says
    // which `Box` this is. The analyzer records nothing, and the backend gives the answer
    // it gave every one of these before this unit rather than choosing an instantiation.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "pub:\n"
        "    v <T> = 0,\n"
        "    static fun zero() <&Self> { return new Self{}; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    Box::zero();\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("no type arguments"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStaticCallOnAGenericTargetDoesNotBorrowItsCallersTypeParameter) {
    // `bad<T>` has a `T` and `Box<T>` has a `T`, and they are different parameters that
    // share a spelling. Nothing here binds Box's, so the recorded node -- if it were
    // recorded by name -- would be resolved against `bad`'s substitution and `Box::zero()`
    // would lower as `Box<int>`, an instantiation nobody in this program asked for. The
    // parameter is compared by identity against what is actually in scope at the call, so
    // this refuses; `bad` is called, because a template nobody instantiates is never
    // emitted and would pass this test for the wrong reason.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "pub:\n"
        "    v <T> = 0,\n"
        "    static fun zero() <&Self> { return new Self{}; }\n"
        "}\n"
        "fun bad<T>(x: T) <noret> {\n"
        "    Box::zero();\n"
        "}\n"
        "fun main() <noret> {\n"
        "    bad(7);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("no type arguments"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStaticCallsTurbofishAfterTheMethodIsStillRefused) {
    // `maybe::unpack::<A>(myvar)` (tests/samples/nullifier.fin:28) -- the fourth `::`
    // spelling, where the type arguments sit after the method name and bind the *method*'s
    // parameters rather than the target's. A separate front-end question from this unit's:
    // `StaticMethodCall::generic_args` is not read by the inference that fills
    // `resolved_target` in, so the target of this call is still the bare template and the
    // backend still says so. Booked here so the day it is implemented, this test is what
    // says the refusal is gone.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "pub:\n"
        "    v <T> = 0,\n"
        "    static fun make(x: T) <&Self> { return new Self{v: x}; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <&Box<int>> = Box::make::<int>(9);\n"
        "    printf(\"%d\\n\", b.v);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("explicit generic arguments"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericMethodIsInstantiatedAtItsCall) {
    // `fun set_x<U>(new_x: U)` -- struct_methods.fin:14. Two substitutions at once,
    // the struct's and the call's, which is what makes this its own unit: `T` comes
    // from the receiver's instantiation and `U` from the argument, and the body needs
    // both bound at the same time. This test was AGenericMethodIsRefusedAtItsCall and
    // asserted the refusal; the boundary moved, so it is inverted rather than deleted.
    //
    // The value is printed rather than an "ok", because the refusal it replaces could
    // only be wrong in one way and a lowering can be wrong in two: `self.val = new_x`
    // has to reach the field `T` laid out for and take the value `U` was inferred as.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun set_x<U>(new_x: U) <noret> { self.val = new_x; }\n"
        "    fun get(self: &Self) <T> { return self.val; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 1 };\n"
        "    b.set_x(5);\n"
        "    printf(\"%d\\n\", b.get());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericMethodNobodyCallsIsNotRefused) {
    // The other side of the pair, and struct_methods.fin's own shape: the method
    // generic is declared, the struct is instantiated, and nothing calls it. A
    // declaration that emits nothing is not a skipped body -- there is no body to
    // emit until a call says what U is.
    const Compiled c = compileOnly(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun set_x<U>(new_x: U) <noret> { self.val = new_x; }\n"
        "    fun get(self: &Self) <T> { return self.val; }\n"
        "}\n"
        "fun use() <int> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 1 };\n"
        "    return b.get();\n"
        "}\n");
    EXPECT_EQ(c.exitCode, 0) << c.why();
    // Named after the input's stem, in the working directory, because no -o was given.
    std::error_code ec;
    fs::remove(c.object, ec);
}

BACKEND_TEST(Soundness_Codegen, AGenericMethodsTwoSubstitutionsAreBothLive) {
    // The claim the whole unit rests on, with a wrong answer rather than a refusal as
    // its failure mode. `T` is char and `U` is int, so they cannot be confused for each
    // other: `cast<T>(new_x)` needs the struct's binding and `new_x` needs the method's,
    // in one statement. A composition that *replaced* the struct's bindings with the
    // method's would store four bytes into a one-byte field, and the sample this is
    // taken from -- struct_methods.fin:14, `self.x = cast<T>(new_x);` -- is written that
    // way precisely because the two are different types.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun set_x<U>(new_x: U) <noret> { self.val = cast<T>(new_x); }\n"
        "    fun get(self: &Self) <T> { return self.val; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<char>> = Box::<char>{ val: 'a' };\n"
        "    b.set_x(98);\n"
        "    printf(\"%c\\n\", b.get());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "b\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericMethodsArgumentIsEvaluatedOnce) {
    // The instance is inferred *from* the argument's type, so the argument has to be
    // emitted before the function it will be passed to exists -- and then converted
    // rather than emitted a second time. An implementation that reached for the shared
    // emitCallArgs after instantiating would evaluate this argument twice, which is not
    // a missing feature but a program that counts to two where it was written to count
    // to one. Observable only through a side effect, which is why the counter is a
    // module-scope `let` and not a local.
    const Built b = build(std::string(kPrintf) +
        "let calls <int> = 0;\n"
        "fun next() <int> { calls = calls + 1; return 41; }\n"
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun set_x<U>(new_x: U) <noret> { self.val = new_x; }\n"
        "    fun get(self: &Self) <T> { return self.val; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 0 };\n"
        "    b.set_x(next());\n"
        "    printf(\"%d %d\\n\", b.get(), calls);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "41 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, OneGenericMethodAtTwoTypesIsTwoInstances) {
    // One struct instantiation, one method template, two calls at different argument
    // types. The key carries *both* substitutions -- `Box<int>.echo<char>` and
    // `Box<int>.echo<int>` -- and a key that carried only the struct's would make the
    // second call find the first instance and truncate.
    //
    // The narrow call comes *first*, and that ordering is the whole test. With the wide
    // one first, reusing its instance for a char argument widens and then prints through
    // `%c`, which round-trips: the wrong instance gives the right characters and the
    // test cannot fail. Narrow first, reuse means 300 arrives as 44.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun echo<U>(v: U) <U> { return v; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 1 };\n"
        "    printf(\"%c %d\\n\", b.echo('z'), b.echo(300));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "z 300\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASetUStyleMethodCalledAtTwoTypesKeepsBoth) {
    // The `set<U>` shape on one receiver: `Box<int>.set_x<char>` then
    // `Box<int>.set_x<int>`. A key that carried only the struct's half would
    // make the second call reuse the char instance and truncate 300 to 44.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun set_x<U>(new_x: U) <noret> { self.val = new_x; }\n"
        "    fun get(self: &Self) <T> { return self.val; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 0 };\n"
        "    b.set_x('z');\n"
        "    printf(\"%d\\n\", b.get());\n"
        "    b.set_x(300);\n"
        "    printf(\"%d\\n\", b.get());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "122\n300\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericMethodsInstanceIsNamedForBothSubstitutions) {
    // `Box<int>.set_x<int>`, and the name is the assertion. It is the least mangling
    // that keeps the instances apart -- the struct's arguments and the method's, in the
    // order they were declared -- and it is what this file already does for a generic
    // free function (`ident<int>`) and for a method (`Box<int>.get`), on the same
    // grounds: the only reader of a Fin symbol is a person reading `nm` output.
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun set_x<U>(new_x: U) <noret> { self.val = new_x; }\n"
        "}\n"
        "fun use() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 1 };\n"
        "    b.set_x(5);\n"
        "}\n");
    EXPECT_NE(trace.find("declared Box<int>.set_x<int>"), std::string::npos) << trace;
    // And once, not twice: asking for the same instance a second time has to find the
    // first rather than start a second definition of it.
    EXPECT_EQ(occurrences(trace, "declared Box<int>.set_x<int>"), 1u) << trace;
}

BACKEND_TEST(Soundness_Codegen, AGenericMethodIsOneSymbolAcrossTwoObjects) {
    // The same bargain AStructMethodIsOneSymbolAcrossTwoObjects strikes, one layer
    // further along: two objects that each declare the struct and each make this call
    // both publish `Box<int>.set_x<int>`, and neither knows the other exists. An
    // external definition in each would make the link fail for a program that is
    // correct, which is why the instance is emitted linkonce_odr.
    const std::string decl =
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun set_x<U>(new_x: U) <noret> { self.val = new_x; }\n"
        "    fun get(self: &Self) <T> { return self.val; }\n"
        "}\n";
    const fs::path libObj = uniqueTempPath("fin_lib_gm", ".o");
    const fs::path mainObj = uniqueTempPath("fin_main_gm", ".o");
    const fs::path exe = uniqueTempPath("fin_linked_gm");
    const fs::path outPath = uniqueTempPath("fin_linked_gm_out");

    const Compiled lib = compileOnly(decl +
        "fun side() <int> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 0 };\n"
        "    b.set_x(20);\n"
        "    return b.get();\n"
        "}\n", libObj);
    ASSERT_EQ(lib.exitCode, 0) << lib.why();
    const Compiled mainPart = compileOnly(std::string(kPrintf) + decl +
        "@define side() <int>;\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 0 };\n"
        "    b.set_x(22);\n"
        "    printf(\"%d\\n\", b.get() + side());\n"
        "}\n", mainObj);
    ASSERT_EQ(mainPart.exitCode, 0) << mainPart.why();

    ASSERT_EQ(fin::runProcess(fin::linkCommand(
        {libObj.string(), mainObj.string()}, exe.string())), 0);

    EXPECT_EQ(fin::runProcess({exe.string()}, outPath.string(), outPath.string()), 0);
    EXPECT_EQ(readProcessOutput(outPath.string()), "42\n");

    std::error_code ec;
    for (const fs::path& q : {libObj, mainObj, exe, outPath}) fs::remove(q, ec);
}

BACKEND_TEST(Soundness_Codegen, AGenericMethodOnAPlainStructNeedsNoStructArguments) {
    // A method template on a struct that is not one. The composition still happens --
    // `Self` is in the struct's bindings whether or not it has type arguments -- and the
    // instance is named for the method's substitution alone, because that is all there
    // is. The struct's half being empty must not be the same code path as it being
    // absent.
    const Built b = build(std::string(kPrintf) +
        "struct Holder {\n"
        "    n <int>,\n"
        "    fun keep<U>(v: U) <U> { return v; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let h <Holder> = Holder{ n: 1 };\n"
        "    printf(\"%d\\n\", h.keep(9));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodTypeParameterThatShadowsTheStructsIsRefused) {
    // The one refusal in this unit that is not a missing feature. TypeMapper::bound-
    // Binding returns the *first* match in the substitution list and the composition
    // appends, so a method `<T>` on a `Box<T>` would silently resolve to the struct's T
    // and instantiate at the field's type whatever the argument was -- a wrong answer,
    // and the only shape in this unit that could produce one. Shadowing is the other
    // possible answer and is not the backend's to choose: struct_methods.fin:14 says of
    // `set_x<U>` that "its separated from the struct generic itself so it cant have the
    // same name as `T`", which is the corpus ruling that this shape is not written.
    // `Box<char>` and an int argument that fits char, so that dropping the check is a
    // *wrong answer* and not a coincidence: the struct's T wins and `echo` becomes
    // char-to-char.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun echo<T>(v: T) <T> { return v; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<char>> = Box::<char>{ val: 'a' };\n"
        "    printf(\"%d\\n\", b.echo(100));\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("echo"), std::string::npos) << b.why();
    EXPECT_NE(b.compileErr.find("'T'"), std::string::npos) << b.why();
    EXPECT_EQ(b.out, "") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericMethodsUnmentionedTypeParameterIsRefused) {
    // Nothing to infer `U` from: no parameter mentions it. Refused naming the parameter,
    // because the alternative is picking a type -- and a method instantiated at a type
    // the program never named is a method the program did not write. The same refusal a
    // generic free function gets (`fun nothing<T>() <int>`), and the turbofish that
    // would fix it is refused a few lines earlier, so there is no second way in.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun blank<U>() <int> { return 1; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 1 };\n"
        "    printf(\"%d\\n\", b.blank());\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("blank"), std::string::npos) << b.why();
    EXPECT_NE(b.compileErr.find("'U'"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ATurbofishOnAMethodCallIsStillRefused) {
    // `b.set_x::<int>(5)` -- the type arguments written rather than inferred. Refused,
    // and not because it is hard: a free function's turbofish binds nothing in
    // Analyzer_Expr (booked), so the backend reads its own, and no corpus site writes
    // one on a *method*. Inference is what every site there does have, so the untested
    // half is the half that is refused rather than the half that is guessed at.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun set_x<U>(new_x: U) <noret> { self.val = new_x; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 1 };\n"
        "    b.set_x::<int>(5);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("set_x"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructMethodIsOneSymbolAcrossTwoObjects) {
    // Two objects that each declare the struct and each use the method both publish
    // `Point.get`, and there is no third place to put it -- the same bargain a
    // generic function instance strikes (AGenericFunctionsInstanceIsOneSymbolAcross-
    // TwoObjects). An external definition in each would make the link fail for a
    // program that is correct, which is why a method is emitted linkonce_odr.
    const std::string decl =
        "struct Point {\n"
        "    x <int>,\n"
        "    fun get(self: &Self) <int> { return self.x; }\n"
        "}\n";
    const fs::path libObj = uniqueTempPath("fin_lib_sm", ".o");
    const fs::path mainObj = uniqueTempPath("fin_main_sm", ".o");
    const fs::path exe = uniqueTempPath("fin_linked_sm");
    const fs::path outPath = uniqueTempPath("fin_linked_sm_out");

    const Compiled lib = compileOnly(decl +
        "fun side() <int> {\n"
        "    let p <Point> = Point{ x: 20 };\n"
        "    return p.get();\n"
        "}\n", libObj);
    ASSERT_EQ(lib.exitCode, 0) << lib.why();
    const Compiled mainPart = compileOnly(std::string(kPrintf) + decl +
        "@define side() <int>;\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point{ x: 22 };\n"
        "    printf(\"%d\\n\", p.get() + side());\n"
        "}\n", mainObj);
    ASSERT_EQ(mainPart.exitCode, 0) << mainPart.why();

    ASSERT_EQ(fin::runProcess(fin::linkCommand(
        {libObj.string(), mainObj.string()}, exe.string())), 0);

    EXPECT_EQ(fin::runProcess({exe.string()}, outPath.string(), outPath.string()), 0);
    EXPECT_EQ(readProcessOutput(outPath.string()), "42\n");

    std::error_code ec;
    for (const fs::path& q : {libObj, mainObj, exe, outPath}) fs::remove(q, ec);
}

// --- A struct operator ------------------------------------------------------
//
// tests/samples/operators.fin is the normative sample and it declares two operators
// and applies neither:
//
//     struct MyInt {
//         val <int>,
//         // Operator with Generic
//         operator + : <T>(other: <T>) <int> {
//             return self.val + cast<int>(other);
//         }
//         // Normal Operator
//         operator -(other: <int>) <int> {
//             return self.val + other;
//         }
//     }
//
// tests/samples/deeptest1.fin:11 is where one is applied:
//
//     pub operator + (other: Vector2) <Vector2> {
//         return Vector2 { x: self.x + other.x, y: self.y + other.y };
//     }
//     ...
//     let v3 <Vector2> = v1 + v2; // Should resolve to Vector2
//
// An operator is a method with two differences, and everything else about it -- the
// receiver, the deferred body, one instance per instantiation, linkonce_odr -- is the
// method unit's and is not re-tested here.
//
// 1. Its name is spelled from its token. `Struct.operator+`, because an
//    OperatorDeclaration carries an ASTTokenKind and not a string, so the backend needs
//    a speller either way -- the diagnostic it used to emit could not name the operator
//    it was refusing.
//
// 2. It is reached by writing it and not by naming it. Which means the *left* operand
//    decides: `v + 1` looks on V and `1 + v` does not look at all (the analyzer refuses
//    that one outright -- "Type mismatch: expected 'int', got 'V'" -- so there is
//    nothing here to decide). And it means the lookup has to happen without disturbing
//    `1 + 2`, which is why it is gated on the program having declared an operator with
//    that token at all: a program with no struct operator in it emits exactly the IR it
//    emitted before this unit existed.
//
// A generic operator, and one bound by `implements`, are declared and lowered by
// nobody -- the same two exemptions a method has, for the same two reasons -- so
// applying one refuses and declaring one is not a refusal. operators.fin declares a
// generic operator, and hashmap.fin:50 (`operator[] implements cast<fn(Self, T)>(__get)`)
// is the other.

BACKEND_TEST(Soundness_Codegen, AStructsOperatorIsCalledByWritingIt) {
    // deeptest1.fin:11-16 and :31 in shape: the operator returns the struct by value,
    // which is the ordinary Fin-to-Fin aggregate return.
    const Built b = build(std::string(kPrintf) +
        "struct Vector2 {\n"
        "    x <int>,\n"
        "    y <int> = 10,\n"
        "    pub operator + (other: Vector2) <Vector2> {\n"
        "        return Vector2{ x: self.x + other.x, y: self.y + other.y };\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let v1 <Vector2> = Vector2 { x: 1, y: 2 };\n"
        "    let v2 <Vector2> = Vector2 { x: 3, y: 4 };\n"
        "    let v3 <Vector2> = v1 + v2;\n"
        "    printf(\"%d %d\\n\", v3.x, v3.y);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "4 6\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnOperatorsSymbolIsSpelledFromItsToken) {
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "struct V {\n"
        "    x <int>,\n"
        "    pub operator + (o: V) <int> { return self.x + o.x; }\n"
        "    pub operator - (o: V) <int> { return self.x - o.x; }\n"
        "    pub operator == (o: V) <bool> { return self.x == o.x; }\n"
        "    pub operator < (o: V) <bool> { return self.x < o.x; }\n"
        "}\n");
    EXPECT_NE(trace.find("declared V.operator+"), std::string::npos) << trace;
    EXPECT_NE(trace.find("declared V.operator-"), std::string::npos) << trace;
    EXPECT_NE(trace.find("declared V.operator=="), std::string::npos) << trace;
    EXPECT_NE(trace.find("declared V.operator<"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, AnOperatorReadsTheLeftOperandThroughItsReceiver) {
    // The receiver is a pointer, as a method's is, so `self.x` is a load through it
    // rather than a field of a copy. Written as a mutating operator on purpose: if the
    // receiver were a copy, this would print the operand unchanged and nothing would
    // say so.
    const Built b = build(std::string(kPrintf) +
        "struct V {\n"
        "    x <int>,\n"
        "    pub operator + (o: int) <int> { self.x = self.x + o; return self.x; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let v <V> = V{ x: 1 };\n"
        "    let a <int> = v + 4;\n"
        "    printf(\"%d %d\\n\", a, v.x);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "5 5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnOperatorTakesItsOtherOperandByValue) {
    // `other: Vector2` is a struct parameter, which between two Fin functions is an
    // LLVM aggregate passed by value -- the same as any other struct parameter, and
    // deliberately not the C ABI's answer (declareFunction refuses that across an
    // `@define` boundary).
    const Built b = build(std::string(kPrintf) +
        "struct V {\n"
        "    a <int>,\n"
        "    b <int>,\n"
        "    pub operator + (o: V) <int> { return self.a * 1000 + o.a * 100 + o.b; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <V> = V{ a: 1, b: 2 };\n"
        "    let q <V> = V{ a: 3, b: 4 };\n"
        "    printf(\"%d\\n\", p + q);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "1304\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AComparisonOperatorReturningBoolDrivesAnIf) {
    const Built b = build(std::string(kPrintf) +
        "struct V {\n"
        "    x <int>,\n"
        "    pub operator == (o: V) <bool> { return self.x == o.x; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <V> = V{ x: 1 };\n"
        "    let b <V> = V{ x: 1 };\n"
        "    let c <V> = V{ x: 2 };\n"
        "    if (a == b) { printf(\"same\\n\"); }\n"
        "    if (a == c) { printf(\"wrong\\n\"); } else { printf(\"different\\n\"); }\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "same\ndifferent\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoStructsWithTheSameOperatorKeepTheirOwn) {
    const Built b = build(std::string(kPrintf) +
        "struct V { x <int>, pub operator + (o: V) <int> { return self.x + o.x; } }\n"
        "struct W { y <int>, pub operator + (o: W) <int> { return self.y * o.y; } }\n"
        "fun main() <noret> {\n"
        "    let a <V> = V{ x: 3 };\n"
        "    let b <V> = V{ x: 4 };\n"
        "    let c <W> = W{ y: 3 };\n"
        "    let d <W> = W{ y: 4 };\n"
        "    printf(\"%d %d\\n\", a + b, c + d);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "7 12\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnOperatorIsFoundThroughAFieldsAddress) {
    // The receiver comes from baseAddress, so every base a field access copes with a
    // written operator copes with too: a field, and a pointer with one load in between.
    const Built b = build(std::string(kPrintf) +
        "struct V { x <int>, pub operator + (o: V) <int> { return self.x + o.x; } }\n"
        "struct Outer { inner <V> }\n"
        "fun main() <noret> {\n"
        "    let v <V> = V{ x: 10 };\n"
        "    let o <Outer> = Outer{ inner: V{ x: 5 } };\n"
        "    let p <&V> = &v;\n"
        "    printf(\"%d %d\\n\", o.inner + v, *p + o.inner);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "15 15\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnOperatorOnAValueWithNoHomeIsRefused) {
    // `make() + v`. The same refusal a method call on a temporary gets, and the same
    // reason: the receiver is a pointer because an operator may assign through it, a
    // value returned by a call has no address, and copying it to a temporary would make
    // a mutating operator silently mutate the copy.
    const Built b = build(std::string(kPrintf) +
        "struct V { x <int>, pub operator + (o: V) <int> { return self.x + o.x; } }\n"
        "fun make() <V> { return V{ x: 7 }; }\n"
        "fun main() <noret> {\n"
        "    let v <V> = V{ x: 1 };\n"
        "    printf(\"%d\\n\", make() + v);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("left operand"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnOperatorOnAGenericStructIsEmittedPerInstantiation) {
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    pub operator + (o: Box<T>) <T> { return self.val + o.val; }\n"
        "}\n"
        "fun use() <int> {\n"
        "    let a <Box<int>> = Box::<int>{ val: 1 };\n"
        "    let b <Box<int>> = Box::<int>{ val: 2 };\n"
        "    let c <Box<long>> = Box::<long>{ val: 3 };\n"
        "    let d <Box<long>> = Box::<long>{ val: 4 };\n"
        "    return cast<int>(a + b) + cast<int>(c + d);\n"
        "}\n");
    EXPECT_EQ(occurrences(trace, "declared Box<int>.operator+"), 1u) << trace;
    EXPECT_EQ(occurrences(trace, "declared Box<long>.operator+"), 1u) << trace;
}

BACKEND_TEST(Soundness_Codegen, AGenericStructsOperatorRunsAtItsOwnInstantiation) {
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    pub operator + (o: Box<T>) <T> { return self.val + o.val; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <Box<int>> = Box::<int>{ val: 20 };\n"
        "    let b <Box<int>> = Box::<int>{ val: 22 };\n"
        "    printf(\"%d\\n\", a + b);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructWithNoOperatorForThatTokenStillRefuses) {
    // The struct declares `+` and the program writes `-`. Nothing is found, and the
    // built-in path is where the refusal comes from -- an arithmetic operator on a
    // struct, which is what it always said.
    const Built b = build(std::string(kPrintf) +
        "struct V { x <int>, pub operator + (o: V) <int> { return self.x + o.x; } }\n"
        "fun main() <noret> {\n"
        "    let a <V> = V{ x: 1 };\n"
        "    let b <V> = V{ x: 2 };\n"
        "    printf(\"%d\\n\", a - b);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("operator"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AProgramWithAStructOperatorStillAddsItsIntegers) {
    // The lookup is gated on the token, and a scalar operand never reaches it. Cheap to
    // state and the thing most worth stating: an overload resolution that leaked into
    // `1 + 2` would be a wrong answer rather than a refusal.
    const Built b = build(std::string(kPrintf) +
        "struct V { x <int>, pub operator + (o: V) <int> { return 999; } }\n"
        "fun main() <noret> {\n"
        "    let n <int> = 1 + 2;\n"
        "    let f <float> = 1.5 + 2.5;\n"
        "    if (f > 4.0) { printf(\"%d big\\n\", n); } else { printf(\"%d small\\n\", n); }\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "3 small\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericOperatorNobodyWritesIsNotARefusal) {
    // operators.fin:15-17. Two substitutions at once -- the struct's and the
    // operator's -- is the method-generic unit, and this is the half of it the
    // normative sample needs: the declaration is not a refusal, because there is no
    // body to lower until something says what T is.
    const Built b = build(std::string(kPrintf) +
        "struct MyInt {\n"
        "    val <int>,\n"
        "    operator + : <T>(other: <T>) <int> {\n"
        "        return self.val + cast<int>(other);\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericOperatorIsInstantiatedWhereItIsWritten) {
    // operators.fin:15-17 verbatim in shape. The operator half of the generic-method
    // unit: `T` comes from the right operand and the struct's bindings come from the
    // left, and writing the operator is the call that fixes both. This test was
    // AGenericOperatorIsRefusedWhereItIsWritten; the boundary moved, so it is inverted.
    const Built b = build(std::string(kPrintf) +
        "struct MyInt {\n"
        "    val <int>,\n"
        "    operator + : <T>(other: <T>) <int> {\n"
        "        return self.val + cast<int>(other);\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let m <MyInt> = MyInt{ val: 1 };\n"
        "    printf(\"%d\\n\", m + 2);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, OneGenericOperatorAtTwoTypesIsTwoInstances) {
    // `MyInt.operator+<char>` and `MyInt.operator+<int>` are two instances of one
    // declaration, keyed the way a generic method's are -- operatorKey's name with the
    // operator's own substitution appended. An operator is a method with a spelled name
    // and its instances need telling apart on exactly the same terms.
    //
    // The char operand first, for the reason OneGenericMethodAtTwoTypesIsTwoInstances
    // gives: reusing a wider instance for a narrower operand is invisible, and reusing a
    // narrower one for 300 is not.
    const Built b = build(std::string(kPrintf) +
        "struct MyInt {\n"
        "    val <int>,\n"
        "    operator + : <T>(other: <T>) <int> {\n"
        "        return self.val + cast<int>(other);\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let m <MyInt> = MyInt{ val: 1 };\n"
        "    printf(\"%d %d\\n\", m + 'A', m + 300);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    // 'A' is 65, so 66; and 301 could not have come from the char instance, which would
    // have truncated 300 to 44 and answered 45.
    EXPECT_EQ(b.out, "66 301\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnOperatorBoundByImplementsIsNotDeclared) {
    // hashmap.fin:50 -- `operator[] implements cast<fn(Self, T)>(__get)`. No body, so
    // there is nothing to emit and nothing is emitted; the binding it describes is a
    // feature of its own, and writing the operator is what refuses.
    const Compiled c = compileOnly(std::string(kPrintf) +
        "@define __get(v: int) <int>;\n"
        "struct V {\n"
        "    x <int>,\n"
        "    pub operator[] implements cast<fn(int)>(__get);\n"
        "}\n"
        "fun use() <int> { return 1; }\n");
    EXPECT_EQ(c.exitCode, 0) << c.why();
    EXPECT_EQ(codegenTrace(std::string(kPrintf) +
        "@define __get(v: int) <int>;\n"
        "struct V {\n"
        "    x <int>,\n"
        "    pub operator[] implements cast<fn(int)>(__get);\n"
        "}\n").find("V.operator["), std::string::npos);
    std::error_code ec;
    fs::remove(c.object, ec);
}

BACKEND_TEST(Soundness_Codegen, ASecondOperatorForOneTokenIsRefused) {
    // Two definitions of one symbol, and declareFunction keeps the first -- so the
    // second body would silently not be the one that runs. Which of the two a use
    // meant is overload resolution, and the analyzer does not have it yet
    // (KnownDefect: "operators have no arity check").
    const Built b = build(std::string(kPrintf) +
        "struct V {\n"
        "    x <int>,\n"
        "    pub operator + (o: V) <int> { return 1; }\n"
        "    pub operator + (o: int) <int> { return 2; }\n"
        "}\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("second operator"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnOperatorWithAWrittenSelfIsAccepted) {
    // An explicitly written self parameter is the receiver, matching method lowering.
    const Built b = build(std::string(kPrintf) +
        "struct V {\n"
        "    x <int>,\n"
        "    pub operator + (self: &Self, o: V) <int> { return self.x + o.x; }\n"
        "}\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnOperatorWithTheWrongArityIsRefusedWhereItIsWritten) {
    // `operator +` taking two operands beside the receiver. There is no second right
    // operand in `v + 1` to fill it, and the analyzer has no arity check for an
    // operator (a booked defect), so this is where it lands.
    const Built b = build(std::string(kPrintf) +
        "struct V {\n"
        "    x <int>,\n"
        "    pub operator + (a: int, b: int) <int> { return a + b; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let v <V> = V{ x: 1 };\n"
        "    printf(\"%d\\n\", v + 1);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("too few arguments"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ACompoundAssignmentToAStructIsStillRefused) {
    // `a += b` is not this path -- visit(BinaryOp&) sends the whole EQUAL family to
    // emitAssignment before any operator lookup happens. Whether `+=` should compose
    // the declared `+` with a store, or want an `operator +=` of its own, is a ruling
    // nobody has made; until then writing it refuses rather than picking one.
    const Built b = build(std::string(kPrintf) +
        "struct V { x <int>, pub operator + (o: V) <V> { return V{ x: self.x + o.x }; } }\n"
        "fun main() <noret> {\n"
        "    let a <V> = V{ x: 1 };\n"
        "    let b <V> = V{ x: 2 };\n"
        "    a += b;\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
}

// ---------------------------------------------------------------------------
// An interface declaration.
// ---------------------------------------------------------------------------
// The whole of an interface's lowering is to emit nothing, and that is *not* the
// skip the rule at the top of this file forbids. The rule is about dropping a
// statement that has runtime meaning: a discarded assignment or call changes what
// the program computes, and does so invisibly. An interface declaration names a
// requirement other types have to satisfy -- no storage is allocated for it, no
// symbol is defined by it, and none of its members is code -- so "emitted
// nothing" and "lowered completely" are the same state, and there is no third
// state in which something was lost. The corpus's two are deeptest1.fin:20 and
// implements_block.fin:5.
//
// What does have runtime meaning is a *body* written inside one. A default method
// is code, and whether an implementor inherits it is an open ruling (§8 of
// docs/HANDOFF.md, "interface-member defaults"), so a body is refused where it is
// written rather than dropped on the way past. That is the line these tests draw:
// the last one is the half that still bites if the first change is ever widened
// into "an interface is always nothing".

BACKEND_TEST(Soundness_Codegen, AnInterfaceDeclarationIsLoweredAsNothing) {
    // deeptest1.fin:20-22, and the program beside it still runs: the point is not
    // that the interface produced something but that it stopped the compile from
    // producing anything.
    const Built b = build(std::string(kPrintf) +
        "interface Printable {\n"
        "    pub fun to_string() <string>;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", 7);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInterfacesMethodIsNotDeclaredAsAFunction) {
    // The other half: "emitted nothing" is a claim about the module, not just about
    // the exit code. A bodiless method that reached declareFunctions would become a
    // declaration with no definition, and the link would fail at the first caller --
    // so the trace has to show it was never declared at all.
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "interface Printable {\n"
        "    pub fun to_string() <string>;\n"
        "}\n"
        "fun main() <noret> { printf(\"%d\\n\", 7); }\n");
    EXPECT_EQ(trace.find("not lowered yet"), std::string::npos) << trace;
    EXPECT_EQ(trace.find("to_string"), std::string::npos) << trace;
    EXPECT_EQ(trace.find("Printable"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, AGenericInterfaceIsLoweredAsNothingToo) {
    // implements_block.fin:5-7. A generic interface has no instantiation to key on
    // and nothing to instantiate, so the type parameter changes nothing about the
    // answer -- which is worth its own test, because every other generic declaration
    // in this file reaches instantiateGeneric and this one must not.
    const Built b = build(std::string(kPrintf) +
        "interface GetVal<T> {\n"
        "    pub fun get_val() <T>;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", 7);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInterfaceOperatorSignatureIsLoweredAsNothing) {
    // implements_block.fin:19-21. An `operator +` with no body is a requirement like
    // any other member; the grammar will not accept a body on one (`expecting
    // SEMICOLON`), so a signature is the only form there is.
    const Built b = build(std::string(kPrintf) +
        "interface Addable<T> {\n"
        "    pub operator + (other: <T>) <T>;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", 7);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInterfaceFieldIsLoweredAsNothing) {
    // A field in an interface is a requirement on an implementor's layout, not
    // storage of its own -- an interface is never the type of a variable here, so
    // there is nothing whose size or offset this could decide. No corpus sample
    // writes one; the grammar accepts it, so it gets an answer rather than a crash.
    const Built b = build(std::string(kPrintf) +
        "interface HasField {\n"
        "    x <int>;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", 7);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInterfaceMethodWithABodyIsRefused) {
    // The line. `{ return 3; }` inside an interface is code, and emitting nothing
    // for the declaration that holds it would discard it -- which is the miscompile
    // the founding rule names, not the harmless nothing above. Refused where it is
    // written, and it stays refused until §8's "interface-member defaults" ruling
    // says who inherits it.
    const Built b = build(std::string(kPrintf) +
        "interface Greeter {\n"
        "    pub fun hello() <int> { return 3; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", 7);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
    EXPECT_NE(b.compileErr.find("hello"), std::string::npos) << b.why();
}

// ---------------------------------------------------------------------------
// An interface as the type of a value: the two-word reference.
// ---------------------------------------------------------------------------
// The section above is about an interface declaration, which lowers to nothing.
// This one is about an interface *name in a type position*, which lowers to
// something: ADR 0019's two words, `{data: i8*, vtable: i8**}` (ADR 0027 fixes
// the layout). `convert` builds the pair by taking the implementor's address and
// the `linkonce_odr` table keyed on (implementor, interface); a method call loads
// the slot and calls through it; a *field* read loads the i64 byte offset the
// table's leading slots hold and byte-GEPs the data word.
//
// Commit `aea960e` built all of that and shipped no test that asserts a value
// through one -- its message body is empty too, so these tests are the record.
// `tests/samples/love.fin` is the corpus's only witness and it exercises two
// shapes; the rest below are the shapes the machinery has to get right for that
// one to be right, each measured against a run rather than a compile.
//
// The refusals at the end are the four edges ADR 0027 explicitly left undecided.
// Three are refused by name and one is a front-end type error; none of them is
// silently miscompiled, which is what these assert.

BACKEND_TEST(Soundness_Codegen, AnInterfaceTypedParameterCallsTheImplementorsMethod) {
    // love.fin's shape, and the one every other test here is a variation of: the
    // callee names the interface, the caller passes a struct, and the value the
    // implementor's body computes is what comes back. A vtable slot resolved to the
    // wrong function would still compile and still link.
    const Built b = build(std::string(kPrintf) +
        "interface Speaker { pub fun speak() <int>; }\n"
        "struct Dog { n <int> }\n"
        "Dog implements <Speaker> {\n"
        "    pub fun speak() <int> { return self.n; }\n"
        "}\n"
        "fun hear(s: Speaker) <int> { return s.speak(); }\n"
        "fun main() <noret> {\n"
        "    let d <Dog> = Dog { n: 42 };\n"
        "    printf(\"%d\\n\", hear(d));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInterfaceTypedLocalCallsTheImplementorsMethod) {
    // The same conversion at a `let` rather than at an argument. Worth its own test
    // because the two go through different code: an argument is converted against a
    // parameter's declared type, a local against its own annotation.
    const Built b = build(std::string(kPrintf) +
        "interface Speaker { pub fun speak() <int>; }\n"
        "struct Dog { n <int> }\n"
        "Dog implements <Speaker> {\n"
        "    pub fun speak() <int> { return self.n; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let d <Dog> = Dog { n: 42 };\n"
        "    let s <Speaker> = d;\n"
        "    printf(\"%d\\n\", s.speak());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStructFieldOfAnInterfaceTypeHoldsTheReference) {
    // The reference is two words wide, so a struct that has one as a field has to
    // reserve both -- `TypeMapper::map` answering with a single pointer would give
    // Holder the wrong size and put the vtable word wherever the next field is.
    const Built b = build(std::string(kPrintf) +
        "interface Speaker { pub fun speak() <int>; }\n"
        "struct Dog { n <int> }\n"
        "Dog implements <Speaker> {\n"
        "    pub fun speak() <int> { return self.n; }\n"
        "}\n"
        "struct Holder { s <Speaker> }\n"
        "fun main() <noret> {\n"
        "    let d <Dog> = Dog { n: 5 };\n"
        "    let h <Holder> = Holder { s: d };\n"
        "    printf(\"%d\\n\", h.s.speak());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AssigningToAnInterfaceVariableRebindsBothWords) {
    // Two implementors of the same interface share a vtable, so an assignment that
    // wrote only the data word would still print the right answer here. The point is
    // that the store is a store of the whole pair: `s = e` after `s = d` has to leave
    // no word of the first behind.
    const Built b = build(std::string(kPrintf) +
        "interface Speaker { pub fun speak() <int>; }\n"
        "struct Dog { n <int> }\n"
        "Dog implements <Speaker> {\n"
        "    pub fun speak() <int> { return self.n; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let d <Dog> = Dog { n: 1 };\n"
        "    let e <Dog> = Dog { n: 2 };\n"
        "    let s <Speaker> = d;\n"
        "    s = e;\n"
        "    printf(\"%d\\n\", s.speak());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFieldRequiredByAnInterfaceIsReadThroughTheReference) {
    // The other half of ADR 0027's table, and the half a method call never touches:
    // the leading slots hold one i64 *byte offset* per required field, and a read is
    // a load of the offset followed by a byte-GEP on the data word. love.fin does not
    // exercise this; `interface Named { pub name <int>; }` is the smallest thing that
    // does.
    const Built b = build(std::string(kPrintf) +
        "interface Named { pub name <int>; }\n"
        "struct Cat { name <int> }\n"
        "Cat implements <Named> { }\n"
        "fun main() <noret> {\n"
        "    let c <Cat> = Cat { name: 9 };\n"
        "    let n <Named> = c;\n"
        "    printf(\"%d\\n\", n.name);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ARequiredFieldAtANonZeroOffsetIsReadThroughTheReference) {
    // The offset has to be *the implementor's*, not the requirement's position. `b` is
    // the interface's only field, so a table that stored 0 -- or that numbered slots by
    // the interface's own field order -- would read `pad` and print 99.
    const Built b = build(std::string(kPrintf) +
        "interface HasB { pub b <int>, }\n"
        "struct S: <HasB> { pad <int>, b <int> }\n"
        "fun main() <noret> {\n"
        "    let s <S> = S { pad: 99, b: 5 };\n"
        "    let y <HasB> = s;\n"
        "    printf(\"%d\\n\", y.b);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoInterfacesOnOneStructGetTheirOwnTables) {
    // The table is keyed per (implementor, interface) pair, not per implementor, and
    // this is the test that says so: one struct, two references live at once, each
    // naming a different field. A single table shared between them would make one of
    // the two reads pick up the other's offset.
    const Built b = build(std::string(kPrintf) +
        "interface HasA { pub a <int>, }\n"
        "interface HasB { pub b <int>, }\n"
        "struct S: <HasA, HasB> { a <int>, b <int> }\n"
        "fun main() <noret> {\n"
        "    let s <S> = S { a: 3, b: 4 };\n"
        "    let x <HasA> = s;\n"
        "    let y <HasB> = s;\n"
        "    printf(\"%d %d\\n\", x.a, y.b);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3 4\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMethodCalledThroughAReferenceWritesToTheOriginal) {
    // The data word is the *implementor's* address, not a copy of it, so a method that
    // mutates `self` through the reference has to change the object the caller still
    // holds. A conversion that spilled the struct into a fresh slot would compile,
    // link, run and print 0 -- the reference would be a copy and no test that only
    // reads could tell.
    const Built b = build(std::string(kPrintf) +
        "interface Settable { pub fun set(n: int) <noret>; }\n"
        "struct C { v <int> }\n"
        "C implements <Settable> {\n"
        "    pub fun set(n: int) <noret> { self.v = n; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let c <C> = C { v: 0 };\n"
        "    let s <Settable> = c;\n"
        "    s.set(11);\n"
        "    printf(\"%d\\n\", c.v);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "11\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInheritedFieldSatisfiesARequirementThroughTheReference) {
    // Soundness_Interfaces.AnInheritedFieldSatisfiesARequirement is the analyzer's half
    // of this; the backend's half is that `interfaceVtable` walks the hierarchy for a
    // field it cannot find on the implementor itself, and stores the offset the field
    // has *in the derived layout*. `a` comes from B, and S's own field follows it.
    const Built b = build(std::string(kPrintf) +
        "interface HasA { pub a <int>, }\n"
        "struct B { pub a <int>, }\n"
        "struct S: <B, HasA> { y <int>, }\n"
        "fun main() <noret> {\n"
        "    let s <S> = S { a: 6, y: 1 };\n"
        "    let h <HasA> = s;\n"
        "    printf(\"%d\\n\", h.a);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "6\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInheritedFieldAtANonZeroOffsetIsReadThroughTheReference) {
    // The one above would still pass if the walk answered 0 for anything it found in a
    // parent, because `a` is at 0 in both layouts. `b` is the parent's *second* field,
    // so this is the test that distinguishes "found in a parent" from "at the offset a
    // parent's field has here": a wrong answer prints 1, and 3 if it counted from S.
    const Built b = build(std::string(kPrintf) +
        "interface HasB { pub b <int>, }\n"
        "struct Base { pub a <int>, pub b <int>, }\n"
        "struct S: <Base, HasB> { c <int>, }\n"
        "fun main() <noret> {\n"
        "    let s <S> = S { a: 1, b: 77, c: 3 };\n"
        "    let h <HasB> = s;\n"
        "    printf(\"%d\\n\", h.b);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "77\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInterfaceReferenceIsPassedOnUnchanged) {
    // A reference that is already a reference must not be converted again. The first
    // callee holds a `Speaker` and hands it to a second one -- a `convert` that took
    // the address of its own parameter slot instead of passing the pair through would
    // make the data word point at the pair rather than at the Dog.
    const Built b = build(std::string(kPrintf) +
        "interface Speaker { pub fun speak() <int>; }\n"
        "struct D { n <int>\n"
        "    fun speak() <int> { return self.n; }\n"
        "}\n"
        "D implements <Speaker> { }\n"
        "fun again(s: Speaker) <int> { return s.speak(); }\n"
        "fun hear(s: Speaker) <int> { return again(s); }\n"
        "fun main() <noret> {\n"
        "    let d <D> = D { n: 9 };\n"
        "    printf(\"%d\\n\", hear(d));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericStructConvertsToAnInterfaceItImplements) {
    // The implementor side of a template: `G<T> implements <Speaker>` is one block over
    // every instantiation, and the table is keyed on the *live* struct, so `G<int>` gets
    // its own. This is the pair the implements-block unit's extras and this section's
    // conversion have to agree about -- the block declares the members, the table names
    // them.
    const Built b = build(std::string(kPrintf) +
        "interface Speaker { pub fun speak() <int>; }\n"
        "struct G<T> { n <T>\n"
        "    fun speak() <int> { return 5; }\n"
        "}\n"
        "G<T> implements <Speaker> { }\n"
        "fun hear(s: Speaker) <int> { return s.speak(); }\n"
        "fun main() <noret> {\n"
        "    let g <G<int>> = G::<int>{ n: 1 };\n"
        "    printf(\"%d\\n\", hear(g));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AConversionFromAValueWithNoAddressIsRefused) {
    // ADR 0027 leaves "what happens to a temporary with no address" undecided, and the
    // backend needs an address to put in the data word -- so `hear(make())` is refused
    // by name rather than given a spilled copy whose lifetime nothing states. A copy
    // would be the wrong answer for AMethodCalledThroughAReferenceWritesToTheOriginal,
    // above: the write would land in the spill and vanish.
    const Built b = build(std::string(kPrintf) +
        "interface Speaker { pub fun speak() <int>; }\n"
        "struct D { n <int>\n"
        "    fun speak() <int> { return self.n; }\n"
        "}\n"
        "D implements <Speaker> { }\n"
        "fun make() <D> { return D { n: 3 }; }\n"
        "fun hear(s: Speaker) <int> { return s.speak(); }\n"
        "fun main() <noret> { printf(\"%d\\n\", hear(make())); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find(
        "an interface conversion from a value without an address"), std::string::npos)
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWriteToAFieldThroughAnInterfaceReferenceIsRefused) {
    // The read is built (AFieldRequiredByAnInterfaceIsReadThroughTheReference); the
    // write is not, because `emitAddress` has no interface-member case -- so `x.a = 12`
    // is refused rather than dropped. It has to be refused and not dropped: a discarded
    // store is exactly the silent miscompile this file's founding rule names. ADR 0027
    // leaves "is a required field writable through the reference" undecided, and it
    // stays refused until that ruling lands; `readonly` is the half that has to be
    // decided first, since a requirement says nothing about mutability today.
    const Built b = build(std::string(kPrintf) +
        "interface HasA { pub a <int>, }\n"
        "struct S: <HasA> { a <int> }\n"
        "fun main() <noret> {\n"
        "    let s <S> = S { a: 1 };\n"
        "    let x <HasA> = s;\n"
        "    x.a = 12;\n"
        "    printf(\"%d\\n\", s.a);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("an assignment to this target"), std::string::npos)
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, AConversionBetweenTwoInterfacesIsRefused) {
    // `Wide` requires everything `Narrow` does, so widening one reference to the other
    // is a question a language has to answer -- and ADR 0027 lists it among the four it
    // does not answer, with no corpus witness to force the issue. The front end holds
    // the line, and this asserts that it is the front end: a `Wide` reaching `convert`
    // as if it were a struct would take the *pair's* address for the data word and
    // build a reference to a reference, which reads a vtable pointer as an object.
    const Built b = build(std::string(kPrintf) +
        "interface Wide { pub a <int>, pub b <int>, }\n"
        "interface Narrow { pub b <int>, }\n"
        "struct S: <Wide, Narrow> { a <int>, b <int> }\n"
        "fun main() <noret> {\n"
        "    let s <S> = S { a: 11, b: 22 };\n"
        "    let w <Wide> = s;\n"
        "    let n <Narrow> = w;\n"
        "    printf(\"%d\\n\", n.b);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("expected 'Narrow', got 'Wide'"), std::string::npos)
        << b.why();
    EXPECT_EQ(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericInterfaceAsAValueTypeLowers) {
    // A generic interface as a value type instantiates its interface definition
    // with the generic arguments substituted, constructing a `{ptr, ptr}` pair
    // and a specialized vtable.
    const Built b = build(std::string(kPrintf) +
        "interface Box<T> { pub fun get() <T>; }\n"
        "struct IB { v <int> }\n"
        "IB implements <Box<int>> {\n"
        "    pub fun get() <int> { return self.v; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let ib <IB> = IB { v: 8 };\n"
        "    let b <Box<int>> = ib;\n"
        "    printf(\"%d\\n\", b.get());\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8\n") << b.why();

    // Directly converting an rvalue struct with no address into an interface
    // reference refuses per ADR 0027.
    const Built bNoAddr = build(std::string(kPrintf) +
        "interface Box<T> { pub fun get() <T>; }\n"
        "struct IB { v <int> }\n"
        "IB implements <Box<int>> {\n"
        "    pub fun get() <int> { return self.v; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = IB { v: 8 };\n"
        "    printf(\"%d\\n\", b.get());\n"
        "}\n");
    EXPECT_NE(bNoAddr.compileExit, 0) << bNoAddr.why();
    EXPECT_NE(bNoAddr.compileErr.find("an interface conversion from a value without an address"),
              std::string::npos) << bNoAddr.why();
}

BACKEND_TEST(KnownDefect_Codegen, AnEscapingInterfaceReferenceIsAcceptedLikeAnyEscapingAddress) {
    // A reference whose implementor died is a dangling `data` word, and this backend
    // accepts it: `make` converts a local and returns the pair, and the caller reads
    // through it. Measured, it prints garbage -- the disassembly is `lea -0x8(%rsp)`,
    // so the word points into the frame `make` just left.
    //
    // Booked rather than fixed, and booked *here* rather than as an interface defect,
    // because it is not one. The second half of this test is the same program with a
    // plain `&D` in place of the interface, and it is accepted just as happily: there
    // is still no general escape analysis anywhere in the pipeline. Wave-5 slice 4
    // gave `#[slaveof]` a real rule (referent validation plus deferred struct
    // destruction), but only for declarations that ask for it -- an untied
    // reference inherits the general property; it does not add one.
    //
    // Neither half asserts the value, because a dead frame's contents are not a
    // specification -- the assertion is that the compile is *accepted*, which is what
    // has to go red the day escape analysis arrives. The two halves must move together:
    // if the interface half ever refuses while the pointer half still compiles, the
    // refusal was written in the wrong place.
    const std::string tail =
        "fun main() <noret> {\n"
        "    let s <Speaker> = make();\n"
        "    printf(\"%d\\n\", s.speak());\n"
        "}\n";
    const Built iface = build(std::string(kPrintf) +
        "interface Speaker { pub fun speak() <int>; }\n"
        "struct D { n <int>\n"
        "    fun speak() <int> { return self.n; }\n"
        "}\n"
        "D implements <Speaker> { }\n"
        "fun make() <Speaker> { let d <D> = D { n: 7 }; return d; }\n" + tail);
    EXPECT_EQ(iface.compileExit, 0) << iface.why();

    const Built ptr = build(std::string(kPrintf) +
        "struct D { n <int> }\n"
        "fun make() <&D> { let d <D> = D { n: 7 }; return &d; }\n"
        "fun main() <noret> {\n"
        "    let p <&D> = make();\n"
        "    printf(\"%d\\n\", p.n);\n"
        "}\n");
    EXPECT_EQ(ptr.compileExit, 0) << ptr.why();
}

// ---------------------------------------------------------------------------
// The no-backend diagnostic names the pin.
// ---------------------------------------------------------------------------
// Not a BACKEND_TEST, and deliberately: the string this checks lives in
// CodeGen_Stub.cpp, which is compiled *only* when FIN_WITH_LLVM=OFF, so a test
// that needed a backend could never reach the build where it matters. It reads
// the two files instead, which works in either configuration.
//
// The defect it closes: the help said "an LLVM 18 development install" after ADR
// 0010's pin moved to 22, so it sent a reader to install the one version
// CMakeLists.txt then rejects with a FATAL_ERROR. The number is spelled in the
// stub rather than passed in as a compile definition, because on that path
// find_package(LLVM) never ran -- FIN_LLVM_MAJOR would be a number nothing had
// checked -- so this test is what keeps the two in step.

TEST(Soundness_Codegen, TheNoBackendHelpNamesThePinnedLlvmMajor) {
    const fs::path repo = fs::path(FIN_TESTS_DIR).parent_path();
    const std::string cmake = readWholeFile((repo / "CMakeLists.txt").string());
    const std::string stub =
        readWholeFile((repo / "src" / "codegen" / "CodeGen_Stub.cpp").string());

    // A file that did not open reads as empty, and an empty haystack would make
    // every find() below vacuously agree. Assert the reads first.
    ASSERT_FALSE(cmake.empty()) << "could not read " << (repo / "CMakeLists.txt");
    ASSERT_FALSE(stub.empty()) << "could not read CodeGen_Stub.cpp under " << repo;

    const std::string key = "set(FIN_LLVM_MAJOR ";
    const size_t at = cmake.find(key);
    ASSERT_NE(at, std::string::npos) << "CMakeLists.txt no longer pins FIN_LLVM_MAJOR";
    size_t p = at + key.size();
    std::string major;
    while (p < cmake.size() && cmake[p] >= '0' && cmake[p] <= '9') major += cmake[p++];
    ASSERT_FALSE(major.empty()) << "FIN_LLVM_MAJOR is pinned to something that is not a number";

    const std::string want = "an LLVM " + major + " development install";
    EXPECT_NE(stub.find(want), std::string::npos)
        << "CMakeLists.txt pins LLVM " << major
        << " but CodeGen_Stub.cpp's help does not say \"" << want << "\"";
}


// ---------------------------------------------------------------------------
// Type aliases, extern aliases and symbol resolution: a declaration that binds a
// name and nothing else.
//
// `TypeDefinition` is six statements wearing one node (src/ast/decls/TypeDef.hpp):
// a real alias `type Integer = int;`, a union alias `type Number = int | uint;`,
// the erasure marker `type Any<...> = any implements <...>`, a symbol resolution
// `pub implements c_printf = printf;` (stdlib/stdio.fin:15), an extern alias
// `extern myns::myfunc as myfunc;` (extern_as.fin:19) and a wildcard extern
// `extern * from a_namespace;` (extern_as.fin:32, :39). All six reach this backend
// through one visitor, and all six are lowered as nothing.
//
// Emitting nothing here is not the skip the founding rule forbids, and the node's
// own shape is the proof. A `TypeDefinition` holds a name, generic parameters,
// `TypeNode`s, and flags -- no `Block`, no `Expression`, no `Statement` child.
// There is no statement inside it that could be dropped, so "emitted nothing" and
// "lowered completely" are the same state rather than two states that look alike.
// Compare `InterfaceDeclaration` just above, which *can* hold a method `body`: that
// is why the interface visitor has refusals and this one needs none. The precedent
// is `visit(EnumDeclaration&)`, which emits nothing for the same reason -- a name
// with no storage and no symbol behind it.
//
// What the statement does bind, the analyzer binds: Soundness_ExternAlias in
// tests/test_soundness.cpp settles that an extern alias carries its target's type,
// that an alias of a type is still a type, and that a wildcard extern is a no-op
// because the names are already in scope. The backend therefore never needs the
// alias table to lower a *declaration*. Where it does need it is a *use* of the new
// name, and the tests below fix that boundary in place: this backend resolves names
// literally, so a renaming alias's new name is refused at the use site. Refused, and
// visibly so -- which is the difference between a boundary and a miscompile.

BACKEND_TEST(Soundness_Codegen, ATypeAliasDeclarationIsLoweredAsNothing) {
    const std::string code = std::string(kPrintf) +
        "type Integer = int;\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n";
    const Built b = build(code);
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();

    // Nothing was emitted *for the alias*, which is a stronger claim than "it
    // compiled": the trace names every function and global the backend declares, so
    // an alias that had quietly become a symbol would appear here by name.
    const std::string trace = codegenTrace(code);
    EXPECT_EQ(trace.find("not lowered yet"), std::string::npos) << trace;
    EXPECT_EQ(trace.find("Integer"), std::string::npos)
        << "the alias became something the backend declared\n" << trace;
}

BACKEND_TEST(Soundness_Codegen, AUnionAliasDeclarationIsLoweredAsNothing) {
    // `type Number = int | uint | float | ...` -- arrays.fin:9, stdlib/types.fin:53,
    // stdlib/typing.fin:10. The alternatives live in `union_members` and the first in
    // `aliased_type`; neither is a statement, so neither is dropped by emitting
    // nothing. A *use* of `Number` as a type is refused, which the boundary tests
    // below record.
    const Built b = build(std::string(kPrintf) +
        "type Number = int | uint;\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TheErasureMarkerAliasDeclarationIsLoweredAsNothing) {
    // `type Any<...> = any implements <...>` sets `has_implements`. Whether `any` is
    // erased or monomorphised is a queued ruling and this test does not decide it: the
    // declaration is lowered as nothing either way, because either answer is about
    // what a *use* of the name means.
    const Built b = build(std::string(kPrintf) +
        "type Any1 = any implements <int>;\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASymbolResolutionDeclarationIsLoweredAsNothing) {
    // `pub implements c_printf = printf;` -- stdlib/stdio.fin:15. The sides are
    // swapped relative to an extern alias but the node is the same and so is the
    // lowering.
    const Built b = build(std::string(kPrintf) +
        "pub implements c_printf = printf;\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASymbolResolutionCallIsLowered) {
    // Calling the alias bound by `implements c_printf = printf;` reaches printf.
    const Built b = build(std::string(kPrintf) +
        "pub implements c_printf = printf;\n"
        "fun main() <noret> { c_printf(\"ok\\n\"); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnExternAliasIntoANamespaceDeclaresNoSecondSymbol) {
    // extern_as.fin:19 -- `extern myns::myfunc as myfunc;`, whose stated purpose is
    // "now we can just access `myns::myfunc` by using `myfunc()` only". The namespace's
    // contents are spliced into the enclosing statement list by the parser, so the
    // function is declared under its own name and the alias adds nothing: one
    // declaration, not two, and no forwarding stub.
    const std::string code = std::string(kPrintf) +
        "namespace myns { pub fun myfunc() <int> { return 4; } }\n"
        "extern myns::myfunc as myfunc;\n"
        "fun main() <noret> { printf(\"%d\\n\", myfunc()); }\n";
    const Built b = build(code);
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4\n") << b.why();

    const std::string trace = codegenTrace(code);
    EXPECT_EQ(occurrences(trace, "declared myfunc"), 1u)
        << "the alias should add no second declaration\n" << trace;
}

BACKEND_TEST(Soundness_Codegen, AWildcardExternFromANamespaceIsLoweredAsNothing) {
    // extern_as.fin:32 -- `extern * from a_namespace;`. Nothing to emit for the same
    // reason the analyzer has nothing to bind: the names are already in scope.
    const Built b = build(std::string(kPrintf) +
        "namespace a_namespace { pub fun a() <int> { return 2; } pub fun b() <int> { return 4; } }\n"
        "extern * from a_namespace;\n"
        "fun main() <noret> { printf(\"%d\\n\", a() + b()); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "6\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWildcardExternFromAnEnumIsLoweredAsNothing) {
    // extern_as.fin:39-43 -- `extern * from MyEnum;` and then `let a <MyEnum> = A;`.
    // An enum's members are already constants folded into their uses, so the wildcard
    // asks for what the file already has.
    const Built b = build(std::string(kPrintf) +
        "enum MyEnum { A, B, C }\n"
        "extern * from MyEnum;\n"
        "fun main() <noret> { let a <MyEnum> = A; printf(\"%d\\n\", a); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAttributeOnATypeAliasIsRefused) {
    // The rule every other declaration site in this file follows: an attribute the
    // backend does not read may be one that changes what is emitted, and ignoring it
    // is how a program that compiles ends up meaning something else. `#[llvm_name]` is
    // honoured on a struct and on a function, so on an alias it is a naming request
    // that would be silently discarded -- refuse instead. This is also the one part of
    // a `TypeDefinition` that could carry a demand rather than a name.
    const Built b = build(std::string(kPrintf) +
        "#[llvm_name=\"renamed\"]\n"
        "type Integer = int;\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("attribute"), std::string::npos) << b.why();
    EXPECT_NE(b.compileErr.find("Integer"), std::string::npos) << b.why();
}

// Renaming extern aliases and symbol resolutions: alias tables in codegen resolve
// functions, globals, and types so the new alias names lower identically to their targets.

BACKEND_TEST(Soundness_Codegen, ARenamingExternAliasLowersWhereItIsCalled) {
    const std::string code = std::string(kPrintf) +
        "namespace myns { pub fun realname() <int> { return 4; } }\n"
        "extern myns::realname as shortname;\n"
        "fun main() <noret> { printf(\"%d\\n\", shortname()); }\n";
    const Built b = build(code);
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "4\n") << b.why();

    const std::string trace = codegenTrace(code);
    EXPECT_NE(trace.find("declared realname"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, AnAliasedTypeLowersWhereItNamesAVariable) {
    // extern_as.fin:23 -- `extern int as Integer;` -- and `type Integer = int;` land
    // here identically, which is the point of both spellings sharing a node. The
    // backend now resolves type aliases via the alias table.
    const Built b = build(std::string(kPrintf) +
        "type Integer = int;\n"
        "fun main() <noret> { let x <Integer> = 5; printf(\"%d\\n\", x); }\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAliasedGlobalLowersWhereTheNewNameIsRead) {
    // extern_as.fin:9 -- "myglobv_diffname is a new name for `myglobv` but they are the
    // same variable just different names". Resolves via the alias table.
    const Built b = build(std::string(kPrintf) +
        "const myglobv <int> = 10;\n"
        "extern myglobv as myglobv_diffname;\n"
        "fun main() <noret> { printf(\"%d\\n\", myglobv_diffname); }\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "10\n") << b.why();
}

// ---------------------------------------------------------------------------
// Bit-width annotations: lowered at the width that was written.
//
// `int{64}` is a written width and it is the type, so this backend emits an i64 for
// it and an i8 for `uint{8}`. What stood here refused every annotated type, and the
// argument was that nothing upstream honoured the annotation: the analyzer walked it
// for its side effects and handed back the unannotated type, so lowering `int{64}`
// as an i32 gave a program a *machine* it did not ask for, silently, on a compile
// that succeeded. That is fixed at the source rather than papered over here -- the
// width is on the type now (Analyzer_Core.cpp reads it through the same constant
// reader an array extent goes through, Layout.cpp sizes it) -- so this file reads it
// the way it reads every other width, through scalarByName.
//
// Each test still pairs the annotated form against the bare one, and the pairs are
// the point rather than a leftover from the refusals. `int{64}` and `long` are one
// type, so the two halves must produce the same number; a lowering that honoured the
// annotation *differently* from the name would pass a test that only asked whether
// the annotated form compiles. The numbers themselves are all measured from the
// base-name half first, which is why there is no row here whose value was computed
// by hand.
//
// Two kinds of annotated type are still refused, and they are refused for one reason
// rather than for the old blanket one: the program asked for a width and did not get
// it, so lowering the base type would answer a question nobody asked while looking
// like it had answered theirs.
//
//   * A width this compiler cannot represent -- `int{7}`, `int{128}`. Layout.hpp
//     names four widths and these are not among them. The front end deliberately
//     does not diagnose one, because "Fin has no 128-bit integer" is a ruling nobody
//     has made and tests/samples/stdlib/types.fin:47 writes `i128` on purpose, so
//     "this compiler does not lower it yet" is the true sentence and it is this
//     file's to say. The refusal names the four, because a reader who wrote `int{7}`
//     needs to know what the set is and not only that 7 is outside it.
//   * A written annotation that yielded no width at all -- `int{8 * 8}`, whose value
//     is not a constant, and `float{128}`, `T{8}` or `(*int){8}`, whose base type has
//     nowhere to put one. Refused for exactly the reason above, which is also why
//     `T{8}` is here: a type parameter bound to `int` is not an integer *name*, and
//     whether a width written on a parameter applies to what it was bound to is a
//     question nobody has ruled.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AWidthAnnotationOnAVariableLowersAtThatWidth) {
    // Was AWidthAnnotationOnAVariableIsRefused. `%lld` and not `%d`, which is where
    // the claim lives: an i32 read as a long by va_arg prints whatever follows it in
    // the register file, so the format string is what makes this a statement about
    // the width rather than about the value.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> { let x <int{64}> = 10; printf(\"%lld\\n\", x); }\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "10\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TheSameVariableWithoutTheAnnotationLowers) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> { let x <int> = 10; printf(\"%d\\n\", x); }\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_EQ(b.out, "10\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANarrowWidthWrapsAtItsOwnWidthAndNotTheBaseNames) {
    // The strongest evidence available that the width reached the machine, and the
    // one assertion no amount of correct-looking IR can fake: 100 + 100 in eight
    // signed bits is -56, and in `int`'s thirty-two it is 200. Had the annotated form
    // been lowered at the base width instead of refused, it would have printed 200
    // and every other test in this section would still have passed.
    //
    // `printf` sees an i32 either way -- promoteVararg extends anything narrower than
    // 32 bits, which is what C's va_arg reads -- so the wrap has already happened in
    // eight bits by the time the value is widened. That ordering is the whole claim,
    // and `char` is the measured twin: the same program spelled with the name prints
    // the same -56.
    const Built narrow = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <int{8}> = 100;\n"
        "    let b <int{8}> = 100;\n"
        "    printf(\"%d\\n\", a + b);\n"
        "}\n");
    ASSERT_TRUE(narrow.ran) << narrow.why();
    EXPECT_EQ(narrow.out, "-56\n") << narrow.why();

    const Built named = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <char> = 100;\n"
        "    let b <char> = 100;\n"
        "    printf(\"%d\\n\", a + b);\n"
        "}\n");
    ASSERT_TRUE(named.ran) << named.why();
    EXPECT_EQ(named.out, "-56\n") << named.why();

    const Built wide = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <int> = 100;\n"
        "    let b <int> = 100;\n"
        "    printf(\"%d\\n\", a + b);\n"
        "}\n");
    ASSERT_TRUE(wide.ran) << wide.why();
    EXPECT_EQ(wide.out, "200\n") << wide.why();
}

BACKEND_TEST(Soundness_Codegen, AWidthAnnotationAndItsNameProduceTheSameProgram) {
    // The identity claim run rather than inspected: for each width that has a name,
    // the annotated spelling and the name produce the same output. `int{32}` against
    // `int` is the row that catches a lowering which treated *any* annotation as a
    // special case, and `uint{8}` has no row of its own to be paired with -- eight
    // unsigned bits is `byte` in cgDisplay's table and in lib/std/types.fin:82, and
    // Analyzer_Core.cpp registers no such name -- so it is asserted alone, zero
    // extended by promoteVararg the way `ushort` is measured to be.
    struct Case { const char* type; const char* fmt; const char* value; const char* out; };
    const std::vector<Case> cases{
        {"int{8}",   "%d",  "100", "100\n"},  {"char",   "%d",  "100", "100\n"},
        {"int{16}",  "%d",  "100", "100\n"},  {"short",  "%d",  "100", "100\n"},
        {"int{32}",  "%d",  "100", "100\n"},  {"int",    "%d",  "100", "100\n"},
        {"int{64}",  "%lld", "100", "100\n"},  {"long",   "%lld", "100", "100\n"},
        {"uint{16}", "%d",  "200", "200\n"},  {"ushort", "%d",  "200", "200\n"},
        {"uint{32}", "%u",  "200", "200\n"},  {"uint",   "%u",  "200", "200\n"},
        {"uint{64}", "%llu", "200", "200\n"},  {"ulong",  "%llu", "200", "200\n"},
        {"uint{8}",  "%d",  "200", "200\n"},
    };
    for (const Case& c : cases) {
        const Built b = build(std::string(kPrintf) +
            "fun main() <noret> { let x <" + c.type + "> = " + c.value +
            "; printf(\"" + c.fmt + "\\n\", x); }\n");
        ASSERT_TRUE(b.ran) << c.type << "\n" << b.why();
        EXPECT_EQ(b.out, c.out) << c.type << "\n" << b.why();
    }
}

BACKEND_TEST(Soundness_Codegen, SizeofAWrittenWidthIsThatWidthInBytes) {
    // `sizeof` reads the module's own DataLayout, so these are LLVM's numbers for the
    // types this file built and not a restatement of Layout.hpp's. Both are asserted
    // -- Soundness_Layout.EveryRepresentableWidthHasItsOwnSize computes the same four
    // in LayoutEngine -- because two passes that agree today are two passes that
    // disagree after one edit, and that disagreement is an ABI split in which every
    // program still compiles and runs.
    //
    // It also fixes the refusal this expression used to get. `sizeof(int{64})` said
    // "'sizeof' of 'int' is not lowered yet", building its text from the node's bare
    // name -- naming the one half of the type that was never the problem.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d %d %d %d %d %d\\n\", sizeof(int{8}), sizeof(uint{8}),\n"
        "           sizeof(int{16}), sizeof(int{32}), sizeof(int{64}),\n"
        "           sizeof(uint{64}));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 1 2 4 8 8\n") << b.why();

    // The same six through the names, which is SizeofAScalarIsItsWidth's measurement
    // read back in this section's terms: 1 2 4 8 is the table and not this expression.
    const Built named = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d %d %d %d\\n\", sizeof(char), sizeof(short), sizeof(int),\n"
        "           sizeof(long));\n"
        "}\n");
    ASSERT_TRUE(named.ran) << named.why();
    EXPECT_EQ(named.out, "1 2 4 8\n") << named.why();
}

BACKEND_TEST(Soundness_Codegen, AWidthAnnotationOnAParameterLowersAtThatWidth) {
    const Built b = build(std::string(kPrintf) +
        "fun f(x: int{64}) <void> { printf(\"%lld\\n\", x); }\n"
        "fun main() <noret> { let n <long> = 7; f(n); }\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWidthAnnotationOnAReturnLowersAtThatWidth) {
    // A return is the one role where a wrong width is invisible at the call site as
    // well as at the definition: the caller reads whatever the ABI says the return
    // register holds, which for an i32 returned where an i64 was declared is the
    // low half and thirty-two bits of whatever was there. 4294967297 is 2^32 + 1, so
    // a truncated return prints 1 -- a plausible number, which is why the value is
    // this and not the 7 the parameter test uses.
    const Built b = build(std::string(kPrintf) +
        "fun f() <int{64}> { return 4294967297; }\n"
        "fun main() <noret> { printf(\"%lld\\n\", f()); }\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4294967297\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWidthAnnotationOnAStructFieldIsThatManyBytes) {
    // The shape test_layout.cpp measures as a field and this asserts as an ABI: a
    // `uint{8}` is one byte written and used to be four laid out. A struct is where
    // that is observable from inside the program -- through sizeof, and through the
    // offset of every field after it -- rather than only in a value.
    //
    // Two structs and not one, because a size alone does not distinguish a narrow
    // field from a narrow struct. `{uint{8}, uint{8}}` is two bytes, which is the
    // number that says the fields are one byte each; `{uint{8}, int{64}}` is sixteen,
    // of which seven are the padding the one-byte field forces, and reading `b` back
    // as 4294967297 is what says the eight-byte field sits where that padding puts it.
    // `{ushort, ushort}` and `{ushort, long}` measure 4 and 16, so the first number
    // is this section's and the second is the alignment's.
    const Built b = build(std::string(kPrintf) +
        "struct Narrow { pub a <uint{8}>, pub b <uint{8}>, }\n"
        "struct Padded { pub a <uint{8}>, pub b <int{64}>, }\n"
        "fun main() <noret> {\n"
        "    let n <Narrow>;\n"
        "    n.a = 200;\n"
        "    n.b = 1;\n"
        "    let p <Padded>;\n"
        "    p.b = 4294967297;\n"
        "    printf(\"%d %d %d %d %lld\\n\", sizeof(Narrow), sizeof(Padded),\n"
        "           n.a, n.b, p.b);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2 16 200 1 4294967297\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWidthAnnotationOnAPointeeLowersAtThatWidth) {
    // tests/samples/type_annotations.fin:11 as repaired: `let p <*int{64}> = &x;` over
    // an `int{64}`, where the sample wrote `*int{32}` over an `int{64}` and so asked
    // for a pointer whose pointee is half its target. The mapper reaches an annotated
    // pointee through mapPointer rather than at the top, so this is the case a width
    // read only for an undecorated type would miss.
    //
    // The store is what makes it a claim about the pointee's width rather than about
    // the pointer's: 4294967297 written through a four-byte slot writes four bytes and
    // leaves the high half of `x` as it was, so the two readings disagree. Reading `x`
    // back through the variable and `*p` through the pointer is what compares them.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <int{64}> = 1;\n"
        "    let p <*int{64}> = &x;\n"
        "    *p = 4294967297;\n"
        "    printf(\"%lld %lld\\n\", x, *p);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4294967297 4294967297\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWidthAnnotationOnAnArrayElementIsTheStride) {
    // Reached through mapArray's element, the other decorated door into the mapper.
    // An array is where a dropped width is worst: the element width is the stride, so
    // an `[int{64}, 2]` emitted as two i32s reserves half the memory the program asked
    // for and then indexes it at the wrong scale, which is a read of somebody else's
    // bytes rather than a narrow value.
    //
    // Both extents are asserted and the last element is read back, because a stride
    // can be wrong in a way a total size is not: four i32s and two i64s are both
    // sixteen bytes. `[long, 2]` measures 16 and `[ushort, 4]` measures 8, so 4 for
    // `[uint{8}, 4]` is this section's number.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int{64}, 2]> = [4294967297, 8589934593];\n"
        "    let n <[uint{8}, 4]>;\n"
        "    n[3] = 200;\n"
        "    printf(\"%d %d %lld %lld %d\\n\", sizeof([int{64}, 2]),\n"
        "           sizeof([uint{8}, 4]), a[0], a[1], n[3]);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "16 4 4294967297 8589934593 200\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWidthAnnotationOnACastTargetTruncatesToThatWidth) {
    // A cast is the one role where the width is the entire request: `cast<int{8}>(x)`
    // asks for eight bits and nothing else, so lowering it as an i32 answered a
    // question nobody asked while looking like it had answered theirs. The numbers are
    // `cast<char>`, `cast<short>` and `cast<ushort>`'s measured answers -- 44, -31073
    // and 4464 -- which is what makes the annotated cast the same cast rather than a
    // second one that agrees with it.
    const Built annotated = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d %d %d %lld\\n\", cast<int{8}>(300), cast<int{16}>(99999),\n"
        "           cast<uint{16}>(70000), cast<int{64}>(1));\n"
        "}\n");
    EXPECT_EQ(annotated.compileExit, 0) << annotated.why();
    ASSERT_TRUE(annotated.ran) << annotated.why();
    EXPECT_EQ(annotated.out, "44 -31073 4464 1\n") << annotated.why();

    const Built named = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d %d %d %lld\\n\", cast<char>(300), cast<short>(99999),\n"
        "           cast<ushort>(70000), cast<long>(1));\n"
        "}\n");
    ASSERT_TRUE(named.ran) << named.why();
    EXPECT_EQ(named.out, annotated.out) << named.why();
}

BACKEND_TEST(Soundness_Codegen, AWidthAnnotationOnAGlobalLowersAtThatWidth) {
    // A global's width is in the object file rather than in a frame, so this is the
    // one role where the wrong answer is linkable: another translation unit reading
    // `g` reads the bytes the initialiser reserved, and a four-byte `g` declared as
    // eight is a read of whatever follows it.
    const Built b = build(std::string(kPrintf) +
        "let g <int{64}> = 4294967297;\n"
        "let n <uint{8}> = 200;\n"
        "fun main() <noret> { printf(\"%lld %d\\n\", g, n); }\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4294967297 200\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWidthAnnotationOnAConstLowersAtThatWidth) {
    // A `const` is a global whose mutability the analyzer checks, so it reaches the
    // same emitter -- asserted rather than assumed because a constant initialiser is
    // built from the type before any store exists to be widened.
    const Built b = build(std::string(kPrintf) +
        "const N <int{64}> = 4294967297;\n"
        "fun main() <noret> { printf(\"%lld\\n\", N); }\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4294967297\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWidthTheTableDoesNotNameIsRefusedAndTheSetIsNamed) {
    // Eight, sixteen, thirty-two and sixty-four are the widths Layout.hpp's table
    // holds, and a well-formed width outside them has no representation here. It is
    // refused in every role, from one place -- the mapper is where a written type
    // becomes a machine type, so each role's refusal is that one failure reported with
    // the role's own noun.
    //
    // The set is in the message. A reader who wrote `int{7}` has been told that 7 is
    // wrong and needs to be told what is right, and there is nowhere else to learn it:
    // the layout pass says the same sentence (Soundness_Layout.AWidthThisCompilerCannot
    // RepresentHasNoLayout) but LayoutEngine has no caller in the compiler yet, so this
    // refusal is the only one a program actually receives.
    struct Case { const char* role; const char* spelled; std::string code; };
    const std::vector<Case> cases{
        {"a variable", "'int{7}'", "fun main() <noret> { let x <int{7}> = 1; }\n"},
        {"a variable", "'int{128}'", "fun main() <noret> { let x <int{128}> = 1; }\n"},
        {"a variable", "'uint{24}'", "fun main() <noret> { let x <uint{24}> = 1; }\n"},
        {"a parameter", "'int{7}'",
         "fun f(x: int{7}) <void> { }\nfun main() <noret> { }\n"},
        {"a return", "'int{7}'",
         "fun f() <int{7}> { return 1; }\nfun main() <noret> { }\n"},
        {"a struct field", "'int{128}'",
         "struct S { pub a <int{128}>, }\nfun main() <noret> { let s <S>; }\n"},
        {"a variable", "'&int{7}'", "fun main() <noret> { let p <*int{7}> = null; }\n"},
        {"a variable", "'[int{7}, 2]'", "fun main() <noret> { let a <[int{7}, 2]>; }\n"},
        {"a cast", "'int{7}'",
         "fun main() <noret> { let y <int> = cast<int{7}>(1); }\n"},
        {"a global", "'int{7}'", "let g <int{7}> = 1;\nfun main() <noret> { }\n"},
    };
    for (const Case& c : cases) {
        const Built b = build(c.code);
        EXPECT_NE(b.compileExit, 0) << c.code << b.why();
        EXPECT_NE(b.compileErr.find(std::string(c.role) + " of type " + c.spelled),
                  std::string::npos)
            << "the refusal names the role and the type as written\n"
            << c.code << b.why();
        EXPECT_NE(b.compileErr.find("8, 16, 32 or 64"), std::string::npos)
            << "the refusal must name the set it is refusing against\n"
            << c.code << b.why();
    }

    // The same ten with a width the table does name, so what is being refused above is
    // the number and not the annotation.
    for (const char* code : {
             "fun main() <noret> { let x <int{16}> = 1; }\n",
             "fun f(x: int{16}) <void> { }\nfun main() <noret> { }\n",
             "fun f() <int{16}> { return 1; }\nfun main() <noret> { }\n",
             "struct S { pub a <int{16}>, }\nfun main() <noret> { let s <S>; }\n",
             "fun main() <noret> { let p <*int{16}> = null; }\n",
             "fun main() <noret> { let a <[int{16}, 2]>; }\n",
             "fun main() <noret> { let y <int> = cast<int{16}>(1); }\n",
             "let g <int{16}> = 1;\nfun main() <noret> { }\n"}) {
        const Built b = build(code);
        EXPECT_EQ(b.compileExit, 0) << "a representable width lowers here:\n"
                                    << code << b.why();
    }
}

BACKEND_TEST(Soundness_Codegen, ANonConstantWidthAnnotationIsRefusedAndSaidToBeOne) {
    // tests/samples/type_annotations.fin:8 -- `let z <int{8 * 8}> = 42;`. The width is
    // an arithmetic expression, folded nowhere, so there is no number to print and the
    // spelling says `{...}`: the same thing ASTPrinter says, for the same reason. The
    // front end accepts it and hands back plain `int` (Soundness_IntegerWidths
    // .AnArithmeticWidthIsNotAConstantAndIsNotRefusedHere), so the type that arrives
    // here is lowerable -- and lowering it would give the program 32 bits where it
    // asked for something it never got an answer about. Refused for that reason and
    // not for the set's, which is why no set is named.
    const Built b = build("fun main() <noret> { let z <int{8 * 8}> = 42; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a variable of type 'int{...}'"), std::string::npos)
        << b.why();
    EXPECT_EQ(b.compileErr.find("8, 16, 32 or 64"), std::string::npos)
        << "the set is not what this refusal is about\n" << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWidthOnATypeThatCannotCarryOneIsRefused) {
    // A width is a count of value bits on an integer. Written on anything else it is
    // read by the front end, checked, and then dropped -- deliberately, so that
    // tests/samples/type_annotations.fin:14's `{int{64}, float{128}}` keeps resolving
    // (Soundness_IntegerWidths.AWidthOnANonIntegerIsStillNotAType). Dropped means the
    // program asked for something and got no answer, which is this section's refusal
    // rather than a new one, so no set is named here either.
    //
    // The parenthesised forms are the same case reached through a different node.
    // `(*int){8}` puts the annotation on the pointer, not on the pointee, and a
    // pointer is one machine word whatever is written after it. It is spelled with the
    // parentheses the program wrote, because `&int{8}` is a different type -- a
    // pointer to eight bits, which lowers.
    struct Case { const char* spelled; std::string code; };
    const std::vector<Case> cases{
        {"'float{128}'", "fun main() <noret> { let x <float{128}> = 1.5; }\n"},
        {"'double{32}'",
         "fun main() <noret> { let x <double{32}> = cast<double>(1.5); }\n"},
        {"'bool{1}'", "fun main() <noret> { let x <bool{1}> = true; }\n"},
        {"'string{8}'", "fun main() <noret> { let x <string{8}> = \"s\"; }\n"},
        {"'S{8}'",
         "struct S { pub a <int>, }\nfun main() <noret> { let x <S{8}>; }\n"},
        {"'any{8}'", "fun main() <noret> { let x <any{8}>; }\n"},
        {"'(&int){8}'",
         "fun main() <noret> { let x <int> = 1; let p <(*int){8}> = &x; }\n"},
        {"'([int, 2]){8}'", "fun main() <noret> { let a <([int, 2]){8}>; }\n"},
    };
    for (const Case& c : cases) {
        const Built b = build(c.code);
        EXPECT_NE(b.compileExit, 0) << c.code << b.why();
        EXPECT_NE(b.compileErr.find(c.spelled), std::string::npos)
            << "the refusal must name the type as written\n" << c.code << b.why();
    }

    // Each of those without its annotation, so the refusals above are about the
    // annotation and not about the base type. `any` is left out: it has no
    // representation either way and is refused on its own account.
    for (const char* code : {
             "fun main() <noret> { let x <float> = 1.5; }\n",
             "fun main() <noret> { let x <double> = cast<double>(1.5); }\n",
             "fun main() <noret> { let x <bool> = true; }\n",
             "fun main() <noret> { let x <string> = \"s\"; }\n",
             "struct S { pub a <int>, }\nfun main() <noret> { let x <S>; }\n",
             "fun main() <noret> { let x <int> = 1; let p <*int> = &x; }\n",
             "fun main() <noret> { let a <[int, 2]>; }\n"}) {
        const Built b = build(code);
        EXPECT_EQ(b.compileExit, 0) << "the base type lowers:\n" << code << b.why();
    }
}

BACKEND_TEST(Soundness_Codegen, AWidthOnATypeParameterIsRefusedAndNotTakenFromTheBinding) {
    // The live miscompile this unit closed, and the reason the check has to sit inside
    // boundBinding rather than beside the refusals above: a bare `T` is replaced by
    // whatever the instantiation bound it to *before* anything looks at the
    // annotation, so `T{8}` bound to `int` lowered as a plain i32 and each of these
    // printed 300 -- a value that does not fit eight bits, out of a program that asked
    // for eight bits, on a compile that reported success.
    //
    // Refused rather than honoured, because "a width written on a type parameter
    // applies to whatever the parameter was bound to" is a rule nobody has stated, and
    // `T{8}` where T is a struct or a `[int]` has no reading at all. The three
    // positions are three different paths through the mapper -- a parameter, a field
    // and a return -- and all three were measured printing 300 before this.
    struct Case { const char* what; std::string code; };
    const std::vector<Case> cases{
        {"a parameter",
         "fun id<T>(x: T{8}) <void> { printf(\"%d\\n\", x); }\n"
         "fun main() <noret> { id::<int>(300); }\n"},
        {"a struct field",
         "struct Box<T> { pub v <T{8}>, }\n"
         "fun main() <noret> { let b <Box<int>>; b.v = 300;\n"
         "                     printf(\"%d\\n\", b.v); }\n"},
        {"a return",
         "fun id<T>(x: T) <T{8}> { return x; }\n"
         "fun main() <noret> { printf(\"%d\\n\", id::<int>(300)); }\n"},
    };
    for (const Case& c : cases) {
        const Built b = build(std::string(kPrintf) + c.code);
        EXPECT_NE(b.compileExit, 0)
            << "a width on a type parameter was lowered at the binding's width: "
            << c.what << "\n" << c.code << b.why();
        EXPECT_NE(b.compileErr.find("'T{8}'"), std::string::npos)
            << "the refusal names the parameter and the width, not the binding\n"
            << c.code << b.why();
    }

    // The same three with the parameter written bare, which is what makes the refusals
    // above about the annotation. 300 is the right answer for an `int`.
    for (const char* code : {
             "fun id<T>(x: T) <void> { printf(\"%d\\n\", x); }\n"
             "fun main() <noret> { id::<int>(300); }\n",
             "struct Box<T> { pub v <T>, }\n"
             "fun main() <noret> { let b <Box<int>>; b.v = 300;\n"
             "                     printf(\"%d\\n\", b.v); }\n",
             "fun id<T>(x: T) <T> { return x; }\n"
             "fun main() <noret> { printf(\"%d\\n\", id::<int>(300)); }\n"}) {
        const Built b = build(std::string(kPrintf) + code);
        ASSERT_TRUE(b.ran) << code << b.why();
        EXPECT_EQ(b.out, "300\n") << code << b.why();
    }
}

BACKEND_TEST(Soundness_Codegen, ARefusedWidthAnnotationWritesNoObject) {
    // The rule the refusal exists to keep: a program whose widths the backend cannot
    // honour produces nothing, so no later build step can pick up an object that
    // computes in the wrong precision. `int{128}` rather than the `int{64}` this test
    // used to carry -- 64 is lowered now, and the property under test needs a width
    // that is still refused.
    fs::path src = uniqueTempPath("fin_ann", ".fin");
    fs::path obj = uniqueTempPath("fin_ann", ".o");
    {
        std::ofstream f(src, std::ios::binary);
        f << "fun main() <noret> { let x <int{128}> = 10; }\n";
    }
    const FincRun r = runFinc({"-c", src.string(), "-o", obj.string()});
    EXPECT_NE(r.exitCode, 0) << stripAnsi(r.err);
    EXPECT_FALSE(fs::exists(obj)) << "an object was written for a refused compile";
    std::error_code ec;
    fs::remove(src, ec);
    fs::remove(obj, ec);
}

// ---------------------------------------------------------------------------
// Function values and lambdas.
//
// A Fin function value is a bare code pointer. That was the open decision this unit
// closed, and the corpus is what closed it rather than a preference: all thirteen
// lambdas in tests/samples read nothing but their own parameters, and the one that
// looks like an exception (lambdas.fin:58, `(msg: string) <void> => printf("Log:
// %s\n", msg)`) reads a symbol and not a variable. Nothing closes over a local, so
// the second word of a closure pair would be a word every `fn` field, parameter and
// return carried for no reader.
//
// The tests below come in two halves and the second half is the one that makes the
// first half safe. A bare pointer is only a sound representation for as long as a
// capture is *refused*, because a captured local lives in a frame that is gone by
// the time the pointer is called -- so ALambdaCapturingALocalIsRefused and its
// assignment sibling are not edge cases, they are the boundary this shape sits
// behind. The same goes for the signature checks: with opaque pointers every `fn` is
// `ptr`, so nothing in the IR distinguishes `fn(int) -> int` from `fn(int, int) ->
// int`, and if convert() did not compare signatures itself the mismatch would emit a
// call that reads an argument register the caller never set.

BACKEND_TEST(Soundness_Codegen, ANamedFunctionIsPassedAsAFunctionParameter) {
    // tests/samples/functions.fin Case A. An llvm::Function is already a pointer
    // constant, so passing one by name emits nothing at all -- which is why this was
    // the cheapest half of the unit and still needed the `fn` parameter type to exist
    // before it could land.
    const Built b = build(std::string(kPrintf) +
        "fun compute(a: int, b: int, operation: fn(int, int) => int) <int> {\n"
        "    return operation(a, b);\n"
        "}\n"
        "fun add(x: int, y: int) <int> { return x + y; }\n"
        "fun main() <noret> { printf(\"%d\\n\", compute(10, 20, add)); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "30\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFunctionValueInAVariableIsCalledThroughIt) {
    // functions.fin Case B. The call is indirect: there is no llvm::Function to name,
    // so the callee is a loaded pointer and the llvm::FunctionType comes from the
    // variable's own CgType. Getting that type from anywhere else is how an indirect
    // call reads the wrong registers.
    const Built b = build(std::string(kPrintf) +
        "fun add(x: int, y: int) <int> { return x + y; }\n"
        "fun main() <noret> {\n"
        "    let my_op <fn(int, int) => int> = add;\n"
        "    printf(\"%d\\n\", my_op(5, 5));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "10\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrowLambdaWithABlockBodyIsCalled) {
    // functions.fin Case C, which the sample's own comment calls a closure and which
    // captures nothing -- the distinction the refusal further down enforces.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let my_op <auto> = (a: int, b: int) <int> => { return a + b; };\n"
        "    printf(\"%d\\n\", my_op(1, 2));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAnonymousFunctionIsWrittenInsideACallsArguments) {
    // functions.fin Case D. The lambda is emitted as a whole function from the middle
    // of another function's body, which is what ScopedEmission already existed for --
    // an instantiation does the same thing -- so the caller's half-built block, its
    // FnInfo and its locals all have to come back afterwards.
    const Built b = build(std::string(kPrintf) +
        "fun compute(a: int, b: int, operation: fn(int, int) => int) <int> {\n"
        "    return operation(a, b);\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let res <int> = compute(100, 50, fun (a: int, b: int) <int> {\n"
        "        return a - b;\n"
        "    });\n"
        "    printf(\"%d\\n\", res);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "50\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnArrowLambdaWithAnExpressionBodyReturnsIt) {
    // lambdas.fin Case 3. The expression form goes through the same prologue every
    // other body gets -- an entry block, a stack slot per parameter -- and differs in
    // exactly one place: the body is a value to return rather than a Block to walk.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let f3 <fn(int) -> int> = (x: int) <int> => x - 3;\n"
        "    printf(\"%d\\n\", f3(10));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AVoidLambdaWithAnExpressionBodyEvaluatesItAndReturnsNothing) {
    // lambdas.fin Case 6, and the reason the expression form cannot simply always
    // return: `printf` is declared `<noret>` here, so there is no value to hand back
    // and `CreateRet` of a void call is invalid IR. The return type is what tells the
    // two apart, because there is no second syntax.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let logger <auto> = (msg: string) <void> => printf(\"Log: %s\\n\", msg);\n"
        "    logger(\"Hello Lambda\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "Log: Hello Lambda\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ALambdaReadsAGlobalSymbolWithoutCapturingIt) {
    // The distinction the capture refusal turns on, asserted rather than assumed:
    // lambdas.fin:58 reads `printf` from inside a lambda and that is a symbol, not a
    // frame slot. A capture check that fired on every free name would refuse this, and
    // it is the single most common shape a lambda in the corpus has.
    const Built b = build(std::string(kPrintf) +
        "const BASE <int> = 100;\n"
        "fun main() <noret> {\n"
        "    let f <fn(int) -> int> = (x: int) <int> => x + BASE;\n"
        "    printf(\"%d\\n\", f(5));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "105\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFunctionReturnsALambdaAndTheCallerCallsIt) {
    // lambdas.fin's `get_adder` and its Case 7. A bare pointer is what makes this work
    // at all: the lambda outlives the call that produced it, and there is no frame to
    // outlive because it captured nothing.
    const Built b = build(std::string(kPrintf) +
        "fun get_adder() <fn(int, int) -> int> {\n"
        "    return (a: int, b: int) <int> => a + b;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let adder <auto> = get_adder();\n"
        "    printf(\"%d\\n\", adder(10, 20));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "30\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ALambdaCapturingALocalLowersAndReadsItsEnv) {
    // Was ALambdaCapturingALocalIsRefused: the bare-pointer boundary is gone,
    // replaced by a closure pair. `outer` lives in main's frame and the lambda
    // is a value holding a snapshot of it, so lowering this reads the env the
    // definition built rather than a frame that is gone. Inverted, not deleted.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let outer <int> = 7;\n"
        "    let f <fn(int) -> int> = (x: int) <int> => x + outer;\n"
        "    printf(\"%d\\n\", f(1));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ALambdaAssigningToACapturedLocalWritesItsOwnCopy) {
    // Was ALambdaAssigningToACapturedLocalIsRefused, inverted with the same
    // boundary change. Captures are by value: the write updates the closure's
    // own env field, and the outer binding reads back unchanged.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let outer <int> = 7;\n"
        "    let f <fn(int) -> int> = (x: int) <int> => { outer = x; return x; };\n"
        "    printf(\"%d %d\\n\", f(1), outer);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 7\n") << b.why();
}

// ---- closures -----------------------------------------------------------
// A capturing lambda is a closure pair: the code beside the env snapshot the
// definition built. What follows pins each way such a value travels -- stored,
// passed as an argument, returned -- because each is a different lowering path
// (a slot, a call argument, a return slot) over the same representation.

BACKEND_TEST(Soundness_Codegen, ACapturingLambdaSnapshotsAtDefinition) {
    // The env is built where the lambda is written, not where it is called:
    // reassigning `outer` after the definition leaves the closure's copy behind.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let outer <int> = 7;\n"
        "    let f <fn(int) -> int> = (x: int) <int> => x + outer;\n"
        "    outer = 99;\n"
        "    printf(\"%d\\n\", f(1));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ACapturingClosurePassedAsAnArgument) {
    // The callee takes an ordinary `fn`: the pair crosses the call like any
    // other argument, and the indirect call threads its env.
    const Built b = build(std::string(kPrintf) +
        "fun compute(val: int, callback: fn(int) -> int) <int> {\n"
        "    return callback(val);\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let outer <int> = 10;\n"
        "    let f <fn(int) -> int> = (x: int) <int> => x + outer;\n"
        "    printf(\"%d\\n\", compute(5, f));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "15\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ACapturingClosureReturnedFromAFunction) {
    // The closure outlives the frame that built it: the env is heap storage,
    // so returning the pair is sound where returning a frame address is not.
    const Built b = build(std::string(kPrintf) +
        "fun get() <fn(int) -> int> {\n"
        "    let outer <int> = 7;\n"
        "    return (x: int) <int> => x + outer;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let f <auto> = get();\n"
        "    printf(\"%d\\n\", f(1));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AClosureCapturingAnotherClosure) {
    // `f` is itself an `fn`-typed local holding a pair: capturing one is what
    // keeps the analyzer's `is_function`/`is_template` exclusions honest, since
    // a nested function of the same type must *not* be captured.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let outer <int> = 10;\n"
        "    let f <fn(int) -> int> = (x: int) <int> => x + 1;\n"
        "    let g <fn(int) -> int> = (x: int) <int> => f(x) + outer;\n"
        "    printf(\"%d\\n\", g(1));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "12\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedClosureReadsThroughTheOuterClosureEnv) {
    // The transitive case: `g` is defined inside `f`'s body, where `outer` is
    // itself a capture rather than a frame slot. The inner snapshot reads the
    // outer env field, so what it sees is the outer closure's copy.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let outer <int> = 100;\n"
        "    let f <auto> = fun (x: int) <int> {\n"
        "        let g <auto> = fun (y: int) <int> { return x + y + outer; };\n"
        "        return g(1);\n"
        "    };\n"
        "    printf(\"%d\\n\", f(10));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "111\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AClosureCapturingAReferenceAliasesItsReferent) {
    // The `byRef` half of the analysis: a `<&int>` capture copies the pointer,
    // so a write through it is visible outside. Value-typed captures snapshot
    // instead (ALambdaAssigningToACapturedLocalWritesItsOwnCopy pins that).
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let cell <&int> = new int(7);\n"
        "    let f <auto> = (x: int) <int> => { *cell = x; return x; };\n"
        "    printf(\"%d %d\\n\", f(5), *cell);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5 5\n") << b.why();
}

// ---- generic lambdas -----------------------------------------------------
//
// `let id <auto> = fun <T>(x: T) <T> { return x; };` binds a name to a template, and
// what follows from that is one ruling applied twice: a template is a recipe, so the
// declaration emits nothing, and a call is where it becomes code.
//
// The declaration therefore allocates no slot -- which is the part worth stating,
// because it makes `id` a name the locals do not hold and every other path that
// resolves a name has to be told. A value use, an address, and a call all reach a
// different table; the tests below pin each one, because a table walked in the wrong
// order is a call to the wrong thing rather than a missing feature.
//
// lambdas.fin's own two are never called, so they are the case where "emits nothing" is
// the whole answer -- and `AGenericLambdaNobodyCallsLowersToNothing` is that sample
// reduced to its assertion.

BACKEND_TEST(Soundness_Codegen, AGenericLambdaNobodyCallsLowersToNothing) {
    // lambdas.fin:69 and :71 reduced. A template is not code, so an uncalled one is not
    // an omission: the same rule `AGenericFunctionNobodyCallsLowersToNothing` states for
    // a named template, one scope in. Asserted on the trace because the *absence* of a
    // symbol is what is being claimed, and an object file has nothing to look at.
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let g <auto> = fun <T>(m: T) <T> { return m; };\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    EXPECT_NE(trace.find("registered the generic lambda g"), std::string::npos) << trace;
    EXPECT_EQ(trace.find("fin.lambda.0<"), std::string::npos) << trace;
    EXPECT_EQ(trace.find("not lowered yet"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, ACalledGenericLambdaIsInstantiatedAtItsArgument) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let id <auto> = fun <T>(x: T) <T> { return x; };\n"
        "    printf(\"%d\\n\", id(7));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericLambdaAtTwoTypesIsTwoFunctions) {
    // Monomorphisation, and the value is what proves it: an erased single body could not
    // return a double from the same code that returns an int. Both spellings of the
    // lambda are exercised elsewhere; this one is about the instantiation.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let id <auto> = fun <T>(x: T) <T> { return x; };\n"
        "    printf(\"%d %.1f\\n\", id(7), id(2.5));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7 2.5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoCallsAtOneTypeShareOneInstance) {
    // The other half of the previous test, and the reason the trace is asserted on a
    // count: a template instantiated twice at one type has to be emitted once, and a
    // test that only looked for the name would pass either way.
    const std::string trace = codegenTrace(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let id <auto> = fun <T>(x: T) <T> { return x; };\n"
        "    printf(\"%d %d\\n\", id(1), id(2));\n"
        "}\n");
    EXPECT_EQ(occurrences(trace, "declared fin.lambda.0<int>"), 1u) << trace;
}

BACKEND_TEST(Soundness_Codegen, TheArrowSpellingOfAGenericLambdaAlsoLowers) {
    // `<T>(m: T) <T> => m` is lambdas.fin:69's spelling with the erasure marker removed.
    // The expression body is the one thing an instance's emission does differently from
    // a named template's, so it is pinned separately.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let same <auto> = <T>(m: T) <T> => m;\n"
        "    printf(\"%d\\n\", same(41) + 1);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericFnAnnotationOnALambdaTemplateLowers) {
    // `let id <fn<T>(x: T) -> T> = ...`. The annotation is not mapped and must not be:
    // TypeMapper::mapFunction refuses every generic `fn` because a template has no
    // representation, and that refusal is right about the type and wrong about the
    // program. So the declaration path checks that the annotation *is* a generic `fn`
    // and asks the mapper nothing.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let id <fn<T>(x: T) -> T> = fun <T>(x: T) <T> { return x; };\n"
        "    printf(\"%d\\n\", id(7));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ATurbofishOnAGenericLambdaPicksTheInstance) {
    // The same route `ident::<long>(5)` takes on a named template: the bindings are
    // resolved at the call by this pass rather than read off the analyzer, so one
    // inference rule serves both and a turbofish cannot mean two things.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let id <auto> = fun <T>(x: T) <T> { return x; };\n"
        "    printf(\"%d\\n\", id::<int>(9));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericLambdaUsedAsAValueIsRefused) {
    // The boundary that replaced the old blanket refusal, and it is the *position* that
    // is refused rather than the construct: a value is one address and a template is two
    // functions, so `let a <auto> = id;` names neither. The same message the named form
    // gets in visit(Identifier&), one scope in.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let id <auto> = fun <T>(x: T) <T> { return x; };\n"
        "    let a <auto> = id;\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("the generic lambda 'id' used as a value"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, TheAddressOfAGenericLambdaIsRefused) {
    // The same boundary through the other path, which is why the check lives in two
    // places -- the reason a capture's does. A read goes through visit(Identifier&) and
    // `&id` comes through emitAddress, so refusing only the read would leave this falling
    // through to "the address of a value with no home": that names a lifetime question,
    // and the answer here is that there is no value.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let id <auto> = fun <T>(x: T) <T> { return x; };\n"
        "    let p <auto> = &id;\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("the address of the generic lambda 'id'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericLambdasErasureMarkerIsRefusedAtTheCall) {
    // lambdas.fin:69's `<T: Castable>`, and the reason the sample builds without this
    // firing: the marker is checked where the representation is first needed, and nothing
    // in the corpus calls that lambda. Same ruling as the named template's, reached
    // through the same predicate.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let id <auto> = fun <T: Castable>(x: T) <T> { return x; };\n"
        "    printf(\"%d\\n\", id(7));\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("the erasure marker 'Castable' on 'T' of the generic "
                                "lambda 'id'"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInnerGenericLambdaShadowsAnOuterOfTheSameName) {
    // Why the table is a stack pushed with the scopes rather than one flat map keyed by
    // the written name. Both templates are called and each answer is different, so a flat
    // table would be visibly wrong in one direction or the other -- and it is the failure
    // emitNestedFunction's own generic refusal names ("keyed by the written name with no
    // scope in it, so a nested template of a name the module also uses would silently be
    // one or the other").
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let id <auto> = fun <T>(x: T) <T> { return x; };\n"
        "    {\n"
        "        let id <auto> = fun <T>(x: T) <int> { return 99; };\n"
        "        printf(\"%d\\n\", id(1));\n"
        "    }\n"
        "    printf(\"%d\\n\", id(2));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "99\n2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ALambdaBesideAGenericLambdaCallsIt) {
    // A template crosses a body boundary where a local may not, and for the reason a
    // nested function does: instantiating one needs a node and a snapshot, neither of
    // which is a frame. So `(n: int) <int> => id(n) + 1` written beside the template
    // builds the same instance the enclosing body would.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let id <auto> = fun <T>(x: T) <T> { return x; };\n"
        "    let plain <auto> = (n: int) <int> => id(n) + 1;\n"
        "    printf(\"%d\\n\", plain(41));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionBesideAGenericLambdaCallsIt) {
    // The same carry into the other kind of body written inside a body. Both are pinned
    // because they are two fillers of one hand-over: a body nobody filled it for has to
    // see none rather than the last filler's set.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let id <auto> = fun <T>(x: T) <T> { return x; };\n"
        "    fun helper(n: int) <int> { return id(n) * 2; }\n"
        "    printf(\"%d\\n\", helper(21));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericLambdaInsideAGenericFunctionSeesTheOuterParameter) {
    // `fun <U>(y: U) <S>` written inside `fun outer<S>` mentions a type parameter that is
    // not its own. An instance built with only its own binding installed would refuse `S`
    // as a type it does not know, so the bindings that were active at the *declaration*
    // are snapshotted and reinstalled under the lambda's own -- under, so a lambda
    // reusing the name shadows the outer one.
    const Built b = build(std::string(kPrintf) +
        "fun outer<S>(v: S) <S> {\n"
        "    let pass <auto> = fun <U>(y: U) <S> { return y; };\n"
        "    return pass(v);\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %.1f\\n\", outer::<int>(5), outer::<double>(cast<double>(2.5)));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5 2.5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericLambdaReusingTheOuterTypeParameterName) {
    // The shadowing the order above buys, and `sizeof` is what makes the two readings
    // give different numbers: inside `outer<char>` the lambda's own `T` is bound to int by
    // its argument, so `sizeof(T)` in its body is 4 -- and would be 1 if the outer binding
    // won. 4 * 10 + 1.
    const Built b = build(std::string(kPrintf) +
        "fun outer<T>(v: T) <int> {\n"
        "    let sz <auto> = fun <T>(x: T) <int> { return cast<int>(sizeof(T)); };\n"
        "    return sz(1) * 10 + cast<int>(sizeof(T));\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", outer::<char>(cast<char>(3)));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "41\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AModuleScopeGenericLambdaIsRefused) {
    // Inside a body the declaration registers a template and emits nothing; at module
    // scope there is nowhere to register it, because the table is pushed and popped with
    // the scopes and declareGlobals runs before any body has one. Refused with the
    // question named, so a reader is sent to "where does a module-scope template live"
    // rather than to the value boundary, which is not what is in the way.
    const Built b = build(std::string(kPrintf) +
        "let id <auto> = fun <T>(x: T) <T> { return x; };\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("the generic lambda 'id' declared at module scope"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASecondGenericLambdaOfOneNameInOneScopeIsRefused) {
    // One name over a slot, a symbol and a template in one scope is a state this file's
    // tables cannot all hold, and picking either silently runs the wrong body. The same
    // refusal emitNestedFunction gives one level along.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let id <auto> = fun <T>(x: T) <T> { return x; };\n"
        "    let id <auto> = fun <T>(x: T) <int> { return 1; };\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a second declaration of 'id' in one scope"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericLambdaCapturingALocalLowers) {
    // Was AGenericLambdaCapturingALocalIsRefused: the template boundary is not
    // relaxed by the lambda capturing, because an instance is a closure like
    // any other and carries its env beside its code. Inverted, not deleted.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let outer <int> = 7;\n"
        "    let id <auto> = fun <T>(x: T) <int> { return outer; };\n"
        "    printf(\"%d\\n\", id(1));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericClosureCapturingAnOuterParameterHasAPerInstanceEnv) {
    // Capture meets monomorphisation: `v` has a different type in each
    // instantiation of `outer`, so each instance builds its own env beside its
    // own code. One shared env would read an `int` slot as a `double`.
    const Built b = build(std::string(kPrintf) +
        "fun outer<S>(v: S) <S> {\n"
        "    let pass <auto> = fun <U>(y: U) <S> { return v; };\n"
        "    return pass(v);\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %.1f\\n\", outer::<int>(5), outer::<double>(cast<double>(2.5)));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5 2.5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericFunctionUsedAsAValueIsRefused) {
    // The named form of the same gap, and refused with a different message on purpose:
    // what is missing is not the representation but the code. `ident<int>` and
    // `ident<char>` are two functions and a bare `ident` names neither.
    //
    // `<auto>` and not `<fn(int) -> int>`, which is what this test asked for first: the
    // analyzer rejects the annotated form outright ("expected 'fn(int) -> int', got
    // 'fn(T) -> T'"), so codegen never saw it and the assertion could not have held.
    // The annotation is what has to go for the backend's boundary to be reachable at
    // all -- a refusal nothing can reach is not a boundary.
    const Built b = build(std::string(kPrintf) +
        "fun ident<T>(v: T) <T> { return v; }\n"
        "fun main() <noret> {\n"
        "    let f <auto> = ident;\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("the generic function 'ident' used as a value"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AVariadicFunctionUsedAsAValueIsRefused) {
    // `printf` has a signature no `fn` type can spell -- there is no `...` in the
    // grammar of one -- so a value of it would have to advertise a type the code does
    // not have. Refused rather than given the non-variadic type it is not, which would
    // put every argument past the first in the wrong place.
    //
    // `<auto>` for the same reason as the test above: with an annotation the analyzer
    // reports the mismatch and the backend is never asked.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let f <auto> = printf;\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("the function 'printf' used as a value"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFunctionValueOfTheWrongSignatureDoesNotCompile) {
    // The trap opaque pointers set, and this test is honest about which pass springs it.
    // Every `fn` is `ptr`, so convert()'s identity shortcut -- "same llvmType, no cast
    // needed" -- is true for *any* pair of function values, and left to it this would
    // compile a two-argument callee into a one-argument call and read a register nobody
    // set. So the signature comparison sits in front of that shortcut.
    //
    // Today the analyzer is what rejects this, and it rejects every shape the backend's
    // guard could otherwise be reached through: annotated, inferred-then-reassigned, and
    // through a struct field were all tried and all reported "Type mismatch: expected
    // 'fn(int) -> int', got 'fn(int, int) -> int'" before codegen ran. The backend's
    // guard is therefore unreachable-by-construction rather than exercised here, and it
    // is kept anyway on this file's standing rule: a construct the backend cannot lower
    // is refused *here*, whatever anything upstream also happens to say. What this test
    // pins is the property that actually matters -- the program does not build -- so
    // that a loosening on either side of the boundary is caught by somebody.
    const Built b = build(std::string(kPrintf) +
        "fun add(x: int, y: int) <int> { return x + y; }\n"
        "fun main() <noret> {\n"
        "    let f <fn(int) -> int> = add;\n"
        "    printf(\"%d\\n\", f(1));\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericFunctionTypeIsRefusedAndNamedInFull) {
    // lambdas.fin:69's annotation, `fn<T: Castable>(m: T) -> T`. Two things are being
    // asserted: that it refuses, and that the refusal spells the type out. A
    // FunctionTypeNode's own `name` is the bare word "fn", so before the speller learned
    // this shape every function type in every refusal read as "of type 'fn'" -- which
    // does not distinguish the generic one this file refuses from the plain one it
    // lowers, and those refuse for entirely different reasons.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let g <fn<T>(m: T) -> T>;\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("fn<...>(T) -> T"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFunctionValueSurvivesAStructFieldRoundTrip) {
    // A `fn` field, which stdlib/collection.fin:18 and :76 write and which this unit
    // makes representable. Worth its own test because a field is where a function value
    // stops being a register and becomes bytes at an offset: the store, the load and the
    // indirect call all have to agree about which pointer it is.
    const Built b = build(std::string(kPrintf) +
        "struct Ops {\n"
        "    apply <fn(int) -> int>\n"
        "}\n"
        "fun twice(v: int) <int> { return v * 2; }\n"
        "fun main() <noret> {\n"
        "    let o <Ops> = Ops { apply: twice };\n"
        "    let f <fn(int) -> int> = o.apply;\n"
        "    printf(\"%d\\n\", f(21));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

// ---------------------------------------------------------------------------
// A `fun` written inside another body.
//
// It lowers to an ordinary function with internal linkage and a generated symbol,
// `fin.nested.<n>.<name>`. That is derived from the front end rather than chosen: the
// analyzer registers a nested declaration in the enclosing *body's* scope
// (Analyzer_Decl.cpp, visit(FunctionDeclaration&) step 6, which defines the name in
// `currentScope->parent`), so the name is visible from the declaration to the end of
// that scope and nowhere else. A sibling function cannot call it, a call written
// *above* it is "Undefined function or type" from the front end, and two bodies may
// each declare `helper` without colliding. A body reached by name from one scope and
// by nothing else is a function nothing outside can name; there is nothing else it
// could be.
//
// So it is deliberately *not* a closure, and the second half of this slice is what
// makes the first half safe -- the same bargain the lambdas above strike. The corpus
// has exactly one nested function, loops.fin:40's `recursive`, and it reads its own
// parameter and calls itself; it captures nothing. A body that read the enclosing
// frame would be reading a frame that is gone by the time an `fn` value calls it, so
// a read of an enclosing local is refused *as a capture* -- ANestedFunctionCapturing
// AnEnclosingLocalIsRefused -- through the machinery a lambda already had, with
// `captureKind_` the only difference between the two messages.
//
// Two tables over one scope stack, and not one: a local is a frame slot and a nested
// function is a symbol, and `nestedFor` walks the two in lockstep innermost-first so
// that a name resolves in the scope the analyzer resolved it in. A collision in the
// same scope is refused in either direction, because the analyzer *overwrites* the
// symbol there -- `let h; fun h()` leaves the variable unnameable while its storage
// is still live -- and there is no state this file's two tables could both hold.

BACKEND_TEST(Soundness_Codegen, ANestedFunctionIsCalledFromTheBodyItIsWrittenIn) {
    // loops.fin:40-46 in shape: the declaration inside `main`, the call below it. This
    // is the sample line the unit exists for, and the whole of `loops.fin` compiles to
    // an object for the first time because of it.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    fun recursive(a: int) <int> {\n"
        "        if (a == 0) { return 0; }\n"
        "        return recursive(a - 1) + a;\n"
        "    }\n"
        "    printf(\"%d\\n\", recursive(5));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "15\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionCallsItselfRatherThanASecondCopy) {
    // The registration order this depends on is the one thing about the lowering that
    // could plausibly have gone the other way: the name has to be in the table *before*
    // its own body is emitted, or `fact(n - 1)` inside `fact` resolves to nothing and
    // the call is refused. A factorial rather than a countdown, so a wrong answer is a
    // wrong number and not merely a missing one.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    fun fact(n: int) <int> {\n"
        "        if (n <= 1) { return 1; }\n"
        "        return n * fact(n - 1);\n"
        "    }\n"
        "    printf(\"%d\\n\", fact(5));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "120\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TwoBodiesEachDeclareTheirOwnFunctionOfOneName) {
    // The property that makes the generated symbol necessary rather than decorative.
    // Both are called `helper` and neither is visible to the other, so publishing the
    // written name would be a duplicate definition -- and `functions_` keeps the first
    // declaration of a name, so the second body would silently have been the first one's
    // code. Both values are printed, because a test that read one would pass either way.
    const Built b = build(std::string(kPrintf) +
        "fun a() <int> { fun helper() <int> { return 1; } return helper(); }\n"
        "fun b() <int> { fun helper() <int> { return 2; } return helper(); }\n"
        "fun main() <noret> { printf(\"%d %d\\n\", a(), b()); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 2\n") << b.why();

    // Internal, and under a name no Fin program can write: the lexer has no `.` in an
    // identifier, so `fin.nested.<n>.<name>` cannot collide with anything a writer
    // spells, and the counter only goes up.
    const std::string trace = codegenTrace(
        std::string(kPrintf) +
        "fun a() <int> { fun helper() <int> { return 1; } return helper(); }\n"
        "fun b() <int> { fun helper() <int> { return 2; } return helper(); }\n"
        "fun main() <noret> { printf(\"%d %d\\n\", a(), b()); }\n");
    EXPECT_NE(trace.find("declared fin.nested.0.helper"), std::string::npos) << trace;
    EXPECT_NE(trace.find("declared fin.nested.1.helper"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionTakesArgumentsAndReturnsAValue) {
    // The ordinary case, which is worth measuring because the arguments go through
    // emitCallArgs exactly as a module-scope call's do: a nested call that skipped it
    // would pass an argument of the wrong width without saying so. The argument is a
    // local, so this is also the pair to ANestedFunctionCapturingAnEnclosingLocalIsRefused
    // -- passing `n` in is the supported way to write what a capture would have read.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    fun triple(x: int) <int> { return x * 3; }\n"
        "    let n <int> = 2;\n"
        "    printf(\"%d\\n\", triple(n));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "6\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionWithNoReturnValueLowers) {
    // `<noret>`, and a call in statement position. The implicit tail of a nested body is
    // the same tail every other body gets, so a missing `ret void` would be invalid IR
    // rather than a wrong value -- which is why this is a separate test from the ones
    // that read a result.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    fun show(x: int) <noret> { printf(\"%d\\n\", x); }\n"
        "    show(5);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "5\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionCallsAnEarlierSiblingAndNotALaterOne) {
    // The visible set is the set *at the declaration*, which is why the body is emitted
    // there rather than queued to the end of the enclosing one. Both halves, because the
    // asymmetry is the measurement: `early` calling `later` written below it is refused,
    // and by the front end rather than here -- the analyzer defines the name at the
    // declaration, so a call above it has no name to resolve.
    const Built lowered = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    fun later() <int> { return 1; }\n"
        "    fun early() <int> { return later(); }\n"
        "    printf(\"%d\\n\", early());\n"
        "}\n");
    ASSERT_TRUE(lowered.ran) << lowered.why();
    EXPECT_EQ(lowered.out, "1\n") << lowered.why();

    const Built refused = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    fun early() <int> { return later(); }\n"
        "    fun later() <int> { return 1; }\n"
        "    printf(\"%d\\n\", early());\n"
        "}\n");
    EXPECT_NE(refused.compileExit, 0) << refused.why();
    EXPECT_NE(refused.compileErr.find("Undefined function or type 'later'"),
              std::string::npos) << refused.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionMayBeWrittenInsideANestedFunction) {
    // Nothing in the lowering is limited to one level, and the level is where a wrong
    // answer would hide: the inner declaration goes into the innermost scope's table, so
    // an implementation that kept one flat table per function would still pass every
    // single-level test and get this one's visibility wrong.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    fun a() <int> { return 1; }\n"
        "    fun b() <int> {\n"
        "        fun c() <int> { return a() + 10; }\n"
        "        return c();\n"
        "    }\n"
        "    printf(\"%d\\n\", b());\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "11\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionIsUsedAsAFunctionValue) {
    // A nested function is a bare code pointer for the same reason a module-scope one is,
    // and it is the refusal of a capture that entitles it to be: with nothing closed
    // over there is no second word for a pair to hold. Both spellings of handing one
    // over -- into a variable of `fn` type, and as an argument to a parameter of one.
    const Built b = build(std::string(kPrintf) +
        "fun apply(f: fn(int) => int, v: int) <int> { return f(v); }\n"
        "fun main() <noret> {\n"
        "    fun dbl(x: int) <int> { return x * 2; }\n"
        "    let g <fn(int) -> int> = dbl;\n"
        "    printf(\"%d %d\\n\", g(6), apply(dbl, 21));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "12 42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionOutranksAModuleFunctionOfTheSameName) {
    // The shadowing the analyzer performs, measured from out here: step 6 *overwrites*
    // the symbol in the enclosing scope, so every read of the name inside that body means
    // the nested one. The module-scope `helper` is still itself everywhere else, and
    // `other()` is what proves it -- a lookup order that reached `functions_` first would
    // print `9 9`, and one that leaked the nested table out of the body would print
    // `4 4`.
    const Built b = build(std::string(kPrintf) +
        "fun helper() <int> { return 9; }\n"
        "fun other() <int> { return helper(); }\n"
        "fun main() <noret> {\n"
        "    fun helper() <int> { return 4; }\n"
        "    printf(\"%d %d\\n\", helper(), other());\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4 9\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionOutranksAGlobalOfTheSameName) {
    // The other direction of the same rule, and the one that says which of the two tables
    // wins: a global is a symbol with storage and a nested function is a symbol with
    // code, so a name that is both has to resolve to the inner one -- which is where the
    // analyzer resolved it.
    const Built b = build(std::string(kPrintf) +
        "let n <int> = 5;\n"
        "fun main() <noret> {\n"
        "    fun n() <int> { return 1; }\n"
        "    printf(\"%d\\n\", n());\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionIsScopedToTheBlockItIsWrittenIn) {
    // A block is a scope, so a declaration in one ends with it. Both halves: the call
    // inside the block runs, and the same call after it has no name to resolve -- which
    // the front end says, because the analyzer's scope is the same scope.
    const Built inside = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    {\n"
        "        fun h() <int> { return 1; }\n"
        "        printf(\"%d\\n\", h());\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(inside.ran) << inside.why();
    EXPECT_EQ(inside.out, "1\n") << inside.why();

    const Built after = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    {\n"
        "        fun h() <int> { return 1; }\n"
        "    }\n"
        "    printf(\"%d\\n\", h());\n"
        "}\n");
    EXPECT_NE(after.compileExit, 0) << after.why();
    EXPECT_NE(after.compileErr.find("Undefined function or type 'h'"), std::string::npos)
        << after.why();
}

BACKEND_TEST(Soundness_Codegen, AnInnerBlocksNestedFunctionShadowsAnOuterOneAndThenStops) {
    // Shadowing in both directions across a block boundary, which is what `nestedFor`'s
    // innermost-first walk is for. Printed as a pair in one program, because the failure
    // this catches is a table that resolved to the outer declaration inside the block
    // (`1 1`) or kept the inner one alive after it (`2 2`).
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    fun h() <int> { return 1; }\n"
        "    {\n"
        "        fun h() <int> { return 2; }\n"
        "        printf(\"%d \", h());\n"
        "    }\n"
        "    printf(\"%d\\n\", h());\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AVariableInAnInnerBlockShadowsANestedFunctionOfTheName) {
    // The reverse collision, one scope apart rather than in the same scope -- which is
    // the case that is *not* refused, because the two tables can both hold it and the
    // analyzer resolved it to the inner name. `nestedFor` walking the locals in lockstep
    // is what answers this correctly; reading the nested table to exhaustion first would
    // print `1` and call a function where the program named a variable.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    fun h() <int> { return 1; }\n"
        "    {\n"
        "        let h <int> = 9;\n"
        "        printf(\"%d \", h);\n"
        "    }\n"
        "    printf(\"%d\\n\", h());\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionIsWrittenInsideAStructMethod) {
    // A method body is a body, and a method arrives here from a queue rather than from
    // the module walk -- so this is the one that checks the carried set is per-body and
    // not a leftover from whoever filled it last. `self` is read by the *method*, and the
    // nested function is handed the field as an argument, which is the supported shape.
    const Built b = build(std::string(kPrintf) +
        "struct S {\n"
        "    v <int>,\n"
        "    fun get(self: &Self) <int> {\n"
        "        fun bump(x: int) <int> { return x + 1; }\n"
        "        return bump(self.v);\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S> = S { v: 41 };\n"
        "    printf(\"%d\\n\", s.get());\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionInATemplateIsEmittedOncePerInstantiation) {
    // A template's body is emitted per distinct binding, so a nested function inside one
    // is too -- and it has to be, because its parameter type is the template's `T`. Two
    // bindings, and the trace counts the bodies: one shared symbol would be a function
    // whose parameter is `int` being called with a `long`.
    const std::string code = std::string(kPrintf) +
        "fun twice_of<T>(a: T) <T> {\n"
        "    fun twice(x: T) <T> { return x + x; }\n"
        "    return twice(a);\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d\\n\", twice_of(3),\n"
        "           cast<int>(twice_of(cast<long>(4))));\n"
        "}\n";
    const Built b = build(code);
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "6 8\n") << b.why();

    const std::string trace = codegenTrace(code);
    EXPECT_EQ(occurrences(trace, "declared fin.nested."), 2u) << trace;
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionIsWrittenInsideALambda) {
    // A lambda's body is a body like any other, and it pushes a scope of its own -- so a
    // `fun` written in one belongs to the lambda and not to the function the lambda sits
    // in. The value is what says the call went to the right place.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let f <auto> = fun () <int> {\n"
        "        fun k() <int> { return 4; }\n"
        "        return k();\n"
        "    };\n"
        "    printf(\"%d\\n\", f());\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ALambdaCallsANestedFunctionBesideIt) {
    // The pair to ALambdaCapturingALocalIsRefused, and the reason the two differ: a local
    // is a slot in a frame the lambda's code pointer cannot reach, and a nested function
    // is a symbol that needs no frame at all. So a lambda written beside `fun one()` emits
    // the same call the enclosing body would, and refusing it would have been a boundary
    // with nothing behind it.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    fun one() <int> { return 1; }\n"
        "    let f <auto> = fun (x: int) <int> { return x + one(); };\n"
        "    printf(\"%d\\n\", f(41));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionReadsAGlobalAndCallsAModuleFunction) {
    // Neither is a capture, and both are worth pinning because the refusal above is
    // written as a *name* check: a rule that had matched too eagerly would stop a body
    // from reading a global or calling a sibling of the enclosing function, which is what
    // lambdas.fin:58 does one construct over.
    const Built b = build(std::string(kPrintf) +
        "let G <int> = 6;\n"
        "fun other() <int> { return 8; }\n"
        "fun main() <noret> {\n"
        "    fun h() <int> { return G + other(); }\n"
        "    printf(\"%d\\n\", h());\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "14\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionCapturingAnEnclosingLocalIsRefused) {
    // The boundary the bare-pointer representation sits behind, one construct over from
    // ALambdaCapturingALocalIsRefused and refused by the same code. `n` lives in `main`'s
    // frame; the nested function is reachable as an `fn` value that outlives no frame it
    // can see, so lowering the read would either load a dead slot or silently pass a
    // different value.
    //
    // Named as a capture and not as an unknown name, because the two send a reader
    // somewhere different: "the name 'n'" reads as a front-end bug and this is a
    // deliberate stop. Paired with the supported spelling, which is to pass it in.
    const Built refused = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let n <int> = 1;\n"
        "    fun h() <int> { return n; }\n"
        "    printf(\"%d\\n\", h());\n"
        "}\n");
    EXPECT_NE(refused.compileExit, 0) << refused.why();
    EXPECT_NE(refused.compileErr.find("a nested function capturing 'n'"),
              std::string::npos) << refused.why();
    // One finding and not two: the refused declaration's name is poisoned, so the call
    // below it is suppressed rather than reported as a call that is not lowered -- which
    // would send a reader to implement a call that already works.
    EXPECT_EQ(occurrences(refused.compileErr, "codegen: "), 1u) << refused.why();

    const Built lowered = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let n <int> = 1;\n"
        "    fun h(v: int) <int> { return v; }\n"
        "    printf(\"%d\\n\", h(n));\n"
        "}\n");
    ASSERT_TRUE(lowered.ran) << lowered.why();
    EXPECT_EQ(lowered.out, "1\n") << lowered.why();
}

BACKEND_TEST(Soundness_Codegen, AnInnerNestedFunctionCapturingAnOuterBodysLocalIsRefused) {
    // Two levels in, and still a capture: the frame `n` is in is no more reachable from
    // the inner function than from the outer one. Said as a capture rather than as an
    // unknown name, which is what makes this its own test -- the enclosing names have to
    // *accumulate* down the nesting, and dropping the outer set would leave this read
    // reported as "the name 'n'".
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let n <int> = 1;\n"
        "    fun outer() <int> {\n"
        "        fun inner() <int> { return n; }\n"
        "        return inner();\n"
        "    }\n"
        "    printf(\"%d\\n\", outer());\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a nested function capturing 'n'"), std::string::npos)
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionWithNoBodyIsRefused) {
    // `fun h() <int>;` inside a body. At module scope this is a prototype for a definition
    // elsewhere; here "elsewhere" is a scope that ends with this one, so nothing outside
    // can define it and nothing inside is obliged to. Treating it as an extern would emit
    // a call to a symbol no object file contains, which is a link error for a program the
    // compiler said was fine.
    const Built b = build(
        "fun main() <noret> {\n"
        "    fun h() <int>;\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a nested function 'h' with no body"), std::string::npos)
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedGenericFunctionIsRefused) {
    // A template is not code until a call says what its type parameters are, and the
    // table that holds one (`fnTemplates_`) is keyed by the written name with no scope in
    // it -- so a nested template of a name the module also uses would silently be one or
    // the other. Refused until a program asks; paired with the same body at module scope,
    // which instantiates.
    const Built refused = build(
        "fun main() <noret> {\n"
        "    fun g<T>(a: T) <T> { return a; }\n"
        "}\n");
    EXPECT_NE(refused.compileExit, 0) << refused.why();
    EXPECT_NE(refused.compileErr.find("a nested generic function 'g'"), std::string::npos)
        << refused.why();

    const Built lowered = build(std::string(kPrintf) +
        "fun g<T>(a: T) <T> { return a; }\n"
        "fun main() <noret> { printf(\"%d\\n\", g(3)); }\n");
    ASSERT_TRUE(lowered.ran) << lowered.why();
    EXPECT_EQ(lowered.out, "3\n") << lowered.why();
}

BACKEND_TEST(Soundness_Codegen, AnAttributeOnANestedFunctionIsRefused) {
    // Not even `#[llvm_name]`, which the module-scope path does read. That attribute names
    // the symbol a declaration publishes and a nested function publishes none: honouring
    // it would put an externally visible name on a function only one scope can call, and
    // ignoring it would drop an attribute the writer expected to change the object.
    // Paired with the same attribute at module scope, where it renames the symbol.
    const Built refused = build(
        "fun main() <noret> {\n"
        "    #[llvm_name=\"zz\"]\n"
        "    fun h() <int> { return 1; }\n"
        "}\n");
    EXPECT_NE(refused.compileExit, 0) << refused.why();
    EXPECT_NE(refused.compileErr.find("the attribute 'llvm_name' on the nested function 'h'"),
              std::string::npos) << refused.why();

    const std::string trace = codegenTrace(
        "#[llvm_name=\"zz\"]\n"
        "fun h() <int> { return 1; }\n"
        "fun main() <noret> { }\n");
    EXPECT_EQ(trace.find("not lowered yet"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, PubOnANestedFunctionIsRefused) {
    // The grammar accepts it inside a body. `pub` says what a *module's* scope hands to an
    // import, and a nested function is not in one -- so there is nothing for the keyword
    // to make public, and accepting it would be this pass claiming to have honoured what
    // nothing honoured. Paired with the identical body without the keyword.
    const Built refused = build(
        "fun main() <noret> {\n"
        "    pub fun h() <int> { return 1; }\n"
        "}\n");
    EXPECT_NE(refused.compileExit, 0) << refused.why();
    EXPECT_NE(refused.compileErr.find("'pub' on the nested function 'h'"),
              std::string::npos) << refused.why();

    const Built lowered = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    fun h() <int> { return 1; }\n"
        "    printf(\"%d\\n\", h());\n"
        "}\n");
    ASSERT_TRUE(lowered.ran) << lowered.why();
    EXPECT_EQ(lowered.out, "1\n") << lowered.why();
}

BACKEND_TEST(Soundness_Codegen, ANestedFunctionCollidingWithALocalInTheSameScopeIsRefused) {
    // `let h <int> = 3; fun h() <int> { ... }`, one scope. The analyzer overwrites the
    // symbol, so every read of `h` after the declaration means the function and the
    // variable becomes unnameable while its storage is still live. One name over a slot
    // and a symbol in the same scope is a state the two tables here cannot both hold, and
    // picking either silently gets a program wrong. Paired with the same two names one
    // block apart, which AVariableInAnInnerBlockShadowsANestedFunctionOfTheName runs.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let h <int> = 3;\n"
        "    fun h() <int> { return 1; }\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a nested function 'h' whose name a variable in the same "
                                "scope already has"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ASecondNestedFunctionOfOneNameInOneScopeIsRefused) {
    // `declareFunction` keeps the first declaration of a name, so the second body would
    // silently not be the one that runs -- the same reason a second method of one name on
    // a struct is refused, one level in. Two bodies with different values, so that a
    // silent pick would be a wrong answer and not merely an ambiguous one.
    const Built b = build(
        "fun main() <noret> {\n"
        "    fun h() <int> { return 1; }\n"
        "    fun h() <int> { return 2; }\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a second nested function 'h' in one scope"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, BreakInsideANestedFunctionInsideALoopIsRefusedAsOutsideALoop) {
    // The loop stack belongs to the function being emitted, and a body emitted from inside
    // one starts with none. Before this it inherited the caller's, so a `break` here
    // branched to a block in *another function* -- `Referring to a basic block in another
    // function!`, which surfaced as "emitted invalid IR" and named a generated symbol
    // rather than the construct the program wrote. The analyzer permits the spelling
    // because it sees the enclosing loop, so this pass is where it stops. Both keywords,
    // and the lambda spelling beside the nested-function one, because one stack fix covers
    // all four.
    for (const char* body : {"fun h() <int> { break; return 1; }",
                             "fun h() <int> { continue; return 1; }",
                             "let f <auto> = fun () <int> { break; return 1; };",
                             "let f <auto> = fun () <int> { continue; return 1; };"}) {
        const Built b = build(std::string(kPrintf) +
            "fun main() <noret> {\n"
            "    let i <int> = 0;\n"
            "    while (i < 3) {\n"
            "        " + body + "\n"
            "        i = i + 1;\n"
            "    }\n"
            "}\n");
        EXPECT_NE(b.compileExit, 0) << body << "\n" << b.why();
        EXPECT_NE(b.compileErr.find("outside a loop"), std::string::npos)
            << body << "\n" << b.why();
        EXPECT_EQ(b.compileErr.find("invalid IR"), std::string::npos)
            << body << "\n" << b.why();
    }
}

// ---------------------------------------------------------------------------
// `blame`, the assert form.
//
// One keyword, two statements, told apart by the operand's type and by nothing else
// because they are written identically (SemanticAnalyzer::visit(BlameStatement&)):
// `blame val > 0` asserts, `blame CollectionError("...")` raises. That split is what
// let the assert form land on its own, and the corpus is what made the split worth
// making -- every `blame` in a sample this backend gets as far as is an assert.
// blame_assert.fin:5 and :8, arrays.fin:34, deeptest4.fin:16 and :17,
// readonly.fin:56. The four raises are all in samples the front end stops first.
//
// A failed assert prints where it failed and aborts: `fprintf` to stderr then
// `abort()`, no runtime and no unwinding. `llvm.trap` was rejected because it
// discards the message, and blame_assert.fin:5 wrote "Value must be positive"
// deliberately.
//
// Deliberately not catchable. `abort` unwinds nothing, so a `blame` inside a `try`
// would leave the `catch` unreached -- and nothing in the corpus puts one there:
// readonly.fin's `try` wraps `a.v1 = 5` and its `blame` at :56 is outside it. A
// mechanism for an unevidenced case would be a mechanism nothing checks.
//
// One detail these tests used to be explicit about, and no longer have to be: the
// file half of the location read `<input>`, because nothing handed the backend the
// source path -- a node's `loc` carries a null filename (the lexer initialises every
// one that way), the AST has no path field, and DiagnosticEngine keeps its copy
// private. `generateObject` takes a `sourceName`, defaulted to DiagnosticEngine's own
// `<input>`, and the driver now passes `options.inputFile` at both of its call sites,
// so a failed assertion names a file a person can open. AFailedBlameNamesAFileAndALine
// is where that arrived: it pinned the whole string, so the driver's one line turned it
// red on the spot, which is the only reason the change could not land half-done.

BACKEND_TEST(Soundness_Codegen, APassingBlameCostsTheProgramNothing) {
    // The ordinary case, and the one that must not print: an assertion that holds is a
    // branch not taken. Worth asserting on its own because the failing path is emitted
    // into the same function, so a mistake in the condition's sense -- swapping the two
    // successors of the CondBr -- would abort every correct program instead.
    const Built b = build(std::string(kPrintf) +
        "fun check(val: int) <void> {\n"
        "    blame val > 0, \"Value must be positive\";\n"
        "    blame val < 100;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    check(10);\n"
        "    printf(\"survived\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "survived\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFailedBlameWithAMessagePrintsItAndAborts) {
    // tests/samples/blame_assert.fin:5 verbatim, with an argument that fails it. The
    // message is the whole reason this lowering is not `llvm.trap`: the text the author
    // wrote is the only part of a failed assertion that says *why*.
    const Built b = build(
        "fun check(val: int) <void> {\n"
        "    blame val > 0, \"Value must be positive\";\n"
        "}\n"
        "fun main() <void> { check(-5); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_NE(b.runExit, 0) << b.why();
    EXPECT_NE(b.out.find(":2: assertion failed: Value must be positive"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFailedBlameWithNoMessageStillSaysWhere) {
    // blame_assert.fin:8's form. No message, so no `: why` -- and the line still has to
    // be there, because "assertion failed" with no location is the diagnostic that sends
    // a reader to grep their own program.
    const Built b = build(
        "fun check(val: int) <void> {\n"
        "    blame val < 100;\n"
        "}\n"
        "fun main() <void> { check(500); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_NE(b.runExit, 0) << b.why();
    EXPECT_NE(b.out.find(":2: assertion failed\n"), std::string::npos) << b.why();
    // Not the message form with an empty message: `assertion failed: ` with nothing
    // after the colon would be a format string chosen by accident.
    EXPECT_EQ(b.out.find("assertion failed:"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFailedBlameNamesAFileAndALine) {
    // The shape of the location, pinned whole -- both halves, one exact string, no
    // substring search. The file half is the path the driver was handed, which is why
    // the harness keeps it: asserting against `b.srcPath` and not a pattern is what
    // makes this test fail if the driver ever stops passing the path, or passes the
    // object's path, or the stem, instead of the source's.
    const Built b = build(
        "fun main() <void> {\n"
        "    let n <int> = 0;\n"
        "    blame n > 0, \"n must be positive\";\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_NE(b.runExit, 0) << b.why();
    // The program's own bytes, pinned whole. The shell running the executable
    // may append its own signal report after them -- "Aborted", with
    // "(core dumped)" when the platform dumps one -- and whether it does
    // depends on the shell and the core pattern, neither of which is this
    // compiler's contract. Only those two epilogues are excused.
    const std::string pinned = b.srcPath + ":3: assertion failed: n must be positive\n";
    EXPECT_TRUE(b.out == pinned || b.out == pinned + "Aborted\n" ||
                b.out == pinned + "Aborted (core dumped)\n")
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFailedBlameGoesToStderrAndNotToStdout) {
    // Where it goes is part of the contract: a program's stdout is its output and a
    // failed assertion is not output. The harness merges the two streams, so this test
    // separates them itself -- redirecting stdout to /dev/null and keeping stderr is the
    // only way to tell "printed to stderr" from "printed at all".
    const fs::path src = uniqueTempPath("fin_blame_stderr", ".fin");
    const fs::path exe = uniqueTempPath("fin_blame_stderr_exe");
    {
        std::ofstream f(src, std::ios::binary);
        const std::string code =
            "fun main() <void> {\n"
            "    let n <int> = 0;\n"
            "    blame n > 0, \"to stderr\";\n"
            "}\n";
        f.write(code.data(), (std::streamsize)code.size());
    }
    const FincRun c = runFinc({src.string(), "-o", exe.string()});
    ASSERT_EQ(c.exitCode, 0) << stripAnsi(c.err);
    ASSERT_TRUE(fs::exists(exe));

    const fs::path outOnly = uniqueTempPath("fin_blame_out");
    const fs::path errOnly = uniqueTempPath("fin_blame_err");
    fin::runProcess({exe.string()}, outOnly.string(), errOnly.string());
    const std::string onOut = readProcessOutput(outOnly.string());
    const std::string onErr = readProcessOutput(errOnly.string());

    EXPECT_EQ(onOut, "") << "a failed assertion reached stdout:\n" << onOut;
    EXPECT_NE(onErr.find("assertion failed: to stderr"), std::string::npos) << onErr;

    std::error_code ec;
    fs::remove(src, ec);
    fs::remove(exe, ec);
    fs::remove(outOnly, ec);
    fs::remove(errOnly, ec);
}

BACKEND_TEST(Soundness_Codegen, ABlamesMessageIsNotUsedAsAFormatString) {
    // The mistake that would look right on every message in the corpus. Passing the
    // message *as* printf's format makes a `%d` in it read a vararg nobody passed, which
    // prints a stack word and can fault. `"%s"` with the message as an argument is why
    // this is safe, and a message full of specifiers is the only test that can tell the
    // two apart.
    const Built b = build(
        "fun main() <void> {\n"
        "    let n <int> = 0;\n"
        "    blame n > 0, \"%d %s %n literal\";\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_NE(b.runExit, 0) << b.why();
    EXPECT_NE(b.out.find("assertion failed: %d %s %n literal"), std::string::npos)
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, ABlamedMessageIsEvaluatedOnlyOnTheFailingPath) {
    // The reason the message is emitted inside the failing block rather than beside the
    // condition. The analyzer requires the message to be a `string`, not a *literal*, so
    // it may be a call -- and evaluating it where the condition is would run its side
    // effects on every pass of an assertion that never fires. A loop makes the
    // difference observable: `why()` prints, so emitting it eagerly would print three
    // times for a program whose assertion always holds.
    const Built b = build(std::string(kPrintf) +
        "fun why() <string> {\n"
        "    printf(\"evaluated\\n\");\n"
        "    return \"reason\";\n"
        "}\n"
        "fun main() <noret> {\n"
        "    for (let i <int> = 0; i < 3; i++) {\n"
        "        blame i < 100, why();\n"
        "    }\n"
        "    printf(\"done\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "done\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ABlameRaisingAValueIsRefused) {
    // The other half of the keyword, and still refused: a raised value needs a runtime
    // shape nobody has ruled on. Refused *as a raise* and not as "this condition",
    // because the two forms are written identically and a reader told "condition" would
    // go looking for a comparison they never wrote.
    const Built b = build(
        "struct MyError { code <int> }\n"
        "fun main() <void> {\n"
        "    blame MyError { code: 1 };\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("'blame' raising a value"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ABlameSharesADeclarationOfAbortWithTheProgramsOwn) {
    // `abort` and `fprintf` go through runtimeFn, which is what `new` and `delete`
    // already use for `malloc` and `free`: a Fin program that declared the same libc
    // entry point itself shares the declaration instead of colliding with it. A
    // *conflicting* declaration refuses rather than calling through a mismatched
    // signature, which would link and put the arguments in the wrong places.
    const Built b = build(
        "@define abort(code: int) <noret>;\n"
        "fun main() <void> {\n"
        "    let n <int> = 5;\n"
        "    blame n > 0;\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("'abort' is declared here with a different signature"),
              std::string::npos) << b.why();
}

// ---------------------------------------------------------------------------
// `#[global]`, from the backend's side (ADR 0021).
//
// The attribute's whole meaning is a front-end one: the analyzer publishes the marked
// name into a scope every file's lookup reaches, so a call resolves with no import. It
// asks this file for nothing -- an extern has no body to emit and no linkage to choose.
//
// That is why it is accepted here rather than refused, and it is the only attribute
// besides a valued `#[llvm_name]` that is. The refusal is the default because an
// attribute this file cannot read may be the one that decides linkage or which of two
// definitions wins; `#[global]` provably decides neither, and the front end has already
// acted on it by the time a program gets here.
//
// Accepted on an `@define` and nowhere else, because an `@define` is the only shape that
// publishes. `#[global]` on a `fun` or a `struct` parses, validates, and then binds
// nothing anywhere -- so accepting it there would be this file claiming to have honoured
// what nothing honoured.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AGlobalExternWrittenInTheRootFileLowersAndRuns) {
    // The whole path, in one file and with no module involved: `namespace std` stamps the
    // attribute, the analyzer publishes the name, and this file has to lower the call
    // anyway. Before `#[global]` was accepted here the program failed to build with
    // `the attribute 'global' on a '@define' is not lowered yet` -- a refusal of the one
    // attribute whose job was already finished.
    const Built b = build(
        "namespace std {\n"
        "#[global]\n"
        "@define printf(fmt: string, ...) <noret>;\n"
        "}\n"
        "fun main() <noret> { printf(\"%d\\n\", 42); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGlobalExternKeepsItsLlvmNameRename) {
    // The two attributes together, which is how lib/std/stdio.fin writes it. They are
    // read by different passes and the accepted-attribute list must not have made the
    // second unreadable: `#[global]` published the Fin name, `#[llvm_name]` says which C
    // symbol it calls, and a build that honoured the first and dropped the second would
    // link against a symbol named `finprint` that nothing defines.
    const std::string code =
        "namespace std {\n"
        "#[llvm_name=\"printf\"]\n"
        "#[global]\n"
        "@define finprint(fmt: string, ...) <noret>;\n"
        "}\n"
        "fun main() <noret> { finprint(\"renamed\\n\"); }\n";
    const Built b = build(code);
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "renamed\n") << b.why();

    // And the rename is the reason it ran, not a coincidence: the trace names the Fin
    // name, and the symbol the object asks for is the C one.
    const std::string trace = codegenTrace(code);
    EXPECT_NE(trace.find("declared finprint"), std::string::npos) << trace;
    EXPECT_EQ(trace.find("not lowered yet"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_Codegen, GlobalIsAcceptedOnAnExternAndRefusedOnAFunction) {
    // The bound on the exception, asserted as a pair so the two halves cannot drift.
    // Only a `DefineDeclaration` publishes -- measured: `#[global]` on a `fun`, a
    // `let`, a `struct`, an `interface`, an `enum` and a `type` all parse and validate
    // and every one of the six is still undefined in a consuming file -- so a `fun`
    // carrying it has been given nothing by the front end, and accepting it here would
    // be a claim to have done something with it.
    const Built onFunction = build(std::string(kPrintf) +
        "namespace std {\n"
        "#[global]\n"
        "pub fun helper() <int> { return 1; }\n"
        "}\n"
        "fun main() <noret> { printf(\"%d\\n\", helper()); }\n");
    EXPECT_NE(onFunction.compileExit, 0) << onFunction.why();
    EXPECT_NE(onFunction.compileErr.find("the attribute 'global' on a function"),
              std::string::npos) << onFunction.why();

    const Built onExtern = build(
        "namespace std {\n"
        "#[global]\n"
        "@define printf(fmt: string, ...) <noret>;\n"
        "}\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    EXPECT_EQ(onExtern.compileExit, 0) << onExtern.why();
}

BACKEND_TEST(Soundness_Codegen, AnExportOnAnExternIsAcceptedBesideAGlobal) {
    // The list grew by one entry, and the case that asked is the one the old
    // refusal named for itself. lib/std carries `#[export]` beside `#[global]`
    // on its `@define`s (env.fin, time.fin), and the standard library has to
    // compile standalone with `-c` -- not only in the spliced form where the
    // loader keeps `#[llvm_name]` alone (ModuleLoader::appendAmbientPrototypes).
    // ADR 0033 rules export import-visibility only, the same kind of front-end
    // fact `#[global]` is, and an extern's linkage is external whatever is
    // written above it because there is nothing to emit. Accepting it here
    // claims nothing about the object file, which is why the acceptance test
    // runs the program rather than merely compiling it.
    const Built b = build(
        "namespace std {\n"
        "#[global]\n"
        "#[export]\n"
        "@define printf(fmt: string, ...) <noret>;\n"
        "}\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ABlameAbortsRatherThanFallingThroughToLaterCode) {
    // The failing block ends in `unreachable` and not in a branch to the surviving one.
    // A branch would tell every later pass that execution continues past a failed
    // assertion, and the observable consequence of getting it wrong is exactly this: the
    // statement after the `blame` runs.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let n <int> = 0;\n"
        "    blame n > 0, \"stop here\";\n"
        "    printf(\"REACHED\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_NE(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out.find("REACHED"), std::string::npos) << b.why();
}

// ---------------------------------------------------------------------------
// `format!`, the one macro the compiler implements.
//
// ADR 0023 step 7. Everything above it is a front end: the analyzer type-checks a
// `format!` call against a signature it holds in a table (BuiltinMacros.cpp) and the
// expander deliberately leaves the invocation standing, because a macro with no body
// has no template to substitute into. So the node arrives here, and this file is the
// implementation -- which is the whole argument the ADR makes for `format!` not being
// a `@macro`: expansion runs before any type is known, and `{}` needs a conversion
// chosen per argument.
//
// The lowering is C's own idiom. `snprintf(null, 0, fmt, ...)` measures, `malloc`
// takes the length plus a NUL, `snprintf(buf, size, fmt, ...)` fills. The arguments
// are emitted once and the two calls share the values, because emitting an argument
// twice would run its side effects twice -- `format!("{}", next())` must advance once.
// `snprintf` and `malloc` are declared on demand through `runtimeFn`, the same channel
// `blame` reaches `fprintf`/`abort` through and `new` reaches `malloc` through.
//
// The conversion for each `{}` comes from the CgType the argument emitted to, and that
// is a deviation from the step's own wording worth stating: the ADR says "the type the
// analyzer recorded", and there is no such record -- `MacroInvocation` (MiscExpr.hpp:78)
// holds a name and a list of arguments and nothing else. The backend types every
// argument anyway, by emitting it, and that CgType is what selects the instruction. A
// second copy on the node could only ever disagree with it.
//
// Three details of the conversions are decisions rather than transcriptions:
//
//   - They must match the *promoted* value, not the written type. `promoteVararg` is
//     the C variadic convention -- float becomes double, anything narrower than an int
//     becomes an i32 -- so a `float` takes `%g` and an `int{8}` takes `%d`.
//   - 64-bit takes `%lld`, never `%ld`. A C `long` is 32 bits on Windows and 64 on
//     Linux; `long long` is 64 everywhere. The corpus writes `printf("%ld", n)` and
//     gets away with it on this platform; a format string the compiler writes itself
//     does not get to be that loose.
//   - `char` and `int8` are one row of Layout.cpp:39 (Int/8/signed), so nothing here
//     can tell them apart, and `%d` is the reading that never invents a character.
//     `format!("{}", 'A')` is "65". Print a character with `printf`, which is told the
//     conversion by the author.
//
// `%s` versus `%p` is decided by `CgType::pointee`, and that works because a `string`
// is the one pointer this backend builds with no pointee at all (byName's Pointer
// scalar; a bare `null` is the only other). Every other pointer goes through
// `mapPointer`/`pointerTo`, which set it. No new flag was needed.
//
// Nobody frees the buffer. A Fin `string` is an `i8*` with no owner and no length
// (ADR 0003), and deeptest2.fin:63 writes `return format!(...)` out of a method -- so a
// stack buffer would dangle at the return, and freeing at the right moment is what the
// tracing collector is for. The leak is deliberate and it is the same shape as `new`,
// which also never frees.
//
// One thing the language allows and this lowering does not: a format string that is
// not a literal. Translating `{}` is type-directed, so it happens at compile time and
// needs the text then; a runtime format would have to carry the conversions into a
// runtime loop as data. `tests/samples/stdlib/stdio.fin:36` is the only site in the
// corpus, in a sample the front end stops for other reasons, and it is refused by name
// here rather than silently printing a `{}` -- the third obligation of this suite is
// that a construct the backend cannot lower is refused and never skipped.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AFormatCallBuildsAStringThatRuns) {
    // The step's verification clause, and nothing more: a `-o` build of a `format!`
    // call runs and prints. Two placeholders from one value, because reusing an
    // argument is what a format string is for and a lowering that consumed each
    // value positionally as it scanned would fail exactly here.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let n <int> = 42;\n"
        "    let s <string> = format!(\"n = {}, again {}\", n, n);\n"
        "    printf(\"%s\\n\", s);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "n = 42, again 42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFormatDispatchesOnEachArgumentsOwnType) {
    // Seven types in one call, which is the per-argument half of the step. Read the
    // expected string carefully: `9000000000` is the `%lld` that `%ld` would also
    // print on Linux and would not on Windows, `1.5` is a float promoted to a double
    // and taking `%g`, `65` is `char` sharing a Layout row with `int8`, and `1` is a
    // `bool` widened to an int. Each of those is a decision the banner above argues
    // for, so each is pinned as an observable byte.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let i <int> = -7;\n"
        "    let u <uint> = 7;\n"
        "    let l <long> = 9000000000;\n"
        "    let f <float> = 1.5;\n"
        "    let t <string> = \"Ada\";\n"
        "    let c <char> = 'A';\n"
        "    let y <bool> = true;\n"
        "    let s <string> = format!(\"{} {} {} {} {} {} {}\", i, u, l, f, t, c, y);\n"
        "    printf(\"%s\\n\", s);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "-7 7 9000000000 1.5 Ada 65 1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, APointerFormatsAsAnAddressAndAStringAsItsBytes) {
    // The `pointee ? "%p" : "%s"` rule, from both sides in one program. Only the
    // string half can be pinned exactly -- an address is whatever the loader chose --
    // so the pointer half compares with the platform's `%p` output and, more to the point,
    // that the program did not walk an integer as if it were a character array.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let p <&int> = &x;\n"
        "    let s <string> = format!(\"[{}]\", p);\n"
        "    printf(\"%s\\n[%p]\\n\", s, p);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    const auto newline = b.out.find('\n');
    ASSERT_NE(newline, std::string::npos) << b.why();
    EXPECT_EQ(b.out.substr(0, newline + 1), b.out.substr(newline + 1)) << b.why();
}

BACKEND_TEST(Soundness_Codegen, APercentInAFormatStringIsText) {
    // A Fin format string is text, and text handed to snprintf as a format is a
    // vararg read the caller never made: `format!("{}%")` with the `%` passed
    // through would read a value off the stack that no argument put there. Same
    // reasoning as `blame`'s message going through `%s` rather than being the format.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let s <string> = format!(\"{}% done\", 100);\n"
        "    printf(\"%s\\n\", s);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "100% done\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFormatWithNoPlaceholdersIsStillAString) {
    // The degenerate call, worth a test because the lowering still has to allocate:
    // returning the literal's own global would hand back a pointer into read-only
    // memory, and every other `format!` result is a `malloc`'d buffer. One shape out,
    // whatever went in.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let s <string> = format!(\"no placeholders\");\n"
        "    printf(\"%s\\n\", s);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "no placeholders\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFormatResultOutlivesTheFunctionThatBuiltIt) {
    // deeptest2.fin:63's shape -- `return format!(...)` out of a method -- and the
    // reason the buffer comes from `malloc` rather than from an `alloca`. A stack
    // buffer passes this test's `printf` about as often as it does not, which is why
    // it is written as a return across a call boundary and not as a same-frame read.
    const Built b = build(std::string(kPrintf) +
        "fun describe(n: int) <string> { return format!(\"n=<{}>\", n); }\n"
        "fun main() <noret> {\n"
        "    printf(\"%s\\n\", describe(5));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "n=<5>\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFormatArgumentMayBeAnotherFormat) {
    // A `format!` is an expression of type `string`, so it is an argument to one --
    // and the inner call is a `string` with no pointee, which is what makes the outer
    // `{}` a `%s`. The recursion is in `emit`, and the two calls' argument vectors
    // must not share a builder position: an inner emission that left the insert point
    // somewhere else would put the outer `snprintf` in the wrong block.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let s <string> = format!(\"nested: {}\", format!(\"{}\", 3));\n"
        "    printf(\"%s\\n\", s);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "nested: 3\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADeclarationOfABuiltinMacroLowersToNothing) {
    // `lib/std/stdio.fin` writes the signature down so a reader can find it (ADR 0023
    // step 8), and a program that imports the standard library therefore has the
    // declaration in its AST. It must emit nothing and refuse nothing.
    //
    // Nothing is not a skip. `@define printf` emits an extern because it names a
    // linker symbol; a macro has none -- which is the ADR's argument for the compiler
    // being its only possible implementer -- so there is no declaration to write and
    // no definition being omitted.
    const Built b = build(std::string(kPrintf) +
        "@define format!(fmt: string, ...) <string>;\n"
        "fun main() <noret> {\n"
        "    printf(\"%s\\n\", format!(\"{}\", 1));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMacroWithABodyIsCompileTimeOnly) {
    // A user macro has no runtime representation; its declaration emits no symbol.
    const Built b = build(
        "@macro format(a) { return quote { $a; }; }\n"
        "fun main() <noret> { let v <int> = format!(1); }\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFormatWhoseFormatStringIsRuntimeTextIsRefused) {
    // The known gap, refused by name. `tests/samples/stdlib/stdio.fin:36` writes
    // `format!(fmt, ...objects)` with `fmt` a parameter, and it type-checks -- the
    // analyzer only requires a `string`. Printing the `{}` through instead would
    // produce a program that runs and lies, which is the one outcome this suite
    // treats as worse than a refusal.
    const Built b = build(std::string(kPrintf) +
        "fun show(fmt: string) <string> { return format!(fmt, 1); }\n"
        "fun main() <noret> { printf(\"%s\\n\", show(\"{}\")); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a 'format!' whose format string is not a literal"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFormatPlaceholderWithAnythingInsideIsRefused) {
    // `{}` is the whole placeholder syntax the ADR specifies. `{0}`, `{name}` and
    // `{:>8}` are all real syntax in other languages and none of them is read here, so
    // an unread `{0}` copied through would silently print itself while the value it
    // named went to the following placeholder or to nowhere.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let s <string> = format!(\"{0}\", 1);\n"
        "    printf(\"%s\\n\", s);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a 'format!' placeholder that is not '{}'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFormatWhosePlaceholdersAndValuesDisagreeIsRefused) {
    // Both directions, and they are not symmetric bugs: too few values makes snprintf
    // read a vararg nobody passed, too many makes a value vanish. Neither is checked
    // by the analyzer -- ADR 0023 leaves the variadic tail untyped on purpose, because
    // which conversion a value needs is a question for the code that builds the string
    // -- so this file is the only place the count can be counted.
    //
    // The message carries both numbers, which is why the scan keeps counting past the
    // last available conversion instead of refusing the moment it runs out: a partial
    // count in a diagnostic is worse than no count.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let s <string> = format!(\"{} {}\", 1);\n"
        "    printf(\"%s\\n\", s);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a 'format!' with 2 '{}' and 1 value"),
              std::string::npos) << b.why();

    const Built c = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let s <string> = format!(\"{}\", 1, 2);\n"
        "    printf(\"%s\\n\", s);\n"
        "}\n");
    EXPECT_NE(c.compileExit, 0) << c.why();
    EXPECT_NE(c.compileErr.find("a 'format!' with 1 '{}' and 2 values"),
              std::string::npos) << c.why();
}

BACKEND_TEST(Soundness_Codegen, AnAggregateFormattedByFormatIsRefused) {
    // `promoteVararg` refuses an aggregate at the C variadic boundary already, but it
    // would refuse it as "a variadic argument" -- and the honest diagnostic names what
    // the author wrote. A struct has no conversion specifier, and inventing one (a
    // field-by-field walk, an address) would be this file deciding what `{}` means for
    // a user's type, which is a language question and not a lowering one.
    const Built b = build(std::string(kPrintf) +
        "struct Point { x <int>, y <int> }\n"
        "fun main() <noret> {\n"
        "    let p <Point> = Point{x: 1, y: 2};\n"
        "    let s <string> = format!(\"{}\", p);\n"
        "    printf(\"%s\\n\", s);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("a struct formatted by 'format!'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, BuiltinAllocAndFreeExecute) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <&void> = @Alloc(8);\n"
        "    let ip <&int> = cast<&int>(p);\n"
        "    *ip = 42;\n"
        "    printf(\"%d\\n\", *ip);\n"
        "    @Free(p);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, TypeLiteralLowersAsMetaType) {
    const Built b = build(std::string(kPrintf) +
        "fun pick(opt: int) <$interface> {\n"
        "    if (opt == 1) {\n"
        "        return interface { pub a <bool> = true; };\n"
        "    } else {\n"
        "        return interface { pub b <bool> = true; };\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let i <$interface> = pick(1);\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

// ---------------------------------------------------------------------------
// `type_metadata`: the provider the compiler calls once per subject, whose
// returned value it stores and emits (docs/compiler-api.md §3.9, ADR 0014).
//
// A `#[provides(type_metadata)]` declaration makes the backend emit one
// `fin.typemeta.<T>` global per lowered struct: its size, its alignment, and
// its pointer map as (offset, stride, count, tag) entries -- stride-encoded so
// the record is counted in fields, not bytes (§3.9's normative constraint). No
// provider means no globals: every program below without one emits exactly
// what it emitted before this slice, which the full suite holds.
// ---------------------------------------------------------------------------

namespace {

// The one declaration that arms emission: a well-formed provider. Every
// emission test below is this plus structs and uses.
const char* const kTypeMetadataProvider =
    "#[use(compiler)]\n"
    "#[use(compiler.components.layout)]\n"
    "#[provides(type_metadata)]\n"
    "@special(pub) meta(s: $struct) <quote> {\n"
    "  return compiler.layout.pointer_map_quote(s);\n"
    "}\n";

}  // namespace

BACKEND_TEST(Soundness_TypeMetadata, EachStructIsEmittedOnce) {
    // Once per subject, not once per use: three functions read through `&Node`
    // and one metadata global answers for all of them. The count is the
    // memoisation made observable.
    const std::string trace = codegenTrace(std::string(kTypeMetadataProvider) +
        "struct Node { pub v <int>, pub next <&Node>, }\n"
        "fun a(n: &Node) <int> { return n.v; }\n"
        "fun b(n: &Node) <int> { return a(n); }\n"
        "fun c(n: &Node) <int> { return b(n); }\n"
        "fun main() <noret> { }\n");
    EXPECT_EQ(occurrences(trace, "fin.typemeta.Node"), 1u) << trace;
    EXPECT_NE(trace.find("type_metadata Node"), std::string::npos) << trace;
    // `v` is 4 bytes, `next` is pointer-aligned at 8: size 16, align 8, one
    // entry -- offset 8, stride 0, count 1, traced.
    EXPECT_NE(trace.find("size=16"), std::string::npos) << trace;
    EXPECT_NE(trace.find("align=8"), std::string::npos) << trace;
    EXPECT_NE(trace.find("entries=1"), std::string::npos) << trace;
    EXPECT_NE(trace.find("(8,0,1,0)"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_TypeMetadata, APointerFreeStructEmitsAConstantRecord) {
    // The cost rule's observable half: a type with no pointers gets a
    // fixed-size record with no entries, however many fields it has.
    const std::string trace = codegenTrace(std::string(kTypeMetadataProvider) +
        "struct Flat { pub a <int>, pub b <double>, pub c <char>, }\n"
        "fun main() <noret> { }\n");
    EXPECT_EQ(occurrences(trace, "fin.typemeta.Flat"), 1u) << trace;
    EXPECT_NE(trace.find("entries=0"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_TypeMetadata, AnArrayFieldIsStrideEncoded) {
    // Fields, not bytes: two pointers in a `[&int, 2]` field are one entry --
    // offset 8 (after the `int` plus padding), stride 8, count 2 -- however
    // large the extent grows.
    const std::string trace = codegenTrace(std::string(kTypeMetadataProvider) +
        "struct S { pub a <int>, pub ps <[&int, 2]>, }\n"
        "fun main() <noret> { }\n");
    EXPECT_NE(trace.find("(8,8,2,0)"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_TypeMetadata, NoProviderEmitsNothing) {
    // The zero-impact half: without the declaration the backend's account of
    // what it lowered contains no metadata at all.
    const std::string trace = codegenTrace(
        "struct Node { pub v <int>, pub next <&Node>, }\n"
        "fun use(n: &Node) <int> { return n.v; }\n"
        "fun main() <noret> { }\n");
    EXPECT_EQ(trace.find("type_metadata"), std::string::npos) << trace;
    EXPECT_EQ(trace.find("fin.typemeta"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_TypeMetadata, AnUnmappableFieldIsRefusedByName) {
    // The backend invariant: a field the map cannot describe -- a gate inside
    // a gate, an enum variant reaching a nested variant-gated pointer that
    // would need two discriminants traced at once -- refuses naming the struct
    // and the field, never emits a guess. (`any` served here until P-A1 mapped
    // it as an opaque blob per ADR 0034, and a prototype until its two buffer
    // words mapped per ADR 0025; the nested gate is the still-unmapped sibling
    // that keeps this refusal honest.)
    // First that the struct lowers cleanly without the provider, so the
    // refusal below is the metadata's and nothing else's.
    const std::string body =
        "enum Inner { A(&int), B = 2, }\n"
        "enum Outer { X(Inner), Y = 2, }\n"
        "struct GateBox { pub gate <Outer>, }\n"
        "fun main() <noret> { }\n";
    const fs::path obj = uniqueTempPath("fin_meta_ok", ".o");
    const Compiled plain = compileOnly(body, obj);
    ASSERT_EQ(plain.exitCode, 0) << plain.why();
    std::error_code ec;
    fs::remove(obj, ec);

    const fs::path bad = uniqueTempPath("fin_meta_bad", ".o");
    const Compiled refused = compileOnly(std::string(kTypeMetadataProvider) + body, bad);
    EXPECT_NE(refused.exitCode, 0) << refused.why();
    EXPECT_NE(refused.err.find("type_metadata"), std::string::npos) << refused.why();
    EXPECT_NE(refused.err.find("GateBox"), std::string::npos) << refused.why();
    EXPECT_NE(refused.err.find("gate"), std::string::npos) << refused.why();
    fs::remove(bad, ec);
}

BACKEND_TEST(Soundness_TypeMetadata, AProgramWithAProviderStillRuns) {
    // Emission is data beside the program, not a change to it: the same
    // binary runs and prints.
    const Built b = build(std::string(kPrintf) + std::string(kTypeMetadataProvider) +
        "struct Pair { pub a <int>, pub b <int>, }\n"
        "fun get(p: &Pair) <int> { return p.a + p.b; }\n"
        "fun main() <noret> {\n"
        "    let p <Pair> = Pair { a: 20, b: 22 };\n"
        "    printf(\"%d\\n\", get(&p));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}


BACKEND_TEST(Soundness_TypeMetadata, AnInterfaceFieldEmitsOneEntry) {
    // Q12's interface half, end to end: an interface-typed field contributes
    // exactly one traced slot -- the data word at the field's offset -- while
    // the field itself occupies both words (`{data, vtable}`, ADR 0019). The
    // vtable word is absent from the map rather than marked: a collector must
    // not follow it, and absence is the two-state staging of ADR 0019's third
    // state.
    const std::string trace = codegenTrace(std::string(kTypeMetadataProvider) +
        "interface I { pub fun f() <int>; }\n"
        "struct S { pub h <I>, pub x <int>, }\n"
        "fun main() <noret> { }\n");
    EXPECT_NE(trace.find("fin.typemeta.S"), std::string::npos) << trace;
    // `h` is 16 bytes at 0, `x` at 16: size 24, align 8, one entry.
    EXPECT_NE(trace.find("size=24"), std::string::npos) << trace;
    EXPECT_NE(trace.find("align=8"), std::string::npos) << trace;
    EXPECT_NE(trace.find("entries=1"), std::string::npos) << trace;
    EXPECT_NE(trace.find("(0,0,1,0)"), std::string::npos) << trace;
}

BACKEND_TEST(Soundness_TypeMetadata, AUnionFieldIsRefusedByName) {
    // Q12's union half, end to end: the map of a union is a diagnostic naming
    // the union and the field, never a zero map of its first member.
    const std::string body =
        "type Number = int | uint | float;\n"
        "struct S { pub v <Number>, }\n"
        "fun main() <noret> { }\n";
    const fs::path bad = uniqueTempPath("fin_meta_union", ".o");
    const Compiled refused = compileOnly(std::string(kTypeMetadataProvider) + body, bad);
    EXPECT_NE(refused.exitCode, 0) << refused.why();
    EXPECT_NE(refused.err.find("union"), std::string::npos) << refused.why();
    EXPECT_NE(refused.err.find("Number"), std::string::npos) << refused.why();
    EXPECT_NE(refused.err.find("v"), std::string::npos) << refused.why();
    std::error_code ec;
    fs::remove(bad, ec);
}

BACKEND_TEST(Soundness_TypeMetadata, NullableFieldHasAMap) {
    // P-A1: `b? <int>` unwraps to its payload's map (ADR 0040 pairs have
    // computable maps). `int` holds no pointers, so the map is empty but the
    // build succeeds; the run proves the tag still reads.
    const std::string trace = codegenTrace(std::string(kTypeMetadataProvider) +
        "struct A { pub b? <int>, }\n"
        "fun main() <noret> { }\n");
    EXPECT_NE(trace.find("fin.typemeta.A"), std::string::npos) << trace;
    EXPECT_NE(trace.find("entries=0"), std::string::npos) << trace;
    const Built b = build(std::string(kPrintf) + std::string(kTypeMetadataProvider) +
        "struct A { pub b? <int>, }\n"
        "fun main() <noret> {\n"
        "    let a <A> = A{};\n"
        "    printf(\"is_null=%d\\n\", a.b == null);\n"
        "    a.b = 42;\n"
        "    printf(\"val=%d is_null=%d\\n\", a.b?, a.b == null);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "is_null=1\nval=42 is_null=0\n") << b.why();
}

BACKEND_TEST(Soundness_TypeMetadata, AnyFieldHasAMap) {
    // P-A1: `v <any>` maps as an opaque blob per ADR 0034 (value semantics;
    // the payload word is not a traced slot). The map is empty; the run
    // proves boxing through the field still reads back.
    const std::string trace = codegenTrace(std::string(kTypeMetadataProvider) +
        "struct S { pub v <any>, }\n"
        "fun main() <noret> { }\n");
    EXPECT_NE(trace.find("fin.typemeta.S"), std::string::npos) << trace;
    EXPECT_NE(trace.find("entries=0"), std::string::npos) << trace;
    const Built b = build(std::string(kPrintf) + std::string(kTypeMetadataProvider) +
        "struct S { pub v <any>, }\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{};\n"
        "    s.v = 41;\n"
        "    printf(\"%d\\n\", cast<int>(s.v));\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "41\n") << b.why();
}

BACKEND_TEST(Soundness_TypeMetadata, DynArrayFieldHasAMap) {
    // P-A1: `a <[int]>` is ADR 0025's `{ptr, len}` pair; the map traces word
    // 0 (the buffer) as one entry. The run proves the buffer still reads.
    const std::string trace = codegenTrace(std::string(kTypeMetadataProvider) +
        "struct S { pub a <[int]>, }\n"
        "fun main() <noret> { }\n");
    EXPECT_NE(trace.find("fin.typemeta.S"), std::string::npos) << trace;
    EXPECT_NE(trace.find("(0,0,1,0)"), std::string::npos) << trace;
    const Built b = build(std::string(kPrintf) + std::string(kTypeMetadataProvider) +
        "struct C { pub xs <[int]>, }\n"
        "fun main() <noret> {\n"
        "    let c <C> = C{xs: new [int, 3]{}};\n"
        "    c.xs[1] = 6;\n"
        "    printf(\"%d %d\\n\", c.xs.length, c.xs[1]);\n"
        "    delete c.xs;\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3 6\n") << b.why();
}

BACKEND_TEST(Soundness_TypeMetadata, EnumWithPointerMapsPerVariant) {
    // Per-variant precise pointer maps: `A(&int)` traces its pointer word
    // gated on A's discriminant; payload-free `B` traces nothing. The run
    // proves values of both variants still read back -- a wrong map exits 0
    // too, so exit 0 alone is not the assertion.
    const std::string trace = codegenTrace(std::string(kTypeMetadataProvider) +
        "enum E { A(&int), B = 2, }\n"
        "struct S { pub e <E>, }\n"
        "fun main() <noret> { }\n");
    EXPECT_NE(trace.find("fin.typemeta.S"), std::string::npos) << trace;
    EXPECT_NE(trace.find("entries=1"), std::string::npos) << trace;
    // A's discriminant is 0, gated as tag 1 (0 stays unconditional-only so a
    // collector can tell "always" from "when A"): the payload is
    // pointer-aligned, so the word at 8 traces if and only if the value is A.
    EXPECT_NE(trace.find("(8,0,1,1)"), std::string::npos) << trace;
    const Built b = build(std::string(kPrintf) + std::string(kTypeMetadataProvider) +
        "enum E { A(&int), B = 2, }\n"
        "struct S { pub e <E>, }\n"
        "fun main() <noret> {\n"
        "    let x <int> = 41;\n"
        "    let s <S> = S{e: E::A(&x)};\n"
        "    let v <E> = s.e;\n"
        "    let got <&int> = v.0;\n"
        "    printf(\"%d\\n\", *got);\n"
        "    let t <S> = S{e: E::B};\n"
        "    if (t.e == E::B) { printf(\"B\\n\"); }\n"
        "    if (s.e != t.e) { printf(\"DIFF\\n\"); }\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "41\nB\nDIFF\n") << b.why();
}

BACKEND_TEST(Soundness_TypeMetadata, APrototypeFieldHasAMap) {
    // A `m <{string, int}>` field is `{ [string], [int] }`: the keys' dynamic
    // array beside the values' -- two `{ptr, len}` pairs, four words. Each
    // pair's buffer word traces exactly as a dynamic-array field does (ADR
    // 0025); the elements' own pointers are dynamic-length with no static
    // offsets and are not traced.
    const std::string trace = codegenTrace(std::string(kTypeMetadataProvider) +
        "struct S { pub m <{string, int}>, }\n"
        "fun main() <noret> { }\n");
    EXPECT_NE(trace.find("fin.typemeta.S"), std::string::npos) << trace;
    EXPECT_NE(trace.find("entries=2"), std::string::npos) << trace;
    EXPECT_NE(trace.find("(0,0,1,0)"), std::string::npos) << trace;
    EXPECT_NE(trace.find("(16,0,1,0)"), std::string::npos) << trace;
    const Built b = build(std::string(kPrintf) + std::string(kTypeMetadataProvider) +
        "struct Bag { pub data <{int, float}> }\n"
        "fun main() <noret> {\n"
        "    let b <Bag> = Bag { data: { 9: 1.5, 8: 2.5 } };\n"
        "    let c <Bag> = b;\n"
        "    printf(\"%d %d %.1f\\n\", c.data.0[0], c.data.0.length, c.data.1[1]);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "9 2 2.5\n") << b.why();
}

// ---------------------------------------------------------------------------
// Erasure-adjacent runtime shapes.
//
// The executable contract the Fin self-host backend must meet, pinned against
// the C++ backend's observed behavior (proven via /tmp probes before encoding:
// probe1_genptr, probe2_freshpush, probe3c). Each asserts stdout bytes, not
// just exits: a truncation, a wrong stride, or a lost buffer still exits 0.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AGenericCarriesPointerWidthValuesThroughCalls) {
    // A bare `<T>` monomorphises, so a 64-bit value through `T` must arrive
    // whole: a backend that passed `T` at int width would print a truncated
    // pointer here while every int-typed generic test stayed green.
    const Built b = build(std::string(kPrintf) +
        "fun ident<T>(x: T) <T> { return x; }\n"
        "struct Box<T> { val <T> }\n"
        "fun unbox<T>(b: Box<T>) <T> { return b.val; }\n"
        "fun main() <noret> {\n"
        "    let s <string> = ident::<string>(\"hello\");\n"
        "    printf(\"%s\\n\", s);\n"
        "    let t <string> = ident(\"world\");\n"
        "    printf(\"%s\\n\", t);\n"
        "    let b <Box<string>> = Box::<string>{ val: \"boxed\" };\n"
        "    printf(\"%s\\n\", unbox(b));\n"
        "    let x <int> = 41;\n"
        "    let p <&int> = &x;\n"
        "    let q <&int> = ident::<&int>(p);\n"
        "    printf(\"%d\\n\", *q + 1);\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "hello\nworld\nboxed\n42\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFreshGenericBufferGrowsAndFrees) {
    // `new Vec::<int>{}` runs no constructor, so `xs` is null until `ensure`;
    // ten pushes cross two growths (4 to 8 to 16), each a fresh buffer, a copy
    // of what is filled, and a delete of the old one. The walk proves no value
    // was lost or duplicated across any copy, and the open `delete` proves the
    // final buffer is still the one the handle names.
    const Built b = build(std::string(kPrintf) +
        "struct Vec<T> {\n"
        "    n <int> = 0,\n"
        "    xs <[T]>,\n"
        "    fun ensure(self: &Self) <noret> {\n"
        "        if (self.xs == null) { self.xs = new [T, 4]{}; }\n"
        "    }\n"
        "    fun push(self: &Self, v: T) <noret> {\n"
        "        self.ensure();\n"
        "        if (self.n >= self.xs.length) {\n"
        "            let grown <[T]> = new [T, self.xs.length * 2]{};\n"
        "            for (let i <int> = 0; i < self.n; i++) { grown[i] = self.xs[i]; }\n"
        "            delete self.xs;\n"
        "            self.xs = grown;\n"
        "        }\n"
        "        self.xs[self.n] = v;\n"
        "        self.n = self.n + 1;\n"
        "    }\n"
        "    fun get(self: &Self, i: int) <T> { return self.xs[i]; }\n"
        "    fun len(self: &Self) <int> { return self.n; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let v <&Vec<int>> = new Vec::<int>{};\n"
        "    for (let i <int> = 0; i < 10; i++) { v.push(i * i); }\n"
        "    printf(\"%d %d\\n\", v.len(), v.xs.length);\n"
        "    for (let i <int> = 0; i < v.len(); i++) { printf(\"%d \", v.get(i)); }\n"
        "    printf(\"\\n\");\n"
        "    delete v.xs;\n"
        "    printf(\"freed\\n\");\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "10 16\n0 1 4 9 16 25 36 49 64 81 \nfreed\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericBuffersStrideAgreesWithItsElementType) {
    // The `[T]` field of `Wrap<long>` strides 8, not 4: the three values each
    // need two int widths, so an int-sized stride reads halves of neighbours.
    // The struct half is the padding case -- stride is size plus tail padding,
    // and a too-small stride lands the second write inside the first element.
    // Both go through a call (a method, where `T` is the instantiation's own)
    // rather than a free function over `&Wrap<T>`, which the front end does
    // not bind today and is a separate question from what the buffer does.
    const Built b = build(std::string(kPrintf) +
        "struct Wrap<T> {\n"
        "    xs <[T]>,\n"
        "    fun fill(self: &Self, a: T, b: T, c: T) <noret> {\n"
        "        self.xs[0] = a;\n"
        "        self.xs[1] = b;\n"
        "        self.xs[2] = c;\n"
        "    }\n"
        "}\n"
        "struct P { x <int>, y <int>, }\n"
        "fun main() <noret> {\n"
        "    let w <Wrap<long>> = Wrap::<long>{ xs: new [long, 3]{} };\n"
        "    w.fill(4294967296, 8589934592, 12884901888);\n"
        "    printf(\"%lld %lld %lld %d\\n\", w.xs[0], w.xs[1], w.xs[2], w.xs.length);\n"
        "    delete w.xs;\n"
        "    let q <Wrap<P>> = Wrap::<P>{ xs: new [P, 2]{} };\n"
        "    q.xs[0] = P{ x: 1, y: 2 };\n"
        "    q.xs[1] = P{ x: 3, y: 4 };\n"
        "    printf(\"%d %d %d %d\\n\", q.xs[0].x, q.xs[0].y, q.xs[1].x, q.xs[1].y);\n"
        "    delete q.xs;\n"
        "}\n");
    ASSERT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4294967296 8589934592 12884901888 3\n1 2 3 4\n") << b.why();
}

// ---------------------------------------------------------------------------
// C++ width behaviors for shifts: the executable contract.
//
// Oracle values were taken from /tmp/sh/battery and re-proven on build/finc
// before pinning; each program below regroups them so the tests share no
// structure with the probes. A sibling stage fix for short shifts may change
// how these lower, but not these bytes.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, IntShiftsAreArithmetic) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", 1 << 3);\n"
        "    printf(\"%d\\n\", 16 >> 2);\n"
        "    printf(\"%d\\n\", -8 >> 2);\n"
        "    printf(\"%d\\n\", -8 << 2);\n"
        "    let lo <int> = -2147483648;\n"
        "    printf(\"%d\\n\", lo >> cast<int>(1));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8\n4\n-2\n-32\n-1073741824\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, UintShiftsAreLogical) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <uint> = cast<uint>(1) << cast<uint>(3);\n"
        "    printf(\"%u\\n\", a);\n"
        "    let c <uint> = cast<uint>(16) >> cast<uint>(2);\n"
        "    printf(\"%u\\n\", c);\n"
        "    let h <uint> = cast<uint>(2147483648);\n"
        "    printf(\"%u\\n\", h >> cast<uint>(1));\n"
        "    let w <uint> = cast<uint>(0) - cast<uint>(8);\n"
        "    printf(\"%u\\n\", w >> cast<uint>(2));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8\n4\n1073741824\n1073741822\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, SignedVsUnsignedRightShift) {
    // The same magnitude through both signs: arithmetic keeps the sign bit,
    // logical shifts in a zero. One program so the contrast is the assertion.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let s <int> = -8 >> cast<int>(2);\n"
        "    printf(\"%d\\n\", s);\n"
        "    let u <uint> = (cast<uint>(0) - cast<uint>(8)) >> cast<uint>(2);\n"
        "    printf(\"%u\\n\", u);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "-2\n1073741822\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, CharShiftsAreArithmetic) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let y <char> = cast<char>(4);\n"
        "    printf(\"%d\\n\", y >> cast<char>(1));\n"
        "    printf(\"%d\\n\", y << cast<char>(1));\n"
        "    let n <char> = cast<char>(0) - cast<char>(8);\n"
        "    printf(\"%d\\n\", n >> cast<char>(2));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "2\n8\n-2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ShortShiftsAreArithmetic) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let y <short> = cast<short>(16);\n"
        "    printf(\"%d\\n\", y << cast<short>(2));\n"
        "    printf(\"%d\\n\", y >> cast<short>(2));\n"
        "    let n <short> = cast<short>(0) - cast<short>(8);\n"
        "    printf(\"%d\\n\", n >> cast<short>(2));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "64\n4\n-2\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, UshortShiftsAreLogical) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <ushort> = cast<ushort>(1) << cast<ushort>(15);\n"
        "    printf(\"%d\\n\", a);\n"
        "    let h <ushort> = cast<ushort>(32768);\n"
        "    printf(\"%d\\n\", h >> cast<ushort>(1));\n"
        "    let m <ushort> = cast<ushort>(0) - cast<ushort>(1);\n"
        "    printf(\"%d\\n\", m >> cast<ushort>(1));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "32768\n16384\n32767\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, LongShiftsAreArithmetic) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <long> = cast<long>(1) << cast<long>(40);\n"
        "    printf(\"%lld\\n\", x);\n"
        "    let n <long> = cast<long>(1) << cast<long>(62);\n"
        "    let neg <long> = cast<long>(0) - n - n;\n"
        "    printf(\"%lld\\n\", neg >> cast<long>(1));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1099511627776\n-4611686018427387904\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, UlongShiftsAreLogical) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let c <ulong> = cast<ulong>(1) << cast<ulong>(40);\n"
        "    printf(\"%llu\\n\", c);\n"
        "    let hi <ulong> = cast<ulong>(1) << cast<ulong>(63);\n"
        "    printf(\"%llu\\n\", hi);\n"
        "    printf(\"%llu\\n\", hi >> cast<ulong>(1));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1099511627776\n9223372036854775808\n4611686018427387904\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AWideningShiftTakesTheWiderWidth) {
    // An int literal shifted by a ulong count is a ulong shift (ADR 0022's
    // wider-integer rule, which shifts share with arithmetic).
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <ulong> = 1 << cast<ulong>(2);\n"
        "    printf(\"%llu\\n\", a);\n"
        "    let c <ulong> = cast<ulong>(1) << 40;\n"
        "    printf(\"%llu\\n\", c);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "4\n1099511627776\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, BitsStdWidthOps) {
    const Built b = build(
        "import { popcount, get_bit, set_bit, clear_bit, toggle_bit, rotate_left, rotate_right, bit_length } from bits::std;\n"
        + std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", popcount(cast<uint>(0)));\n"
        "    printf(\"%d\\n\", popcount(cast<uint>(255)));\n"
        "    printf(\"%d\\n\", popcount(cast<uint>(4294967295)));\n"
        "    printf(\"%d\\n\", get_bit(cast<uint>(5), 0));\n"
        "    printf(\"%d\\n\", get_bit(cast<uint>(5), 1));\n"
        "    printf(\"%u\\n\", set_bit(cast<uint>(8), 0));\n"
        "    printf(\"%u\\n\", clear_bit(cast<uint>(9), 0));\n"
        "    printf(\"%u\\n\", toggle_bit(cast<uint>(8), 0));\n"
        "    printf(\"%u\\n\", rotate_left(cast<uint>(1), cast<uint>(1)));\n"
        "    printf(\"%u\\n\", rotate_right(cast<uint>(2), cast<uint>(1)));\n"
        "    printf(\"%d\\n\", bit_length(cast<uint>(255)));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0\n8\n32\n1\n0\n9\n8\n9\n2\n1\n8\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, HashStdVectors) {
    const Built b = build(
        "import { crc32, fnv1a_32, fnv1a_64, djb2, sdbm } from hash::std;\n"
        + std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%u\\n\", crc32(\"\"));\n"
        "    printf(\"%u\\n\", crc32(\"hello\"));\n"
        "    printf(\"%u\\n\", crc32(\"abc\"));\n"
        "    printf(\"%u\\n\", fnv1a_32(\"\"));\n"
        "    printf(\"%u\\n\", fnv1a_32(\"hello\"));\n"
        "    printf(\"%u\\n\", fnv1a_32(\"abc\"));\n"
        "    printf(\"%llu\\n\", fnv1a_64(\"hello\"));\n"
        "    printf(\"%llu\\n\", djb2(\"hello\"));\n"
        "    printf(\"%llu\\n\", sdbm(\"hello\"));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "0\n907060870\n891568578\n2166136261\n1335831723\n440920331\n"
                      "11831194018420276491\n210714636441\n7416051667693574450\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, HashStdCheckVectors) {
    const Built b = build(
        "import { crc32, fnv1a_32, fnv1a_64, djb2, sdbm } from hash::std;\n"
        + std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%u\\n\", crc32(\"123456789\"));\n"
        "    printf(\"%u\\n\", crc32(\"a\"));\n"
        "    printf(\"%u\\n\", crc32(\"The quick brown fox jumps over the lazy dog\"));\n"
        "    printf(\"%u\\n\", fnv1a_32(\"a\"));\n"
        "    printf(\"%llu\\n\", fnv1a_64(\"\"));\n"
        "    printf(\"%llu\\n\", fnv1a_64(\"a\"));\n"
        "    printf(\"%llu\\n\", djb2(\"\"));\n"
        "    printf(\"%llu\\n\", djb2(\"a\"));\n"
        "    printf(\"%llu\\n\", sdbm(\"\"));\n"
        "    printf(\"%llu\\n\", sdbm(\"a\"));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "3421780262\n3904355907\n1095738169\n3826002220\n"
                      "14695981039346656037\n12638187200555641996\n"
                      "5381\n177670\n0\n97\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AShiftThroughAGenericFieldLowers) {
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 100 };\n"
        "    printf(\"%d\\n\", b.val >> 2);\n"
        "    printf(\"%d\\n\", 1 << 2);\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "25\n4\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, MixedIntUintShiftsAreRefused) {
    // Equal widths with different signs are not a widening (ADR 0022), so a
    // uint value shifted by an int count -- and the mirror -- is a diagnostic.
    const Built toUint = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <uint> = cast<uint>(16) >> 2;\n"
        "    printf(\"%u\\n\", a);\n"
        "}\n");
    EXPECT_NE(toUint.compileExit, 0) << toUint.why();
    EXPECT_NE(toUint.compileErr.find("Type mismatch"), std::string::npos) << toUint.why();
    EXPECT_NE(toUint.compileErr.find("'uint'"), std::string::npos) << toUint.why();

    const Built toInt = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let b <int> = 1 << cast<uint>(2);\n"
        "    printf(\"%d\\n\", b);\n"
        "}\n");
    EXPECT_NE(toInt.compileExit, 0) << toInt.why();
    EXPECT_NE(toInt.compileErr.find("Type mismatch"), std::string::npos) << toInt.why();
    EXPECT_NE(toInt.compileErr.find("'int'"), std::string::npos) << toInt.why();
}

BACKEND_TEST(Soundness_Codegen, ABoolShiftIsRefused) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", true << 1);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("Type mismatch"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFloatShiftIsRefusedAtCodegen) {
    // The front end accepts the shape and the backend refuses it by name.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", 1.0 << 1);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
    EXPECT_NE(b.compileErr.find("non-integer"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnOversizedShiftCountIsRefusedPerWidth) {
    // Owner decision: a constant count at or past the value's width is a
    // diagnostic naming the count and the width, never LLVM-poison UB.
    const fs::path o8 = uniqueTempPath("fin_shift_8", ".o");
    const Compiled c8 = compileOnly(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", cast<char>(1) << 8);\n"
        "}\n", o8);
    EXPECT_NE(c8.exitCode, 0) << c8.why();
    EXPECT_NE(c8.err.find("Shift count 8"), std::string::npos) << c8.why();
    EXPECT_NE(c8.err.find("8-bit"), std::string::npos) << c8.why();
    std::error_code ec;
    fs::remove(o8, ec);

    const fs::path o16 = uniqueTempPath("fin_shift_16", ".o");
    const Compiled c16 = compileOnly(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", cast<short>(1) << 16);\n"
        "}\n", o16);
    EXPECT_NE(c16.exitCode, 0) << c16.why();
    EXPECT_NE(c16.err.find("Shift count 16"), std::string::npos) << c16.why();
    EXPECT_NE(c16.err.find("16-bit"), std::string::npos) << c16.why();
    fs::remove(o16, ec);

    const fs::path o32 = uniqueTempPath("fin_shift_32", ".o");
    const Compiled c32 = compileOnly(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", 1 << 32);\n"
        "}\n", o32);
    EXPECT_NE(c32.exitCode, 0) << c32.why();
    EXPECT_NE(c32.err.find("Shift count 32"), std::string::npos) << c32.why();
    EXPECT_NE(c32.err.find("32-bit"), std::string::npos) << c32.why();
    fs::remove(o32, ec);

    const fs::path o64 = uniqueTempPath("fin_shift_64", ".o");
    const Compiled c64 = compileOnly(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%llu\\n\", cast<ulong>(1) << 64);\n"
        "}\n", o64);
    EXPECT_NE(c64.exitCode, 0) << c64.why();
    EXPECT_NE(c64.err.find("Shift count 64"), std::string::npos) << c64.why();
    EXPECT_NE(c64.err.find("64-bit"), std::string::npos) << c64.why();
    fs::remove(o64, ec);

    const fs::path oc64 = uniqueTempPath("fin_shift_c64", ".o");
    const Compiled cc64 = compileOnly(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%llu\\n\", cast<ulong>(1) << cast<ulong>(64));\n"
        "}\n", oc64);
    EXPECT_NE(cc64.exitCode, 0) << cc64.why();
    EXPECT_NE(cc64.err.find("Shift count 64"), std::string::npos) << cc64.why();
    EXPECT_NE(cc64.err.find("64-bit"), std::string::npos) << cc64.why();
    fs::remove(oc64, ec);
}

BACKEND_TEST(Soundness_Codegen, ANegativeShiftCountIsRefused) {
    const fs::path obj = uniqueTempPath("fin_shift_neg2", ".o");
    const Compiled c = compileOnly(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", 1 << -1);\n"
        "}\n", obj);
    EXPECT_NE(c.exitCode, 0) << c.why();
    EXPECT_NE(c.err.find("Shift count -1"), std::string::npos) << c.why();
    EXPECT_NE(c.err.find("32-bit"), std::string::npos) << c.why();
    std::error_code ec;
    fs::remove(obj, ec);
}

BACKEND_TEST(Soundness_Codegen, BoundaryShiftCountsAreAccepted) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", 1 << 0);\n"
        "    printf(\"%d\\n\", 1 << 31);\n"
        "    printf(\"%llu\\n\", cast<ulong>(1) << cast<ulong>(63));\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1\n-2147483648\n9223372036854775808\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AVariableShiftCountStillCompiles) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let n <int> = 3;\n"
        "    printf(\"%d\\n\", 1 << n);\n"
        "    let m <ulong> = cast<ulong>(2);\n"
        "    printf(\"%llu\\n\", cast<ulong>(1) << m);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "8\n4\n") << b.why();
}

// ---------------------------------------------------------------------------
// Shadowed bare calls: FRONTEND WINS (owner decision I1).
//
// A bare call inside a struct's method that names one of the struct's own
// methods binds the method -- the frontend checks it against the method's
// signature -- so the backend must lower it as that method (receiver in,
// free function out). Three staged shapes: an instance method shadowed by a
// free function of a different shape (check-0/build-1), an identical shape
// (silent wrong-callee at run time), and a static method shadowed by a free
// function (check-0/build-1 on arity).
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, AShadowedBareCallLowersToTheMethod) {
    // `foo(self, v - 1)` inside `S::foo` is the method recursing, not the free
    // `foo(a, b)`. Used to refuse at build with `this conversion (from 'a
    // struct' to 'an integer') is not lowered yet`: the frontend had checked
    // `(self, v - 1)` against the method while the backend lowered by name to
    // the free `(int, int)`.
    const Built b = build(std::string(kPrintf) +
        "fun foo(a: int, b: int) <int> { printf(\"CALLEE=free\\n\"); return 200 + a + b; }\n"
        "struct S {\n"
        "    pub x <int>,\n"
        "    pub fun foo(self: &Self, v: int) <int> {\n"
        "        printf(\"CALLEE=method v=%d\\n\", v);\n"
        "        if (v <= 0) { return 100; }\n"
        "        let f <int> = foo(self, v - 1);\n"
        "        return f + 1;\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{x: 0};\n"
        "    let m <int> = s.foo(2);\n"
        "    printf(\"outer=%d\\n\", m);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "CALLEE=method v=2\nCALLEE=method v=1\nCALLEE=method v=0\nouter=102\n")
        << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnIdenticalShapeBareCallBindsTheMethod) {
    // The shapes coincide (`foo(s: &S, v: int)` free vs `foo(self: &Self, v:
    // int)` method), so the old backend lowered the bare call to the free
    // function and the program ran the wrong callee with no diagnostic at any
    // stage. Terminating variant of the probe: the method recurses only while
    // v is positive, so running the method body ends. A free run would print
    // `CALLEE=free` and answer 201; the method answers 101.
    const Built b = build(std::string(kPrintf) +
        "struct S {\n"
        "    pub x <int>,\n"
        "    pub fun foo(self: &Self, v: int) <int> {\n"
        "        printf(\"CALLEE=method\\n\");\n"
        "        if (v <= 0) { return 100; }\n"
        "        let f <int> = foo(self, v - 1);\n"
        "        return v + f;\n"
        "    }\n"
        "}\n"
        "fun foo(s: &S, v: int) <int> { printf(\"CALLEE=free\\n\"); return 200 + v; }\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{x: 0};\n"
        "    let m <int> = s.foo(1);\n"
        "    printf(\"outer=%d\\n\", m);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "CALLEE=method\nCALLEE=method\nouter=101\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AShadowedBareStaticCallLowersToTheStaticMethod) {
    // `make(1)` inside `S::probe` is the static `S::make`, not the free
    // `make(a, b)`. Used to refuse at build with `a call to 'make' with too
    // few arguments is not lowered yet`: checked against the 1-parameter
    // static, lowered by name to the 2-parameter free function.
    const Built b = build(std::string(kPrintf) +
        "fun make(a: int, b: int) <int> { return 200 + a + b; }\n"
        "struct S {\n"
        "    pub x <int>,\n"
        "    pub static fun make(v: int) <int> { return 100 + v; }\n"
        "    pub fun probe(self: &Self, v: int) <int> {\n"
        "        let f <int> = make(1);\n"
        "        return v + f;\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{x: 0};\n"
        "    let m <int> = s.probe(0);\n"
        "    printf(\"outer=%d\\n\", m);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "outer=101\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AFreeCallBesideASameNamedMethodStillCallsTheFreeFunction) {
    // The other half of the shadowing rule: outside the struct's own methods a
    // bare call is the free function, and a dot call is the method. Neither
    // spelling may leak into the other.
    const Built b = build(std::string(kPrintf) +
        "fun foo(a: int, b: int) <int> { printf(\"CALLEE=free\\n\"); return 200 + a + b; }\n"
        "struct S {\n"
        "    pub x <int>,\n"
        "    pub fun foo(self: &Self, v: int) <int> { printf(\"CALLEE=method\\n\"); return 100 + v; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{x: 0};\n"
        "    let m <int> = s.foo(1);\n"
        "    printf(\"dot=%d\\n\", m);\n"
        "    let f <int> = foo(1, 2);\n"
        "    printf(\"free=%d\\n\", f);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "CALLEE=method\ndot=101\nCALLEE=free\nfree=203\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ABareCallBeforeTheMethodDeclarationBindsTheFreeFunction) {
    // The shadowing is declaration-ordered today: a method body only sees the
    // methods registered above it, so `other` (written first) calling `foo(1,
    // 2)` binds the free `foo`. This locks the order rule: whatever changes it
    // must change the backend in the same commit, never stage them apart.
    const Built b = build(std::string(kPrintf) +
        "fun foo(a: int, b: int) <int> { printf(\"CALLEE=free\\n\"); return 200 + a + b; }\n"
        "struct S {\n"
        "    pub x <int>,\n"
        "    pub fun other(self: &Self, v: int) <int> {\n"
        "        let f <int> = foo(1, 2);\n"
        "        return v + f;\n"
        "    }\n"
        "    pub fun foo(self: &Self, v: int) <int> { printf(\"CALLEE=method\\n\"); return 100 + v; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{x: 0};\n"
        "    let m <int> = s.other(0);\n"
        "    printf(\"outer=%d\\n\", m);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "CALLEE=free\nouter=203\n") << b.why();
}

// ---------------------------------------------------------------------------
// Generic inference, as the C++ front end does it today.
//
// The sibling working in finc/* owns Fin-side checking; this section pins the
// C++ implementation's contract only: what infers (with the runtime value that
// proves the instantiation), and where inference stops (with the exact refusal
// a caller gets). Each accept below runs the program because a generic that
// checks but miscompiles is the failure only a run catches.
//
// Letter count is pinned as indifference: every shape below runs both a
// single-letter parameter (T) and a multi-letter one (Key/A/B) and asserts the
// same verdict. If naming ever starts meaning something, these are the tests
// that say so.
//
// A check-only run: what `finc <file>` says without `-o`/`-c`, for the pins
// where checking accepts what the backend refuses.
FincRun checkOnly(const std::string& code) {
    fs::path src = uniqueTempPath("fin_gi_check", ".fin");
    {
        std::ofstream f(src, std::ios::binary);
        f.write(code.data(), (std::streamsize)code.size());
    }
    const FincRun r = runFinc({src.string()});
    std::error_code ec;
    fs::remove(src, ec);
    return r;
}

BACKEND_TEST(Soundness_GenericInference, AParameterIsInferredFromItsArgument) {
    // Both spellings in one program so a difference cannot hide between tests.
    const Built b = build(std::string(kPrintf) +
        "fun id<T>(x: T) <T> { return x; }\n"
        "fun id2<Key>(x: Key) <Key> { return x; }\n"
        "fun main() <noret> { printf(\"%d %d\\n\", id(41) + 1, id2(41) + 1); }\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42 42\n") << b.why();
}

BACKEND_TEST(Soundness_GenericInference, TwoFreeFunctionParametersAreInferredIndependently) {
    const Built b = build(std::string(kPrintf) +
        "fun pair<T, U>(a: T, b: U) <T> { return a; }\n"
        "fun pair2<A, B>(a: A, b: B) <A> { return a; }\n"
        "fun main() <noret> { printf(\"%d %d\\n\", pair(40, \"s\") + 2, pair2(40, \"s\") + 2); }\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42 42\n") << b.why();
}

BACKEND_TEST(Soundness_GenericInference, EitherParameterCanDecideTheReturnType) {
    // The return type follows whichever parameter it names, not the first one.
    const Built b = build(std::string(kPrintf) +
        "fun snd<T, U>(a: T, b: U) <U> { return b; }\n"
        "fun main() <noret> { printf(\"%s\\n\", snd(1, \"hi\")); }\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "hi\n") << b.why();
}

BACKEND_TEST(Soundness_GenericInference, InferenceReachesThroughAReference) {
    // `&T` still mentions T, so a reference argument binds the parameter.
    const Built b = build(std::string(kPrintf) +
        "fun get<T>(p: &T) <T> { return p; }\n"
        "fun main() <noret> {\n"
        "    let v <int> = 41;\n"
        "    printf(\"%d\\n\", get(&v) + 1);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_GenericInference, NestedCallsInferInsideOut) {
    const Built b = build(std::string(kPrintf) +
        "fun id<T>(x: T) <T> { return x; }\n"
        "fun id2<Key>(x: Key) <Key> { return x; }\n"
        "fun main() <noret> { printf(\"%d %d\\n\", id(id(41)) + 1, id2(id2(41)) + 1); }\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42 42\n") << b.why();
}

BACKEND_TEST(Soundness_GenericInference, AnElementTypeIsInferredFromAFixedArray) {
    const Built b = build(std::string(kPrintf) +
        "fun first<T>(a: [T, 3]) <T> { return a[0]; }\n"
        "fun first2<Key>(a: [Key, 3]) <Key> { return a[0]; }\n"
        "fun main() <noret> {\n"
        "    let a <[int, 3]> = [10, 20, 30];\n"
        "    printf(\"%d %d\\n\", first(a), first2(a));\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "10 10\n") << b.why();
}

BACKEND_TEST(Soundness_GenericInference, AGenericMethodInfersFromItsArgument) {
    const Built b = build(std::string(kPrintf) +
        "struct S {\n"
        "    v <int>,\n"
        "    fun m<T>(self: &Self, x: T) <T> { return x; }\n"
        "    fun m2<Key>(self: &Self, x: Key) <Key> { return x; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{v: 1};\n"
        "    printf(\"%d %d\\n\", s.m(41) + 1, s.m2(41) + 1);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42 42\n") << b.why();
}

BACKEND_TEST(Soundness_GenericInference, OneGenericMethodInstantiatedAtTwoTypes) {
    // Each call site gets its own instantiation: the method is not locked to
    // the first type it was seen with.
    const Built b = build(std::string(kPrintf) +
        "struct S {\n"
        "    v <int>,\n"
        "    fun m<T>(self: &Self, x: T) <T> { return x; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{v: 1};\n"
        "    printf(\"%d %s\\n\", s.m(1), s.m(\"s\"));\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 s\n") << b.why();
}

BACKEND_TEST(Soundness_GenericInference, AMethodOnAGenericInstanceSeesTheInstanceArgument) {
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    v <T>,\n"
        "    fun get(self: &Self) <T> { return self.v; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box{v: 41};\n"
        "    printf(\"%d\\n\", b.get() + 1);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_GenericInference, AGenericConstructorInfersItsArgumentFromItsFields) {
    // No annotation on the constructor: the field value decides Box<int>.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    v <T>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <auto> = Box{v: 41};\n"
        "    printf(\"%d\\n\", b.v + 1);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_GenericInference, AReturnedGenericStructCarriesTheInferredArgument) {
    // Inference flows out through the return type into the constructor.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    v <T>,\n"
        "}\n"
        "fun wrap<T>(x: T) <Box<T>> { return Box{v: x}; }\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = wrap(41);\n"
        "    printf(\"%d\\n\", b.v + 1);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_GenericInference, AMatchingTurbofishIsAccepted) {
    const Built b = build(std::string(kPrintf) +
        "fun id<T>(x: T) <T> { return x; }\n"
        "fun pair<T, U>(a: T, b: U) <T> { return a; }\n"
        "fun main() <noret> {\n"
        "    let x <int> = id::<int>(5);\n"
        "    let y <int> = pair::<int, string>(40, \"s\");\n"
        "    printf(\"%d %d\\n\", x + 37, y + 2);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42 42\n") << b.why();
}

BACKEND_TEST(Soundness_GenericInference, ATwoParameterGenericStructConstructsAndReads) {
    const Built b = build(std::string(kPrintf) +
        "struct P<T, U> {\n"
        "    a <T>,\n"
        "    b <U>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <P<int, string>> = P{a: 1, b: \"s\"};\n"
        "    printf(\"%d %s\\n\", p.a, p.b);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "1 s\n") << b.why();
}

BACKEND_TEST(Soundness_GenericInference, ConflictingConstraintsAreRefused) {
    // One parameter bound to two types: the first argument wins and the second
    // is diagnosed against it.
    for (const char* decl : {"fun same<T>(a: T, b: T) <T> { return a; }",
                             "fun same<A>(a: A, b: A) <A> { return a; }"}) {
        const Built b = build(std::string(decl) +
            "\nfun main() <noret> { let x <int> = same(1, \"s\"); }\n");
        EXPECT_NE(b.compileExit, 0) << decl << "\n" << b.why();
        EXPECT_NE(b.compileErr.find("Type mismatch: expected 'int', got 'string'"),
                  std::string::npos) << decl << "\n" << b.why();
    }
}

BACKEND_TEST(Soundness_GenericInference, AReturnTypeMismatchIsReportedAgainstTheInferredType) {
    // T is int from the argument, so the annotation is what does not fit.
    const Built b = build(
        "fun id<T>(x: T) <T> { return x; }\n"
        "fun main() <noret> { let x <string> = id(1); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("Type mismatch: expected 'string', got 'int'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_GenericInference, AGenericStructFieldMismatchNamesTheFieldTypes) {
    const Built b = build(
        "struct Box<T> {\n"
        "    v <T>,\n"
        "}\n"
        "fun main() <noret> { let b <Box<int>> = Box{v: \"s\"}; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("Type mismatch: expected 'int', got 'string'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_GenericInference, ANestedElementMismatchNamesTheWholeArray) {
    // The parameter unified with [string, 3] while the context wanted the
    // return to be int, so the diagnostic names both array types.
    const Built b = build(
        "fun first<T>(a: [T, 3]) <T> { return a[0]; }\n"
        "fun main() <noret> {\n"
        "    let a <[string, 3]> = [\"a\", \"b\", \"c\"];\n"
        "    let x <int> = first(a);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("Type mismatch: expected '[int, 3]', got '[string, 3]'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_GenericInference, AnUninferrableReturnOnlyParameterIsCheckedButNotLowered) {
    // Nothing mentions T, so checking accepts -- there is nothing to check
    // against -- and the backend refuses rather than guessing a type.
    for (const char* decl : {"fun make<T>() <T> { return 1; }\n"
                             "fun main() <noret> { let x <int> = make(); }\n",
                             "fun make2<Key>() <Key> { return 1; }\n"
                             "fun main() <noret> { let x <int> = make2(); }\n"}) {
        EXPECT_EQ(checkOnly(decl).exitCode, 0) << decl;
    }
    const Built b = build(
        "fun make<T>() <T> { return 1; }\n"
        "fun main() <noret> { let x <int> = make(); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find(
        "codegen: a call to 'make' whose type argument 'T' no argument mentions is not lowered yet"),
        std::string::npos) << b.why();
    const Built b2 = build(
        "fun make2<Key>() <Key> { return 1; }\n"
        "fun main() <noret> { let x <int> = make2(); }\n");
    EXPECT_NE(b2.compileExit, 0) << b2.why();
    EXPECT_NE(b2.compileErr.find(
        "codegen: a call to 'make2' whose type argument 'Key' no argument mentions is not lowered yet"),
        std::string::npos) << b2.why();
}

BACKEND_TEST(Soundness_GenericInference, APhantomParameterIsCheckedButNotLowered) {
    // T appears in the declaration but no argument mentions it: same split as
    // the return-only case -- check accepts, backend refuses by name.
    const std::string code =
        "fun f<T>(x: int) <int> { return x; }\n"
        "fun main() <noret> { let q <int> = f(1); }\n";
    EXPECT_EQ(checkOnly(code).exitCode, 0);
    const Built b = build(code);
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find(
        "codegen: a call to 'f' whose type argument 'T' no argument mentions is not lowered yet"),
        std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_GenericInference, AStructHandleSeedsTheReturnAndLowers) {
    // `make_default(struct { ... })` (tests/samples/literal_struct.fin): the
    // `$struct` handle seeds the bare `<T>` return with the literal's own
    // anonymous struct, so the backend monomorphizes instead of refusing for
    // an unmentioned type argument. The run proves the value, not just the
    // build: the default-built field reads back what the literal declared.
    const std::string code =
        "@define printf(fmt: string, ...) <noret>;\n"
        "fun make_default<T>(st: $struct) <T> {\n"
        "    let instance <auto> = st{};\n"
        "    return cast<T>(instance);\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <auto> = make_default(struct {\n"
        "        pub x <int> = 42,\n"
        "    });\n"
        "    printf(\"%d\\n\", p.x);\n"
        "    blame p.x == 42;\n"
        "}\n";
    EXPECT_EQ(checkOnly(code).exitCode, 0);
    const Built b = build(code);
    EXPECT_EQ(b.compileExit, 0) << b.why();
    EXPECT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_GenericInference, ATwoHandleCallStillRefuses) {
    // Seeding needs one unambiguous handle: two `$struct` literals name two
    // structs, so neither seeds the return and the call keeps the old refusal
    // rather than guessing one. The return is `<int>` (not a bare generic),
    // so even one handle would not seed here.
    const std::string code =
        "fun f<T>(a: $struct, b: $struct) <int> { return 1; }\n"
        "fun main() <noret> {\n"
        "    let q <int> = f(struct {\n"
        "        pub x <int> = 1,\n"
        "    }, struct {\n"
        "        pub y <int> = 2,\n"
        "    });\n"
        "}\n";
    EXPECT_EQ(checkOnly(code).exitCode, 0);
    const Built b = build(code);
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find(
        "codegen: a call to 'f' whose type argument 'T' no argument mentions is not lowered yet"),
        std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_GenericInference, AHandleConstructionWithoutAGenericReturnStillRefuses) {
    // `st{}` with no generic return to resolve it to: the handle's struct is
    // known only at the call, and a non-generic body is shared across calls,
    // so there is no one struct to build. Refused naming the handle.
    const std::string code =
        "fun make_it(st: $struct) <int> {\n"
        "    let instance <auto> = st{};\n"
        "    return 1;\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let q <int> = make_it(struct {\n"
        "        pub x <int> = 42,\n"
        "    });\n"
        "}\n";
    EXPECT_EQ(checkOnly(code).exitCode, 0);
    const Built b = build(code);
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen: a literal of struct 'st' is not lowered yet"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_GenericInference, APartialTurbofishIsCheckedButNotLowered) {
    // One of two arguments given explicitly: checking accepts and the backend
    // refuses the arity rather than mixing explicit with inferred.
    const std::string code =
        "fun pair<T, U>(a: T, b: U) <T> { return a; }\n"
        "fun main() <noret> { let x <int> = pair::<int>(1, \"s\"); }\n";
    EXPECT_EQ(checkOnly(code).exitCode, 0);
    const Built b = build(code);
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find(
        "codegen: a call to 'pair' with 1 type argument(s) where it declares 2 is not lowered yet"),
        std::string::npos) << b.why();
}

// ---------------------------------------------------------------------------
// Plain `let` with an explicit annotation, and field reads on generic
// instances: what an annotation accepts, what it refuses, and what a read
// computes at run time.
//
// Integer widening/narrowing between named integer types is already pinned
// (Soundness_IntegerWidening, test_soundness.cpp); what it does not pin is
// the exact wording of the cross-kind mismatches, any run-time value for a
// widening that passes, or a generic field read in either direction. Those
// three are what a Fin mirror of this behaviour needs, so they live here:
// the accepts run, because a widening that checks but miscompiles is the
// failure only a run catches.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_PlainLet, AnIntInitIsRefusedForAStringAnnotation) {
    const Built b = build("fun main() <int> { let x <string> = 1; return 0; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("Type mismatch: expected 'string', got 'int'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_PlainLet, AStringInitIsRefusedForAnIntAnnotation) {
    const Built b = build("fun main() <int> { let x <int> = \"s\"; return 0; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("Type mismatch: expected 'int', got 'string'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_PlainLet, AFloatInitIsRefusedForAnIntAnnotation) {
    // The variable form, so the pin is about the type rule and not the
    // constant-fits rule for integer literals (Soundness_IntegerConstants):
    // a float value does not narrow to int even when the constant would fit.
    const Built b = build(
        "fun main() <int> { let a <float> = 1.5; let x <int> = a; return 0; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("Type mismatch: expected 'int', got 'float'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_PlainLet, AWideningInitComputesAtRunTime) {
    // The three widenings the analyser admits on variables: int -> long,
    // short -> int (Soundness_IntegerWidening) and int -> float (the one
    // float rule in PrimitiveType::isAssignableTo). Each prints, because a
    // widening that checked but truncated or garbled at run time would still
    // exit 0 on the check alone.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <int> = 42;\n"
        "    let x <long> = a;\n"
        "    let s <short> = 42;\n"
        "    let y <int> = s;\n"
        "    let f <float> = a;\n"
        "    printf(\"%d %d %.1f\\n\", x, y, f);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "42 42 42.0\n") << b.why();
}

BACKEND_TEST(Soundness_GenericFieldRead, AFieldReadCarriesTheInstantiatedIntType) {
    const Built b = build(std::string(kPrintf) +
        "struct B<T> {\n"
        "    v <T>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <B<int>> = B::<int>{ v: 42 };\n"
        "    let x <int> = b.v;\n"
        "    printf(\"%d\\n\", x);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "42\n") << b.why();
}

BACKEND_TEST(Soundness_GenericFieldRead, AFieldReadAgainstAStringAnnotationNamesBothTypes) {
    const Built b = build(
        "struct B<T> {\n"
        "    v <T>,\n"
        "}\n"
        "fun main() <int> {\n"
        "    let b <B<int>> = B::<int>{ v: 42 };\n"
        "    let x <string> = b.v;\n"
        "    return 0;\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("Type mismatch: expected 'string', got 'int'"),
              std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_GenericFieldRead, AStringFieldReadsBack) {
    // Both directions of the string instantiation: the accept runs, the
    // mismatch names both types. The accept is what stops a later change
    // from "fixing" the mismatch by refusing every string field.
    const Built ok = build(std::string(kPrintf) +
        "struct B<T> {\n"
        "    v <T>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <B<string>> = B::<string>{ v: \"hi\" };\n"
        "    let x <string> = b.v;\n"
        "    printf(\"%s\\n\", x);\n"
        "}\n");
    EXPECT_EQ(ok.compileExit, 0) << ok.why();
    ASSERT_TRUE(ok.ran) << ok.why();
    EXPECT_EQ(ok.out, "hi\n") << ok.why();

    const Built bad = build(
        "struct B<T> {\n"
        "    v <T>,\n"
        "}\n"
        "fun main() <int> {\n"
        "    let b <B<string>> = B::<string>{ v: \"hi\" };\n"
        "    let x <int> = b.v;\n"
        "    return 0;\n"
        "}\n");
    EXPECT_NE(bad.compileExit, 0) << bad.why();
    EXPECT_NE(bad.compileErr.find("Type mismatch: expected 'int', got 'string'"),
              std::string::npos) << bad.why();
}

BACKEND_TEST(Soundness_GenericFieldRead, ANestedFieldReadCarriesTheInnerType) {
    const Built ok = build(std::string(kPrintf) +
        "struct B<T> {\n"
        "    v <T>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <B<B<int>>> = B::<B<int>>{ v: B::<int>{ v: 7 } };\n"
        "    let x <int> = b.v.v;\n"
        "    printf(\"%d\\n\", x);\n"
        "}\n");
    EXPECT_EQ(ok.compileExit, 0) << ok.why();
    ASSERT_TRUE(ok.ran) << ok.why();
    EXPECT_EQ(ok.out, "7\n") << ok.why();

    const Built bad = build(
        "struct B<T> {\n"
        "    v <T>,\n"
        "}\n"
        "fun main() <int> {\n"
        "    let b <B<B<int>>> = B::<B<int>>{ v: B::<int>{ v: 7 } };\n"
        "    let x <string> = b.v.v;\n"
        "    return 0;\n"
        "}\n");
    EXPECT_NE(bad.compileExit, 0) << bad.why();
    EXPECT_NE(bad.compileErr.find("Type mismatch: expected 'string', got 'int'"),
              std::string::npos) << bad.why();
}

BACKEND_TEST(Soundness_GenericFieldRead, AWideningReachesThroughAFieldRead) {
    // A field read is not a second type system: B<int>.v is an int, so it
    // widens to float exactly as a plain int variable does -- and B<long>.v
    // is a long, so it narrows to int exactly as badly.
    const Built ok = build(std::string(kPrintf) +
        "struct B<T> {\n"
        "    v <T>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <B<int>> = B::<int>{ v: 42 };\n"
        "    let f <float> = b.v;\n"
        "    printf(\"%.1f\\n\", f);\n"
        "}\n");
    EXPECT_EQ(ok.compileExit, 0) << ok.why();
    ASSERT_TRUE(ok.ran) << ok.why();
    EXPECT_EQ(ok.out, "42.0\n") << ok.why();

    const Built bad = build(
        "struct B<T> {\n"
        "    v <T>,\n"
        "}\n"
        "fun main() <int> {\n"
        "    let b <B<long>> = B::<long>{ v: 42 };\n"
        "    let x <int> = b.v;\n"
        "    return 0;\n"
        "}\n");
    EXPECT_NE(bad.compileExit, 0) << bad.why();
    EXPECT_NE(bad.compileErr.find("Type mismatch: expected 'int', got 'long'"),
              std::string::npos) << bad.why();
}

BACKEND_TEST(Soundness_Codegen, AReturnOfAFnTypedCallTypesAsItsReturnType) {
    // `f(x)` where `f: fn(int) -> int` is an `int`, not the `fn` spelling.
    // The accept runs; the mismatch names the return type on both sides.
    const Built ok = build(std::string(kPrintf) +
        "fun apply(f: fn(int) -> int, x: int) <int> { return f(x); }\n"
        "fun inc(a: int) <int> { return a + 1; }\n"
        "fun main() <noret> { printf(\"%d\\n\", apply(inc, 41)); }\n");
    EXPECT_EQ(ok.compileExit, 0) << ok.why();
    ASSERT_TRUE(ok.ran) << ok.why();
    EXPECT_EQ(ok.runExit, 0) << ok.why();
    EXPECT_EQ(ok.out, "42\n") << ok.why();

    const Built bad = build(
        "fun apply(f: fn(int) -> int, x: int) <int> { return f(x); }\n"
        "fun inc(a: int) <int> { return a + 1; }\n"
        "fun bad() <string> { return apply(inc, 1); }\n"
        "fun main() <int> { return 0; }\n");
    EXPECT_NE(bad.compileExit, 0) << bad.why();
    EXPECT_NE(bad.compileErr.find("Type mismatch: expected 'string', got 'int'"),
              std::string::npos) << bad.why();
    EXPECT_EQ(bad.compileErr.find("got 'fn"), std::string::npos) << bad.why();
    EXPECT_EQ(bad.compileErr.find("expected 'fn"), std::string::npos) << bad.why();
}

BACKEND_TEST(Soundness_GenericInference, APairArgumentBindsBothParametersThroughInference) {
    // `Pair<A, B>` against `Pair<int, char>` binds A=int, B=char from the
    // argument. Both directions are read back so a half-bound callee fails.
    const Built b = build(std::string(kPrintf) +
        "struct Pair<A, B> {\n"
        "    first <A>,\n"
        "    second <B>\n"
        "}\n"
        "fun first<A, B>(p: Pair<A, B>) <A> { return p.first; }\n"
        "fun second<A, B>(p: Pair<A, B>) <B> { return p.second; }\n"
        "fun main() <noret> {\n"
        "    let p <Pair<int, char>> = Pair::<int, char>{ first: 300, second: 'z' };\n"
        "    printf(\"%d %d\\n\", first(p), cast<int>(second(p)));\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "300 122\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ABoxWithADifferentArgumentIsRefused) {
    // Same base, different argument: `Box<string>` is not `Box<int>`.
    // The accept runs so a later change cannot "fix" the refusal by
    // refusing every generic argument.
    const Built ok = build(std::string(kPrintf) +
        "struct Box<T> { val <T> }\n"
        "fun take(b: Box<int>) <int> { return b.val; }\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 7 };\n"
        "    printf(\"%d\\n\", take(b));\n"
        "}\n");
    EXPECT_EQ(ok.compileExit, 0) << ok.why();
    ASSERT_TRUE(ok.ran) << ok.why();
    EXPECT_EQ(ok.out, "7\n") << ok.why();

    const Built bad = build(
        "struct Box<T> { val <T> }\n"
        "fun take(b: Box<int>) <int> { return b.val; }\n"
        "fun main() <int> {\n"
        "    let b <Box<string>> = Box::<string>{ val: \"hi\" };\n"
        "    return take(b);\n"
        "}\n");
    EXPECT_NE(bad.compileExit, 0) << bad.why();
    EXPECT_NE(bad.compileErr.find("Type mismatch: expected 'Box<int>', got 'Box<string>'"),
              std::string::npos) << bad.why();
}

BACKEND_TEST(Soundness_Codegen, ACollectionWithADifferentElementIsRefused) {
    // The struct-element shape of the same rule: `Collection<Data>` is not
    // `Collection<int>`. The accept runs for the same reason as above.
    const Built ok = build(std::string(kPrintf) +
        "struct Data { x <int> }\n"
        "struct Collection<T> { v <T> }\n"
        "fun take(c: Collection<int>) <int> { return c.v; }\n"
        "fun main() <noret> {\n"
        "    let c <Collection<int>> = Collection::<int>{ v: 11 };\n"
        "    printf(\"%d\\n\", take(c));\n"
        "}\n");
    EXPECT_EQ(ok.compileExit, 0) << ok.why();
    ASSERT_TRUE(ok.ran) << ok.why();
    EXPECT_EQ(ok.out, "11\n") << ok.why();

    const Built bad = build(
        "struct Data { x <int> }\n"
        "struct Collection<T> { v <T> }\n"
        "fun take(c: Collection<int>) <int> { return 0; }\n"
        "fun main() <int> {\n"
        "    let c <Collection<Data>> = Collection::<Data>{ v: Data{ x: 1 } };\n"
        "    return take(c);\n"
        "}\n");
    EXPECT_NE(bad.compileExit, 0) << bad.why();
    EXPECT_NE(bad.compileErr.find(
        "Type mismatch: expected 'Collection<int>', got 'Collection<Data>'"),
              std::string::npos) << bad.why();
}

// ---------------------------------------------------------------------------
// Reference-access shapes at run time.
//
// Sibling owns finc/* codegen concurrently; these pins assert only what the
// compiled program does today (exact stdout bytes + exit codes), verified
// first via `build/finc -o` on this machine. If any goes red, the backend
// changed -- not the test.
// ---------------------------------------------------------------------------

BACKEND_TEST(Soundness_Codegen, ARestrictCopyReadsOwnedFlagAndBranches) {
    // tests/samples/const.fin:28-34 in miniature: a `.restrict` copy is read
    // through (`*a.restrict`), the owned flag is read off the copy, and the
    // branch is taken per copy. `restrict` is assigned after construction
    // (`a.restrict = &a`); assigning `&self` inside the constructor reads
    // back garbage (measured: 32765/22020 for 5/7), so that shape is not
    // pinned here.
    const Built b = build(std::string(kPrintf) +
        "struct Handle {\n"
        "    owned <bool>,\n"
        "    val <int>,\n"
        "    restrict <&Handle>\n"
        "}\n"
        "fun check(const h: Handle) <noret> {\n"
        "    if (h.owned == false) {\n"
        "        printf(\"readonly %d\\n\", h.val);\n"
        "    } else {\n"
        "        printf(\"writable %d\\n\", h.val);\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <Handle> = Handle { owned: false, val: 5, restrict: null };\n"
        "    let b <Handle> = Handle { owned: true, val: 7, restrict: null };\n"
        "    a.restrict = &a;\n"
        "    b.restrict = &b;\n"
        "    check(*a.restrict);\n"
        "    check(*b.restrict);\n"
        "    printf(\"kept %d %d\\n\", a.val, b.val);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "readonly 5\nwritable 7\nkept 5 7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ACtorTimeSelfAddressReadsBackThroughRestrict) {
    // The constructor form of the shape above: `self.restrict = &self` inside
    // the constructor must capture the object being constructed, so reading
    // back through `*a.restrict` sees what the constructor wrote. Lowering
    // `&self` to the slot holding the receiver pointer instead stores a
    // callee-frame address that dies at `ret` (measured: readonly 32764 /
    // writable 22050 for 5 / 7, exit 0).
    const Built b = build(std::string(kPrintf) +
        "struct Handle {\n"
        "    owned <bool>,\n"
        "    val <int>,\n"
        "    restrict <&Handle>\n"
        "    Handle(v: int, o: bool) {\n"
        "        self.val = v;\n"
        "        self.owned = o;\n"
        "        self.restrict = &self;\n"
        "    }\n"
        "}\n"
        "fun check(h: Handle) <noret> {\n"
        "    if (h.owned == false) {\n"
        "        printf(\"readonly %d\\n\", h.val);\n"
        "    } else {\n"
        "        printf(\"writable %d\\n\", h.val);\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <Handle> = Handle(5, false);\n"
        "    let b <Handle> = Handle(7, true);\n"
        "    check(*a.restrict);\n"
        "    check(*b.restrict);\n"
        "    printf(\"kept %d %d\\n\", a.val, b.val);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "readonly 5\nwritable 7\nkept 5 7\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AReadAndWriteThroughASliceParameterReachesTheCaller) {
    // tests/samples/arrays.fin:11 (`sort(array: &[T])`) in miniature: the
    // callee reads two elements through the `&[int]` parameter and writes one
    // back, and the caller observes the write. The index expressions are
    // distinct slots, so a lowering that aliased them would print 10 30 30.
    const Built b = build(std::string(kPrintf) +
        "fun touch(a: &[int]) <noret> {\n"
        "    a[1] = a[0] + a[2];\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let v <[int]> = [10, 20, 30];\n"
        "    touch(&v);\n"
        "    printf(\"%d %d %d\\n\", v[0], v[1], v[2]);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "10 40 30\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AStaticMethodOnASelfPointerGuardsItsStores) {
    // tests/samples/letssee.fin:37-43 (`normalize(ptr: &Self)`) in miniature:
    // the method call goes through the `&Self` receiver (`(*ptr).total()`),
    // and both stores are guarded by the same condition -- the zero vector
    // takes neither. Integer 1/2 stand in for the unit vector so the pin does
    // not depend on float lowering.
    const Built b = build(std::string(kPrintf) +
        "struct V {\n"
        "    x <int>,\n"
        "    y <int>,\n"
        "    fun total(self: &Self) <int> { return self.x + self.y; }\n"
        "    static fun normalize(ptr: &Self) <noret> {\n"
        "        let t <int> = (*ptr).total();\n"
        "        if (t > 0) {\n"
        "            ptr.x = 1;\n"
        "            ptr.y = 2;\n"
        "        }\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let v <V> = V { x: 6, y: 3 };\n"
        "    V::normalize(&v);\n"
        "    printf(\"%d %d\\n\", v.x, v.y);\n"
        "    let z <V> = V { x: 0, y: 0 };\n"
        "    V::normalize(&z);\n"
        "    printf(\"%d %d\\n\", z.x, z.y);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out, "1 2\n0 0\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADeleteOfAStackAddressAborts) {
    // tests/samples/simple_pointers.fin:10 (`delete &temp`) documents the
    // abort: freeing a stack address is not a success path. Pinned as an abort
    // (non-zero exit, `unreached` never printed), with glibc's text, the same
    // way ADoubleDeleteStillAbortsWithoutClaimant pins "double free".
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let x <int> = 5;\n"
        "    delete &x;\n"
        "    printf(\"unreached\\n\");\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.why();
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_NE(b.runExit, 0) << b.why();
    EXPECT_EQ(b.out.find("unreached"), std::string::npos) << b.why();
    EXPECT_NE(b.out.find("invalid pointer"), std::string::npos) << b.why();
}
