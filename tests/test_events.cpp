#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Corpus.hpp"
#include "Pipeline.hpp"
#include "ast/StructuralWalk.hpp"
#include "ast/decls/Program.hpp"
#include "ast/exprs/FunctionCall.hpp"
#include "diagnostics/DiagnosticEngine.hpp"
#include "semantics/EventRegistry.hpp"
#include "semantics/SemanticAnalyzer.hpp"

// Wave-4 step 15: `#[on(...)]` collection and the three-phase model
// (docs/compiler-api.md §3.4). W2 validated the `#[on(name)]` shape against the
// §3.2 event set; this suite pins collection into a registry, arming through
// top-level `compiler.events.enable`, and the Q10 handler ordering. Firing is
// the next slice and nothing here executes a handler.

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

// A temp .fin file. test_soundness.cpp has its own; duplicated rather than
// shared because these two files have no other reason to be coupled, and the
// copy is nine lines.
class Src {
public:
    explicit Src(const std::string& contents) {
        path_ = uniqueTempPath("fin_events", ".fin");
        std::ofstream f(path_, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    ~Src() { std::error_code ec; fs::remove(path_, ec); }
    std::string str() const { return path_.string(); }

private:
    fs::path path_;
};

// Compiles a string and returns what the machine contract says (ADR 0009):
// 0 accepted, 1 rejected with diagnostics.
FincRun compile(const std::string& code) {
    Src s(code);
    return runFinc({s.str()});
}

// The diagnostic message lines, and nothing else. Searching raw stderr for an
// identifier also matches the echoed source line under the caret, so assertions
// read this and not the raw stream (test_soundness.cpp says why).
std::string messagesOnly(const std::string& stripped) {
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

size_t errorCount(const std::string& stripped) {
    size_t n = 0;
    for (size_t i = 0; i < stripped.size();) {
        size_t eol = stripped.find('\n', i);
        if (eol == std::string::npos) eol = stripped.size();
        if (stripped.compare(i, 7, "error: ") == 0) ++n;
        i = eol + 1;
    }
    return n;
}

struct EventAnalysis {
    bool parsed = false;
    bool analyzerFlag = false;
    bool engineErrors = false;
    int errorCount = 0;
    std::vector<fin::events::HandlerRecord> handlers;
    std::vector<std::string> armed;
    int enableCallsLeft = -1;
};

// Parses and analyses one module, then snapshots the test hook: the collected
// handlers and the armed set. No handler runs here; firing is a later slice.
EventAnalysis analyzeEvents(const std::string& code, const std::string& module = "a.fin") {
    EventAnalysis a;
    auto diag = std::make_unique<fin::DiagnosticEngine>("", "<test>");
    diag->setColorMode(fin::ColorMode::Never);

    auto parsed = parseSource(code, *diag);
    a.parsed = parsed.parsed;
    if (!a.parsed) {
        a.engineErrors = diag->hasErrors();
        a.errorCount = diag->getErrorCount();
        return a;
    }

    fin::SemanticAnalyzer analyzer(*diag, false);
    analyzer.setModulePath(module);
    analyzer.visit(*parsed.ast);

    a.analyzerFlag = analyzer.hasError;
    a.engineErrors = diag->hasErrors();
    a.errorCount = diag->getErrorCount();
    a.handlers = analyzer.eventRegistry().all();
    a.armed = analyzer.eventRegistry().armed();

    struct Counter : public fin::StructuralWalk {
        int n = 0;
        bool enter(fin::ASTNode& node) override {
            if (node.kind() == fin::NodeKind::MethodCall &&
                fin::events::isEnableCall(static_cast<fin::MethodCall&>(node)))
                ++n;
            return true;
        }
    } counter;
    counter.walkAll(parsed.ast->statements);
    a.enableCallsLeft = counter.n;
    return a;
}

const fin::events::HandlerRecord* findHandler(const std::vector<fin::events::HandlerRecord>& hs,
                                             const std::string& name) {
    for (const auto& h : hs)
        if (h.handler == name) return &h;
    return nullptr;
}

} // namespace

// --- Slice 1: Collect -------------------------------------------------------

TEST(EventCollect, HandlersLandInTheRegistryUnderTheirEventKeys) {
    auto a = analyzeEvents(
        "#[on(variable_scope_exit)]\n@special gc_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "#[on(allocation_site)]\n@special gc_alloc(t: $type, count: quote, dest: quote) <quote> {}\n"
        "fun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_FALSE(a.analyzerFlag) << "errors: " << a.errorCount;
    EXPECT_EQ(a.errorCount, 0);
    ASSERT_EQ(a.handlers.size(), 2u);
    const auto* exit = findHandler(a.handlers, "gc_exit");
    const auto* alloc = findHandler(a.handlers, "gc_alloc");
    ASSERT_NE(exit, nullptr);
    ASSERT_NE(alloc, nullptr);
    EXPECT_EQ(exit->event, "variable_scope_exit");
    EXPECT_EQ(alloc->event, "allocation_site");
    EXPECT_EQ(exit->module, "a.fin");
    EXPECT_EQ(alloc->module, "a.fin");
}

TEST(EventCollect, MultipleHandlersShareOneKeyInDeclarationOrder) {
    auto a = analyzeEvents(
        "#[on(variable_scope_exit)]\n@special first(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "#[on(variable_scope_exit)]\n@special second(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "fun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    auto hs = a.handlers;
    ASSERT_EQ(hs.size(), 2u);
    EXPECT_EQ(hs[0].handler, "first");
    EXPECT_EQ(hs[1].handler, "second");
}

TEST(EventCollect, DuplicateIdenticalRegistrationsCoalesce) {
    // One entry no matter how many times the identical registration arrives:
    // a double-fire later would free the same variable twice (ADR 0007).
    fin::events::EventRegistry r;
    fin::events::HandlerRecord rec{"variable_scope_exit", "h", "m.fin", 0, 0, false};
    r.add(rec);
    r.add(rec);
    EXPECT_EQ(r.size(), 1u);
    EXPECT_EQ(r.handlers("variable_scope_exit").size(), 1u);
}

TEST(EventCollect, HandlerOnAPlainFunctionIsRefused) {
    auto r = compile(
        "#[on(variable_scope_exit)]\nfun not_special() <void> {}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = messagesOnly(stripAnsi(r.err));
    EXPECT_NE(err.find("on"), std::string::npos) << err;
    EXPECT_NE(err.find("@special"), std::string::npos)
        << "the refusal must name what a handler has to be:\n" << err;
}

TEST(EventCollect, UnknownEventStaysASingleDiagnostic) {
    // W2 owns the unknown-event refusal; collection must stay silent so the
    // program reports it exactly once.
    auto r = compile("#[on(nosuchevent)]\n@special h() <void> {}\nfun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = messagesOnly(stripAnsi(r.err));
    EXPECT_NE(err.find("nosuchevent"), std::string::npos) << err;
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u) << r.err;
}

TEST(EventCollect, MalformedOnStaysASingleDiagnostic) {
    auto r = compile("#[on]\n@special h() <void> {}\nfun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u) << r.err;
}

// --- Slice 2: Arm -----------------------------------------------------------

TEST(EventArm, TopLevelEnableArmsTheHandler) {
    auto a = analyzeEvents(
        "#[on(variable_scope_exit)]\n@special gc_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "compiler.events.enable(gc_exit);\n"
        "fun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_FALSE(a.analyzerFlag) << "errors: " << a.errorCount;
    EXPECT_EQ(a.errorCount, 0);
    ASSERT_EQ(a.armed.size(), 1u);
    EXPECT_EQ(a.armed[0], "gc_exit");

    auto r = compile(
        "#[on(variable_scope_exit)]\n@special gc_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "compiler.events.enable(gc_exit);\n"
        "fun main() <noret> {}\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(EventArm, EnableInsideAFunctionIsRefused) {
    auto r = compile(
        "#[use(compiler)]\n"
        "#[use(compiler.components.events)]\n"
        "#[on(variable_scope_exit)]\n@special gc_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "#[use(compiler)]\n"
        "#[use(compiler.components.events)]\n"
        "fun main() <noret> {\n"
        "    compiler.events.enable(gc_exit);\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = messagesOnly(stripAnsi(r.err));
    EXPECT_NE(err.find("top-level"), std::string::npos)
        << "the refusal must name the arm-phase rule:\n" << err;
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u) << r.err;
}

TEST(EventArm, EnableOfAnUnknownHandlerIsRefused) {
    auto r = compile(
        "compiler.events.enable(nosuchhandler);\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = messagesOnly(stripAnsi(r.err));
    EXPECT_NE(err.find("nosuchhandler"), std::string::npos) << err;
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u) << r.err;
}

TEST(EventArm, EnableOfAPlainFunctionIsRefused) {
    auto r = compile(
        "fun plain() <void> {}\n"
        "compiler.events.enable(plain);\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = messagesOnly(stripAnsi(r.err));
    EXPECT_NE(err.find("plain"), std::string::npos) << err;
}

TEST(EventArm, EnableWithAStringArgumentIsRefused) {
    // The handler is a @special reference, not its name. A string would arm
    // nothing the compiler can check, so it is refused rather than accepted.
    auto r = compile(
        "#[on(variable_scope_exit)]\n@special gc_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "compiler.events.enable(\"gc_exit\");\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = messagesOnly(stripAnsi(r.err));
    EXPECT_NE(err.find("enable"), std::string::npos) << err;
}

TEST(EventArm, DuplicateEnableArmsOnce) {
    auto a = analyzeEvents(
        "#[on(variable_scope_exit)]\n@special gc_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "compiler.events.enable(gc_exit);\n"
        "compiler.events.enable(gc_exit);\n"
        "fun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    ASSERT_EQ(a.armed.size(), 1u);
    EXPECT_EQ(a.armed[0], "gc_exit");
}

TEST(EventArm, ConsumedEnableLeavesTheTree) {
    // Arming is a compile-time effect: the statement must not survive into the
    // backend, which has no lowering for it.
    auto a = analyzeEvents(
        "#[on(variable_scope_exit)]\n@special gc_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> {}\n"
        "compiler.events.enable(gc_exit);\n"
        "fun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(a.errorCount, 0);
    EXPECT_EQ(a.enableCallsLeft, 0);
}

TEST(EventArm, GrantsForTheEventsComponentValidate) {
    // `events` is a component like the other four: its grant validates and its
    // reference answers. Only `enable` exists on it yet; the rest of §2.5 is
    // the firing slice's.
    auto a = analyzeEvents(
        "#[use(compiler)]\n"
        "#[use(compiler.components.events)]\n"
        "@special h() <void> {}\n"
        "fun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
}

// --- Slice 3: Q10 ordering --------------------------------------------------

TEST(EventOrder, OrdersHandlersAcrossAnImportDag) {
    // Two modules importing a third. The root imports zebra first, so import
    // order and module-path order disagree on purpose: the path tie-break must
    // win. Dependencies come before their importers, the root last.
    fin::events::EventRegistry r;
    r.recordImport("root.fin", "zebra.fin");
    r.recordImport("root.fin", "apple.fin");
    r.recordImport("zebra.fin", "shared.fin");
    r.recordImport("apple.fin", "shared.fin");
    r.add({"variable_scope_exit", "h_root", "root.fin", 0, 0, false});
    r.add({"variable_scope_exit", "h_zebra", "zebra.fin", 0, 0, false});
    r.add({"variable_scope_exit", "h_apple", "apple.fin", 0, 0, false});
    r.add({"variable_scope_exit", "h_shared", "shared.fin", 0, 0, false});

    auto ordered = r.orderedHandlers("variable_scope_exit");
    ASSERT_EQ(ordered.size(), 4u);
    EXPECT_EQ(ordered[0].handler, "h_shared");
    EXPECT_EQ(ordered[1].handler, "h_apple");
    EXPECT_EQ(ordered[2].handler, "h_zebra");
    EXPECT_EQ(ordered[3].handler, "h_root");
}

TEST(EventOrder, KeepsDeclarationOrderWithinAModule) {
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

TEST(EventOrder, UnknownEventOrdersToNothing) {
    fin::events::EventRegistry r;
    r.add({"variable_scope_exit", "h", "m.fin", 0, 0, false});
    EXPECT_TRUE(r.orderedHandlers("allocation_site").empty());
    EXPECT_TRUE(r.handlers("allocation_site").empty());
}
