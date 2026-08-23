#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Corpus.hpp"
#include "Pipeline.hpp"
#include "ast/decls/ClassDecl.hpp"
#include "ast/decls/Program.hpp"
#include "semantics/SemanticAnalyzer.hpp"
#include "types/StructType.hpp"

// The soundness defects listed at the top of docs/plan.md, each pinned by a test
// that runs the real compiler. They are gathered in one file because they share a
// convention, and the convention is the reason the file exists.
//
// Two suites, and the difference between them is which direction a failure means.
//
//   `Soundness_*`   asserts what the compiler must do. A failure is a regression.
//                   These must pass forever.
//
//   `KnownDefect_*` asserts what the compiler does *wrong today*, verified by
//                   running it. These pass now. **A failure here is good news: it
//                   means the defect was fixed.** The fixer inverts the assertion,
//                   moves the test into `Soundness_*`, and strikes the defect from
//                   docs/plan.md. Every one of them names the correct behaviour in
//                   its failure message, so the flip needs no archaeology.
//
// This is the same discipline the corpus uses for `//@ unimplemented "<reason>"`
// (ADR 0008): record what happens now, so that behaviour changing is what trips the
// alarm. A defect nobody asserts is a defect that gets fixed and un-fixed in
// silence, and a defect asserted as `EXPECT_TRUE(broken)` with no note is a booby
// trap for whoever fixes it.
//
// A `KnownDefect` is not a licence. Each one accepts a wrong program, so each is
// worth more than the feature work queued behind it.

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

