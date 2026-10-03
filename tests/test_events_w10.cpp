#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Corpus.hpp"
#include "Pipeline.hpp"
#include "ast/decls/FunctionDecl.hpp"
#include "ast/decls/Program.hpp"
#include "ast/stmts/ControlFlow.hpp"
#include "ast/stmts/Statement.hpp"
#include "ast/stmts/VariableDecl.hpp"
#include "diagnostics/DiagnosticEngine.hpp"
#include "semantics/EventRegistry.hpp"
#include "semantics/LoopBackEdge.hpp"
#include "semantics/SemanticAnalyzer.hpp"

// Wave-4 step 20 (docs/compiler-api.md §3.2), W10: `loop_back_edge` — the
// LAST floor event (owner Q9: floor-last). A tracing collector needs
// safepoints; a call-free hot loop otherwise runs unboundedly.
//
// Prior batches landed everything else and are consumed here, not restated:
// collection/arming (test_events.cpp), W5 (struct_layout_finalised,
// variable_declared), W6 (function_entry/exit, assignment,
// allocation_site/delete_site), W7 (variable_scope_exit + moved). W9
// (parallel) owns live_pointers_quote — nothing here touches scopes.

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

class W10Src {
public:
    explicit W10Src(const std::string& contents) {
        path_ = uniqueTempPath("fin_events_w10", ".fin");
        std::ofstream f(path_, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    ~W10Src() { std::error_code ec; fs::remove(path_, ec); }
    std::string str() const { return path_.string(); }

private:
    fs::path path_;
};

FincRun w10compile(const std::string& code) {
    W10Src s(code);
    return runFinc({s.str()});
}

std::string w10messages(const std::string& stripped) {
    std::string out;
    for (size_t i = 0; i < stripped.size();) {
        size_t eol = stripped.find('\n', i);
        if (eol == std::string::npos) eol = stripped.size();
        if (stripped.compare(i, 7, "error: ") == 0 || stripped.compare(i, 9, "warning: ") == 0)
            out.append(stripped, i, eol - i).append("\n");
        i = eol + 1;
    }
    return out;
}

size_t w10errorCount(const std::string& stripped) {
    size_t n = 0;
    for (size_t i = 0; i < stripped.size();) {
        size_t eol = stripped.find('\n', i);
        if (eol == std::string::npos) eol = stripped.size();
        if (stripped.compare(i, 7, "error: ") == 0) ++n;
        i = eol + 1;
    }
    return n;
}

struct W10Analysis {
    bool parsed = false;
    int errorCount = 0;
    std::unique_ptr<fin::DiagnosticEngine> diag;
    std::unique_ptr<fin::SemanticAnalyzer> analyzer;
    std::unique_ptr<fin::Program> ast;
};

// Parses, analyses, and snapshots the back-edge points and the fired log.
// The analyzer stays alive in the result so the fired log can be read off it.
W10Analysis analyzeW10(const std::string& code) {
    W10Analysis a;
    a.diag = std::make_unique<fin::DiagnosticEngine>("", "<test>");
    a.diag->setColorMode(fin::ColorMode::Never);
    auto parsed = parseSource(code, *a.diag);
    a.parsed = parsed.parsed;
    if (!a.parsed) {
        a.errorCount = a.diag->getErrorCount();
        return a;
    }
    a.analyzer = std::make_unique<fin::SemanticAnalyzer>(*a.diag, false);
    a.analyzer->setModulePath("a.fin");
    a.analyzer->visit(*parsed.ast);
    a.errorCount = a.diag->getErrorCount();
    a.ast = std::move(parsed.ast);
    return a;
}

bool w10firedHas(const std::vector<fin::events::W10FiredHandler>& log,
                 const std::string& handler, const std::string& substr) {
    for (const auto& f : log)
        if (f.handler == handler && f.detail.find(substr) != std::string::npos) return true;
    return false;
}

const char* const kW10Handler =
    "@define printf(fmt: string, ...) <noret>;\n"
    "#[on(loop_back_edge)]\n"
    "@special h_sp(depth: int) <quote> {\n"
    "    return quote { printf(\"w10-safepoint\\n\"); };\n"
    "}\n";

} // namespace

// --- Slice 1: the payload row ------------------------------------------------
// docs/compiler-api.md §3.2: `loop_back_edge` carries `(depth: int)` and
// returns `quote`. One event, one owner: no older table answers for it.

TEST(W10Payload, LoopBackEdgeCarriesDepth) {
    const auto* p = fin::events::findW10Payload("loop_back_edge");
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(p->params.size(), 1u);
    EXPECT_EQ(p->params[0].name, "depth");
    EXPECT_EQ(p->params[0].type, "int");
    EXPECT_EQ(p->returns, "quote");
}

TEST(W10Payload, W10OwnsOnlyLoopBackEdge) {
    EXPECT_TRUE(fin::events::isW10Event("loop_back_edge"));
    EXPECT_FALSE(fin::events::isW10Event("variable_declared"));
    EXPECT_FALSE(fin::events::isW10Event("assignment"));
    EXPECT_FALSE(fin::events::isW10Event("function_exit"));
    EXPECT_FALSE(fin::events::isW10Event("variable_scope_exit"));
    EXPECT_EQ(fin::events::findW10Payload("variable_declared"), nullptr);
    EXPECT_EQ(fin::events::findW10Payload("nosuchevent"), nullptr);
    EXPECT_EQ(fin::events::findW5Payload("loop_back_edge"), nullptr);
    EXPECT_EQ(fin::events::findW6Payload("loop_back_edge"), nullptr);
    EXPECT_EQ(fin::events::findW7Payload("loop_back_edge"), nullptr);
}

// --- Slice 1: back-edge detection per loop form ------------------------------
// One static latch point per loop statement. The language has no `loop`
// statement (checked against NodeKind + the grammar): `for`, `while`
// (including `do-while`), and `foreach` are the whole set.

TEST(W10Detect, WhileLoopFiresOneBackEdge) {
    auto a = analyzeW10(
        "fun main() <noret> {\n"
        "    let c <bool> = true;\n"
        "    while (c) {\n"
        "        c = false;\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    ASSERT_EQ(a.analyzer->w10FirePoints().size(), 1u);
    EXPECT_EQ(a.analyzer->w10FirePoints()[0].kind, "while");
    EXPECT_EQ(a.analyzer->w10FirePoints()[0].depth, 1);
}

TEST(W10Detect, ForLoopFiresOneBackEdge) {
    auto a = analyzeW10(
        "fun main() <noret> {\n"
        "    for (i : int = 0; i < 3; i++) {\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    ASSERT_EQ(a.analyzer->w10FirePoints().size(), 1u);
    EXPECT_EQ(a.analyzer->w10FirePoints()[0].kind, "for");
    EXPECT_EQ(a.analyzer->w10FirePoints()[0].depth, 1);
}

TEST(W10Detect, ForeachLoopFiresOneBackEdge) {
    auto a = analyzeW10(
        "fun main() <noret> {\n"
        "    let a <[int, 5]> = [1,2,3,4,5];\n"
        "    foreach (element <int> in a) {\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    ASSERT_EQ(a.analyzer->w10FirePoints().size(), 1u);
    EXPECT_EQ(a.analyzer->w10FirePoints()[0].kind, "foreach");
    EXPECT_EQ(a.analyzer->w10FirePoints()[0].depth, 1);
}

TEST(W10Detect, DoWhileFiresOneBackEdge) {
    auto a = analyzeW10(
        "fun main() <noret> {\n"
        "    do {\n"
        "    } while (false);\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    ASSERT_EQ(a.analyzer->w10FirePoints().size(), 1u);
    EXPECT_EQ(a.analyzer->w10FirePoints()[0].depth, 1);
}

TEST(W10Detect, NestedLoopsCarryDepth) {
    auto a = analyzeW10(
        "fun main() <noret> {\n"
        "    let c <bool> = true;\n"
        "    let d <bool> = true;\n"
        "    while (c) {\n"
        "        while (d) {\n"
        "            break;\n"
        "        }\n"
        "        c = false;\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    ASSERT_EQ(a.analyzer->w10FirePoints().size(), 2u);
    EXPECT_EQ(a.analyzer->w10FirePoints()[0].depth, 1);
    EXPECT_EQ(a.analyzer->w10FirePoints()[1].depth, 2);
}

TEST(W10Detect, CallFreeHotLoopFiresAndTerminates) {
    // The safepoint case itself: a loop with no calls in it. The point fires
    // per static edge, not per iteration, so analysis terminates even though
    // the loop at runtime would not.
    auto a = analyzeW10(
        "fun main() <noret> {\n"
        "    let c <bool> = true;\n"
        "    while (c) {\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    ASSERT_EQ(a.analyzer->w10FirePoints().size(), 1u);
    EXPECT_EQ(a.analyzer->w10FirePoints()[0].kind, "while");
}

TEST(W10Detect, NoLoopNoPoints) {
    auto a = analyzeW10("fun main() <noret> {\n    let x <int> = 1;\n}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    EXPECT_TRUE(a.analyzer->w10FirePoints().empty());
}

TEST(W10Detect, IfIsNotALoop) {
    auto a = analyzeW10(
        "fun main() <noret> {\n"
        "    let c <bool> = true;\n"
        "    if (c) {\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    EXPECT_TRUE(a.analyzer->w10FirePoints().empty());
}

// --- Slice 2: the signature IS the payload -----------------------------------

TEST(W10Mismatch, WrongArityIsRefused) {
    auto r = w10compile(
        "#[on(loop_back_edge)]\n@special h_sp() <quote> {}\n"
        "compiler.events.enable(h_sp);\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w10messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_sp"), std::string::npos) << err;
    EXPECT_NE(err.find("loop_back_edge"), std::string::npos) << err;
    EXPECT_NE(err.find("(depth: int) <quote>"), std::string::npos)
        << "the refusal must name the expected payload:\n" << err;
}

TEST(W10Mismatch, WrongParamTypeIsRefused) {
    auto r = w10compile(
        "#[on(loop_back_edge)]\n@special h_sp(depth: string) <quote> {}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w10messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_sp"), std::string::npos) << err;
    EXPECT_NE(err.find("(depth: int) <quote>"), std::string::npos) << err;
}

TEST(W10Mismatch, WrongReturnIsRefused) {
    auto r = w10compile(
        "#[on(loop_back_edge)]\n@special h_sp(depth: int) <void> {}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w10messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_sp"), std::string::npos) << err;
    EXPECT_NE(err.find("(depth: int) <quote>"), std::string::npos) << err;
}

TEST(W10Mismatch, MismatchIsDiagnosedOnce) {
    auto r = w10compile(
        "#[on(loop_back_edge)]\n@special h_sp() <quote> {}\n"
        "compiler.events.enable(h_sp);\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    EXPECT_EQ(w10errorCount(stripAnsi(r.err)), 1u) << r.err;
}

TEST(W10Line, LoopInAHandlerIsRefusedByName) {
    auto r = w10compile(
        "#[on(loop_back_edge)]\n"
        "@special h_sp(depth: int) <quote> {\n"
        "    while (1 == 1) {}\n"
        "}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w10messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_sp"), std::string::npos) << err;
    EXPECT_NE(err.find("control flow"), std::string::npos) << err;
}

// --- Slice 2: firing with identity payload + ordering ------------------------

TEST(W10Fire, OnlyArmedHandlersFire) {
    auto a = analyzeW10(
        "#[on(loop_back_edge)]\n@special armed_h(depth: int) <quote> {}\n"
        "#[on(loop_back_edge)]\n@special quiet_h(depth: int) <quote> {}\n"
        "compiler.events.enable(armed_h);\n"
        "fun main() <noret> {\n"
        "    let c <bool> = true;\n"
        "    while (c) {\n"
        "        c = false;\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    EXPECT_TRUE(w10firedHas(a.analyzer->w10Fired(), "armed_h", "while"));
    EXPECT_FALSE(w10firedHas(a.analyzer->w10Fired(), "quiet_h", "while"));
}

TEST(W10Fire, FiresInSourceOrderWithIdentity) {
    auto a = analyzeW10(
        std::string(kW10Handler) +
        "compiler.events.enable(h_sp);\n"
        "fun main() <noret> {\n"
        "    let c <bool> = true;\n"
        "    while (c) {\n"
        "        c = false;\n"
        "    }\n"
        "    for (i : int = 0; i < 3; i++) {\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    ASSERT_EQ(a.analyzer->w10Fired().size(), 2u);
    EXPECT_NE(a.analyzer->w10Fired()[0].detail.find("while"), std::string::npos)
        << a.analyzer->w10Fired()[0].detail;
    EXPECT_NE(a.analyzer->w10Fired()[0].detail.find("depth=1"), std::string::npos)
        << a.analyzer->w10Fired()[0].detail;
    EXPECT_NE(a.analyzer->w10Fired()[1].detail.find("for"), std::string::npos)
        << a.analyzer->w10Fired()[1].detail;
}

TEST(W10Fire, SpliceLandsAtLoopHeader) {
    // The safepoint poll runs each iteration, including past a `continue`:
    // the quote is prepended to the body block, not appended after it.
    auto a = analyzeW10(
        std::string(kW10Handler) +
        "compiler.events.enable(h_sp);\n"
        "fun main() <noret> {\n"
        "    let c <bool> = true;\n"
        "    while (c) {\n"
        "        let t <int> = 1;\n"
        "        c = false;\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    ASSERT_NE(a.ast, nullptr);
    const fin::WhileLoop* loop = nullptr;
    for (const auto& stmt : a.ast->statements) {
        const auto* fn = dynamic_cast<const fin::FunctionDeclaration*>(stmt.get());
        if (!fn || fn->name != "main" || !fn->body) continue;
        for (const auto& inner : fn->body->statements) {
            if (const auto* w = dynamic_cast<const fin::WhileLoop*>(inner.get())) loop = w;
        }
    }
    ASSERT_NE(loop, nullptr) << "the while loop survives analysis";
    ASSERT_NE(loop->body, nullptr);
    ASSERT_GE(loop->body->statements.size(), 3u) << "header injection + 2 originals";
    EXPECT_NE(dynamic_cast<const fin::ExpressionStatement*>(loop->body->statements[0].get()),
              nullptr)
        << "the injected safepoint is first in the body (the loop header)";
    EXPECT_NE(dynamic_cast<const fin::VariableDeclaration*>(loop->body->statements[1].get()),
              nullptr)
        << "the original first statement follows the injection";
}

TEST(W10Fire, BlameAbortsNamingHandlerEventAndPoint) {
    auto r = w10compile(
        "#[on(loop_back_edge)]\n@special first_sp(depth: int) <quote> {\n"
        "    blame 1 == 1, \"first saw latch\";\n"
        "}\n"
        "#[on(loop_back_edge)]\n@special second_sp(depth: int) <quote> {\n"
        "    blame 1 == 1, \"second saw latch\";\n"
        "}\n"
        "compiler.events.enable(first_sp);\n"
        "compiler.events.enable(second_sp);\n"
        "fun main() <noret> {\n"
        "    let c <bool> = true;\n"
        "    while (c) {\n"
        "        c = false;\n"
        "    }\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w10messages(stripAnsi(r.err));
    EXPECT_NE(err.find("first_sp"), std::string::npos) << err;
    EXPECT_NE(err.find("second_sp"), std::string::npos)
        << "the remaining handler still runs after the first aborts:\n" << err;
    EXPECT_NE(err.find("loop_back_edge"), std::string::npos) << err;
    EXPECT_EQ(w10errorCount(stripAnsi(r.err)), 2u) << r.err;
}

TEST(W10Order, CoRegisteredHandlersOrderByModuleDagThenDeclaration) {
    fin::events::EventRegistry r;
    r.recordImport("root.fin", "lib.fin");
    r.add({"loop_back_edge", "second", "lib.fin", 1, 0, false});
    r.add({"loop_back_edge", "first", "lib.fin", 0, 0, false});
    r.add({"loop_back_edge", "root_h", "root.fin", 0, 0, false});
    auto ordered = r.orderedHandlers("loop_back_edge");
    ASSERT_EQ(ordered.size(), 3u);
    EXPECT_EQ(ordered[0].handler, "first");
    EXPECT_EQ(ordered[1].handler, "second");
    EXPECT_EQ(ordered[2].handler, "root_h");
}

#ifndef FIN_TESTS_HAVE_BACKEND
#define W10_BACKEND_TEST(suite, name)                                            \
    TEST(suite, name) { GTEST_SKIP() << "built with FIN_WITH_LLVM=OFF"; }       \
    [[maybe_unused]] static void w10_backend_body_##suite##_##name()
#else
#define W10_BACKEND_TEST(suite, name) TEST(suite, name)
#endif

namespace {

std::string w10shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

struct W10Built {
    int compileExit = -1;
    std::string compileErr;
    bool ran = false;
    int runExit = -1;
    std::string out;
};

W10Built w10buildRun(const std::string& code) {
    W10Built b;
    fs::path src = uniqueTempPath("fin_w10", ".fin");
    fs::path exe = uniqueTempPath("fin_w10_exe");
    {
        std::ofstream f(src, std::ios::binary);
        f.write(code.data(), (std::streamsize)code.size());
    }
    const FincRun c = runFinc({src.string(), "-o", exe.string()});
    b.compileExit = c.exitCode;
    b.compileErr = stripAnsi(c.err);
    if (b.compileExit == 0 && fs::exists(exe)) {
        fs::path outPath = uniqueTempPath("fin_w10_out");
        std::string cmd =
            w10shellQuote(exe.string()) + " > " + w10shellQuote(outPath.string()) + " 2>&1";
        int status = std::system(cmd.c_str());
        b.runExit = status;
        b.ran = true;
        std::ifstream f(outPath, std::ios::binary);
        b.out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        std::error_code ec;
        fs::remove(outPath, ec);
    }
    std::error_code ec;
    fs::remove(src, ec);
    fs::remove(exe, ec);
    return b;
}

} // namespace

// --- Slice 3: Fin probe ------------------------------------------------------
// A safepoint-style handler observing a hot loop: the injected poll runs and
// the program's own behavior is unchanged.

W10_BACKEND_TEST(W10LoopProbe, InjectedMarkerRunsAndBehaviorUnchanged) {
    W10Built b = w10buildRun(
        std::string(kW10Handler) +
        "compiler.events.enable(h_sp);\n"
        "fun main() <noret> {\n"
        "    let s <int> = 0;\n"
        "    for (i : int = 0; i < 3; i++) {\n"
        "        s = s + i;\n"
        "    }\n"
        "    printf(\"sum %d\\n\", s);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.compileErr;
    ASSERT_TRUE(b.ran) << "compile exit " << b.compileExit << "\n" << b.compileErr;
    EXPECT_NE(b.out.find("w10-safepoint"), std::string::npos)
        << "the handler's quote ran at the back edge:\n" << b.out;
    EXPECT_NE(b.out.find("sum 3"), std::string::npos)
        << "the loop still computed its own answer:\n" << b.out;
}

W10_BACKEND_TEST(W10LoopProbe, UnarmedHandlerInjectsNothing) {
    W10Built b = w10buildRun(
        std::string(kW10Handler) +
        "fun main() <noret> {\n"
        "    let s <int> = 0;\n"
        "    for (i : int = 0; i < 3; i++) {\n"
        "        s = s + i;\n"
        "    }\n"
        "    printf(\"sum %d\\n\", s);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.compileErr;
    ASSERT_TRUE(b.ran) << "compile exit " << b.compileExit << "\n" << b.compileErr;
    EXPECT_EQ(b.out.find("w10-safepoint"), std::string::npos)
        << "nothing fires without arming:\n" << b.out;
    EXPECT_NE(b.out.find("sum 3"), std::string::npos) << b.out;
}

// --- Straight-line threading (the comptime value model, ADR 0006) -----------
// Lets, bare calls, rebinds and known-bool branches thread with parameters
// bound; loops stay refused, anything wider the existing named gap.

TEST(W10Threaded, LetBeforeReturnQuoteEvaluates) {
    auto a = analyzeW10(
        "@define printf(fmt: string, ...) <noret>;\n"
        "#[on(loop_back_edge)]\n"
        "@special h_sp(depth: int) <quote> {\n"
        "    let d <int> = depth;\n"
        "    return quote { printf(\"w10-threaded\\n\"); };\n"
        "}\n"
        "compiler.events.enable(h_sp);\n"
        "fun main() <noret> {\n"
        "    let c <bool> = true;\n"
        "    while (c) {\n"
        "        c = false;\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(a.errorCount, 0) << "a let aliasing the depth parameter threads";
    EXPECT_TRUE(w10firedHas(a.analyzer->w10Fired(), "h_sp", "while"));
}

TEST(W10Threaded, CallBeforeReturnQuoteEvaluates) {
    auto a = analyzeW10(
        "@define printf(fmt: string, ...) <noret>;\n"
        "fun id(v: int) <int> {\n"
        "  return v;\n"
        "}\n"
        "#[on(loop_back_edge)]\n"
        "@special h_sp(depth: int) <quote> {\n"
        "    let y <int> = id(7);\n"
        "    return quote { printf(\"w10-called\\n\"); };\n"
        "}\n"
        "compiler.events.enable(h_sp);\n"
        "fun main() <noret> {\n"
        "    let c <bool> = true;\n"
        "    while (c) {\n"
        "        c = false;\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(a.errorCount, 0) << "a straight-line helper call threads";
    EXPECT_TRUE(w10firedHas(a.analyzer->w10Fired(), "h_sp", "while"));
}

TEST(W10Threaded, HelperQuoteReturnSplices) {
    auto a = analyzeW10(
        "@define printf(fmt: string, ...) <noret>;\n"
        "@special mkq() <quote> { return quote { printf(\"w10-q\\n\"); }; }\n"
        "#[on(loop_back_edge)]\n"
        "@special h_sp(depth: int) <quote> {\n"
        "    let q <quote> = mkq();\n"
        "    return q;\n"
        "}\n"
        "compiler.events.enable(h_sp);\n"
        "fun main() <noret> {\n"
        "    let c <bool> = true;\n"
        "    while (c) {\n"
        "        c = false;\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(a.errorCount, 0) << "a helper call evaluating to a quote splices";
    EXPECT_TRUE(w10firedHas(a.analyzer->w10Fired(), "h_sp", "while"));
}

TEST(W10Threaded, NonQuoteReturnStillRefused) {
    auto r = w10compile(
        "#[on(loop_back_edge)]\n"
        "@special h_sp(depth: int) <quote> {\n"
        "    return 42;\n"
        "}\n"
        "compiler.events.enable(h_sp);\n"
        "fun main() <noret> {\n"
        "    let c <bool> = true;\n"
        "    while (c) {\n"
        "        c = false;\n"
        "    }\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    EXPECT_NE(w10messages(stripAnsi(r.err)).find("only a quote literal"), std::string::npos)
        << r.err;
}

W10_BACKEND_TEST(W10ThreadedProbe, ThreadedMarkerRunsAndBehaviorUnchanged) {
    W10Built b = w10buildRun(
        "@define printf(fmt: string, ...) <noret>;\n"
        "#[on(loop_back_edge)]\n"
        "@special h_sp(depth: int) <quote> {\n"
        "    let d <int> = depth;\n"
        "    return quote { printf(\"w10-threaded\\n\"); };\n"
        "}\n"
        "compiler.events.enable(h_sp);\n"
        "fun main() <noret> {\n"
        "    let s <int> = 0;\n"
        "    for (i : int = 0; i < 3; i++) {\n"
        "        s = s + i;\n"
        "    }\n"
        "    printf(\"sum %d\\n\", s);\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.compileErr;
    ASSERT_TRUE(b.ran) << "compile exit " << b.compileExit << "\n" << b.compileErr;
    EXPECT_NE(b.out.find("w10-threaded"), std::string::npos)
        << "the threaded handler's quote ran at the back edge:\n" << b.out;
    EXPECT_NE(b.out.find("sum 3"), std::string::npos)
        << "the loop still computed its own answer:\n" << b.out;
}
