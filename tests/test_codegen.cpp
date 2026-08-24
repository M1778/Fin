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
    // A generic struct is well-typed and cannot be lowered: monomorphisation is a
    // unit of its own, and the erasure rule it has to obey (ADR 0002) is not
    // decided here. The compile must fail and say so; the one outcome that must
    // never happen is exit 0 with a binary whose behaviour does not match the
    // program.
    //
    // This used to use a plain struct, which is the better example of the rule
    // right up until the rule stops applying -- structs lower as of this unit, so
    // keeping it would have turned a passing refusal test into a passing test of
    // nothing. The construct in a refusal test is a moving part.
    const Built b = build(
        "struct Box<T> { pub v <T>, }\n"
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
    // It used to be `i++`, which is exactly the trap AnUnloweredConstructIsRefused
    // warns about one screen above: `++` lowers now, so this test went from
    // asserting a located refusal to asserting nothing, and it failed rather than
    // passing vacuously only because it checks the exit code too. The construct is a
    // generic struct for the same reason that one uses it -- monomorphisation is a
    // unit of its own and the erasure rule it must obey (ADR 0002) is not decided
    // here, so it is the furthest thing in this file from being lowered.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let i <int> = 1;\n"
        "}\n"
        "\n"
        "struct Box<T> { pub v <T>, }\n");
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
    // tested. A field default (`b <int> = 5`) parses and is not honoured
    // anywhere yet; when it is, this expectation changes with it.
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

BACKEND_TEST(Soundness_Codegen, AMethodCallOnAStructIsRefused) {
    // A struct with methods lowers as data; calling one needs a name for the
    // emitted symbol, and the mangling scheme needs finn's ABI story. Refused, not
    // guessed -- and specifically not skipped, which would drop the call and leave
    // the assignment reading an undefined value.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>\n"
        "    pub fun get() <int> { return self.a; }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 1 };\n"
        "    let x <int> = p.get();\n"
        "    printf(\"%d\\n\", x);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
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

BACKEND_TEST(Soundness_Codegen, AFieldDefaultIsRefusedRatherThanIgnored) {
    // `b <int> = 5` parses and nothing honours it. Zeroing `b` would give the
    // program a value it never wrote, and honouring it is a front-end unit that
    // does not exist -- so it refuses. This is the boundary of
    // AnOmittedFieldIsZeroed: zero is the answer for a field with no default.
    const Built b = build(std::string(kPrintf) +
        "struct P { a <int>, b <int> = 5 }\n"
        "fun main() <noret> {\n"
        "    let p <P> = P { a: 1 };\n"
        "    printf(\"%d %d\\n\", p.a, p.b);\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
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

BACKEND_TEST(Soundness_Codegen, AGenericStructIsRefused) {
    const Built b = build(std::string(kPrintf) +
        "struct Box<T> { v <T> }\n"
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
