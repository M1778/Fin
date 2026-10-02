#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "Corpus.hpp"
#include "Pipeline.hpp"
#include "ast/decls/Program.hpp"
#include "diagnostics/DiagnosticEngine.hpp"
#include "semantics/EventPayloads.hpp"
#include "semantics/EventRegistry.hpp"
#include "semantics/SemanticAnalyzer.hpp"

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

// A temp .fin file, and the compile result. Mirrors test_events.cpp's Src:
// duplicated rather than shared because the two files have no other reason
// to be coupled.
class W6Src {
public:
    explicit W6Src(const std::string& contents) {
        path_ = uniqueTempPath("fin_events_w6", ".fin");
        std::ofstream f(path_, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    ~W6Src() { std::error_code ec; fs::remove(path_, ec); }
    std::string str() const { return path_.string(); }

private:
    fs::path path_;
};

FincRun w6compile(const std::string& code) {
    W6Src s(code);
    return runFinc({s.str()});
}

// Diagnostic message lines only: searching raw stderr also matches the echoed
// source under the caret (test_events.cpp says why).
std::string w6messages(const std::string& stripped) {
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

} // namespace

// Wave-4 step 17 (docs/compiler-api.md §3.1-§3.2), W6 floor:
// function_entry/function_exit, assignment, allocation_site/delete_site.
// The handler's signature IS the event's payload: one row per event is the
// parameter list every handler for it must declare, and the quote it must
// return. W5's match check answers from these rows.

TEST(W6Payload, FunctionEntryIsFunctionIdentity) {
    const auto* p = fin::events::findW6Payload("function_entry");
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(p->params.size(), 1u);
    EXPECT_EQ(p->params[0].name, "f");
    EXPECT_EQ(p->params[0].type, "function");
    EXPECT_EQ(p->returns, "quote");
}

TEST(W6Payload, FunctionExitCarriesExitKind) {
    const auto* p = fin::events::findW6Payload("function_exit");
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(p->params.size(), 2u);
    EXPECT_EQ(p->params[0].name, "f");
    EXPECT_EQ(p->params[0].type, "function");
    EXPECT_EQ(p->params[1].name, "exit_kind");
    EXPECT_EQ(p->params[1].type, "int");
    EXPECT_EQ(p->returns, "quote");
}

TEST(W6Payload, AssignmentCarriesTargetValueAndType) {
    const auto* p = fin::events::findW6Payload("assignment");
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(p->params.size(), 3u);
    EXPECT_EQ(p->params[0].name, "target");
    EXPECT_EQ(p->params[0].type, "quote");
    EXPECT_EQ(p->params[1].name, "value");
    EXPECT_EQ(p->params[1].type, "quote");
    EXPECT_EQ(p->params[2].name, "t");
    EXPECT_EQ(p->params[2].type, "$type");
    EXPECT_EQ(p->returns, "quote");
}

TEST(W6Payload, AllocationSiteCarriesTypeCountAndDest) {
    const auto* p = fin::events::findW6Payload("allocation_site");
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(p->params.size(), 3u);
    EXPECT_EQ(p->params[0].name, "t");
    EXPECT_EQ(p->params[0].type, "$type");
    EXPECT_EQ(p->params[1].name, "count");
    EXPECT_EQ(p->params[1].type, "quote");
    EXPECT_EQ(p->params[2].name, "dest");
    EXPECT_EQ(p->params[2].type, "quote");
    EXPECT_EQ(p->returns, "quote");
}

TEST(W6Payload, DeleteSiteCarriesTypeAndPointer) {
    const auto* p = fin::events::findW6Payload("delete_site");
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(p->params.size(), 2u);
    EXPECT_EQ(p->params[0].name, "t");
    EXPECT_EQ(p->params[0].type, "$type");
    EXPECT_EQ(p->params[1].name, "ptr");
    EXPECT_EQ(p->params[1].type, "quote");
    EXPECT_EQ(p->returns, "quote");
}

TEST(W6Payload, W5EventsHaveNoW6Payload) {
    // variable_declared, variable_scope_exit and struct_layout_finalised are
    // W5's: this table must not answer for them, or two match checks would
    // own one event and diagnose it twice.
    EXPECT_EQ(fin::events::findW6Payload("variable_declared"), nullptr);
    EXPECT_EQ(fin::events::findW6Payload("variable_scope_exit"), nullptr);
    EXPECT_EQ(fin::events::findW6Payload("struct_layout_finalised"), nullptr);
    EXPECT_EQ(fin::events::findW6Payload("struct_layout_deciding"), nullptr);
    EXPECT_EQ(fin::events::findW6Payload("loop_back_edge"), nullptr);
    EXPECT_FALSE(fin::events::isW6Event("variable_scope_exit"));
    EXPECT_TRUE(fin::events::isW6Event("assignment"));
}

TEST(W6Payload, UnknownEventHasNoPayload) {
    EXPECT_EQ(fin::events::findW6Payload("nosuchevent"), nullptr);
    EXPECT_FALSE(fin::events::isW6Event("nosuchevent"));
}

TEST(W6Payload, ExpectedSignatureNamesTheWholeShape) {
    const auto* p = fin::events::findW6Payload("function_exit");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(fin::events::expectedSignature(*p), "(f: function, exit_kind: int) <quote>");
}

// --- Slice 2: the payloads are spellable -----------------------------------
//
// A handler must be able to *write* its event's payload. `$type`, `quote`,
// `string` and `int` resolve today; `function` does not, so the two function
// events' handlers are unspellable until it registers beside them.

TEST(W6Probe, FunctionEntryHandlerSpellsItsPayload) {
    auto r = w6compile(
        "#[on(function_entry)]\n@special h_entry(f: function) <quote> {}\n"
        "compiler.events.enable(h_entry);\n"
        "fun main() <noret> {}\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(W6Probe, FunctionExitHandlerSpellsItsPayload) {
    auto r = w6compile(
        "#[on(function_exit)]\n@special h_exit(f: function, exit_kind: int) <quote> {}\n"
        "compiler.events.enable(h_exit);\n"
        "fun main() <noret> {}\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(W6Probe, AssignmentHandlerSpellsItsPayload) {
    auto r = w6compile(
        "#[on(assignment)]\n@special h_asg(target: quote, value: quote, t: $type) <quote> {}\n"
        "compiler.events.enable(h_asg);\n"
        "fun main() <noret> {}\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(W6Probe, AllocationSiteHandlerSpellsItsPayload) {
    auto r = w6compile(
        "#[on(allocation_site)]\n@special h_alloc(t: $type, count: quote, dest: quote) <quote> {}\n"
        "compiler.events.enable(h_alloc);\n"
        "fun main() <noret> {}\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(W6Probe, DeleteSiteHandlerSpellsItsPayload) {
    auto r = w6compile(
        "#[on(delete_site)]\n@special h_del(t: $type, ptr: quote) <quote> {}\n"
        "compiler.events.enable(h_del);\n"
        "fun main() <noret> {}\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

// --- Slice 3: the signature IS the payload ---------------------------------
//
// docs/compiler-api.md §3.1: a handler for an event must have exactly that
// event's parameter list and return quote. A mismatch is caught before
// anything runs, naming the handler, the event, and both shapes. The check
// is W6's events only: W5's match check owns W5's events, and two checks
// owning one event would diagnose it twice.

TEST(W6Mismatch, ExtraParameterIsRefused) {
    auto r = w6compile(
        "#[on(function_entry)]\n@special h_entry(f: function, extra: int) <quote> {}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w6messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_entry"), std::string::npos) << err;
    EXPECT_NE(err.find("function_entry"), std::string::npos) << err;
    EXPECT_NE(err.find("(f: function) <quote>"), std::string::npos)
        << "the refusal must name the expected payload:\n" << err;
}

TEST(W6Mismatch, WrongParameterTypeIsRefused) {
    auto r = w6compile(
        "#[on(function_entry)]\n@special h_entry(f: string) <quote> {}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w6messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_entry"), std::string::npos) << err;
    EXPECT_NE(err.find("(f: function) <quote>"), std::string::npos) << err;
}

TEST(W6Mismatch, WrongReturnTypeIsRefused) {
    auto r = w6compile(
        "#[on(function_entry)]\n@special h_entry(f: function) <void> {}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w6messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_entry"), std::string::npos) << err;
    EXPECT_NE(err.find("(f: function) <quote>"), std::string::npos) << err;
}

TEST(W6Mismatch, MissingParameterIsRefused) {
    auto r = w6compile(
        "#[on(assignment)]\n@special h_asg(target: quote, value: quote) <quote> {}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w6messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_asg"), std::string::npos) << err;
    EXPECT_NE(err.find("assignment"), std::string::npos) << err;
    EXPECT_NE(err.find("(target: quote, value: quote, t: $type) <quote>"), std::string::npos)
        << err;
}

TEST(W6Mismatch, AllocationSitePayloadIsChecked) {
    auto r = w6compile(
        "#[on(allocation_site)]\n@special h_alloc(t: $type, count: quote) <quote> {}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w6messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_alloc"), std::string::npos) << err;
    EXPECT_NE(err.find("(t: $type, count: quote, dest: quote) <quote>"), std::string::npos)
        << err;
}

TEST(W6Mismatch, DeleteSitePayloadIsChecked) {
    auto r = w6compile(
        "#[on(delete_site)]\n@special h_del(ptr: quote) <quote> {}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w6messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_del"), std::string::npos) << err;
    EXPECT_NE(err.find("(t: $type, ptr: quote) <quote>"), std::string::npos) << err;
}

// --- Slice 3b: the interpretability line is held ---------------------------
//
// No control flow in handlers (owner decision). An over-wide handler is
// refused by name: the diagnostic names the handler and the statement form,
// so the library knows what to remove rather than what rule to look up.

TEST(W6Line, IfInAHandlerIsRefusedByName) {
    auto r = w6compile(
        "#[on(assignment)]\n"
        "@special h_asg(target: quote, value: quote, t: $type) <quote> {\n"
        "    if (1 == 1) {}\n"
        "}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w6messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_asg"), std::string::npos) << err;
    EXPECT_NE(err.find("control flow"), std::string::npos) << err;
}

TEST(W6Line, LoopInAHandlerIsRefusedByName) {
    auto r = w6compile(
        "#[on(allocation_site)]\n"
        "@special h_alloc(t: $type, count: quote, dest: quote) <quote> {\n"
        "    while (1 == 1) {}\n"
        "}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w6messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_alloc"), std::string::npos) << err;
    EXPECT_NE(err.find("control flow"), std::string::npos) << err;
}

