#pragma once

#include "../ast/Visitor.hpp"
#include "../ast/ASTNode.hpp"
#include "../diagnostics/DiagnosticEngine.hpp"
#include "Scope.hpp"
#include "../types/Type.hpp"
#include "../types/CompilerApiType.hpp"
#include "CompilerApi.hpp"
#include <vector>
#include <string>
#include <fmt/core.h>
#include <fmt/color.h>

namespace fin {

class ModuleLoader; // Forward declaration
// Declared, not included: the two members below name it only through a reference and
// a shared_ptr, and pulling TypeImpl.hpp into this header would put every concrete
// type in front of every translation unit that analyses anything.
class FunctionType;
class StructType; // buildOperatorSignature takes the owner, to look a method up in it
class ArrayType;  // checkIndexInBounds reads its extent

struct AnalysisContext {
    bool inLoop = false;
    std::shared_ptr<Type> currentFuncReturnType = nullptr;
};

class SemanticAnalyzer : public Visitor {
public:
    bool hasError = false;

    SemanticAnalyzer(DiagnosticEngine& diag, bool debug = false);
    ~SemanticAnalyzer();

    void setModuleLoader(ModuleLoader* loader) { this->loader = loader; }
    
    std::shared_ptr<Scope> getGlobalScope() { return globalScope; }

    // --- Visitor Implementation ---
    void visit(Program& node) override;
    void visit(VariableDeclaration& node) override;
    void visit(FunctionDeclaration& node) override;
    void visit(StructDeclaration& node) override;
    void visit(InterfaceDeclaration& node) override;
    void visit(EnumDeclaration& node) override;
    void visit(ImportModule& node) override;
    void visit(DefineDeclaration& node) override;
    void visit(MacroDeclaration& node) override;
    void visit(OperatorDeclaration& node) override;
    void visit(ConstructorDeclaration& node) override;
    void visit(DestructorDeclaration& node) override;
    void visit(TypeDefinition& node) override;
    void visit(SpecialDeclaration& node) override;
    void visit(ClassDeclaration& node) override;
    void visit(ImplementsBlock& node) override;

    void visit(Block& node) override;
    void visit(ReturnStatement& node) override;
    void visit(ExpressionStatement& node) override;
    void visit(IfStatement& node) override;
    void visit(WhileLoop& node) override;
    void visit(ForLoop& node) override;
    void visit(ForeachLoop& node) override;
    void visit(BreakStatement& node) override;
    void visit(ContinueStatement& node) override;
    void visit(DeleteStatement& node) override;
    void visit(TryCatch& node) override;
    void visit(BlameStatement& node) override;

    void visit(BinaryOp& node) override;
    void visit(UnaryOp& node) override;
    void visit(Literal& node) override;
    void visit(PrototypeLiteral& node) override;
    void visit(Identifier& node) override;
    void visit(FunctionCall& node) override;
    void visit(MethodCall& node) override;
    void visit(MacroCall& node) override;
    void visit(MacroInvocation& node) override;
    void visit(CastExpression& node) override;
    void visit(TypeLiteralExpression& node) override;
    void visit(NewExpression& node) override;
    void visit(MemberAccess& node) override;
    void visit(StructInstantiation& node) override;
    void visit(ArrayLiteral& node) override;
    void visit(ArrayAccess& node) override;
    void visit(SizeofExpression& node) override;
    void visit(TernaryOp& node) override;
    void visit(FunctionTypeNode& node) override;
    void visit(LambdaExpression& node) override;
    void visit(QuoteExpression& node) override;
    void visit(TypeNode& node) override;
    void visit(SuperExpression& node) override;
    void visit(PointerTypeNode& node) override;
    void visit(ArrayTypeNode& node) override;
    void visit(StaticMethodCall& node) override;
    void visit(Parameter& node) override;
    void visit(StructMember& node) override;

private:
    DiagnosticEngine& diag;
    bool debugMode;
    ModuleLoader* loader = nullptr; // Reference to loader

    std::vector<std::shared_ptr<Scope>> scopeStack;

    // Shared pointer for memory safety
    std::shared_ptr<Scope> globalScope;
    std::shared_ptr<Scope> currentScope;
    
    AnalysisContext context;
    std::shared_ptr<Type> lastExprType;
    std::shared_ptr<Type> currentStructContext = nullptr; 

