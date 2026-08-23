#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "Corpus.hpp"
#include "Pipeline.hpp"
#include "semantics/SemanticAnalyzer.hpp"
#include "types/Type.hpp"

using namespace fin::testing;

namespace {

struct Analysis {
    bool parsed = false;
    bool analyzerFlag = false;
    bool engineErrors = false;
    int errorCount = 0;
    std::shared_ptr<fin::Scope> globals;

    bool clean() const { return parsed && !analyzerFlag && !engineErrors; }
};

// Runs the real SemanticAnalyzer. Nothing in the old suite ever constructed one.
Analysis analyze(const std::string& code) {
    Analysis a;
    static fin::DiagnosticEngine* keep = nullptr;
    auto diag = std::make_unique<fin::DiagnosticEngine>("", "<test>");
    keep = diag.get();
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

TEST(SemanticAnalyzer, RejectsAPrivateStructFieldAccessFromOutside) {
    // Fields are private unless declared `pub`. This records the behaviour the
    // analyzer has today rather than asserting a preference: reading an
    // undecorated field from a free function is `Cannot access private field`.
    auto a = analyze(
        "struct Point { x <int>, y <int> }\n"
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
    EXPECT_TRUE(a.analyzerFlag || a.engineErrors)
        << "interfaces.fin carries `//@ unimplemented`; if it now analyses clean, "
           "promote that expectation to `//@ ok`";
}
