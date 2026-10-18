#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "Corpus.hpp"
#include "Pipeline.hpp"
#include "ast/StructuralWalk.hpp"
#include "ast/decls/Program.hpp"
#include "ast/exprs/FunctionCall.hpp"
#include "ast/exprs/Identifier.hpp"
#include "ast/exprs/Lambda.hpp"
#include "ast/exprs/StructureExpr.hpp"
#include "ast/stmts/ControlFlow.hpp"
#include "diagnostics/DiagnosticEngine.hpp"
#include "semantics/SemanticAnalyzer.hpp"
#include "types/Type.hpp"

using namespace fin::testing;

namespace {

struct Analysis {
    bool parsed = false;
    bool analyzerFlag = false;
    bool engineErrors = false;
    int errorCount = 0;
    std::vector<fin::Diagnostic> diagnostics;
    std::shared_ptr<fin::Scope> globals;

    bool clean() const { return parsed && !analyzerFlag && !engineErrors; }
};

// Runs the real SemanticAnalyzer. Nothing in the old suite ever constructed one.
Analysis analyze(const std::string& code) {
    Analysis a;
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
    analyzer.visit(*parsed.ast);

    a.analyzerFlag = analyzer.hasError;
    a.engineErrors = diag->hasErrors();
    a.errorCount = diag->getErrorCount();
    a.diagnostics = diag->getDiagnostics();
    a.globals = analyzer.getGlobalScope();
    // The scope holds no reference into the engine, but the AST does not outlive
    // this call, so nothing else may be read from `a` afterwards.
    return a;
}

} // namespace

TEST(SemanticAnalyzer, AcceptsAnEmptyProgram) {
    auto a = analyze("");
    EXPECT_TRUE(a.parsed);
    EXPECT_FALSE(a.analyzerFlag);
    EXPECT_FALSE(a.engineErrors);
}

