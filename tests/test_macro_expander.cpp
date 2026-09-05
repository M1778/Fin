#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "Pipeline.hpp"
#include "ast/ASTPrinter.hpp"
#include "diagnostics/DiagnosticEngine.hpp"
#include "macros/MacroExpander.hpp"
#include "semantics/Scope.hpp"

using namespace fin::testing;

namespace {

struct Expansion {
    bool parsed = false;
    bool errors = false;
    int errorCount = 0;
    std::string firstMessage;
    std::shared_ptr<fin::Scope> scope;
};

// Runs the real MacroExpander. Nothing in the old suite ever constructed one.
Expansion expand(const std::string& code) {
    Expansion e;
    fin::DiagnosticEngine diag("", "<test>");
    diag.setColorMode(fin::ColorMode::Never);

    auto parsed = parseSource(code, diag);
    e.parsed = parsed.parsed;
    if (!e.parsed) {
        e.errors = diag.hasErrors();
        e.errorCount = diag.getErrorCount();
        if (!diag.getDiagnostics().empty()) e.firstMessage = diag.getDiagnostics().front().message;
        return e;
    }

    auto scope = std::make_shared<fin::Scope>(nullptr);
    fin::MacroExpander expander(diag, scope.get());
    expander.expand(*parsed.ast);

    e.errors = diag.hasErrors();
    e.errorCount = diag.getErrorCount();
    if (!diag.getDiagnostics().empty()) e.firstMessage = diag.getDiagnostics().front().message;
    e.scope = scope;
    return e;
}

} // namespace

TEST(MacroExpander, LeavesAProgramWithNoMacrosAlone) {
    auto e = expand("fun main() <noret> {}\n");
    ASSERT_TRUE(e.parsed);
    EXPECT_FALSE(e.errors) << e.firstMessage;
}

TEST(MacroExpander, RegistersAMacroDeclarationInTheScope) {
    auto e = expand(
        "@macro twice(a) { return quote { a + a; }; }\n"
        "fun main() <noret> {}\n");
    ASSERT_TRUE(e.parsed);
    ASSERT_NE(e.scope, nullptr);
    EXPECT_NE(e.scope->resolveMacro("twice"), nullptr)
        << "a macro declaration must be visible to the expander's scope";
    EXPECT_FALSE(e.errors) << e.firstMessage;
}

TEST(MacroExpander, ExpandsAKnownInvocationWithoutADiagnostic) {
    auto e = expand(
        "@macro twice(a) { return quote { a + a; }; }\n"
        "fun main() <noret> { let x <int> = twice!(3); }\n");
    ASSERT_TRUE(e.parsed);
    EXPECT_FALSE(e.errors) << e.firstMessage;
}

TEST(MacroExpander, RejectsAnUndefinedMacro) {
    auto e = expand("fun main() <noret> { let x <int> = nope!(3); }\n");
    ASSERT_TRUE(e.parsed);
    EXPECT_TRUE(e.errors);
    EXPECT_NE(e.firstMessage.find("Undefined macro"), std::string::npos) << e.firstMessage;
}

TEST(MacroExpander, RejectsTheWrongNumberOfArguments) {
    auto e = expand(
        "@macro twice(a) { return quote { a + a; }; }\n"
        "fun main() <noret> { let x <int> = twice!(1, 2); }\n");
    ASSERT_TRUE(e.parsed);
    EXPECT_TRUE(e.errors);
    EXPECT_NE(e.firstMessage.find("expects"), std::string::npos) << e.firstMessage;
}

TEST(MacroExpander, AcceptsAtLeastMinArgsForAVararg) {
    auto e = expand(
        "@macro many(a...) { return quote { a; }; }\n"
        "fun main() <noret> { let x <int> = many!(1, 2, 3); }\n");
    ASSERT_TRUE(e.parsed);
    EXPECT_FALSE(e.errors) << e.firstMessage;
}

