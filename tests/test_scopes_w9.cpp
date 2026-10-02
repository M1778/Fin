#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Corpus.hpp"
#include "semantics/LivePointers.hpp"
#include "semantics/MovedAnalysis.hpp"
#include "types/Layout.hpp"
#include "types/PointerType.hpp"
#include "types/PrimitiveType.hpp"
#include "types/StructType.hpp"

// Wave-4 step 18 (docs/compiler-api.md §3.2, §2.5), W9:
// `compiler.scopes.live_pointers_quote` — the projection that makes a shadow
// stack expressible without loops: at a point, the set of live pointers as a
// quote.
//
// Prior batches landed and are consumed here, not restated: collection/arming
// (test_events.cpp), the firing loop with struct_layout_finalised and
// variable_declared (test_events_w5.cpp), the W6 floor of
// function_entry/exit, assignment, allocation_site/delete_site
// (test_events_w6.cpp), moved analysis with MovedMaybe plus attribution
// (test_events_w7.cpp), and the typed pointer maps (test_layout.cpp).
// W10 (parallel) owns loop_back_edge: nothing here names that event.
//
// C++ first (owner decision): the live set and its quote projection live in
// src/semantics/LivePointers.* as analysis. No handler execution, no codegen
// roots: a handler spelling `compiler.scopes.live_pointers_quote()` resolves
// through the scopes component table, and calls evaluate only when the
// comptime interpreter lands (the same gap W5's firing reports for every
// quote-returning call).

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