    void enterScope();
    void exitScope();
    
    // Resolves a written type, honouring `TypeNode::is_nullable`. Every caller
    // wants that, which is why the flag is read here and not at the twenty
    // grammar sites that set it.
    std::shared_ptr<Type> resolveTypeFromAST(TypeNode* node);

    // The same, but returning the error sentinel instead of nullptr when the
    // written type does not resolve. Use this at every site that *declares*
    // something: a field, a parameter, a return type, a signature.
    //
    // Fifteen such sites each used to gate on the null and drop the declaration
    // -- `if (t) define(...)`, `if (memberType) defineField(...)`,
    // `if (t) paramTypes.push_back(t)` -- which is how one unresolved annotation
    // became one diagnostic per use of the thing it declared. The rule is
    // identical at all of them, so it lives here once.
    //
    // Only for declarations. Expression positions (a cast target, a generic
    // argument) want the null: there is no entity there to keep alive, and the
    // sentinel would only hide the next question.
    std::shared_ptr<Type> resolveTypeOrError(TypeNode* node);

    // Bind one parameter into the scope of the body that will read it.
    //
    // A parameter is a mutable binding unless it was written `const`. That is the
    // same rule `let` and `const` follow for a local, and tests/samples/const.fin is
    // where it is settled: the sample marks parameters `const` in eight places, says
    // on 10 that a constant parameter "cannot be reassigned or changed inside
    // function body", keeps `// a = 10;` commented out on 12 "since it raises an
    // error", and then on 14-16 writes out the copy a caller is forced into --
    // `let scope_a <int> = a; scope_a = 5;`. None of that says anything unless a
    // parameter without the marker is assignable. stdlib/error.fin:13 is the corpus
    // paying for the other reading, `err_code = -1;` inside the constructor that
    // takes `err_code`.
    //
    // The constness is read off the *type* node, because that is where the parser
    // put it (parser.y:1247) and `Parameter` has no mutability field of its own. The
    // by-reference spelling `const &arr: [any]` (stdlib/types.fin:102) sets the flag
    // on the pointer node the parser synthesises, so the outermost node is the right
    // one to ask at all three productions. `Type` carries no constness, only
    // `TypeNode` does, which is why this takes the AST node and not just the
    // resolved type.
    //
    // Nine call sites, one per shape of thing that has parameters -- function,
    // method, operator, constructor, lambda -- and they were nine copies of
    // `define({param->name, t, false, true})`. One of them disagreeing is the bug
    // this replaces.
    void defineParameter(const Parameter& param, const std::shared_ptr<Type>& type) {
        const bool isConst = param.type && param.type->is_const;
        currentScope->define({param.name, type, !isConst, true});
    }

    // ---- The compiler API (docs/compiler-api.md, ADR 0012) --------------------
    //
    // The components the *enclosing declaration* granted with
    // `#[use(compiler.components.<name>)]`. Empty everywhere else, which is what
    // makes a grant per-declaration: nothing about a granted `@special` reaches the
    // one written under it.
    std::vector<std::string> currentGrants;

    // Reads `#[use(...)]` off a declaration's attributes: reports a malformed or
    // misspelled grant, fills `currentGrants`, and defines `compiler` in the current
    // scope when `#[use(compiler)]` is among them. Called from inside the
    // declaration's own scope, so both the name and the grants leave with it.
    //
    // `attrs` is the declaration's attribute vector; there is no common base holding
    // one (parser.y says so at its own attribute helper), so this takes the vector.
    void applyUseAttributes(ASTNode& node,
                            const std::vector<std::unique_ptr<Attribute>>& attrs);

    // Resolves `compiler.<member>` / `compiler.components.<member>` /
    // `compiler.<component>.<member>` for a *read*. `call` is the argument list when
    // the member is being called and null when it is being read, which is the whole
    // of the difference between an operation and a constant at the use site.
    //
    // Returns null having reported, or the member's type. `node` is the MemberAccess
    // or MethodCall being resolved, for the diagnostic's location.
    std::shared_ptr<Type> resolveCompilerApi(
        ASTNode& node, const CompilerApiType& base, const std::string& member,
        std::vector<std::unique_ptr<Expression>>* args,
        std::vector<std::unique_ptr<TypeNode>>* generic_args);