TEST(MacroExpander, RejectsAMacroWhoseBodyDoesNotReturnAQuote) {
    auto e = expand(
        "@macro bad(a) { return a; }\n"
        "fun main() <noret> { let x <int> = bad!(3); }\n");
    ASSERT_TRUE(e.parsed);
    EXPECT_TRUE(e.errors);
    EXPECT_NE(e.firstMessage.find("quote"), std::string::npos) << e.firstMessage;
}

TEST(MacroExpander, TheArmsFormIsASyntaxErrorRatherThanACrash) {
    // `macro f { () => { ... } }` was the arms form, and this test used to assert that
    // *invoking* one was refused rather than crashing. It crashed before that: the arms
    // constructor filled `rules` and left `body` null, the expander read
    // `def->body->statements` with no guard, and finc exited 139 -- not one of the four
    // codes ADR 0009 gives it, so a script reading the status learned neither that the
    // build succeeded nor that it was rejected. Declaring one and never calling it was
    // always fine, which is why no sample found it: none calls one.
    //
    // ADR 0023 step 1 deletes the form rather than refusing it, so the guard this test
    // was written against is gone and the refusal moved a pass earlier. What it protects
    // is unchanged and is the only thing that ever mattered: a program with this text in
    // it does not crash the compiler. It is now rejected at the point the text is read,
    // which is strictly better -- a diagnostic at the declaration rather than at whatever
    // call site happened to reach it -- and `parsed` being false is what says so.
    //
    // The form was deleted rather than kept because `MacroRule::pattern` was one
    // `std::string` and could hold neither `$x:expr` nor `$(...),*`: it was a shell
    // around a pattern language that does not exist. See the comment on
    // `macro_declaration` in parser.y.
    auto e = expand(
        "macro f { () => { 1; } }\n"
        "fun main() <noret> { f!(); }\n");
    EXPECT_FALSE(e.parsed) << "the arms form no longer parses";
    EXPECT_TRUE(e.errors);
}

TEST(MacroExpander, AMacroDeclarationWithoutTheAtIsASyntaxError) {
    // The `@` is mandatory (ADR 0023 step 2), and until the arms form went, the grammar
    // disagreed with itself about it: the parameter form required `AT KW_MACRO` and the
    // arms form forbade it, so `macro_definitions.fin:9`'s own `@macro my_vec {` matched
    // neither production and reported `unexpected LBRACE, expecting LPAREN`. With the
    // arms form deleted there is one macro-declaration production and it requires the
    // `@`, which is how every other declaration modifier in Fin is spelled.
    auto bare = expand(
        "macro twice(a) { return quote { $a + $a; }; }\n"
        "fun main() <noret> { let x <int> = twice!(3); }\n");
    EXPECT_FALSE(bare.parsed) << "a macro declaration needs the `@`";

    // The same text with the `@` is accepted, so what the case above measures is the
    // sigil and not something else in the line.
    auto marked = expand(
        "@macro twice(a) { return quote { $a + $a; }; }\n"
        "fun main() <noret> { let x <int> = twice!(3); }\n");
    ASSERT_TRUE(marked.parsed);
    EXPECT_FALSE(marked.errors) << marked.firstMessage;
}