class W9Src {
public:
    explicit W9Src(const std::string& contents) {
        path_ = uniqueTempPath("fin_scopes_w9", ".fin");
        std::ofstream f(path_, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    ~W9Src() { std::error_code ec; fs::remove(path_, ec); }
    std::string str() const { return path_.string(); }

private:
    fs::path path_;
};

FincRun w9compile(const std::string& code) {
    W9Src s(code);
    return runFinc({s.str()});
}

std::string w9messages(const std::string& stripped) {
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

fin::TypePtr w9prim(const std::string& n) { return std::make_shared<fin::PrimitiveType>(n); }

fin::TypePtr w9ptr(const std::string& to) {
    return std::make_shared<fin::PointerType>(w9prim(to));
}

fin::scopes::LiveBinding w9bind(const std::string& name, fin::TypePtr type,
                                fin::events::MovedState moved = fin::events::MovedState::Live) {
    return fin::scopes::LiveBinding{name, std::move(type), moved};
}

std::vector<std::string> w9names(const std::vector<fin::scopes::LiveBinding>& live) {
    std::vector<std::string> out;
    for (const auto& b : live) out.push_back(b.name);
    return out;
}

// The moved state of `name` in a branch snapshot: what a straight-line,
// branch-joined, or loop-joined walk leaves behind, without re-deriving the
// analysis in this suite.
fin::events::MovedState w9stateOf(const fin::events::MovedAnalysis::Snapshot& snap,
                                  const std::string& name) {
    for (const auto& frame : snap.frames)
        for (const auto& var : frame.vars)
            if (var.first == name) return var.second;
    return fin::events::MovedState::Live;
}

}  // namespace

// --- Slice 1: the live set at a point ----------------------------------------
// Pointer-containing bindings live at the point; everything else is not a
// root. Pointer containment is W4's answer (LayoutEngine::pointerCount), not
// a second walk here.

TEST(W9LiveSet, StraightLineKeepsOnlyLivePointers) {
    fin::LayoutEngine engine;
    const std::vector<fin::scopes::LiveBinding> at = {
        w9bind("p", w9ptr("int")),
        w9bind("x", w9prim("int")),
        w9bind("s", w9prim("string")),
    };
    EXPECT_EQ(w9names(fin::scopes::livePointers(at, engine)),
              (std::vector<std::string>{"p"}));
}

TEST(W9LiveSet, StructHoldingAPointerIsARoot) {
    fin::LayoutEngine engine;
    auto node = std::make_shared<fin::StructType>("Node");
    node->defineField("next", w9ptr("Node"), true);
    auto plain = std::make_shared<fin::StructType>("Plain");
    plain->defineField("x", w9prim("int"), true);
    const std::vector<fin::scopes::LiveBinding> at = {
        w9bind("n", node),
        w9bind("v", plain),
    };
    EXPECT_EQ(w9names(fin::scopes::livePointers(at, engine)),
              (std::vector<std::string>{"n"}));
}

TEST(W9LiveSet, BranchJoinKeepsBindingsLiveOnEitherSide) {
    // `let q <&int>` on one branch only: the join keeps the side's state,
    // and the live set answers from the joined state, not from the
    // declaration site.
    fin::events::MovedAnalysis moved;
    moved.enterFunction();
    moved.enterBlock();
    moved.declare("p", "&int");
    auto snap = moved.snapshot();
    moved.declare("q", "&int");
    const auto thenEnd = moved.snapshot();
    moved.installJoin(thenEnd, snap);
    const auto joined = moved.snapshot();
    fin::LayoutEngine engine;
    const std::vector<fin::scopes::LiveBinding> at = {
        w9bind("p", w9ptr("int"), w9stateOf(joined, "p")),
        w9bind("q", w9ptr("int"), w9stateOf(joined, "q")),
    };
    EXPECT_EQ(w9names(fin::scopes::livePointers(at, engine)),
              (std::vector<std::string>{"p", "q"}));
}

TEST(W9LiveSet, RefusedLayoutsAreNotRoots) {
    // A refused map is not a root (ADR 0034: `any` stays refused; an
    // interface has no decided representation). Guessing "pointer" here is
    // D's all-zero-bitmap defect in a new spelling.
    fin::LayoutEngine engine;
    const std::vector<fin::scopes::LiveBinding> at = {
        w9bind("a", w9prim("any")),
        w9bind("p", w9ptr("int")),
    };
    EXPECT_EQ(w9names(fin::scopes::livePointers(at, engine)),
              (std::vector<std::string>{"p"}));
}

TEST(W9LiveSet, FinProbeStraightLineProgramResolvesTheProjection) {
    // End to end at the analysis seam: a straight-line program spelling the
    // projection under the scopes grant compiles clean.
    auto r = w9compile(
        "#[use(compiler)]\n"
        "#[use(compiler.components.scopes)]\n"
        "@special probe() <quote> {\n"
        "    return compiler.scopes.live_pointers_quote();\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let p <&int> = null;\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

// --- Slice 2: the quote projection -------------------------------------------
// The compiler does the walking so the handler needs no loop (Q4): the whole
// set arrives as literal syntax. Empty when none.

TEST(W9Quote, EmptyWhenNoLivePointers) {
    fin::LayoutEngine engine;
    const fin::scopes::LivePoint point{"variable_declared", "p:&int", 3};
    const std::vector<fin::scopes::LiveBinding> at = {w9bind("x", w9prim("int"))};
    const auto answer = fin::scopes::livePointersQuote(point, at, engine);
    EXPECT_TRUE(answer.names.empty());
    EXPECT_EQ(answer.quote, "quote {}");
    EXPECT_EQ(answer.point.event, "variable_declared");
}

TEST(W9Quote, ProjectsAddressesInOrder) {
    fin::LayoutEngine engine;
    const fin::scopes::LivePoint point{"function_exit", "return", 7};
    const std::vector<fin::scopes::LiveBinding> at = {
        w9bind("p", w9ptr("int")),
        w9bind("x", w9prim("int")),
        w9bind("q", w9ptr("int")),
    };
    const auto answer = fin::scopes::livePointersQuote(point, at, engine);
    EXPECT_EQ(answer.names, (std::vector<std::string>{"p", "q"}));
    EXPECT_EQ(answer.quote, "quote { [&p, &q]; }");
    // The point identity travels with the answer: attribution (step 19)
    // names the handler, the event and the point, never a bare quote.
    EXPECT_EQ(answer.point.event, "function_exit");
    EXPECT_EQ(answer.point.detail, "return");
    EXPECT_EQ(answer.point.line, 7);
}

TEST(W9Quote, ScopesComponentAnswersPresent) {
    auto r = w9compile(
        "#[use(compiler)]\n"
        "@special probe() <bool> {\n"
        "    return compiler.components.scopes.present();\n"
        "}\n"
        "fun main() <noret> {}\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(W9Quote, ProjectionNeedsNoArguments) {
    auto r = w9compile(
        "#[use(compiler)]\n"
        "#[use(compiler.components.scopes)]\n"
        "@special probe() <quote> {\n"
        "    return compiler.scopes.live_pointers_quote(1);\n"
        "}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(W9Quote, ProjectionNeedsTheScopesGrant) {
    auto r = w9compile(
        "#[use(compiler)]\n"
        "@special probe() <quote> {\n"
        "    return compiler.scopes.live_pointers_quote();\n"
        "}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << stripAnsi(r.err);
    EXPECT_NE(w9messages(stripAnsi(r.err)).find("scopes"), std::string::npos) << r.err;
}

TEST(W9Quote, ArmedHandlerHittingTheProjectionReportsTheInterpreterGap) {
    // Consumes the firing interface: a quote-returning component call is
    // understood but not runnable yet, so firing reports the gap by name
    // rather than splicing a guessed quote or skipping in silence.
    auto r = w9compile(
        "#[on(variable_declared)]\n"
        "#[use(compiler)]\n"
        "#[use(compiler.components.scopes)]\n"
        "@special h_roots(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    return compiler.scopes.live_pointers_quote();\n"
        "}\n"
        "compiler.events.enable(h_roots);\n"
        "fun main() <noret> {\n"
        "    let p <&int> = null;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << stripAnsi(r.err);
    const std::string err = w9messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_roots"), std::string::npos) << err;
    EXPECT_NE(err.find("do not run"), std::string::npos) << err;
}

TEST(W9Quote, AHandlerThatWouldWalkIsRefused) {
    // The interpretability line HELD: the quote IS the loop-free projection,
    // so a handler looping over it is refused by name, whatever it calls.
    auto r = w9compile(
        "#[on(variable_declared)]\n"
        "#[use(compiler)]\n"
        "#[use(compiler.components.scopes)]\n"
        "@special h_roots(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    while (1 == 1) {}\n"
        "}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << stripAnsi(r.err);
    const std::string err = w9messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_roots"), std::string::npos) << err;
    EXPECT_NE(err.find("control flow"), std::string::npos) << err;
}

// --- Slice 3: moved-state interplay ------------------------------------------
// Moved-from is not live. MovedMaybe is included, conservatively: excluding
// a maybe-moved root risks freeing a live object (unsafe); including a dead
// one only pins memory (safe direction for a collector). Documented in
// LivePointers.hpp, pinned here.

TEST(W9Moved, MovedFromIsNotLive) {
    fin::LayoutEngine engine;
    const std::vector<fin::scopes::LiveBinding> at = {
        w9bind("p", w9ptr("int"), fin::events::MovedState::Moved),
        w9bind("q", w9ptr("int"), fin::events::MovedState::Live),
    };
    EXPECT_EQ(w9names(fin::scopes::livePointers(at, engine)),
              (std::vector<std::string>{"q"}));
    const fin::scopes::LivePoint point{"variable_scope_exit", "p:&int", 5};
    const auto answer = fin::scopes::livePointersQuote(point, at, engine);
    EXPECT_EQ(answer.quote, "quote { [&q]; }");
}

TEST(W9Moved, MaybeIsIncludedConservatively) {
    fin::LayoutEngine engine;
    const std::vector<fin::scopes::LiveBinding> at = {
        w9bind("p", w9ptr("int"), fin::events::MovedState::Maybe),
    };
    EXPECT_EQ(w9names(fin::scopes::livePointers(at, engine)),
              (std::vector<std::string>{"p"}));
}

TEST(W9Moved, ConditionalMoveLeavesAMaybeRoot) {
    // Through the real analysis: moved on one branch only joins to Maybe,
    // and the live set keeps the root.
    fin::events::MovedAnalysis moved;
    moved.enterFunction();
    moved.enterBlock();
    moved.declare("p", "&int");
    auto snap = moved.snapshot();
    moved.move("p");
    const auto thenEnd = moved.snapshot();
    moved.restore(snap);
    moved.installJoin(thenEnd, moved.snapshot());
    const auto joined = moved.snapshot();
    ASSERT_EQ(w9stateOf(joined, "p"), fin::events::MovedState::Maybe);
    fin::LayoutEngine engine;
    const std::vector<fin::scopes::LiveBinding> at = {
        w9bind("p", w9ptr("int"), w9stateOf(joined, "p")),
    };
    EXPECT_EQ(w9names(fin::scopes::livePointers(at, engine)),
              (std::vector<std::string>{"p"}));
}

TEST(W9Moved, UnconditionalMoveLeavesNoRoot) {
    // Through the real analysis: moved on every path is Moved, and the
    // scope-exit payload agrees (moved=moved) while the live set drops it.
    fin::events::MovedAnalysis moved;
    moved.enterFunction();
    moved.enterBlock();
    moved.declare("p", "&int");
    moved.move("p");
    const auto end = moved.snapshot();
    ASSERT_EQ(w9stateOf(end, "p"), fin::events::MovedState::Moved);
    fin::LayoutEngine engine;
    const std::vector<fin::scopes::LiveBinding> at = {
        w9bind("p", w9ptr("int"), w9stateOf(end, "p")),
    };
    EXPECT_TRUE(fin::scopes::livePointers(at, engine).empty());
    const fin::scopes::LivePoint point{"variable_scope_exit", "p:&int", 5};
    EXPECT_EQ(fin::scopes::livePointersQuote(point, at, engine).quote, "quote {}");
}

TEST(W9Moved, FinProbeMoveDiagnosticsAreUndisturbed) {
    // Non-interference: consulting moved state for roots changes nothing
    // about what the moved analysis reports.
    auto r = w9compile(
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let y <int> = @move(x);\n"
        "    let z <int> = x;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << stripAnsi(r.err);
    const std::string err = w9messages(stripAnsi(r.err));
    EXPECT_NE(err.find("moved"), std::string::npos) << err;
}