    // One member of the table, as a type. `R` is the turbofish argument.
    std::shared_ptr<Type> compilerApiMemberType(const compilerapi::Member& m,
                                                const std::shared_ptr<Type>& turbofish);

    // A `::`-separated path from an `extern X as Y;` or a `pub implements Y = X;`,
    // resolved as a symbol rather than as a type. Null when the path names no symbol,
    // which is the caller's cue to read it as a type instead. Analyzer_Decl.cpp carries
    // the three path shapes and why the namespace qualifier goes unchecked.
    std::shared_ptr<Type> resolveExternPathAsSymbol(const std::string& path);

    // The body of the above, without the nullable wrap. Split out rather than
    // handled at each `return` because there are eight of them and a new arm
    // silently forgetting the wrap is exactly the bug this feature was.
    std::shared_ptr<Type> resolveTypeUnwrapped(TypeNode* node);

    void error(ASTNode& node, const std::string& msg);

    // Nesting depth of the quiet pre-passes below. `error` returns before it reports
    // and before it sets hasError while this is non-zero.
    int quietDepth = 0;

    // Silences diagnostics for the lifetime of the object.
    //
    // Only legal around a *pre-pass whose every resolution is repeated by a later
    // reporting pass*. That is not a style rule, it is the whole argument: silence is
    // sound here precisely because the diagnostic is not lost, and the two sites that
    // need it -- a struct's and a class's signature registration -- are followed by a
    // body pass that resolves the same TypeNodes and reports. Used anywhere else it
    // deletes diagnostics.
    //
    // The alternative shapes were a resolved-type cache keyed on the TypeNode, and
    // making resolution never report and every caller report instead. Both are bigger
    // and neither is needed while the guarantee holds; Soundness_ErrorRecovery.
    // AnAnnotationInAStructSignatureIsReportedOnce and AMethodParameterAnnotationIs-
    // StillReportedOnce are what hold it. An interface is the counter-example that
    // fixes the boundary: it has no body pass, so its signature pass must report.
    struct QuietPass {
        explicit QuietPass(SemanticAnalyzer& a) : an(a) { ++an.quietDepth; }
        ~QuietPass() { --an.quietDepth; }
        QuietPass(const QuietPass&) = delete;
        QuietPass& operator=(const QuietPass&) = delete;
        SemanticAnalyzer& an;
    };

    // Builds the FunctionType that StructType::methods stores for one method.
    //
    // The receiver is not in it. `struct_methods.fin:10` says the first parameter
    // "will be injected by compiler and it will be the struct itself" whether or not
    // the author wrote `self`, and the same file writes both spellings in one struct
    // (:10 and :14), so a signature that kept a written `self` would make the two
    // spellings call differently. It is stored as it is called.
    //
    // Opens a scope and declares the method's own generics before resolving, because
    // `fun set_x<U>(new_x: U)` (struct_methods.fin:14) is resolved here now and `U` is
    // declared by the method, not by the struct. Without that the parameter resolves to
    // the sentinel, and a sentinel parameter silently switches the argument check off
    // rather than failing loudly.
    // `receiver` is the type the method is being declared on, and is passed only
    // where a method may spell its receiver out under a name of its own -- an
    // implements block on an enum (stdlib/typing.fin:27). Null elsewhere, which
    // leaves the `self`-by-name rule as the only one, as it was everywhere before.
    std::shared_ptr<FunctionType> buildMethodSignature(
        FunctionDeclaration& method,
        const std::shared_ptr<StructType>& receiver = nullptr);
    // The same for an operator, and the reason it is not buildMethodSignature is the
    // `implements cast<fn(Self, T)>(__get)` form (tests/samples/stdlib/hashmap.fin:50),
    // which has no parameter list and no written return type: both come out of the
    // cast, and the return type out of the method the cast names -- which is what
    // `owner` is for. `owner` may be null where there is no type to look a method up
    // in yet.
    std::shared_ptr<FunctionType> buildOperatorSignature(OperatorDeclaration& op,
                                                         const std::shared_ptr<StructType>& owner);