// A temp .fin file. test_cli.cpp has its own; duplicated rather than shared
// because these two files have no other reason to be coupled, and the copy is
// nine lines.
class Src {
public:
    explicit Src(const std::string& contents) {
        path_ = uniqueTempPath("fin_sound", ".fin");
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

// The diagnostic message lines, and nothing else.
//
// This exists because searching all of stderr for an identifier is not evidence that
// the identifier was diagnosed. Every rendered diagnostic echoes the offending source
// line under a caret, so `stripAnsi(r.err).find("nosuchvar")` finds the *program's own
// text* whenever the program contains that name -- which, in a test that put the name
// there on purpose, is always. Two tests in this file were written wrong that way, one
// of them going red against a correct compiler and one of them passing vacuously, before
// this helper existed. Assert against this, not against raw stderr.
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

// How many diagnostics were reported. Line-anchored for the same reason.
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

} // namespace

// ---------------------------------------------------------------------------
// An unresolved type must not silence the code around it.
//
// When a type annotation fails to resolve, the analyser drops the entity that was
// being declared -- and everything that referred to it then reports a second time.
// `fun f(a: NoSuchType) <int> { return a + a; }` produces three diagnostics: the
// real one, and `Undefined variable 'a'` once per use. Four declaration kinds do
// this, and they are one rule, not four bugs:
//
//   let x <NoSuchType> = ...      the variable is never defined       (Analyzer_Decl, visit(VariableDeclaration))
//   fun f(a: NoSuchType)          the parameter is never defined      (the parameter loops)
//   struct S { pub v <NoSuchType> }   the member is never defined     -> `Struct 'S' has no member 'v'`
//   fun f() <NoSuchType>          the whole function is dropped       -> `Undefined function or type 'f'`
//
// The variable case is worse than noise. `if (!type) return;` returns before the
// initialiser is visited, so `let x <NoSuchType> = nosuchvar;` reports the type and
// never mentions `nosuchvar`. One bad annotation silently disables analysis of an
// arbitrarily large expression -- wrong code, no diagnostic, which is the category
// the plan puts first. Measured on the corpus: two real errors in
// prototype_test.fin (`Undefined type 'Collection'` and `'HashMap'`) are hidden by
// exactly this and appear the moment it is fixed.
//
// Measured before it was written, by patching the analyser to define the entity
// anyway and re-running the corpus: 335 diagnostics become 317. Twenty-one cascades
// go, two hidden errors arrive. That also means every other unit's ranking-by-count
// is inflated until this lands, which is the second reason to do it early.
//
// The fix is an error type: a single sentinel that is assignable in both directions,
// answers any member, method and index with itself, and is never spellable by a
// program. `auto` looks like a free ride and is not -- a declaration whose annotation
// is `auto` infers from its initialiser, so an unresolved annotation would silently
// become whatever the initialiser was and the program would compile. The sentinel
// prints as `<error>`, which is not an identifier, so a leak into a diagnostic is
// both impossible to write by hand and obvious when seen. One test below watches for
// exactly that leak.
// ---------------------------------------------------------------------------

TEST(Soundness_ErrorRecovery, AnInitialiserIsAnalysedWhenTheAnnotationDoesNotResolve) {
    // The soundness half, and the reason this unit is not merely about noise:
    // `if (!type) return;` skipped the initialiser entirely.
    auto r = compile("fun main() <int> { let x <NoSuchType> = nosuchvar; return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    const std::string err = messagesOnly(stripAnsi(r.err));
    EXPECT_NE(err.find("NoSuchType"), std::string::npos) << err;
    EXPECT_NE(err.find("nosuchvar"), std::string::npos)
        << "a bad annotation must not silence its own initialiser:\n" << err;
}

TEST(Soundness_ErrorRecovery, AnInitialiserCallIsAnalysedWhenTheAnnotationDoesNotResolve) {
    // A call takes a different path from a bare name and reports a different message,
    // so a fix that reached one and not the other would leave this silent.
    auto r = compile("fun main() <int> { let x <NoSuchType> = nosuchfn(); return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    const std::string err = messagesOnly(stripAnsi(r.err));
    EXPECT_NE(err.find("nosuchfn"), std::string::npos)
        << "a bad annotation must not silence a call in its initialiser:\n" << err;
}

TEST(Soundness_ErrorRecovery, AnUnresolvedAnnotationIsStillReportedExactlyOnce) {
    // The control that stops the fix from going the other way: suppressing cascades
    // must not suppress the diagnostic that started it, nor duplicate it.
    auto r = compile("fun main() <int> { let x <NoSuchType> = 0; return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(errorCount(err), 1u) << "exactly the real error, nothing else:\n" << err;
    EXPECT_NE(err.find("Undefined type 'NoSuchType'"), std::string::npos) << err;
}

TEST(Soundness_ErrorRecovery, AnUnresolvedAnnotationDoesNotInferFromItsInitialiser) {
    // Why the sentinel cannot be `auto`. visit(VariableDeclaration) infers when the
    // annotation is `auto`, so reusing it here would make this program compile.
    auto r = compile("fun main() <int> { let x <NoSuchType> = 5; return 0; }\n");
    EXPECT_EQ(r.exitCode, 1)
        << "an unresolved annotation is an error, not a request to infer:\n" << r.err;

    // The exit code alone does not say it: the annotation reports either way, so this
    // test passed just as happily with `auto` as the sentinel. What separates them is
    // what `x` then *is*. Under the sentinel a later use of x is silent; under `auto` it
    // has inferred `int` and the mismatch below appears, making the count 2.
    const std::string err = stripAnsi(compile(
        "fun main() <int> { let x <NoSuchType> = 5; let y <bool> = x; return 0; }\n").err);
    EXPECT_EQ(errorCount(err), 1u)
        << "a second diagnostic here means the annotation inferred a type\n" << err;
}

TEST(Soundness_ErrorRecovery, AUseOfAVariableWithAnUnresolvedTypeDoesNotCascade) {
    // Two uses, so the count distinguishes "one cascade" from "one per use".
    auto r = compile("fun main() <int> { let a <NoSuchType> = 0;\n"
                     "                   let b <int> = a; let c <int> = a; return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("Undefined variable 'a'"), std::string::npos)
        << "`a` was declared; the annotation is what failed:\n" << err;
    EXPECT_EQ(errorCount(err), 1u) << err;
}

TEST(Soundness_ErrorRecovery, AUseOfAParameterWithAnUnresolvedTypeDoesNotCascade) {
    // const.fin's shape: `fun test_3(const a: rptr<int>)` where rptr is unresolvable,
    // and nine of the corpus's thirty-two `Undefined variable` diagnostics are this.
    auto r = compile("fun f(a: NoSuchType) <int> { return a + a; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("Undefined variable 'a'"), std::string::npos) << err;
    EXPECT_EQ(errorCount(err), 1u) << err;
}

TEST(Soundness_ErrorRecovery, AMemberAccessOnAnUnresolvedTypeDoesNotCascade) {
    auto r = compile("fun f(a: NoSuchType) <int> { return a.field; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u) << stripAnsi(r.err);
}

TEST(Soundness_ErrorRecovery, AMethodCallOnAnUnresolvedTypeDoesNotCascade) {
    // const.fin:32 `a.set(10)` -- five of that sample's diagnostics are this shape.
    auto r = compile("fun f(a: NoSuchType) <int> { return a.m(1); }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u) << stripAnsi(r.err);
}

TEST(Soundness_ErrorRecovery, AnIndexOnAnUnresolvedTypeDoesNotCascade) {
    auto r = compile("fun f(a: NoSuchType) <int> { return a[0]; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u) << stripAnsi(r.err);
}

TEST(Soundness_ErrorRecovery, AStructMemberWithAnUnresolvedTypeStillExists) {
    // The member is dropped today, so reading it reports `Struct 'S' has no member 'v'`
    // -- a claim about the program that is false. The member was written; its type was
    // the problem.
    auto r = compile("struct S { pub v <NoSuchType>, }\n"
                     "fun f(s: &S) <int> { let q <int> = s.v; return 0; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("has no member 'v'"), std::string::npos)
        << "the member exists; its type did not resolve:\n" << err;
    EXPECT_EQ(errorCount(err), 1u) << err;
}

TEST(Soundness_ErrorRecovery, AClassMemberWithAnUnresolvedTypeStillExists) {
    // The class copy of the test above. Not redundant with it: the two member loops are
    // separate code (Analyzer_Decl.cpp ~185 and ~648, the largest of this codebase's
    // duplicated loops), and a mutation matrix found this exact hole -- restoring the
    // drop on the class side killed nothing, because every test used a struct.
    auto r = compile("class C { pub v <NoSuchType>, }\n"
                     "fun f(c: &C) <int> { let q <int> = c.v; return 0; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("has no member 'v'"), std::string::npos)
        << "the member exists; its type did not resolve:\n" << err;
    EXPECT_EQ(errorCount(err), 1u) << err;
}

TEST(Soundness_ErrorRecovery, AFunctionWithAnUnresolvedReturnTypeIsStillCallable) {
    // The widest cascade of the four: the function is not registered at all, so every
    // call site reports `Undefined function or type 'f'` on top of the real error.
    auto r = compile("fun f() <NoSuchType> { return 0; }\n"
                     "fun main() <int> { let z <int> = f(); let y <int> = f(); return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("Undefined function or type 'f'"), std::string::npos)
        << "`f` was declared; its return type is what failed:\n" << err;
    EXPECT_EQ(errorCount(err), 1u) << err;
}

TEST(Soundness_ErrorRecovery, TheErrorSentinelNeverAppearsInADiagnostic) {
    // It exists to be suppressed. If it reaches a message, some site propagated it
    // without checking, and the user is being shown a type they cannot have written.
    for (const char* code : {"fun main() <int> { let x <NoSuchType> = 0; let y <int> = x; return 0; }\n",
                             "fun f(a: NoSuchType) <int> { return a.m(1)[0].k; }\nfun main() <int> { return 0; }\n",
                             "struct S { pub v <NoSuchType>, }\nfun f(s: &S) <int> { return s.v; }\nfun main() <int> { return 0; }\n",
                             // Each of these three leaked, and each leaked through a
                             // different hole. A cast printed the sentinel because the
                             // cast rule is hand-rolled and consulted neither
                             // isErrorType nor isCastableTo; naming a function whose
                             // return type failed printed `fn(int) -> <error>` because
                             // isErrorType did not look inside a FunctionType; the
                             // array and nullable spellings are here because the same
                             // wrapping argument applies to them and only the pointer
                             // one had a test.
                             "fun main() <int> { let x <NoSuchType> = 0; let y <float> = cast<float>(x); return 0; }\n",
                             "fun f(a: int) <NoSuchType> { return 0; }\nfun main() <int> { let g <int> = f; return 0; }\n",
                             "fun f(a: [NoSuchType]) <int> { let q <int> = a; return 0; }\nfun main() <int> { return 0; }\n",
                             "fun f(a: NoSuchType?) <int> { let q <int> = a; return 0; }\nfun main() <int> { return 0; }\n"}) {
        const std::string err = stripAnsi(compile(code).err);
        EXPECT_EQ(err.find("<error>"), std::string::npos)
            << "the error sentinel leaked into a diagnostic:\n" << code << err;
    }
}

// The sentinel is permissive by design, which makes every one of these a test that it
// is not permissive about anything else. Each was a real diagnostic before the unit
// and must still be one after.

TEST(Soundness_ErrorRecovery, AMemberAccessOnAPrimitiveIsStillAnError) {
    auto r = compile("fun main() <int> { let x <int> = 1; let y <int> = x.field; return 0; }\n");
    EXPECT_NE(r.exitCode, 0) << "an int has no members:\n" << r.err;
}

TEST(Soundness_ErrorRecovery, AMethodCallOnAPrimitiveIsStillAnError) {
    auto r = compile("fun main() <int> { let x <int> = 1; let y <int> = x.m(); return 0; }\n");
    EXPECT_NE(r.exitCode, 0) << "an int has no methods:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("does not have methods"), std::string::npos) << stripAnsi(r.err);
}

TEST(Soundness_ErrorRecovery, AnIndexOnANonArrayIsStillAnError) {
    auto r = compile("fun main() <int> { let x <int> = 1; let y <int> = x[0]; return 0; }\n");
    EXPECT_NE(r.exitCode, 0) << "an int is not indexable:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("is not an array or pointer"), std::string::npos) << stripAnsi(r.err);
}

TEST(Soundness_ErrorRecovery, ARealTypeMismatchIsStillReported) {
    auto r = compile("fun main() <int> { let x <int> = \"s\"; return 0; }\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    EXPECT_NE(stripAnsi(r.err).find("Type mismatch"), std::string::npos) << stripAnsi(r.err);
}

TEST(Soundness_ErrorRecovery, AnUndefinedNameIsStillReportedWhenNoTypeFailed) {
    // The cascade suppression must key on "this entity's type failed", not on "some
    // type failed somewhere", or one bad annotation would mute the whole file.
    //
    // This is a control against a specific wrong fix, and the matrix spells it out as
    // I-undefname: answering the sentinel from the undefined-variable path in
    // visit(Identifier&) instead of reporting. Nothing in the sentinel's own machinery
    // can reach this test, because a name with no declaration has no type that could
    // have failed -- so the mutant that kills it has to be the mistake itself.
    auto r = compile("fun main() <int> { let a <NoSuchType> = 0; let b <int> = alsonothere; return 0; }\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    EXPECT_NE(messagesOnly(stripAnsi(r.err)).find("alsonothere"), std::string::npos)
        << "an unrelated undefined name is still an error:\n" << stripAnsi(r.err);
}

TEST(Soundness_ErrorRecovery, AMissingStructMemberIsStillReportedWhenNoTypeFailed) {
    // The member-access counterpart of the control above; the mutant is I-nomember.
    auto r = compile("struct S { pub v <int>, }\n"
                     "fun f(s: &S) <int> { return s.nope; }\nfun main() <int> { return 0; }\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    EXPECT_NE(stripAnsi(r.err).find("has no member"), std::string::npos) << stripAnsi(r.err);
}

// ---------------------------------------------------------------------------
// The other eleven declaration sites.
//
// The four tests above cover a variable, a function parameter, a struct member and
// a function return type. resolveTypeOrError is called from fifteen places, and
// the eleven below are the rest: a constructor's parameters (twice over, in the
// signature pass and the body pass), a method's return type, an interface method's
// return type, an extern's return type and parameters, an operator's parameters, a
// @special's parameters, and an implements-block method's parameters.
//
// One test each rather than a loop, because they are not one code path: each site
// registers a different kind of entity, and a fix to one is not a fix to another.
// That is not a guess -- the eight-site mutation matrix for visitParameterDefaults
// showed each site killed by exactly its own test and nothing else. The same
// matrix runs over these.
// ---------------------------------------------------------------------------

TEST(Soundness_ErrorRecovery, AConstructorParameterWithAnUnresolvedTypeKeepsItsArity) {
    // The signature pass used to drop the unresolved parameter from paramTypes, so
    // the constructor advertised zero arguments and calling it with the one the
    // program wrote earned a second, false diagnostic.
    const FincRun r = compile("struct S { S(p: NoSuchType) {} }\n"
                              "fun main() <int> { let s <auto> = S(1); return 0; }\n");
    EXPECT_EQ(stripAnsi(r.err).find("expects 0 arguments"), std::string::npos)
        << "the unresolved parameter still counts toward the arity\n" << stripAnsi(r.err);

    // And the arity is enforced, not merely silent: this is the half that tells a
    // sentinel apart from a shrug.
    const FincRun wrong = compile("struct S { S(p: NoSuchType) {} }\n"
                                  "fun main() <int> { let s <auto> = S(1, 2); return 0; }\n");
    EXPECT_NE(stripAnsi(wrong.err).find("expects 1 arguments, got 2"), std::string::npos)
        << stripAnsi(wrong.err);
}

TEST(Soundness_ErrorRecovery, AMethodWithAnUnresolvedReturnTypeIsStillCallable) {
    // defineMethod was gated on the return type resolving, so calling the method
    // reported "Method 'm' not found" about a method written three lines up.
    const FincRun r = compile(
        "struct S { pub v <int>, fun m(self: &Self) <NoSuchType> { return 0; } }\n"
        "fun main() <int> { let s <&S> = new S{v: 1}; s.m(); return 0; }\n");
    EXPECT_EQ(stripAnsi(r.err).find("not found in type"), std::string::npos)
        << "m is declared; only its return type is unknown\n" << stripAnsi(r.err);

    // The control, in the same program shape: a name that really is not a method
    // must still be reported, or the sentinel has swallowed the check itself.
    const FincRun bad = compile(
        "struct S { pub v <int>, fun m(self: &Self) <NoSuchType> { return 0; } }\n"
        "fun main() <int> { let s <&S> = new S{v: 1}; s.nosuchmethod(); return 0; }\n");
    EXPECT_NE(stripAnsi(bad.err).find("'nosuchmethod' not found"), std::string::npos)
        << stripAnsi(bad.err);
}

TEST(Soundness_ErrorRecovery, AClassMethodWithAnUnresolvedReturnTypeIsStillCallable) {
    // The class copy of the same loop. Its own test because it is its own code:
    // Analyzer_Decl.cpp carries the struct and class member passes twice over, and
    // a mutant that restores the gate in one is not caught by the other's test.
    //
    // Proving that took a change to the matrix, not to this test. The two copies are
    // textually identical, so one substitution mutated both and the struct test killed
    // first, leaving this one in the never-killed list looking redundant. The mutants are
    // now R-methodS and R-methodC, each patching one occurrence, and each is killed by
    // exactly one of the two tests.
    //
    // Splitting the mutants was necessary but not sufficient: R-methodC still killed
    // nothing, because this test used to assert only a diagnostic count over a program
    // whose `main` was `return 0;`. Undeclaring the method changes nothing a program that
    // never calls it can notice. The call is the assertion; the count rides along.
    const FincRun r = compile(
        "class C { pub v <int>, fun m(self: &Self) <NoSuchType> { return 0; } }\n"
        "fun main() <int> { let c <&C> = new C{v: 1}; c.m(); return 0; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("not found in type"), std::string::npos)
        << "m is declared; only its return type is unknown\n" << err;
    EXPECT_EQ(errorCount(err), 1u)
        << "the unresolved return type, once -- the signature pass is quiet and the body\n"
           "pass reports (Soundness_ErrorRecovery.AnAnnotationInAStructSignatureIsReportedOnce)\n"
        << err;

    // No `nosuchmethod` control here, deliberately. The lookup that would have to be
    // swallowed is one site shared by both (Analyzer_Expr.cpp:370), and the struct
    // sibling above already covers it; a second copy would be the redundancy this
    // unit spent a matrix removing.
}

TEST(Soundness_ErrorRecovery, AnInterfaceMethodWithAnUnresolvedReturnTypeDoesNotCrash) {
    // This site was worse than a cascade: `ifaceType->defineMethod(name, retType)`
    // was unguarded, so an unresolved return type handed it a live nullptr. Nothing
    // in the corpus dereferenced it, which is the only reason it had not crashed.
    const FincRun r = compile("interface I { fun m(a: int) <NoSuchType>; }\n"
                              "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "a diagnostic, not a crash and not a pass\n" << r.err;
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u) << stripAnsi(r.err);

    // The declaration alone proves nothing: the null was stored quietly and the program
    // above never read it back, so this test passed before the fix as well. Calling the
    // method through a bounded type parameter is what reads it -- with a null in the
    // map, getMethodReturnType answers "not found" and the call reports a second error
    // about a method the interface plainly requires.
    const std::string used = stripAnsi(compile(
        "interface I { pub fun m() <NoSuchType>; }\n"
        "fun use<T: I>(v: T) <int> { let s <int> = v.m(); return 0; }\n"
        "fun main() <int> { return 0; }\n").err);
    EXPECT_EQ(used.find("not found"), std::string::npos)
        << "the requirement exists; its return type is what failed\n" << used;
    EXPECT_EQ(errorCount(used), 1u) << used;
}

TEST(Soundness_ErrorRecovery, AnExternWithAnUnresolvedReturnTypeIsStillCallable) {
    // `if (!retType) return;` abandoned the whole declaration, so the extern went
    // unregistered and every call to it reported an undefined name. This matters
    // most of all fifteen sites: the standard library is a wall of @define, and
    // one unresolved type in one of them silenced every use.
    const FincRun r = compile("@define ext(a: int) <NoSuchType>;\n"
                              "fun main() <int> { ext(1); return 0; }\n");
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u)
        << "the one undefined type, and nothing about `ext` being undefined\n"
        << stripAnsi(r.err);
}

TEST(Soundness_ErrorRecovery, AnExternWithAnUnresolvedParameterTypeIsStillCallable) {
    const FincRun r = compile("@define ext(a: NoSuchType) <int>;\n"
                              "fun main() <int> { ext(1); return 0; }\n");
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u) << stripAnsi(r.err);
}

TEST(Soundness_ErrorRecovery, AnExternWithAnUnresolvedReturnTypeStillWalksItsDefaults) {
    // The bare `return` skipped visitParameterDefaults too, so this one line
    // silenced the whole feature the previous unit built. Kept as its own test
    // because the two are separately reversible: a fix that registers the extern
    // but keeps an early return above the walk passes every other test here.
    const FincRun r = compile("@define ext(a: int = nosuchvar) <NoSuchType>;\n"
                              "fun main() <int> { return 0; }\n");
    const std::string msgs = messagesOnly(stripAnsi(r.err));
    EXPECT_NE(msgs.find("NoSuchType"), std::string::npos) << msgs;
    EXPECT_NE(msgs.find("nosuchvar"), std::string::npos)
        << "the default is still analysed even though the return type failed\n" << msgs;
}

TEST(Soundness_ErrorRecovery, AnOperatorParameterWithAnUnresolvedTypeIsStillUsable) {
    // The parameter must be *defined*, or the operator body reports an undefined
    // variable for every mention of it.
    //
    // The body mentions `o` twice on purpose. There used to be a second test here with
    // the body `return 0;`, asserting the count was 1, and no mutant could kill it: with
    // the parameter never mentioned, leaving it undefined changes nothing to observe. The
    // count assertion only means something over a body that uses what was dropped, so it
    // moved here and the other test went away.
    const FincRun r = compile(
        "struct S { pub v <int>, operator +(self: &Self, o: NoSuchType) <int> { return o + o; } }\n"
        "fun main() <int> { return 0; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(messagesOnly(err).find("'o'"), std::string::npos)
        << "o is declared; only its type is unknown\n" << err;
    EXPECT_EQ(errorCount(err), 1u) << err;
}

TEST(Soundness_ErrorRecovery, ASpecialDeclarationParameterWithAnUnresolvedTypeIsStillUsable) {
    const FincRun r = compile("@special sp(a: NoSuchType) <int> { return a; }\n"
                              "fun main() <int> { return 0; }\n");
    EXPECT_EQ(messagesOnly(stripAnsi(r.err)).find("'a'"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_ErrorRecovery, AnImplementsBlockMethodParameterWithAnUnresolvedTypeIsStillUsable) {
    const FincRun r = compile(
        "interface I { pub fun m(a: int) <int>; }\n"
        "struct S { val <int> }\n"
        "S implements <I> {\n"
        "    pub fun m(a: NoSuchType) <int> { return a; }\n"
        "}\n"
        "fun main() <int> { return 0; }\n");
    EXPECT_EQ(messagesOnly(stripAnsi(r.err)).find("'a'"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_ErrorRecovery, AConstructorBodyCanUseAParameterWhoseTypeDidNotResolve) {
    // The constructor's parameters are resolved twice -- once to record the arity, once
    // to populate the body scope -- and only the second one makes the parameter usable.
    // The arity test above covers the first; this covers the second, which no test
    // reached: restoring the drop in the body pass alone broke nothing.
    //
    // Asserted as the absence of `'p'` rather than a count, because the two passes
    // report the annotation twice today (KnownDefect_ErrorRecovery, below) and this
    // test must not have to change when that is fixed.
    for (const char* kw : {"struct", "class"}) {
        const std::string code = std::string(kw) + " S {\n"
            "    v <int>,\n"
            "    S(p: NoSuchType) { self.v = p; }\n"
            "}\n"
            "fun main() <int> { return 0; }\n";
        const std::string err = messagesOnly(stripAnsi(compile(code).err));
        EXPECT_EQ(err.find("'p'"), std::string::npos)
            << "the parameter is declared; its type is what failed\n" << kw << "\n" << err;
    }
}

TEST(Soundness_ErrorRecovery, AnOperatorWithAnUnresolvedReturnTypeIsStillDeclared) {
    // The fifth shape of the drop-the-entity rule, and the loudest: with the operator
    // undeclared, `s + 1` fell back to the built-in rule for `+` and reported
    // `Type mismatch: expected 'S', got 'int'` -- which reads as though the program had
    // not declared the operator it plainly did declare.
    const FincRun r = compile(
        "struct S {\n"
        "    v <int>,\n"
        "    pub operator +(other: <int>) <NoSuchType> { return 0; }\n"
        "}\n"
        "fun main() <int> { let s <S> = S{v: 1}; let q <int> = s + 1; return 0; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("expected 'S', got 'int'"), std::string::npos)
        << "the operator is declared; its return type is what failed\n" << err;
}

TEST(Soundness_ErrorRecovery, AnOperatorWithAnUnresolvedReturnTypeIsVisibleToAMethodDeclaredBeforeIt) {
    // The same rule, from the side that pins *which* of the two registrations matters.
    // An operator is registered twice: once in visit(StructDeclaration&)'s signature
    // pass, once when visit(OperatorDeclaration&) walks its body. Only the first has run
    // by the time an earlier sibling's body is analysed, so `self + 1` in a method
    // declared above the operator sees the signature pass or nothing at all -- and with
    // nothing, it fell through to the built-in rule for `+` and said
    // `Type mismatch: expected 'Self', got 'int'`.
    //
    // Written this way round on purpose: the ordinary spelling (the operator first) is
    // covered by whichever registration happens to survive, so it cannot tell the two
    // apart, and a mutation matrix proved it -- dropping either registration alone left
    // every other test in this file green.
    const FincRun r = compile(
        "struct S {\n"
        "    v <int>,\n"
        "    pub fun m() <int> { let q <int> = self + 1; return 0; }\n"
        "    pub operator +(other: <int>) <NoSuchType> { return 0; }\n"
        "}\n"
        "fun main() <int> { return 0; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("Type mismatch"), std::string::npos)
        << "the operator is declared above; the method below it should see it\n" << err;
}

TEST(Soundness_ErrorRecovery, AClassOperatorWithAnUnresolvedReturnTypeIsVisibleToAnEarlierMethod) {
    // The class copy, for the reason spelled out at
    // AClassMethodWithAnUnresolvedReturnTypeIsStillCallable: two identical copies of the
    // loop, one mutant each (R-opretS, R-opretC), one test each.
    const FincRun r = compile(
        "class C {\n"
        "    v <int>,\n"
        "    pub fun m() <int> { let q <int> = self + 1; return 0; }\n"
        "    pub operator +(other: <int>) <NoSuchType> { return 0; }\n"
        "}\n"
        "fun main() <int> { return 0; }\n");
    EXPECT_EQ(stripAnsi(r.err).find("Type mismatch"), std::string::npos) << stripAnsi(r.err);
}

// Was two tests. KnownDefect_Generics.AnOperatorsOwnGenericParameterIsNotInScopeInPassOne
// booked a signature pass that resolved an operator's return type without the operator's
// own generic parameters in scope: `operator + : <T>(other: <T>) <T>` reported
// "Undefined type 'T'" about a parameter it declares one token earlier, and registered the
// operator returning the error sentinel. When the pass was fixed the KnownDefect was
// inverted into Soundness_Generics.AnOperatorsOwnGenericParameterIsInScopeInPassOne,
// asserting that the program compiles clean.
//
// That test was unkillable, and this is where it went. Two reasons, both general:
//
//   * The signature pass is quiet (SemanticAnalyzer::QuietPass), so the diagnostic is gone
//     whether or not the scope is right. Mutant D-opgen reverts the fix and the inverted
//     test stayed green.
//   * A sentinel registration is silent *by construction* -- checkType stops before it
//     compares against the sentinel -- so "this program compiles clean" is satisfied by
//     registering nothing at all. Any assertion of the form "no diagnostic" is blind to
//     the failure it was written for.
//
// So the claim has to be read off the registration instead: call the operator and check
// the type of the call. `errorCount == 0` survives below as one case among several, and it
// is deliberately not the assertion the test rests on.
TEST(Soundness_Generics, AnOperatorsRegisteredReturnTypeIsWhatItsCallIsTyped) {
    // The declaration on its own, which must not report -- kept from the inverted test.
    // See above for why this assertion cannot detect the defect it came from.
    EXPECT_EQ(errorCount(stripAnsi(compile(
        "struct S {\n"
        "    v <int>,\n"
        "    operator + : <T>(other: <T>) <T> { return other; }\n"
        "}\n"
        "fun main() <int> { return 0; }\n").err)), 0u)
        << "the operator declares T one token before it uses it";

    // The corpus spelling, where the parameter uses T and the return type does not
    // (tests/samples/operators.fin:15). That one was clean even before the fix, which is
    // why the defect survived being written down.
    EXPECT_EQ(errorCount(stripAnsi(compile(
        "struct S {\n"
        "    v <int>,\n"
        "    operator + : <T>(other: <T>) <int> { return 0; }\n"
        "}\n"
        "fun main() <int> { return 0; }\n").err)), 0u)
        << "the corpus's own generic operator spelling";

    // The non-generic case first, to fix what "typed as its return type" means: the
    // operator returns int, the annotation says string, one mismatch.
    const FincRun r = compile(
        "struct S {\n"
        "    v <int>,\n"
        "    operator -(other: <int>) <int> { return 0; }\n"
        "}\n"
        "fun main() <int> {\n"
        "    let s <S> = S{v: 1};\n"
        "    let r <string> = s - 1;\n"
        "    return 0;\n"
        "}\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(errorCount(err), 1u) << err;
    EXPECT_NE(err.find("expected 'string', got 'int'"), std::string::npos)
        << "the operator's own return type, not the operand's and not the struct's\n" << err;

    // The generic case, which is the one that needs the signature pass to have declared
    // T. This asserts only that *some* real type was registered and reported -- what T
    // should have been narrowed to is a separate question, booked in
    // KnownDefect_Generics.AnOperatorsGenericParameterIsNotInferredFromItsOperand.
    // Silence is the failure mode being watched for here: an operator registered
    // returning the sentinel makes this program compile clean.
    const FincRun g = compile(
        "struct S {\n"
        "    v <int>,\n"
        "    operator + : <T>(other: <T>) <T> { return other; }\n"
        "}\n"
        "fun main() <int> {\n"
        "    let s <S> = S{v: 1};\n"
        "    let r <string> = s + 1;\n"
        "    return 0;\n"
        "}\n");
    const std::string gerr = stripAnsi(g.err);
    EXPECT_EQ(errorCount(gerr), 1u)
        << "an operator registered returning the sentinel would compile this clean\n" << gerr;
    EXPECT_EQ(gerr.find("<error>"), std::string::npos)
        << "the signature pass registered the sentinel: it resolved <T> without T in scope\n"
        << gerr;
}

TEST(KnownDefect_Generics, AnOperatorsGenericParameterIsNotInferredFromItsOperand) {
    // `s + 1` calls `operator + : <T>(other: <T>) <T>` with an int, so T is int and the
    // call is an int. The analyser reports the mismatch against the *unsubstituted*
    // parameter instead, printing a type name that exists only inside the operator's own
    // declaration: `expected 'string', got 'T'`.
    //
    // A function call has the same shape and the same gap -- there is no inference from
    // arguments to generic parameters anywhere yet, which is why this is booked here
    // rather than fixed: the fix is a unification pass shared by calls, method calls and
    // operators, and it wants its own tests.
    const FincRun r = compile(
        "struct S {\n"
        "    v <int>,\n"
        "    operator + : <T>(other: <T>) <T> { return other; }\n"
        "}\n"
        "fun main() <int> {\n"
        "    let s <S> = S{v: 1};\n"
        "    let r <string> = s + 1;\n"
        "    return 0;\n"
        "}\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_NE(err.find("got 'T'"), std::string::npos)
        << "GOOD NEWS: an operator's generic parameter is inferred from its operand.\n"
           "The diagnostic should now read \"expected 'string', got 'int'\" -- move that\n"
           "assertion into Soundness_Generics.AnOperatorsRegisteredReturnTypeIsWhatItsCall-\n"
           "IsTyped, which currently asserts only that the type is not the sentinel, and\n"
           "delete this test.\n"
        << err;
}

TEST(Soundness_ErrorRecovery, ACastOfAnOperandThatDidNotTypeKeepsItsTargetType) {
    // The cast rule is hand-rolled in visit(CastExpression&) and consults neither
    // isErrorType nor the isCastableTo family, so it was the one expression site the
    // sentinel did not reach: `cast<float>(x)` reported an invalid cast from a type the
    // program never wrote. The result stays the *target* type -- a cast asserts a type
    // and that assertion is still readable when its operand is not.
    const FincRun r = compile(
        "fun main() <int> {\n"
        "    let x <NoSuchType> = 0;\n"
        "    let y <float> = cast<float>(x);\n"
        "    return 0;\n"
        "}\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("Invalid cast"), std::string::npos) << err;
    EXPECT_EQ(errorCount(err), 1u) << err;

    // ...and the assertion is still checked against its use.
    const std::string bad = stripAnsi(compile(
        "fun main() <int> {\n"
        "    let x <NoSuchType> = 0;\n"
        "    let y <bool> = cast<float>(x);\n"
        "    return 0;\n"
        "}\n").err);
    EXPECT_NE(bad.find("expected 'bool', got 'float'"), std::string::npos)
        << "the cast still says what the value is\n" << bad;
}

TEST(Soundness_ErrorRecovery, ATypeAliasWhoseTargetDidNotResolveStillNamesSomething) {
    // The twenty-first site, and the largest cascade in the corpus by a factor of four:
    // `stdlib/operators.fin` writes `type Output = Any<...>;` on line 6 and then names
    // `Output` as the return type of thirty operator requirements. `Any` is undefined,
    // so the alias was not defined, so all thirty reported -- 30 of that sample's 57
    // diagnostics, from one annotation.
    //
    // An alias is a declaration like any other: the name was written, and it is the
    // target that failed.
    const FincRun r = compile("type Alias = NoSuchType;\n"
                              "fun f(a: Alias) <int> { return 0; }\n"
                              "fun g(b: Alias) <int> { return 0; }\n"
                              "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("Undefined type 'Alias'"), std::string::npos)
        << "the alias is declared; its target is what failed\n" << err;
    EXPECT_EQ(errorCount(err), 1u) << err;
}

// ---------------------------------------------------------------------------
// `any` and `object`: the two dynamic types.
//
// Neither is declared anywhere in the corpus, and both are *used* there, which is
// what makes them builtins rather than library names. The evidence is an absence:
// `nullifier.fin` writes `let mibombo? <any> = null;` and `literal_struct.fin`
// writes `fun make_default<T: any implements Struct>` with **zero imports** in
// either file, and `prototype_test.fin` writes `<{object, object}>` importing only
// `HashMap` and `Collection`. A name a file uses without importing it is a name the
// compiler owes.
//
// `Any` is the opposite case and the control below pins it: `stdlib/types.fin:69`
// declares `pub type Any = any;`, so `Any` is a library alias of the builtin and a
// program that has not imported it must still be told so. Registering `any` and
// getting `Any` for free would compile programs here that a real standard library
// would reject.
//
// The two are distinct, and the corpus says how. `any` is compile-time erasure --
// "any type that is visible in compile time (can be identified in compile time)"
// (types.fin:97) -- while `object` is boxing: "an expensive type but can fit any
// datatype in it at the cost of memory and speed" (prototype_test.fin:40). Nothing
// below distinguishes their *behaviour*, because nothing in the analyser can yet:
// the difference is what the code generator emits. They are registered as two names
// so that the day it matters the names are already right, in the same spirit as the
// `$type` family (Analyzer_Core.cpp) -- registering a name is not deciding its
// semantics.
// ---------------------------------------------------------------------------

TEST(Soundness_DynamicTypes, AnyIsABuiltinAndNeedsNoImport) {
    // nullifier.fin:34 and literal_struct.fin:4, both with no import statement.
    const FincRun r = compile("fun main() <int> { let x <any> = 5; return 0; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("Undefined type 'any'"), std::string::npos)
        << "two corpus samples write `any` with no imports at all\n" << err;
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_DynamicTypes, ObjectIsABuiltinAndNeedsNoImport) {
    // prototype_test.fin:40 writes `<{object, object}>`, importing only `HashMap`
    // and `Collection` -- neither of which declares `object`.
    const FincRun r = compile("fun main() <int> { let x <object> = 5; return 0; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("Undefined type 'object'"), std::string::npos) << err;
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_DynamicTypes, AnyIsSpellableInEveryPositionTheCorpusWritesIt) {
    // One program per position rather than one test per position: the failure mode
    // being guarded against is a name registered in the scope but unreachable from
    // one syntactic form, and a single program shows which form in its diagnostic.
    //
    //   parameter          stdlib/types.fin:90   `@special _resolve_type(v: any)`
    //   array element      stdlib/types.fin:102  `resolve_arr_type(const &arr: [any])`
    //   prototype halves   stdlib/prototypes.fin:19 `typeof_keys(prtp: {any, any})`
    //   return type        implied by `resolve_type` returning what it was handed
    //   alias target       stdlib/types.fin:69   `pub type Any = any;`
    //   generic bound      literal_struct.fin:4  `make_default<T: any implements Struct>`
    //   operator parameter stdlib/operators.fin:9 `pub operator ==(other: any)`
    const FincRun r = compile(
        "type Aliased = any;\n"
        "fun byvalue(v: any) <int> { return 0; }\n"
        "fun byarray(a: [any]) <int> { return 0; }\n"
        "fun byproto(p: {any, any}) <int> { return 0; }\n"
        "fun returns() <any> { return 5; }\n"
        "fun bounded<T: any>(v: T) <int> { return 0; }\n"
        "struct S { pub v <int>, operator +(other: any) <int> { return 0; } }\n"
        "fun main() <int> { return 0; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("Undefined type 'any'"), std::string::npos) << err;
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_DynamicTypes, EveryConcreteTypeIsAssignableToAny) {
    // The whole point of the type. `stdlib/operators.fin` declares thirty
    // requirements taking `(other: any)`, which means every operand of every
    // operator in the language reaches an `any` parameter.
    const FincRun r = compile(
        "struct S { pub v <int> }\n"
        "fun take(v: any) <int> { return 0; }\n"
        "fun main() <int> {\n"
        "  let s <&S> = new S{v: 1};\n"
        "  take(1); take(\"str\"); take(true); take(1.5); take(s);\n"
        "  let a <any> = 1; let b <any> = \"str\"; let c <any> = true;\n"
        "  return 0;\n"
        "}\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(errorCount(err), 0u)
        << "nothing may fail to fit `any`; a target that rejects a value is not `any`\n"
        << err;
}

TEST(Soundness_DynamicTypes, AnArrayOfAnyAcceptsAnArrayOfInt) {
    // `stdlib/types.fin:102` takes `const &arr: [any]`, which is useless if the
    // only thing assignable to `[any]` is another `[any]`.
    //
    // This is the recursive reach of the rule, and it is a separate assertion
    // because it is a separate mechanism: ArrayType::isAssignableTo asks the
    // element types, so a rule written only at the top level of a comparison
    // answers for `any` and not for `[any]`.
    const FincRun r = compile(
        "fun take(a: [any]) <int> { return 0; }\n"
        "fun main() <int> { let v <[int]> = [1, 2]; return take(v); }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_DynamicTypes, TwoArraysWithAssignableElementsAreAssignable) {
    // Found while writing the test above, and independent of `any`: an array was
    // assignable to another array only when it was *fixed-size* and the target was
    // not (ArrayType.cpp). Two dynamic arrays of the same element type went through
    // `equals` in the base and nothing else, so `[int] -> [auto]` -- the same shape
    // with a more permissive element -- was rejected.
    //
    // Kept as its own test because it is its own rule: whoever narrows the `any`
    // rule must not be able to take this with it.
    const FincRun r = compile(
        "fun take(a: [auto]) <int> { return 0; }\n"
        "fun main() <int> { let v <[int]> = [1, 2]; return take(v); }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(errorCount(err), 0u) << err;

    // The control, because "assignable elements" is the load-bearing half: an
    // element type that does not fit must still fail, or arrays have stopped
    // being checked at all.
    const FincRun bad = compile(
        "fun take(a: [string]) <int> { return 0; }\n"
        "fun main() <int> { let v <[int]> = [1, 2]; return take(v); }\n");
    EXPECT_NE(stripAnsi(bad.err).find("Type mismatch"), std::string::npos)
        << "[int] does not fit [string]\n" << stripAnsi(bad.err);
}

TEST(Soundness_Pointers, AVoidPointerIsAssignableInBothDirections) {
    // Written to make a *deletion* safe rather than to catch a new defect: both of
    // these compile today, and the rule that allows them is written twice --
    // Type::isAssignableTo has a `&T -> &void` arm, and PointerType::isAssignableTo
    // has the same arm plus the reverse and an element-wise recursion.
    //
    // The base copy is reachable only from inside the override (`this->as<PointerType>()`
    // needs a pointer receiver, and the override's first line is the only qualified
    // call), and it accepts a strict subset of what the override accepts. So it is
    // dominated, and dominated code is not free: a mutation of the *override's* rule
    // kills nothing while the base arm answers first, which is how the array copy of
    // this same duplication was found (see the note on Soundness_Arrays
    // .AFixedListInitialisesADynamicArrayOfTheSameElementType).
    for (const char* code : {
            "fun t(p: &void) <int> { return 0; }\n"
            "fun main() <int> { let x <int> = 1; return t(&x); }\n",
            "fun t(p: &int) <int> { return 0; }\n"
            "fun main() <int> { let x <int> = 1; let v <&void> = &x; return t(v); }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(errorCount(stripAnsi(r.err)), 0u) << code << stripAnsi(r.err);
    }

    // The control, because "a void pointer converts freely" must not become "a
    // pointer converts freely". stdlib depends on the first and nothing wants the
    // second.
    const FincRun bad = compile(
        "fun t(p: &string) <int> { return 0; }\n"
        "fun main() <int> { let x <int> = 1; return t(&x); }\n");
    EXPECT_NE(stripAnsi(bad.err).find("Type mismatch"), std::string::npos)
        << "&int does not fit &string\n" << stripAnsi(bad.err);
}

TEST(Soundness_DynamicTypes, AnyDoesNotInferFromItsInitialiser) {
    // The distinction from `auto`, and the reason `any` cannot be implemented by
    // aliasing it. `let x <auto> = 5;` makes `x` an `int`, so the next line reports
    // `got 'int'`. `any` is a type, not an instruction to infer one: `x` is an
    // `any`, and a diagnostic about it has to say so.
    //
    // Asserted on the message text rather than on the count because both spellings
    // produce exactly one diagnostic here. The count cannot tell them apart, and a
    // test that cannot tell the wrong answer from the right one is not a test --
    // the same lesson as Soundness_ErrorRecovery.AnUnresolvedAnnotationDoesNot
    // InferFromItsInitialiser, which had this exact hole.
    const FincRun r = compile(
        "fun main() <int> { let x <any> = 5; let y <string> = x; return 0; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_NE(err.find("got 'any'"), std::string::npos)
        << "`x` is an `any`, not the `int` it was initialised from\n" << err;
    EXPECT_EQ(err.find("got 'int'"), std::string::npos)
        << "that is what `auto` would say\n" << err;
}

TEST(Soundness_DynamicTypes, AnyIsNotImplicitlyAssignableToAConcreteType) {
    // One direction only, and the corpus says which. `stdlib/types.fin:74`: "ANY
    // type is allowed to be passed but it has to be casted or handled by the user
    // itself". A dynamic type that flows back out into a concrete slot for free is
    // not a dynamic type, it is a hole -- every `(other: any)` operator parameter
    // in stdlib/operators.fin would become assignable to everything.
    const FincRun r = compile(
        "fun take(v: any) <int> { let n <int> = v; return n; }\n"
        "fun main() <int> { return take(1); }\n");
    EXPECT_NE(stripAnsi(r.err).find("Type mismatch"), std::string::npos)
        << "an `any` reaching an `int` slot needs a cast\n" << stripAnsi(r.err);
}

TEST(Soundness_DynamicTypes, AnyDoesNotAcceptNull) {
    // nullifier.fin:36 states the rule in the sample: "type any cannot be null".
    //
    // It is the one case where "everything is assignable to `any`" has to be read as
    // "every *type* is". `null` is not a value of a type, it is the absence of one,
    // and NullType::isAssignableTo falls through to the base -- so the rule stated as
    // "a target that is `any` accepts anything" silently accepts null as well. The
    // `!this->as<NullType>()` in Type.cpp is what this test is here for.
    //
    // Asserted in *assignment* position, and that is not a weaker choice than the
    // declaration the sample writes -- it is the only position where the rule is
    // decidable at all. A declaration initialised to `null` is accepted whatever it
    // is annotated (checkInitializer, Analyzer_Core.cpp:401), because deeptest4.fin:6
    // writes `integer <int> = null` and stdlib/error.fin:11 writes `err_code: int =
    // null` and neither is nullable. So `let x <any> = null;` is clean for a reason
    // that has nothing to do with `any`, and asserting on it would have tested
    // checkInitializer while claiming to test this rule.
    const char* targets[] = {"any", "object"};
    for (const char* ty : targets) {
        const FincRun r = compile(std::string("fun main() <int> { let x <") + ty +
                                  "> = 5; x = null; return 0; }\n");
        const std::string err = stripAnsi(r.err);
        EXPECT_NE(err.find(std::string("expected '") + ty + "', got 'null'"), std::string::npos)
            << ty << " cannot be null -- nullifier.fin:36\n" << err;
    }

    // Control, and the reason the exclusion is on NullType and not on the target:
    // null is not banned from every target, only from one that did not ask for it.
    const FincRun nullable = compile(
        "fun main() <int> { let x? <int> = 5; x = null; return 0; }\n");
    EXPECT_EQ(errorCount(stripAnsi(nullable.err)), 0u)
        << "`int?` asked to hold null\n" << stripAnsi(nullable.err);

    // And `any?` is spellable: nullifier.fin:34 writes `let mibombo? <any> = null;`
    // in the sample as ordinary code. The type that "cannot be null" can still be
    // wrapped in the type that can, which is what makes the rule above about `any`
    // and not about a prohibition on the word.
    const FincRun ok = compile("fun main() <int> { let mibombo? <any> = null; return 0; }\n");
    EXPECT_EQ(errorCount(stripAnsi(ok.err)), 0u)
        << "nullifier.fin:34 writes exactly this\n" << stripAnsi(ok.err);
}

TEST(KnownDefect_Nullability, DenullifyingANullableAnyIsNotAnError) {
    // The other half of nullifier.fin:34-36, which the Soundness test above
    // deliberately does not claim:
    //
    //     let mibombo? <any> = null;
    //     let _ <any> = mibombo?;   // "this should be an error since type any
    //                               //  cannot be null"
    //
    // The sample requires a diagnostic on the second line and gives the reason, but
    // not the culprit, and the two candidates are different fixes. Either `any?` is
    // not a legal type -- in which case the diagnostic belongs on line 34, which the
    // sample writes without comment -- or postfix `?` on an `any?` is what cannot be
    // typed, in which case line 34 stands and the denullify is the error. Choosing is
    // an owner ruling (docs/plan.md, Rulings owed: the nullability edges), so this
    // books the state instead of guessing at the shape of a diagnostic.
    //
    // Note also that this file's `//@ unimplemented` note expected `Undefined type
    // 'any'` at 36:12; that diagnostic is gone now, which is what promoted this line
    // from "blocked behind `any`" to an open question.
    const FincRun r = compile(
        "fun main() <int> { let mibombo? <any> = null; let _ <any> = mibombo?; return 0; }\n");
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 0u)
        << "when this fails, the ruling has been made: turn it into "
           "Soundness_Nullability and assert the ruled diagnostic\n"
        << stripAnsi(r.err);

    // Control: denullifying a nullable *concrete* type is not in question and must
    // stay clean whichever way the ruling goes.
    const FincRun ctl = compile(
        "fun main() <int> { let m? <int> = null; let _ <int> = m?; return 0; }\n");
    EXPECT_EQ(errorCount(stripAnsi(ctl.err)), 0u)
        << "`int?` denullifies -- nullifier.fin:29\n" << stripAnsi(ctl.err);
}

TEST(Soundness_DynamicTypes, AnUnknownTypeNameIsStillUndefined) {
    // The control for the wrong fix. Two names are registered, not a rule that
    // shrugs at unknown ones, and the third case is the one that matters most:
    // `Any` is *library* -- `stdlib/types.fin:69` declares `pub type Any = any;`
    // behind `#[export]` -- so a program that has not imported it must be told.
    // Handing it out with `any` would compile files here that a real standard
    // library rejects, which is the worst kind of green.
    for (const char* name : {"anyy", "objekt", "Any", "Object", "AnyType"}) {
        const FincRun r = compile(
            std::string("fun main() <int> { let x <") + name + "> = 5; return 0; }\n");
        EXPECT_NE(stripAnsi(r.err).find(std::string("Undefined type '") + name + "'"),
                  std::string::npos)
            << name << " is not a builtin\n" << stripAnsi(r.err);
    }
}

TEST(Soundness_DynamicTypes, ATypeAliasWithAnImplementsBoundIsNotAFabricatedStruct) {
    // `visit(TypeDefinition&)` answered an `implements` bound by fabricating
    // `StructType("any")` -- a struct with no fields, no methods, no relation to
    // the interface, and a `toString()` of `any`. It was the worst available answer
    // because it was authoritative: `type E = any implements <I>; let x <E> = 5;`
    // reported `Type mismatch: expected 'any', got 'int'`, naming a type the
    // program never wrote as a concrete type, and `v.nosuch` reported `Struct
    // 'any' has no member 'nosuch'` about a struct that does not exist.
    //
    // What the bound *means* -- whether an `int` satisfies `implements <I>` -- is a
    // separate question and an owner ruling (docs/plan.md, "Rulings owed"). This
    // test asserts only that the answer is not a fiction, which is true under
    // every reading of the bound.
    const FincRun r = compile(
        "interface I { fun m(self: &Self) <int>; }\n"
        "type E = any implements <I>;\n"
        "fun f(v: E) <int> { return v.nosuch; }\n"
        "fun main() <int> { return 0; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("Struct 'any' has no member"), std::string::npos)
        << "there is no struct named `any` in this program\n" << err;
    EXPECT_EQ(err.find("Undefined type 'E'"), std::string::npos)
        << "`E` is declared on line 2; dropping it is the cascade this project "
           "spent a unit removing\n" << err;
}

TEST(Soundness_DynamicTypes, ABoundIsRenderedInTheDiagnosticThatQuotesTheType) {
    // The bound is stored on DynamicType rather than dropped, and this is what that
    // buys: the type in a diagnostic is a string the reader can find in their own
    // source. `type Bound = any implements <I>;` renders as `any implements <I>`, not
    // as a bare `any` that matches three other aliases in stdlib/types.fin, and not
    // as the fabricated struct that used to render as `any` while claiming to be one.
    //
    // Reached through the *concrete* direction, because that is the only direction
    // that produces a mismatch: everything is assignable to a dynamic type, so a
    // diagnostic quoting one can only come from it being the source.
    const FincRun r = compile(
        "interface I { pub fun m() <int>; }\n"
        "type Bound = any implements <I>;\n"
        "fun main() <int> { let a <Bound> = 5; let b <int> = a; return 0; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_NE(err.find("got 'any implements <I>'"), std::string::npos)
        << "the bound the program wrote is part of the type's name\n" << err;
}

TEST(KnownDefect_DynamicTypes, AnImplementsBoundOnADynamicTypeIsNotEnforced) {
    // `type EnumType = any implements <Enum>;` (stdlib/enums.fin:6) reads as "any
    // type that implements Enum". Only the first half is enforced: the bound is
    // resolved and recorded on the DynamicType, and nothing consults it, so a struct
    // that implements nothing is accepted where the bound was written.
    //
    // Booked rather than fixed because enforcing it needs the answer to a question
    // the corpus does not settle -- stdlib/types.fin:78 writes `type nullptr = any
    // implements <&void>;`, where the bound is a *pointer type* and not an interface
    // at all, and stdlib/typing.fin:10 writes `string | any implements <Error>` inside
    // a union. A check that only understands interfaces would reject both of those
    // spellings, which the corpus writes as ordinary library code. See docs/plan.md,
    // "Rulings owed": does a primitive satisfy an interface bound.
    const char* prog =
        "interface I { pub fun m() <int>; }\n"
        "struct Good { pub v <int> }\n"
        "Good implements <I> { pub fun m() <int> { return self.v; } }\n"
        "struct Bad { pub v <int> }\n"
        "type Bound = any implements <I>;\n";

    // The satisfying case must stay clean whichever way the ruling goes.
    const FincRun good = compile(std::string(prog) +
        "fun main() <int> { let a <Bound> = new Good{v: 1}; return 0; }\n");
    EXPECT_EQ(errorCount(stripAnsi(good.err)), 0u)
        << "Good implements I\n" << stripAnsi(good.err);

    // These two are the defect. When either starts failing, the bound is enforced:
    // split this test, keep the `Good` case as Soundness_DynamicTypes, and assert
    // the ruled diagnostic on whichever of the two the ruling covers.
    const FincRun bad = compile(std::string(prog) +
        "fun main() <int> { let a <Bound> = new Bad{v: 1}; return 0; }\n");
    EXPECT_EQ(errorCount(stripAnsi(bad.err)), 0u)
        << "Bad implements nothing and is accepted today\n" << stripAnsi(bad.err);

    const FincRun prim = compile(std::string(prog) +
        "fun main() <int> { let a <Bound> = 5; return 0; }\n");
    EXPECT_EQ(errorCount(stripAnsi(prim.err)), 0u)
        << "an int where `implements <I>` was written -- this is the case the ruling "
           "is about, since `implements <&void>` in stdlib/types.fin:78 means the "
           "answer cannot simply be no\n" << stripAnsi(prim.err);
}

TEST(KnownDefect_DynamicTypes, AnImplementsBoundOnANonDynamicAliasIsDropped) {
    // The bound is attached only when the alias target is `any` or `object`, because
    // that is the only shape the corpus writes it on -- except literal_struct.fin:15,
    // `type PlayerStructLike = Struct implements <_PlayerStructLike>;`, where the
    // target is `Struct` and the whole construct is part of the unimplemented struct
    // literal feature.
    //
    // On any other target the bound is resolved and then discarded. Resolving it is
    // deliberate and is what the second half of this test pins: an unenforced bound
    // that also fails to reject a misspelling is not a partial implementation, it is
    // a blind spot, and that was the state before this unit.
    const FincRun r = compile(
        "interface I { pub fun m() <int>; }\n"
        "type F = int implements <I>;\n"
        "fun main() <int> { let x <F> = 5; return 0; }\n");
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 0u)
        << "`F` is a plain `int` today; the bound went nowhere\n" << stripAnsi(r.err);

    // The alias is still the alias -- dropping the bound must not have dropped the
    // target with it.
    const FincRun mismatch = compile(
        "interface I { pub fun m() <int>; }\n"
        "type F = int implements <I>;\n"
        "fun main() <int> { let x <F> = \"s\"; return 0; }\n");
    EXPECT_NE(stripAnsi(mismatch.err).find("expected 'int', got 'string'"), std::string::npos)
        << "F resolves to int\n" << stripAnsi(mismatch.err);

    // A typo in a dropped bound is still reported. This half is Soundness in spirit
    // and lives here so that a fix to the paragraph above cannot quietly take it out.
    const FincRun typo = compile(
        "type F = int implements <NoSuchIface>;\n"
        "fun main() <int> { let x <F> = 5; return 0; }\n");
    EXPECT_NE(stripAnsi(typo.err).find("Undefined type 'NoSuchIface'"), std::string::npos)
        << "a bound is resolved even where it is not kept\n" << stripAnsi(typo.err);
}

TEST(Soundness_DynamicTypes, AnyWithGenericArgumentsIsStillDynamic) {
    // The second fabrication site, and the same mistake as the first.
    // resolveTypeUnwrapped's generics arm ends in `type =
    // std::make_shared<StructType>(node->name, args)` for any resolved type that is
    // not a StructType, so `any<int>` came back as a struct named `any` -- which
    // rejected every value (`Type mismatch: expected 'any<int>', got 'int'`) and
    // answered member access with `Struct 'any' has no member`, the exact fiction
    // ATypeAliasWithAnImplementsBoundIsNotAFabricatedStruct forbids on the alias path.
    //
    // This is not a hypothetical spelling. stdlib/types.fin:74 declares `type Any<...>
    // = any implements <...>;` and the library uses `Any<...>` as a generic bound, so
    // every use of it went through this arm.
    const FincRun r = compile("fun main() <noret> { let x <any<int>> = 5; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("expected 'any<int>'"), std::string::npos)
        << "a dynamic type with generic arguments is still dynamic\n" << err;
    EXPECT_EQ(errorCount(err), 0u) << err;

    const FincRun mem = compile(
        "fun f(v: any<int>) <int> { return v.nosuch; }\n"
        "fun main() <noret> { }\n");
    EXPECT_EQ(stripAnsi(mem.err).find("Struct 'any' has no member"), std::string::npos)
        << "there is no struct named `any` in this program either\n" << stripAnsi(mem.err);

    // `object` is the same production and must not be fixed only for `any`.
    const FincRun obj = compile("fun main() <noret> { let x <object<int>> = 5; }\n");
    EXPECT_EQ(errorCount(stripAnsi(obj.err)), 0u) << stripAnsi(obj.err);
}

TEST(Soundness_DynamicTypes, AnyInsideAGenericSurvivesInstantiation) {
    // `DynamicType::substitute` had no caller anywhere in this suite or the corpus,
    // proven rather than argued: replacing its body with `std::abort()` and running
    // every test in the binary killed nothing. A mutant that kills nothing says some
    // rule has no test able to see it, and this one was the plainest of the five
    // reasons -- the test was simply missing.
    //
    // Two things have to hold when a generic carrying `any` is instantiated, and they
    // pull in opposite directions. The parameter must be replaced, or the instantiation
    // did nothing. The `any` must NOT be, or `any` has silently become whatever type
    // the neighbouring parameter was bound to -- which is what a substitute() that
    // returned its bound, or a fabricated struct, would do.
    const char* decl =
        "struct Pair<T> {\n"
        "    pub k <any>\n"
        "    pub v <T>\n"
        "}\n";

    const FincRun sub = compile(std::string(decl) +
        "fun main() <noret> { let p <Pair<int>>; p.v = \"s\"; }\n");
    EXPECT_NE(stripAnsi(sub.err).find("expected 'int', got 'string'"), std::string::npos)
        << "T was substituted: the field is an int in Pair<int>\n" << stripAnsi(sub.err);

    const FincRun kept = compile(std::string(decl) +
        "fun main() <noret> { let p <Pair<int>>; let n <int> = p.k; }\n");
    EXPECT_NE(stripAnsi(kept.err).find("expected 'int', got 'any'"), std::string::npos)
        << "and `any` was not: it is still `any` in Pair<int>, still not implicitly an "
           "int, and still spelled the way the program wrote it\n" << stripAnsi(kept.err);

    const FincRun ok = compile(std::string(decl) +
        "fun main() <noret> { let p <Pair<int>>; p.k = \"s\"; p.v = 1; }\n");
    EXPECT_EQ(errorCount(stripAnsi(ok.err)), 0u)
        << "the instantiated struct accepts a string in the `any` field and an int in "
           "the substituted one\n" << stripAnsi(ok.err);

    // The bounded spelling, which is the one stdlib/types.fin:74 actually writes:
    // `type Any<...> = any implements <...>`. substitute() walks the bounds, because a
    // bound can name a generic parameter, and the temptation in a function that returns
    // a substituted type is to return the substituted bound. That would erase the
    // erasure -- `any implements <I>` would arrive as plain `I`, a nominal type with
    // members, and a value of it would then be rejected for not being one. It is also
    // the mutant that measured nothing in the first matrix: I-substname returns the
    // first bound, and until this program existed no test instantiated a generic over
    // a dynamic type that had one.
    const FincRun bound = compile(
        "interface I { fun m(self: &Self) <int>; }\n"
        "type E = any implements <I>;\n"
        "struct Pair<T> {\n"
        "    pub k <E>\n"
        "    pub v <T>\n"
        "}\n"
        "fun main() <noret> { let p <Pair<int>>; let n <int> = p.k; }\n");
    EXPECT_NE(stripAnsi(bound.err).find("got 'any implements <I>'"), std::string::npos)
        << "instantiation keeps both the name and the bound\n" << stripAnsi(bound.err);
}

TEST(KnownDefect_DynamicTypes, GenericArgumentsOnADynamicTypeAreNotConstraints) {
    // Having kept `any<int>` dynamic, the open question is whether the `<int>` means
    // anything. Today it does not: `any<int>` accepts a string.
    //
    // The corpus does not settle it, and the one spelling it actually writes points
    // away from narrowing: stdlib/types.fin:74 is `type Any<...> = any implements
    // <...>;`, whose argument is the literal variadic `...`, and whose comment says
    // "ANY type is allowed to be passed but it has to be casted or handled by the user
    // itself". A narrowing reading would make `Any<...>` mean "any type that is `...`",
    // which is not a type. So the arguments are carried and ignored, and the ruling on
    // whether a written `any<int>` should narrow is owed (docs/plan.md).
    const FincRun r = compile("fun main() <noret> { let x <any<int>> = \"s\"; }\n");
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 0u)
        << "when this fails, `any<T>` narrows: make it Soundness and assert the "
           "mismatch, and check stdlib/types.fin:74's `Any<...>` still resolves\n"
        << stripAnsi(r.err);
}

TEST(KnownDefect_DynamicTypes, ObjectAndAnyAreMutuallyAssignable) {
    // Both directions compile, and one of them probably should not.
    //
    // The corpus draws a distinction between the two names. stdlib/types.fin:97 says
    // of `any`: "any type that is visible in compile time (can be identified in
    // compile time)". prototype_test.fin:40 says of `object`: "object type is an
    // expensive type but can fit any datatype in it at the cost of memory and speed".
    // Read together those are erasure and boxing: an `any` is a type the compiler
    // knows and has stopped checking, an `object` is a value carrying its type at
    // runtime. Every static type fits either. But an `object` fits an `any` only if
    // its content was compile-time known, which is exactly what the box exists to
    // stop promising -- so `object -> any` looks like it should be rejected while
    // `any -> object` stays.
    //
    // Booked rather than implemented because no corpus program exercises the
    // direction, and a directional rule between the two would change what
    // `<{object, object}>` accepts across prototype_test.fin. The ruling is owed
    // (docs/plan.md, "Rulings owed").
    //
    // This test also records where DynamicType::equals's name comparison stands. That
    // comparison is what keeps `any` and `object` two types rather than one, and
    // today nothing can observe it: the "every type fits a dynamic target" rule in
    // Type::isAssignableTo answers before equality in both directions, and the one
    // remaining channel -- the cast -- rejects both names alike
    // (Soundness_Casts.ACastToOrFromADynamicTypeIsAllowed). A mutant that drops the
    // name comparison kills no test, which is a statement about the consumer not
    // existing yet and not about the comparison being wrong. Deleting it would make
    // `any` and `object` one type, a stronger claim than the corpus supports.
    for (const char* code : {"fun main() <noret> { let a <any> = 1; let b <object> = a; }\n",
                             "fun main() <noret> { let a <object> = 1; let b <any> = a; }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(errorCount(stripAnsi(r.err)), 0u)
            << "GOOD NEWS IF THIS FAILED on the second program: the boxing/erasure "
               "distinction has been ruled on. Split this test, keep `any -> object` "
               "as Soundness, and assert the mismatch for the other direction.\n"
            << code << stripAnsi(r.err);
    }
}

TEST(Soundness_Prototypes, AHeterogeneousPrototypeLiteralInfersObject) {
    // Inverted from KnownDefect_Prototypes.AHeterogeneousPrototypeLiteralInfersAnyNotObject.
    // `prototype_test.fin:14` writes `let a <auto> = { 10 : 10, "a": true };` and says
    // in the sample: "auto would resolve to `<{object, object}>`".
    //
    // The distinction is the corpus's own (types.fin:97 vs prototype_test.fin:40):
    // `any` is compile-time erasure of a type that *is* known, `object` is a box for
    // one that is not. A literal mixing an `int` key with a `string` key has no single
    // compile-time key type, so `object` is the truthful answer and `any` was a claim
    // the compiler could not make good on.
    //
    // The widening reached for `any` by name (Analyzer_Expr.cpp, the PrototypeLiteral
    // visit) since before `any` was a type anyone could write -- so for as long as
    // that code has existed, a mixed prototype literal has had a type whose name did
    // not resolve. It is the oldest evidence in the tree that `any` was always meant
    // to be registered.
    const FincRun r = compile(
        "fun main() <int> { let a <auto> = { 1: 2, \"a\": true }; "
        "let y <string> = a; return 0; }\n");
    EXPECT_NE(stripAnsi(r.err).find("got '<{object, object}>'"), std::string::npos)
        << stripAnsi(r.err);

    // The control: a literal whose keys and values *are* of one type keeps it. Without
    // this, "infer object" is one edit away from "box everything", and every prototype
    // in the corpus starts paying for a runtime tag it does not need.
    const FincRun homo = compile(
        "fun main() <int> { let a <auto> = { 1: 2 }; let y <string> = a; return 0; }\n");
    EXPECT_NE(stripAnsi(homo.err).find("got '<{int, int}>'"), std::string::npos)
        << stripAnsi(homo.err);
}

TEST(Soundness_Prototypes, APrototypeOfConcreteTypesFitsAPrototypeOfADynamicType) {
    // `prototype_test.fin:40` writes
    //   let obj <{object, object}> = { 1: 1.0, "a":CustomDT{} };
    // and comments that `object` "can fit any datatype in it". It reported
    // `expected '<{object, object}>', got '<{any, any}>'` -- and the plain
    // `let a <{object, object}> = { 1: 2 };` failed too, so the mixed literal's `any`
    // was not the cause. PrototypeType::isAssignableTo compared its key and value
    // types with `equals`, which is exact, so no prototype ever fitted a prototype of
    // a different key or value type at all.
    //
    // This is the same defect ArrayType had and the same fix: ask the contained types
    // whether they are *assignable*, not whether they are equal. The comment on
    // ArrayType::isAssignableTo states the general form -- reaching the element check
    // and then discarding its answer means every permissive element type stops at the
    // container boundary. Two containers had it; PrototypeType is the second.
    for (const char* code : {"fun main() <noret> { let a <{object, object}> = { 1: 2 }; }\n",
                             "fun main() <noret> { let a <{object, object}> = { 1: true }; }\n",
                             "fun main() <noret> { let a <{any, any}> = { 1: 2 }; }\n",
                             "fun main() <noret> { let a <{any, int}> = { 1: 2 }; }\n",
                             "fun main() <noret> { let a <{object, object}> = { 1: 2, \"a\": true }; }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(errorCount(stripAnsi(r.err)), 0u) << code << stripAnsi(r.err);
    }
}

TEST(Soundness_Prototypes, APrototypeFitsABareDynamicTarget) {
    // `let a <any> = { 1: 2 };` reported `expected 'any', got '<{int, int}>'` while
    // `let a <any> = [1, 2];` compiled clean, and the difference was not about
    // prototypes: PrototypeType::isAssignableTo was the one override in src/types that
    // never called Type::isAssignableTo. ArrayType, PointerType, NullableType and
    // PrimitiveType all open with it, which is how each of them inherits `-> auto`,
    // `-> any` and `-> object` without restating them.
    //
    // Its local `if (other.toString() == "auto") return true;` was the visible half of
    // that: one of the base's three target rules, copied in, and the other two lost.
    for (const char* code : {"fun main() <noret> { let a <any> = { 1: 2 }; }\n",
                             "fun main() <noret> { let a <object> = { 1: 2 }; }\n",
                             "fun main() <noret> { let a <auto> = { 1: 2 }; }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(errorCount(stripAnsi(r.err)), 0u) << code << stripAnsi(r.err);
    }
}

TEST(Soundness_Prototypes, APrototypeWithAnUnassignableKeyOrValueIsStillRejected) {
    // The control for the test above, and it is the load-bearing half: "compare by
    // assignability" is one edit away from "return true", and a prototype that fits
    // every prototype would take the whole container out of the type system silently
    // -- nothing in the corpus writes a mismatched prototype, so the sample suite
    // could not notice.
    for (const char* code : {"fun main() <noret> { let a <{string, int}> = { 1: 2 }; }\n",
                             "fun main() <noret> { let a <{int, string}> = { 1: 2 }; }\n"}) {
        const FincRun r = compile(code);
        EXPECT_NE(stripAnsi(r.err).find("Type mismatch"), std::string::npos)
            << code << stripAnsi(r.err);
    }
}

TEST(KnownDefect_TypeAliases, AGenericTypeAliasIsNeverDeclared) {
    // Not the drop-the-entity rule -- worse. visit(TypeDefinition&) returns early for any
    // alias with generic parameters, resolving the target for its diagnostics and never
    // calling defineType at all, so `type ArrayType<T> = [T];` leaves `ArrayType`
    // undefined whether or not `[T]` resolved. The debugLog calls it "partially
    // supported"; nothing is supported. The fix is a TypeAlias in the type system that
    // can be instantiated, which is why this is booked rather than fixed here.
    //
    // Corpus footprint: `arrays.fin:4` writes one and never names it, so this costs
    // nothing today and will cost a diagnostic per use the first time it is used.
    const FincRun r = compile("type Alias<T> = [T];\n"
                              "fun f(a: Alias<int>) <int> { return 0; }\n"
                              "fun main() <int> { return 0; }\n");
    EXPECT_NE(stripAnsi(r.err).find("Undefined type 'Alias'"), std::string::npos)
        << "GOOD NEWS: generic type aliases are declared now. Invert this test -- the\n"
           "program should compile clean -- and rename it to\n"
           "Soundness_TypeAliases.AGenericTypeAliasIsDeclaredAndInstantiable.\n"
        << stripAnsi(r.err);

    // The control: the non-generic spelling of the same alias is declared and usable,
    // which is what makes the above a gap in one branch rather than the feature missing.
    const FincRun plain = compile("type Alias = [int];\n"
                                  "fun f(a: Alias) <int> { return 0; }\n"
                                  "fun main() <int> { return 0; }\n");
    EXPECT_EQ(plain.exitCode, 0) << stripAnsi(plain.err);
}

TEST(Soundness_ErrorRecovery, AMemberDefaultIsReportedOncePerProgramNotOncePerPass) {
    // Not a cascade -- the opposite failure, and found while fixing the fields
    // above. visit(StructDeclaration) walked every member default twice: once in
    // the registration pass and again in the body pass, each reporting. The body
    // pass is the well-formed one (it has currentStructContext set and reads the
    // field type back from the struct), so the registration pass now registers the
    // field and nothing more.
    for (const char* kw : {"struct", "class"}) {
        const std::string code =
            std::string(kw) + " S { pub v <int> = nosuchvar }\nfun main() <int> { return 0; }\n";
        const FincRun r = compile(code);
        EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u)
            << "one undefined name, one diagnostic\n" << code << stripAnsi(r.err);
    }

    // The control: the check that pass still performs must still perform it. If the
    // deletion had taken the type check with it, this would go quiet.
    const FincRun mismatch =
        compile("struct S { pub v <int> = \"str\" }\nfun main() <int> { return 0; }\n");
    EXPECT_NE(stripAnsi(mismatch.err).find("Type mismatch"), std::string::npos)
        << "a member default of the wrong type is still checked\n" << stripAnsi(mismatch.err);
}

// ---------------------------------------------------------------------------
// A container literal whose element did not type.
//
// The sentinel unit fixed this at five sites -- member access, method call, index,
// cast and checkType -- and every one of them was about a *named entity* whose
// annotation failed. A container literal reaches the same cascade from the other
// direction: nothing was annotated, so nothing failed to resolve; an element simply
// has no type because the expression inside it was already reported.
//
// The two literals answered differently, and neither answer was right. The prototype
// substituted `any` for the missing type, so `let a <{int, int}> = { nosuchvar : 1 };`
// said "Undefined variable" and then "expected '<{int, int}>', got '<{any, int}>'" --
// a claim about a type the program never wrote, which is the exact shape the sentinel
// exists to prevent. The array bailed out with `if (!firstType) return;`, which is
// quiet but stops the walk, so a second undefined name in the same literal went
// unreported.

TEST(Soundness_ErrorRecovery, APrototypeLiteralWithAnUntypedElementDoesNotCascade) {
    // `any` was the wrong substitute for two separate reasons. It is a real type, so
    // it compares -- and it does not fit `int`, so the mismatch fires. And it is a
    // *claim*: `<{any, int}>` says the program asked for a boxed key, when what
    // happened is that the analyser could not tell what the key was.
    for (const char* code : {"fun main() <noret> { let a <{int, int}> = { nosuchvar : 1 }; }\n",
                             "fun main() <noret> { let a <{int, int}> = { 1 : nosuchvar }; }\n",
                             "fun main() <noret> { let a <auto> = { nosuchvar : 1 }; }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u)
            << "one undefined name, one diagnostic\n" << code << stripAnsi(r.err);
        EXPECT_NE(stripAnsi(r.err).find("Undefined variable"), std::string::npos)
            << "and it is the undefined name, not a type mismatch\n" << stripAnsi(r.err);
    }
}

TEST(Soundness_ErrorRecovery, AnArrayLiteralWithAnUntypedElementDoesNotCascade) {
    // The array half already passed when this was written, and it is here because the
    // fix changes how: it used to leave the literal with no type at all, and now the
    // literal is typed `[<error>]` and isErrorType unwraps it. Both are quiet, but
    // only the second one keeps walking -- see the next test.
    for (const char* code : {"fun main() <noret> { let a <[int]> = [nosuchvar]; }\n",
                             "fun main() <noret> { let a <auto> = [nosuchvar]; }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u) << code << stripAnsi(r.err);
    }
}

TEST(Soundness_ErrorRecovery, EveryElementOfAContainerLiteralIsWalked) {
    // `[n1, n2]` reported only n1. visit(ArrayLiteral) returned as soon as the first
    // element had no type, and the return skipped the loop that visits the rest.
    //
    // Suppressing a cascade and skipping the walk look alike from one diagnostic away
    // and are opposites: the first drops a message that says nothing new, the second
    // drops a message about a different mistake. The parameter-defaults unit learned
    // the same thing about a default that is never visited. The prototype loop already
    // visited every pair, which is why only one of these two was red.
    struct Case { const char* code; unsigned n; };
    for (const Case& c : {Case{"fun main() <noret> { let a <[int]> = [n1, n2]; }\n", 2u},
                          Case{"fun main() <noret> { let a <[int]> = [n1, n2, n3]; }\n", 3u},
                          Case{"fun main() <noret> { let a <{int, int}> = { n1 : 1, n2 : 2 }; }\n", 2u},
                          Case{"fun main() <noret> { let a <{int, int}> = { 1 : n1, 2 : n2 }; }\n", 2u}}) {
        const FincRun r = compile(c.code);
        EXPECT_EQ(errorCount(stripAnsi(r.err)), c.n)
            << "one diagnostic per undefined name and nothing else\n" << c.code << stripAnsi(r.err);
    }
}

TEST(Soundness_ErrorRecovery, AnUntypedElementDoesNotSuppressTheRestOfItsOwnLiteral) {
    // The control for the test above, and for the sentinel generally: an element that
    // *did* type is still checked against its siblings. `[1, "s"]` must still report,
    // and it must still report when an untyped element sits in front of it -- the
    // sentinel is meant to absorb comparisons that involve it, not every comparison
    // in the construct that contains it.
    const FincRun plain = compile("fun main() <noret> { let a <auto> = [1, \"s\"]; }\n");
    EXPECT_NE(stripAnsi(plain.err).find("Type mismatch"), std::string::npos)
        << "a heterogeneous array literal is still an error\n" << stripAnsi(plain.err);

    // With the sentinel in front, the mismatch between elements 1 and 2 is a
    // comparison against the sentinel and is correctly absorbed -- so what this
    // asserts is that the *names* are all still reported and nothing else is.
    const FincRun mixed = compile("fun main() <noret> { let a <auto> = [n1, 1, \"s\"]; }\n");
    EXPECT_EQ(errorCount(stripAnsi(mixed.err)), 1u)
        << "the undefined name only: every later element is compared against <error>\n"
        << stripAnsi(mixed.err);
    EXPECT_EQ(stripAnsi(mixed.err).find("<error>"), std::string::npos)
        << "and the sentinel is never rendered\n" << stripAnsi(mixed.err);
}

TEST(Soundness_ErrorRecovery, ASentinelElementIsNotWidenedAwayByItsSiblings) {
    // The prototype literal has a rule the array literal does not: two elements of
    // different real types widen the inferred key or value to `object` instead of
    // reporting, which is what Soundness_Prototypes.AHeterogeneousPrototypeLiteralInfersObject
    // asserts. The sentinel must not take part in that widening. If it does, a literal
    // holding one untyped element and one typed one comes out `<{object, int}>` -- a
    // real type, so it compares, so it mismatches, and the cascade is back by a
    // different route than the one three tests above.
    //
    // This test exists because a mutation matrix found the rule unguarded: deleting
    // both `isErrorType(...) -> errorType()` arms from visit(PrototypeLiteral) killed
    // nothing at all. The cascade tests above use `{ n1 : 1, n2 : 2 }`, where *both*
    // keys are the sentinel -- and `<error>.equals(<error>)` is true, so the widening
    // branch is never reached and the deleted arms never ran. The mixture is the point:
    // one element the analyser could not type, one it could, in the same literal.
    for (const char* code : {"fun main() <noret> { let a <{int, int}> = { nosuchvar : 1, 5 : 2 }; }\n",
                             "fun main() <noret> { let a <{int, int}> = { 5 : 2, nosuchvar : 1 }; }\n",
                             "fun main() <noret> { let a <{int, int}> = { 1 : nosuchvar, 2 : 5 }; }\n",
                             "fun main() <noret> { let a <{int, int}> = { 1 : 5, 2 : nosuchvar }; }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u)
            << "the undefined name only\n" << code << stripAnsi(r.err);
        EXPECT_EQ(stripAnsi(r.err).find("object"), std::string::npos)
            << "and nothing claims the program asked for a boxed key or value\n"
            << code << stripAnsi(r.err);
    }
}

// ---------------------------------------------------------------------------
// A method call has a signature.
//
// Until this unit StructType::methods was `map<string, TypePtr>` holding only a
// return type, so a method call was checked for neither arity nor argument types on
// a struct, a class, an implements block or an inherited parent -- while a free
// function was checked for both. The map now holds a FunctionType and the three
// call sites share one check with visit(FunctionCall&).
//
// Two things about the stored signature are decisions, not accidents:
//
//   * The receiver is not in it. `struct_methods.fin:10` says the first parameter
//     "will be injected by compiler and it will be the struct itself", whether or
//     not the author wrote `self`, so both spellings must call the same way. The
//     signature is stored as it is *called*, which makes AWrittenSelfParameter-
//     IsNotAnArgument a test of the store rather than of every call site.
//   * It is resolved in the signature pass, quietly, because the body pass resolves
//     the same annotations again and reports. Resolving twice reporting is exactly
//     the duplicate that Soundness_ErrorRecovery.AnImplementsBlockMethodParameter-
//     IsReportedOnce was split out to hold, and AMethodParameterAnnotationIsStill-
//     ReportedOnce below is the guard for the other three sites.
// ---------------------------------------------------------------------------

TEST(Soundness_MethodCalls, AMethodCallIsCheckedAgainstItsSignature) {
    // Was KnownDefect_MethodCalls.AMethodCallIsNotCheckedAgainstItsSignature, whose
    // own text set the target: "each of these reports a wrong-arity error, exactly as
    // `fun f(a: int)` called `f(1, 2)` already does".
    //
    // Found by mutation: the implements block built a parameter-type vector that no one
    // read, and breaking it killed no test because nothing could observe a method
    // signature. This is the consumer whose absence made it dead.
    struct Case { const char* what; const char* code; };
    for (const Case& c : {
             Case{"struct",
                  "struct S {\n"
                  "    v <int>,\n"
                  "    pub fun m(a: int) <int> { return a; }\n"
                  "}\n"
                  "fun main() <int> { let s <S> = S{v: 1}; return s.m(1, 2); }\n"},
             Case{"class",
                  "class C {\n"
                  "    v <int>,\n"
                  "    pub fun m(a: int) <int> { return a; }\n"
                  "}\n"
                  "fun main() <int> { let c <&C> = new C{v: 1}; return c.m(1, 2); }\n"},
             Case{"implements block",
                  "struct S { v <int> }\n"
                  "interface I { pub fun m(a: int) <int>; }\n"
                  "S implements <I> {\n"
                  "    pub fun m(a: int) <int> { return a; }\n"
                  "}\n"
                  "fun main() <int> { let s <S> = S{v: 1}; return s.m(1, 2); }\n"}}) {
        const std::string err = stripAnsi(compile(c.code).err);
        EXPECT_NE(err.find("Method 'm' expects 1 arguments, got 2"), std::string::npos)
            << c.what << "\n" << err;
        EXPECT_EQ(errorCount(err), 1u) << "one call, one diagnostic\n" << c.what << "\n" << err;
    }

    // Too few, on the same declaration: the check is a range, not an upper bound.
    const std::string few = stripAnsi(compile(
        "struct S { v <int>, pub fun m(a: int, b: int) <int> { return a; } }\n"
        "fun main() <int> { let s <S> = S{v: 1}; return s.m(1); }\n").err);
    EXPECT_NE(few.find("Method 'm' expects 2 arguments, got 1"), std::string::npos) << few;

    // The free-function control: the same mistake in the same shape, which is what
    // makes the two messages worth keeping in one wording.
    const std::string fn = stripAnsi(compile(
        "fun m(a: int) <int> { return a; }\n"
        "fun main() <int> { return m(1, 2); }\n").err);
    EXPECT_NE(fn.find("Function 'm' expects 1 arguments, got 2"), std::string::npos) << fn;
}

TEST(Soundness_MethodCalls, ACallsTypeIsItsReturnTypeNotItsLastArgument) {
    // visit(MethodCall&) assigned lastExprType = retType and *then* walked the
    // arguments, so every argument overwrote the call's own type and a call with
    // arguments was typed as its last argument. Both directions were wrong at once:
    // a true type was rejected and a false one accepted. visit(StaticMethodCall&)
    // already ordered the two operations the other way round, which is what showed
    // this was an ordering slip and not a missing rule.
    const char* decl = "struct S {\n"
                       "    v <int>,\n"
                       "    pub fun m(self: &Self, a: string) <int> { return 1; }\n"
                       "    pub fun n(self: &Self) <int> { return 2; }\n"
                       "    pub static fun s(a: string) <int> { return 3; }\n"
                       "}\n";

    // The false positive: `m` returns int, the annotation is int, and the argument's
    // type was reported as if it were the call's.
    const std::string ok = stripAnsi(compile(
        std::string(decl) + "fun main() <int> { let s <S> = S{v: 1}; let r <int> = s.m(\"x\"); return 0; }\n").err);
    EXPECT_EQ(errorCount(ok), 0u) << "a call's type is its return type\n" << ok;

    // The missed error, which is the half a caret-placement fix would have hidden:
    // the call was accepted where a string was wanted because a string was passed.
    const std::string bad = stripAnsi(compile(
        std::string(decl) + "fun main() <int> { let s <S> = S{v: 1}; let r <string> = s.m(\"x\"); return 0; }\n").err);
    EXPECT_NE(bad.find("expected 'string', got 'int'"), std::string::npos)
        << "the call's type is int, whatever it was passed\n" << bad;

    // The control that localised it: with no arguments there was nothing to overwrite
    // with, so a no-argument method typed correctly throughout.
    const std::string none = stripAnsi(compile(
        std::string(decl) + "fun main() <int> { let s <S> = S{v: 1}; let r <string> = s.n(); return 0; }\n").err);
    EXPECT_NE(none.find("expected 'string', got 'int'"), std::string::npos) << none;

    // And the static site, which was already right and must stay right.
    const std::string stat = stripAnsi(compile(
        std::string(decl) + "fun main() <int> { let r <string> = S::s(\"x\"); return 0; }\n").err);
    EXPECT_NE(stat.find("expected 'string', got 'int'"), std::string::npos) << stat;
}

TEST(Soundness_MethodCalls, AMethodArgumentIsCheckedAgainstItsParameter) {
    // Deliberately a *discarded* call: `let r <int> = s.m("x")` would have reported
    // "expected 'int', got 'string'" even before this unit, through the ordering bug
    // above, so testing arguments in an annotated position proves nothing about
    // whether arguments are checked.
    struct Case { const char* what; const char* code; };
    for (const Case& c : {
             Case{"struct",
                  "struct S { v <int>, pub fun m(a: int) <int> { return a; } }\n"
                  "fun main() <int> { let s <S> = S{v: 1}; s.m(\"x\"); return 0; }\n"},
             Case{"class",
                  "class C { v <int>, pub fun m(a: int) <int> { return a; } }\n"
                  "fun main() <int> { let c <&C> = new C{v: 1}; c.m(\"x\"); return 0; }\n"},
             Case{"static",
                  "struct S { v <int>, pub static fun m(a: int) <int> { return a; } }\n"
                  "fun main() <int> { S::m(\"x\"); return 0; }\n"}}) {
        const std::string err = stripAnsi(compile(c.code).err);
        EXPECT_NE(err.find("expected 'int', got 'string'"), std::string::npos)
            << c.what << "\n" << err;
    }

    // The argument that is right is still right -- an arity-only check would pass the
    // three above by accident if it reported on every call.
    const std::string clean = stripAnsi(compile(
        "struct S { v <int>, pub fun m(a: int) <int> { return a; } }\n"
        "fun main() <int> { let s <S> = S{v: 1}; s.m(1); return 0; }\n").err);
    EXPECT_EQ(errorCount(clean), 0u) << clean;
}

TEST(Soundness_MethodCalls, AWrittenSelfParameterIsNotAnArgument) {
    // struct_methods.fin writes both spellings in one struct -- `fun print_point(self:
    // &Self)` at :10 and `fun set_x<U>(new_x: U)` at :14 -- and its comment says the
    // receiver is injected either way. So the two must be called identically, which
    // means the stored signature cannot simply be the declared parameter list.
    struct Case { const char* what; const char* decl; };
    for (const Case& c : {Case{"written self", "pub fun m(self: &Self, a: int) <int> { return a; }"},
                          Case{"injected self", "pub fun m(a: int) <int> { return a; }"}}) {
        const std::string head = std::string("struct S { v <int>, ") + c.decl + " }\n"
                                 "fun main() <int> { let s <S> = S{v: 1}; return ";
        EXPECT_EQ(errorCount(stripAnsi(compile(head + "s.m(1); }\n").err)), 0u)
            << c.what << ": one written argument is the one argument\n";
        EXPECT_NE(stripAnsi(compile(head + "s.m(); }\n").err).find("expects 1 arguments, got 0"),
                  std::string::npos) << c.what;
        EXPECT_NE(stripAnsi(compile(head + "s.m(1, 2); }\n").err).find("expects 1 arguments, got 2"),
                  std::string::npos) << c.what;
    }

    // A written `self` is not silently dropped either: it still names the receiver in
    // the body, which is the reason the spelling exists.
    EXPECT_EQ(errorCount(stripAnsi(compile(
        "struct S { pub v <int>, pub fun m(self: &Self) <int> { return self.v; } }\n"
        "fun main() <int> { let s <S> = S{v: 1}; return s.m(); }\n").err)), 0u);
}

TEST(Soundness_MethodCalls, AnInheritedMethodIsCheckedAgainstItsSignature) {
    // getMethodReturnType walked `parents` (StructType.cpp:37) and getMethodType must
    // walk it the same way, or inheriting a method would lose its signature and every
    // call through a child would be unchecked again -- the defect this unit closes,
    // reintroduced for exactly the programs deeptest2.fin:67 writes.
    const char* decl = "struct P { pub x <int>, pub fun base(a: int) <int> { return a; } }\n"
                       "struct C : <P> { pub y <int> }\n";
    const std::string wrong = stripAnsi(compile(
        std::string(decl) + "fun main() <int> { let c <C> = C{x: 1, y: 2}; return c.base(1, 2); }\n").err);
    EXPECT_NE(wrong.find("Method 'base' expects 1 arguments, got 2"), std::string::npos) << wrong;

    const std::string right = stripAnsi(compile(
        std::string(decl) + "fun main() <int> { let c <C> = C{x: 1, y: 2}; return c.base(1); }\n").err);
    EXPECT_EQ(errorCount(right), 0u) << right;
}

TEST(Soundness_MethodCalls, ANullableMethodParameterIsOptional) {
    // nullifier.fin:2 records that "a nullable parameter is optional at the call site"
    // is implemented -- for functions. The optionality lives in the `required` count
    // that visit(FunctionCall&) computes, so sharing the check is what carries the
    // rule to methods; writing the arity check separately would have lost it.
    const char* decl = "struct S { v <int>, pub fun m(a?: int) <int> { return 1; } }\n";
    EXPECT_EQ(errorCount(stripAnsi(compile(
        std::string(decl) + "fun main() <int> { let s <S> = S{v: 1}; return s.m(); }\n").err)), 0u)
        << "a nullable parameter may be omitted";
    EXPECT_EQ(errorCount(stripAnsi(compile(
        std::string(decl) + "fun main() <int> { let s <S> = S{v: 1}; return s.m(1); }\n").err)), 0u)
        << "and may be supplied";
    const std::string over = stripAnsi(compile(
        std::string(decl) + "fun main() <int> { let s <S> = S{v: 1}; return s.m(1, 2); }\n").err);
    EXPECT_NE(over.find("expects between 0 and 1 arguments, got 2"), std::string::npos)
        << "optional is not unlimited\n" << over;
}

TEST(Soundness_MethodCalls, AMethodCallThroughAnInterfaceIsChecked) {
    // interfaces.fin:16 writes the bounded form and its comment offers the
    // interface-in-parameter-position form as "an easier way to write this", so both
    // spellings are corpus syntax and both find the method on the interface rather
    // than on a struct. The interface's own signature pass is the one that must record
    // parameters, and it is the one site that cannot resolve them quietly: an
    // interface has no body, so its signature pass is the only pass there is.
    struct Case { const char* what; const char* code; };
    for (const Case& c : {
             Case{"generic bound",
                  "interface I { pub fun m(a: int) <int>; }\n"
                  "fun f<T: I>(item: T) <noret> { let x <int> = item.m(1, 2); }\n"
                  "fun main() <int> { return 0; }\n"},
             Case{"parameter position",
                  "interface I { pub fun m(a: int) <int>; }\n"
                  "fun f(item: I) <noret> { let x <int> = item.m(1, 2); }\n"
                  "fun main() <int> { return 0; }\n"}}) {
        const std::string err = stripAnsi(compile(c.code).err);
        EXPECT_NE(err.find("Method 'm' expects 1 arguments, got 2"), std::string::npos)
            << c.what << "\n" << err;
    }
}

TEST(Soundness_MethodCalls, AMethodWithAnUnresolvedParameterKeepsItsWrittenArity) {
    // The sentinel rule at a new site. Soundness_TypeResolution.AnUnresolvedParameter-
    // DoesNotChangeTheReportedArity holds this for functions; dropping an unresolved
    // parameter out of a method signature would make `pub fun m(a: NoSuchType)` called
    // `s.m(1)` say "expects 0 arguments, got 1" -- a claim about a signature nobody
    // wrote, on top of the one real diagnostic.
    const std::string err = stripAnsi(compile(
        "struct S { v <int>, pub fun m(a: NoSuchType) <int> { return 1; } }\n"
        "fun main() <int> { let s <S> = S{v: 1}; return s.m(1); }\n").err);
    EXPECT_EQ(errorCount(err), 1u) << "the annotation is the whole diagnostic\n" << err;
    EXPECT_NE(err.find("Undefined type 'NoSuchType'"), std::string::npos) << err;
    EXPECT_EQ(err.find("arguments"), std::string::npos) << "arity is what was written\n" << err;
}

TEST(KnownDefect_MethodCalls, AStaticCallOnAGenericStructIsCheckedAgainstTheTemplate) {
    // Now that a call is checked against its signature, a signature holding `Self` has to
    // say which Self. For a method it does: the receiver is the *instance* type
    // `G<int>`, StructType::substitute rewrote Self when the instance was made, and both
    // the parameter and the return type compare clean. A static call has no receiver --
    // it resolves `G` by name and gets the uninstantiated template, whose Self is still
    // Self -- so the same signature reports against every argument and every use of the
    // result.
    //
    // Found in the corpus, not here: tests/samples/letssee.fin went from four
    // diagnostics to three when method calls started being checked, and the one it
    // gained was `Vec2::normalize(scaled)` at line 73 -- an argument position, where
    // before only the two static return types (59, 77) misreported. The sample's own
    // //@ unimplemented note already books the root cause.
    //
    // The fix is generic argument inference on a static call path (`G::f(g)` has to
    // learn T from somewhere -- the argument, or the `G::<int>::f` spelling the parser
    // accepts), which is the same missing unification as
    // KnownDefect_Generics.AnOperatorsGenericParameterIsNotInferredFromItsOperand.
    const FincRun r = compile(
        "struct G<T> {\n"
        "    v <T>,\n"
        "    static fun f(p: <&Self>) <noret> { }\n"
        "    static fun g() <&Self> { return null; }\n"
        "}\n"
        "fun main() <int> {\n"
        "    let g <&G<int>> = new G::<int>{v: 1};\n"
        "    G::f(g);\n"
        "    let a <&G<int>> = G::g();\n"
        "    return 0;\n"
        "}\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_NE(err.find("expected '&Self', got '&G<int>'"), std::string::npos)
        << "GOOD NEWS: a static call substitutes the generic arguments into its\n"
           "signature. Invert this test -- both statements should compile clean -- and\n"
           "rename it Soundness_MethodCalls.AStaticCallOnAGenericStructIsCheckedAgainst-\n"
           "TheInstance. Then re-run tests/tools/corpus_snapshot.sh: letssee.fin should\n"
           "drop from three diagnostics to zero, and its //@ unimplemented note goes.\n"
        << err;
    EXPECT_NE(err.find("expected '&G<int>', got '&Self'"), std::string::npos)
        << "the return-type half of the same defect\n" << err;

    // The control, and the reason this is a static-call defect and not a Self defect: a
    // non-generic struct has nothing to substitute, so its static signature is right.
    const FincRun plain = compile(
        "struct P {\n"
        "    v <int>,\n"
        "    static fun f(p: <&P>) <noret> { }\n"
        "}\n"
        "fun main() <int> {\n"
        "    let p <&P> = new P{v: 1};\n"
        "    P::f(p);\n"
        "    return 0;\n"
        "}\n");
    EXPECT_EQ(errorCount(stripAnsi(plain.err)), 0u) << stripAnsi(plain.err);

    // The other control: the same generic struct reached through a receiver, where both
    // positions are clean. This is what the static path should look like.
    const FincRun method = compile(
        "struct G<T> {\n"
        "    v <T>,\n"
        "    fun m(self: <&Self>, p: <&Self>) <noret> { }\n"
        "    fun r(self: <&Self>) <&Self> { return self; }\n"
        "}\n"
        "fun main() <int> {\n"
        "    let g <&G<int>> = new G::<int>{v: 1};\n"
        "    g.m(g);\n"
        "    let a <&G<int>> = g.r();\n"
        "    return 0;\n"
        "}\n");
    EXPECT_EQ(errorCount(stripAnsi(method.err)), 0u)
        << "a method call substitutes Self through the instance receiver\n"
        << stripAnsi(method.err);
}

TEST(KnownDefect_IndexOperator, AnIndexExpressionNeverConsultsOperatorBracket) {
    // `operator []` parses, resolves, and is registered on the struct -- and nothing
    // ever looks it up. visit(ArrayAccess) in Analyzer_Expr.cpp knows two shapes,
    // ArrayType and PointerType, and a struct is neither, so a declared subscript
    // operator has no reader at all. Two faces, and the pointer one is the dangerous
    // half because it is silent.
    //
    // Found by measurement, not by reading: mutant S-retvoid ("a method with no
    // written return type gets the sentinel, not void") killed nothing, and the reason
    // was that no method can *have* no written return type -- every function production
    // in parser.y requires `LT type GT`. Checking whether the operator loop's identical
    // `else` branch was equally dead turned up parser.y:939, the
    // `operator[] implements cast<fn(Self, T)>(__get)` form, where `return_type` really
    // is null. That form is reachable (tests/samples/stdlib/hashmap.fin:50) -- and
    // probing what it registered is how this came out.
    //
    // A third defect sits behind these two and only becomes visible once they are
    // fixed: `OperatorDeclaration::implements_expr`, which holds the cast that carries
    // that form's whole signature, is written by the parser and read by nobody
    // (`grep -rn implements_expr src/` finds only the declaration and its assignment).
    // So `operator[] implements cast<fn(Self, T)>(__get)` registers `operator[]`
    // returning `void` and drops the binding to `__get` on the floor.

    // Face one, silent: `&Box<int>` is a PointerType whose pointee is not an array, so
    // the subscript is typed as the pointee -- C pointer-arithmetic semantics applied to
    // a struct that asked for something else. No diagnostic says so; the only reason
    // this is visible at all is the annotation it then fails against.
    const std::string ptr = stripAnsi(compile(
        "struct Box<T> {\n"
        "    v <T>,\n"
        "    operator [](i: <int>) <int> { return 1; }\n"
        "}\n"
        "fun main() <int> {\n"
        "    let b <&Box<int>> = new Box::<int>{v: 7};\n"
        "    let r <string> = b[0];\n"
        "    return 0;\n"
        "}\n").err);
    EXPECT_NE(ptr.find("got 'Box<int>'"), std::string::npos)
        << "GOOD NEWS: a subscript on a struct consults its operator []. Invert this\n"
           "test -- the pointer case should report `expected 'string', got 'int'`, the\n"
           "value case should compile clean, and a struct with no operator [] must still\n"
           "report `is not an array or pointer` -- then rename it\n"
           "Soundness_IndexOperator.AnIndexExpressionOnAStructIsTypedByItsOperator.\n"
           "Registering the operator's *parameter* type is part of that fix: today\n"
           "StructType::operators maps an operator to a return type and nothing else,\n"
           "so there is nowhere to check the index against. Then re-run\n"
           "tests/tools/corpus_snapshot.sh -- hashmap.fin and collection.fin both use\n"
           "the `implements cast` form and their notes need rereading.\n"
        << ptr;

    // Face two, loud and wrong: a value receiver is not a pointer either, so the
    // operator that exists to make this legal is reported as if it did not exist.
    const std::string val = stripAnsi(compile(
        "struct Box {\n"
        "    v <int>,\n"
        "    operator [](i: <int>) <int> { return self.v; }\n"
        "}\n"
        "fun main() <int> {\n"
        "    let b <Box> = Box{v: 7};\n"
        "    let r <int> = b[0];\n"
        "    return 0;\n"
        "}\n").err);
    EXPECT_NE(val.find("Type 'Box' is not an array or pointer"), std::string::npos)
        << "the value-receiver half: the declared operator [] is not consulted\n" << val;
    EXPECT_EQ(errorCount(val), 1u) << "and it is the only diagnostic\n" << val;

    // Face three: the index is forced to `int` before the receiver is even classified,
    // so an operator declared to take a string key -- which is what a hashmap is for --
    // reports twice for one legal expression.
    const std::string key = stripAnsi(compile(
        "struct Map {\n"
        "    v <int>,\n"
        "    operator [](k: <string>) <int> { return self.v; }\n"
        "}\n"
        "fun main() <int> {\n"
        "    let m <Map> = Map{v: 7};\n"
        "    let r <int> = m[\"k\"];\n"
        "    return 0;\n"
        "}\n").err);
    EXPECT_NE(key.find("expected 'int', got 'string'"), std::string::npos)
        << "the index is checked against int, not against the operator's parameter\n" << key;
    EXPECT_EQ(errorCount(key), 2u) << "two diagnostics for one legal subscript\n" << key;

    // The control, and the reason this is an operator defect and not a subscript defect:
    // indexing what visit(ArrayAccess) does know about is right, element type and all.
    const std::string arr = stripAnsi(compile(
        "fun main() <int> {\n"
        "    let a <[int]> = [1, 2, 3];\n"
        "    let e <string> = a[0];\n"
        "    return 0;\n"
        "}\n").err);
    EXPECT_NE(arr.find("expected 'string', got 'int'"), std::string::npos)
        << "an array subscript is typed as its element type\n" << arr;
    EXPECT_EQ(errorCount(arr), 1u) << arr;
}

// `a + f()` is not a call.
//
// LPAREN was declared at line 190 of parser.y, `%right KW_NEW KW_CAST KW_SIZEOF
// LPAREN`, which is near the bottom of the precedence table -- looser than every
// binary operator. So in `i < g . LPAREN` bison compared the rule `expression LT
// expression` (precedence LT, line 198) against the lookahead LPAREN (line 190),
// found the rule tighter, and reduced: `i < g` became the callee and the whole
// comparison was rebuilt as `FunctionCall("unknown", {})` by the else branch of the
// postfix-call production. Every binary operator was affected, not just the
// comparisons -- `i + g()`, `i * g()`, `i == g()` and `i - g()` each reported
// "Undefined function or type 'unknown'" about an expression that names no function
// at all.
//
// Found while standing up lib/std/hashmap.fin: `for (let i <int> = 0; i <
// self.keys.len(); i++)` would not compile, and the draft in
// tests/samples/stdlib/hashmap.fin writes that loop three times (lines 19, 26 and
// 41). No sample caught it because all four samples that reach such a loop fail on
// `module not found` first, so the corpus was measuring the import and never the
// body.
//
// A call is a postfix operator and it binds tighter than any infix one. LPAREN now
// sits with LBRACKET, DOT and LBRACE.
TEST(Soundness_Precedence, ACallOnTheRightOfABinaryOperatorIsStillACall) {
    const std::string prelude =
        "fun g() <int> { return 3; }\n"
        "fun g0() <bool> { return true; }\n";

    // One per operator, because the defect was in the precedence table and not in any
    // single production: whatever binds looser than LPAREN mis-parses, and that was
    // everything.
    struct Row { const char* expr; const char* type; };
    const Row rows[] = {
        {"1 + g()", "int"},   {"1 - g()", "int"},   {"1 * g()", "int"},
        {"1 / g()", "int"},   {"1 % g()", "int"},
        {"1 < g()", "bool"},  {"1 > g()", "bool"},  {"1 <= g()", "bool"},
        {"1 >= g()", "bool"}, {"1 == g()", "bool"}, {"1 != g()", "bool"},
        {"1 << g()", "int"},  {"1 >> g()", "int"},
        {"true && g0()", "bool"}, {"true || g0()", "bool"},
        // `1 & g()`, `1 | g()` and `1 ^ g()` are absent from this table because those
        // three have no binary production at all -- see
        // KnownDefect_Operators.EightOperatorsCanBeDeclaredAndNeverWritten. This test
        // is about precedence, and a token with no production has none to get wrong.
    };
    for (const auto& r : rows) {
        const std::string err = stripAnsi(compile(
            prelude + "fun main() <int> { let a <" + r.type + "> = " + r.expr + "; return 0; }\n").err);
        EXPECT_EQ(errorCount(err), 0u) << r.expr << " is a binary expression\n" << err;
        EXPECT_EQ(err.find("'unknown'"), std::string::npos)
            << r.expr << " names no function\n" << err;
    }

    // And the operator's own type survives the parse. If `1 < g()` were still a call
    // the annotation would be compared against the callee's return type and this would
    // be quiet; the diagnostic is the proof that a comparison was built.
    const std::string wrong = stripAnsi(compile(
        prelude + "fun main() <int> { let a <int> = 1 < g(); return 0; }\n").err);
    EXPECT_EQ(errorCount(wrong), 1u) << "a comparison is a bool\n" << wrong;
    EXPECT_NE(wrong.find("expected 'int', got 'bool'"), std::string::npos) << wrong;

    // A method call on the right-hand side is the shape lib/std/hashmap.fin needed.
    const std::string method = stripAnsi(compile(
        "struct S { n <int>, fun len(self: &Self) <int> { return self.n; } }\n"
        "fun main() <int> {\n"
        "  let s <&S> = new S{n: 2};\n"
        "  for (let i <int> = 0; i < s.len(); i++) { }\n"
        "  return 0;\n"
        "}\n").err);
    EXPECT_EQ(errorCount(method), 0u) << "a loop bound can be a method call\n" << method;

    // A call on the left never broke -- the callee was already reduced by the time the
    // operator was seen -- and it still does not.
    const std::string lhs = stripAnsi(compile(
        prelude + "fun main() <int> { let a <int> = g() + 1; return 0; }\n").err);
    EXPECT_EQ(errorCount(lhs), 0u) << lhs;

    // Chained, so the fix is not "one call per expression".
    const std::string chain = stripAnsi(compile(
        prelude + "fun main() <int> { let a <int> = g() + g() * g(); return 0; }\n").err);
    EXPECT_EQ(errorCount(chain), 0u) << chain;

    // Unary minus was broken by the same entry and is fixed by the same move: UMINUS
    // used to be tighter than LPAREN, so `-g()` reduced to `(-g)()`.
    const std::string unary = stripAnsi(compile(
        prelude + "fun main() <int> { let a <int> = -g(); let b <int> = 1 - -g(); return 0; }\n").err);
    EXPECT_EQ(errorCount(unary), 0u) << "-g() negates a call\n" << unary;

    // The parenthesised primary still parses, which is what LPAREN's old precedence
    // was next to. `(g())` and a call whose argument is a parenthesised expression are
    // the two shapes that share the token.
    const std::string paren = stripAnsi(compile(
        prelude + "fun h(x: int) <int> { return x; }\n"
        "fun main() <int> { let a <int> = 1 + (g()); let b <int> = h((1 + 2) * 3); return 0; }\n").err);
    EXPECT_EQ(errorCount(paren), 0u) << paren;
}

// Eight operators can be declared, required by an interface, and never written.
//
// The declaration side is nearly complete: `operator &`, `operator |`, `operator ^`,
// `operator %=`, `operator &=`, `operator |=`, `operator <<=` and `operator >>=` all
// parse in an interface or a struct, and lib/std/operators.fin declares seven of them
// because tests/samples/stdlib/operators.fin does -- BitAnd, BitOr, BitAndAssign,
// BitOrAssign, ModAssign, ShiftLeftAssign, ShiftRightAssign. The expression side has no
// production for any of them, so an interface that requires `operator &` requires
// something no program can invoke.
//
// Three of the eight are worse than missing, they are half-declared: SHIFTLEFTEQUAL and
// SHIFTRIGHTEQUAL sit in the assignment precedence group at parser.y:189 and CARET has
// its own `%left CARET` line, so the precedence table ranks three tokens against
// operators they can never meet.
//
// `^` is in neither the spec nor any sample -- the lexer produces CARET (lexer.l:265)
// and `ASTTokenKind::CARET` exists for the declaration form, and that is the whole of
// it. `^=` is the one spelling that exists nowhere at all: no token spells it, so
// `operator ^=` does not even declare, which is why the count is eight and not nine.
//
// `&` and `|` are the two with a reason to be careful rather than just absent: `&` is
// also address-of and the reference marker, `|` is also the union-type separator. `^`
// has no second meaning at all.
//
// Found while fixing Soundness_Precedence.ACallOnTheRightOfABinaryOperatorIsStillACall,
// whose first draft asserted sixteen operators and discovered three of them do not
// exist. The asymmetry is the point: the compiler will happily typecheck a program
// whose interfaces demand operators the grammar cannot spell.
//
// When this is fixed, invert it: Soundness_Operators.EveryDeclarableOperatorHasAn-
// ExpressionThatInvokesIt, asserting zero diagnostics for each row instead.
TEST(KnownDefect_Operators, EightOperatorsCanBeDeclaredAndNeverWritten) {
    // Every one of the eight is accepted as a requirement.
    const std::string decls = stripAnsi(compile(
        "interface BitAnd { pub operator &(rhs: any) <int>; }\n"
        "interface BitOr { pub operator |(rhs: any) <int>; }\n"
        "interface Xor { pub operator ^(rhs: any) <int>; }\n"
        "interface ModAssign { pub operator %=(rhs: any) <int>; }\n"
        "interface BitAndAssign { pub operator &=(rhs: any) <int>; }\n"
        "interface BitOrAssign { pub operator |=(rhs: any) <int>; }\n"
        "interface ShiftLeftAssign { pub operator <<=(rhs: any) <int>; }\n"
        "interface ShiftRightAssign { pub operator >>=(rhs: any) <int>; }\n"
        "fun main() <int> { return 0; }\n").err);
    EXPECT_EQ(errorCount(decls), 0u) << "all eight are declarable\n" << decls;

    // `^=` is not among them: no token spells it, so the declaration itself is a syntax
    // error and there is nothing to require.
    const std::string xoreq = stripAnsi(compile(
        "interface XorAssign { pub operator ^=(rhs: any) <int>; }\n"
        "fun main() <int> { return 0; }\n").err);
    EXPECT_NE(xoreq.find("syntax error"), std::string::npos)
        << "`^=` does not lex as one operator\n" << xoreq;

    // And none of the eight is writable.
    const char* binary[] = {"1 & 2", "1 | 2", "1 ^ 2"};
    for (const char* e : binary) {
        const std::string err = stripAnsi(compile(
            std::string("fun main() <int> { let a <auto> = ") + e + "; return 0; }\n").err);
        EXPECT_NE(err.find("syntax error"), std::string::npos)
            << e << " has no binary production\n" << err;
    }
    const char* compound[] = {"%=", "&=", "|=", "<<=", ">>="};
    for (const char* op : compound) {
        const std::string err = stripAnsi(compile(
            std::string("fun main() <int> { let a <int> = 1; a ") + op + " 2; return 0; }\n").err);
        EXPECT_NE(err.find("syntax error"), std::string::npos)
            << op << " has no assignment production\n" << err;
    }

    // The four that do exist, as the control: whatever is wrong above is not "compound
    // assignment does not work".
    const char* works[] = {"+=", "-=", "*=", "/="};
    for (const char* op : works) {
        const std::string err = stripAnsi(compile(
            std::string("fun main() <int> { let a <int> = 1; a ") + op + " 2; return 0; }\n").err);
        EXPECT_EQ(errorCount(err), 0u) << op << " is the control\n" << err;
    }
    const std::string shifts = stripAnsi(compile(
        "fun main() <int> { let a <auto> = (1 << 2) >> 1; return 0; }\n").err);
    EXPECT_EQ(errorCount(shifts), 0u) << "the shifts themselves exist\n" << shifts;
}

TEST(Soundness_GenericConstraints, AMethodInAnInterfaceDeclaresItsOwnGenerics) {
    // The sibling of Soundness_GenericConstraints.AMethodInAnImplementsBlockDeclares-
    // ItsOwnGenerics, and it was broken: visit(InterfaceDeclaration) opened a scope per
    // method but never called declareGenericParams for it -- the operator loop three
    // lines below did -- so `pub fun m<T>(a: T) <T>;` reported "Undefined type 'T'"
    // twice, once for the parameter and once for the return type.
    //
    // Found by counting diagnostics for the quiet signature pass rather than by a test:
    // the interface is the one site that must keep reporting, so its output was the
    // output being counted.
    EXPECT_EQ(errorCount(stripAnsi(compile(
        "interface I { pub fun m<T>(a: T) <T>; }\nfun main() <int> { return 0; }\n").err)), 0u)
        << "a generic interface method declares its own T";

    // And the generics do not leak: the next method's signature is resolved in its own
    // scope, so T is undefined there -- once, for the one annotation that names it.
    const std::string leak = stripAnsi(compile(
        "interface I { pub fun m<T>(a: T) <int>; pub fun n(b: T) <int>; }\n"
        "fun main() <int> { return 0; }\n").err);
    EXPECT_EQ(errorCount(leak), 1u) << "one method's generics stay in one method\n" << leak;
}

TEST(Soundness_ErrorRecovery, AMethodParameterAnnotationIsStillReportedOnce) {
    // The guard on the quiet signature pass. Before this unit no site resolved a
    // method's parameter types in pass 1, so each was reported once, by the body pass.
    // Pass 1 now resolves them to build the signature -- and must not report, or every
    // one of these becomes two diagnostics. That is the same duplicate that
    // AnImplementsBlockMethodParameterIsReportedOnce was split out of a KnownDefect to
    // hold, so this test states it for the sites that pass 1 covers.
    //
    // Interfaces are absent on purpose: an interface method has no body, so nothing
    // resolves its parameters a second time and its signature pass reports. That
    // asymmetry is the whole reason the flag is a flag and not a policy.
    struct Case { const char* what; const char* code; };
    for (const Case& c : {
             Case{"struct method",
                  "struct S { v <int>, pub fun m(a: NoSuchType) <int> { return 1; } }\n"
                  "fun main() <int> { return 0; }\n"},
             Case{"class method",
                  "class C { v <int>, pub fun m(a: NoSuchType) <int> { return 1; } }\n"
                  "fun main() <int> { return 0; }\n"},
             Case{"static method",
                  "struct S { v <int>, pub static fun m(a: NoSuchType) <int> { return 1; } }\n"
                  "fun main() <int> { return 0; }\n"},
             Case{"implements block",
                  "interface I { pub fun m(a: int) <int>; }\n"
                  "struct S { val <int> }\n"
                  "S implements <I> { pub fun m(a: NoSuchType) <int> { return 0; } }\n"
                  "fun main() <int> { return 0; }\n"}}) {
        const std::string err = stripAnsi(compile(c.code).err);
        EXPECT_EQ(errorCount(err), 1u) << c.what << ": one annotation, one diagnostic\n" << err;
    }

    // The interface, stated rather than left to inference: exactly one, from the only
    // pass that sees it.
    const std::string iface = stripAnsi(compile(
        "interface I { pub fun m(a: NoSuchType) <int>; }\nfun main() <int> { return 0; }\n").err);
    EXPECT_EQ(errorCount(iface), 1u) << "an interface method reports from its signature pass\n" << iface;
}

// ---------------------------------------------------------------------------
// An annotation inside a struct signature is reported once.
//
// It used to be reported once per resolution pass. visit(StructDeclaration) resolves
// each method's and constructor's parameter and return types twice: once in the
// registration pass, which needs the signature before any body is analysed, and again
// when the body is visited. Both called a reporting resolver on the same TypeNode, so
// `S(p: NoSuchType)` said "Undefined type 'NoSuchType'" twice.
//
// The ranking on the KnownDefect this replaces read "fix it when the pass structure is
// being changed for another reason, not before", and the method-signature unit was that
// reason: pass 1 had to start resolving parameter types to build a FunctionType, which
// would have turned one duplicate into three. The fix is the second of the two shapes
// that KnownDefect named -- resolution in the registration pass does not report,
// because the body pass resolves the same nodes and does. It is a pre-pass whose every
// diagnostic is re-raised, which is what makes silence there sound rather than lossy.
//
// The one site that keeps reporting is the interface, and it is the reason this is a
// scoped guard and not a policy: an interface method has no body, so its registration
// pass is the only pass over its signature. Soundness_MethodCalls.AMethodCallThrough-
// AnInterfaceIsChecked calls those signatures and AMethodParameterAnnotationIsStill-
// ReportedOnce counts their diagnostics.
// ---------------------------------------------------------------------------
TEST(Soundness_ErrorRecovery, AnAnnotationInAStructSignatureIsReportedOnce) {
    struct Case { const char* what; const char* code; };
    for (const Case& c : {
             Case{"constructor parameter",
                  "struct S { S(p: NoSuchType) {} }\nfun main() <int> { return 0; }\n"},
             Case{"class constructor parameter",
                  "class C { pub v <int>, C(p: NoSuchType) {} }\n"
                  "fun main() <int> { return 0; }\n"},
             Case{"method return type",
                  "struct S { pub v <int>, fun m(self: &Self) <NoSuchType> { return 0; } }\n"
                  "fun main() <int> { return 0; }\n"},
             Case{"class method return type",
                  "class C { pub v <int>, fun m(self: &Self) <NoSuchType> { return 0; } }\n"
                  "fun main() <int> { return 0; }\n"},
             // The operator return type was never booked -- it is the same two passes
             // over the same node, found while counting what the quiet pass covers,
             // and it would have stayed unmeasured because no test called an operator
             // whose return type failed. An operator *parameter* is still resolved by
             // one pass only: defineOperator stores a return type and nothing else,
             // so operators have no signature yet and `s + 1` is unchecked for arity
             // and argument type the way a method call was.
             Case{"operator return type",
                  "struct S { pub v <int>, pub operator + (other: <S>) <NoSuchType> { return 0; } }\n"
                  "fun main() <int> { return 0; }\n"},
             // The implements-block method parameter used to be a third case here and
             // is now a Soundness test of its own, below: the pass that duplicated it
             // was building a signature no one read, so deleting the pass fixed the
             // duplicate outright. The two that remain are the two passes that both
             // exist for a reason -- a signature pass that must run before any body,
             // and a body pass that must populate a scope -- so neither could be
             // deleted and one had to fall silent instead. Parameters are counted at
             // all four signature sites by AMethodParameterAnnotationIsStillReportedOnce
             // above; the two cases here are the two the KnownDefect carried.
             }) {
        const FincRun r = compile(c.code);
        EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u)
            << "one unresolved type, one diagnostic -- not one per pass over the signature\n"
            << c.what << "\n" << stripAnsi(r.err);
    }

    // The control, and the reason this is a two-pass defect and not a two-sites
    // defect: a plain function's signature is resolved once, so the same annotation
    // in the same position outside a struct is reported once.
    const FincRun once = compile("fun f(p: NoSuchType) <int> { return 0; }\n"
                                 "fun main() <int> { return 0; }\n");
    EXPECT_EQ(errorCount(stripAnsi(once.err)), 1u)
        << "a free function resolves its signature once\n" << stripAnsi(once.err);
}

TEST(Soundness_ErrorRecovery, AnImplementsBlockMethodParameterIsReportedOnce) {
    // Split out of the KnownDefect above when it went green. The implements block
    // resolved every parameter twice: once into a vector that StructType had nowhere to
    // put -- StructType::methods held a return type and nothing else -- and once in
    // visit(FunctionDeclaration&), which is the walk that has a use for them. Deleting
    // the first left the diagnostic and dropped the duplicate.
    //
    // The map holds a full FunctionType now (Soundness_MethodCalls), so the block
    // resolves its parameters again -- quietly, which is what keeps this at one.
    const FincRun r = compile("interface I { pub fun m(a: int) <int>; }\n"
                              "struct S { val <int> }\n"
                              "S implements <I> {\n"
                              "    pub fun m(a: NoSuchType) <int> { return 0; }\n"
                              "}\nfun main() <int> { return 0; }\n");
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u)
        << "one annotation, one diagnostic\n" << stripAnsi(r.err);
}

// ---------------------------------------------------------------------------
// Interface fields are not checked at all.
//
// Analyzer_Decl.cpp resolves an interface's member types without ever calling
// defineField, so StructType::implements (StructType.cpp) has no fields to
// compare and checks methods and constructors only. Every interface contract in
// the standard library is decorative until this is fixed, which is what makes it
// the most serious item in the plan rather than merely the oldest.
// ---------------------------------------------------------------------------

TEST(Soundness_Interfaces, AMissingMethodIsRejected) {
    // The control. Method checking works, which is what localises the defect to
    // fields: without this test, "interfaces are unchecked" would fit the
    // evidence just as well, and the fix would be aimed at the wrong function.
    auto r = compile(
        "interface I { fun m(self: &Self) <int>; }\n"
        "struct S : <I> { y <int>, }\n");
    EXPECT_NE(r.exitCode, 0) << "a struct missing a required method must be rejected";
    EXPECT_NE(r.err.find("does not implement"), std::string::npos) << r.err;
}

TEST(KnownDefect_Interfaces, AMissingFieldIsAccepted) {
    auto r = compile(
        "interface I { x <int>; }\n"
        "struct S : <I> { y <int>, }\n");
    EXPECT_EQ(r.exitCode, 0)
        << "FIXED: a struct that does not carry a required field is now rejected. "
           "Invert this to EXPECT_NE(r.exitCode, 0), move it to Soundness_Interfaces, "
           "and delete the interface-fields entry from docs/plan.md.";
}

TEST(KnownDefect_Interfaces, AMissingFieldIsAcceptedEvenWhenTheMethodsAreChecked) {
    // Sharper than the test above. Here the struct satisfies the method half, so
    // `implements` runs to completion and returns true anyway — the field is not
    // merely unchecked when nothing else is, it is invisible to a check that did
    // happen.
    auto r = compile(
        "interface I { x <int>; fun m(self: &Self) <int>; }\n"
        "struct S : <I> {\n"
        "  y <int>,\n"
        "  fun m(self: &Self) <int> { return 1; }\n"
        "}\n");
    EXPECT_EQ(r.exitCode, 0)
        << "FIXED: the field half of an interface contract is now checked "
           "alongside the method half. See KnownDefect_Interfaces.AMissingFieldIsAccepted.";
}

TEST(KnownDefect_Interfaces, AFieldOfTheWrongTypeIsAccepted) {
    // And the field is not checked even when it is present: `x <string>` where
    // the interface said `x <int>`. So the fix needs both a presence check and a
    // type comparison, and a fix that only adds presence will leave this failing.
    auto r = compile(
        "interface I { x <int>; }\n"
        "struct S : <I> { pub x <string>, }\n");
    EXPECT_EQ(r.exitCode, 0)
        << "FIXED: a required field's type is now compared. If the presence check "
           "landed but not this, that is the remaining half.";
}

// ---------------------------------------------------------------------------
// Integer widths are a lie.
//
// resolveTypeFromAST (Analyzer_Core.cpp) walks the `{N}` width annotation for
// side effects and returns the unannotated type, so uint{8} and uint{64} are one
// type. lib/std builds i64, i128, u64, u128 and size_t on top of this. Harmless
// exactly as long as there is no codegen, and wrong machine code the day there is.
// ---------------------------------------------------------------------------

TEST(KnownDefect_IntegerWidths, AWiderValueAssignsToANarrowerBinding) {
    auto r = compile(
        "fun main() <void> {\n"
        "  let a <uint{8}>;\n"
        "  let b <uint{64}> = a;\n"
        "}\n");
    EXPECT_EQ(r.exitCode, 0)
        << "FIXED: width annotations now produce distinct types. Widening may well "
           "be legal by a language decision — if so, keep this as Soundness and "
           "record the decision; the narrowing test below is the one that must reject.";
}

TEST(KnownDefect_IntegerWidths, ANarrowerParameterAcceptsAWiderArgument) {
    // This is the direction that cannot be excused by any implicit-conversion
    // rule: passing uint{64} where uint{8} was asked for truncates.
    auto r = compile(
        "fun f(x: uint{8}) <void> {}\n"
        "fun main() <void> {\n"
        "  let big <uint{64}>;\n"
        "  f(big);\n"
        "}\n");
    EXPECT_EQ(r.exitCode, 0)
        << "FIXED: a narrowing argument is now rejected. Invert and move to "
           "Soundness_IntegerWidths.";
}

TEST(KnownDefect_IntegerWidths, TheWidthIsAbsentFromDiagnosticText) {
    // Evidence for the *cause* rather than the symptom, and the reason this one is
    // worth its own test: the annotation is gone by the time anything can see it,
    // so a diagnostic about `uint{8}` says plain `uint`. That also makes the fix
    // observable without codegen — when the type carries its width, this text
    // changes, and a user reading `expected 'uint'` while having written
    // `uint{8}` is being told something untrue today.
    auto r = compile("fun main() <void> { let a <uint{8}> = -1; }\n");
    ASSERT_NE(r.exitCode, 0) << "expected a signedness mismatch here: " << r.err;
    const std::string err = stripAnsi(r.err);
    EXPECT_NE(err.find("expected 'uint'"), std::string::npos) << err;
    EXPECT_EQ(err.find("uint{8}'"), std::string::npos)
        << "FIXED: the diagnostic now names the annotated width. " << err;
}

// ---------------------------------------------------------------------------
// `any` is not a registered type. Analyzer_Core.cpp never registers it, so
// `fun f(x: any)` is `Undefined type 'any'`. The single highest-leverage fix for
// the standard library: all 25 interfaces in operators.fin take `other: any`,
// plus types.fin, prototypes.fin and enums.fin.
// ---------------------------------------------------------------------------

TEST(Soundness_DynamicTypes, AnyIsRegistered) {
    // Inverted from KnownDefect_AnyType.IsNotRegistered, which asserted `Undefined
    // type 'any'` and said "Invert this" in its own failure message.
    //
    // Kept as its own test rather than folded into AnyIsABuiltinAndNeedsNoImport,
    // because the program is different in a way that used to matter: the declaration
    // has *no initialiser*. Registration and inference are separate mechanisms, and
    // an `any` that only worked where something was assigned to it would pass every
    // other test in this suite.
    auto r = compile("fun main() <void> { let x <any>; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("Undefined type 'any'"), std::string::npos)
        << "`any` is registered in the SemanticAnalyzer constructor\n" << err;
    // Retained from the original: the fix belonged in registration, not in the lexer
    // or the grammar, and this is what says so.
    EXPECT_EQ(err.find("syntax error"), std::string::npos)
        << "`any` must still parse -- if it stopped parsing, that is a new defect: " << err;
    EXPECT_EQ(errorCount(err), 0u) << err;
}

// KnownDefect_AnyType.IsNotRegisteredAsAParameterTypeEither is deliberately not
// inverted into a test of its own. Its program was `fun f(x: any) <void> {}` and its
// point was the parameter position; that position is one of the seven in
// Soundness_DynamicTypes.AnyIsSpellableInEveryPositionTheCorpusWritesIt, which also
// covers the six the original missed. A second copy would be the redundancy this
// suite has removed elsewhere.

// ---------------------------------------------------------------------------
// The raise half of `blame` is rejected.
//
// Analyzer_Stmt.cpp requires the first operand to be bool and the second string,
// so `blame CollectionError("...")` — which is how collection.fin:63 and
// stdptr.fin:54 spell it — fails with `expected 'bool', got 'CollectionError'`.
// CONTEXT.md ratifies one keyword doing two jobs and the analyzer implements one.
// Per ADR 0008 the normative samples convict the compiler, so the analyzer changes.
// ---------------------------------------------------------------------------

TEST(Soundness_Blame, TheAssertHalfWorks) {
    // The control, in both spellings the corpus uses: blame_assert.fin:5 with a
    // message and :8 without.
    auto r = compile(
        "fun check(val: int) <void> {\n"
        "  blame val > 0, \"Value must be positive\";\n"
        "  blame val < 100;\n"
        "}\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(KnownDefect_Blame, RaisingAValueIsRejected) {
    auto r = compile(
        "struct E { pub m <string>, }\n"
        "fun main() <void> {\n"
        "  let e <E>;\n"
        "  blame e;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << "FIXED: `blame <value>` raises now. Invert this test.";
    const std::string err = stripAnsi(r.err);
    EXPECT_NE(err.find("expected 'bool'"), std::string::npos)
        << "still rejected, but no longer for the documented reason — read this "
           "before assuming the defect is unchanged: " << err;
}

// ---------------------------------------------------------------------------
// `true` and `false` are literals.
//
// Not in the plan's list, found while probing the one above: the lexer had no
// rule for either word, so both fell through to the identifier rule and the
// analyzer reported `Undefined variable 'true'` — in a condition, an initialiser
// and a return alike. Fixed in wave 2 with two lexer rules (`KW_TRUE`,
// `KW_FALSE` in lexer.l beside `"null"`) and two `literal` productions in
// parser.y building `Literal(text, ASTTokenKind::BOOL)`. Nothing downstream
// needed changing: ASTTokenKind::BOOL already existed (ast/nodes/ASTNode.hpp)
// and Analyzer_Expr.cpp already typed that kind as `bool`.
//
// These four tests were written pointing the other way, asserting the defect, and
// were inverted by the change that fixed it. They stay because the words are now
// keywords: a regression here is either a lost lexer rule or a lost production,
// and both show up as a nonzero exit on one of these four lines.
// ---------------------------------------------------------------------------

TEST(Soundness_BooleanLiterals, TrueIsALiteralInAnInitialiser) {
    auto r = compile("fun main() <void> { let b <bool> = true; }\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(Soundness_BooleanLiterals, FalseIsALiteralInAnInitialiser) {
    auto r = compile("fun main() <void> { let b <bool> = false; }\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(Soundness_BooleanLiterals, TrueIsALiteralInACondition) {
    auto r = compile("fun main() <void> { if (true) { } }\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(Soundness_BooleanLiterals, TrueIsALiteralInAReturn) {
    // The position that matters most: every `@special` predicate in the stdlib
    // returns one, e.g. error.fin's is_error_type.
    auto r = compile("fun f() <bool> { return true; }\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

// ---------------------------------------------------------------------------
// Attributes.
//
// Half of this defect is fixed and half is not, so the split is on purpose. The
// parser's attribute dispatch now has a ClassDeclaration branch (parser.y), and
// Attribute::accept now dispatches instead of having an empty body — both were
// wave 1 work. What remains is that nothing reads an attribute: `attributes` does
// not appear anywhere in src/semantics/ or src/driver/. So `#[export]`,
// `#[llvm_name=...]` and `#[use(compiler)]` reach the AST and stop there, which
// matters because `#[use(...)]` is the gate on the component API (CONTEXT.md).
// ---------------------------------------------------------------------------

TEST(Soundness_Attributes, AnAttributeOnAClassReachesTheAST) {
    // Regression lock on the fixed half. The bug was a dynamic_cast chain with no
    // ClassDeclaration branch, and ClassDeclaration does not derive from
    // StructDeclaration, so the attribute was dropped without a diagnostic. A
    // silent drop is invisible from the CLI, which is why this test reads the AST.
    fin::DiagnosticEngine diag("", "<test>");
    diag.setColorMode(fin::ColorMode::Never);
    auto parsed = parseSource("#[export]\nclass C { }\n", diag);
    ASSERT_TRUE(parsed.parsed) << "#[export] class C {} must parse";
    ASSERT_EQ(parsed.ast->statements.size(), 1u);
    auto* cls = dynamic_cast<fin::ClassDeclaration*>(parsed.ast->statements[0].get());
    ASSERT_NE(cls, nullptr) << "expected a ClassDeclaration";
    ASSERT_EQ(cls->attributes.size(), 1u)
        << "the attribute was dropped: parser.y's attribute dispatch lost its "
           "ClassDeclaration branch";
    EXPECT_EQ(cls->attributes[0]->name, "export");
}

TEST(KnownDefect_Attributes, AnUnknownAttributeIsAccepted) {
    // Nothing validates an attribute name, so a misspelled one is silently inert
    // — the failure mode CONTEXT.md's "known component name" rule exists to
    // prevent for components, applied one level up. Whether an unknown *attribute*
    // is an error is a language decision and not yet ratified; this test exists so
    // that decision is taken deliberately rather than discovered.
    auto r = compile("#[definitely_not_an_attribute]\nfun f() <void> {}\n");
    EXPECT_EQ(r.exitCode, 0)
        << "CHANGED: attribute names are validated now. If that was a ratified "
           "language decision, move this to Soundness_Attributes and invert it.";
}

// ---------------------------------------------------------------------------
// Field declaration order is destroyed.
//
// StructType::fields is std::unordered_map<std::string, FieldInfo> and
// defineField is its only writer, so the order the fields were declared in is
// lost when the semantic type is built. The AST has it; the type throws it away.
// Layout, ABI, offsets and every pointer map are functions of declaration order.
//
// This is the one defect here with no CLI-visible symptom, because nothing yet
// consumes layout. It is asserted at the type instead, and that comes with a
// limit worth stating plainly: the test reads `fields`, so a fix that adds a
// separate ordered accessor and leaves `fields` alone will not trip it. It is
// evidence that order is lost, not a guarantee that it stays lost. Whoever fixes
// this should replace the body rather than trust it.
// ---------------------------------------------------------------------------

TEST(KnownDefect_FieldOrder, TheSemanticTypeCannotReproduceDeclarationOrder) {
    // Several candidate field-name sets, because whether one particular set comes
    // back in declaration order is an accident of the hash. If order is preserved,
    // *every* set round-trips; one mismatch proves it is not preserved. That makes
    // the test independent of the standard library's hash function, which a test
    // asserting one specific wrong order would not be.
    //
    // Do not reduce this to one candidate. Measured against both containers: all
    // five misorder under an unordered_map, but only three of the five misorder
    // under a std::map — `{a..f}` and `{first,second,third}` are already in
    // alphabetical order and would round-trip. So a single-candidate version using
    // either of those would report "fixed" if someone swapped the container for a
    // std::map, which does not preserve declaration order either. The set is the
    // test.
    const std::vector<std::vector<std::string>> candidates = {
        {"alpha", "beta", "gamma", "delta"},
        {"a", "b", "c", "d", "e", "f"},
        {"first", "second", "third"},
        {"x", "y", "z", "w", "v", "u", "t", "s"},
        {"header", "payload", "checksum", "footer", "reserved"},
    };

    bool anyMisordered = false;
    std::string report;

    for (const auto& names : candidates) {
        std::string src = "struct S {";
        for (const auto& n : names) src += " pub " + n + " <int>,";
        src += " }\n";

        fin::DiagnosticEngine diag("", "<test>");
        diag.setColorMode(fin::ColorMode::Never);
        auto parsed = parseSource(src, diag);
        ASSERT_TRUE(parsed.parsed) << src;

        fin::SemanticAnalyzer analyzer(diag, false);
        analyzer.visit(*parsed.ast);
        ASSERT_FALSE(diag.hasErrors()) << src;

        auto st = std::dynamic_pointer_cast<fin::StructType>(
            analyzer.getGlobalScope()->resolveType("S"));
        ASSERT_NE(st, nullptr) << "the analyzer must publish a struct type for S";
        ASSERT_EQ(st->fields.size(), names.size()) << "a field went missing entirely";

        std::vector<std::string> seen;
        for (const auto& kv : st->fields) seen.push_back(kv.first);
        if (seen != names) {
            anyMisordered = true;
            report = "declared:";
            for (const auto& n : names) report += " " + n;
            report += " / recovered:";
            for (const auto& n : seen) report += " " + n;
            break;
        }
    }

    EXPECT_TRUE(anyMisordered)
        << "FIXED, or the hash got lucky " << candidates.size() << " times in a row. "
           "If StructType now records declaration order, delete this test and assert "
           "the real ordered accessor instead — see this test's comment for why it "
           "cannot detect that itself.";
    if (anyMisordered) {
        // Not an assertion. Printed so the run carries the evidence rather than
        // just a green tick, since a passing test here means a live defect.
        ::testing::Test::RecordProperty("misordered", report);
    }
}

// ---------------------------------------------------------------------------
// Conditions are not type-checked.
//
// `if (1)`, `if ("s")`, `while (1)` and a `for` header's `1` all compile clean.
// Not in the plan's list, and not findable by counting samples: no sample fails
// because of it, because accepting too much never fails.
//
// What makes this a defect rather than a design choice is that the analyzer
// already decided the question twice, in the other direction, and once in this
// very file. `let b <bool> = 1;` is rejected. `blame 1;` is rejected with
// `expected 'bool', got 'int'` — the check at Analyzer_Stmt.cpp:116. And the
// same check is *written* for `if` at Analyzer_Stmt.cpp:34 and commented out,
// justified as "(optional, C++ allows int)". Fin is not C++ and has already
// refused the C++ rule in the two places above, so the comment's premise is
// contradicted twenty lines away from itself. `WhileLoop` (:39) and `ForLoop`
// (:47) do not even carry the commented-out line.
//
// The corpus does not license the loose reading. Every condition in all fifty
// samples is boolean-valued — comparisons, `!`, `bool` fields, calls returning
// `bool`, and `true`/`false`. Nothing writes `if (ptr)` or `if (n)`:
// nullability is always spelled `== null`, in both `//@ ok` samples that have a
// condition at all (`blame_assert.fin`, `deeptest3.fin`).
//
// One sample does write it: `undefined_behavior.fin` has `if (0)` twice, and it
// is *normative*. It licenses nothing, and the reason is worth stating because
// it is a property of the harness rather than of that file. Its expectation is
// `//@ error`, and this runner reads `//@ error` as "at least this diagnostic"
// — `checkSampleAgainstFinc` searches stderr for the message and never asserts
// it is the only one. So an `//@ error` sample cannot make anything legal; only
// `//@ ok` licenses. `if (0)` there is the C idiom for a provably-dead branch,
// written to demonstrate return-path analysis, and the file says nothing about
// conditions either way.
//
// So this is pinned rather than fixed, because the fix is one uncommented line
// that changes what the language accepts, and `undefined_behavior.fin` — a
// normative sample — stops compiling the moment it lands. Under ADR 0008 a
// normative sample changes only by a deliberate language decision, so enabling
// the check and rewriting that sample's `if (0)` to `if (false)` is one ruling,
// not two edits. If the ruling goes the other way, delete these tests and
// delete the commented-out line, so that the next reader does not find the same
// contradiction and reopen it.
// ---------------------------------------------------------------------------

// The controls. These are what make the tests below evidence of an
// inconsistency rather than merely of a missing check: the bool check exists,
// it works, and it is applied everywhere except in a condition.
TEST(Soundness_Conditions, ABoolBindingStillRejectsAnInteger) {
    auto r = compile("fun main() <void> { let b <bool> = 1; }\n");
    EXPECT_NE(r.exitCode, 0);
    EXPECT_NE(stripAnsi(r.err).find("expected 'bool'"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_Conditions, BlameStillRejectsAnInteger) {
    auto r = compile("fun main() <void> { blame 1; }\n");
    EXPECT_NE(r.exitCode, 0);
    EXPECT_NE(stripAnsi(r.err).find("expected 'bool'"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(KnownDefect_Conditions, AnIfAcceptsAnInteger) {
    auto r = compile("fun main() <void> { if (1) { } }\n");
    EXPECT_EQ(r.exitCode, 0)
        << "FIXED: conditions are checked now. Invert these and rule on "
           "undefined_behavior.fin's `if (0)` — see this block's comment.\n"
        << stripAnsi(r.err);
}

TEST(KnownDefect_Conditions, AnIfAcceptsAString) {
    // A separate test from the integer because "C allows int" is at least an
    // argument, and there is none at all for a string. If a ruling ever does
    // admit truthiness, it has to say what this line means.
    auto r = compile("fun main() <void> { if (\"s\") { } }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(KnownDefect_Conditions, AnIfAcceptsANonBoolVariable) {
    // Not a literal, so this cannot be dismissed as constant folding.
    auto r = compile("fun main() <void> { let n <int> = 3; if (n) { } }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(KnownDefect_Conditions, AWhileAcceptsAnInteger) {
    // Separate from `if` because the fix is separate: WhileLoop has no
    // commented-out check to uncomment, so a fix that only edits line 34
    // leaves this one passing.
    auto r = compile("fun main() <void> { while (1) { } }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(KnownDefect_Conditions, AForHeaderAcceptsAnInteger) {
    auto r = compile("fun main() <void> { for (let i <int> = 0; 1; i = i + 1) { } }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

// ---------------------------------------------------------------------------
// A binary operator checks that its operands agree with each other, never that
// the operator accepts them.
//
// Analyzer_Expr.cpp:176 ends visit(BinaryOp&) with
//
//     if (!checkType(node, rightType, leftType)) { lastExprType = nullptr; }
//     else { lastExprType = leftType; }
//
// which asks one question — are the two sides the same type — and answers the
// other one by assumption. So `1 + "s"` is rejected, correctly but for the wrong
// reason (the operands disagree, not that `+` is undefined on strings), and
// `"a" - "b"`, `"a" % "b"`, `true + false` and `true / false` are all accepted,
// each yielding the operand type.
//
// The same function already does this right twice. At :158 `&&` and `||` check
// both operands against `bool` and yield `bool`; at :148 an operand-specific
// lookup handles struct operator overloads. So the fix is a third case of a shape
// the file already contains, not new machinery — which is the argument for
// treating this as a defect rather than an unimplemented feature.
//
// Nothing in the corpus licenses the loose reading. There is no `+` on strings in
// any of the fifty samples; string building is `format!(...)` throughout
// (deeptest2.fin:26, error.fin:19, stdio.fin:36), so string arithmetic is not a
// feature waiting for a checker to catch up with it.
//
// Comparisons at :168 share the shape — `checkType(right, left)` then yield
// `bool` — and accept `"x" < "y"` and `true < false`. Those are deliberately not
// asserted here: ordering strings is a real operation in many languages, and C++
// orders `bool`, so "the operator accepts these operands" may well be the right
// answer for `<`. The defect is that the question is never asked; where the
// answer would be yes, no test should pretend otherwise.
// ---------------------------------------------------------------------------

TEST(Soundness_BinaryOperators, IntegerArithmeticStillCompiles) {
    // The control that costs the fix something. Any repair narrow enough to
    // reject `"a" - "b"` must still admit this.
    auto r = compile("fun main() <void> { let a <int> = 1 + 2 * 3 - 4 % 5; }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(Soundness_BinaryOperators, MismatchedOperandsAreStillRejected) {
    // This is the check that does exist, and pinning it is what makes the
    // KnownDefects below diagnostic rather than anecdotal: they are not "no type
    // checking here", they are "the wrong question, asked properly".
    auto r = compile("fun main() <void> { let a <int> = 1 + \"s\"; }\n");
    EXPECT_NE(r.exitCode, 0);
    EXPECT_NE(stripAnsi(r.err).find("Type mismatch"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_BinaryOperators, LogicalAndStillRequiresBooleanOperands) {
    // The proof that this file already knows how to check an operator against its
    // operands. If this ever fails, the fix for the defects below went in by
    // deleting the working case rather than joining it.
    auto r = compile("fun main() <void> { let a <bool> = 1 && 2; }\n");
    EXPECT_NE(r.exitCode, 0);
    EXPECT_NE(stripAnsi(r.err).find("expected 'bool'"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(KnownDefect_BinaryOperators, SubtractionAcceptsTwoStrings) {
    // Asserting the *result type*, not merely that it compiles: this passes only
    // if `"a" - "b"` was typed `string`, which is the pass-through at :176 rather
    // than some unrelated leniency.
    auto r = compile("fun main() <void> { let a <string> = \"a\" - \"b\"; }\n");
    EXPECT_EQ(r.exitCode, 0)
        << "FIXED: a binary operator now checks that it accepts its operands. "
           "Invert this and the two below to EXPECT_NE, move them to "
           "Soundness_BinaryOperators, and strike the entry from docs/plan.md.\n"
        << stripAnsi(r.err);
}

TEST(KnownDefect_BinaryOperators, ModuloAcceptsTwoStrings) {
    // Separate from subtraction because `%` is the case with no reading at all:
    // a fix that whitelists `+` and `-` for strings on the way to concatenation
    // would leave this one accepted, and it should not.
    auto r = compile("fun main() <void> { let a <string> = \"a\" % \"b\"; }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(KnownDefect_BinaryOperators, ArithmeticAcceptsTwoBooleans) {
    // A different operand kind, so a numeric whitelist that admits `bool` by way
    // of integer promotion would leave this green while the string cases go red.
    auto r = compile("fun main() <void> { let a <bool> = true / false; }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

// ---------------------------------------------------------------------------
// A unary operator other than `&` and `*` returns its operand's type without
// checking anything.
//
// visit(UnaryOp&) handles two operators and then gives up. Analyzer_Expr.cpp:207:
//
//     else { lastExprType = type; }
//
// `&` builds a PointerType, `*` unwraps one and errors on a non-pointer, and
// every other operator — `!`, unary `-`, `~`, `++`, `--` — falls into that
// clause. So `!` on an integer yields `int` and unary `-` on a string yields
// `string`.
//
// This defect hides behind the binding it appears in, which is why it took a
// deliberate probe to see. `let a <bool> = !1;` *is* rejected, so `!` looks
// checked; the diagnostic is "expected 'bool', got 'int'" from the initialiser,
// because `!1` was typed `int` and the binding caught it. Give the binding the
// pass-through type instead — `let a <int> = !1;` — and it compiles. Every test
// below therefore states the type it expects the expression to have, since
// exit-code-only assertions here measure the binding rather than the operator.
// ---------------------------------------------------------------------------

TEST(Soundness_UnaryOperators, NegationAndNotStillWorkOnTheRightTypes) {
    auto r = compile("fun main() <void> { let a <bool> = !true; let b <int> = -1; }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(Soundness_UnaryOperators, DereferencingANonPointerIsStillRejected) {
    // The control. One branch of this function does check its operand, so the
    // defect is the missing cases and not the function being unreachable.
    auto r = compile("fun main() <void> { let n <int> = 1; let a <int> = *n; }\n");
    EXPECT_NE(r.exitCode, 0);
    EXPECT_NE(stripAnsi(r.err).find("Cannot dereference"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(KnownDefect_UnaryOperators, NotPassesAnIntegerStraightThrough) {
    // `<int>`, not `<bool>`: this compiling is the proof that `!1` is typed
    // `int`. With `<bool>` the file already rejects it, for the wrong reason.
    auto r = compile("fun main() <void> { let a <int> = !1; }\n");
    EXPECT_EQ(r.exitCode, 0)
        << "FIXED: `!` now requires a boolean operand. Invert this and the one "
           "below, move them to Soundness_UnaryOperators, and strike the entry "
           "from docs/plan.md.\n"
        << stripAnsi(r.err);
}

TEST(KnownDefect_UnaryOperators, NegationPassesAStringStraightThrough) {
    // Separate operator, separate case in the fix: `!` wants `bool` and `-`
    // wants a number, so a repair that only learns about `!` leaves this green.
    auto r = compile("fun main() <void> { let a <string> = -\"s\"; }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

// ---------------------------------------------------------------------------
// Redeclaring a name in one scope silently replaces the first declaration.
//
// Scope.hpp:30 is the whole of it:
//
//     void define(Symbol sym) { symbols[sym.name] = sym; }
//
// `operator[]` on an unordered_map assigns over an existing key, so a second
// declaration overwrites the first with no diagnostic and the later one wins.
// Both `let` and `fun` reach it: variables at Analyzer_Decl.cpp:27, functions at
// Analyzer_Decl.cpp:86, which register into the parent scope through the same
// call. One line, one fix, both symptoms.
//
// Unlike the conditions defect, this one does not need an owner ruling — the
// corpus already answered it in writing. stdlib/stdio.fin:33 declares a second
// `printf` under `#[overwrite(printf)]`, commented in the sample itself as
// "this tells the compiler we are overwriting printf and it will ignore the
// current definition of it and doesn't raise an error". An attribute whose stated
// job is to suppress the error presupposes the error, and stdio.fin is normative.
// So a bare duplicate must be rejected, and silently taking the second is exactly
// the behaviour `#[overwrite]` exists to have to ask for.
//
// Two defects currently cancel in that sample. Attributes are parsed and carried
// but nothing interprets one (docs/plan.md, wave 4), so `#[overwrite]` does
// nothing today — and the duplicate it guards is accepted anyway, because no
// duplicate is ever refused. Whoever fixes this line must therefore land it with
// the attribute, or stdio.fin starts failing on a construct it was written to
// demonstrate. That ordering constraint is the reason this is recorded here
// rather than filed as a one-line fix.
//
// Not asserted as defects, because they are ordinary: shadowing in a nested
// scope, and a `let` shadowing a parameter. Both were probed, both are accepted,
// and neither is a redeclaration in *one* scope.
// ---------------------------------------------------------------------------

TEST(Soundness_Duplicates, AnUndeclaredNameIsStillRejected) {
    // The control: resolution does fail when it should, so the defect is the
    // missing check at definition and not a scope that admits anything.
    auto r = compile("fun main() <void> { let a <int> = nosuchvar; }\n");
    EXPECT_NE(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(Soundness_Duplicates, ShadowingInANestedScopeStillWorks) {
    // Pinned so the fix stays aimed at one scope. The cheap repair — reject any
    // name that already resolves — would break this, and it is legitimate.
    auto r = compile("fun main() <void> { let a <int> = 1; { let a <string> = \"s\"; } }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(KnownDefect_Duplicates, ARedeclarationSilentlyChangesTheType) {
    auto r = compile("fun main() <void> { let a <int> = 1; let a <string> = \"s\";\n"
                     "                    let b <string> = a; }\n");
    EXPECT_EQ(r.exitCode, 0)
        << "FIXED: a duplicate declaration in one scope is now refused. Check it "
           "was landed together with #[overwrite] support, or stdlib/stdio.fin "
           "breaks; then invert this and the one below into Soundness_Duplicates "
           "and strike the entry from docs/plan.md.\n"
        << stripAnsi(r.err);
}

TEST(KnownDefect_Duplicates, ARedeclaredFunctionSilentlyReplacesTheFirst) {
    // Binding `<string>` is what proves the second definition won: the call sees
    // the later signature, so the first `f` became unreachable without a word.
    auto r = compile("fun f() <int> { return 1; }\n"
                     "fun f() <string> { return \"s\"; }\n"
                     "fun main() <void> { let x <string> = f(); }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

// ---------------------------------------------------------------------------
// Not a defect: a call must follow its declaration, and `@define` is how the
// corpus says so.
//
// A probe found that `fun main() <void> { g(); } fun g() <void> {}` is rejected
// with "Undefined function or type 'g'", and that mutual recursion between two
// plain definitions is impossible for the same reason: Analyzer_Core.cpp:195
// walks top-level statements once in source order, and a function registers
// itself in the enclosing scope at Analyzer_Decl.cpp:86 partway through its own
// visit — after its predecessors, before its body.
//
// That reads like a serious defect and is not one. Declaration-before-use with an
// explicit prototype is C's design, and the corpus follows it without exception:
// zero of the fifty samples call a top-level function declared later, and eight
// of them open with `@define printf(fmt: string, ...) <noret>;` (letssee.fin:3,
// enums.fin:5, complex.fin:5, readonly.fin:5, hashmap.fin:10, and others). The
// mechanism works, including for mutual recursion, which the tests below pin.
//
// It is recorded here because a finding characterised and dismissed is worth as
// much as one confirmed, and cheaper to write down than to re-derive. Hoisting
// every top-level signature would make `@define` decorative and change what the
// eight samples above are demonstrating — so it is a language decision (ADR
// 0008), not a repair, and nobody should reach for it on the strength of the
// probe alone.
// ---------------------------------------------------------------------------

TEST(Soundness_Declarations, ADefineLetsACallPrecedeTheDefinition) {
    auto r = compile("@define g() <void>;\n"
                     "fun main() <void> { g(); }\n"
                     "fun g() <void> {}\n");
    EXPECT_EQ(r.exitCode, 0)
        << "@define is the corpus's forward declaration and eight samples open "
           "with one; if this fails they all do.\n"
        << stripAnsi(r.err);
}

TEST(Soundness_Declarations, MutualRecursionWorksThroughADefine) {
    // The case that makes declaration-before-use liveable. Without this the
    // language would have no way to write two functions that call each other,
    // and the ordering rule really would be a defect.
    auto r = compile("@define b() <void>;\n"
                     "fun a() <void> { b(); }\n"
                     "fun b() <void> { a(); }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

// ---------------------------------------------------------------------------
// `cast` admits any primitive to any other, and refuses both casts the corpus
// actually writes. One rule, wrong in both directions.
//
// Analyzer_Expr.cpp:361 is the too-broad half:
//
//     else if (dynamic_cast<const PrimitiveType*>(sourceType.get()) &&
//              dynamic_cast<const PrimitiveType*>(targetType.get())) valid = true;
//
// and Analyzer_Core.cpp:21 registers `string` as a `PrimitiveType`. So `string`
// is lumped in with the numerics and `cast<int>("s")`, `cast<string>(1)` and
// `cast<bool>("s")` are all accepted. Turning a string into an int is not a
// conversion, it is parsing; the other direction is formatting. Neither is a
// reinterpretation of a value, which is what a cast is for.
//
// The same rule is too narrow, and this is the half that matters more, because
// the corpus depends on it. Both casts written in the fifty samples are rejected
// today: `cast<[char]>("SomeData for testing")` (stdio.fin:156) fails with
// "Invalid cast from 'string' to '[char]'" because `[char]` is an ArrayType and
// so the pair is not two primitives, and `cast<&auto>(base_val)`
// (deeptest2.fin:26) fails the same way for a PointerType. A permissive rule
// that still refuses the only two real uses is not erring on the side of
// permissive — it is not looking at the question.
//
// Both are masked. stdio.fin and deeptest2.fin are `//@ unimplemented` for
// unrelated reasons, so neither cast is reached and no test goes red for either.
// Whoever repairs this should expect those two samples to be the acceptance
// criteria and not to find them in the failing list first.
//
// A related *grammar* gap, reported to the frontend owner rather than fixed
// here: `cast<&int>(p)` is a syntax error ("unexpected TYPE_INT, expecting
// IDENTIFIER") while `cast<&auto>(n)` parses, because the cast's type argument
// accepts `&IDENTIFIER` but not `&`-plus-a-builtin-type-keyword. So the pointer
// half of this defect cannot even be written down in a test yet.
// ---------------------------------------------------------------------------

TEST(Soundness_Casts, NumericConversionStillWorks) {
    // The control that constrains the fix: narrowing the primitive rule must not
    // take the numeric casts with it.
    auto r = compile("fun main() <void> { let a <float> = cast<float>(1); }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(Soundness_Casts, AStructToAnIntIsStillRejected) {
    // Proof the function does refuse things, so the accepts below are the rule
    // being wrong rather than the check being absent.
    auto r = compile("struct S { }\n"
                     "fun main() <void> { let a <int> = cast<int>(S()); }\n");
    EXPECT_NE(r.exitCode, 0);
    EXPECT_NE(stripAnsi(r.err).find("Invalid cast"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_Casts, ACastToOrFromADynamicTypeIsAllowed) {
    // Casting out of a dynamic type is what a dynamic type is for, and the corpus
    // writes it twice. nullifier.fin:12 declares `pub static fun unpack(v: any) <T>`
    // whose body is `return cast<T>(v);` with the comment "raises a panic if cast
    // fails" -- a cast out of `any`, checked at runtime, by design. stdlib/types.fin:33
    // declares the library's own `cast`: `pub fun cast<_Type: $type>(value: Any<...>)
    // <_Type>`, whose parameter is dynamic for every call in the language.
    //
    // Both of these said `Invalid cast from 'any' to 'int'`. The corpus site survived
    // only because its target `T` is a generic parameter, and the cast rule lets any
    // generic through -- so the corpus never showed the defect and the standard
    // library's own signature was the thing that would have.
    //
    // The rule is stated for either side. A cast *into* `any` is the boxing operation
    // and has to be writable for the reverse to mean anything, and stdlib/types.fin:90
    // takes `v: any` from callers who had a concrete value.
    for (const char* code : {"fun main() <noret> { let a <any> = 1; let b <int> = cast<int>(a); }\n",
                             "fun main() <noret> { let a <object> = 1; let b <int> = cast<int>(a); }\n",
                             "fun main() <noret> { let a <any> = cast<any>(1); }\n",
                             "fun main() <noret> { let a <any> = 1; let b <object> = cast<object>(a); }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(errorCount(stripAnsi(r.err)), 0u) << code << stripAnsi(r.err);
    }
    // Soundness_Casts.AStructToAnIntIsStillRejected is this rule's control: widening
    // the cast for dynamic types must not widen it for everything.
}

TEST(KnownDefect_Casts, AStringToAnIntegerIsAccepted) {
    auto r = compile("fun main() <void> { let a <int> = cast<int>(\"s\"); }\n");
    EXPECT_EQ(r.exitCode, 0)
        << "FIXED: `cast` no longer treats `string` as interchangeable with the "
           "numerics. Invert this and the one below into Soundness_Casts, and "
           "check the two too-narrow tests below now pass.\n"
        << stripAnsi(r.err);
}

TEST(KnownDefect_Casts, AnIntegerToAStringIsAccepted) {
    // Separate direction, separate fix: a repair that adds a string-to-number
    // parse would leave this one accepted, and formatting is not a cast either.
    auto r = compile("fun main() <void> { let a <string> = cast<string>(1); }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(KnownDefect_Casts, TheCorpusOwnStringToCharArrayCastIsRejected) {
    // stdio.fin:156 writes exactly this. The `KnownDefect` here asserts a
    // *rejection*, which is the opposite shape to every other test in this file:
    // the defect is that a legitimate cast fails, so the assertion that must
    // eventually flip is EXPECT_NE, not EXPECT_EQ.
    auto r = compile("fun main() <void> { let a <[char]> = cast<[char]>(\"data\"); }\n");
    EXPECT_NE(r.exitCode, 0)
        << "FIXED: string to [char] now converts, which is what stdio.fin:156 "
           "needs. Invert to EXPECT_EQ and move to Soundness_Casts.";
    EXPECT_NE(stripAnsi(r.err).find("Invalid cast"), std::string::npos)
        << "still failing, but no longer on the cast — check this is not now a "
           "parse error, which would mean the grammar regressed:\n"
        << stripAnsi(r.err);
}

TEST(KnownDefect_Casts, TheCorpusOwnPointerCastIsRejected) {
    // deeptest2.fin:26 writes `cast<&auto>(base_val)`. Written with `&auto`
    // rather than `&int` on purpose: `cast<&int>(p)` does not parse at all, so
    // `&auto` is the only spelling in which this defect is reachable today.
    auto r = compile("fun main() <void> { let n <int> = 1; let a <&auto> = cast<&auto>(n); }\n");
    EXPECT_NE(r.exitCode, 0)
        << "FIXED: a pointer cast is accepted, which deeptest2.fin:26 needs.";
    EXPECT_NE(stripAnsi(r.err).find("Invalid cast"), std::string::npos)
        << stripAnsi(r.err);
}

// ---------------------------------------------------------------------------
// A generic bound is never enforced, and on a function it is never even read.
// `debugLog` is the only consumer of a resolved constraint in the compiler.
//
// Two sites, two different failures. `Analyzer_Decl.cpp:40-42` registers a
// function's generic parameters and never mentions `gen->constraint` at all:
//
//     for (auto& gen : node.generic_params) {
//         currentScope->defineType(gen->name, std::make_shared<GenericType>(gen->name));
//     }
//
// so `fun f<T: NoSuchI>(x: T)` compiles — the bound is invisible, and a typo in
// one is silent. `Analyzer_Decl.cpp:117-122` (structs) and `:560-567` do read it:
//
//     if (gen->constraint) {
//         auto constraintType = resolveTypeFromAST(gen->constraint.get());
//         if (constraintType) {
//             debugLog(..., "      [Constraint] Generic '{}' : '{}'\n", ...);
//         }
//     }
//
// which resolves the bound — so a bogus name *is* caught there — and then passes
// the result to a debug message and drops it. It is never stored on the
// `GenericType` and never consulted at an instantiation, so `S<int>` satisfies
// `<T: I>` for any `I`.
//
// The asymmetry is the useful part of this finding: the same construct is
// rejected on a struct and accepted on a function, which localises the first fix
// to three lines that already exist twenty lines away in the same file. The
// second fix — storing the bound and checking arguments against it — is real
// work, and it is what wave 3's monomorphisation needs anyway.
//
// **Both halves of the first fix have landed; read the rest of this comment as
// history.** There were seven copies of that loop, not two: function, struct,
// class, interface, interface-operator, implements-block operator, and the `fn<T>`
// type node, plus the lambda expression. All eight now call
// SemanticAnalyzer::declareGenericParams (Analyzer_Core.cpp), which declares every
// parameter name first and *then* resolves the bounds — two passes, so a bound may
// name a sibling parameter or the one it constrains, which the single-pass struct
// version rejected. The resolved bound is assigned to `GenericType::constraint`
// instead of being logged and dropped.
//
// That one assignment turned out to un-deaden two separate readers, which is the
// part worth remembering: `checkConstraint` (Analyzer_Core.cpp) and `getStructType`
// (Analyzer_Expr.cpp:27-33, "If T : Interface, treat T as Interface") both read
// that field behind a null guard, so both had been unreachable since they were
// written. The second is why a method call on a bounded parameter works now.
//
// The two KnownDefects below survive the fix, and for a reason that is no longer
// the one written above: the bound *is* stored and *is* consulted now.
// checkConstraint only reports when the argument is itself a StructType, so
// `S<int>` against `<T: I>` passes because `int` is a PrimitiveType and there is
// no ruling yet on whether a primitive satisfies an interface bound — `Castable`
// and `Any` are erasure markers (ADR 0018) that primitives must surely satisfy, so
// rejecting every non-struct is not obviously right. Function *calls* are not
// checked against bounds at any point, which is the other one.
//
// This is the defect the standard library is most exposed to after interface
// fields. `stdio.fin` alone declares `<X: Any<Printable>>` three times and
// `<T: Strict<Stream>>` once, and the erasure markers `Any` and `Castable`
// (ADR 0018) are *spelled as bounds* — so "bounds are decorative" means the
// erasure machinery has nothing to read. A bound that cannot be trusted is worse
// than no generics for a library author, because the signature documents a
// guarantee the compiler does not make.
// ---------------------------------------------------------------------------

TEST(Soundness_GenericBounds, ABogusBoundOnAStructIsRejected) {
    // The control, and the half of the asymmetry that works. Without it "bounds
    // are ignored" would fit the evidence and the fix would be aimed at the
    // wrong function.
    auto r = compile("struct S<T: NoSuchI> { x <T>, }\n"
                     "fun main() <void> {}\n");
    EXPECT_NE(r.exitCode, 0);
    EXPECT_NE(stripAnsi(r.err).find("Undefined type 'NoSuchI'"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_GenericBounds, ArityIsStillCheckedOnAGenericFunction) {
    // Proves a generic call is checked at all, so the accepts below are the bound
    // being ignored rather than generic calls being skipped wholesale.
    auto r = compile("fun f<T>(x: T) <noret> {}\n"
                     "fun main() <void> { f(1,2); }\n");
    EXPECT_NE(r.exitCode, 0);
    EXPECT_NE(stripAnsi(r.err).find("expects 1 arguments"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_GenericBounds, ABogusBoundOnAFunctionIsRejected) {
    // Was KnownDefect_GenericBounds.ABogusBoundOnAFunctionIsAccepted. The
    // asymmetry this suite was built around -- same text, rejected on a struct and
    // accepted on a function -- is gone: all seven sites that can declare a
    // generic parameter now go through SemanticAnalyzer::declareGenericParams,
    // which resolves the bound. Compare with the struct control above; they must
    // agree from here on.
    auto r = compile("fun f<T: NoSuchI>(x: T) <noret> {}\n"
                     "fun main() <void> {}\n");
    EXPECT_NE(r.exitCode, 0) << stripAnsi(r.err);
    EXPECT_NE(stripAnsi(r.err).find("Undefined type 'NoSuchI'"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_GenericBounds, ABogusBoundOnAnInterfaceIsRejected) {
    auto r = compile("interface I<T: NoSuchI> { pub fun m() <int>; }\n");
    EXPECT_NE(r.exitCode, 0) << stripAnsi(r.err);
    EXPECT_NE(stripAnsi(r.err).find("Undefined type 'NoSuchI'"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_GenericBounds, ABogusBoundOnAnOperatorIsRejected) {
    // The operator site is reached twice -- once for an operator *requirement*
    // inside an interface and once for an operator *definition* inside an
    // implements block -- and both had their own copy of the loop.
    auto r = compile(
        "interface Marker { pub fun mk() <int>; }\n"
        "struct P { x <int> }\n"
        "P implements <Marker> {\n"
        "    pub fun mk() <int> { return 1; }\n"
        "    operator + : <T: NoSuchI>(other: <T>) <int> { return 1; }\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << stripAnsi(r.err);
    EXPECT_NE(stripAnsi(r.err).find("Undefined type 'NoSuchI'"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_GenericBounds, AMethodInAnImplementsBlockDeclaresItsOwnGenerics) {
    // A false *rejection*, found while probing the six sites above, and the
    // opposite failure mode to the rest of this suite: visit(ImplementsBlock&)
    // resolved each method's parameter types in the block's scope without opening
    // one for the method or declaring its generic_params, so `T` was undefined
    // inside its own signature.
    auto r = compile(
        "interface Marker { pub fun mk() <int>; }\n"
        "struct S { v <int> }\n"
        "S implements <Marker> {\n"
        "    pub fun mk() <int> { return 1; }\n"
        "    pub fun m<T>(item: T) <int> { return 1; }\n"
        "}\n");
    EXPECT_EQ(stripAnsi(r.err).find("Undefined type 'T'"), std::string::npos)
        << "a method's own generic parameter must be in scope for its signature:\n"
        << stripAnsi(r.err);
}

TEST(Soundness_GenericBounds, ABoundIsVisibleToMethodResolution) {
    // Storing the bound on the GenericType did more than enable the check below:
    // getStructType (Analyzer_Expr.cpp:27-33) already said "If T : Interface,
    // treat T as Interface" and had been dead for exactly as long, because the
    // field it reads was never assigned. So a method call on a bounded type
    // parameter now resolves, which is the defect `tests/samples/interfaces.fin`
    // was pinned on -- it reported `Type 'T' does not have methods` for
    // `item.to_string()` at :17. This test is what keeps that working.
    auto r = compile(
        "interface Printable { pub fun to_string() <string>; }\n"
        "fun show<T: Printable>(item: T) <string> { return item.to_string(); }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
    EXPECT_EQ(stripAnsi(r.err).find("does not have methods"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_GenericBounds, AnUnboundedParameterStillHasNoMethods) {
    // The other side of the test above, and the one that keeps the fix honest:
    // resolving methods *through* a bound must not mean resolving methods on
    // anything shaped like a generic. Without a bound there is nothing to consult
    // and the call has to stay an error.
    auto r = compile(
        "fun show<T>(item: T) <string> { return item.to_string(); }\n");
    EXPECT_NE(r.exitCode, 0) << stripAnsi(r.err);
    EXPECT_NE(stripAnsi(r.err).find("does not have methods"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(KnownDefect_GenericBounds, AnArgumentViolatingAFunctionBoundIsAccepted) {
    // `int` does not implement `I`, and `f` says it requires it. The bound is
    // resolved and stored now (see the Soundness tests above), so what is missing
    // here is the call site: nothing on the path through visit(FunctionCall&) looks
    // at the callee's generic parameters at all, so there is no place the bound
    // would be consulted even though it is finally there to consult.
    auto r = compile("interface I { fun m() <int>; }\n"
                     "fun f<T: I>(x: T) <noret> {}\n"
                     "fun main() <void> { f(1); }\n");
    EXPECT_EQ(r.exitCode, 0)
        << "FIXED: an argument is checked against its generic bound.\n"
        << stripAnsi(r.err);
}

TEST(KnownDefect_GenericBounds, AStructInstantiationViolatingItsBoundIsAccepted) {
    // Still accepted, but no longer for the reason it was written for. The bound is
    // stored now and checkConstraint does run on it; it returns true because the
    // argument `int` is a PrimitiveType and checkConstraint only reports on a
    // StructType argument. So the fix is a ruling followed by three lines, not
    // plumbing: does a primitive satisfy an interface bound? `Castable` and `Any`
    // are bounds in the standard library that every primitive must satisfy
    // (ADR 0018), so the answer is not simply "no".
    auto r = compile("interface I { fun m() <int>; }\n"
                     "struct S<T: I> { x <T>, }\n"
                     "fun main() <void> { let s <S<int>>; }\n");
    EXPECT_EQ(r.exitCode, 0)
        << "FIXED: a generic argument is checked against the parameter's bound.\n"
        << stripAnsi(r.err);
}

// ---------------------------------------------------------------------------
// A type that failed to resolve was carried on as if it had resolved.
//
// `resolveTypeFromAST` (Analyzer_Core.cpp) reports `Undefined type 'X'` and
// returns nullptr when a name does not resolve -- correct, and every caller
// checks it. But each *composite* branch wraps whatever the recursive call
// returned without looking at it: `PointerType(inner)` at :58,
// `ArrayType(inner, fixed)` at :79, `FunctionType(pTypes, rType)` at :89,
// `PrototypeType(keyType, valueType)` at :107 and the generic `args` at :123.
// A failed child therefore comes back inside a *non-null* composite, every
// caller's `if (!type) return;` passes, and the first `toString()` on the tree
// dereferences the null child and segfaults.
//
// Nine of the fifty samples died this way. The tests are split one per branch
// because the crash surfaces in the type layer -- PointerType.cpp:6,
// FunctionType.cpp:9, PrototypeType.hpp:17 -- where a null check in any one
// `toString()` would turn that spelling green and leave the rest crashing,
// while fixing nothing: a composite over a type that does not exist is not a
// type, and rendering it as `&<unknown>` would only move the crash to the next
// caller that asks it a real question.
//
// Each is asserted against the compiler's own behaviour on the simplest
// spelling of the same mistake -- the undefined type named directly, with no
// composite around it -- rather than against a written-down exit code or
// message. The claim under test is that wrapping a mistake does not change what
// the compiler decides about it.
// ---------------------------------------------------------------------------

namespace {

// The undefined type named on its own: the one spelling that already works.
FincRun bareUndefinedType() {
    return compile("fun f() <int> { let x <NoSuchType>; return 0; }");
}

size_t occurrencesOf(const std::string& haystack, const std::string& needle) {
    size_t n = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size())) {
        ++n;
    }
    return n;
}

// Asserts that `code` -- which names `NoSuchType` inside some composite -- ends
// the same way as naming `NoSuchType` by itself.
void expectResolvesLikeTheBareSpelling(const std::string& code, const std::string& composite) {
    const FincRun bare = bareUndefinedType();
    ASSERT_EQ(bare.exitCode, 1)
        << "an undefined type named directly must be a plain rejection, or this test has no "
           "baseline to compare against\n" << bare.err;
    ASSERT_NE(bare.err.find("Undefined type 'NoSuchType'"), std::string::npos)
        << "the baseline no longer reports the cause it is supposed to\n" << bare.err;

    const FincRun r = compile(code);
    EXPECT_EQ(r.exitCode, bare.exitCode)
        << "`" << composite << "` over an undefined type did not end the way the undefined "
           "type does on its own"
        << (r.exitCode >= 128
                ? std::string(": the compiler was killed by signal ")
                      + std::to_string(r.exitCode - 128)
                      + ", because the failed resolution was wrapped in a composite type "
                        "instead of being propagated, so the null child was dereferenced "
                        "by that composite's toString()."
                : std::string("."))
        << "\nstderr:\n" << r.err;
    EXPECT_NE(r.err.find("Undefined type 'NoSuchType'"), std::string::npos)
        << "the cause must still be reported when it is wrapped in `" << composite
        << "`, not swallowed by whatever propagates it\nstderr:\n" << r.err;
}

} // namespace

TEST(Soundness_TypeResolution, APointerToAnUndefinedTypeIsRejectedNotFatal) {
    expectResolvesLikeTheBareSpelling(
        "fun f() <int> { let p <&NoSuchType>; return 0; }", "&T");
}

TEST(Soundness_TypeResolution, AnArrayOfAnUndefinedTypeIsRejectedNotFatal) {
    expectResolvesLikeTheBareSpelling(
        "fun f() <int> { let a <[NoSuchType]>; return 0; }", "[T]");
}

TEST(Soundness_TypeResolution, AFunctionTypeTakingAnUndefinedTypeIsRejectedNotFatal) {
    expectResolvesLikeTheBareSpelling(
        "fun f() <int> { let g <fn(NoSuchType) -> int>; return 0; }", "fn(T) -> int");
}

// Separate from the parameter case: the parameters and the return type are
// resolved by two different lines (Analyzer_Core.cpp:86 and :88) and reach
// FunctionType through two different arguments, so a fix that checks one and
// not the other leaves this green.
TEST(Soundness_TypeResolution, AFunctionTypeReturningAnUndefinedTypeIsRejectedNotFatal) {
    expectResolvesLikeTheBareSpelling(
        "fun f() <int> { let g <fn(int) -> NoSuchType>; return 0; }", "fn(int) -> T");
}

TEST(Soundness_TypeResolution, APrototypeOverAnUndefinedTypeIsRejectedNotFatal) {
    expectResolvesLikeTheBareSpelling(
        "fun f() <int> { let p <{NoSuchType, int}>; return 0; }", "<{T, int}>");
}

TEST(Soundness_TypeResolution, AGenericArgumentThatIsUndefinedIsRejectedNotFatal) {
    expectResolvesLikeTheBareSpelling(
        "struct S<T> { x <T> }\n"
        "fun f() <int> { let s <S<NoSuchType>>; return 0; }", "S<T>");
}

// The same failure through a different door, and the one that killed six of the
// nine samples. Analyzer_Decl.cpp:74 resolves the declared return type, :78
// stores it in `context.currentFuncReturnType` whether or not it resolved, and
// :95 dereferences it to ask whether it is void. No composite type is involved,
// so every fix to the branches above leaves this one crashing.
TEST(Soundness_TypeResolution, AFunctionReturningAnUndefinedTypeIsRejectedNotFatal) {
    expectResolvesLikeTheBareSpelling(
        "fun f() <NoSuchType> { let x <int> = 1; }", "fun f() <T>");
}

// Propagating a failure must not report it twice. The child that could not
// resolve has already said so; a composite that adds its own account of the
// same mistake makes one error read as two, which is the defect
// Soundness_ModuleIdentity guards against on the module side.
TEST(Soundness_TypeResolution, WrappingAnUndefinedTypeDoesNotReportItTwice) {
    const FincRun bare = bareUndefinedType();
    const size_t expected = occurrencesOf(bare.err, "Undefined type 'NoSuchType'");
    ASSERT_EQ(expected, 1u)
        << "the bare spelling must report the cause exactly once, or this test has no "
           "baseline\n" << bare.err;

    const std::vector<std::pair<std::string, std::string>> spellings = {
        {"&T",           "fun f() <int> { let p <&NoSuchType>; return 0; }"},
        {"[T]",          "fun f() <int> { let a <[NoSuchType]>; return 0; }"},
        {"fn(T) -> int", "fun f() <int> { let g <fn(NoSuchType) -> int>; return 0; }"},
        {"fn(int) -> T", "fun f() <int> { let g <fn(int) -> NoSuchType>; return 0; }"},
        {"<{T, int}>",   "fun f() <int> { let p <{NoSuchType, int}>; return 0; }"},
        {"fun f() <T>",  "fun f() <NoSuchType> { let x <int> = 1; }"},
    };
    for (const auto& s : spellings) {
        const FincRun r = compile(s.second);
        EXPECT_EQ(occurrencesOf(r.err, "Undefined type 'NoSuchType'"), expected)
            << "`" << s.first << "` reported the one undefined type a different number of "
               "times than naming it directly does\nstderr:\n" << r.err;
    }
}

// A lambda's return type is resolved and wrapped by a second construction site
// (Analyzer_Expr.cpp:538) that repeats the mistake independently of
// resolveTypeFromAST's branches, so fixing those leaves this crashing. This is
// the site that killed lambdas.fin, whose `//@ unimplemented` reason recorded
// the segfault in prose while the harness enforced nothing about it.
TEST(Soundness_TypeResolution, ALambdaReturningAnUndefinedTypeIsRejectedNotFatal) {
    expectResolvesLikeTheBareSpelling(
        "fun main() <int> { let g <auto> = (m: int) <NoSuchType> => m; return 0; }",
        "(m: int) <T> =>");
}

// ---------------------------------------------------------------------------
// A generic lambda's type parameters are in scope in its own signature.
//
// Was a defect: visit(LambdaExpression&) resolved the return type and the
// parameter types *before* enterScope(), and nothing registered the lambda's
// `<T>` at all, so T was undefined in the one place it is introduced and the
// parameter it typed was undefined in the body that used it. Fixed by entering
// the scope first and registering the generic params in it, as
// visit(FunctionDeclaration&) does at Analyzer_Decl.cpp:43.
//
// Both spellings are exercised because lambdas.fin declares both -- the arrow
// form on line 69 and the `fun` form on line 71 -- and they reach the same
// visitor by different parser paths.
// ---------------------------------------------------------------------------
TEST(Soundness_GenericLambdas, ATypeParameterIsInScopeInItsOwnSignature) {
    const FincRun r = compile(
        "fun main() <int> { let c <auto> = <T>(m: T) <T> => m; return 0; }");
    EXPECT_EQ(r.exitCode, 0)
        << "a generic lambda must compile: `<T>` introduces T for the rest of the "
           "signature and the body\nstderr:\n" << r.err;
    EXPECT_EQ(r.err.find("Undefined type 'T'"), std::string::npos)
        << "T was reported undefined in the signature that declares it\nstderr:\n" << r.err;
    EXPECT_EQ(r.err.find("Undefined variable 'm'"), std::string::npos)
        << "the parameter typed by T was not visible in the body\nstderr:\n" << r.err;
}

TEST(Soundness_GenericLambdas, TheFunSpellingAlsoHasItsTypeParametersInScope) {
    const FincRun r = compile(
        "fun main() <int> { let c <auto> = fun <G>(a: G, b: G) <G> { return a; }; return 0; }");
    EXPECT_EQ(r.exitCode, 0)
        << "the `fun <G>(...)` lambda spelling must compile too (lambdas.fin:71)\nstderr:\n"
        << r.err;
}

// ---------------------------------------------------------------------------
// A generic lambda is not instantiated at its call site.
//
// With its type parameters now in scope, `<T>(m: T) <T> => m` has type
// `fn(T) -> T`, and calling it with an int compares the argument against the
// uninstantiated `T` instead of binding T to int. Named types get this right
// through StructType::instantiate; a lambda's FunctionType has no equivalent
// path, so the call is rejected.
//
// Booked rather than fixed because inferring a lambda's type arguments from its
// call is a distinct piece of work from getting its declaration in scope, and
// the declaration is what lambdas.fin:69 writes.
// ---------------------------------------------------------------------------
TEST(KnownDefect_GenericLambdas, CallingOneDoesNotBindItsTypeParameters) {
    const FincRun r = compile(
        "fun main() <int> { let c <auto> = <T>(m: T) <T> => m; return c(1); }");
    EXPECT_NE(r.err.find("Type mismatch"), std::string::npos)
        << "FIXED? A generic lambda now accepts an argument. Correct behaviour is that "
           "`c(1)` binds T to int and yields int, the way a call to a generic function "
           "does. Invert this into Soundness_GenericLambdas.\n"
        << r.err;
    EXPECT_NE(r.exitCode, 0)
        << "FIXED? The call now compiles.\n" << r.err;
}

// ---------------------------------------------------------------------------
// A generic `fn` type's parameters are in scope inside that type.
//
// The same defect as Soundness_GenericLambdas, in the other place the language
// writes generic parameters: the *type annotation*
// `fn<T: Castable>(m: T) -> T`. resolveTypeFromAST's FunctionTypeNode branch
// (Analyzer_Core.cpp) resolved the parameter and return types without ever
// registering the node's own generic_params, so T was undefined in the type that
// declares it.
//
// Separate from the lambda test because it is a separate branch in a separate
// function: fixing the lambda leaves this reporting `Undefined type 'T'`, which
// is exactly what lambdas.fin:69 did once its lambda half was fixed -- the
// annotation and the value on that one line each declare `<T>` and each needed
// it in scope.
// ---------------------------------------------------------------------------
TEST(Soundness_GenericLambdas, AGenericFnTypeHasItsTypeParametersInScope) {
    const FincRun r = compile("fun main() <int> { let f <fn<T>(m: T) -> T>; return 0; }");
    EXPECT_EQ(r.exitCode, 0)
        << "a generic `fn` type must resolve: `fn<T>` introduces T for the parameter and "
           "return types that follow it\nstderr:\n" << r.err;
    EXPECT_EQ(r.err.find("Undefined type 'T'"), std::string::npos)
        << "T was reported undefined in the type that declares it\nstderr:\n" << r.err;
}

// The whole of lambdas.fin:69: the annotation and the lambda each declare `<T>`.
TEST(Soundness_GenericLambdas, AGenericFnTypeAcceptsAGenericLambda) {
    const FincRun r = compile(
        "fun main() <int> { let c <fn<T>(m: T) -> T> = <T>(m: T) <T> => m; return 0; }");
    EXPECT_EQ(r.exitCode, 0)
        << "the spelling lambdas.fin:69 writes must compile\nstderr:\n" << r.err;
}

// ---------------------------------------------------------------------------
// A call to a function whose signature did not resolve is quiet.
//
// This was KnownDefect_TypeResolution.ACallToAFunctionWithAnUnresolvedSignatureCascades,
// and its note said what to build: "a sentinel error type -- one that resolution
// returns instead of nullptr, that is assignable to and from everything, and that
// suppresses any further complaint about the expression it reaches. That would let
// the function be registered with its correct arity and let this call go quiet."
// src/types/ErrorType.hpp is that type. Inverted rather than relaxed, per its own
// instruction.
//
// The two objections that kept step 6 gated are both answered by the sentinel
// rather than argued away: a FunctionType built over it dereferences safely in
// toString() because it is not null, and the arity stays the written one because
// the unresolved parameter is carried into paramTypes instead of dropped.
//
// Still owed, and named here because this test's predecessor named it: the
// "null means unknown, stop asking" convention that Analyzer_Stmt.cpp:15,21 and
// Analyzer_Decl.cpp step 7 implement by hand. `context.currentFuncReturnType` is
// deliberately still null and not the sentinel -- its reader is the missing-return
// check, which would otherwise demand a return the program cannot write.
// ---------------------------------------------------------------------------
TEST(Soundness_TypeResolution, ACallToAFunctionWithAnUnresolvedSignatureIsQuiet) {
    // Guard: with the type defined, the same two files compile clean, so anything
    // reported below is caused by the unresolved type and not by the shape of the
    // program.
    const FincRun control = compile(
        "fun f(p: int) <int> { return 0; }\n"
        "fun main() <int> { return f(1); }");
    ASSERT_EQ(control.exitCode, 0)
        << "declaring and calling a one-parameter function must compile, or this test has "
           "no baseline\nstderr:\n" << control.err;

    for (const auto& code : {
            std::string("fun f(p: NoSuchType) <int> { return 0; }\n"
                        "fun main() <int> { return f(1); }"),
            std::string("fun f() <NoSuchType> { return 0; }\n"
                        "fun main() <int> { let y <auto> = f(); return 0; }")}) {
        const FincRun r = compile(code);
        const std::string err = stripAnsi(r.err);
        EXPECT_EQ(err.find("Undefined function or type 'f'"), std::string::npos)
            << "f is defined; only its type is unknown\n" << code << err;
        EXPECT_EQ(errorCount(err), 1u)
            << "the one undefined type is the only diagnostic this program earns:\n"
            << code << err;
    }

    // And quiet is not the same as blind. The signature keeps the arity the program
    // wrote, so a call that really is wrong is still caught -- this is what
    // distinguishes carrying the sentinel from the old behaviour of dropping the
    // parameter, which reported "expects 0 arguments" for `f(1)`.
    const FincRun wrong = compile(
        "fun f(p: NoSuchType) <int> { return 0; }\n"
        "fun main() <int> { return f(1, 2); }");
    EXPECT_NE(stripAnsi(wrong.err).find("expects 1 arguments, got 2"), std::string::npos)
        << "the unresolved parameter still counts toward the arity\nstderr:\n" << wrong.err;
}

// The half of that defect which is fixed: whatever the call reports, it must not
// report an arity nobody wrote. An unresolved parameter used to be dropped from
// paramTypes and the function registered anyway, so `fun f(p: NoSuchType)` called
// as `f(1)` was told it "expects 0 arguments, got 1" -- a false statement about a
// signature the program does not contain, and the kind of diagnostic that sends
// someone to edit the call site instead of the type name.
TEST(Soundness_TypeResolution, AnUnresolvedParameterDoesNotChangeTheReportedArity) {
    const FincRun r = compile(
        "fun f(p: NoSuchType) <int> { return 0; }\n"
        "fun main() <int> { return f(1); }");
    EXPECT_EQ(r.err.find("expects 0 arguments"), std::string::npos)
        << "the unresolved parameter was dropped from the signature, so the call was judged "
           "against an arity the program never declared\nstderr:\n" << r.err;
    // Guard: the undefined type itself is still reported, so this is not passing
    // merely because the compiler went quiet.
    EXPECT_NE(r.err.find("Undefined type 'NoSuchType'"), std::string::npos)
        << "the cause must still be reported\nstderr:\n" << r.err;
}

// ---------------------------------------------------------------------------
// An integer constant had exactly one type, and it was `int`.
//
// `let x <long> = 1;` did not compile. Neither did `<short>`, `<char>`,
// `<uint>`, `<ulong>`, `<ushort>` or `<double>` -- every integer type Fin has
// except `int` itself rejected a decimal literal, in every position: an
// initializer, a struct field default, a `const`, a function argument, a
// `return`, an assignment, a `for` counter, a comparison, a ternary branch.
// `visit(Literal&)` gave INTEGER the type `int` and `PrimitiveType::isAssignableTo`
// admitted one conversion, `int` to `float`, so `pointer <ulong> = 0` in
// stdlib/stdio.fin was a type error and so was `blame myarr[0] == 0` in
// arrays.fin.
//
// The rule now: an integer *constant* takes the integer type its context asks
// for. Fin's own spelling of the distinction is what makes this checkable
// without inference -- a literal is a `Literal` node and `-1` is a `UnaryOp`
// over one, so "is this expression an integer constant, and is it negative"
// is a question about the syntax rather than about a value flowing through the
// analyzer.
//
// What is deliberately NOT admitted, each with its own test below: an `int`-typed
// *expression* assigned to an unsigned type (a language decision, not a defect --
// see KnownDefect_IntegerConstants), and a constant too large for its target (the
// widths do not exist yet; KnownDefect_IntegerWidths owns that line).
// ---------------------------------------------------------------------------

namespace {

// Every integer type Analyzer_Core.cpp registers, in the order it registers them.
const std::vector<std::string>& integerTypeNames() {
    static const std::vector<std::string> names{
        "int", "long", "short", "char", "uint", "ulong", "ushort"};
    return names;
}

} // namespace

TEST(Soundness_IntegerConstants, ANonNegativeConstantTakesAnyIntegerType) {
    for (const std::string& t : integerTypeNames()) {
        const FincRun r = compile("fun main() <noret> { let x <" + t + "> = 1; }\n");
        EXPECT_EQ(r.exitCode, 0)
            << "`let x <" << t << "> = 1;` must compile: 1 is representable in every "
               "integer type Fin has, and a language in which it is not cannot spell "
               "an unsigned zero.\nstderr:\n" << r.err;
    }
}

TEST(Soundness_IntegerConstants, ZeroIsAValidUnsignedInitialiser) {
    // Separate from the loop above because this is the case the corpus actually
    // contains -- `pointer <ulong> = 0` (stdlib/stdio.fin:97) -- and because a fix
    // keyed on the literal being nonzero would leave the loop green and this red.
    const FincRun r = compile("fun main() <noret> { let p <ulong> = 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(Soundness_IntegerConstants, TheConstantIsAcceptedInEveryPositionThatChecksAType) {
    // One test per call site of checkType() that the corpus reaches with a literal.
    // Split from the loop above because checkType() is called with the literal node
    // in some positions and with a parent node in others: a fix that only handles
    // `let` leaves all of these red, and they are where the standard library lives.
    struct Case { const char* what; std::string code; };
    const std::vector<Case> cases{
        {"struct field default",
         "struct S { pub a <uint> = 1, }\nfun main() <noret> { }\n"},
        {"const declaration",
         "const N <ulong> = 100;\nfun main() <noret> { }\n"},
        {"function argument",
         "fun f(x: uint) <noret> { }\nfun main() <noret> { f(1); }\n"},
        {"return value",
         "fun f() <uint> { return 1; }\nfun main() <noret> { }\n"},
        {"assignment",
         "fun main() <noret> { let a <uint>; a = 1; }\n"},
        {"compound assignment",
         "fun main() <noret> { let a <uint>; a += 1; }\n"},
        {"for counter",
         "fun main() <noret> { for (i: uint = 0; i > 3; i++) { } }\n"},
        {"comparison, constant on the right",
         "fun main() <noret> { let a <uint> = 0; blame a == 0; }\n"},
        {"comparison, constant on the left",
         "fun main() <noret> { let a <uint> = 0; blame 0 == a; }\n"},
        {"ternary branch",
         "fun main() <noret> { let a <uint> = 0; let b <uint> = true : 1 ? a; }\n"},
    };
    for (const Case& c : cases) {
        const FincRun r = compile(c.code);
        EXPECT_EQ(r.exitCode, 0)
            << "an integer constant is rejected in this position: " << c.what
            << "\n" << c.code << "stderr:\n" << r.err;
    }
}

TEST(Soundness_IntegerConstants, AConstantComparesTheSameOnEitherSide) {
    // `0 == a` and `a == 0` are the same question, and the compiler answered them
    // differently: the comparison branch of visit(BinaryOp&) took the left operand
    // as the expectation, so a constant on the left made the *variable* the error
    // ("expected 'int', got 'uint'"). Its own test because the constant rule above
    // fixes one order and not the other -- checkType() receives the right operand.
    //
    // The rule added is only about constants, not about comparisons in general: a
    // comparison between two differently-typed *variables* is judged exactly as it
    // was, because whether `int` and `float` may be compared is a separate question
    // and nothing here answers it.
    const FincRun onLeft = compile("fun main() <noret> { let a <uint> = 0; blame 0 == a; }\n");
    const FincRun onRight = compile("fun main() <noret> { let a <uint> = 0; blame a == 0; }\n");
    EXPECT_EQ(onLeft.exitCode, onRight.exitCode)
        << "`0 == a` and `a == 0` must be judged alike.\n"
        << "constant on the left:\n" << onLeft.err << "constant on the right:\n" << onRight.err;
    EXPECT_EQ(onLeft.exitCode, 0) << onLeft.err;
}

TEST(Soundness_IntegerConstants, AConstantIsAlsoAFloatOrADouble) {
    // `int` to `float` was the one conversion PrimitiveType admitted; `double` was
    // not, so `let x <double> = 1;` failed while `let x <float> = 1;` passed. Same
    // rule, and it is asserted here so that narrowing the constant rule to integers
    // alone shows up as a failure rather than as a silent regression.
    for (const std::string& t : {"float", "double"}) {
        const FincRun r = compile("fun main() <noret> { let x <" + t + "> = 1; }\n");
        EXPECT_EQ(r.exitCode, 0) << "`let x <" << t << "> = 1;`\n" << r.err;
    }
}

TEST(Soundness_IntegerConstants, ANegativeConstantIsNotUnsigned) {
    // The half of the rule that must reject, and the reason the fix reads the AST
    // rather than widening PrimitiveType::isAssignableTo: `-1` is a UnaryOp over a
    // Literal, so "negative constant" is a syntactic question with an exact answer.
    // A fix that admits `int` to `uint` wholesale passes every test above and this
    // one is the only thing that catches it.
    //
    // **The corpus disagrees, and this is the one place in this file where that is
    // true.** stdlib/stdio.fin:107 writes `fun read(nbytes: ulong = -1)` and :110
    // tests `nbytes == -1`, which is the C idiom for "the maximum" -- so a normative
    // sample says a negative constant on an unsigned type is legal and wraps. No ADR
    // rules on it, so this test keeps the rejection that was already there before the
    // constant rule existed: the rule admitted new spellings and must not be what
    // smuggles that one in. **Ruling needed** -- if the answer is C wraparound, invert
    // this test and delete the `!negative` in constantFitsType; if it is that unsigned
    // maxima are spelled explicitly, stdio.fin needs a ratified edit. Either way the
    // sample is currently rejected either way, so nothing depends on the answer today.
    for (const std::string& t : {"uint", "ulong", "ushort"}) {
        const FincRun r = compile("fun main() <noret> { let x <" + t + "> = -1; }\n");
        EXPECT_NE(r.exitCode, 0)
            << "`let x <" << t << "> = -1;` must be rejected -- -1 is not representable "
               "in an unsigned type, and accepting it is worse than rejecting `= 1` was.";
    }
    // And it stays legal where it is representable.
    for (const std::string& t : {"int", "long", "short", "float", "double"}) {
        const FincRun r = compile("fun main() <noret> { let x <" + t + "> = -1; }\n");
        EXPECT_EQ(r.exitCode, 0) << "`let x <" << t << "> = -1;`\n" << r.err;
    }
}

TEST(Soundness_IntegerConstants, AConstantIsStillNotABoolOrAString) {
    // The rule is about integer and floating targets and nothing else. Without this,
    // "the context decides" is indistinguishable from "the check was deleted".
    for (const std::string& t : {"bool", "string"}) {
        const FincRun r = compile("fun main() <noret> { let x <" + t + "> = 1; }\n");
        EXPECT_NE(r.exitCode, 0)
            << "`let x <" << t << "> = 1;` must still be a type error.";
        EXPECT_NE(stripAnsi(r.err).find("Type mismatch"), std::string::npos) << r.err;
    }
}

// ---------------------------------------------------------------------------
// What the constant rule does not reach, booked rather than built.
// ---------------------------------------------------------------------------

TEST(KnownDefect_IntegerConstants, AnIntTypedExpressionIsNotUnsigned) {
    // Not a defect in the same sense as the above: whether `let u <uint> = i;` for an
    // `int` i is legal is a language decision, and the corpus needs an answer because
    // stdlib/stdio.fin does mixed arithmetic on `int` counters and `ulong` fields at
    // :110, :112, :115, :124 and :126 -- seven sites in one file, reported three times
    // over because three samples import it.
    //
    // C converts silently, Rust and Zig refuse without a cast. Fin has `cast<T>` and a
    // `Castable` interface, which is an argument for refusing. This test asserts the
    // refusal that exists today so that whichever way it is ruled, the change is
    // deliberate; if the ruling is "convert", invert this and widen
    // PrimitiveType::isAssignableTo, which is a one-line change and would then also
    // subsume the constant rule.
    const FincRun r = compile("fun main() <noret> { let i <int> = 1; let u <uint> = i; }\n");
    EXPECT_NE(r.exitCode, 0)
        << "FIXED or RULED: an int-typed expression now converts to unsigned. If that "
           "was the ruling, record it and invert this test.";
    EXPECT_NE(stripAnsi(r.err).find("expected 'uint', got 'int'"), std::string::npos) << r.err;
}

TEST(KnownDefect_IntegerWidths, AConstantTooLargeForItsTargetIsAccepted) {
    // Introduced by the constant rule and recorded here in the same commit, which is
    // the whole point of this suite: the rule checks the *sign* of a constant and not
    // its magnitude, because Fin has not said how wide `short` or `char` is. The
    // `{N}` annotation that would say is erased before anything can read it (see
    // TheWidthIsAbsentFromDiagnosticText above), so a magnitude check today would be
    // inventing the widths rather than enforcing them.
    //
    // Rejecting every constant was the alternative and it is strictly worse: it makes
    // `let p <ulong> = 0;` unwritable. When the widths become real this is where the
    // range check goes, and this test inverts into
    // Soundness_IntegerConstants.AConstantMustFitItsTarget.
    for (const char* code : {"fun main() <noret> { let x <short> = 99999; }\n",
                             "fun main() <noret> { let x <char> = 300; }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(r.exitCode, 0)
            << "FIXED: the constant's magnitude is now checked against its target. "
               "Invert this and name the widths in the ADR that decided them.\n"
            << code << r.err;
    }
}

TEST(Soundness_Arrays, AFixedListInitialisesADynamicArrayOfTheSameElementType) {
    // Split out of the KnownDefect below, where it was asserted only as a sentence in
    // a comment. It is what makes that defect single-cause: if this ever regressed,
    // the KnownDefect would keep passing for a reason its own text denies.
    for (const char* code : {"fun main() <noret> { let a <[int]> = [1, 2]; }\n",
                             "fun main() <noret> { let x <int> = 1; let a <[int]> = [x, x]; }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(r.exitCode, 0) << "a fixed list must initialise a dynamic array of "
                                    "the same element type:\n" << code << r.err;
    }
}

// ---------------------------------------------------------------------------
// `.length` on an array and on a string.
//
// The corpus writes it five times across four samples and declares it nowhere:
// arrays.fin:12,17,18 (`array.length`, where `array: &[T]`), loops.fin:14
// (`a.length`, where `a: [int, 5]`), const.fin:67 (`a.value.length`, reached through
// an rptr), and stdlib/stdio.fin:160 (`path.length`, where `path: string`). Every one
// of them reported `Type '<the array>' is not a struct` -- which is what
// visit(MemberAccess&) says when getStructType() returns nothing, because before this
// the analyser resolved no member on any non-struct type whatsoever.
//
// Typed `int`, and that is forced rather than chosen. Fin converts between no two
// integer types at all (KnownDefect_IntegerWidths.AnIntIsNotAssignableToAnUnsigned),
// so whatever width `.length` returns is the *only* width it can be compared with --
// and all five corpus sites compare it against an `int`: `array.length <= 1`,
// `i < a.length - 1` with `i: int` declared in the same header, `path.length == 10`.
// A `ulong` length would convict four of the five sites the day it landed, so `int` is
// what the samples say. ALengthIsAnIntAndNotAnotherIntegerWidth is what pins it, so a
// later widening ruling has to come past a red test rather than through a silent edit.
//
// Deliberately narrow. Arrays and strings get one member and no methods, because one
// member and no methods is what the corpus asks for; `letssee.fin:63` writes
// `a.length()` with parentheses but `a` there is a `Vec2` with a method of that name,
// which is a different thing that already worked. Nothing here gives a prototype,
// an enum or a nullable a `.length`, and the two `is not a struct` diagnostics that
// remain on prototypes (stdlib/prototypes.fin:11,15 -- `prtp.0`) are a separate unit
// with a separate spec: they return arrays of keys and of values, not a count.

TEST(Soundness_BuiltinMembers, ADynamicArrayHasALengthOfTypeInt) {
    const FincRun r = compile(
        "fun main() <noret> { let a <[int]> = [1, 2, 3]; let n <int> = a.length; }\n");
    EXPECT_EQ(r.exitCode, 0) << "a dynamic array's `.length` must be an int:\n" << r.err;
}

TEST(Soundness_BuiltinMembers, AFixedArrayHasALengthOfTypeInt) {
    // loops.fin:14 verbatim in miniature. A fixed array knows its count statically and
    // could in principle be folded to a literal; it is still typed `int`, because the
    // sample writes `i < a.length - 1` against an `int` loop variable and arithmetic on
    // a differently-typed constant would be no better off than arithmetic on a `ulong`.
    const FincRun r = compile(
        "fun main() <noret> { let a <[int, 5]> = [1,2,3,4,5]; let n <int> = a.length; }\n");
    EXPECT_EQ(r.exitCode, 0) << "a fixed array's `.length` must be an int:\n" << r.err;
}

TEST(Soundness_BuiltinMembers, ALengthIsReachedThroughAPointerToAnArray) {
    // arrays.fin's spelling: the array is a parameter passed by reference so that it is
    // not copied (the sample's own comment on :10 says so), which makes every one of
    // its three sites a `.length` on a `&[T]` and not on a `[T]`. Resolving the member
    // on the array but not through the pointer would have left all three red.
    for (const char* code : {
             "fun f(a: &[int]) <int> { return a.length; }\n"
             "fun main() <noret> { let v <[int]> = [1]; let n <int> = f(&v); }\n",
             // Two indirections. Nothing in the corpus writes one, but the unwrap is
             // recursive for structs (getStructType, Analyzer_Expr.cpp:13) and an
             // array that stopped after one hop would be an inconsistency to explain.
             "fun f(a: &&[int]) <int> { return a.length; }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(r.exitCode, 0) << "`.length` must be reachable through a pointer to "
                                    "an array:\n" << code << r.err;
    }
}

TEST(Soundness_BuiltinMembers, AnArraysElementTypeDoesNotAffectItsLength) {
    // arrays.fin:12 exactly: `fun sort<T: Number>(array: &[T])` then `array.length`.
    // The element type is a generic parameter with a bound, so anything that reached
    // for the element's members -- or that resolved the array through its element --
    // would fail here while passing on `[int]`.
    const FincRun r = compile(
        "fun sort<T>(array: &[T]) <int> { return array.length; }\n");
    EXPECT_EQ(r.exitCode, 0) << "an unresolved element type must not stop `.length`:\n" << r.err;
}

TEST(Soundness_BuiltinMembers, AStringHasALengthOfTypeInt) {
    // stdlib/stdio.fin:160, `path.length == 10`. A string is a PrimitiveType here and
    // not an array of char, so it needs saying separately; `[char]` is a different type
    // and gets its length from the array rule above.
    const FincRun r = compile(
        "fun main() <noret> { let s <string> = \"abc\"; let n <int> = s.length; }\n");
    EXPECT_EQ(r.exitCode, 0) << "a string's `.length` must be an int:\n" << r.err;
}

TEST(Soundness_BuiltinMembers, ALengthIsAnIntAndNotAnotherIntegerWidth) {
    // The test that makes the width a decision instead of an accident. With no
    // conversion between integer types, `let n <ulong> = a.length;` is rejected if and
    // only if `.length` is not itself a `ulong` -- so this failing means the width
    // moved, and the four corpus sites that compare a length against an `int` moved
    // with it. If a ruling widens it, that ruling owns this test and the samples.
    for (const char* code : {"fun main() <noret> { let a <[int]> = [1]; let n <ulong> = a.length; }\n",
                             "fun main() <noret> { let s <string> = \"a\"; let n <ulong> = s.length; }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(r.exitCode, 1) << "`.length` is an int, so a ulong target must be "
                                    "rejected while no integer conversion exists:\n"
                                 << code << r.err;
        EXPECT_NE(stripAnsi(r.err).find("expected 'ulong', got 'int'"), std::string::npos)
            << code << stripAnsi(r.err);
    }
}

TEST(Soundness_BuiltinMembers, AStructFieldNamedLengthIsNotTheBuiltin) {
    // lib/std/collection.fin has a `length` field and reads it eight times, so a
    // builtin that outranked a declared field would break the standard library. It
    // cannot, because the builtin is only consulted for types that have no fields at
    // all -- but "cannot by construction" is exactly the claim worth a test, since the
    // construction is one `if` away from changing.
    const FincRun r = compile(
        "struct S {\n"
        "  pub:\n"
        "    length <string>,\n"
        "}\n"
        "fun main() <noret> { let s <S>; let n <string> = s.length; }\n");
    EXPECT_EQ(r.exitCode, 0) << "a declared field named `length` keeps its own type:\n" << r.err;
}

TEST(Soundness_BuiltinMembers, AnUnknownMemberOnAnArrayIsStillDiagnosed) {
    // The failure mode this whole unit risks: resolving one member on arrays by
    // handing back a type, and then handing back a type for every other member too.
    // A typo must still be a diagnostic, and it now says which member was missing
    // rather than `is not a struct` -- the latter is true of an array and tells the
    // author nothing they did not already know.
    for (const char* code : {"fun main() <noret> { let a <[int]> = [1]; let n <int> = a.lenght; }\n",
                             "fun main() <noret> { let s <string> = \"a\"; let n <int> = s.size; }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(r.exitCode, 1) << "an unknown member must still be reported:\n" << code << r.err;
        EXPECT_NE(messagesOnly(stripAnsi(r.err)).find("has no member"), std::string::npos)
            << code << stripAnsi(r.err);
    }
}

TEST(Soundness_BuiltinMembers, ATypeWithNoMembersStillSaysSo) {
    // And the rest of the type system keeps the old message, because for an `int` the
    // old message is the right one: there is no member set to be missing from.
    const FincRun r = compile("fun main() <noret> { let x <int> = 5; let n <int> = x.length; }\n");
    EXPECT_EQ(r.exitCode, 1) << "an int has no members:\n" << r.err;
    EXPECT_NE(messagesOnly(stripAnsi(r.err)).find("is not a struct"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(KnownDefect_IntegerConstants, AnArrayOfConstantsDoesNotTakeTheAnnotatedElementType) {
    // `let myarr <[uint]> = [7,3,4];` (arrays.fin:29) reports
    // `expected '[uint]', got '[int; fixed]'`. The message names two differences but
    // only one of them is a defect: `let a <[int]> = [1,2];` compiles clean, so a
    // fixed list already converts to a dynamic array of the same element type
    // (ArrayType.cpp:22, and Soundness_Arrays.AFixedListInitialisesADynamicArray
    // holds that). The `fixed` in the text is only how the right-hand side prints.
    //
    // So the single cause is the element type: checkType() is handed the ArrayLiteral,
    // whose type is an ArrayType, and by then nothing remembers that its elements were
    // constants. The fix belongs with whoever gives ArrayType an element-wise
    // conversion -- a constant list has no element type of its own until its context
    // supplies one -- and because there is only one cause, one fix ends this test.
    const FincRun r = compile("fun main() <noret> { let a <[uint]> = [1,2]; }\n");
    EXPECT_NE(r.exitCode, 0)
        << "FIXED: an array of constants now takes its annotated element type. Invert "
           "this into Soundness_IntegerConstants; the element type is the whole claim, "
           "the `fixed` half of the old message was never a defect.";
    EXPECT_NE(stripAnsi(r.err).find("[int; fixed]"), std::string::npos) << r.err;
}

// ---------------------------------------------------------------------------
// A parameter default: walked now, still not type-checked.
//
// This block was written when neither happened, and the split it predicted held.
// The walk was the whole defect's first half and is fixed -- see
// Soundness_ParameterDefaults, which owns that half and its eight call sites. The
// diagnosis here was right about the cause and wrong about the remedy: visit(Parameter&)
// does walk the default, but nothing dispatches to it, so the fix was not to add a
// checkType inside a dead visitor but to reach the defaults from the eleven parameter
// loops that do run. It is still dead code; Analyzer_Core.cpp says so at its definition.
//
// What remains below is the type half, and it is blocked rather than unwritten. Adding
// the check convicts stdlib/stdio.fin:87 and :109 (`nbytes: ulong = -1`) the moment it
// lands, which is the integer ruling. Mutation-tested in advance: applying the naive
// version (checkType, not checkInitializer) kills ANullDefaultIsStillAccepted, because
// stdlib/error.fin:11 writes `err_code: int = null` and a plain checkType has no null
// exemption. So the eventual fix is checkInitializer, and that is known before it is
// written rather than after.
//
// Measured, and still worth knowing: the corpus test for stdio.fin would NOT catch a
// regression here -- its expectation is prose (`//@ unimplemented "..."`), so a new
// diagnostic in that file flips nothing. These tests are the only thing watching.
// ---------------------------------------------------------------------------

TEST(Soundness_ParameterDefaults, AParameterDefaultIsAnalysed) {
    // Inverted from KnownDefect_Declarations.AParameterDefaultIsNotAnalysedAtAll, which
    // asserted that an undefined name in a parameter default went unreported, and which
    // asked its successor to "check that the diagnostic points inside the default rather
    // than at the function". It does: column 16 in both programs below is where the
    // default expression starts, not where `fun` or the parameter does.
    //
    // Two spellings because they take different paths through the analyser -- a bare name
    // reports `Undefined variable`, a call reports `Undefined function or type` -- and a
    // fix that reached only one of them would leave the other silent.
    for (const char* code : {"fun f(x: int = nosuchthing) <noret> { }\nfun main() <noret> { }\n",
                             "fun f(x: int = nosuchfn()) <noret> { }\nfun main() <noret> { }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(r.exitCode, 1) << "a parameter default is analysed:\n" << code << r.err;
        const std::string err = stripAnsi(r.err);
        EXPECT_NE(messagesOnly(err).find("nosuch"), std::string::npos) << code << err;
        EXPECT_NE(err.find(":1:16"), std::string::npos)
            << "the caret belongs inside the default, at the expression:\n" << code << err;
    }

    // The same expression one line lower is caught, which is what ruled out "the
    // analyser cannot see this expression" as an explanation when this was a defect.
    const FincRun body = compile("fun f() <noret> { let x <int> = nosuchthing; }\nfun main() <noret> { }\n");
    EXPECT_NE(body.exitCode, 0) << "an undefined name in a body must still be caught: " << body.err;
}

TEST(KnownDefect_ParameterDefaults, AParameterDefaultIsNotCheckedAgainstItsType) {
    // The type half. The walk it waited on exists now (Soundness_ParameterDefaults),
    // and this still asserts the defect -- which is exactly the split predicted when
    // both halves were one test.
    const FincRun r = compile("fun f(x: uint = \"nope\") <noret> { }\nfun main() <noret> { }\n");
    EXPECT_EQ(r.exitCode, 0)
        << "FIXED: a parameter default is type-checked now. Invert this into "
           "Soundness_ParameterDefaults and check the negative-constant case too -- "
           "`fun f(x: uint = -1)` is the corpus's own spelling and the ruling on it "
           "decides whether that is a second diagnostic or none.";
}

TEST(KnownDefect_ParameterDefaults, AStructFieldDefaultIsCheckedButAParameterIsNot) {
    // The asymmetry is the evidence that this is a missing call and not a missing
    // capability: the same wrong default in a struct field is both walked and checked
    // (Analyzer_Decl.cpp:181 accepts it, :183 calls checkType). Its own test because a
    // fix that adds checking to parameters must not disturb the path that works.
    const FincRun field = compile("struct S { pub x <uint> = \"nope\", }\nfun main() <noret> { }\n");
    EXPECT_NE(field.exitCode, 0)
        << "a struct field default of the wrong type must still be caught: " << field.err;
    EXPECT_NE(stripAnsi(field.err).find("Type mismatch"), std::string::npos) << field.err;

    const FincRun param = compile("fun f(x: uint = \"nope\") <noret> { }\nfun main() <noret> { }\n");
    EXPECT_EQ(param.exitCode, 0)
        << "FIXED: parameters are checked like fields now. Invert this.";
}

// ---------------------------------------------------------------------------
// Every unresolved type reports where it was written.
//
// It did not, and the scale is why this is here: `Undefined type 'X'` is the
// largest single error class in the corpus -- 99 occurrences, more than twice the
// next one -- and every one of them rendered at 1:1. Not a rendering bug.
// SemanticAnalyzer reports at the TypeNode's own `loc` (Analyzer_Core.cpp:204),
// and `base_type` in parser.y built a TypeNode without ever calling `setLoc(@$)`:
// of its fifteen productions, five did -- the five wave 2 added -- and of the older
// ten, eight did not, `IDENTIFIER` included (the other two are pass-throughs that
// keep the inner type's location on purpose). parser.y calls setLoc 280 times
// elsewhere, so the omission was local to types and not a convention the file
// declines to follow. `pointer_type` and `array_type` already set theirs, which
// is why `[Nope]` pointed at line 1 through its *element*, not its brackets.
//
// The consequence was worse than a misplaced caret. Line 1 of every sample is its
// `//@` expectation comment, so an unlocated diagnostic was attributed to the very
// sentence describing it -- and in a project where the samples are the
// specification, that corrupts the evidence rather than merely presenting it
// badly. KnownDefect_DiagnosticAttribution.DiagnosticsAreAttributedToExpectationComments
// (test_cli.cpp) counts that corpus-wide; the tests below are the unit-level
// companions and name the production behind each case.
//
// Every case puts the offending type on line 3, so "line 1" is wrong by two lines
// and cannot be an accident of a one-line file. Written as three KnownDefect tests
// first, confirmed red for the stated reason, then inverted here when the nine
// productions gained their locations.
// ---------------------------------------------------------------------------

namespace {

// The `--> path:line:column` of a rendered diagnostic as "line:column", or "" if
// it carried none. Reads the rendered text rather than the JSON on purpose: the
// human renderer is what the corpus census reads, so it is what must agree.
std::string firstLocation(const std::string& stderrText) {
    const std::string err = stripAnsi(stderrText);
    const size_t arrow = err.find("--> ");
    if (arrow == std::string::npos) return "";
    const size_t eol = err.find('\n', arrow);
    const std::string line = err.substr(arrow + 4, eol - arrow - 4);
    const size_t colon = line.rfind(':', line.rfind(':') - 1);
    if (colon == std::string::npos) return "";
    return line.substr(colon + 1);
}

// Two comment lines, so the code under test starts at line 3.
const char* kPad = "// line 1\n// line 2\n";

std::string reportedLoc(const std::string& body) {
    const FincRun r = compile(std::string(kPad) + body);
    return firstLocation(r.err);
}

std::string reportedLine(const std::string& body) {
    const std::string loc = reportedLoc(body);
    return loc.substr(0, loc.find(':'));
}

// The caret row of the first diagnostic, trimmed -- "^^^^ here". What the reader
// actually looks at, and the only thing that shows the *span* is right rather
// than just the start column.
std::string caretRow(const std::string& body) {
    const FincRun r = compile(std::string(kPad) + body);
    const std::string err = stripAnsi(r.err);
    const size_t here = err.find("^");
    if (here == std::string::npos) return "";
    const size_t eol = err.find('\n', here);
    std::string row = err.substr(here, eol - here);
    while (!row.empty() && row.back() == ' ') row.pop_back();
    return row;
}

} // namespace

TEST(Soundness_DiagnosticLocation, ADiagnosticThatHasALocationReportsIt) {
    // The control, and it was not optional: while the three tests below asserted a
    // defect, this is what ruled out "the extractor is broken" as the reason they
    // passed. Both errors here are raised by the same analyzer on the same line as
    // the type errors below, so the node is the only difference.
    EXPECT_EQ(reportedLine("fun main() <noret> { let x <int> = nope; }\n"), "3")
        << "an undefined variable on line 3 must be reported on line 3";
    EXPECT_EQ(reportedLine("fun main() <noret> { let x <bool> = 1; }\n"), "3")
        << "a type mismatch on line 3 must be reported on line 3";
}

TEST(Soundness_DiagnosticLocation, AnUndefinedNamedTypeReportsWhereItWasWritten) {
    // Thirteen positions, and they share one production: every named type in the
    // language comes from `base_type: IDENTIFIER` (or its generic form), wherever it
    // appears. Looped rather than split for that reason -- and if a change ever makes
    // some pass and not others, the loop names which, which is why all thirteen stay.
    //
    // The expected line is carried per case rather than assumed. One case needs two
    // lines of setup and its type is genuinely on line 4; asserting 3 there would be
    // asserting the wrong answer, and the first run of this loop did exactly that.
    struct Case { const char* line; const char* body; };
    for (const Case& c : {
             Case{"3", "fun main() <noret> { let x <Nope> = 1; }\n"},          // let annotation
             Case{"3", "fun f(p: Nope) <noret> { }\nfun main() <noret> { }\n"},  // parameter
             Case{"3", "fun f() <Nope> { }\nfun main() <noret> { }\n"},       // return type
             Case{"3", "fun main() <noret> { let x <Nope<int>> = 1; }\n"},     // generic base
             Case{"4", "struct S<T> { pub x <T>, }\n"                          // generic argument
                       "fun main() <noret> { let s <S<Nope>> = 0; }\n"},
             Case{"3", "struct S { pub x <Nope>, }\nfun main() <noret> { }\n"},  // struct field
             Case{"3", "fun main() <noret> { let a <[Nope]> = 0; }\n"},        // array element
             Case{"3", "fun main() <noret> { let a <&Nope> = 0; }\n"},         // pointer target
             Case{"3", "fun main() <noret> { let a <{Nope, int}> = 0; }\n"},   // prototype element
             Case{"3", "const C <Nope> = 1;\nfun main() <noret> { }\n"},      // const
             Case{"3", "fun main() <noret> { let f <fn(Nope) -> int> = 0; }\n"},  // fn-type param
             Case{"3", "type A = Nope;\nfun main() <noret> { }\n"},           // alias target
             Case{"3", "fun main() <noret> { let x <int> = cast<Nope>(1); }\n"},  // cast target
         }) {
        EXPECT_EQ(reportedLine(c.body), c.line)
            << "an undefined type must be reported on the line it was written on, "
               "not on line 1 -- line 1 of a sample is its `//@` expectation:\n"
            << c.body;
    }
}

TEST(Soundness_DiagnosticLocation, TheCaretSpansTheTypeNameAndNothingElse) {
    // Split from the line test because they fail apart: a TypeNode could carry the
    // right line with a one-column caret, or a span covering the whole annotation,
    // and the line assertion above would not notice either. The span is what makes
    // the diagnostic usable, so it is asserted where it is determinate.
    EXPECT_EQ(reportedLoc("fun main() <noret> { let x <Nope> = 1; }\n"), "3:29");
    EXPECT_EQ(caretRow("fun main() <noret> { let x <Nope> = 1; }\n"), "^^^^ here")
        << "the caret must cover `Nope` exactly";
    EXPECT_EQ(caretRow("fun f(p: Nope) <noret> { }\nfun main() <noret> { }\n"), "^^^^ here");
    // Was `any` until `any` became a registered type and stopped producing a
    // diagnostic to place a caret under. The assertion is about the *width* tracking
    // the name, so any three-character undefined name serves; the point is that a
    // four-caret answer here would mean the span was hardcoded to `Nope`.
    EXPECT_EQ(caretRow("fun main() <noret> { let x <Zap> = 1; }\n"), "^^^ here")
        << "three carets for a three-character type";
}

// Soundness_DiagnosticLocation.TheAnyTypeReportsWhereItWasWritten was removed here,
// and the reason is worth recording because removing a Soundness test is not a thing
// this suite does lightly.
//
// It asserted that `Undefined type 'any'` carried a line rather than 1:1, and its
// comment gave the stake: "40 of the corpus's 99 `Undefined type` diagnostics name
// it". Registering `any` and `object` retired that diagnostic, and with it the only
// diagnostic that could point at a KW_ANY type node -- `any` now resolves in every
// position, `S implements <any>` is accepted, and `new any{}` fails in the parser
// with a token location rather than a node one. There is nothing left to observe, so
// the test could only have been kept by giving it a different vehicle, which would
// have made it a second copy of AnUndefinedNamedTypeReportsWhereItWasWritten above.
//
// The location machinery it guarded is still guarded: the loop above covers the
// IDENTIFIER production, and Soundness_DynamicTypes covers what `any` does now.

TEST(Soundness_DiagnosticLocation, ABareSelfOutsideAStructReportsWhereItWasWritten) {
    // KW_SELF_TYPE, a third production. The diagnostic itself is arguably right --
    // `Self` really is undefined outside a struct -- which is why this test is
    // about the location alone and says nothing about whether the error should
    // exist. If `Self` ever becomes legal here, delete the test; do not weaken it.
    EXPECT_EQ(reportedLine("fun main() <noret> { let x <Self> = 1; }\n"), "3");
}

TEST(Soundness_DiagnosticLocation, ANewExpressionsTypeReportsWhereItWasWritten) {
    // A fourth production, found by re-running the corpus census after the nine
    // above were fixed rather than by reading the grammar: `new Nope{}` builds its
    // TypeNode inside the `KW_NEW IDENTIFIER` action (parser.y:2301) and set a
    // location on the NewExpression but not on the type inside it -- and it is the
    // type that fails to resolve. Its own test because it is its own production:
    // fixing `base_type` did nothing for it, which is how it survived to be found.
    EXPECT_EQ(reportedLine("fun main() <noret> { let x <int> = new Nope{}; }\n"), "3");
    EXPECT_EQ(reportedLine("fun main() <noret> { let x <int> = new Nope::<int>{}; }\n"), "3")
        << "the turbofish form is a second production and must carry one too";
}

TEST(Soundness_DiagnosticLocation, AStaticCallsTypeReportsWhereItWasWritten) {
    // A fifth production family, and found the same way the fourth was: by the corpus
    // census going red, not by reading the grammar. `Nope::zero()` builds its TypeNode
    // inside the `static_method_call` actions and set a location on the call but not on
    // the type inside it -- and the type is what fails to resolve, so the diagnostic
    // came out at 1:1, which in a sample is a `//@` expectation comment.
    //
    // It stayed hidden until the error sentinel landed. `let x <HashMap<...>> = ...`
    // never analysed its initialiser while an unresolved annotation was fatal to the
    // declaration, so prototype_test.fin:27 reported the annotation and stopped. With
    // the initialiser analysed the call is reached, and the missing location with it.
    //
    // Five shapes because they are five separate actions, each building its own
    // TypeNode: three spellings of `Type::method(...)`, `Self::method()`, and
    // `new Self{}`. A fix to one is not a fix to the others -- that is exactly how
    // ANewExpressionsTypeReportsWhereItWasWritten's production survived the previous
    // sweep -- so each is asserted, and the loop names which one broke.
    for (const char* body : {
             "fun main() <noret> { let x <int> = Nope::zero(); }\n",
             "fun main() <noret> { let x <int> = Nope::<int>::zero(); }\n",
             "fun main() <noret> { let x <int> = Nope::zero::<int>(); }\n",
             "fun main() <noret> { let x <int> = Self::zero(); }\n",
             "fun main() <noret> { let x <int> = new Self{}; }\n"}) {
        EXPECT_EQ(reportedLine(body), "3") << body;
    }

    // The column, not just the line: the type name starts at column 36 in the first
    // shape. Asserted on one shape rather than all five because the line assertions
    // above already say the location is the type's own, and this says the fix put it
    // on the type name rather than on the whole call expression -- `@1`, not `@$`.
    EXPECT_EQ(reportedLoc("fun main() <noret> { let x <int> = Nope::zero(); }\n"), "3:36")
        << "the caret belongs on the type name, so the location is @1 and not @$";
}

TEST(Soundness_DiagnosticLocation, AMissingReturnReportsWhereTheFunctionIsDeclared) {
    // Not a type diagnostic at all -- `error(node, ...)` at Analyzer_Decl.cpp:121
    // reports against the FunctionDeclaration -- and it is here because the corpus
    // census does not care what kind of node lost its location, only that three of
    // the five diagnostics still landing on `//@` lines came from this one message.
    EXPECT_EQ(reportedLine("fun f(a: int) <int> { if (a > 0) { return 1; } }\n"
                           "fun main() <noret> { }\n"), "3");

    // The control, and the reason this is one test and not two: a method already
    // reported correctly (parser.y:894 sets a location, :465 did not), so the two
    // productions differ by exactly the missing call and nothing else. If this half
    // ever breaks, the fix went to the wrong place.
    EXPECT_EQ(reportedLine("struct S {\n"
                           "  pub fun m(self: &Self, a: int) <int> { if (a > 0) { return 1; } }\n"
                           "}\n"
                           "fun main() <noret> { }\n"), "4")
        << "a method's missing-return diagnostic must keep pointing at the method";
}

// A struct, interface, enum and class declaration each had no location at all.
//
// Only the *forward* declaration production called setLoc (parser.y:496); the four
// full-body ones -- `KW_STRUCT IDENTIFIER generic_params_opt inheritance_opt LBRACE
// struct_body_content RBRACE` and its interface, enum and class siblings -- built the
// node and returned it without one. So every diagnostic raised against a declaration
// landed on 1:1, and 1:1 in the corpus is a `//@` expectation comment: `stdlib/
// hashmap.fin`'s conformance failure pointed the reader at the line that says what the
// file is *expected* to do, which is the most misleading place in the file for it to go.
// Soundness_DiagnosticAttribution.NoDiagnosticPointsAtAnExpectationComment over the
// whole corpus is what caught it.
//
// All four productions get the call, and two of them can be tested. The other two have
// no diagnostic that reaches the declaration node at all yet:
//
//   * An interface cannot inherit an interface -- `interface D : <Base>` is a syntax
//     error, because KW_INTERFACE's production has no `inheritance_opt` where the struct
//     and class ones do -- so conformance never runs on one. No sample writes that form,
//     which is why it stays a note here rather than a KnownDefect of its own.
//   * An enum member's payload type is never resolved, so `enum E { pub Ok <Nope>, }`
//     compiles clean. That one is booked:
//     KnownDefect_Enums.AnEnumMemberPayloadTypeIsNeverResolved.
//
// A first draft of this test asserted the interface case and *passed*, which is the trap
// worth recording: the syntax error above happens to be reported on the same line the
// conformance diagnostic would have used, so a green result meant nothing.
TEST(Soundness_DiagnosticLocation, ADeclarationReportsWhereItWasWritten) {
    // A struct that promises an interface and does not deliver.
    EXPECT_EQ(reportedLine("interface I { pub fun f(self: &Self) <int>; }\n"
                           "struct S : <I> { pub: v <int>, }\n"
                           "fun main() <noret> { }\n"), "4")
        << "the conformance diagnostic belongs on the struct, not on line 1";

    // The same, one line further down, so a hardcoded line number cannot pass.
    EXPECT_EQ(reportedLine("interface I { pub fun f(self: &Self) <int>; }\n"
                           "\n"
                           "struct S : <I> { pub: v <int>, }\n"
                           "fun main() <noret> { }\n"), "5");

    // A class, which is a different production building a different node type.
    EXPECT_EQ(reportedLine("interface I { pub fun f(self: &Self) <int>; }\n"
                           "class K : <I> { pub: v <int>, }\n"
                           "fun main() <noret> { }\n"), "4")
        << "KW_CLASS ... RBRACE builds a ClassDeclaration and needs its own setLoc";
}

// An enum member's payload type is never resolved.
//
// `enum E { pub Ok <Nope>, }` compiles clean, and `Nope` is undefined. The payloads are
// parsed and stored -- EnumDeclaration::member_payloads is built in the KW_ENUM
// production and CloneVisitor carries it -- but nothing walks them looking for a type to
// resolve, so a typo in a payload type is silent until something tries to use the value.
//
// This is load-bearing for the library rather than hypothetical:
// `tests/samples/stdlib/stdio.fin:49` declares `pub enum IOResult<T: Strict<Stream>> {
// pub Err <IOError>, pub Ok <T>, }` and `lib/std/stdio.fin` carries its own copy. If
// `IOError` were misspelled there, eleven importers would inherit the mistake and none
// of them would say so.
//
// The inversion is Soundness_Enums.AnEnumMemberPayloadTypeIsResolvedWhereItIsWritten,
// and the diagnostic then wants the payload's own location, not the enum's.
TEST(KnownDefect_Enums, AnEnumMemberPayloadTypeIsNeverResolved) {
    const FincRun undefinedPayload = compile(
        "enum E { pub Ok <Nope>, }\n"
        "fun main() <noret> { }\n");
    EXPECT_EQ(undefinedPayload.exitCode, 0)
        << "when this starts reporting, invert the test and rename it\n"
        << stripAnsi(undefinedPayload.err);

    // Not a quirk of `pub`, and not a quirk of the single-member case.
    const FincRun several = compile(
        "struct Real {}\n"
        "enum E { Ok <Real>, Err <AlsoNope>, }\n"
        "fun main() <noret> { }\n");
    EXPECT_EQ(several.exitCode, 0) << stripAnsi(several.err);

    // The control: the very same name in the very same file *is* reported when it is
    // written in a place that gets resolved, so the name is genuinely undefined and it
    // is the payload position that is not looked at.
    const FincRun control = compile(
        "enum E { pub Ok <Nope>, }\n"
        "fun main() <noret> { let x <Nope> = 1; }\n");
    EXPECT_EQ(control.exitCode, 1) << stripAnsi(control.err);
    EXPECT_NE(stripAnsi(control.err).find("Undefined type 'Nope'"), std::string::npos)
        << stripAnsi(control.err);
}

// ---------------------------------------------------------------------------
// `new` on a type that does not resolve killed the compiler.
//
// visit(NewExpression&) (Analyzer_Expr.cpp:393) wrapped the allocated type in a
// PointerType without asking whether it resolved, so an unresolved type produced
// a non-null PointerType over a null pointee -- which passes every
// `if (!type) return;` downstream and dies at the first toString(), in
// PointerType.cpp:6, reached from checkType's own error message.
//
// The same shape and the same crash site as the nine samples in the earlier
// composite-type sweep, at a place that sweep did not reach: it fixed the
// branches of resolveTypeFromAST, and this one is in an expression visitor. A
// grep of every remaining make_shared<PointerType|ArrayType|FunctionType|
// PrototypeType> in src/semantics found the other six all guarded -- the
// prototype literal falls back to `any`, the rest return early -- so this was
// the last one.
//
// Found while asserting where its diagnostic points, from the 1.2 seconds the
// location test spent writing a core file. That is worth saying plainly: the
// exit code was never checked, because 139 satisfies EXPECT_NE(exitCode, 0).
// ---------------------------------------------------------------------------

TEST(Soundness_MachineContract, NewOnAnUndefinedTypeIsRejectedRatherThanFatal) {
    // EQ 1, not NE 0. The whole reason this defect survived a passing suite is
    // that a segfault is a nonzero exit code, so `NE 0` is not an assertion that
    // the compiler ran -- ADR 0009 admits exactly {0,1,2,3} and 139 is none of them.
    for (const char* code : {"fun main() <noret> { let x <int> = new Nope{}; }\n",
                             "fun main() <noret> { let x <int> = new Nope::<int>{}; }\n",
                             "fun main() <noret> { let x <int> = new Nope{a: 1}; }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(r.exitCode, 1)
            << "a `new` over an unresolved type must be a diagnostic, not a signal:\n"
            << code << r.err;
        EXPECT_NE(stripAnsi(r.err).find("Undefined type 'Nope'"), std::string::npos)
            << code << r.err;
    }
}

// ---------------------------------------------------------------------------
// A generic struct that mentions `Self` becomes self-referential the moment it is
// instantiated, and substituting it a second time used to recurse until the stack
// ran out.
//
// `pub fun me(self: &Self) <Self>` on `C<T>` is stored as `fn() -> Self`. Instantiating
// C replaces that `Self` with the new `C<int>` -- correctly, that is what `Self` means
// -- and the result is a StructType one of whose members points back at it. Nothing was
// wrong until the type was substituted again, which is what happens when C appears as a
// field of another generic struct: `struct H<T> { keys <&C<T>>, }`. Substituting
// `H<int>` walks the field, which walks `C<T>`, which walks its method's return type,
// which is `C<T>`, which walks its method's return type, for as long as the stack lasts.
//
// A single instantiation never crashed -- `let c <C<int>>;` builds the cycle and stops
// -- so the defect needed two levels to show, and the corpus reaches two levels only
// through the standard library: `HashMap<T, U>` holds two `Collection`s and
// `Collection<T>` has `from_prototype() <Self>`. The crash appeared the hour lib/std
// started resolving, on prototype_test.fin, as SIGSEGV in
// Soundness_MachineContract.NoSampleTerminatesTheCompilerBySignal.
//
// The cycle is not the bug and must not be broken: `Self` really is the type being
// built. The fix is that a substitution in progress publishes its result before it walks
// its members, so a recursive visit finds it instead of starting again.
// ---------------------------------------------------------------------------

TEST(Soundness_MachineContract, ASelfReferentialInstantiationIsNotFatal) {
    // The shape that crashed, at its smallest. EQ 0 and an empty stderr, because 139 is
    // a nonzero exit code and "it reported something" is not the assertion.
    const FincRun nested = compile(
        "struct C<T> {\n"
        "  v <T>,\n"
        "  pub fun me(self: &Self) <Self> { return C{v: self.v}; }\n"
        "}\n"
        "struct H<T> { keys <&C<T>>, }\n"
        "fun main() <int> { let h <H<int>>; return 0; }\n");
    EXPECT_EQ(nested.exitCode, 0) << "a struct held by a struct is not a stack overflow:\n"
                                  << stripAnsi(nested.err);
    EXPECT_EQ(errorCount(stripAnsi(nested.err)), 0u) << stripAnsi(nested.err);

    // A static returning `Self` is the spelling lib/std/collection.fin uses
    // (`from_prototype`), and it is the one prototype_test.fin reaches.
    const FincRun statik = compile(
        "struct C<T> {\n"
        "  v <T>,\n"
        "  pub static fun make(x: T) <Self> { return C{v: x}; }\n"
        "}\n"
        "struct H<T, U> { keys <&C<T>>, values <&C<U>>, }\n"
        "fun main() <int> { let h <&H<string, int>> = new H::<string, int>{}; return 0; }\n");
    EXPECT_EQ(statik.exitCode, 0) << stripAnsi(statik.err);
    EXPECT_EQ(errorCount(stripAnsi(statik.err)), 0u) << stripAnsi(statik.err);

    // Three levels, so the fix is not "one level of nesting".
    const FincRun deep = compile(
        "struct A<T> { v <T>, pub fun me(self: &Self) <Self> { return A{v: self.v}; } }\n"
        "struct B<T> { a <&A<T>>, pub fun me(self: &Self) <Self> { return B{a: self.a}; } }\n"
        "struct C2<T> { b <&B<T>>, }\n"
        "fun main() <int> { let c <C2<int>>; return 0; }\n");
    EXPECT_EQ(deep.exitCode, 0) << stripAnsi(deep.err);
    EXPECT_EQ(errorCount(stripAnsi(deep.err)), 0u) << stripAnsi(deep.err);

    // The same struct used twice at the same level, which is what HashMap does with
    // Collection: the two substitutions must not be confused for one another.
    const FincRun twice = compile(
        "struct C<T> { v <T>, pub fun me(self: &Self) <Self> { return C{v: self.v}; } }\n"
        "struct H<T, U> { a <&C<T>>, b <&C<U>>, }\n"
        "fun main() <int> {\n"
        "  let h <H<int, string>>;\n"
        "  let g <H<string, int>>;\n"
        "  return 0;\n"
        "}\n");
    EXPECT_EQ(twice.exitCode, 0) << stripAnsi(twice.err);
    EXPECT_EQ(errorCount(stripAnsi(twice.err)), 0u) << stripAnsi(twice.err);

    // And a single instantiation, which never crashed, still does not: the guard must
    // not turn a working case into a wrong one.
    const FincRun single = compile(
        "struct C<T> { v <T>, pub fun me(self: &Self) <Self> { return C{v: self.v}; } }\n"
        "fun main() <int> { let c <C<int>>; return 0; }\n");
    EXPECT_EQ(single.exitCode, 0) << stripAnsi(single.err);

    // The cycle is still a cycle. `me` returns the instantiated type, so its result has
    // the instantiated field type -- if the guard had cut the recursion by handing back
    // the uninstantiated template instead, `c.me().v` would be `T` and this would report.
    const FincRun preserved = compile(
        "struct C<T> { pub: v <T>, pub fun me(self: &Self) <Self> { return C{v: self.v}; } }\n"
        "struct H<T> { pub: keys <&C<T>>, }\n"
        "fun main() <int> {\n"
        "  let h <&H<int>> = new H::<int>{};\n"
        "  let n <int> = h.keys.me().v;\n"
        "  return 0;\n"
        "}\n");
    EXPECT_EQ(preserved.exitCode, 0)
        << "the self-reference survives instantiation\n" << stripAnsi(preserved.err);
    EXPECT_EQ(errorCount(stripAnsi(preserved.err)), 0u) << stripAnsi(preserved.err);
}

// ---------------------------------------------------------------------------
// The four meta-types are unregistered, so every declaration that annotates one
// is rejected.
//
// `$type`, `$struct`, `$interface` and `$enum_member` are the compile-time
// reflection types. The corpus says what they are rather than leaving it to be
// guessed: `stdlib/types.fin:33` writes `fun cast<_Type: $type>(value: Any<...>)`
// and comments "$type == literal type"; `:83` returns one from `tftid(tid: uint)
// <$type>`, "returns a type from typeid"; `stdlib/enums.fin:22` takes
// `$enum_member` with the example `keyidof(Ok)`; `literal_interface.fin:5` takes
// both `$interface` and `$struct` in one signature.
//
// The grammar learned all four in wave 2 -- parser.y:1776-1778 build a TypeNode
// named `$type` / `$struct` / `$interface` from `DOLLAR` plus the keyword, and
// `$enum_member` arrives through the `DOLLAR IDENTIFIER` catch-all at :1783 --
// and the analyzer learned none of them, so all four die at Analyzer_Core.cpp:204.
// That makes this a name-resolution gap and nothing more: what a `$type` *value*
// can do belongs to wave 4, and registering the name does not decide it.
//
// Four tests and not one, because the productions differ. A fix that registers
// `$type` alone leaves the other three exactly as broken, and `$enum_member`
// reaches the analyzer down a different grammar path than the other three.
// ---------------------------------------------------------------------------

TEST(Soundness_MetaTypes, DollarTypeIsADeclarableType) {
    auto r = compile("fun f(t: $type) <int> { return 1; }\n");
    EXPECT_EQ(r.exitCode, 0) << "`$type` is a builtin meta-type, not an undefined name:\n" << r.err;
}

TEST(Soundness_MetaTypes, DollarStructIsADeclarableType) {
    auto r = compile("fun f(s: $struct) <int> { return 1; }\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
    // The old diagnostic offered `did you mean 'struct'?`, which is the one
    // suggestion that must never be taken: `struct` is the declaration keyword
    // and `$struct` is a type. A regression that resolved `$struct` *to* a struct
    // would pass the exit-code check above, so the suggestion is asserted gone.
    EXPECT_EQ(stripAnsi(r.err).find("did you mean"), std::string::npos) << r.err;
}

TEST(Soundness_MetaTypes, DollarInterfaceIsADeclarableType) {
    auto r = compile("fun f(i: $interface) <int> { return 1; }\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(Soundness_MetaTypes, DollarEnumMemberIsADeclarableType) {
    // Reaches resolveTypeFromAST through `DOLLAR IDENTIFIER` (parser.y:1783),
    // not through a keyword production like the three above.
    auto r = compile("fun f(e: $enum_member) <int> { return 1; }\n");
    EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST(Soundness_MetaTypes, EveryPositionThatTakesATypeTakesAMetaType) {
    // Return, binding, array element and pointee, because resolveTypeFromAST is
    // reached from each and the array and pointer arms run *before* the name
    // lookup (Analyzer_Core.cpp:130-180): they recurse, so a fix that registered
    // the name only for a bare annotation would leave `[$type]` broken.
    for (const char* code : {"fun f() <$type> { return 1; }\n",
                             "fun f() <int> { let t <$type> = 1; return 1; }\n",
                             "fun f(a: [$type]) <int> { return 1; }\n",
                             "fun f(p: &$type) <int> { return 1; }\n"}) {
        auto r = compile(code);
        EXPECT_EQ(stripAnsi(r.err).find("Undefined type '$type'"), std::string::npos)
            << "the meta-type must resolve in every type position:\n" << code << r.err;
    }
}

TEST(Soundness_MetaTypes, AnUnknownDollarNameIsStillUndefined) {
    // The guard, and the reason the fix is four names rather than a prefix rule.
    // `DOLLAR IDENTIFIER` (parser.y:1783) accepts *any* `$name` as a type, so
    // "resolve anything beginning with `$`" is a one-line fix that turns every
    // typo into a silently accepted type. This test is what forbids it.
    auto r = compile("fun f(x: $wierd) <int> { return 1; }\n");
    EXPECT_EQ(r.exitCode, 1) << "only the four reflection meta-types exist:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("Undefined type '$wierd'"), std::string::npos) << r.err;
}

TEST(Soundness_MetaTypes, AMetaTypeIsNotAssignableFromAnOrdinaryValue) {
    // Registering the name must not make `$type` a synonym for `auto`. An int
    // initialiser for a `$type` binding has to be rejected -- and this is the
    // assertion that distinguishes "the meta-type resolves" from "the meta-type
    // swallows anything", which is what a PrimitiveType named `any` would do.
    auto r = compile("fun f() <int> { let t <$type> = 1; return 1; }\n");
    EXPECT_EQ(r.exitCode, 1) << "an int is not a type value:\n" << r.err;
}


// ---------------------------------------------------------------------------
// Nullability.
//
// tests/samples/nullifier.fin is a complete specification of it, and every rule
// below is quoted from that file or from another sample that exercises the same
// construct. The grammar has accepted all of it since wave 2 --
// `TypeNode::is_nullable` is set in twenty places in parser.y -- and until this
// wave nothing in src/semantics/ or src/types/ ever read the flag. So every
// nullable spelling parsed and then meant exactly what the non-nullable spelling
// meant, which is the worst of the three possible states: the source says the
// value may be absent, and the compiler agrees to nothing.
//
// The rules, and where each comes from:
//
//   `T?` accepts `null`              nullifier.fin:4, :18, :34
//   `T?` accepts a plain `T`         nullifier.fin:7 (`b? <int>`, `return self.b`)
//   `T` rejects a `T?`              nullifier.fin:31 -- the `?` on `make_A(-1)?`
//                                    is what makes it assignable, so without the
//                                    `?` it must not be
//   `T` rejects `null`              nullifier.fin:31 again, and undefined_behavior.fin:16
//   `&T` accepts `null`              deeptest3.fin:64, :75, :80 -- a pointer is
//                                    already nullable and stays that way
//   postfix `?` strips one `?`       nullifier.fin:31, :42, undefined_behavior.fin:16
//   `fun?` makes the return nullable nullifier.fin:6, :16, undefined_behavior.fin:9
//   `fun?` may fall off the end      nullifier.fin:23 "Automatically returns null
//                                    even without an else statement", and
//                                    undefined_behavior.fin:9 "this function compiles"
//   `n?: int` is optional at a call  nullifier.fin:39 "since make_A says \"n?: int\"
//                                    we know that n can be null and we don't need
//                                    to pass any arguments"
//   anything may be compared to null nullifier.fin:40 compares a *denullified*
//                                    `_` with null and calls it correct, so the
//                                    comparison cannot require a nullable operand.
//                                    stdlib/error.fin:12 compares a plain `int`
//                                    parameter with null.
//   `= null` on an explicit type     nullifier.fin:4 calls `b? <int>` "equavelant
//                                    to `b <int> = null`". deeptest4.fin:6-7 and
//                                    stdlib/error.fin:11 both write the `= null`
//                                    spelling on a non-nullable type and then use
//                                    the member or parameter *without* a denullify
//                                    (deeptest4.fin:16 `a["Hi"].integer == 10`,
//                                    error.fin:14 `error_id: err_code`), which is
//                                    what settles the reading: `= null` is a
//                                    permitted "absent" initialiser, and it does
//                                    not change the declared type. The stronger
//                                    reading -- that it makes the declaration
//                                    nullable -- would require both of those
//                                    normative samples to denullify, and neither
//                                    does. See ANullDefaultDoesNotMakeItNullable.
//
// What is *not* decided here, and is booked in docs/plan.md instead: what a
// postfix `?` on an already-non-nullable value means (nullifier.fin:36 says
// `mibombo?` on an `any` "should be an error", but `any` itself is unresolved),
// and whether two nullable types of different inner type may be compared.

TEST(Soundness_Nullability, ANullableSlotAcceptsNull) {
    // nullifier.fin:34, spelled exactly as the sample spells it.
    //
    // Read what this does and does not prove. It is a *declaration*, so
    // checkInitializer waves the null through before assignability is consulted:
    // it would still pass with NullType assignable to nothing at all. Mutation
    // confirmed that -- M6 ("NullType is not assignable to a nullable") does not
    // kill it. What it pins is that the sample's own line compiles; the
    // assignability rule is pinned by the two tests below, which are in positions
    // no exemption covers.
    auto r = compile("fun main() <int> { let x? <int> = null; return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "`let x? <int> = null` is nullifier.fin:34:\n" << r.err;
}

TEST(Soundness_Nullability, ANullableSlotMayBeAssignedNull) {
    // The assignment position, which nothing exempts -- so this is the test that
    // actually reaches "NullType is assignable to a NullableType". The
    // declaration above cannot get there.
    auto r = compile("fun main() <int> { let x? <int> = 1; x = null; return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "`x = null` on an `int?` is legal:\n" << r.err;
}

TEST(Soundness_Nullability, ANonNullableParameterRejectsNull) {
    // The control. If this ever passes, `?` has stopped meaning anything and
    // every test above it is vacuous.
    //
    // An argument, not `let x <int> = null` -- that spelling is a *declaration*
    // and is legal (AnExplicitTypeMayBeInitialisedToNull below, deeptest4.fin:6).
    // The first draft of this test used it and passed for the wrong reason, which
    // is the argument for the boundary test that follows the two of them.
    auto r = compile("fun g(n: int) <int> { return 0; }\n"
                     "fun main() <int> { let z <int> = g(null); return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "a plain `int` parameter cannot take null:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("expected 'int'"), std::string::npos) << r.err;
}

TEST(Soundness_Nullability, ANonNullableReturnRejectsNull) {
    // The other non-declaration position, and the one undefined_behavior.fin:9
    // turns on: a plain `fun` may not return null, a `fun?` may. Split from the
    // argument case because `visit(ReturnStatement&)` and the argument loop in
    // `visit(FunctionCall&)` are separate checks.
    auto r = compile("fun f() <int> { return null; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "a plain `fun` cannot return null:\n" << r.err;
}

TEST(Soundness_Nullability, NullNamesItselfInADiagnostic) {
    // Split from the control above because the two have different fixes: the
    // rejection was already right, the *message* was not. `&void` was an
    // implementation detail of how the literal was typed, and it named a pointer
    // type in a diagnostic about a program containing no pointer.
    auto r = compile("fun g(n: int) <int> { return 0; }\n"
                     "fun main() <int> { let z <int> = g(null); return 0; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_NE(err.find("got 'null'"), std::string::npos)
        << "the literal's type should print as `null`, not as a pointer:\n" << err;
    EXPECT_EQ(err.find("&void"), std::string::npos)
        << "no diagnostic about `null` should mention a pointer type:\n" << err;
}

TEST(Soundness_Nullability, TheNullInitialiserRuleStopsAtTheDeclaration) {
    // The boundary, in one program, because the three positions are three
    // separate checks and only the first is permitted. Written as one compile so
    // that a future change which widens the rule from declarations to "anywhere"
    // cannot pass by fixing one position at a time.
    auto r = compile("fun g(n: int) <int> { return 0; }\n"
                     "fun main() <int> {\n"
                     "    let x <int> = null;\n"   // legal: a declaration
                     "    x = null;\n"             // not: an assignment
                     "    let z <int> = g(null);\n" // not: an argument
                     "    return 0;\n"
                     "}\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    const std::string err = stripAnsi(r.err);
    // Two diagnostics, on lines 4 and 5. Line 3 must produce none.
    EXPECT_NE(err.find(":4:"), std::string::npos) << "`x = null` must be reported:\n" << err;
    EXPECT_NE(err.find(":5:"), std::string::npos) << "`g(null)` must be reported:\n" << err;
    EXPECT_EQ(err.find(":3:"), std::string::npos)
        << "`let x <int> = null` is a declaration and must not be:\n" << err;
}

TEST(Soundness_Nullability, APointerStillAcceptsNull) {
    // deeptest3.fin:75 `print_if_exists(null)` against `val_ptr: &int`. A pointer
    // was nullable before this wave and must stay nullable: retyping the literal
    // must not take that away.
    //
    // An argument, not `let p <&int> = null`. The first draft used the
    // declaration, and a mutant that removed the pointer arm from
    // NullType::isAssignableTo entirely *survived* it -- because a declaration's
    // initialiser is exempted from the type check before assignability is ever
    // asked (checkInitializer). The test asserted a rule it never reached.
    auto r = compile("fun g(p: &int) <int> { return 0; }\n"
                     "fun main() <int> { let z <int> = g(null); return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "a pointer is nullable (deeptest3.fin:75):\n" << r.err;
}

TEST(Soundness_Nullability, APointerMayBeAssignedNull) {
    // The third position, and the one no exemption covers: a plain assignment.
    // deeptest3.fin:80's `pub next <&Node> = null` is a declaration, so on its own
    // it leaves the assignment case unpinned.
    auto r = compile("fun main() <int> { let a <int> = 1; let p <&int> = &a; p = null; return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "`p = null` on a pointer is legal:\n" << r.err;
}

TEST(Soundness_Nullability, AMemberOfANullableStructNeedsADenullify) {
    // The safety property the whole feature is for: a value that may be absent
    // must not be walked into. Reaching a field through an `S?` has to be refused,
    // and it is the one consequence of nullability that is about *use* rather than
    // about assignment.
    auto r = compile("struct S { pub b <int>, }\n"
                     "fun main() <int> { let s? <S> = S{b: 1}; let v <int> = s.b; return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "`s.b` on an `S?` must be refused:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("'S?'"), std::string::npos)
        << "and the diagnostic must name the nullable type:\n" << stripAnsi(r.err);
}

TEST(Soundness_Nullability, AMethodOnANullableStructNeedsADenullify) {
    // Same property, different visit: MethodCall and MemberAccess are separate
    // functions with separate diagnostics (Analyzer_Expr.cpp:357 and :482), so a
    // fix reaching one leaves the other.
    auto r = compile("struct S { pub fun m(self: &Self) <int> { return 1; } }\n"
                     "fun main() <int> { let s? <S> = S{}; let v <int> = s.m(); return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "`s.m()` on an `S?` must be refused:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("'S?'"), std::string::npos) << stripAnsi(r.err);
}

TEST(Soundness_Nullability, DenullifyThenReachIntoIt) {
    // nullifier.fin:42 `make_A(10)?.get_b()?` -- the pair to the two tests above,
    // and the reason they are not simply "nullable values are unusable". One `?`
    // is the difference between the three programs.
    auto r = compile("struct S { pub fun m(self: &Self) <int> { return 1; } }\n"
                     "fun main() <int> { let v <int> = 0; let s? <S> = S{}; v = s?.m(); return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "`s?.m()` is nullifier.fin:42:\n" << r.err;
}

TEST(Soundness_Nullability, ANullableSlotAcceptsAPlainValue) {
    // nullifier.fin:7 returns `self.b` -- an `int?` member -- from a `fun?`
    // returning `int`, and the reverse direction appears at :27. Passed before
    // the fix too, but for the wrong reason (`int?` *was* `int`), so it is here
    // to pin that widening survived the type actually existing.
    auto r = compile("fun main() <int> { let x? <int> = 5; return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "`T?` is wider than `T`:\n" << r.err;
}

TEST(Soundness_Nullability, ANonNullableSlotRejectsANullableValue) {
    // The other half, and the half that was silently wrong. nullifier.fin:31
    // spells `let myvar2 <A> = make_A(-1)?` with a `?`, and the comment says the
    // `?` is what "makes any null value raise a panic error OR just returns the
    // normal value". If the assignment were legal without it, the `?` would be
    // decoration and the sample would be lying about why it is there.
    auto r = compile("fun main() <int> { let a? <int> = 5; let b <int> = a; return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "an `int?` needs a denullify to become an `int`:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("got 'int?'"), std::string::npos)
        << "and the diagnostic has to say which type is nullable:\n" << stripAnsi(r.err);
}

TEST(Soundness_Nullability, DenullifyMakesItAssignable) {
    // The pair to the test above: identical program plus one `?`. Split from it
    // because a fix that made `int?` and `int` interchangeable again would leave
    // this one green, and only the pair pins that the `?` is what did the work.
    auto r = compile("fun main() <int> { let a? <int> = 5; let b <int> = a?; return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "postfix `?` strips the nullability (nullifier.fin:31):\n" << r.err;
}

TEST(Soundness_Nullability, DenullifyStripsExactlyOneLevel) {
    // `let b? <int> = a?` must still be legal -- widening back to nullable -- and
    // more importantly a single `?` must not be read as "make this assignable to
    // anything". Without this, `isAssignableTo` returning true unconditionally
    // for a denullified value would pass every other test in the suite.
    auto r = compile("fun main() <int> { let a? <int> = 5; let b <string> = a?; return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "denullifying an `int?` gives an `int`, not a free pass:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("expected 'string', got 'int'"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_Nullability, ANullableStructMemberIsNullable) {
    // nullifier.fin:4. The member spellings are six separate grammar rules
    // (parser.y, `IDENTIFIER QUESTION LT type GT` and its five siblings), so a
    // fix that only reached `let` would leave the sample's own construct broken.
    auto r = compile("struct S { pub v? <int>, }\n"
                     "fun main() <int> { let s <S> = S{}; let q <int> = s.v; return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "`v? <int>` is an `int?`, and `q <int>` needs a denullify:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("got 'int?'"), std::string::npos) << stripAnsi(r.err);
}

TEST(Soundness_Nullability, ANullableParameterIsNullable) {
    // parser.y's `IDENTIFIER QUESTION COLON type` -- nullifier.fin:16 `n?: int`.
    // Same reason as the member test: a different grammar rule sets the flag, so
    // it needs its own assertion that the flag was read.
    auto r = compile("fun g(n?: int) <int> { let k <int> = n; return 0; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "`n?: int` is an `int?` inside the body:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("got 'int?'"), std::string::npos) << stripAnsi(r.err);
}

TEST(Soundness_Nullability, ANullableParameterMayBeOmitted) {
    // nullifier.fin:39, quoted in the header comment above. Was `Function 'g'
    // expects 1 arguments, got 0`.
    auto r = compile("fun g(n?: int) <int> { return 0; }\n"
                     "fun main() <int> { let z <int> = g(); return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "a nullable parameter is optional at the call site:\n" << r.err;
}

TEST(Soundness_Nullability, APlainParameterIsStillRequired) {
    // The control for the arity change. One `?` apart from the test above.
    auto r = compile("fun g(n: int) <int> { return 0; }\n"
                     "fun main() <int> { let z <int> = g(); return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "a plain parameter is still required:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("expects 1 arguments, got 0"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_Nullability, TooManyArgumentsIsStillAnError) {
    // The other side of the arity change: making the minimum smaller must not
    // make the maximum unbounded. Without this, "skip the arity check when any
    // parameter is nullable" would pass every arity test in the suite.
    auto r = compile("fun g(n?: int) <int> { return 0; }\n"
                     "fun main() <int> { let z <int> = g(1, 2); return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "two arguments for one parameter is still wrong:\n" << r.err;
}

TEST(Soundness_Nullability, ANullableParameterAcceptsNull) {
    // The point of declaring it nullable. Was rejected with `expected 'int', got
    // '&void'` -- the argument check ran against the un-nullified `int`.
    auto r = compile("fun g(n?: int) <int> { return 0; }\n"
                     "fun main() <int> { let z <int> = g(null); return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "`n?: int` accepts null:\n" << r.err;
}

TEST(Soundness_Nullability, FunQuestionMakesTheReturnTypeNullable) {
    // nullifier.fin:16's comment: "using '?' in types tells us that it might
    // return that type OR null". undefined_behavior.fin:16 then writes
    // `add2()?`, and the `?` is only meaningful if the call's type needs one.
    auto r = compile("fun? f() <int> { return 1; }\n"
                     "fun main() <int> { let x <int> = f(); return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "`fun?` returns `int?`, so this assignment needs a `?`:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("got 'int?'"), std::string::npos) << stripAnsi(r.err);
}

TEST(Soundness_Nullability, DenullifyingAFunQuestionCallGivesThePlainType) {
    // undefined_behavior.fin:16 exactly. Pairs with the test above the way
    // DenullifyMakesItAssignable pairs with ANonNullableSlotRejectsANullableValue.
    auto r = compile("fun? f() <int> { return 1; }\n"
                     "fun main() <int> { let x <int> = f()?; return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "`f()?` is an `int` (undefined_behavior.fin:16):\n" << r.err;
}

TEST(Soundness_Nullability, FunQuestionMayReturnNull) {
    // nullifier.fin:18 `return null;` inside `fun? make_A(...) <A>`.
    auto r = compile("fun? f() <int> { return null; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "a `fun?` may return null explicitly:\n" << r.err;
}

TEST(Soundness_Nullability, FunQuestionMayFallOffTheEnd) {
    // nullifier.fin:23 -- "Automatically returns null even without an else
    // statement" -- and undefined_behavior.fin:9, whose comment says in so many
    // words "this function compiles". It did not: finc reported "Function 'add2'
    // is missing a return statement on some paths" on the very line the sample
    // says is legal. The sample passed anyway because `//@ error` means "at least
    // this diagnostic" (test_expectations.cpp), so the extra one was invisible.
    auto r = compile("fun? f() <int> { if (0) { return 1; } }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "a `fun?` has an implicit `return null`:\n" << r.err;
}

TEST(Soundness_Nullability, APlainFunctionStillNeedsAReturnOnEveryPath) {
    // The control, and undefined_behavior.fin:3 is the sample that pins it. One
    // `?` apart from the test above; the whole missing-return check must not be
    // what got suppressed.
    auto r = compile("fun f() <int> { if (0) { return 1; } }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "a plain function must return on every path:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("missing a return statement"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_Nullability, AnythingMayBeComparedToNull) {
    // nullifier.fin:40 `blame _? == null;` compares a denullified value -- a
    // plain `int` -- with null, and the sample's own gloss (`assert @unpacked(_)
    // == null`) says that is the intended reading. stdlib/error.fin:12 does the
    // same with a plain `int` parameter. Both were `expected 'int', got '&void'`.
    auto r = compile("fun main() <int> { let x <int> = 0; blame x == null; return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "comparing against null is always legal:\n" << r.err;
}

TEST(Soundness_Nullability, NullMayBeTheLeftOperandOfAComparison) {
    // Split from the test above because the equality path checks the right
    // operand against the left (Analyzer_Expr.cpp, visit(BinaryOp&)), so the two
    // orders take different branches and a fix aimed at one leaves the other red.
    auto r = compile("fun main() <int> { let x <int> = 0; blame null == x; return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "`null == x` is the same question as `x == null`:\n" << r.err;
}

TEST(Soundness_Nullability, NullIsNotOrdered) {
    // The scope limit on the comparison rule. `==` and `!=` against null are
    // always legal; `<` is not, because no sample orders anything against null and
    // deciding what "less than null" means would be a ruling, not a fix. Without
    // this line, extending the exemption to the whole relational family -- the
    // six comparison operators share one branch in visit(BinaryOp&) -- passes
    // every other test in the suite.
    auto r = compile("fun main() <int> { let x <int> = 0; blame x < null; return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "`x < null` is not a question with an answer:\n" << r.err;
}

TEST(Soundness_Nullability, NullIsStillRejectedInAnOrdinaryAssignment) {
    // The scope limit on the `= null` rule below. A *declaration* may say
    // `= null`; a later assignment to a non-nullable variable may not. Without
    // this line, "accept null against anything" passes the whole suite.
    auto r = compile("fun main() <int> { let x <int> = 0; x = null; return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "`x = null` on a non-nullable `int` is an error:\n" << r.err;
}

TEST(Soundness_Nullability, AnExplicitTypeMayBeInitialisedToNull) {
    // deeptest4.fin:6-7 and stdlib/error.fin:11. Both normative, both write
    // `= null` against a non-nullable type, and both were rejected twice over --
    // struct members are checked in registration and again in the visit, so
    // deeptest4's two members produced four diagnostics.
    auto r = compile("struct S { v <int> = null, }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "`v <int> = null` is deeptest4.fin:6:\n" << r.err;
}

TEST(Soundness_Nullability, ANullDefaultDoesNotMakeItNullable) {
    // The reading this fix commits to, and the one measurement that separates it
    // from the alternative. deeptest4.fin:16 uses `a["Hi"].integer` in an `== 10`
    // comparison and stdlib/error.fin:14 passes `err_code` straight into an
    // `<int>` field, neither with a denullify -- so `= null` cannot have made
    // those declarations nullable. If it had, both normative samples would need
    // a `?` they do not write.
    auto r = compile("struct S { pub v <int> = null, }\n"
                     "fun main() <int> { let s <S> = S{}; let q <int> = s.v; return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "`v <int> = null` leaves v an `int` (deeptest4.fin:16):\n" << r.err;
}

TEST(Soundness_Nullability, ANullDefaultOnAParameterIsAccepted) {
    // stdlib/error.fin:11 `Error(msg: string, err_code: int = null)`. A separate
    // grammar path from the struct member, hence a separate test.
    //
    // Unprovable, on purpose, and the reason is worth keeping: no mutant of this
    // wave can kill it, because `visit(Parameter&)` resolves the declared type and
    // visits the default expression without ever comparing the two. `fun g(n: string = 3)`
    // is accepted for the same reason. So this passes because the check is absent,
    // not because null is exempt -- and when that defect is fixed (docs/plan.md,
    // "a parameter's default value is never checked") this test starts asserting
    // something, and a `= null` default must keep passing while `= 3` starts failing.
    auto r = compile("fun g(n: int = null) <int> { return n; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "`n: int = null` is stdlib/error.fin:11:\n" << r.err;
}

TEST(Soundness_Nullability, ANullableTypeIsPrintedWithItsQuestionMark) {
    // The diagnostic contract for the new type. `int?` and `int` are different
    // types now, so a message naming both must be readable -- "expected 'int',
    // got 'int'" is worse than no message.
    auto r = compile("fun main() <int> { let a? <int> = 5; let b <string> = a; return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << r.err;
    EXPECT_NE(stripAnsi(r.err).find("expected 'string', got 'int?'"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_Nullability, ANullableIsNotAssignableToADifferentNullable) {
    // `int?` and `string?` are as distinct as `int` and `string`. Guards against
    // "target is nullable, therefore anything goes", which is the shortest wrong
    // implementation of the widening rule.
    auto r = compile("fun main() <int> { let a? <int> = 5; let b? <string> = a; return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "`int?` is not a `string?`:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("expected 'string?', got 'int?'"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_Nullability, ANullablePointerIsStillAPointer) {
    // `&int` and `&int?` are both nullable at runtime but they are not the same
    // written type, and the wrapper must nest rather than collapse. parser.y sets
    // is_nullable on the outer TypeNode, so `p? <&int>` is `(&int)?`.
    auto r = compile("fun main() <int> { let p? <&int> = null; let q <&int> = p?; return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "`(&int)?` denullifies to `&int`:\n" << r.err;
}

// ---------------------------------------------------------------------------
// Soundness_ParameterDefaults
//
// A parameter's default value was the only expression in the language that no
// pass ever visited. `fun g(n: int = nosuchvar)` compiled clean, and so did the
// same default on a struct method, a class method, a struct constructor, a class
// constructor, and an interface method requirement -- six spellings, six loops,
// none of which looked at `default_value`. `visit(Parameter&)` does visit it, and
// nothing calls `visit(Parameter&)`: every parameter loop in Analyzer_Decl.cpp
// walks `param->type` by hand. The same "N copies of one loop" shape as the
// generic-parameter bounds, and found the same way -- by asking why a construct
// that is checked in a struct member (`pub v <int> = nosuchvar` reports it) is not
// checked one line away in a constructor signature.
//
// These tests assert only that the default is *visited*, which is what catches the
// undefined name. They deliberately do not assert that it is type-checked against
// the parameter's declared type -- see KnownDefect_ParameterDefaults below, which
// records why that half is blocked.
//
// One test per declaration form on purpose: the loops are separate copies, so a
// fix that reaches `visit(FunctionDeclaration&)` leaves the four others green.

TEST(Soundness_ParameterDefaults, AFunctionDefaultIsVisited) {
    auto r = compile("fun g(n: int = nosuchvar) <int> { return 0; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "an undefined name in a default must be reported:\n" << r.err;
    EXPECT_NE(messagesOnly(stripAnsi(r.err)).find("nosuchvar"), std::string::npos) << stripAnsi(r.err);
}

TEST(Soundness_ParameterDefaults, AStructMethodDefaultIsVisited) {
    auto r = compile("struct S { pub fun m(self: &Self, n: int = nosuchvar) <int> { return 0; } }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "a struct method's default:\n" << r.err;
    EXPECT_NE(messagesOnly(stripAnsi(r.err)).find("nosuchvar"), std::string::npos) << stripAnsi(r.err);
}

TEST(Soundness_ParameterDefaults, AClassMethodDefaultIsVisited) {
    auto r = compile("class C { pub v <int>, pub fun m(self: &Self, n: int = nosuchvar) <int> { return 0; } }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "a class method's default:\n" << r.err;
    EXPECT_NE(messagesOnly(stripAnsi(r.err)).find("nosuchvar"), std::string::npos) << stripAnsi(r.err);
}

TEST(Soundness_ParameterDefaults, AStructConstructorDefaultIsVisited) {
    // stdlib/error.fin:11's shape. A constructor's parameters are walked twice --
    // once to register the signature, once for the body -- so this is also the
    // test that would catch the default being reported twice.
    auto r = compile("struct S { pub v <int>, S(n: int = nosuchvar) { self.v = n; } }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "a struct constructor's default:\n" << r.err;
    const std::string err = stripAnsi(r.err);
    // messagesOnly, because the caret snippet echoes the offending source line and
    // `nosuchvar` is in it -- the first draft counted raw stderr and went red against a
    // correct compiler. See messagesOnly's own comment.
    const std::string msgs = messagesOnly(err);
    EXPECT_NE(msgs.find("nosuchvar"), std::string::npos) << err;
    EXPECT_EQ(errorCount(msgs), 1u)
        << "reported once, not once per pass over the parameter list:\n" << err;
}

TEST(Soundness_ParameterDefaults, AClassConstructorDefaultIsVisited) {
    auto r = compile("class C { pub v <int>, C(n: int = nosuchvar) { self.v = n; } }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "a class constructor's default:\n" << r.err;
    const std::string err = stripAnsi(r.err);
    // messagesOnly, because the caret snippet echoes the offending source line and
    // `nosuchvar` is in it -- the first draft counted raw stderr and went red against a
    // correct compiler. See messagesOnly's own comment.
    const std::string msgs = messagesOnly(err);
    EXPECT_NE(msgs.find("nosuchvar"), std::string::npos) << err;
    EXPECT_EQ(errorCount(msgs), 1u)
        << "reported once, not once per pass over the parameter list:\n" << err;
}

TEST(Soundness_ParameterDefaults, AnInterfaceMethodDefaultIsVisited) {
    // stdlib/stdio.fin:87 `fun read(nbytes: ulong = -1) <[char]>;` -- a requirement
    // with no body, and the only pass over its parameters is the one that registers
    // the requirement. So it is the site most likely to be missed by a fix aimed at
    // function bodies.
    auto r = compile("interface I { fun m(n: int = nosuchvar) <int>; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "an interface method requirement's default:\n" << r.err;
    EXPECT_NE(messagesOnly(stripAnsi(r.err)).find("nosuchvar"), std::string::npos) << stripAnsi(r.err);
}

TEST(Soundness_ParameterDefaults, AnOperatorDefaultIsVisited) {
    // deeptest2.fin:29's form. An operator declaration has its own parameter loop
    // and its own visit, so it is a seventh copy rather than a special case of the
    // method loop.
    auto r = compile("struct S { pub v <int>, operator +(self: &Self, other: int = nosuchvar) <int> { return 0; } }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "an operator's default:\n" << r.err;
    EXPECT_NE(messagesOnly(stripAnsi(r.err)).find("nosuchvar"), std::string::npos) << stripAnsi(r.err);
}

TEST(Soundness_ParameterDefaults, AnInterfaceOperatorDefaultIsVisited) {
    // deeptest2.fin:15 `operator +(self: &Self, other : T) <T>;` -- a requirement,
    // and an eighth loop.
    auto r = compile("interface I { operator +(self: &Self, other: int = nosuchvar) <int>; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "an interface operator requirement's default:\n" << r.err;
    EXPECT_NE(messagesOnly(stripAnsi(r.err)).find("nosuchvar"), std::string::npos) << stripAnsi(r.err);
}

TEST(Soundness_ParameterDefaults, AnExternDeclarationDefaultIsVisited) {
    // `@define printf(fmt: string, ...) <int>;` is struct_methods.fin:3 and
    // complex.fin:5. An extern has no body, so like the interface requirement it
    // gets exactly one pass over its parameters.
    auto r = compile("@define ext(fmt: string = nosuchvar, ...) <int>;\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "an extern's default:\n" << r.err;
    EXPECT_NE(messagesOnly(stripAnsi(r.err)).find("nosuchvar"), std::string::npos) << stripAnsi(r.err);
}

TEST(Soundness_ParameterDefaults, ASpecialDeclarationDefaultIsVisited) {
    // `@special(pub) typeid(T: $type) <int>` is the standard library's spelling
    // (stdlib/types.fin:24). Compile-time functions are analyzed by their own visit.
    auto r = compile("@special sp(n: int = nosuchvar) <int> { return 0; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "a compile-time function's default:\n" << r.err;
    EXPECT_NE(messagesOnly(stripAnsi(r.err)).find("nosuchvar"), std::string::npos) << stripAnsi(r.err);
}

TEST(Soundness_ParameterDefaults, AValidDefaultIsStillAccepted) {
    // The control. Visiting the default must not make a correct one an error.
    auto r = compile("fun g(n: int = 3) <int> { return n; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "`n: int = 3` is legal:\n" << r.err;
}

TEST(Soundness_ParameterDefaults, ANullDefaultIsStillAccepted) {
    // stdlib/error.fin:11 `err_code: int = null`. Guards the nullability rule
    // (Soundness_Nullability, checkInitializer) against this change.
    auto r = compile("fun g(n: int = null) <int> { return 0; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "`n: int = null` is stdlib/error.fin:11:\n" << r.err;
}

TEST(Soundness_ParameterDefaults, ADefaultMayNameAnEarlierDeclaration) {
    // Visiting the default has to happen somewhere, and where decides what a
    // default may name. Nothing in the corpus needs a global here and no ruling
    // covers it, so the looser reading is taken deliberately rather than inventing
    // a scoping rule that would reject a sample nobody has written yet.
    // This test alone does NOT pin the placement: a global is visible from the
    // enclosing scope as well, so a mutant that moved the visit above the
    // parameter-declaring loop survived it. ADefaultMayNameASiblingParameter is
    // the one that pins it.
    auto r = compile("let g_default <int> = 7;\n"
                     "fun g(n: int = g_default) <int> { return n; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "a default may name a global:\n" << r.err;
}

// ---------------------------------------------------------------------------
// KnownDefect_ParameterDefaults
//
// The other half of the same defect, and the half that is blocked. A parameter's
// default is not compared against the parameter's declared type, so
// `fun g(n: string = 3)` is accepted. Every other default in the language is
// checked -- a struct member's `pub v <int> = "nope"` reports `expected 'int', got
// 'string'` -- so this is an inconsistency, not a design.
//
// It is not fixed here because the corpus would regress on a question the owner
// has not answered. stdlib/stdio.fin:87 and :109 both write
// `fun read(nbytes: ulong = -1)`, and `let x <ulong> = -1` is an error today
// (`expected 'ulong', got 'int'`). Adding the check therefore puts two new
// diagnostics on a normative sample, and whether it should is exactly the integer
// ruling in docs/plan.md: is `-1` a legal unsigned constant? Answer that and this
// becomes a two-line change at the site the tests above already reach.

TEST(KnownDefect_ParameterDefaults, ADefaultOfTheWrongTypeIsAccepted) {
    auto r = compile("fun g(n: string = 3) <int> { return 0; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 0)
        << "when this fails, the default is being type-checked: invert it, and check\n"
           "stdlib/stdio.fin -- `nbytes: ulong = -1` decides whether that is correct.\n"
        << r.err;
}

TEST(KnownDefect_ParameterDefaults, AnUnsignedParameterDefaultingToMinusOneIsAccepted) {
    // stdlib/stdio.fin:87 and :109, reduced. This is the sample line that the fix
    // above would break, kept as its own test so that the blocker is visible from
    // the suite and not only from the plan.
    auto r = compile("fun g(n: ulong = -1) <int> { return 0; }\n"
                     "fun main() <int> { return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "stdlib/stdio.fin:87's `nbytes: ulong = -1`:\n" << r.err;
}

TEST(Soundness_ParameterDefaults, ADefaultMayNameASiblingParameter) {
    // The test that pins *where* the default is visited. The call sits after the
    // loop that defines each parameter in the body scope, so an earlier parameter
    // is in scope in a later one's default. Move the call above that loop and this
    // is the only test that notices -- which is why it exists separately from
    // ADefaultMayNameAnEarlierDeclaration, whose global resolves either way.
    // Both arguments are passed: whether a defaulted parameter may be *omitted* is
    // a different question, held open by KnownDefect_ParameterDefaults below.
    auto r = compile("fun g(a: int, b: int = a) <int> { return a + b; }\n"
                     "fun main() <int> { let z <int> = g(1, 2); return 0; }\n");
    EXPECT_EQ(r.exitCode, 0) << "an earlier parameter is in scope in a later default:\n" << r.err;
}

// The second half of the same root cause, and a bigger change than the first.
//
// `required` is computed in Analyzer_Expr's arity check as the index of the last
// parameter that is not nullable, plus one -- so a nullable parameter is optional and
// nothing else is. A parameter with a default is still counted as required, which
// leaves the default with no observable purpose at a call site: it can be named in an
// expression (see above) but never actually supplied by omission.
//
// Not fixed here, for a reason worth writing down rather than a lack of clarity about
// the meaning. The arity check reads a `FunctionType`, and `FunctionType` records only
// `param_types`, `return_type` and `is_vararg` -- it has no idea which parameters had
// defaults. Fixing this means a new field carried through eleven construction sites
// plus `substitute` and `clone`, which is the same "N copies of one loop" shape that
// this wave has now hit three times. It is a unit of its own.
//
// It is also, by measurement, a low-ranked one: the corpus declares exactly three
// defaulted parameters (stdlib/stdio.fin:87 and :109, stdlib/error.fin:11) and calls
// none of them. The one call that would need this, `Error("The answer is forbidden")`
// at blame_assert.fin:15, is commented out. So the corpus effect of the fix is zero
// diagnostics, which puts it below every other unit currently queued.

TEST(KnownDefect_ParameterDefaults, ADefaultedParameterIsStillRequired) {
    // Passes by asserting the defect. When the arity check learns about defaults this
    // goes red -- invert it to EXPECT_EQ(r.exitCode, 0) and move it to Soundness.
    auto r = compile("fun g(a: int, b: int = 2) <int> { return a + b; }\n"
                     "fun main() <int> { let z <int> = g(1); return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "today a default does not make a parameter optional:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("expects 2 arguments, got 1"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(KnownDefect_ParameterDefaults, ADefaultedConstructorParameterIsStillRequired) {
    // stdlib/error.fin:11's shape, called the way blame_assert.fin:15 wants to call it.
    // A constructor is reached as `S(args)` and goes through the same arity check.
    auto r = compile("struct S { pub v <int>, S(msg: int, code: int = null) { return new S{v: msg}; } }\n"
                     "fun main() <int> { let b <S> = S(1); return 0; }\n");
    EXPECT_EQ(r.exitCode, 1) << "today a constructor's default does not make it optional:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("expects 2 arguments, got 1"), std::string::npos)
        << stripAnsi(r.err);
}

// ---------------------------------------------------------------------------
// `foreach`, and the index binding the parser stores but nobody read.
//
// loops.fin:19 writes `foreach(idx <int>, element <int> in a)` and its own comment
// names the first binding the index and the second the element. Both spellings of that
// form parse -- parenthesised and bare, four productions in all (parser.y:2030) -- and
// ForeachLoop carries `index_name` and `index_type` for it. visit(ForeachLoop&) defined
// only `var_name`, so the body's `a[idx]` reported `Undefined variable 'idx'`: parsed,
// stored, and read by nobody, which is the third time that shape has turned up (see
// also the parameter defaults and `namespace_path`).
//
// This was the last diagnostic standing between loops.fin and `//@ ok`.

TEST(Soundness_Foreach, TheIndexBindingIsDefined) {
    // Both spellings, because there are two productions for the two-binding form and
    // one of them could be fixed while the other stayed broken -- they build the same
    // node but from different numbered slots, which is exactly how a transposed `$3`
    // hides.
    for (const char* code : {
             "fun main() <noret> { let a <[int]> = [1];\n"
             "  foreach (idx <int>, element <int> in a) { blame element == a[idx]; } }\n",
             "fun main() <noret> { let a <[int]> = [1];\n"
             "  foreach idx <int>, element <int> in a { blame element == a[idx]; } }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(r.exitCode, 0) << "the index binding must be defined in the loop body:\n"
                                 << code << r.err;
    }
}

TEST(Soundness_Foreach, TheIndexBindingIsScopedToTheLoop) {
    // Written as one program with both halves so that it cannot pass vacuously. Before
    // the fix `idx` was undefined everywhere, so an after-the-loop test on its own was
    // green for the wrong reason -- the same trap that a draft of
    // Soundness_DiagnosticLocation.ADeclarationReportsWhereItWasWritten fell into. Here
    // the first use must resolve and the second must not, and no single mistake gives
    // both.
    const FincRun r = compile(
        "fun main() <noret> { let a <[int]> = [1];\n"
        "  foreach (idx <int>, element <int> in a) { let inside <int> = idx; }\n"
        "  let outside <int> = idx; }\n");
    EXPECT_EQ(r.exitCode, 1) << "the index binding must not outlive the loop:\n" << r.err;
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u)
        << "exactly one: the use after the loop. Two means the use inside it failed too, "
           "and the scoping half of this test is then vacuous.\n"
        << stripAnsi(r.err);
    EXPECT_NE(messagesOnly(stripAnsi(r.err)).find("Undefined variable 'idx'"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_Foreach, TheElementBindingIsDefinedInEveryForm) {
    // The control. All four productions, so that adding the index binding to some of
    // them cannot lose the element binding from any of them.
    for (const char* code : {
             "fun main() <noret> { let a <[int]> = [1]; foreach (e <int> in a) { let z <int> = e; } }\n",
             "fun main() <noret> { let a <[int]> = [1]; foreach e <int> in a { let z <int> = e; } }\n",
             "fun main() <noret> { let a <[int]> = [1]; foreach (i <int>, e <int> in a) { let z <int> = e; } }\n",
             "fun main() <noret> { let a <[int]> = [1]; foreach i <int>, e <int> in a { let z <int> = e; } }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(r.exitCode, 0) << "the element binding must be defined:\n" << code << r.err;
    }
}

TEST(KnownDefect_Foreach, ABindingTypeIsNeverCheckedAgainstTheIterable) {
    // `foreach (e <string> in a)` over a `[int]` is accepted, and so is an index bound
    // as anything at all. visit(ForeachLoop&) resolves each written type and defines a
    // variable of it; it never asks the iterable what its elements are, so the
    // annotation is taken on trust and the body then type-checks against a lie.
    //
    // Not fixed here because it is not one check but a definition Fin has not written
    // down: what is iterable. An array yields its elements and an index that is an
    // `int` (arrays are indexed by `int` and nothing else -- Soundness_Arrays), a string
    // plausibly yields `char`, and a prototype has two candidate answers. The corpus
    // writes `foreach` twice, both over an array, both correctly annotated, so it
    // settles the array case and says nothing about the rest. Ruling first, then this
    // inverts into Soundness_Foreach.ABindingTypeIsCheckedAgainstTheIterable.
    for (const char* code : {
             "fun main() <noret> { let a <[int]> = [1]; foreach (e <string> in a) { let z <string> = e; } }\n",
             "fun main() <noret> { let a <[int]> = [1]; foreach (i <string>, e <int> in a) { let z <string> = i; } }\n"}) {
        const FincRun r = compile(code);
        EXPECT_EQ(r.exitCode, 0)
            << "FIXED: a foreach binding is now checked against the iterable. Invert this "
               "and record which types are iterable and what each one yields.\n"
            << code << r.err;
    }
}

TEST(KnownDefect_Foreach, TheIterableIsNeverCheckedForBeingIterable) {
    // `foreach (e <int> in 5)` compiles clean. The iterable is walked -- so an undefined
    // name in it is still reported -- and then discarded without being asked whether it
    // can be iterated at all. Same blocked ruling as above and the same inversion; kept
    // separate because it is the cheaper half: refusing a non-container needs only the
    // list of container kinds, while checking the binding needs each kind's yield.
    const FincRun r = compile("fun main() <noret> { foreach (e <int> in 5) { let z <int> = e; } }\n");
    EXPECT_EQ(r.exitCode, 0)
        << "FIXED: a non-iterable is now refused. Invert this into "
           "Soundness_Foreach.TheIterableMustBeIterable.\n"
        << r.err;
    // The walk itself must keep happening, or the fix above would be building on a
    // silence rather than on a type.
    const FincRun q = compile("fun main() <noret> { foreach (e <int> in nosuchthing) { } }\n");
    EXPECT_EQ(q.exitCode, 1) << "the iterable expression must still be walked:\n" << q.err;
    EXPECT_NE(messagesOnly(stripAnsi(q.err)).find("nosuchthing"), std::string::npos)
        << stripAnsi(q.err);
}

// ---------------------------------------------------------------------------
// `catch (Error as err)`: which side is the type.
//
// The corpus writes exactly one catch clause, readonly.fin:50, and it writes
// `catch (Error as err)`. The grammar read the two the other way round --
// `KW_CATCH LPAREN IDENTIFIER KW_AS type RPAREN`, so `Error` became the binding name
// and `err` was handed to resolveTypeFromAST -- and the sample's single remaining
// diagnostic was `Undefined type 'err'`, pointing at its own binding.
//
// Type-on-the-left is not a coin toss: every other `as` in Fin reads the same way.
// `import stdio as io` and `extern original as local` (extern_as.fin, whose whole
// subject it is) both name something that exists on the left and bind it on the right.
// A catch clause that inverted that would be the only one, and the one corpus site
// agrees with the rest of the language rather than with the grammar.

TEST(Soundness_TryCatch, TheCatchClauseBindsTheTypeOnTheLeftToTheNameOnTheRight) {
    // Three claims in one program, and the third is what rules out a fix that merely
    // swaps the diagnostic: the type resolves, the name is bound, and the bound name
    // carries the type's members. Swapping the slots without meaning it would leave
    // `e.code` reporting on a struct called `e`.
    const FincRun r = compile(
        "struct MyErr {\n"
        "  pub:\n"
        "    code <int>,\n"
        "}\n"
        "fun main() <noret> { try { } catch (MyErr as e) { let c <int> = e.code; } }\n");
    EXPECT_EQ(r.exitCode, 0) << "`catch (T as name)` binds name to T:\n" << r.err;
}

TEST(Soundness_TryCatch, TheCatchBindingIsScopedToTheCatchBlock) {
    // Both halves in one program for the reason Soundness_Foreach.TheIndexBindingIsScopedToTheLoop
    // gives: before the swap the binding was undefined everywhere, so an
    // after-the-block assertion on its own would have been green against the bug.
    const FincRun r = compile(
        "struct MyErr {\n"
        "  pub:\n"
        "    code <int>,\n"
        "}\n"
        "fun main() <noret> {\n"
        "  try { } catch (MyErr as e) { let inside <int> = e.code; }\n"
        "  let outside <int> = e.code; }\n");
    EXPECT_EQ(r.exitCode, 1) << "the catch binding must not outlive its block:\n" << r.err;
    EXPECT_EQ(errorCount(stripAnsi(r.err)), 1u)
        << "exactly one: the use after the block. Two means the use inside it failed "
           "too, and the scoping half of this test is vacuous.\n"
        << stripAnsi(r.err);
    EXPECT_NE(messagesOnly(stripAnsi(r.err)).find("Undefined variable 'e'"), std::string::npos)
        << stripAnsi(r.err);
}

TEST(Soundness_TryCatch, AnUnknownCatchTypeIsReportedAsAType) {
    // The direct inversion of the bug. It reported `Undefined type 'err'` -- the name
    // the author chose -- for a clause whose type was perfectly good; it must report
    // the type, and only when the type is the thing that is missing.
    const FincRun r = compile(
        "fun main() <noret> { try { } catch (NoSuchErr as e) { } }\n");
    EXPECT_EQ(r.exitCode, 1) << "an unresolved catch type must be reported:\n" << r.err;
    EXPECT_NE(messagesOnly(stripAnsi(r.err)).find("Undefined type 'NoSuchErr'"), std::string::npos)
        << stripAnsi(r.err);
    EXPECT_EQ(messagesOnly(stripAnsi(r.err)).find("'e'"), std::string::npos)
        << "the binding name must not appear in a diagnostic about the type:\n"
        << stripAnsi(r.err);
}

// ---------------------------------------------------------------------------
// `readonly` is a run-time guarantee, not a compile-time one.
//
// This was booked as a defect -- "readonly is not enforced; a write from outside is
// accepted" -- on the strength of a probe, and the sample it was booked against says
// the opposite. readonly.fin:48-52 is
//
//     try {
//         a.v1 = 5; // this will blame an error
//     } catch (Error as err) {
//         printf("ERROR: cannot change value of readonly members");
//       }
//
// A write to a readonly member from outside the struct, wrapped in a `try` and expected
// to *blame* -- Fin's run-time error mechanism -- and caught as an `Error`. A compiler
// that rejected line 49 statically would make that sample unwritable, and the sample is
// normative. So accepting the write is the ratified behaviour, and the missing half is
// the run-time check, which belongs to codegen (wave 5) along with everything else that
// has to exist at run time.
//
// Kept as a Soundness test rather than struck as a non-defect, because "the compiler
// accepts this" is a thing a future readonly-enforcement change would silently break.
// Whoever adds static enforcement has to come past this test and past readonly.fin.

TEST(Soundness_Readonly, AWriteToAReadonlyMemberFromOutsideIsNotACompileTimeError) {
    // readonly.fin:49 in miniature, minus the try/catch, which is not what makes it
    // legal -- a `try` does not license its contents.
    const FincRun r = compile(
        "struct S {\n"
        "  pub readonly v <int>,\n"
        "}\n"
        "fun main() <noret> { let a <S> = S{v: 10}; a.v = 5; }\n");
    EXPECT_EQ(r.exitCode, 0)
        << "readonly.fin:49 requires this to compile -- the violation there is caught at "
           "run time by `catch (Error as err)`, so static rejection would make a "
           "normative sample unwritable. If enforcement is ruled to be static after all, "
           "that ruling owns this test and readonly.fin:48-52.\n"
        << r.err;
}

TEST(Soundness_Readonly, AReadonlyMemberIsStillReadableAndStillTypeChecked) {
    // The other half, and the reason the test above is not simply "readonly is ignored":
    // the modifier must not cost the member its type or its visibility. readonly.fin:46
    // copies one into an `int` and :56 compares one against an int literal.
    const FincRun r = compile(
        "struct S {\n"
        "  pub readonly v <int>,\n"
        "}\n"
        "fun main() <noret> { let a <S> = S{v: 10}; let bad <string> = a.v; }\n");
    EXPECT_EQ(r.exitCode, 1) << "a readonly member keeps its type:\n" << r.err;
    EXPECT_NE(stripAnsi(r.err).find("expected 'string', got 'int'"), std::string::npos)
        << stripAnsi(r.err);
}