// --- Slice 4: fire points --------------------------------------------------
//
// Where §3.2's five points are, recorded during analysis with no diagnostic
// of their own: a program that fires them compiles exactly as before, and
// W5's firing pass reads the points from here rather than re-deriving them.
// Site identity is the enclosing function plus the line; detail is the
// payload's human half (target/type spelling, exit path).

namespace {

struct W6Analysis {
    bool parsed = false;
    int errorCount = 0;
    std::vector<fin::events::FirePoint> points;
};

W6Analysis analyzeW6(const std::string& code) {
    W6Analysis a;
    auto diag = std::make_unique<fin::DiagnosticEngine>("", "<test>");
    diag->setColorMode(fin::ColorMode::Never);
    auto parsed = parseSource(code, *diag);
    a.parsed = parsed.parsed;
    if (!a.parsed) {
        a.errorCount = diag->getErrorCount();
        return a;
    }
    fin::SemanticAnalyzer analyzer(*diag, false);
    analyzer.setModulePath("a.fin");
    analyzer.visit(*parsed.ast);
    a.errorCount = diag->getErrorCount();
    a.points = analyzer.firePoints();
    return a;
}

bool hasPoint(const std::vector<fin::events::FirePoint>& ps, const std::string& event,
              const std::string& site, const std::string& detail = "") {
    for (const auto& p : ps)
        if (p.event == event && p.site == site && (detail.empty() || p.detail == detail))
            return true;
    return false;
}

} // namespace

