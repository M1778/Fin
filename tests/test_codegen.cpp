#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Corpus.hpp"
#include "types/Layout.hpp"
#include "types/PrimitiveType.hpp"
#include "types/StructType.hpp"

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

std::string shellQuoteLocal(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

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
    {
        std::ofstream f(src, std::ios::binary);
        f.write(code.data(), (std::streamsize)code.size());
    }

    const FincRun c = runFinc({src.string(), "-o", exe.string()});
    b.compileExit = c.exitCode;
    b.compileErr = stripAnsi(c.err);

    if (b.compileExit == 0 && fs::exists(exe)) {
        fs::path outPath = uniqueTempPath("fin_cg_out");
        std::string cmd = shellQuoteLocal(exe.string()) + " > " +
                          shellQuoteLocal(outPath.string()) + " 2>&1";
        int status = std::system(cmd.c_str());
#ifdef WIFEXITED
        b.runExit = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#else
        b.runExit = status;
#endif
        b.ran = true;
        b.out = readWholeFile(outPath.string());
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
    // and suggesting FIN_CC. Five corpus samples reach it (macros.fin, macros2.fin,
    // macro_definitions.fin, stdlib/somelib.fin, stdlib/networking.fin), and every
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

    const char* fromEnv = std::getenv("FIN_CC");
    const std::string cc = (fromEnv && *fromEnv) ? fromEnv : "cc";
    const fs::path outPath = uniqueTempPath("fin_linked_out");
    const std::string link = shellQuoteLocal(cc) + " " + shellQuoteLocal(libObj.string()) +
                             " " + shellQuoteLocal(mainObj.string()) + " -o " +
                             shellQuoteLocal(exe.string());
    ASSERT_EQ(std::system(link.c_str()), 0) << link;

    const std::string run = shellQuoteLocal(exe.string()) + " > " +
                            shellQuoteLocal(outPath.string()) + " 2>&1";
    std::system(run.c_str());
    EXPECT_EQ(readWholeFile(outPath.string()), "42\n");

    std::error_code ec;
    for (const fs::path& p : {libObj, mainObj, exe, outPath}) fs::remove(p, ec);
}

BACKEND_TEST(Soundness_Codegen, ACompileOnlyBuildStillRefusesWhatItCannotLower) {
    // `-c` is not a way around the refusals, and a failed compile leaves no object
    // -- a stale one is a link that succeeds against yesterday's code.
    const fs::path obj = uniqueTempPath("fin_obj_bad", ".o");
    const Compiled c = compileOnly(
        "interface Drawable { fun draw(self: &Self) <noret>; }\n"
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
    const std::string cmd = shellQuoteLocal(exe.string()) + " > " +
                            shellQuoteLocal(outPath.string()) + " 2>&1";
    EXPECT_EQ(std::system(cmd.c_str()), 0);
    EXPECT_EQ(readWholeFile(outPath.string()), "30\n");

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
    // An interface declaration is well-typed and cannot be lowered: an interface
    // reference is two words and the pointer map has three states (ADR 0019), and
    // building the witness table that makes a dynamic call work is a unit of its own.
    // The compile must fail and say so; the one outcome that must never happen is
    // exit 0 with a binary whose behaviour does not match the program.
    //
    // The construct in a refusal test is a moving part, and this one has now moved
    // twice. It was a plain struct until structs lowered, then a generic struct until
    // monomorphisation landed, and each time keeping it would have turned a passing
    // refusal test into a passing test of nothing. Which is the argument for picking
    // the construct that is furthest from being lowered rather than the one that
    // reads best.
    const Built b = build(
        "interface Drawable { fun draw(self: &Self) <noret>; }\n"
        "fun main() <noret> { let i <int> = 1; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ARefusalNamesTheLine) {
    // A refusal with no location is a refusal the reader has to go looking for,
    // and in a file of any size that is most of the cost of the error. The
    // construct here is below line 1 so that a location the backend simply left
    // default would not pass by accident.
    //
    // It used to be `i++`, and then a generic struct, which is exactly the trap
    // AnUnloweredConstructIsRefused warns about one screen above: each of those
    // lowered in turn, and each time this test went from asserting a located refusal
    // to asserting nothing -- failing rather than passing vacuously only because it
    // checks the exit code too. The construct is an interface for the same reason
    // that one uses it: a witness table is a unit of its own (ADR 0019), so it is the
    // furthest thing in this file from being lowered.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let i <int> = 1;\n"
        "}\n"
        "\n"
        "interface Drawable { fun draw(self: &Self) <noret>; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find(".fin:5:"), std::string::npos) << b.why();
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
        "    printf(\"%ld %ld %ld\\n\", a[0], a[1], a[2]);\n"
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
    // `[T, 0]` is a legal type of zero bytes, following the empty-struct precedent
    // -- and unlike an empty struct it is not refused, because it has an element
    // type and therefore a stride. Nothing may index it, which the front end
    // enforces (Soundness_ArrayBounds.AZeroLengthArrayHasNoElementZero).
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let a <[int, 0]> = [];\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, ADynamicArrayIsRefused) {
    // Not a fixed array with a number missing. How a `[T]` is represented is an
    // undecided ruling, and it decides what `array.length` compiles to inside a
    // callee that was handed one. Refused rather than guessed at.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let a <[int]> = [1, 2, 3];\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
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

BACKEND_TEST(KnownDefect_Codegen, AnEnumMemberDoesNotHoist) {
    // The same defect KnownDefect_Codegen.AStructTypeDoesNotHoist books, at the other
    // kind of declaration: the analyzer defines an enum's members when it reaches the
    // enum, so a member named above it is an undefined *variable* rather than an
    // undefined type. Both halves are the same missing pass.
    //
    // The backend's half is done -- declareEnums numbers every enum in the module
    // before any body is emitted -- which is why this asserts a front-end refusal.
    // When declaration hoisting lands, this fails, becomes
    // Soundness_Codegen.AnEnumDeclaredBelowItsUseLowers, and asserts the program
    // prints 5, which it already would.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", Late);\n"
        "}\n"
        "enum E { Early = 4, Late }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("Undefined variable 'Late'"), std::string::npos) << b.why();
    // And specifically not a codegen refusal, so that a front end which starts
    // accepting it fails this rather than passing on the backend's answer.
    EXPECT_EQ(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AMemberWithAPayloadIsRefused) {
    // The tagged union, which is a layout ruling and not a detail to pick here. The
    // refusal is eager -- it fails the build whether or not anything uses the enum --
    // for the reason declareStructs gives: a declaration that is quietly skipped is a
    // type name that later resolves to nothing.
    const Built b = build(
        "enum R { Ok <int>, Err }\n"
        "fun main() <noret> { }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("payload"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AGenericEnumIsRefused) {
    // `Result <T: Any<...>, U: ErrorLike>` (stdlib/typing.fin:14). Erasure (ADR 0002)
    // decides what a generic enum's members carry, and its members are what would be
    // numbered here.
    const Built b = build(
        "enum E<T> { A, B }\n"
        "fun main() <noret> { }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("generic"), std::string::npos) << b.why();
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
        "    printf(\"%.2f %d %s %ld\\n\", F, B, S, L);\n"
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
    const Built b = build(std::string(kPrintf) +
        "#[slaveof($Fin)]\n"
        "const G <int> = 1;\n"
        "fun main() <noret> { printf(\"%d\\n\", G); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(KnownDefect_Codegen, AGlobalDoesNotHoist) {
    // A function above the global cannot see it: "Undefined variable 'G'" from the
    // analyzer, not a refusal from here. The backend's half is done -- every global
    // is declared before any body is emitted -- so this is the same missing
    // declaration-hoisting pass that AStructTypeDoesNotHoist and
    // AnEnumMemberDoesNotHoist book at the other two kinds of declaration. When it
    // exists, this becomes Soundness_Codegen.AGlobalDeclaredBelowItsUseLowers.
    const Built b = build(std::string(kPrintf) +
        "fun get() <int> { return G; }\n"
        "let G <int> = 7;\n"
        "fun main() <noret> { printf(\"%d\\n\", get()); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("Undefined variable 'G'"), std::string::npos) << b.why();
    EXPECT_EQ(b.compileErr.find("codegen"), std::string::npos) << b.why();
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
    // corpus writes `%d` and `%ld`, `bool` a byte in storage though an i1 in a
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

BACKEND_TEST(Soundness_Codegen, SizeofATypeWithNoRepresentationIsRefused) {
    // A dynamic `[T]`: how one is represented is undecided, so its size is the same
    // undecided thing rather than a pointer's width guessed here.
    const Built b = build("fun main() <noret> { let n <int> = sizeof([int]); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("sizeof"), std::string::npos) << b.why();
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
        "    printf(\"%d %d %d %ld %.2f %.2f %s %d\\n\",\n"
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
        "    printf(\"%d %ld %d\\n\", p.a, p.big, p.z);\n"
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

BACKEND_TEST(KnownDefect_Codegen, AStructTypeDoesNotHoist) {
    // Functions hoist -- ACallAboveItsDeclarationLinks, and 6f48a89 for the front
    // end's half. A struct *type* does not: the analyzer resolves a type name
    // against what it has already seen, so a variable of a struct declared lower in
    // the file is "Undefined type".
    //
    // The backend's half is done. declareStructs registers every struct in the
    // module before any function is emitted, in two passes precisely so that
    // declaration order does not decide, which is why this asserts a *front-end*
    // refusal and not a codegen one. When type hoisting lands, this test fails,
    // becomes Soundness_Codegen.AStructDeclaredBelowItsUseLowers, and asserts the
    // program prints 42 -- which it already would.
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 41, b: 1 };\n"
        "    printf(\"%d\\n\", p.a + p.b);\n"
        "}\n"
        "struct P { a <int>, b <int> }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("Undefined type 'P'"), std::string::npos) << b.why();
    // And specifically not a codegen refusal: if the front end starts accepting it,
    // this must fail rather than quietly keep passing on a backend refusal.
    EXPECT_EQ(b.compileErr.find("codegen"), std::string::npos) << b.why();
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

BACKEND_TEST(Soundness_Codegen, ADefaultOfNullForANonNullableFieldIsRefused) {
    // tests/samples/deeptest4.fin:6 writes `integer <int> = null` and the front end
    // takes it. There is no null int -- 0 is a value the program did not write and
    // the analyzer would not have accepted it as one -- so the backend refuses
    // instead of picking the bit pattern that looks most like nothing.
    const Built b = build(std::string(kPrintf) +
        "struct P { x <int> = null }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { };\n"
        "    printf(\"%d\\n\", p.x);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
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

BACKEND_TEST(Soundness_Codegen, AnEmptyStructIsRefused) {
    // LLVM makes it size 0 and C makes it size 1. Picking one here would be
    // inventing a rule the language has not made, and nothing in the corpus writes
    // an empty struct.
    const Built b = build(
        "struct S { }\n"
        "fun main() <noret> { let s <S>; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AClassIsRefused) {
    // Whether a `class` is a value like a struct or a reference is not settled, and
    // the two lower differently at every single assignment. The analyzer accepts the
    // declaration, so the refusal has to be here.
    const Built b = build(
        "class C { a <int> }\n"
        "fun main() <noret> { let c <C> = C { a: 1 }; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnOperatorOnAStructIsRefused) {
    // Whether `a == b` on two structs compares field-wise is a ruling nobody has
    // made. It matters that this refuses rather than crashes: commonType compares
    // bit widths, a struct has none, and handing the aggregate to CreateICmpEQ is an
    // assertion inside LLVM -- which reads as a compiler crash rather than as the
    // unlowered operator it is.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int> }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 1 };\n"
        "    let q <P> = P { a: 1 };\n"
        "    if (p == q) { printf(\"same\\n\"); }\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
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
BACKEND_TEST(Soundness_Codegen, AGenericClassIsRefusedAtItsDeclarationNotItsUse) {
    const Built b = build(std::string(kPrintf) +
        "class Box<T> { v <T> }\n"
        "fun main() <noret> { printf(\"%d\\n\", 1); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
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

    const std::string triple = llvm::sys::getDefaultTargetTriple();
    std::string lookupError;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, lookupError);
    ASSERT_NE(target, nullptr) << lookupError;
    llvm::TargetOptions options;
    std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
        triple, "generic", "", options, llvm::Reloc::PIC_));
    ASSERT_NE(machine, nullptr) << "no TargetMachine for " << triple;
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
    const std::string triple = llvm::sys::getDefaultTargetTriple();
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

BACKEND_TEST(Soundness_Codegen, TheAddressOfAStringLiteralIsRefused) {
    // tests/samples/variables.fin:11 (`let Complex <&string> = &"Hello world";`).
    // A `string` is already a pointer to bytes here, so `&"..."` is either that
    // same pointer -- making `*Complex` a char and `&string` the same
    // representation as `string` -- or the address of an anonymous cell holding
    // it, making `*Complex` the string. Nothing in the corpus reads `Complex`, so
    // both readings run, and picking one would be inventing the answer.
    const Built b = build(std::string(kPrintf) +
        "let Complex <&string> = &\"Hello world\";\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
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
        "    printf(\"%d %ld %d\\n\", cast<int>(small.val), big.val,\n"
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
        "    printf(\"%d %ld\\n\", cast<int>(p.val), q.val);\n"
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
    // one is also empty: an instantiation of it would refuse (see the next test),
    // and the declaration on its own still may not.
    const Built b = build(std::string(kPrintf) +
        "struct M <T> {}\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    ASSERT_TRUE(b.ran) << b.why();
    EXPECT_EQ(b.out, "ok\n") << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnEmptyGenericStructRefusesWhereItIsInstantiated) {
    // The consequence of deferring: `struct M<T> {}` is fine until someone asks
    // for `M<int>`, and then the empty-struct question (LLVM says size 0, C says
    // 1, the corpus says nothing) has to be answered and is not.
    const Built b = build(std::string(kPrintf) +
        "struct M <T> {}\n"
        "fun main() <noret> {\n"
        "    let m <M<int>> = M::<int>{};\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnInstantiationAtATypeThisFileCannotLowerIsRefused) {
    // `Box<[int]>` is a fine template at a type argument with no representation
    // yet: a dynamic `[T]` is the undecided one. The refusal has to name the
    // argument rather than the template, because the template is not the problem
    // and `Box<int>` right beside it still works.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<[int]>> = Box::<[int]>{ val: [1, 2] };\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
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
    // stdlib/error.fin:3 writes `#[uncastable]`, and what it excludes is a cast --
    // a rule about the type, not about its name. Honouring `llvm_name` must not turn
    // the attribute check into "attributes are decoration".
    const Built b = build(std::string(kPrintf) +
        "#[uncastable]\n"
        "struct S { v <int> }\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{ v: 1 };\n"
        "    printf(\"%d\\n\", s.v);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("uncastable"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnLlvmNameBesideAnUnreadAttributeIsStillRefused) {
    // stdlib/error.fin:2-5 is exactly this: `#[llvm_name="Error"] #[uncastable]
    // #[stderror] #[class]`. Reading one of the four is not permission to drop the
    // other three.
    const Built b = build(std::string(kPrintf) +
        "#[llvm_name=\"Error\"]\n"
        "#[uncastable]\n"
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

BACKEND_TEST(Soundness_Codegen, AStructsConstructorIsRefusedAtItsDeclaration) {
    // The literal path already refuses `new S(1)`, and that refusal is about the
    // *call*. This is about the body: `constructor` is a function, and an object
    // without it is a function the source declared and the object does not have.
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    constructor(nx: int) { self.x = nx; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("constructor"), std::string::npos) << b.why();
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

    const char* fromEnv = std::getenv("FIN_CC");
    const std::string cc = (fromEnv && *fromEnv) ? fromEnv : "cc";
    const fs::path outPath = uniqueTempPath("fin_linked_rn_out");
    const std::string link = shellQuoteLocal(cc) + " " + shellQuoteLocal(libObj.string()) +
                             " " + shellQuoteLocal(mainObj.string()) + " -o " +
                             shellQuoteLocal(exe.string());
    ASSERT_EQ(std::system(link.c_str()), 0) << link;

    const std::string run = shellQuoteLocal(exe.string()) + " > " +
                            shellQuoteLocal(outPath.string()) + " 2>&1";
    std::system(run.c_str());
    EXPECT_EQ(readWholeFile(outPath.string()), "42\n");

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
    // `#[export]` (stdlib/stdio.fin:23) says something about linkage this file does
    // not implement, and `#[overwrite(printf)]` (stdlib/stdio.fin:35) says which of
    // two definitions wins. Both were being dropped; neither is decoration.
    const Built b = build(std::string(kPrintf) +
        "#[export]\n"
        "fun twice(n: int) <int> { return n * 2; }\n"
        "fun main() <noret> { printf(\"%d\\n\", twice(4)); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("export"), std::string::npos) << b.why();
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
// A struct member's were unread too. readonly.fin:19 writes `#[debug]` on a field, and
// a field attribute is one edit away from being a field *offset* attribute.

BACKEND_TEST(Soundness_Codegen, AnAttributeOnALocalIsRefused) {
    const Built b = build(std::string(kPrintf) +
        "fun main() <noret> {\n"
        "    let z <int> = 1;\n"
        "    #[slaveof(z)]\n"
        "    let m <int> = 2;\n"
        "    printf(\"%d\\n\", z + m);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("slaveof"), std::string::npos) << b.why();
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

BACKEND_TEST(Soundness_Codegen, AnAttributeOnAStructMemberIsRefused) {
    const Built b = build(std::string(kPrintf) +
        "struct S {\n"
        "    #[debug]\n"
        "    v <int>\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let s <S> = S{ v: 5 };\n"
        "    printf(\"%d\\n\", s.v);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("debug"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnAttributeOnAGenericStructsMemberIsRefusedAtTheTemplate) {
    // At the declaration and not once per instantiation, for the reason the method
    // refusal gives: it will not become lowerable at `Box<int>`.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    #[debug]\n"
        "    val <T>\n"
        "}\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("debug"), std::string::npos) << b.why();
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
//   -- the other branch, which ADR 0002 says is a raw pointer. Still refused, by
//   name, because a monomorphised body for it would be a different program that
//   happens to agree on these arguments.
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

    const char* fromEnv = std::getenv("FIN_CC");
    const std::string cc = (fromEnv && *fromEnv) ? fromEnv : "cc";
    const fs::path outPath = uniqueTempPath("fin_linked_gi_out");
    const std::string link = shellQuoteLocal(cc) + " " + shellQuoteLocal(libObj.string()) +
                             " " + shellQuoteLocal(mainObj.string()) + " -o " +
                             shellQuoteLocal(exe.string());
    ASSERT_EQ(std::system(link.c_str()), 0) << link;

    const std::string run = shellQuoteLocal(exe.string()) + " > " +
                            shellQuoteLocal(outPath.string()) + " 2>&1";
    std::system(run.c_str());
    EXPECT_EQ(readWholeFile(outPath.string()), "42\n");

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

BACKEND_TEST(Soundness_Codegen, AnErasureMarkedGenericFunctionIsRefused) {
    // generics_interfaces.fin:8. ADR 0002: an erasure-marker constraint on any one
    // parameter selects erasure, and an erased generic is a raw pointer. That is a
    // different representation, not a different spelling, so monomorphising it would
    // compile and be a different program -- one that happens to agree here and
    // disagree wherever the erased pointer is what the program is about.
    const Built b = build(std::string(kPrintf) +
        "fun erased<T: Castable>(a: T) <int> { return cast<int>(a); }\n"
        "fun main() <noret> { printf(\"%d\\n\", erased(5)); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("Castable"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, AnErasureMarkedGenericStructIsRefused) {
    // nullifier.fin:10's `struct maybe<T: Castable>`, which was being monomorphised
    // silently -- the same rule as the function's, and the same reason. Refused at
    // the declaration, because a marker is a property of the template rather than of
    // one instantiation.
    const Built b = build(std::string(kPrintf) +
        "struct maybe<T: Castable> {\n"
        "    val <T>\n"
        "}\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("Castable"), std::string::npos) << b.why();
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

BACKEND_TEST(Soundness_Codegen, AMethodOnAValueWithNoHomeIsRefused) {
    // A struct returned by a call is a value with no address, and the receiver is a
    // pointer. Materialising a temporary to point at it would compile and then throw
    // away anything the method assigned -- so it is refused, which is the same answer
    // `make()[0]` gets for the same reason.
    //
    // OWNER RULING NEEDED: whether `Point::make(1).get()` should copy to a temporary
    // for a method that only reads. Answering it needs a notion of a method that
    // does not mutate, and Fin has no such marker yet.
    const Built b = build(std::string(kPrintf) +
        "struct Point {\n"
        "    x <int>,\n"
        "    pub static fun make(v: int) <Point> { return Point{ x: v }; }\n"
        "    fun get(self: &Self) <int> { return self.x; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    printf(\"%d\\n\", Point::make(1).get());\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("receiver"), std::string::npos) << b.why();
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

BACKEND_TEST(Soundness_Codegen, AGenericMethodIsRefusedAtItsCall) {
    // `fun set_x<U>(new_x: U)` -- struct_methods.fin:14. Two substitutions at once,
    // the struct's and the call's, which is a unit of its own. Refused at the call
    // rather than at the declaration, because a template is not code and the
    // declaration in the sample is never called.
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> {\n"
        "    val <T>,\n"
        "    fun set_x<U>(new_x: U) <noret> { self.val = new_x; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let b <Box<int>> = Box::<int>{ val: 1 };\n"
        "    b.set_x(5);\n"
        "    printf(\"ok\\n\");\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("set_x"), std::string::npos) << b.why();
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

    const char* fromEnv = std::getenv("FIN_CC");
    const std::string cc = (fromEnv && *fromEnv) ? fromEnv : "cc";
    const std::string link = shellQuoteLocal(cc) + " " + shellQuoteLocal(libObj.string()) +
                             " " + shellQuoteLocal(mainObj.string()) + " -o " +
                             shellQuoteLocal(exe.string());
    ASSERT_EQ(std::system(link.c_str()), 0) << link;

    const std::string run = shellQuoteLocal(exe.string()) + " > " +
                            shellQuoteLocal(outPath.string()) + " 2>&1";
    std::system(run.c_str());
    EXPECT_EQ(readWholeFile(outPath.string()), "42\n");

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

BACKEND_TEST(Soundness_Codegen, AGenericOperatorIsRefusedWhereItIsWritten) {
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
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("generic"), std::string::npos) << b.why();
    EXPECT_NE(b.compileErr.find("+"), std::string::npos) << b.why();
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

BACKEND_TEST(Soundness_Codegen, AnOperatorWithAWrittenSelfIsRefused) {
    // visit(OperatorDeclaration&) in the analyzer defines `self` unconditionally and
    // keeps every written parameter, so a written `self` here is an ordinary parameter
    // shadowed by the injected receiver -- two things of one name, disagreeing about
    // arity. A method's written `self` is a receiver because buildMethodSignature drops
    // it; nothing drops this one.
    const Built b = build(std::string(kPrintf) +
        "struct V {\n"
        "    x <int>,\n"
        "    pub operator + (self: &Self, o: V) <int> { return self.x + o.x; }\n"
        "}\n"
        "fun main() <noret> { printf(\"ok\\n\"); }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("self"), std::string::npos) << b.why();
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