TEST(SemanticAnalyzer, AcceptsAMinimalFunction) {
    auto a = analyze("fun main() <noret> {}\n");
    EXPECT_TRUE(a.clean()) << "errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, DefinesATopLevelFunctionInTheGlobalScope) {
    auto a = analyze("fun main() <noret> {}\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_NE(a.globals, nullptr);
    EXPECT_NE(a.globals->resolve("main"), nullptr)
        << "the analyzer must publish a top-level function into the global scope";
}

TEST(SemanticAnalyzer, DefinesAStructTypeInTheGlobalScope) {
    auto a = analyze("struct Point { x <int>, y <int> }\n");
    ASSERT_TRUE(a.parsed);
    ASSERT_NE(a.globals, nullptr);
    EXPECT_NE(a.globals->resolveType("Point"), nullptr);
}

TEST(SemanticAnalyzer, RejectsAnUndefinedVariable) {
    auto a = analyze("fun main() <noret> { let x <int> = y; }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.analyzerFlag || a.engineErrors)
        << "reading an undeclared name must be a diagnostic";
}

TEST(SemanticAnalyzer, RejectsAnUndefinedType) {
    auto a = analyze("fun main() <noret> { let x <Nope>; }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.analyzerFlag || a.engineErrors);
}

TEST(SemanticAnalyzer, RejectsACallToAnUndefinedFunction) {
    auto a = analyze("fun main() <noret> { nope(); }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.analyzerFlag || a.engineErrors);
}

TEST(SemanticAnalyzer, AcceptsAPublicStructFieldAccess) {
    // `pub` on a field is the form the corpus uses: generics_interfaces.fin:4
    // and readonly.fin:9 both declare fields that way.
    auto a = analyze(
        "struct Point { pub x <int>, pub y <int> }\n"
        "fun main() <noret> { let p <Point>; let n <int> = p.x; }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.clean()) << "errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, RejectsAPrivFieldAccessFromOutside) {
    // Reading a field declared `priv` from a free function is `Cannot access private
    // field`. The subject used to be an *undecorated* field, on the premise that
    // fields are private unless declared `pub` -- and the comment said outright that
    // it recorded the behaviour of the day rather than a claim. The corpus says the
    // opposite: structs.fin is `//@ ok` and reads three unprefixed fields from
    // main(), so the default is public. Rewritten to keep what it was really
    // protecting -- that the refusal exists -- with a subject that is actually
    // private. The default itself is now Soundness_FieldVisibility's, in
    // test_soundness.cpp.
    auto a = analyze(
        "struct Point { priv x <int>, pub y <int> }\n"
        "fun main() <noret> { let p <Point>; let n <int> = p.x; }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.analyzerFlag || a.engineErrors);
}

TEST(SemanticAnalyzer, RejectsAnUnknownStructField) {
    auto a = analyze(
        "struct Point { x <int>, y <int> }\n"
        "fun main() <noret> { let p <Point>; let n <int> = p.zzz; }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.analyzerFlag || a.engineErrors)
        << "reading a field the struct does not declare must be a diagnostic";
}

TEST(SemanticAnalyzer, RejectsDuplicateStructFields) {
    // Issue #43: `struct P { x <int>, x <int> }` is a static program error and
    // must be diagnosed by semantics, not surface downstream as a codegen
    // "not lowered yet" refusal. The diagnostic names the field and the struct
    // and points at the second declaration.
    for (const std::string structDecl :
         {"struct P { x <int>, x <int> }\n", "struct Q { x <int>, x <string> }\n"}) {
        auto a = analyze(structDecl + "fun main() <noret> {}\n");
        ASSERT_TRUE(a.parsed) << structDecl;
        EXPECT_TRUE(a.analyzerFlag) << structDecl;
        EXPECT_TRUE(a.engineErrors) << structDecl;
        const char want = structDecl[7]; // 'P' or 'Q'
        bool found = false;
        for (const auto& d : a.diagnostics) {
            if (d.severity != fin::DiagnosticSeverity::Error) continue;
            if (d.message.find("uplicate") != std::string::npos &&
                d.message.find("'x'") != std::string::npos &&
                d.message.find(std::string("'") + want + "'") != std::string::npos) {
                found = true;
                EXPECT_EQ(d.line, 1) << d.message;
            }
        }
        EXPECT_TRUE(found) << "no duplicate-field error for: " << structDecl;
    }
}

TEST(SemanticAnalyzer, SetsHasErrorAndTheEngineTogether) {
    // The driver used to gate `Build Successful.` on analyzer.hasError alone. A
    // semantic error must show in both places, so neither gate alone is enough
    // and both agree when it is a semantic error.
    auto a = analyze("fun main() <noret> { nope(); }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.analyzerFlag);
    EXPECT_TRUE(a.engineErrors);
    EXPECT_GT(a.errorCount, 0);
}

TEST(SemanticAnalyzer, IsReusableAcrossTranslationUnits) {
    // The lexer and the `fin::root` global are shared mutable state; a second
    // analysis in the same process must not inherit the first one's.
    auto first = analyze("fun main() <noret> { nope(); }\n");
    ASSERT_TRUE(first.parsed);
    ASSERT_TRUE(first.analyzerFlag);

    auto second = analyze("fun main() <noret> {}\n");
    EXPECT_TRUE(second.clean()) << "state leaked from the previous analysis";
}

// The one sample that has ever reached the analyzer, kept as a unit test so a
// regression in the analyzer is visible without the corpus runner.
TEST(SemanticAnalyzer, InterfacesSampleStillReachesTheAnalyzer) {
    const std::string src = readWholeFile(samplesDir() + "/interfaces.fin");
    ASSERT_FALSE(src.empty());
    auto a = analyze(src);
    EXPECT_TRUE(a.parsed) << "interfaces.fin is the only sample that parses AND analyses";
    EXPECT_FALSE(a.analyzerFlag || a.engineErrors)
        << "interfaces.fin is expected to analyse clean after implementing Printable";
}

// ---------------------------------------------------------------------------
// Lambda capture analysis (closure slice 1).
//
// SemanticAnalyzer::visit(LambdaExpression&) records each free variable the body
// reads from an enclosing function scope on LambdaExpression::captures, so that
// codegen can build the env struct without re-deriving scope resolution. These
// tests read the recorded set through the analyzed AST: behavior through the
// analyzer's own interface, not through internals.
// ---------------------------------------------------------------------------

namespace {

struct CapturedProgram {
    std::unique_ptr<fin::DiagnosticEngine> diag;
    fin::testing::ParseResult parsed;
    bool analyzerError = false;
};

// Parse + analyze while keeping the AST alive for capture inspection.
CapturedProgram analyzeKeepAst(const std::string& code) {
    CapturedProgram cp;
    cp.diag = std::make_unique<fin::DiagnosticEngine>("", "<test>");
    cp.diag->setColorMode(fin::ColorMode::Never);
    cp.parsed = fin::testing::parseSource(code, *cp.diag);
    if (!cp.parsed.parsed) return cp;
    fin::SemanticAnalyzer analyzer(*cp.diag, false);
    analyzer.visit(*cp.parsed.ast);
    cp.analyzerError = analyzer.hasError || cp.diag->hasErrors();
    return cp;
}

class LambdaCollector : public fin::StructuralWalk {
public:
    std::vector<fin::LambdaExpression*> lambdas;
    bool enter(fin::ASTNode& node) override {
        if (auto* lam = dynamic_cast<fin::LambdaExpression*>(&node))
            lambdas.push_back(lam);
        return true;
    }
};

std::vector<fin::LambdaExpression*> collectLambdas(fin::Program& prog) {
    LambdaCollector c;
    c.walk(prog);
    return c.lambdas;
}

}  // namespace

TEST(SemanticAnalyzer, LambdaCapturesAnOuterLocalByValue) {
    CapturedProgram cp = analyzeKeepAst(
        "fun main() <noret> {\n"
        "    let outer <int> = 7;\n"
        "    let f <auto> = (x: int) <int> => x + outer;\n"
        "}\n");
    ASSERT_TRUE(cp.parsed.parsed);
    ASSERT_FALSE(cp.analyzerError);
    auto lambdas = collectLambdas(*cp.parsed.ast);
    ASSERT_EQ(lambdas.size(), 1u);
    ASSERT_EQ(lambdas[0]->captures.size(), 1u);
    EXPECT_EQ(lambdas[0]->captures[0].name, "outer");
    EXPECT_FALSE(lambdas[0]->captures[0].byRef)
        << "an <int> capture is a snapshot copy";
}

TEST(SemanticAnalyzer, LambdaCapturesAReferenceBindingByReference) {
    CapturedProgram cp = analyzeKeepAst(
        "fun main() <noret> {\n"
        "    let cell <&int> = new int(7);\n"
        "    let f <auto> = (x: int) <int> => x + *cell;\n"
        "}\n");
    ASSERT_TRUE(cp.parsed.parsed);
    ASSERT_FALSE(cp.analyzerError);
    auto lambdas = collectLambdas(*cp.parsed.ast);
    ASSERT_EQ(lambdas.size(), 1u);
    ASSERT_EQ(lambdas[0]->captures.size(), 1u);
    EXPECT_EQ(lambdas[0]->captures[0].name, "cell");
    EXPECT_TRUE(lambdas[0]->captures[0].byRef)
        << "a <&int> capture copies the pointer, so it aliases the referent";
}

TEST(SemanticAnalyzer, LambdaShadowingIsNotACapture) {
    CapturedProgram cp = analyzeKeepAst(
        "fun main() <noret> {\n"
        "    let v <int> = 1;\n"
        "    let f <auto> = (v: int) <int> => v + 1;\n"
        "    let g <auto> = fun () <int> {\n"
        "        let v <int> = 2;\n"
        "        return v;\n"
        "    };\n"
        "}\n");
    ASSERT_TRUE(cp.parsed.parsed);
    ASSERT_FALSE(cp.analyzerError);
    auto lambdas = collectLambdas(*cp.parsed.ast);
    ASSERT_EQ(lambdas.size(), 2u);
    EXPECT_TRUE(lambdas[0]->captures.empty())
        << "a parameter shadowing an outer local is the parameter, not a capture";
    EXPECT_TRUE(lambdas[1]->captures.empty())
        << "a body-local shadowing an outer local is the local, not a capture";
}

TEST(SemanticAnalyzer, NestedLambdaCapturesPropagateToTheOuterLambda) {
    CapturedProgram cp = analyzeKeepAst(
        "fun main() <noret> {\n"
        "    let x <int> = 3;\n"
        "    let outer <auto> = fun () <int> {\n"
        "        let inner <auto> = fun () <int> { return x + 1; };\n"
        "        return inner();\n"
        "    };\n"
        "}\n");
    ASSERT_TRUE(cp.parsed.parsed);
    ASSERT_FALSE(cp.analyzerError);
    auto lambdas = collectLambdas(*cp.parsed.ast);
    ASSERT_EQ(lambdas.size(), 2u);
    // Pre-order: the outer lambda first, the inner second.
    EXPECT_EQ(lambdas[0]->captures.size(), 1u);
    EXPECT_EQ(lambdas[0]->captures[0].name, "x");
    EXPECT_EQ(lambdas[1]->captures.size(), 1u);
    EXPECT_EQ(lambdas[1]->captures[0].name, "x");
}

TEST(SemanticAnalyzer, LambdaDoesNotCaptureGlobalsOrFunctions) {
    CapturedProgram cp = analyzeKeepAst(
        "const BASE <int> = 100;\n"
        "fun helper() <int> { return 1; }\n"
        "@define printf(fmt: string, ...) <noret>;\n"
        "fun main() <noret> {\n"
        "    let f <auto> = (x: int) <int> => x + BASE + helper();\n"
        "}\n");
    ASSERT_TRUE(cp.parsed.parsed);
    ASSERT_FALSE(cp.analyzerError);
    auto lambdas = collectLambdas(*cp.parsed.ast);
    ASSERT_EQ(lambdas.size(), 1u);
    EXPECT_TRUE(lambdas[0]->captures.empty())
        << "globals and module functions are symbols, not frame slots";
}

// ---------------------------------------------------------------------------
// Integer type-alias completeness: the analyzer must register the short
// spellings whose widths U81 added to the Layout/codegen table only.
// `let x<u8> = 1` reported `Undefined type 'u8'` with the width in place.
// ---------------------------------------------------------------------------

TEST(SemanticAnalyzer, AcceptsIntegerAliasSpellingsInAnnotations) {
    // One fitting initializer per spelling. Float literals type as `float`,
    // which is a different name from `f32`/`f64` with no implicit conversion
    // between them (as `double` shows: `let x<double> = 1.0` is refused
    // today), so the float aliases initialize through a cast -- the same
    // cast the corpus uses to assert a value's type.
    struct Case { const char* type; const char* init; };
    const Case cases[] = {
        {"u8", "1"}, {"i8", "1"}, {"u16", "1"}, {"i16", "1"},
        {"u32", "1"}, {"i32", "1"}, {"u64", "1"}, {"i64", "1"},
        {"f32", "cast<f32>(1.0)"}, {"f64", "cast<f64>(1.0)"},
        {"usize", "1"}, {"isize", "1"}, {"size_t", "1"},
    };
    for (const auto& c : cases) {
        const std::string code =
            std::string("fun main() <noret> { let x <") + c.type + "> = " + c.init + "; }\n";
        auto a = analyze(code);
        ASSERT_TRUE(a.parsed) << c.type;
        EXPECT_TRUE(a.clean()) << "<" << c.type << "> must resolve; errors: " << a.errorCount;
    }
}

TEST(SemanticAnalyzer, IntegerAliasSpellingsIncrementLikeTheirKind) {
    // Every existing integer and float answers `++`; the aliases must too.
    const char* types[] = {"u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64",
                           "f32", "f64", "usize", "isize", "size_t"};
    for (const char* t : types) {
        const std::string code =
            std::string("fun main() <noret> { let x <") + t + "> = 1; x++; }\n";
        auto a = analyze(code);
        ASSERT_TRUE(a.parsed) << t;
        EXPECT_TRUE(a.clean()) << "<" << t << "> must increment; errors: " << a.errorCount;
    }
}

TEST(SemanticAnalyzer, PreexistingTypeNamesStillResolve) {
    // The registration being extended, not replaced: every name the analyzer
    // resolved before still resolves with a fitting value.
    struct Case { const char* type; const char* init; };
    const Case cases[] = {
        {"int", "1"}, {"uint", "1"}, {"short", "1"}, {"ushort", "1"},
        {"long", "1"}, {"ulong", "1"}, {"char", "1"},
        {"float", "1.0"}, {"bool", "true"}, {"string", "\"s\""},
    };
    for (const auto& c : cases) {
        const std::string code =
            std::string("fun main() <noret> { let x <") + c.type + "> = " + c.init + "; }\n";
        auto a = analyze(code);
        ASSERT_TRUE(a.parsed) << c.type;
        EXPECT_TRUE(a.clean()) << "<" << c.type << "> regressed; errors: " << a.errorCount;
    }
}

// ---------------------------------------------------------------------------
// Wave-4 @implements query lowering, stage A: analyzer fold.
//
// `@implements(User, Printable)` folds through the compiler-API op
// (`compiler.types.implements`, the `symbols.defined` precedent: one
// predicate behind both spellings), so `if` over the query eliminates the
// untaken arm. Non-foldable `$struct`/`$interface` params stay `bool`.
// ---------------------------------------------------------------------------

namespace {

const char* const kImplementsShapes =
    "interface Printable {\n"
    "    pub fun to_string() <string>;\n"
    "}\n"
    "struct User: <Printable> {\n"
    "    name <string>,\n"
    "    age <int>,\n"
    "    pub fun to_string() <string> {\n"
    "        return \"User\";\n"
    "    }\n"
    "}\n"
    "struct Empty {\n"
    "    x <int>,\n"
    "}\n";

}  // namespace

TEST(SemanticAnalyzer, ImplementsTrueFoldEliminatesElseArm) {
    // A conforming pair folds true, so the else arm -- which names a type
    // that does not exist -- is never walked.
    auto a = analyze(std::string(kImplementsShapes) +
        "fun main() <noret> {\n"
        "    if (@implements(User, Printable)) {\n"
        "    } else {\n"
        "        let bad <NoSuchType> = 1;\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.clean()) << "the else arm must be eliminated; errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, ImplementsFalseFoldEliminatesThenArm) {
    // A non-conforming pair folds false, so the then arm is never walked.
    // Spelled `== true` like literal_interface.fin:6, which must fold too.
    auto a = analyze(std::string(kImplementsShapes) +
        "fun main() <noret> {\n"
        "    if (@implements(Empty, Printable) == true) {\n"
        "        let bad <NoSuchType> = 1;\n"
        "    } else {\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.clean()) << "the then arm must be eliminated; errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, ImplementsOverMetaParamsStaysBool) {
    // Non-foldable `$struct`/`$interface` params (literal_interface.fin:5-6)
    // stay typed `bool` with the query recorded: both arms still walk, and
    // the function checks clean.
    auto a = analyze(
        "fun compatible(iface: $interface, struct_: $struct) <bool> {\n"
        "    if (@implements(struct_, iface) == true) {\n"
        "        return true;\n"
        "      } else {\n"
        "          return false;\n"
        "        }\n"
        "  }\n"
        "fun main() <noret> {\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.clean()) << "meta params must stay bool; errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, ImplementsThroughCompilerApiEliminatesArm) {
    // Grammar now spells `.implements` after DOT (parser.y: `DOT
    // KW_IMPLEMENTS`, the `@implements`/`attr_id` precedent for a keyword in
    // member position), so the compiler-API spelling parses directly to the
    // MethodCall the old pin planted by hand -- and folds through the same op
    // as `@implements(...)`.
    auto a = analyze(std::string(kImplementsShapes) +
        "#[use(compiler)]\n"
        "#[use(compiler.components.types)]\n"
        "fun main() <noret> {\n"
        "    if (compiler.types.implements(User, Printable)) {\n"
        "    } else {\n"
        "        let bad <NoSuchType> = 1;\n"
        "    }\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.clean()) << "the API spelling must fold like @implements; errors: " << a.errorCount;
}

// ---------------------------------------------------------------------------
// Returning a reference out of the frame that owns it (#99, ADR 0055).
//
// The rule's two halves are spelled differently and refuse for one reason: a
// returned `&local` hands out the slot, and a local converted to an interface
// hands out the slot's address as the pair's data word. What decides is whose
// frame the storage belongs to, not which spelling asked -- so the receiver,
// a parameter, a global and `#[slaveof($Fin)]` storage all stay legal, because
// each of them outlives the call that returns it.
// ---------------------------------------------------------------------------

namespace {

// Whether some error diagnostic contains `needle`. The messages are matched by
// substring, as the duplicate-field tests above do: the wording is the rule's
// user-visible contract, the surrounding frame is the engine's.
bool refusedWith(const Analysis& a, const std::string& needle) {
    for (const auto& d : a.diagnostics) {
        if (d.severity != fin::DiagnosticSeverity::Error) continue;
        if (d.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

const char* const kSpeaker =
    "interface Speaker { pub fun speak() <int>; }\n";

} // namespace

TEST(SemanticAnalyzer, AcceptsReturningTheAddressOfASelfField) {
    // `self` is by reference: a method assigning `self.n` is visible in the
    // caller, so `&self.n` addresses the caller's object and outlives the
    // call. Refusing it would reject caller-owned storage.
    auto a = analyze(
        "struct D { n <int>\n"
        "    fun ref() <&int> { return &self.n; }\n"
        "}\n"
        "fun main() <noret> { let d <D> = D { n: 7 }; }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.clean())
        << "`&self.n` is caller-owned storage, not this frame's; errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, AcceptsReturningSelfAsAnInterface) {
    // Same storage question, the interface spelling. The pair's data word
    // points at the caller's object, which the call does not end.
    auto a = analyze(std::string(kSpeaker) +
        "struct D { n <int>\n"
        "    fun speak() <int> { return self.n; }\n"
        "    fun me() <Speaker> { return self; }\n"
        "}\n"
        "D implements <Speaker> { }\n"
        "fun main() <noret> { let d <D> = D { n: 7 }; }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.clean())
        << "`self` as an interface borrows the caller's object; errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, AcceptsReturningTheAddressOfAParameterField) {
    // A pointer parameter dereferences into storage the caller already owns.
    auto a = analyze(
        "struct D { n <int> }\n"
        "fun get(p: &D) <&int> { return &p.n; }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.clean()) << "errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, AcceptsReturningTheAddressOfAGlobal) {
    // A global's storage is the program's, so no call can end it.
    auto a = analyze(
        "struct D { n <int> }\n"
        "let g <D> = D { n: 1 };\n"
        "fun get() <&D> { return &g; }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.clean()) << "errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, RefusesReturningTheAddressOfALocal) {
    auto a = analyze(
        "struct D { n <int> }\n"
        "fun make() <&D> { let d <D> = D { n: 7 }; return &d; }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(refusedWith(a, "cannot return the address of local variable 'd'"))
        << "errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, RefusesReturningALocalAsAnInterface) {
    auto a = analyze(std::string(kSpeaker) +
        "struct D { n <int>\n"
        "    fun speak() <int> { return self.n; }\n"
        "}\n"
        "D implements <Speaker> { }\n"
        "fun make() <Speaker> { let d <D> = D { n: 7 }; return d; }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(refusedWith(a, "cannot return local variable 'd' as an interface"))
        << "errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, RefusesReturningTheAddressOfALocalAsAStructResult) {
    // The constructor convention accepts a returned pointer where the declared
    // result is the value type, because `new S{}` is the constructor's storage
    // operation. `&local` is not: it is the same dangle the `<&D>` spelling
    // refuses, reached through the exemption that waives the type check.
    auto a = analyze(
        "struct D { n <int> }\n"
        "fun make() <D> { let d <D> = D { n: 7 }; return &d; }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(refusedWith(a, "cannot return the address of local variable 'd'"))
        << "the struct-result exemption must not also waive the lifetime rule; errors: "
        << a.errorCount;
}

TEST(SemanticAnalyzer, RefusesReturningTheAddressOfALocalArrayElement) {
    // An element's storage is the array's, so indexing reaches the same frame
    // a bare `&a` would.
    auto a = analyze(
        "fun make() <&int> { let a <[int]> = [1, 2, 3]; return &a[0]; }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(refusedWith(a, "cannot return the address of local variable 'a'"))
        << "indexing does not leave the frame; errors: " << a.errorCount;
}

TEST(Soundness_Semantics, RefusesAnEscapingAddressLaunderedThroughAPointerLocal) {
    // #105: a pointer local initialised from `&d` is `d`'s address however
    // many pointer hops intervene, so both spellings refuse with the existing
    // wording, blamed on the returned expression. Provenance-lite tracks the
    // initializer (and copies of it, `let q = p`); a pointer parameter, a
    // global, and a call's result have no entry and stay accepted.
    //
    // CEILING (documented): reassignment inside one branch of a conditional
    // defeats the tracking -- the join keeps whichever entry ran last -- so
    // that shape is still accepted. Closing it needs real alias analysis, an
    // owner-level call, not this slice.
    auto a = analyze(
        "struct D { n <int> }\n"
        "fun viaLocal() <&D> { let d <D> = D { n: 7 }; let p <&D> = &d; return p; }\n"
        "fun viaField() <&int> { let d <D> = D { n: 8 }; let p <&D> = &d; return &p.n; }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(refusedWith(a, "cannot return the address of local variable 'd'"))
        << "errors: " << a.errorCount;
    EXPECT_EQ(a.errorCount, 2) << "both laundered spellings must refuse";
}

TEST(SemanticAnalyzer, AcceptsReturningAPointerLocalCopiedFromAParameter) {
    // The #105 non-goal: a pointer with no `&local` initializer points at
    // storage someone else owns, so forwarding it stays accepted -- refusing
    // it would move the bug rather than close it.
    auto a = analyze(
        "struct D { n <int> }\n"
        "fun fwd(p: &D) <&D> { let q <&D> = p; return q; }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.clean()) << "errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, AcceptsReturningAPointerLocalToAGlobal) {
    // Same storage question, the global spelling: the referent outlives the
    // call, so laundering it through a local changes nothing.
    auto a = analyze(
        "struct D { n <int> }\n"
        "let g <D> = D { n: 1 };\n"
        "fun get() <&D> { let p <&D> = &g; return p; }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.clean()) << "errors: " << a.errorCount;
}

// ---------------------------------------------------------------------------
// Issues #109/ADR 0056: the routes that carry a frame-local address out
// without presenting a `return` operand the 0055 reading could ask. Each
// refusal keeps the wording and blames the expression that carries the
// address; each control keeps the same-frame spelling legal.
// ---------------------------------------------------------------------------

TEST(Soundness_Semantics, RefusesAReturnedStructLiteralFieldCarryingALocalAddress) {
    // The literal answers no root to `returnedStorage`, so the operand rule
    // never reached `W { p: &d }`; the field is the same slot `&d` hands out.
    auto a = analyze(
        "struct W { p <&int> }\n"
        "fun leak() <W> { let d <int> = 7; return W { p: &d }; }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(refusedWith(a, "cannot return the address of local variable 'd'"))
        << "errors: " << a.errorCount;
}

TEST(Soundness_Semantics, RefusesAReturnedArrayLiteralElementCarryingALocalAddress) {
    // Same reading, one element deep.
    auto a = analyze(
        "fun leak() <[&int]> { let d <int> = 7; return [&d]; }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(refusedWith(a, "cannot return the address of local variable 'd'"))
        << "errors: " << a.errorCount;
}

TEST(Soundness_Semantics, RefusesAReturnedLiteralFieldLaunderedThroughAPointerLocal) {
    // #105's readability, at the field: `W { p: q }` copies `q`, whose
    // provenance says `d`.
    auto a = analyze(
        "struct W { p <&int> }\n"
        "fun leak() <W> { let d <int> = 7; let q <&int> = &d; return W { p: q }; }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(refusedWith(a, "cannot return the address of local variable 'd'"))
        << "errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, AcceptsSameFrameLiteralFieldsCarryingLocalAddresses) {
    // The pinned counter-cases: the same spellings built into same-frame
    // locals are how pointer fields and pointer arrays get their values, and
    // the pins (Soundness_Codegen.APointerFieldInAStructRoundTrips,
    // .AnArrayOfPointersIsIndexedAndDereferenced,
    // .ATypeParameterUnderAPointerIsSubstituted) hold them. The walk runs
    // only on returned literals.
    auto a = analyze(
        "struct Holder { p <&int> }\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "    let y <int> = 2;\n"
        "    let h <Holder> = Holder { p: &x };\n"
        "    let ps <[&int, 2]> = [&x, &y];\n"
        "    *h.p = 42;\n"
        "    *ps[1] = 20;\n"
        "}\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.clean()) << "errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, RefusesAnOutParameterStoreOfALocalAddress) {
    // `*out` is the caller's slot: what is written there outlives this frame,
    // and `&d` does not. Not a return, so the operand rule never ran; the
    // store route answers at the statement.
    auto a = analyze(
        "fun leak(out: &&int) <noret> { let d <int> = 7; *out = &d; }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(refusedWith(a,
        "cannot store the address of local variable 'd' into storage that outlives the call"))
        << "errors: " << a.errorCount;
    EXPECT_EQ(a.errorCount, 1) << "one store, one refusal";
}

TEST(SemanticAnalyzer, RefusesAGlobalStoreOfALocalAddress) {
    auto a = analyze(
        "let g <&int>;\n"
        "fun leak() <noret> { let d <int> = 7; g = &d; }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(refusedWith(a,
        "cannot store the address of local variable 'd' into storage that outlives the call"))
        << "errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, AcceptsStoringAFrameAddressInFrameStorage) {
    // The deref-store exemption: a pointer local proven to point at this
    // frame's own storage dies with it, so `*p = &e` both-sides-frame is
    // legal. Same for a global's own address into a global.
    auto a = analyze(
        "let g <&&int>;\n"
        "let g2 <&int>;\n"
        "fun shortlived() <noret> {\n"
        "    let e <int> = 7;\n"
        "    let d <int> = 8;\n"
        "    let p <&int> = &d;\n"
        "    *p = &e;\n"
        "    g = &g2;\n"
        "}\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.clean()) << "errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, AcceptsStoringAParameterOrGlobalAddressOut) {
    // The exemptions mirrored: through a pointer (the parameter's referent),
    // into the receiver-shaped and $Fin-pinned targets. A parameter's slot
    // address is still this frame's -- the pointer hop carries its *referent*,
    // caller-owned -- while `&global` is program storage.
    auto a = analyze(
        "struct D { n <int>\n"
        "    fun stow(p: &int) <noret> { self.n = 0; }\n"
        "}\n"
        "let g <&int>;\n"
        "let keep <int> = 3;\n"
        "fun stowParam(d: &D, out: &&D) <noret> { *out = d; }\n"
        "fun stowGlobal() <noret> { g = &keep; }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.clean()) << "errors: " << a.errorCount;
}

TEST(Soundness_Semantics, RefusesAFrameAddressThroughAForwardingCall) {
    // The call wrapper: `id` forwards its pointer parameter, so `id(&a)` is
    // `return &a` in everything but syntax. The summary pass says which
    // parameter the return carries; the site holds the argument to the
    // operand rule, blamed on the argument that carries the address.
    auto a = analyze(
        "fun id(p: &int) <&int> { return p; }\n"
        "fun leak() <&int> { let a <int> = 7; return id(&a); }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(refusedWith(a, "cannot return the address of local variable 'a'"))
        << "errors: " << a.errorCount;
}

TEST(Soundness_Semantics, RefusesAForwardingCallChain) {
    // Two hops: wrap forwards id's forwarding. The fixed point over the call
    // graph must compose the facts before any body is walked.
    auto a = analyze(
        "fun id(p: &int) <&int> { return p; }\n"
        "fun wrap(q: &int) <&int> { return id(q); }\n"
        "fun leak() <&int> { let a <int> = 7; return wrap(&a); }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(refusedWith(a, "cannot return the address of local variable 'a'"))
        << "errors: " << a.errorCount;
}

TEST(Soundness_Semantics, RefusesAForwardedInterfacePairFedALocal) {
    // The interface spelling of the same route: `fwd` forwards its pair
    // parameter, and `fwd(d)` forms that pair at this site, out of this
    // frame's `d`.
    auto a = analyze(std::string(kSpeaker) +
        "struct D { n <int>\n"
        "    fun speak() <int> { return self.n; }\n"
        "}\n"
        "D implements <Speaker> { }\n"
        "fun fwd(s: Speaker) <Speaker> { return s; }\n"
        "fun leak() <Speaker> { let d <D> = D { n: 7 }; return fwd(d); }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(refusedWith(a, "cannot return local variable 'd' as an interface"))
        << "errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, AcceptsForwardingCallWithCallerOwnedArguments) {
    // The ADR 0055 non-goals through the same route: a pointer parameter's
    // value, a global, a `new`, and the pinned forwarded-pair spelling all
    // stay legal at the call site.
    auto a = analyze(std::string(kSpeaker) +
        "struct D { n <int>\n"
        "    fun speak() <int> { return self.n; }\n"
        "}\n"
        "D implements <Speaker> { }\n"
        "fun fwd(s: Speaker) <Speaker> { return s; }\n"
        "fun id(p: &D) <&D> { return p; }\n"
        "let g <D> = D { n: 1 };\n"
        "fun fromParam(p: &D) <&D> { return id(p); }\n"
        "fun fromGlobal() <&D> { return id(&g); }\n"
        "fun fromHeap() <&D> { return id(new D { n: 3 }); }\n"
        "fun fromPair() <Speaker> { return fwd(g); }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(a.clean()) << "errors: " << a.errorCount;
}

TEST(SemanticAnalyzer, AcceptsForwardingCallAcrossDefinitionOrderAndCycles) {
    // The fixed point's reason: `late` is declared below the call that
    // reaches it, and the `weird` -> `loop` edge points below itself with the
    // seed (`return q`) inside the cycle. Convergence precedes any body, so
    // none of this depends on where the declarations sat: the composed facts
    // refuse exactly the local-address arguments, and the same shapes fed a
    // global stay legal.
    auto a = analyze(
        "fun user(p: &int) <&int> { return late(p); }\n"
        "fun weird(q: &int) <&int> { return loop(q); }\n"
        "fun loop(q: &int) <&int> { return q; }\n"
        "fun late(z: &int) <&int> { return z; }\n"
        "let g <int> = 1;\n"
        "fun leak() <&int> { let a <int> = 7; return weird(&a); }\n"
        "fun ok() <&int> { return user(&g); }\n"
        "fun main() <noret> { }\n");
    ASSERT_TRUE(a.parsed);
    EXPECT_TRUE(refusedWith(a, "cannot return the address of local variable 'a'"))
        << "errors: " << a.errorCount;
    EXPECT_EQ(a.errorCount, 1) << "errors: " << a.errorCount;
}