TEST(W6Fire, FunctionEntryFiresAfterParamsAreBound) {
    auto a = analyzeW6("fun f() <void> {}\nfun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(a.errorCount, 0);
    EXPECT_TRUE(hasPoint(a.points, "function_entry", "f")) << "entry for f";
    EXPECT_TRUE(hasPoint(a.points, "function_entry", "main")) << "entry for main";
}

TEST(W6Fire, ReturnIsAFunctionExit) {
    auto a = analyzeW6("fun f() <int> { return 1; }\nfun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(a.errorCount, 0);
    EXPECT_TRUE(hasPoint(a.points, "function_exit", "f", "return"));
}

TEST(W6Fire, FallthroughIsAFunctionExit) {
    auto a = analyzeW6("fun g() <void> {}\nfun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(a.errorCount, 0);
    EXPECT_TRUE(hasPoint(a.points, "function_exit", "g", "fallthrough"));
    EXPECT_TRUE(hasPoint(a.points, "function_exit", "main", "fallthrough"));
}

TEST(W6Fire, PlainAssignmentFiresWithTargetIdentity) {
    auto a = analyzeW6(
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    x = 2;\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(a.errorCount, 0);
    ASSERT_TRUE(hasPoint(a.points, "assignment", "main")) << "an assignment in main";
    bool namesTarget = false;
    for (const auto& p : a.points)
        if (p.event == "assignment" && p.detail.find("x") != std::string::npos)
            namesTarget = true;
    EXPECT_TRUE(namesTarget) << "the point carries the target identity";
}

TEST(W6Fire, NewAndDeleteFireWithTypeIdentity) {
    auto a = analyzeW6(
        "fun main() <noret> {\n"
        "    let p <&int>;\n"
        "    p = new int*;\n"
        "    delete p;\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    EXPECT_TRUE(hasPoint(a.points, "allocation_site", "main")) << "the new fires";
    EXPECT_TRUE(hasPoint(a.points, "delete_site", "main")) << "the delete fires";
    bool namesType = false;
    for (const auto& p : a.points)
        if (p.event == "allocation_site" && p.detail.find("int") != std::string::npos)
            namesType = true;
    EXPECT_TRUE(namesType) << "the alloc point carries the allocated type";
}

// --- Slice 4b: ordering with co-registered handlers ------------------------
//
// W5's firing loop is orderedHandlers (Q10, ADR 0007 as corrected) gated on
// the armed set. These pin that contract for W6's event names: two handlers
// on one event order by module DAG then declaration, and only armed ones run.

TEST(W6Order, CoRegisteredHandlersOrderByModuleDagThenDeclaration) {
    fin::events::EventRegistry r;
    r.recordImport("root.fin", "lib.fin");
    r.add({"assignment", "second", "lib.fin", 1, 0, false});
    r.add({"assignment", "first", "lib.fin", 0, 0, false});
    r.add({"assignment", "root_h", "root.fin", 0, 0, false});
    auto ordered = r.orderedHandlers("assignment");
    ASSERT_EQ(ordered.size(), 3u);
    EXPECT_EQ(ordered[0].handler, "first");
    EXPECT_EQ(ordered[1].handler, "second");
    EXPECT_EQ(ordered[2].handler, "root_h");
}

TEST(W6Order, OnlyArmedHandlersRun) {
    fin::events::EventRegistry r;
    r.add({"delete_site", "armed_h", "a.fin", 0, 0, false});
    r.add({"delete_site", "quiet_h", "a.fin", 1, 0, false});
    r.arm("armed_h");
    auto ordered = r.orderedHandlers("delete_site");
    ASSERT_EQ(ordered.size(), 2u);
    std::vector<std::string> firing;
    for (const auto& h : ordered)
        if (r.isArmed(h.handler)) firing.push_back(h.handler);
    ASSERT_EQ(firing.size(), 1u);
    EXPECT_EQ(firing[0], "armed_h");
}