TEST(MacroExpander, SubstitutesTheArgumentIntoTheExpansion) {
    // The expander clones the quote body and runs SubstitutionVisitor over it, which
    // replaces `$name` where `name` is a parameter (SubstitutionVisitor.cpp:33-41:
    // "Check if identifier starts with $").
    //
    // This test used to write bare `a` in the quote and then record a KNOWN DEFECT that
    // "the argument is not substituted". It was misdiagnosing itself. Substitution works
    // and always did; the body simply did not ask for it, because a bare identifier in a
    // quote is a reference to whatever is in scope where the quote lands and `$a` is the
    // parameter. What the old body demonstrated was the *correct* refusal --
    // `Undefined variable 'a'`, twice, once per mention -- which is what a quote naming
    // something in neither scope should give.
    //
    // Asserted positively now, with the substitution read back through the printer,
    // which is the only reader of the expanded tree available from here.
    fin::DiagnosticEngine diag("", "<test>");
    diag.setColorMode(fin::ColorMode::Never);
    auto parsed = parseSource(
        "@macro twice(a) { return quote { $a + $a; }; }\n"
        "fun main() <noret> { let x <int> = twice!(3); }\n", diag);
    ASSERT_TRUE(parsed.parsed);
    auto scope = std::make_shared<fin::Scope>(nullptr);
    fin::MacroExpander expander(diag, scope.get());
    expander.expand(*parsed.ast);

    testing::internal::CaptureStdout();
    fin::ASTPrinter printer;
    printer.print(*parsed.ast);
    const std::string tree = testing::internal::GetCapturedStdout();

    // The macro *declaration* is left in the tree and still holds its own `$a` -- the
    // expander clones the quote rather than consuming the template -- so the assertion
    // is about the expansion, which is everything from `main` onward.
    const auto atMain = tree.find("FunctionDecl");
    ASSERT_NE(atMain, std::string::npos) << tree;
    const std::string expansion = tree.substr(atMain);

    EXPECT_NE(expansion.find("Literal"), std::string::npos)
        << "`$a` is replaced by the argument, so the expansion holds a literal:\n" << tree;
    EXPECT_EQ(expansion.find("$a"), std::string::npos)
        << "no `$a` survives into the expansion:\n" << tree;
    EXPECT_EQ(expansion.find("ID 'a'"), std::string::npos)
        << "and neither does the bare parameter name:\n" << tree;
}

TEST(MacroExpander, ABareParameterNameInAQuoteIsNotSubstituted) {
    // The other half, and what the test above was accidentally measuring. A quote's bare
    // identifier is a reference resolved where the quote lands, not a parameter, so this
    // expansion reads `a + a` and the analyzer reports it -- which is correct, and is the
    // boundary that makes `$` mean something.
    auto e = expand(
        "@macro twice(a) { return quote { a + a; }; }\n"
        "fun main() <noret> { let x <int> = twice!(3); }\n");
    ASSERT_TRUE(e.parsed);
    EXPECT_FALSE(e.errors) << "the expander itself has nothing to object to:\n"
                           << e.firstMessage;

    fin::DiagnosticEngine diag("", "<test>");
    diag.setColorMode(fin::ColorMode::Never);
    auto parsed = parseSource(
        "@macro twice(a) { return quote { a + a; }; }\n"
        "fun main() <noret> { let x <int> = twice!(3); }\n", diag);
    ASSERT_TRUE(parsed.parsed);
    auto scope = std::make_shared<fin::Scope>(nullptr);
    fin::MacroExpander expander(diag, scope.get());
    expander.expand(*parsed.ast);

    testing::internal::CaptureStdout();
    fin::ASTPrinter printer;
    printer.print(*parsed.ast);
    const std::string tree = testing::internal::GetCapturedStdout();

    // Read from `main` onward, past the macro declaration the expander leaves in place,
    // for the same reason as the test above.
    const auto atMain = tree.find("FunctionDecl");
    ASSERT_NE(atMain, std::string::npos) << tree;
    const std::string expansion = tree.substr(atMain);

    EXPECT_NE(expansion.find("ID 'a'"), std::string::npos)
        << "the bare name survives into the expansion as an identifier:\n" << tree;
    EXPECT_EQ(expansion.find("Literal"), std::string::npos)
        << "and the argument is nowhere in it:\n" << tree;
}

TEST(MacroExpander, IsReusableAcrossTranslationUnits) {
    auto first = expand("fun main() <noret> { let x <int> = nope!(3); }\n");
    ASSERT_TRUE(first.parsed);
    ASSERT_TRUE(first.errors);
    auto second = expand("fun main() <noret> {}\n");
    EXPECT_FALSE(second.errors) << "state leaked from the previous expansion";
}