    // The arity-and-argument check shared by every call that has a signature:
    // visit(FunctionCall&), visit(MethodCall&) and visit(StaticMethodCall&).
    //
    // It walks the arguments, so the caller must set `lastExprType` to the call's own
    // type *after* calling it -- which is the bug that made this a shared helper rather
    // than three copies: visit(MethodCall&) set lastExprType to the return type and then
    // walked the arguments, so a call's type was the type of its last argument.
    //
    // `kind` is the word the diagnostic uses for the callee ("Function", "Method",
    // "Static method"), matching what each site's own not-found message already says.
    void checkCallArguments(ASTNode& node, const char* kind, const std::string& name,
                            const FunctionType& sig,
                            std::vector<std::unique_ptr<Expression>>& args);

    // The arity half of checkCallArguments on its own.
    //
    // Split out for the one caller that cannot use the pair together: inferring a
    // generic constructor's arguments means walking them first, and checkCallArguments
    // reports arity *before* it walks. Calling this first keeps that order -- an arity
    // error on `Box(1, 2)` still prints ahead of anything the arguments say.
    void checkCallArity(ASTNode& node, const char* kind, const std::string& name,
                        const FunctionType& sig, size_t actual);

    // The check for a call whose callee still mentions a generic parameter: walks the
    // arguments, checks them against the parameters *as instantiated*, and returns the
    // call's own type. Used in place of checkCallArguments, not alongside it.
    //
    // `owner` is the type the callee was reached through, whose instantiation becomes
    // `Self`; null where the return type carries the whole answer. See the definition for
    // what is read in which order.
    // `seed` is the bindings the call already states outright -- a written turbofish --
    // which outrank both of the sources this reads, because unifyGeneric's first binding
    // wins and these are in the map before it runs.
    std::shared_ptr<Type> checkGenericCall(ASTNode& node, const char* kind,
                                           const std::string& name, FunctionType& sig,
                                           std::vector<std::unique_ptr<Expression>>& args,
                                           const std::shared_ptr<StructType>& owner,
                                           TypeMap seed = {});

    // The type an expression is about to be checked against, and the exact expression
    // node it belongs to.
    //
    // Read by generic inference at a call, which cannot always learn a parameter from
    // the arguments: `Vec2::zero()` (tests/samples/letssee.fin:77) has none,
    // `Vec2::from_angle(0.7854)` (:59) has one that says nothing about T, and
    // `rptr([1,2,3,4])` (const.fin:98) has one whose type is a *fixed* array where the
    // annotation's is not. In all three the annotation on the left is the only thing
    // that says what the generic argument is.
    //
    // Written at three sites, which are the three places the corpus states what a value
    // is about to become:
    //
    //   a declaration's annotation   `let r <Result<int, string>> = Ok(10);`
    //                                (enums.fin:44) -- visit(VariableDeclaration&).
    //   an assignment's target       `r = Err("Blame ME!");` (enums.fin:47) --
    //                                visit(BinaryOp&).
    //   a return's function          `return Err("File don't exists");` inside
    //                                `<IOResult<Stream>>` (stdlib/stdio.fin:154) --
    //                                visit(ReturnStatement&).
    //
    // Nothing distinguishes them once installed: each says "this expression is expected
    // to be that type", which is the whole of what inference needs from them.
    //
    // Keyed on the node so it cannot leak inward: only the expression the annotation
    // actually applies to matches, and every subexpression has a different address. A
    // plain member would seed the inner `Box("x")` of
    // `let b <Box<int>> = unwrap(Box("x"));` from the outer annotation --
    // Soundness_GenericInference.AnAnnotationDoesNotReachASubexpression.
    const ASTNode* typeHintFor = nullptr;
    std::shared_ptr<Type> typeHint = nullptr;

    // The hint if it belongs to this node, else null.
    std::shared_ptr<Type> hintFor(const ASTNode& node) const {
        return typeHintFor == &node ? typeHint : nullptr;
    }
    bool checkType(ASTNode& node, std::shared_ptr<Type> actual, std::shared_ptr<Type> expected);

    // Whether a constant subscript is inside a known extent. Both halves of that are
    // the rule: a run-time index and a dynamic array are both normal, and neither is
    // a thing this can answer. See the definition in Analyzer_Expr.cpp.
    void checkIndexInBounds(const ArrayAccess& node, const ArrayType& arr);

    // checkType, except that `null` is accepted whatever the declared type is.
    // For a declaration's initialiser and a member or parameter default only --
    // see the comment on the definition for why the two cannot share one check.
    bool checkInitializer(ASTNode& node, std::shared_ptr<Type> actual, std::shared_ptr<Type> expected);

