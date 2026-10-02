#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Corpus.hpp"
#include "Pipeline.hpp"
#include "ast/StructuralWalk.hpp"
#include "ast/decls/FunctionDecl.hpp"
#include "ast/decls/Program.hpp"
#include "ast/decls/TypeDef.hpp"
#include "ast/stmts/VariableDecl.hpp"
#include "diagnostics/DiagnosticEngine.hpp"
#include "semantics/EventFiring.hpp"
#include "semantics/EventRegistry.hpp"
#include "semantics/SemanticAnalyzer.hpp"

// Wave-4 step 17 (docs/compiler-api.md §3.2, §3.8), W5 floor: the firing loop
// plus `struct_layout_finalised` and `variable_declared`.
//
// W6 (parallel) owns function_entry/exit, assignment, allocation_site/
// delete_site: nothing here names those events except to pin the disjoint
// ownership. variable_scope_exit + MovedMaybe is a later batch.

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

// A temp .fin file. Duplicated from test_events.cpp rather than shared: the
// two files have no other reason to be coupled.
class W5Src {
public:
    explicit W5Src(const std::string& contents) {
        path_ = uniqueTempPath("fin_events_w5", ".fin");
        std::ofstream f(path_, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    ~W5Src() { std::error_code ec; fs::remove(path_, ec); }
    std::string str() const { return path_.string(); }

private:
    fs::path path_;
};

FincRun w5compile(const std::string& code) {
    W5Src s(code);
    return runFinc({s.str()});
}

// Diagnostic message lines only: searching raw stderr also matches the echoed
// source under the caret (test_events.cpp says why).
std::string w5messages(const std::string& stripped) {
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

size_t w5errorCount(const std::string& stripped) {
    size_t n = 0;
    for (size_t i = 0; i < stripped.size();) {
        size_t eol = stripped.find('\n', i);
        if (eol == std::string::npos) eol = stripped.size();
        if (stripped.compare(i, 7, "error: ") == 0) ++n;
        i = eol + 1;
    }
    return n;
}

struct W5Analysis {
    bool parsed = false;
    int errorCount = 0;
    std::unique_ptr<fin::Program> ast;
    std::vector<fin::events::W5FirePoint> points;
    std::vector<fin::events::FiredHandler> fired;
};

// Parses, analyses (which fires), and snapshots the points and the fired log.
W5Analysis analyzeW5(const std::string& code) {
    W5Analysis a;
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
    a.points = analyzer.w5FirePoints();
    a.fired = analyzer.w5Fired();
    a.ast = std::move(parsed.ast);
    return a;
}

bool firedInOrder(const std::vector<fin::events::FiredHandler>& log,
                  const std::string& first, const std::string& second) {
    int fi = -1, si = -1;
    for (size_t i = 0; i < log.size(); ++i) {
        if (log[i].handler == first && fi < 0) fi = (int)i;
        if (log[i].handler == second && si < 0) si = (int)i;
    }
    return fi >= 0 && si >= 0 && fi < si;
}

} // namespace

// --- Slice 1: the W5 payload table ------------------------------------------
// The handler's signature IS the event's payload (§3.1): one row per event is
// the parameter list every handler for it must declare, and the quote it must
// return. W6's table must not answer for these rows, or two match checks
// would own one event and diagnose it twice.

TEST(W5Payload, StructLayoutFinalisedIsStructIdentity) {
    const auto* p = fin::events::findW5Payload("struct_layout_finalised");
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(p->params.size(), 1u);
    EXPECT_EQ(p->params[0].name, "s");
    EXPECT_EQ(p->params[0].type, "$struct");
    EXPECT_EQ(p->returns, "quote");
}

TEST(W5Payload, VariableDeclaredCarriesNameTypeAndMutability) {
    const auto* p = fin::events::findW5Payload("variable_declared");
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(p->params.size(), 3u);
    EXPECT_EQ(p->params[0].name, "name");
    EXPECT_EQ(p->params[0].type, "string");
    EXPECT_EQ(p->params[1].name, "t");
    EXPECT_EQ(p->params[1].type, "$type");
    EXPECT_EQ(p->params[2].name, "is_mutable");
    EXPECT_EQ(p->params[2].type, "bool");
    EXPECT_EQ(p->returns, "quote");
}

TEST(W5Payload, W5AndW6OwnDisjointEvents) {
    EXPECT_TRUE(fin::events::isW5Event("struct_layout_finalised"));
    EXPECT_TRUE(fin::events::isW5Event("variable_declared"));
    EXPECT_FALSE(fin::events::isW5Event("function_entry"));
    EXPECT_FALSE(fin::events::isW5Event("function_exit"));
    EXPECT_FALSE(fin::events::isW5Event("assignment"));
    EXPECT_FALSE(fin::events::isW5Event("allocation_site"));
    EXPECT_FALSE(fin::events::isW5Event("delete_site"));
    EXPECT_FALSE(fin::events::isW5Event("variable_scope_exit"));
    EXPECT_FALSE(fin::events::isW5Event("nosuchevent"));
    EXPECT_EQ(fin::events::findW5Payload("assignment"), nullptr);
    EXPECT_EQ(fin::events::findW5Payload("nosuchevent"), nullptr);
}

// --- Slice 1: the firing loop on a synthetic harness ------------------------
// No real event yet: two handlers, the armed set, and the orderedHandlers
// sequence (Q10). The loop fires armed handlers in order and skips the rest.

TEST(W5Fire, OrderFollowsOrderedHandlers) {
    auto a = analyzeW5(
        "#[on(variable_declared)]\n@special first(name: string, t: $type, is_mutable: bool) <quote> {}\n"
        "#[on(variable_declared)]\n@special second(name: string, t: $type, is_mutable: bool) <quote> {}\n"
        "compiler.events.enable(first);\n"
        "compiler.events.enable(second);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    ASSERT_EQ(a.fired.size(), 2u);
    EXPECT_EQ(a.fired[0].handler, "first");
    EXPECT_EQ(a.fired[1].handler, "second");
    EXPECT_EQ(a.fired[0].event, "variable_declared");
}

TEST(W5Fire, OnlyArmedHandlersFire) {
    auto a = analyzeW5(
        "#[on(variable_declared)]\n@special armed_h(name: string, t: $type, is_mutable: bool) <quote> {}\n"
        "#[on(variable_declared)]\n@special quiet_h(name: string, t: $type, is_mutable: bool) <quote> {}\n"
        "compiler.events.enable(armed_h);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    ASSERT_EQ(a.fired.size(), 1u);
    EXPECT_EQ(a.fired[0].handler, "armed_h");
}

TEST(W5Fire, NothingArmedFiresNothing) {
    auto a = analyzeW5(
        "#[on(variable_declared)]\n@special h(name: string, t: $type, is_mutable: bool) <quote> {}\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    EXPECT_TRUE(a.fired.empty());
}

// --- Slice 1: signature mismatch is a named diagnostic, not a silent skip ---
// §3.1 + §3.8: caught before anything runs, naming the handler, the event,
// and both shapes. The firing pass must not diagnose it a second time.

TEST(W5Mismatch, WrongArityIsRefused) {
    auto r = w5compile(
        "#[on(variable_declared)]\n@special h_decl(name: string) <quote> {}\n"
        "compiler.events.enable(h_decl);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w5messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_decl"), std::string::npos) << err;
    EXPECT_NE(err.find("variable_declared"), std::string::npos) << err;
    EXPECT_NE(err.find("(name: string, t: $type, is_mutable: bool) <quote>"), std::string::npos)
        << "the refusal must name the expected payload:\n" << err;
}

TEST(W5Mismatch, WrongReturnIsRefused) {
    auto r = w5compile(
        "#[on(variable_declared)]\n@special h_decl(name: string, t: $type, is_mutable: bool) <void> {}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w5messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_decl"), std::string::npos) << err;
    EXPECT_NE(err.find("(name: string, t: $type, is_mutable: bool) <quote>"), std::string::npos)
        << err;
}

TEST(W5Mismatch, WrongParamTypeIsRefused) {
    auto r = w5compile(
        "#[on(struct_layout_finalised)]\n@special h_lay(s: string) <quote> {}\n"
        "struct S { x <int> }\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w5messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_lay"), std::string::npos) << err;
    EXPECT_NE(err.find("(s: $struct) <quote>"), std::string::npos) << err;
}

TEST(W5Mismatch, MismatchIsDiagnosedOnce) {
    // The pre-pass check and the firing pass share one verdict: an armed
    // mismatched handler is one diagnostic, never a check-then-fire cascade.
    auto r = w5compile(
        "#[on(variable_declared)]\n@special h_decl(name: string) <quote> {}\n"
        "compiler.events.enable(h_decl);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    EXPECT_EQ(w5errorCount(stripAnsi(r.err)), 1u) << r.err;
}

// --- Slice 1: the interpretability line is held at fire time ----------------
// Five statement forms, no control flow; quote-returning calls + splice only.
// A handler that exceeds it is refused by name.

TEST(W5Line, ControlFlowIsRefusedByName) {
    auto r = w5compile(
        "#[on(variable_declared)]\n"
        "@special h_decl(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    if (1 == 1) {}\n"
        "}\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w5messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_decl"), std::string::npos) << err;
    EXPECT_NE(err.find("control flow"), std::string::npos) << err;
    EXPECT_EQ(w5errorCount(stripAnsi(r.err)), 1u) << r.err;
}

// --- Slice 2: struct_layout_finalised ---------------------------------------

TEST(W5StructLayout, FiresAfterLayoutWithStructIdentity) {
    auto a = analyzeW5(
        "#[on(struct_layout_finalised)]\n@special h_lay(s: $struct) <quote> {}\n"
        "compiler.events.enable(h_lay);\n"
        "struct Point { x <int>, y <int> }\n"
        "fun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    ASSERT_EQ(a.fired.size(), 1u);
    EXPECT_EQ(a.fired[0].event, "struct_layout_finalised");
    EXPECT_EQ(a.fired[0].handler, "h_lay");
    EXPECT_EQ(a.fired[0].detail, "Point");
}

TEST(W5StructLayout, EachStructFiresOnce) {
    auto a = analyzeW5(
        "#[on(struct_layout_finalised)]\n@special h_lay(s: $struct) <quote> {}\n"
        "compiler.events.enable(h_lay);\n"
        "struct A { x <int> }\n"
        "struct B { y <int> }\n"
        "fun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    ASSERT_EQ(a.fired.size(), 2u);
    EXPECT_EQ(a.fired[0].detail, "A");
    EXPECT_EQ(a.fired[1].detail, "B");
}

TEST(W5StructLayout, NonEmptyAnswerIsRefused) {
    // §3.2: the only legal answer for struct_layout_finalised is empty. A
    // handler that returns a quote fails compilation IFF it fired; the same
    // program with the handler unarmed compiles clean, so the refusal proves
    // end-to-end fire rather than collection.
    const std::string handler =
        "@define printf(fmt: string, ...) <noret>;\n"
        "#[on(struct_layout_finalised)]\n@special h_lay(s: $struct) <quote> {\n"
        "    return quote { printf(\"leaked\\n\"); };\n"
        "}\n"
        "struct S { x <int> }\n";
    auto armed = w5compile(handler +
        "compiler.events.enable(h_lay);\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(armed.exitCode, 0) << armed.err;
    const std::string err = w5messages(stripAnsi(armed.err));
    EXPECT_NE(err.find("h_lay"), std::string::npos) << err;
    EXPECT_NE(err.find("struct_layout_finalised"), std::string::npos) << err;

    auto unarmed = w5compile(handler + "fun main() <noret> {}\n");
    EXPECT_EQ(unarmed.exitCode, 0) << unarmed.err;
}

TEST(W5StructLayout, BlameAbortsNamingHandlerEventAndPoint) {
    // §3.8: a handler that executes blame is aborted with one diagnostic
    // naming the handler, the event and the event point; the remaining
    // handlers for that event still run, so two blaming handlers are two
    // diagnostics.
    auto r = w5compile(
        "#[on(struct_layout_finalised)]\n@special first_lay(s: $struct) <quote> {\n"
        "    blame 1 == 1, \"first saw layout\";\n"
        "}\n"
        "#[on(struct_layout_finalised)]\n@special second_lay(s: $struct) <quote> {\n"
        "    blame 1 == 1, \"second saw layout\";\n"
        "}\n"
        "compiler.events.enable(first_lay);\n"
        "compiler.events.enable(second_lay);\n"
        "struct S { x <int> }\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w5messages(stripAnsi(r.err));
    EXPECT_NE(err.find("first_lay"), std::string::npos) << err;
    EXPECT_NE(err.find("second_lay"), std::string::npos)
        << "the remaining handler still runs after the first aborts:\n" << err;
    EXPECT_NE(err.find("struct_layout_finalised"), std::string::npos) << err;
    EXPECT_NE(err.find("S"), std::string::npos) << err;
    EXPECT_EQ(w5errorCount(stripAnsi(r.err)), 2u) << r.err;
}

// --- Slice 3: variable_declared ----------------------------------------------

TEST(W5VarDecl, FiresAtDeclarationWithNameAndType) {
    auto a = analyzeW5(
        "#[on(variable_declared)]\n@special h_decl(name: string, t: $type, is_mutable: bool) <quote> {}\n"
        "compiler.events.enable(h_decl);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    ASSERT_EQ(a.fired.size(), 1u);
    EXPECT_EQ(a.fired[0].event, "variable_declared");
    EXPECT_EQ(a.fired[0].handler, "h_decl");
    EXPECT_NE(a.fired[0].detail.find("x"), std::string::npos) << a.fired[0].detail;
    EXPECT_NE(a.fired[0].detail.find("int"), std::string::npos) << a.fired[0].detail;
}

TEST(W5VarDecl, SpliceLandsAfterItsDeclaration) {
    // The returned quote is inserted at the event point: the injected
    // statement follows its `let` in the same block.
    auto a = analyzeW5(
        "@define printf(fmt: string, ...) <noret>;\n"
        "#[on(variable_declared)]\n@special h_decl(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    return quote { printf(\"w5-vdecl\\n\"); };\n"
        "}\n"
        "compiler.events.enable(h_decl);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_EQ(a.errorCount, 0) << "errors: " << a.errorCount;
    ASSERT_NE(a.ast, nullptr);
    bool found = false;
    for (const auto& stmt : a.ast->statements) {
        const auto* fn = dynamic_cast<const fin::FunctionDeclaration*>(stmt.get());
        if (!fn || fn->name != "main" || !fn->body) continue;
        const auto& body = fn->body->statements;
        for (size_t i = 0; i + 1 < body.size(); ++i) {
            const auto* decl = dynamic_cast<const fin::VariableDeclaration*>(body[i].get());
            const auto* next = dynamic_cast<const fin::ExpressionStatement*>(body[i + 1].get());
            if (decl && decl->name == "x" && next) found = true;
        }
    }
    EXPECT_TRUE(found) << "the injected statement follows `let x` in main's block";
}

#ifndef FIN_TESTS_HAVE_BACKEND
// A build configured without the backend still has to compile this file, so the
// test exists and says why it did not run rather than vanishing from the count
// (test_codegen.cpp says why the body becomes an uncalled function).
#define W5_BACKEND_TEST(suite, name)                                            \
    TEST(suite, name) { GTEST_SKIP() << "built with FIN_WITH_LLVM=OFF"; }       \
    [[maybe_unused]] static void w5_backend_body_##suite##_##name()
#else
#define W5_BACKEND_TEST(suite, name) TEST(suite, name)
#endif

namespace {

std::string w5shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

// Compiles a string to a real executable and runs it: the Fin-level probe
// that an injected marker survives lowering and executes.
struct W5Built {
    int compileExit = -1;
    std::string compileErr;
    bool ran = false;
    int runExit = -1;
    std::string out;
};

W5Built w5buildRun(const std::string& code) {
    W5Built b;
    fs::path src = uniqueTempPath("fin_w5", ".fin");
    fs::path exe = uniqueTempPath("fin_w5_exe");
    {
        std::ofstream f(src, std::ios::binary);
        f.write(code.data(), (std::streamsize)code.size());
    }
    const FincRun c = runFinc({src.string(), "-o", exe.string()});
    b.compileExit = c.exitCode;
    b.compileErr = stripAnsi(c.err);
    if (b.compileExit == 0 && fs::exists(exe)) {
        fs::path outPath = uniqueTempPath("fin_w5_out");
        std::string cmd =
            w5shellQuote(exe.string()) + " > " + w5shellQuote(outPath.string()) + " 2>&1";
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

const char* const kW5Handler =
    "@define printf(fmt: string, ...) <noret>;\n"
    "#[on(variable_declared)]\n@special h_decl(name: string, t: $type, is_mutable: bool) <quote> {\n"
    "    return quote { printf(\"w5-vdecl\\n\"); };\n"
    "}\n";

} // namespace

W5_BACKEND_TEST(W5VarDeclProbe, InjectedMarkerRunsAtTheDeclaration) {
    W5Built b = w5buildRun(std::string(kW5Handler) +
        "compiler.events.enable(h_decl);\n"
        "fun main() <noret> {\n"
        "    printf(\"before\\n\");\n"
        "    let x <int> = 1;\n"
        "    printf(\"after\\n\");\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.compileErr;
    ASSERT_TRUE(b.ran) << "compile exit " << b.compileExit << "\n" << b.compileErr;
    EXPECT_NE(b.out.find("w5-vdecl"), std::string::npos)
        << "the handler's quote ran at the declaration:\n" << b.out;
}

W5_BACKEND_TEST(W5VarDeclProbe, UnarmedHandlerInjectsNothing) {
    W5Built b = w5buildRun(std::string(kW5Handler) +
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    printf(\"done\\n\");\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.compileErr;
    ASSERT_TRUE(b.ran) << "compile exit " << b.compileExit << "\n" << b.compileErr;
    EXPECT_EQ(b.out.find("w5-vdecl"), std::string::npos)
        << "nothing fires without arming:\n" << b.out;
    EXPECT_NE(b.out.find("done"), std::string::npos) << b.out;
}

// --- Step 19 (docs/compiler-api.md §2.5 diag, §3.8): compiler.diag.* --------
// A handler's own diagnostics. `error` records, suppresses that handler's
// injection at that point, and fails the build; `warning`/`note` report and
// the quote still splices. Every one names the handler, the event and the
// event point — an error in code the user never wrote must say whose handler
// wrote it (the Rust-derive lesson, §1.9).

namespace {

const char* const kW5DiagGrants =
    "#[use(compiler)]\n#[use(compiler.components.diag)]\n";

} // namespace

TEST(W5Diag, ErrorFromHandlerNamesHandlerEventAndPoint) {
    auto r = w5compile(
        std::string("@define printf(fmt: string, ...) <noret>;\n") +
        kW5DiagGrants +
        "#[on(variable_declared)]\n@special h_diag(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    compiler.diag.error(\"trace me\");\n"
        "    return quote { printf(\"unreached\\n\"); };\n"
        "}\n"
        "compiler.events.enable(h_diag);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w5messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_diag"), std::string::npos) << err;
    EXPECT_NE(err.find("variable_declared"), std::string::npos) << err;
    EXPECT_NE(err.find("trace me"), std::string::npos) << err;
    EXPECT_NE(err.find("x"), std::string::npos) << err;
    // The diag call is executed, not gap-diagnosed: one point, one diagnostic.
    EXPECT_EQ(w5errorCount(stripAnsi(r.err)), 1u) << r.err;
}

TEST(W5Diag, DiagCallNeedsTheGrant) {
    // No `diag` grant: refused at the declaration, before anything fires.
    auto r = w5compile(
        std::string("#[use(compiler)]\n") +
        "#[on(variable_declared)]\n@special h_nogrant(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    compiler.diag.error(\"trace me\");\n"
        "    return quote { };\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w5messages(stripAnsi(r.err));
    EXPECT_NE(err.find("diag"), std::string::npos) << err;
    EXPECT_NE(err.find("not granted"), std::string::npos) << err;
}

TEST(W5Diag, DiagMessageMustBeALiteral) {
    // The line holds no string operations, so a message is a literal or it is
    // refused by name — never evaluated into something it is not.
    auto r = w5compile(
        std::string("@define printf(fmt: string, ...) <noret>;\n") +
        kW5DiagGrants +
        "#[on(variable_declared)]\n@special h_lit(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    compiler.diag.error(name);\n"
        "    return quote { printf(\"unreached\\n\"); };\n"
        "}\n"
        "compiler.events.enable(h_lit);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    const std::string err = w5messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_lit"), std::string::npos) << err;
    EXPECT_NE(err.find("literal"), std::string::npos) << err;
}

TEST(W5Diag, NoteFromHandlerReportsWithoutFailing) {
    auto r = w5compile(
        std::string("@define printf(fmt: string, ...) <noret>;\n") +
        kW5DiagGrants +
        "#[on(variable_declared)]\n@special h_note(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    compiler.diag.note(\"fyi\");\n"
        "    return quote { printf(\"w5-note\\n\"); };\n"
        "}\n"
        "compiler.events.enable(h_note);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
    const std::string err = stripAnsi(r.err);
    EXPECT_NE(err.find("h_note"), std::string::npos) << err;
    EXPECT_NE(err.find("fyi"), std::string::npos) << err;
}

W5_BACKEND_TEST(W5DiagProbe, WarningFromHandlerWarnsAndStillSplices) {
    W5Built b = w5buildRun(
        std::string("@define printf(fmt: string, ...) <noret>;\n") +
        kW5DiagGrants +
        "#[on(variable_declared)]\n@special h_warn(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    compiler.diag.warning(\"careful\");\n"
        "    return quote { printf(\"w5-warn-marker\\n\"); };\n"
        "}\n"
        "compiler.events.enable(h_warn);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    printf(\"done\\n\");\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.compileErr;
    ASSERT_TRUE(b.ran) << "compile exit " << b.compileExit << "\n" << b.compileErr;
    EXPECT_NE(b.compileErr.find("warning"), std::string::npos) << b.compileErr;
    EXPECT_NE(b.compileErr.find("h_warn"), std::string::npos) << b.compileErr;
    EXPECT_NE(b.compileErr.find("careful"), std::string::npos) << b.compileErr;
    EXPECT_NE(b.out.find("w5-warn-marker"), std::string::npos)
        << "a warning does not suppress the injection:\n" << b.out;
    EXPECT_NE(b.out.find("done"), std::string::npos) << b.out;
}

// --- Step 19: injected-node attribution end to end ---------------------------
// The handler injects code the user never wrote; when that code does not
// check, the diagnostic names the handler and the event point (file:line of
// the event and the handler name). Proven with Fin probes: a .fin file is
// written, finc is run, the build fails with exit 1 and the message carries
// the attribution.

TEST(W5Attribution, BadInjectionNamesHandlerEventAndPoint) {
    auto r = w5compile(
        std::string("@define printf(fmt: string, ...) <noret>;\n") +
        "#[on(variable_declared)]\n@special h_bad(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    return quote { nosuchfn_xyz(); };\n"
        "}\n"
        "compiler.events.enable(h_bad);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    EXPECT_EQ(r.exitCode, 1) << "a diagnosable fault is exit 1, never an internal error:\n" << r.err;
    const std::string err = w5messages(stripAnsi(r.err));
    EXPECT_NE(err.find("h_bad"), std::string::npos) << err;
    EXPECT_NE(err.find("variable_declared"), std::string::npos) << err;
    EXPECT_NE(err.find("x"), std::string::npos) << err;
    EXPECT_EQ(w5errorCount(stripAnsi(r.err)), 1u) << r.err;
}

TEST(W5Attribution, JsonCarriesHandlerAttribution) {
    // The machine contract: the attribution is data, not just prose. The
    // injected-code diagnostic carries a non-null `attribution` object naming
    // the handler and the event.
    W5Src s(
        std::string("@define printf(fmt: string, ...) <noret>;\n") +
        "#[on(variable_declared)]\n@special h_bad(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    return quote { nosuchfn_xyz(); };\n"
        "}\n"
        "compiler.events.enable(h_bad);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    const FincRun r = runFinc({s.str(), "--diagnostics=json"});
    EXPECT_EQ(r.exitCode, 1) << r.err;
    EXPECT_NE(r.err.find("\"attribution\":{\"handler\":\"h_bad\",\"event\":\"variable_declared\"}"),
              std::string::npos)
        << "the injected-code diagnostic must carry handler attribution:\n" << r.err;
}

// --- Step 19: attribution survival across phases -----------------------------
// collect → fire → splice → check keeps identity: with two event points the
// check reports two diagnostics, each naming the handler and its own point,
// and no diagnostic from generated code goes unattributed.

TEST(W5AttributionSurvival, TwoPointsTwoDiagnosticsEachAttributed) {
    auto r = w5compile(
        std::string("@define printf(fmt: string, ...) <noret>;\n") +
        "#[on(variable_declared)]\n@special h_two(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    return quote { nosuchfn_xyz(); };\n"
        "}\n"
        "compiler.events.enable(h_two);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let y <int> = 2;\n"
        "}\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    const std::string stripped = stripAnsi(r.err);
    EXPECT_EQ(w5errorCount(stripped), 2u) << r.err;
    const std::string err = w5messages(stripped);
    EXPECT_NE(err.find("x:int"), std::string::npos) << err;
    EXPECT_NE(err.find("y:int"), std::string::npos) << err;
    // No unattributed errors from generated code: every diagnostic line names
    // the handler that wrote it.
    for (size_t i = 0; i < err.size();) {
        size_t eol = err.find('\n', i);
        if (eol == std::string::npos) eol = err.size();
        if (err.compare(i, 7, "error: ") == 0)
            EXPECT_NE(err.find("h_two", i), std::string::npos) << err.substr(i, eol - i);
        i = eol + 1;
    }
}

TEST(W5AttributionSurvival, DiagErrorPlusBadQuoteIsOneDiagnostic) {
    // §3.8 row one: a handler that reports suppresses its own injection at
    // that point, so the bad quote never reaches the check. One point, one
    // diagnostic — the suppression made visible.
    auto r = w5compile(
        std::string("@define printf(fmt: string, ...) <noret>;\n") +
        kW5DiagGrants +
        "#[on(variable_declared)]\n@special h_stop(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    compiler.diag.error(\"stop\");\n"
        "    return quote { nosuchfn_xyz(); };\n"
        "}\n"
        "compiler.events.enable(h_stop);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    const std::string stripped = stripAnsi(r.err);
    EXPECT_EQ(w5errorCount(stripped), 1u) << r.err;
    const std::string err = w5messages(stripped);
    EXPECT_NE(err.find("h_stop"), std::string::npos) << err;
    EXPECT_NE(err.find("stop"), std::string::npos) << err;
    EXPECT_EQ(err.find("nosuchfn_xyz"), std::string::npos) << err;
}
