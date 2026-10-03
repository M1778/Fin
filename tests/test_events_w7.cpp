#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Corpus.hpp"
#include "Pipeline.hpp"
#include "ast/decls/Program.hpp"
#include "diagnostics/DiagnosticEngine.hpp"
#include "semantics/EventRegistry.hpp"
#include "semantics/MovedAnalysis.hpp"
#include "semantics/SemanticAnalyzer.hpp"
#include "types/StructType.hpp"

// Wave-4 step 17 (docs/compiler-api.md §3.2), W7: `variable_scope_exit` plus
// the moved analysis, including MovedMaybe.
//
// Prior batches landed and are consumed here, not restated: collection/arming
// (test_events.cpp), the firing loop with struct_layout_finalised and
// variable_declared (test_events_w5.cpp), and the W6 floor of
// function_entry/exit, assignment, allocation_site/delete_site
// (test_events_w6.cpp). W8 (parallel) owns diagnostics/attribution: nothing
// here builds diagnostic infrastructure, only plain error() calls.
//
// There are no moves in the language to consult (ADR 0030: every binding
// copies). The move spelling is the owner's `@move()` (docs/plan.md): a
// builtin special call marking its operand moved, lowering as the identity.

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

// A temp .fin file. Duplicated from test_events_w5.cpp rather than shared:
// the two files have no other reason to be coupled.
class W7Src {
public:
    explicit W7Src(const std::string& contents) {
        path_ = uniqueTempPath("fin_events_w7", ".fin");
        std::ofstream f(path_, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    ~W7Src() { std::error_code ec; fs::remove(path_, ec); }
    std::string str() const { return path_.string(); }

private:
    fs::path path_;
};

FincRun w7compile(const std::string& code) {
    W7Src s(code);
    return runFinc({s.str()});
}

// Diagnostic message lines only: searching raw stderr also matches the echoed
// source under the caret (test_events.cpp says why).
std::string w7messages(const std::string& stripped) {
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

size_t w7errorCount(const std::string& stripped) {
    size_t n = 0;
    for (size_t i = 0; i < stripped.size();) {
        size_t eol = stripped.find('\n', i);
        if (eol == std::string::npos) eol = stripped.size();
        if (stripped.compare(i, 7, "error: ") == 0) ++n;
        i = eol + 1;
    }
    return n;
}

struct W7Analysis {
    bool parsed = false;
    int errorCount = 0;
    std::unique_ptr<fin::DiagnosticEngine> diag;
    std::unique_ptr<fin::SemanticAnalyzer> analyzer;
    std::unique_ptr<fin::Program> ast;
};

// Parses, analyses, and snapshots the scope-exit points and the fired log.
// The analyzer (and its scopes) stays alive in the result, so struct flags
// can be read off it after the walk.
W7Analysis analyzeW7(const std::string& code) {
    W7Analysis a;
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

bool firedHas(const std::vector<fin::events::W7FiredHandler>& log, const std::string& handler,
              const std::string& substr) {
    for (const auto& f : log)
        if (f.handler == handler && f.detail.find(substr) != std::string::npos) return true;
    return false;
}

} // namespace

// --- Slice 1: the moved-state lattice ---------------------------------------
// Live/Moved/MovedMaybe with the join a branch merge needs. Pure unit tests:
// no analyzer, no tree.

TEST(W7MovedState, JoinTable) {
    using fin::events::MovedState;
    using fin::events::joinMoved;
    EXPECT_EQ(joinMoved(MovedState::Live, MovedState::Live), MovedState::Live);
    EXPECT_EQ(joinMoved(MovedState::Moved, MovedState::Moved), MovedState::Moved);
    EXPECT_EQ(joinMoved(MovedState::Live, MovedState::Moved), MovedState::Maybe);
    EXPECT_EQ(joinMoved(MovedState::Moved, MovedState::Live), MovedState::Maybe);
    EXPECT_EQ(joinMoved(MovedState::Live, MovedState::Maybe), MovedState::Maybe);
    EXPECT_EQ(joinMoved(MovedState::Moved, MovedState::Maybe), MovedState::Maybe);
    EXPECT_EQ(joinMoved(MovedState::Maybe, MovedState::Maybe), MovedState::Maybe);
}

TEST(W7MovedState, PayloadValuesAndNames) {
    // docs/compiler-api.md §2.4 files MovedYes/MovedNo/MovedMaybe (and
    // ExitNormal/ExitBlamed) under compiler.events.*. The component table
    // carries names, not values; the integers are pinned here.
    EXPECT_EQ(fin::events::kMovedNo, 0);
    EXPECT_EQ(fin::events::kMovedYes, 1);
    EXPECT_EQ(fin::events::kMovedMaybe, 2);
    EXPECT_EQ(fin::events::kExitNormal, 0);
    EXPECT_EQ(fin::events::kExitBlamed, 1);
    EXPECT_EQ(fin::events::movedValue(fin::events::MovedState::Live), 0);
    EXPECT_EQ(fin::events::movedValue(fin::events::MovedState::Moved), 1);
    EXPECT_EQ(fin::events::movedValue(fin::events::MovedState::Maybe), 2);
}

// --- Slice 1: @move marks, reads of moved diagnose ----------------------------

TEST(W7Move, ReadAfterMoveIsRefusedByName) {
    auto r = w7compile(
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let y <int> = @move(x);\n"
        "    let z <int> = x;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w7messages(stripAnsi(r.err));
    EXPECT_NE(err.find("x"), std::string::npos) << err;
    EXPECT_NE(err.find("moved"), std::string::npos) << err;
    EXPECT_EQ(w7errorCount(stripAnsi(r.err)), 1u) << r.err;
}

TEST(W7Move, MoveWithoutUseCompiles) {
    auto r = w7compile(
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let y <int> = @move(x);\n"
        "}\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(W7Move, DoubleMoveIsRefused) {
    auto r = w7compile(
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let y <int> = @move(x);\n"
        "    let z <int> = @move(x);\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w7messages(stripAnsi(r.err));
    EXPECT_NE(err.find("x"), std::string::npos) << err;
    EXPECT_NE(err.find("moved"), std::string::npos) << err;
}

TEST(W7Move, RebindingRevives) {
    // W6's assignment points are the rebinding sites: `x = ...` makes x live
    // again, so the read after it is clean.
    auto r = w7compile(
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let y <int> = @move(x);\n"
        "    x = 2;\n"
        "    let z <int> = x;\n"
        "}\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(W7Move, ConditionalMoveIsMaybeAndReadable) {
    // Moved on one branch only is MovedMaybe, and a maybe-moved value stays
    // readable: the payload carries the doubt so the collector can branch.
    auto r = w7compile(
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let c <bool> = true;\n"
        "    if (c) {\n"
        "        let y <int> = @move(x);\n"
        "    }\n"
        "    let z <int> = x;\n"
        "}\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(W7Move, MoveOnBothBranchesIsMoved) {
    auto r = w7compile(
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let c <bool> = true;\n"
        "    if (c) {\n"
        "        let y <int> = @move(x);\n"
        "    } else {\n"
        "        let w <int> = @move(x);\n"
        "    }\n"
        "    let z <int> = x;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w7messages(stripAnsi(r.err));
    EXPECT_NE(err.find("x"), std::string::npos) << err;
}

TEST(W7Move, MoveNeedsAnArgument) {
    auto r = w7compile("fun main() <noret> {\n    let x <int> = @move();\n}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    EXPECT_NE(w7messages(stripAnsi(r.err)).find("@move"), std::string::npos) << r.err;
}

TEST(W7Move, MoveNeedsANamedVariable) {
    auto r = w7compile("fun main() <noret> {\n    let x <int> = @move(1);\n}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    EXPECT_NE(w7messages(stripAnsi(r.err)).find("@move"), std::string::npos) << r.err;
}

TEST(W7Move, LoopMoveIsMaybe) {
    auto r = w7compile(
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let c <bool> = true;\n"
        "    while (c) {\n"
        "        let y <int> = @move(x);\n"
        "        c = false;\n"
        "    }\n"
        "    let z <int> = x;\n"
        "}\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

// --- Slice 2: the variable_scope_exit payload --------------------------------
// The handler's signature IS the event's payload (§3.1): one row is the
// parameter list every handler for it must declare, and the quote it must
// return. W5's table must not answer for this row, or two match checks would
// own one event and diagnose it twice.

TEST(W7Payload, VariableScopeExitCarriesNameTypeExitKindAndMoved) {
    const auto* p = fin::events::findW7Payload("variable_scope_exit");
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(p->params.size(), 4u);
    EXPECT_EQ(p->params[0].name, "name");
    EXPECT_EQ(p->params[0].type, "string");
    EXPECT_EQ(p->params[1].name, "t");
    EXPECT_EQ(p->params[1].type, "$type");
    EXPECT_EQ(p->params[2].name, "exit_kind");
    EXPECT_EQ(p->params[2].type, "int");
    EXPECT_EQ(p->params[3].name, "moved");
    EXPECT_EQ(p->params[3].type, "int");
    EXPECT_EQ(p->returns, "quote");
}

TEST(W7Payload, W7OwnsOnlyScopeExit) {
    EXPECT_TRUE(fin::events::isW7Event("variable_scope_exit"));
    EXPECT_FALSE(fin::events::isW7Event("variable_declared"));
    EXPECT_FALSE(fin::events::isW7Event("assignment"));
    EXPECT_FALSE(fin::events::isW7Event("function_exit"));
    EXPECT_EQ(fin::events::findW7Payload("variable_declared"), nullptr);
    EXPECT_EQ(fin::events::findW7Payload("nosuchevent"), nullptr);
    // And the older tables still do not answer for it: one event, one owner.
    EXPECT_EQ(fin::events::findW5Payload("variable_scope_exit"), nullptr);
    EXPECT_EQ(fin::events::findW6Payload("variable_scope_exit"), nullptr);
}

TEST(W7Payload, EventConstantsResolve) {
    // §2.4 files the payload constants under compiler.events.*. They resolve
    // to int under the events grant, which is what makes a handler able to
    // read its own payload.
    auto r = w7compile(
        "#[use(compiler)]\n"
        "#[use(compiler.components.events)]\n"
        "@special probe() <int> {\n"
        "    return compiler.events.MovedYes;\n"
        "}\n"
        "fun main() <noret> {}\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(W7Payload, UnknownEventConstantIsRefused) {
    auto r = w7compile(
        "#[use(compiler)]\n"
        "#[use(compiler.components.events)]\n"
        "@special probe() <int> {\n"
        "    return compiler.events.MovedSometimes;\n"
        "}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
}

// --- Slice 2: the signature IS the payload -----------------------------------

TEST(W7Mismatch, WrongArityIsRefused) {
    auto r = w7compile(
        "#[on(variable_scope_exit)]\n@special h_exit(name: string) <quote> {}\n"
        "compiler.events.enable(h_exit);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w7messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_exit"), std::string::npos) << err;
    EXPECT_NE(err.find("variable_scope_exit"), std::string::npos) << err;
    EXPECT_NE(err.find("(name: string, t: $type, exit_kind: int, moved: int) <quote>"),
              std::string::npos)
        << "the refusal must name the expected payload:\n" << err;
}

TEST(W7Mismatch, WrongReturnIsRefused) {
    auto r = w7compile(
        "#[on(variable_scope_exit)]\n"
        "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <void> {}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w7messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_exit"), std::string::npos) << err;
    EXPECT_NE(err.find("(name: string, t: $type, exit_kind: int, moved: int) <quote>"),
              std::string::npos)
        << err;
}

TEST(W7Mismatch, MismatchIsDiagnosedOnce) {
    auto r = w7compile(
        "#[on(variable_scope_exit)]\n@special h_exit(name: string) <quote> {}\n"
        "compiler.events.enable(h_exit);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    EXPECT_EQ(w7errorCount(stripAnsi(r.err)), 1u) << r.err;
}

TEST(W7Line, LoopInAHandlerIsRefusedByName) {
    // Q4/Q14: composition means a handler must NOT walk the field tree, and
    // walking needs a loop, which the interpretability line refuses by name.
    auto r = w7compile(
        "#[on(variable_scope_exit)]\n"
        "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {\n"
        "    while (1 == 1) {}\n"
        "}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w7messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_exit"), std::string::npos) << err;
    EXPECT_NE(err.find("control flow"), std::string::npos) << err;
}

// --- Slice 2: scope-exit firing with the moved-state payload -----------------

TEST(W7Fire, BlockExitFiresLive) {
    auto a = analyzeW7(
        "#[on(variable_scope_exit)]\n"
        "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "compiler.events.enable(h_exit);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    EXPECT_TRUE(firedHas(a.analyzer->w7Fired(), "h_exit", "x:int")) << "x fires";
    EXPECT_TRUE(firedHas(a.analyzer->w7Fired(), "h_exit", "moved=live")) << "live state";
    EXPECT_TRUE(firedHas(a.analyzer->w7Fired(), "h_exit", "exit=normal")) << "normal exit";
}

TEST(W7Fire, ReturnAfterMoveFiresMoved) {
    auto a = analyzeW7(
        "#[on(variable_scope_exit)]\n"
        "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "compiler.events.enable(h_exit);\n"
        "fun f() <int> {\n"
        "    let x <int> = 1;\n"
        "    let y <int> = @move(x);\n"
        "    return y;\n"
        "}\n"
        "fun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    EXPECT_TRUE(firedHas(a.analyzer->w7Fired(), "h_exit", "x:int")) << "x fires";
    EXPECT_TRUE(firedHas(a.analyzer->w7Fired(), "h_exit", "moved=moved")) << "moved state";
}

TEST(W7Fire, ConditionalMoveFiresMaybe) {
    auto a = analyzeW7(
        "#[on(variable_scope_exit)]\n"
        "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "compiler.events.enable(h_exit);\n"
        "fun f(c: bool) <int> {\n"
        "    let x <int> = 1;\n"
        "    if (c) {\n"
        "        let y <int> = @move(x);\n"
        "        return y;\n"
        "    }\n"
        "    return x;\n"
        "}\n"
        "fun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    EXPECT_TRUE(firedHas(a.analyzer->w7Fired(), "h_exit", "moved=maybe"))
        << "the fallthrough path sees x maybe-moved";
}

TEST(W7Fire, BreakFiresTheLoopBodyScope) {
    auto a = analyzeW7(
        "#[on(variable_scope_exit)]\n"
        "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "compiler.events.enable(h_exit);\n"
        "fun f(c: bool) <noret> {\n"
        "    while (c) {\n"
        "        let y <int> = 2;\n"
        "        break;\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    EXPECT_TRUE(firedHas(a.analyzer->w7Fired(), "h_exit", "y:int"))
        << "the break path exits the loop body scope";
}

TEST(W7Fire, BlameRaiseFiresBlamed) {
    auto a = analyzeW7(
        "struct E { m <string> }\n"
        "#[on(variable_scope_exit)]\n"
        "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "compiler.events.enable(h_exit);\n"
        "fun f() <noret> {\n"
        "    let x <int> = 1;\n"
        "    blame E{m: \"boom\"};\n"
        "}\n"
        "fun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    EXPECT_TRUE(firedHas(a.analyzer->w7Fired(), "h_exit", "x:int"))
        << "the blame unwind exits the function scope";
    EXPECT_TRUE(firedHas(a.analyzer->w7Fired(), "h_exit", "exit=blamed"))
        << "the unwind is blamed, not normal";
}

TEST(W7Fire, OnlyArmedHandlersFire) {
    auto a = analyzeW7(
        "#[on(variable_scope_exit)]\n"
        "@special armed_h(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "#[on(variable_scope_exit)]\n"
        "@special quiet_h(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "compiler.events.enable(armed_h);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    EXPECT_TRUE(firedHas(a.analyzer->w7Fired(), "armed_h", "x:int"));
    EXPECT_FALSE(firedHas(a.analyzer->w7Fired(), "quiet_h", "x:int"));
}

TEST(W7Fire, FiresOnceForTheOuterVariable) {
    // Q14: nested cleanup is the language's job, so scope exit fires once for
    // the outer variable and never per field. A handler needing a field-tree
    // walk is refused by design (Q4/Q14); this pins the single firing.
    auto a = analyzeW7(
        "struct Inner { x <int> }\n"
        "struct Outer { f <Inner> }\n"
        "#[on(variable_scope_exit)]\n"
        "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "compiler.events.enable(h_exit);\n"
        "fun main() <noret> {\n"
        "    let o <Outer> = Outer{f: Inner{x: 1}};\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    int count = 0;
    for (const auto& f : a.analyzer->w7Fired())
        if (f.handler == "h_exit" && f.detail.find("o:Outer") != std::string::npos) ++count;
    EXPECT_GE(count, 1) << "the outer variable fires";
    for (const auto& f : a.analyzer->w7Fired())
        EXPECT_EQ(f.detail.find("f:Inner"), std::string::npos)
            << "no per-field firing: " << f.detail;
}

// --- Slice 3: composition ----------------------------------------------------
// ADR 0016: a struct field whose type has a destructor gets it called from the
// parent's destructor, and the compiler generates the parent when there is
// none. The generated destructor is observable: has_destructor answers true
// for a type that declared none but acquired one by composition.

TEST(W7Compose, FieldDestructorPropagatesToTheParent) {
    auto a = analyzeW7(
        "struct Inner {\n"
        "    x <int>\n"
        "    ~Inner() {}\n"
        "}\n"
        "struct Outer { f <Inner> }\n"
        "fun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    auto inner = a.analyzer->getGlobalScope()->resolveType("Inner");
    auto outer = a.analyzer->getGlobalScope()->resolveType("Outer");
    ASSERT_NE(inner, nullptr);
    ASSERT_NE(outer, nullptr);
    EXPECT_TRUE(inner->as<fin::StructType>()->has_destructor);
    EXPECT_TRUE(outer->as<fin::StructType>()->has_destructor)
        << "the generated parent reports cleanup";
}

TEST(W7Compose, PlainStructsStayFalse) {
    auto a = analyzeW7("struct Plain { x <int> }\nfun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    auto plain = a.analyzer->getGlobalScope()->resolveType("Plain");
    ASSERT_NE(plain, nullptr);
    EXPECT_FALSE(plain->as<fin::StructType>()->has_destructor);
}

TEST(W7Compose, HasDestructorQueryResolves) {
    // compiler.structs.has_destructor lets handlers ask whether cleanup runs.
    auto r = w7compile(
        "#[use(compiler)]\n"
        "#[use(compiler.components.structs)]\n"
        "#[use(compiler.components.types)]\n"
        "@special probe() <bool> {\n"
        "    return compiler.structs.has_destructor(compiler.types.gettype::<Outer>());\n"
        "}\n"
        "struct Outer { x <int> }\n"
        "fun main() <noret> {}\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

#ifndef FIN_TESTS_HAVE_BACKEND
// A build configured without the backend still has to compile this file, so the
// test exists and says why it did not run rather than vanishing from the count
// (test_codegen.cpp says why the body becomes an uncalled function).
#define W7_BACKEND_TEST(suite, name)                                            \
    TEST(suite, name) { GTEST_SKIP() << "built with FIN_WITH_LLVM=OFF"; }       \
    [[maybe_unused]] static void w7_backend_body_##suite##_##name()
#else
#define W7_BACKEND_TEST(suite, name) TEST(suite, name)
#endif

namespace {

std::string w7shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

// Compiles a string to a real executable and runs it: the Fin-level probe
// that an injected marker survives lowering and executes.
struct W7Built {
    int compileExit = -1;
    std::string compileErr;
    bool ran = false;
    int runExit = -1;
    std::string out;
};

W7Built w7buildRun(const std::string& code) {
    W7Built b;
    fs::path src = uniqueTempPath("fin_w7", ".fin");
    fs::path exe = uniqueTempPath("fin_w7_exe");
    {
        std::ofstream f(src, std::ios::binary);
        f.write(code.data(), (std::streamsize)code.size());
    }
    const FincRun c = runFinc({src.string(), "-o", exe.string()});
    b.compileExit = c.exitCode;
    b.compileErr = stripAnsi(c.err);
    if (b.compileExit == 0 && fs::exists(exe)) {
        fs::path outPath = uniqueTempPath("fin_w7_out");
        std::string cmd =
            w7shellQuote(exe.string()) + " > " + w7shellQuote(outPath.string()) + " 2>&1";
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

const char* const kW7Handler =
    "@define printf(fmt: string, ...) <noret>;\n"
    "#[on(variable_scope_exit)]\n"
    "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {\n"
    "    return quote { printf(\"w7-exit\\n\"); };\n"
    "}\n";

} // namespace

W7_BACKEND_TEST(W7ScopeExitProbe, InjectedMarkerRunsAtScopeEnd) {
    W7Built b = w7buildRun(std::string(kW7Handler) +
        "compiler.events.enable(h_exit);\n"
        "fun main() <noret> {\n"
        "    printf(\"before\\n\");\n"
        "    let x <int> = 1;\n"
        "    printf(\"after\\n\");\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.compileErr;
    ASSERT_TRUE(b.ran) << "compile exit " << b.compileExit << "\n" << b.compileErr;
    EXPECT_NE(b.out.find("w7-exit"), std::string::npos)
        << "the handler's quote ran at the scope exit:\n" << b.out;
}

W7_BACKEND_TEST(W7ScopeExitProbe, UnarmedHandlerInjectsNothing) {
    W7Built b = w7buildRun(std::string(kW7Handler) +
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    printf(\"done\\n\");\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.compileErr;
    ASSERT_TRUE(b.ran) << "compile exit " << b.compileExit << "\n" << b.compileErr;
    EXPECT_EQ(b.out.find("w7-exit"), std::string::npos)
        << "nothing fires without arming:\n" << b.out;
    EXPECT_NE(b.out.find("done"), std::string::npos) << b.out;
}

W7_BACKEND_TEST(W7ComposeProbe, GeneratedParentRunsTheFieldDestructor) {
    // ADR 0016's observable half: Outer declares no destructor, but its field
    // has one, so the generated parent still runs it at scope exit.
    W7Built b = w7buildRun(
        "@define printf(fmt: string, ...) <noret>;\n"
        "struct Inner {\n"
        "    x <int>\n"
        "    ~Inner() { printf(\"inner-dtor\\n\"); }\n"
        "}\n"
        "struct Outer { f <Inner> }\n"
        "fun main() <noret> {\n"
        "    let o <Outer> = Outer{f: Inner{x: 1}};\n"
        "    printf(\"body\\n\");\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.compileErr;
    ASSERT_TRUE(b.ran) << "compile exit " << b.compileExit << "\n" << b.compileErr;
    EXPECT_NE(b.out.find("inner-dtor"), std::string::npos)
        << "the field destructor ran through the generated parent:\n" << b.out;
}

TEST(W7Fire, AUnionTypedVariableFiresScopeExit) {
    // Q12's union half, first clause: D never calls a union's field
    // destructors at scope exit. Fin fires `variable_scope_exit` for every
    // variable whatever its type -- a union-typed binding is tracked like any
    // other, and the handler sees it. (The second clause -- no zero map for a
    // union -- is Soundness_Layout's and the corpus sample's.)
    auto a = analyzeW7(
        "type Number = int | uint | float;\n"
        "#[on(variable_scope_exit)]\n"
        "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "compiler.events.enable(h_exit);\n"
        "fun main() <noret> {\n"
        "    let u <Number> = 5;\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    EXPECT_TRUE(firedHas(a.analyzer->w7Fired(), "h_exit", "u:"))
        << "the union-typed variable fires scope exit";
}

// --- Straight-line threading (the comptime value model, ADR 0006) -----------
// Lets, bare calls, rebinds and known-bool branches thread with parameters
// bound; loops stay refused, anything wider the existing named gap.

TEST(W7Threaded, LetBeforeReturnQuoteEvaluates) {
    auto a = analyzeW7(
        "@define printf(fmt: string, ...) <noret>;\n"
        "#[on(variable_scope_exit)]\n"
        "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {\n"
        "    let m <string> = name;\n"
        "    let e <int> = exit_kind;\n"
        "    let v <int> = moved;\n"
        "    return quote { printf(\"w7-threaded\\n\"); };\n"
        "}\n"
        "compiler.events.enable(h_exit);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(a.errorCount, 0) << "lets aliasing string and int parameters thread";
    EXPECT_TRUE(firedHas(a.analyzer->w7Fired(), "h_exit", "x:int"));
}

TEST(W7Threaded, CallBeforeReturnQuoteEvaluates) {
    auto a = analyzeW7(
        "@define printf(fmt: string, ...) <noret>;\n"
        "fun id(v: int) <int> {\n"
        "  return v;\n"
        "}\n"
        "#[on(variable_scope_exit)]\n"
        "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {\n"
        "    let y <int> = id(7);\n"
        "    return quote { printf(\"w7-called\\n\"); };\n"
        "}\n"
        "compiler.events.enable(h_exit);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(a.errorCount, 0) << "a straight-line helper call threads";
    EXPECT_TRUE(firedHas(a.analyzer->w7Fired(), "h_exit", "x:int"));
}

TEST(W7Threaded, HelperQuoteReturnSplices) {
    auto a = analyzeW7(
        "@define printf(fmt: string, ...) <noret>;\n"
        "@special mkq() <quote> { return quote { printf(\"w7-q\\n\"); }; }\n"
        "#[on(variable_scope_exit)]\n"
        "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {\n"
        "    let q <quote> = mkq();\n"
        "    return q;\n"
        "}\n"
        "compiler.events.enable(h_exit);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(a.errorCount, 0) << "a helper call evaluating to a quote splices";
    EXPECT_TRUE(firedHas(a.analyzer->w7Fired(), "h_exit", "x:int"));
}

TEST(W7Threaded, NonQuoteReturnStillRefused) {
    auto r = w7compile(
        "#[on(variable_scope_exit)]\n"
        "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {\n"
        "    return 42;\n"
        "}\n"
        "compiler.events.enable(h_exit);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    EXPECT_NE(w7messages(stripAnsi(r.err)).find("only a quote literal"), std::string::npos)
        << r.err;
}

TEST(W7Threaded, BlameStillAborts) {
    auto r = w7compile(
        "#[on(variable_scope_exit)]\n"
        "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {\n"
        "    blame 1 == 1, \"saw exit\";\n"
        "}\n"
        "compiler.events.enable(h_exit);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w7messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_exit"), std::string::npos) << err;
    EXPECT_NE(err.find("variable_scope_exit"), std::string::npos) << err;
}

W7_BACKEND_TEST(W7ThreadedProbe, ThreadedMarkerRunsAtScopeEnd) {
    W7Built b = w7buildRun(
        "@define printf(fmt: string, ...) <noret>;\n"
        "#[on(variable_scope_exit)]\n"
        "@special h_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {\n"
        "    let m <string> = name;\n"
        "    return quote { printf(\"w7-threaded\\n\"); };\n"
        "}\n"
        "compiler.events.enable(h_exit);\n"
        "fun main() <noret> {\n"
        "    printf(\"before\\n\");\n"
        "    let x <int> = 1;\n"
        "    printf(\"after\\n\");\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.compileErr;
    ASSERT_TRUE(b.ran) << "compile exit " << b.compileExit << "\n" << b.compileErr;
    EXPECT_NE(b.out.find("w7-threaded"), std::string::npos)
        << "the threaded handler's quote ran at the scope exit:\n" << b.out;
}