    // Whether an integer constant written as `node` may take the type `target`.
    //
    // An integer constant has no type of its own until its context supplies one,
    // which is what makes `let p <ulong> = 0;` and `blame myarr[0] == 0;` legal.
    // It cannot live in PrimitiveType::isAssignableTo because the answer depends
    // on the expression and not only on the two types: `1` and `i` both have type
    // `int`, and only one of them may become a `uint`.
    //
    // Static because visit(BinaryOp&) asks the same question about an operand.
    static bool constantFitsType(const ASTNode& node, const Type& target);
    bool checkConstraint(TypeNode* typeNode, std::shared_ptr<Type> actualType, std::shared_ptr<Type> constraint);

    // Declares `<T>` / `<T: C>` into the current scope and resolves each
    // constraint. Every declaration form that can be generic calls this, which is
    // the point: the six sites had six near-identical loops, three of which
    // resolved the constraint and three of which did not, so
    // `fun f<T: NoSuchType>()` compiled clean while `struct S<T: NoSuchType>` did
    // not. Held by Soundness_GenericBounds in tests/test_soundness.cpp, one
    // test per site.
    //
    // Two passes on purpose: every name is defined before any constraint is
    // resolved, so a constraint may refer to a parameter declared beside it or to
    // the one it constrains -- `<T: Comparable<T>>`, `<T: Castable, U: T>`. The
    // single-pass order the struct site used rejected both.
    //
    // `collect` receives the GenericTypes in declaration order, for the callers
    // that also record them on a StructType as its generic_args.
    void declareGenericParams(const std::vector<std::unique_ptr<GenericParam>>& params,
                              std::vector<std::shared_ptr<Type>>* collect = nullptr);

    // A parameter's default value was the only expression in the language that no
    // pass ever visited, so `fun g(n: int = nosuchvar)` compiled clean. `visit(Parameter&)`
    // does visit it and nothing calls `visit(Parameter&)`: every parameter loop in
    // Analyzer_Decl.cpp walks `param->type` by hand and eight of them can carry a
    // default. Same shape as declareGenericParams above, found the same way -- by
    // asking why a struct member's `= nosuchvar` is reported and a constructor
    // parameter's is not. Held by Soundness_ParameterDefaults, one test per site.
    //
    // Called from the eight *definition* sites, and deliberately not from the two
    // signature-registration passes over the same constructor parameters (struct
    // and class), which would report every diagnostic twice.
    //
    // It visits and does not type-check. Comparing the default against the declared
    // type is blocked on the integer ruling: stdlib/stdio.fin:87 and :109 write
    // `nbytes: ulong = -1`, and `let x <ulong> = -1` is an error today, so the check
    // would put two new diagnostics on a normative sample over a question the owner
    // has not answered. KnownDefect_ParameterDefaults holds that half.
    void visitParameterDefaults(const std::vector<std::unique_ptr<Parameter>>& params);
    bool checkReturnPaths(Statement* node);

    // Defines every top-level function's and `@special`'s name at file scope before
    // visit(Program&) walks the file, so a call may sit above the declaration it
    // names. stdlib/memory.fin:14 is the corpus site -- `@GET_MEMORY_LIMIT()` inside
    // `falloc`, declared on line 40 -- and mutual recursion between two top-level
    // functions is the shape no single in-order walk can satisfy at all.
    //
    // Quiet, and legal to be quiet for the reason QuietPass states: the in-order walk
    // resolves the same TypeNodes and reports on them, so nothing is lost here.
    //
    // A signature that does not fully resolve is skipped rather than registered with
    // the sentinel. The case is a parameter naming a struct declared further down --
    // types are not hoisted -- and the sentinel would let a call above the declaration
    // type-check against a signature nobody wrote. Skipping leaves the honest
    // "Undefined function or type"; KnownDefect_DeclarationOrder holds that half.
    //
    // Top level only. A namespace body is its own scope and gets no pre-pass.
    void hoistTopLevelSignatures(Program& node);
    
    template <typename... Args>
    void debugLog(const fmt::text_style& style, fmt::format_string<Args...> format, Args&&... args) {
        if (debugMode) {
            std::string msg = fmt::format(format, std::forward<Args>(args)...);
            fmt::print(style, "{}", msg);
        }
    }
};

} // namespace fin