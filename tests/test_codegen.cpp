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
    // A struct is well-typed and cannot be lowered yet. The compile must fail and
    // say so; the one outcome that must never happen is exit 0 with a binary whose
    // behaviour does not match the program.
    const Built b = build(
        "struct S { pub v <int>, }\n"
        "fun main() <noret> { let s <S> = S{v: 1}; }\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find("codegen"), std::string::npos) << b.why();
}

BACKEND_TEST(Soundness_Codegen, ARefusalNamesTheLine) {
    // A refusal with no location is a refusal the reader has to go looking for,
    // and in a file of any size that is most of the cost of the error. The
    // construct here is on line 3 rather than line 1 so that a location the
    // backend simply left default would not pass by accident.
    const Built b = build(
        "fun main() <noret> {\n"
        "    let i <int> = 0;\n"
        "    i++;\n"
        "}\n");
    EXPECT_NE(b.compileExit, 0) << b.why();
    EXPECT_NE(b.compileErr.find(".fin:3:"), std::string::npos) << b.why();
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
// The layout table and the backend are the same table.
// ---------------------------------------------------------------------------

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
