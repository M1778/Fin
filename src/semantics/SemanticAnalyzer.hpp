#pragma once

#include "../ast/Visitor.hpp"
#include "../ast/ASTNode.hpp"
#include "../diagnostics/DiagnosticEngine.hpp"
#include "Scope.hpp"
#include "../types/Type.hpp"
#include "../types/CompilerApiType.hpp"
#include "CompilerApi.hpp"
#include "EventFiring.hpp"
#include "EventPayloads.hpp"
#include "EventRegistry.hpp"
#include "LoopBackEdge.hpp"
#include "MovedAnalysis.hpp"
#include <set>
#include <unordered_set>
#include <vector>
#include <string>
#include <unordered_map>
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
class PrototypeType; // checkPrototypeMethod reads its key and value types
class NamespaceType; // lowerModuleCall names the qualifier it resolved through
class SpecialDeclaration; // collectProvider reads the bearer's contract
class MemberAccess; // tryLayoutMember reads the member-access site

namespace comptime {
class Interpreter; // installComptimeHooks installs the fold hooks on one
}

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
    void setExternalGlobalScope(const std::shared_ptr<Scope>& scope);
    // fin-guard, threaded from CompilerOptions by the driver (default on).
    void setFinGuard(bool enabled) { finGuardEnabled = enabled; }
    
    std::shared_ptr<Scope> getGlobalScope() { return globalScope; }

    // Wave-4 step 15 (docs/compiler-api.md §3.4): the `#[on(...)]` subscription
    // table this analyzer collected, and the set its Arm phase armed. A test
    // hook and the firing slice's seam: firing reads the ordered armed handlers
    // from here rather than re-deriving them. Each analyzer collects its own
    // module; cross-module merging with loader-canonical paths is the firing
    // slice's, not this one's.
    const events::EventRegistry& eventRegistry() const { return event_registry_; }
    // Wave-4 step 17 (W6 floor): fire points for function_entry/function_exit,
    // assignment, allocation_site/delete_site (docs/compiler-api.md §3.2).
    // Recorded during analysis with no diagnostic of their own, so a program
    // that fires them compiles exactly as before. W5's firing pass reads the
    // points from here rather than re-deriving where they are; each analyzer
    // records its own module, as with the registry above.
    const std::vector<events::FirePoint>& firePoints() const { return event_fire_points_; }
    void noteFirePoint(events::FirePoint point) {
        // Injected code does not fire events (§3.3): the post-splice check
        // walk visits spliced statements, and a `return` or `delete` in them
        // recording a fire point would arm a second firing off code no handler
        // was ever invoked for.
        if (injectedWalk_) return;
        event_fire_points_.push_back(std::move(point));
    }
    // Wave-4 step 17 (W5 floor): this module's W5 fire points, the pre-pass
    // refused set, and the fired log. variable_declared points accumulate
    // during the walk and fire deferred at the end of visit(Program&)
    // (splicing mid-walk would mutate the vector being walked);
    // struct_layout_finalised fires inline at the end of
    // visit(StructDeclaration&), so it lands before any later body is
    // analysed. Both append to the same log in fire order.
    const std::vector<events::W5FirePoint>& w5FirePoints() const { return w5_points_; }
    const std::vector<events::FiredHandler>& w5Fired() const { return w5_fired_; }
    // Wave-4 step 17 (W7): this module's variable_scope_exit points, the
    // pre-pass refused set, and the fired log. Points accumulate during the
    // walk (one per variable per exit path, carrying the moved state on that
    // path) and fire deferred at the end of visit(Program&), like W5's.
    const std::vector<events::ScopeExitPoint>& w7FirePoints() const { return moved_.points(); }
    const std::vector<events::W7FiredHandler>& w7Fired() const { return w7_fired_; }
    // Wave-4 step 20 (W10): this module's loop_back_edge latch points, the
    // pre-pass refused set, and the fired log. Points accumulate during the
    // walk (one per loop statement, carrying the form and the function-local
    // nesting depth) and fire deferred at the end of visit(Program&), like
    // W5's and W7's.
    const std::vector<events::LoopBackEdgePoint>& w10FirePoints() const { return w10_points_; }
    const std::vector<events::W10FiredHandler>& w10Fired() const { return w10_fired_; }
    // The function whose body is being walked, or "<root>" outside one. Names
    // the site a fire point belongs to. Set and restored by
    // visit(FunctionDeclaration&), which also covers methods; returns in
    // constructors, operators, destructors and lambdas record nothing until
    // those bodies track it too.
    const std::string& currentFunction() const { return current_function_; }
    // Which module this analyzer is analysing, recorded on every handler for
    // the Q10 ordering. The driver and loader do not set it yet (the firing
    // slice wires them); until then every module reports "<root>".
    void setModulePath(const std::string& path) { module_path_ = path; }

    // --- Visitor Implementation ---
    void visit(Program& node) override;
    // Erases every import that bound everything it named. See its definition.
    void dropConsumedImports(Program& node);
    // Refuses every `#[global]` the parser did not stamp as written inside
    // `namespace std` (ADR 0021). See its definition for why it walks attributes
    // rather than declaration shapes.
    void refuseMisplacedGlobals(Program& node);
    // Publishes a declaration the parser stamped `#[global]` into the scope the
    // ModuleLoader owns, so it resolves in files that import nothing (ADR 0021).
    // A no-op when no loader-owned scope was injected. See its definition.
    //
    // True when this call is what published the name -- so the caller can do the other
    // half of `#[global]` for the shapes that need one, without a second reading of the
    // stamp. False for an unmarked declaration, for one with no resolved type, for a
    // refused conflict, and when no ambient scope was injected at all. An identical
    // second declaration of a published name returns true, because it *is* a publish of
    // that name -- the two declarations are one fact and either may be the one a reader
    // finds.
    bool publishIfGlobal(ASTNode& node,
                         const std::vector<std::unique_ptr<Attribute>>& attributes,
                         const std::string& name,
                         const std::shared_ptr<Type>& type);
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
    // Resolves a module-qualified call into a plain call on the name it named, when the
    // root program the backend walks will declare that name for the same declaration.
    // See its definition for the gate and for why the qualifier does not reach codegen.
    void lowerModuleCall(MethodCall& node, const NamespaceType& ns, const Symbol& member);
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
    // Wave 4 slice 1b: attributes are checkable. The override validates one
    // attribute (known name and shape); the declarations walk their vectors to
    // it through validateAttributes below.
    void visit(Attribute& node) override;

private:
    DiagnosticEngine& diag;
    bool debugMode;
    ModuleLoader* loader = nullptr; // Reference to loader
    // fin-guard (default on): bare `let`/`const x = ...` rewrites to `<auto>` with a
    // warning; off rejects it. Set from CompilerOptions by the driver.
    bool finGuardEnabled = true;

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

    // Wave-4 step 15: this module's event subscriptions and armed set, and the
    // module path recorded on them. See eventRegistry()/setModulePath above.
    events::EventRegistry event_registry_;
    std::string module_path_ = "<root>";
    // Wave-4 step 17 (W6 floor): the §3.2 fire points this module recorded,
    // and the function whose body is being walked ("<root>" outside one).
    std::vector<events::FirePoint> event_fire_points_;
    std::string current_function_ = "<root>";
    // Wave-4 step 17 (W5 floor): this module's W5 points, the pre-pass
    // refused set, the fired log, and the program being walked (for the
    // inline struct_layout_finalised fire, which needs the handler bodies).
    std::vector<events::W5FirePoint> w5_points_;
    std::set<std::string> w5_refused_;
    std::vector<events::FiredHandler> w5_fired_;
    Program* w5_program_ = nullptr;
    // Wave-4 step 17 (W7): the moved-state machine (see MovedAnalysis.hpp),
    // this module's scope-exit points, and the pre-pass refused set. The
    // machine rides the main walk through one-liner hooks in the impl files;
    // all state lives in it, so a bug there cannot corrupt a scope.
    events::MovedAnalysis moved_;
    std::set<std::string> w7_refused_;
    std::vector<events::W7FiredHandler> w7_fired_;
    // The empty blocks a folded `@defined`/`@implements` guard left behind
    // when it pruned its untaken arm (Analyzer_Stmt visit(IfStatement&)).
    // checkReturnPaths answers from the taken arm when it meets one; every
    // other walker treats them as the empty blocks they are.
    std::unordered_set<const Block*> prunedArms_;
    // Wave-4 step 20 (W10): this module's loop_back_edge latch points, the
    // pre-pass refused set, the fired log, and the function-local loop
    // nesting depth (1 = outermost). Saved and reset on function and lambda
    // entry, so a loop inside a nested body starts at 1 again.
    std::vector<events::LoopBackEdgePoint> w10_points_;
    std::set<std::string> w10_refused_;
    std::vector<events::W10FiredHandler> w10_fired_;
    int loopDepth_ = 0;
    // Lambda capture analysis (closure slice 1). One frame per lambda whose
    // body is being walked, innermost last. visit(LambdaExpression&) pushes
    // after entering the lambda's scope and pops before leaving it; every
    // Identifier resolved to a function-local binding outside the lambda is
    // recorded on each enclosing frame it is outside of, so a nested lambda's
    // captures propagate to the outer lambda whose env provides them at
    // runtime. Codegen builds one env struct per lambda from `captures`.
    struct LambdaCaptureFrame {
        LambdaExpression* node = nullptr;
        Scope* lambdaScope = nullptr;
    };
    std::vector<LambdaCaptureFrame> lambdaCaptures_;
    // The scopes opened for struct/class bodies and implements blocks, innermost
    // last, each with the type whose methods it holds. A bare call naming one of
    // the struct's own methods resolves through this scope (visit(Function-
    // Declaration&) registers the method there), which shadows a free function
    // of the same name. visit(FunctionCall&) reads the stack to tell that
    // binding apart from a free one with no change to lookup order: pushed where
    // currentStructContext is set for bodies, popped with the scope.
    struct StructBodyScope {
        std::shared_ptr<Type> structType;
        Scope* scope = nullptr;
        // Whether each method the body declares is static, read off the
        // declaration. The scope registration cannot say: an instance method
        // without a written `self` registers the same bare parameter list a
        // static one does, and whether `self` is in scope says what the
        // *caller* is, not the callee.
        std::unordered_map<std::string, bool> methodStatic;
    };
    std::vector<StructBodyScope> structScopes_;
    // Records a use of `name` (read or write: both need the env field) on
    // every enclosing lambda it is a capture for. No-op outside any lambda.
    void noteLambdaUse(const std::string& name);
    // Wave-4 step 19: the scope chain live at each variable_declared anchor,
    // keyed by the anchored declaration. The post-splice check walks spliced
    // statements inside a child of that scope, so injected code resolves the
    // locals it was injected beside. The shared_ptrs keep the whole chain
    // alive past the walk that built it (parents are raw pointers).
    std::unordered_map<const Statement*, std::vector<std::shared_ptr<Scope>>> w5_scopes_;
    // Wave-4 step 19: attribution active while the post-splice check walks
    // injected code. Every diagnostic reported on a spliced node names the
    // handler that wrote it and the event point that fired it (the
    // Rust-derive lesson: an error in code the user never wrote must say
    // whose handler wrote it). Empty outside that walk.
    struct ActiveAttribution {
        std::string handler;
        std::string event;
        std::string detail;
        int line = -1;
        bool active = false;
    };
    ActiveAttribution activeAttr_;
    // Wave-4 step 19: inside a handler for a known event (`#[on(...)]`), a
    // `quote` is data, not code: its block is not analysed as live code at
    // the declaration. The quote is checked once, after splicing, attributed
    // to the handler at its event point — which is also why an unarmed
    // handler's bad quote compiles clean. Outside handlers nothing changes.
    bool inHandler_ = false;
    // Wave-4 step 19: inside the post-splice check walk. Fire-point
    // recording is off (see noteFirePoint); the variable_declared recorder
    // below reads this too.
    bool injectedWalk_ = false;
    // Checks spliced handler quotes after firing: walks each chunk's
    // statements in a child of the anchor's scope with that chunk's
    // attribution active, so a diagnostic in generated code names its
    // handler and its event point. Analysis-time by design; codegen is
    // untouched because a quote that does not check never reaches it.
    void checkInjectedChunks(const std::vector<events::InjectedChunk>& chunks);

    // Reads `#[use(...)]` off a declaration's attributes: reports a malformed or
    // misspelled grant, fills `currentGrants`, and defines `compiler` in the current
    // scope when `#[use(compiler)]` is among them. Called from inside the
    // declaration's own scope, so both the name and the grants leave with it.
    //
    // `attrs` is the declaration's attribute vector; there is no common base holding
    // one (parser.y says so at its own attribute helper), so this takes the vector.
    void applyUseAttributes(ASTNode& node,
                            const std::vector<std::unique_ptr<Attribute>>& attrs);

    // Walks a declaration's attributes through accept, so visit(Attribute&)
    // validates each. `withUse` is false where applyUseAttributes already
    // checked `#[use(...)]` (function, @special): it binds there, and walking
    // it again would report the same grant twice. `isSpecialBearer` is true on
    // a `@special` only: `#[provides(...)]` is collected whole by
    // collectProvider there, `#[protocol(...)]` by collectProtocol there, and
    // both are refused on every other bearer here.
    void validateAttributes(const std::vector<std::unique_ptr<Attribute>>& attrs,
                            bool withUse = true, bool isSpecialBearer = false);

    // The operations-layer gate (ADR 0012): reaching *through* a component
    // needs its grant. Shared by the component-call path and the hybrid
    // member-read path so the two cannot disagree about what "granted" means.
    bool hasComponentGrant(const std::string& component) const;
    bool requireComponentGrant(ASTNode& node, const std::string& component);

    // The provider mechanism (§3.9, ADR 0014): one claim per slot per program.
    // Collected from `#[provides(<slot>)]` on `@special` declarations; a
    // second claimant for a slot is a diagnostic naming both.
    struct ProviderClaim {
        std::string slot;
        std::string name;
    };
    std::vector<ProviderClaim> providerClaims_;
    void collectProvider(SpecialDeclaration& node);

    // The protocol mechanism (§3.7, ADR 0014, wave-5 slice 0): one claim per
    // slot per program, mirroring the provider registry clause for clause.
    // Collected from `#[protocol(<slot>)]` on `@special` declarations, armed
    // by default (collecting IS the claim -- no per-site opt-in, exactly as
    // providers need none). A second claimant for a slot is a diagnostic
    // naming both sites; a lone well-formed claimant is recorded and refused
    // by reportSingleProtocolClaims below (replacement is not lowered yet).
    struct ProtocolClaim {
        std::string slot;
        std::string name;
        int line = 0;
        // The declaration, for the deferred not-lowered refusal's location.
        // Non-owning and valid: claims are collected during visit(Program&)'s
        // walk and reported before it returns, so the tree outlives every use.
        SpecialDeclaration* node = nullptr;
    };
    std::vector<ProtocolClaim> protocolClaims_;
    void collectProtocol(SpecialDeclaration& node);
    // Refuses every slot with exactly one well-formed claimant except the
    // ones whose slice landed (`move_or_copy` in slice 1, `destructor` in
    // slice 2, `deallocate` in slice 3): the slot is recognized and the claimant recorded, but
    // replacement is not lowered yet. Runs at the end of visit(Program&) so
    // a second claimant still reports exclusivity instead of this. Slots
    // with zero claimants report nothing: the default lowering stays
    // byte-identical.
    void reportSingleProtocolClaims();

    // Wave-4 Round 3, Q11: warns when a `@special` body branches on a value
    // read from the host (`compiler.system.get_*_memory`, `get_memorycard_model`).
    // Reading is legal; branching makes the program depend on the build machine.
    // Direct reads and same-body `let`s warn syntactically; taint through
    // `@special` calls warns via the interpreter's value model (ADR 0017).
    // `hasSystemGrant` gates the walk so an ungranted read (already an
    // error) is not also a warning on the same line.
    void warnOnHostBranch(SpecialDeclaration& node, bool hasSystemGrant);

    // Issue #41: warns on a bare `=` directly under an if/while/for/ternary
    // condition (`if (x = 2)`), naming the `=`/`==` suspicion. Warning-level
    // only, never failing the build (the warnOnHostBranch precedent). A
    // parenthesised assignment is the intentional form and stays silent, as
    // does an assignment nested under any other operator (e.g. `(x = 2) == 2`).
    void warnOnAssignmentInCondition(Expression& cond);

    // Issue #46: warns on never-read `let`/`const` locals bound to pure
    // values. One frame per open scope, pushed and popped with it, so
    // shadowing resolves the way Scope::resolve does (nearest frame holding
    // the name). Only visit(VariableDeclaration&) records -- parameters,
    // `self`, catch and foreach bindings never warn, and neither does a
    // binding whose initializer may run code (a call, method, `new`, struct
    // construction or macro: the ignored-result idiom, whose removal would
    // delete the effects) -- and only visit(Identifier&) (plus the
    // closure-variable call path, which never walks an Identifier) marks.
    // Warning-level only, never failing the build (the warnOnHostBranch
    // precedent); a `_`-prefixed name is the intentional form and stays
    // silent, as the corpus already uses it (stdlib/collection.fin:30).
    struct UnusedLocalFrame {
        // Non-owning: the tree outlives the walk, and a recorded declaration
        // is warned at its scope's exit, before anything could drop it.
        std::vector<VariableDeclaration*> decls;
        std::unordered_set<std::string> used;
    };
    std::vector<UnusedLocalFrame> unusedStack_;
    void recordLocalBinding(VariableDeclaration& node);
    void markLocalUsed(const std::string& name);

    // The hybrid layout-member rule: `t.size` on a `$type`/`$struct` value reads
    // through the `layout` component and needs its grant, exactly as the
    // `compiler.layout.size_of(t)` call does. True when the member was a layout
    // member (handled, `lastExprType` set); false when this is not a layout
    // read at all and the caller falls through to the old diagnostics.
    bool tryLayoutMember(MemberAccess& node, const std::shared_ptr<Type>& objType);

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

    // Wave-4 `@implements` lowering, stage A: installs the comptime fold
    // hooks on an interpreter. One hook behind both spellings --
    // `@implements(S, I)` and `compiler.types.implements(S, I)` -- the
    // `symbols.defined` precedent (one predicate behind `@defined` and
    // `compiler.symbols.defined`): the hook answers from these scopes
    // through `StructType::implements`, so a guard folds the same pair the
    // query later lowers. Anything but a concrete struct/interface pair --
    // a `$struct`/`$interface` parameter, an anonymous literal, `any`,
    // nullable, union, enum-as-struct, an open generic -- answers nullopt
    // and stays a gap, never a guessed false.
    void installComptimeHooks(comptime::Interpreter& interp);

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

    // The same, with an explanation the compiler is sure of. It occupies the `= help:`    // row and so displaces the typo heuristic, which is the point: a rule the compiler
    // can state outright is worth more than a guess at what the programmer meant. Added
    // for the builtin-macro table (ADR 0023 step 6), where the useful half of the
    // diagnostic is the list of macros the compiler does implement.
    void error(ASTNode& node, const std::string& msg, const std::string& help);

    // A handler's `compiler.diag.warning` / `compiler.diag.note`, and any
    // warning or note met while checking injected code. Reported, never
    // failing: neither sets hasError, so neither can turn a clean build into
    // a failing one. Both carry the active attribution, if any, exactly as
    // error does.
    void warning(ASTNode& node, const std::string& msg);
    void note(ASTNode& node, const std::string& msg);

    // Step 19 helpers: the active attribution as message suffix and as engine
    // value. The suffix names the handler, the event, the point's human half
    // and the event point's line; the struct carries handler and event for
    // the JSON path (which already renders it). Both are empty when no check
    // walk is active, so ordinary diagnostics read exactly as before.
    std::string attributedMessage(const std::string& msg) const;
    DiagnosticAttribution engineAttribution() const;

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
    // `ownerInstanceOut`, when given, receives what `owner` was instantiated to -- and
    // null where `owner` was already concrete or nothing bound its parameters. The
    // return value is the call's *result*, which for `Vec2::normalize(scaled)` is
    // `noret` and says nothing about which Vec2 was called; recordResolvedTarget needs
    // the receiver, so this is the one thing about the instantiation the caller cannot
    // reconstruct from what it already has.
    std::shared_ptr<Type> checkGenericCall(ASTNode& node, const char* kind,
                                           const std::string& name, FunctionType& sig,
                                           std::vector<std::unique_ptr<Expression>>& args,
                                           const std::shared_ptr<StructType>& owner,
                                           TypeMap seed = {},
                                           std::shared_ptr<Type>* ownerInstanceOut = nullptr);

    // A free function's generic parameters in declaration order (`fun id<T>` ->
    // `["T"]`), so a written turbofish binds positionally. A FunctionType carries
    // parameter types and no parameter names, which is why the call site cannot
    // pair `id::<string>` without this. Recorded where the function is declared
    // (hoist + in-order walk); read where it is called.
    std::unordered_map<std::string, std::vector<std::string>> functionGenericOrder_;
    // A method's own generic parameters in declaration order, keyed
    // `Struct::method`. The struct's parameters are already substituted into the
    // method's signature through the receiver's instantiation, so only the
    // method's own remain to bind -- from the call's turbofish, the hint and the
    // arguments. Recorded beside every defineMethod; read in visit(MethodCall&).
    // Walks parents on lookup, because an inherited method keeps the declaration
    // it was written with.
    std::unordered_map<std::string, std::vector<std::string>> methodGenericOrder_;
    void recordFunctionGenerics(const std::string& name,
                                const std::vector<std::unique_ptr<GenericParam>>& params);
    void recordMethodGenerics(const std::string& structName, const std::string& methodName,
                              const std::vector<std::unique_ptr<GenericParam>>& params);
    const std::vector<std::string>* lookupFunctionGenerics(const std::string& name) const;
    const std::vector<std::string>* lookupMethodGenerics(
        const std::shared_ptr<StructType>& structType, const std::string& methodName) const;

    // Which loaded module an imported bare name came from, by index into
    // ModuleLoader::modulePrograms() (the backend's `modules_` in the same
    // order). A named import overwrites and a star import binds only what the
    // scope lacks -- exactly what visit(ImportModule&) does to the scope, so
    // this always agrees with it -- and a file-scope `fun` of the same name
    // erases, so a call that resolves to the file's own declaration records
    // nothing. Read in visit(FunctionCall&) to stamp the resolved module on
    // the call for the backend.
    std::unordered_map<std::string, size_t> importedOwner_;

    // Records on a `::` call which instantiation of a generic target it resolved to,
    // for the backend to map instead of the bare template (HANDOFF section 6, item 6).
    // Records nothing where that instantiation has no node to spell it -- see
    // StaticMethodCall::resolved_target and spellType.
    void recordResolvedTarget(StaticMethodCall& node, const std::shared_ptr<Type>& instance);

    // Records on a constructor call the type arguments inference found, for the
    // backend to instantiate where the call wrote no turbofish (`Box(5)` for
    // `let b <Box<int>> = Box(5)`). The constructor-call half of
    // recordResolvedTarget's rule: the front end infers (annotation first, then
    // arguments -- b690f60), the backend lowers what was recorded. Records
    // nothing where an argument has no node to spell it, and nothing where a
    // parameter is still standing unresolvable here -- see FunctionCall::
    // resolved_args and everyGenericParamResolvesHere. Either way the failure
    // is the old refusal, never a wrong instantiation.
    void recordResolvedArgs(FunctionCall& node, const std::shared_ptr<Type>& inferred);

    // The struct a `struct { ... }` literal declares, keyed by the literal node.
    // A literal's *type* is the `$struct` meta-type (Soundness_TypeLiterals.
    // AStructLiteralIsTypedDollarStructAndNotSomethingElse pins the spelling),
    // but its *value* denotes the anonymous struct -- which is what a `$struct`
    // parameter receives and what a Struct-bounded generic return is seeded
    // from (checkGenericCall). Recorded in visit(TypeLiteralExpression&) before
    // the literal's scope is discarded; read only through that seeding, so an
    // interface literal (recorded in interfaceLiteralTypes_ below) and an
    // ambiguous call (read never) keep today's behaviour exactly.
    std::unordered_map<const ASTNode*, std::shared_ptr<StructType>> structLiteralTypes_;
    // The interface an `interface { ... }` literal declares, keyed by the
    // literal node. The mirror of structLiteralTypes_ above, recorded by the
    // same visit: an anonymous interface's *value* is a `$interface` tid word
    // (ruling 5), and what it denotes is what the Stage-C runtime chain gates
    // on -- which needs the full InterfaceInfo, so the literal's semantic
    // type is kept here rather than re-derived. A struct literal never lands
    // here and an interface literal never lands above.
    std::unordered_map<const ASTNode*, std::shared_ptr<StructType>> interfaceLiteralTypes_;

    // Records on a struct literal the type arguments inference found, for the
    // backend to instantiate where the literal wrote none (`Box{ val: 7 }`
    // under `Box<int>`, `wptr{...}` under `wptr<T>`). The literal half of the
    // same rule: a written turbofish needs no record, and what is recorded is
    // spelled out of the instantiation the literal's own inference computed --
    // hint first, then fields -- with the same spelling guards. See
    // StructInstantiation::resolved_args.
    void recordLiteralArgs(StructInstantiation& node, const std::shared_ptr<Type>& inferred);

    // Records on an array literal the type inference found (`[int, 4]` for
    // `[1, 2, 3, 4]`), for the backend to build where no declaration set a
    // hint. The array half of the same rule: what is recorded is the type the
    // elements were checked against, spelled back out, with the same spelling
    // guards -- so the backend builds what the front end typed rather than
    // guessing from an element. See ArrayLiteral::resolved_type.
    void recordLiteralType(ArrayLiteral& node, const std::shared_ptr<Type>& inferred);

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

    // A subscript may be any integer, on the same rule and from the same table as an
    // allocation's extent. Returns true when the index is acceptable -- or already
    // failed to type -- which is also the caller's signal to run the bounds check.
    bool checkIntegerIndex(ASTNode& node, const std::shared_ptr<Type>& idxType);

    // The fixed set of methods a prototype has, ADR 0028's initial API. Reports and
    // types the call; never falls through to the struct path, because a prototype is
    // not struct-shaped and `getStructType` on one comes back empty. See the definition
    // for which names are recognised and why `rm` is among them.
    void checkPrototypeMethod(MethodCall& node, const PrototypeType& proto);

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
    // It visits *and* type-checks, the second half having landed after the first. The
    // check is checkInitializer, so a default follows the same rule as every other
    // initialiser -- `= null` is permitted whatever the declared type is, widening
    // reaches it, and a negative constant is not an unsigned value. What that costs is
    // two diagnostics on tests/samples/stdlib/stdio.fin, whose :87 and :109 write
    // `nbytes: ulong = -1`; the argument for paying it is that :110's `nbytes == -1` has
    // been refused in that same file since ADR 0022, so the alternative was a compiler
    // that disagreed with itself about one line. Soundness_ParameterDefaults and
    // Soundness_DefaultArguments hold both halves.
    //
    // Whether a defaulted parameter may be *omitted* at a call is a separate defect and
    // still open: the arity check reads a FunctionType, which records no defaults.
    // KnownDefect_ParameterDefaults.ADefaultedParameterIsStillRequired holds it.
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

    // Tracks top-level struct/class/interface/enum declarations hoisted during the pre-pass
    // so that subsequent in-order visits reuse the exact type instance.
    std::unordered_map<const ASTNode*, std::shared_ptr<StructType>> hoistedTypes_;
    
    template <typename... Args>
    void debugLog(const fmt::text_style& style, fmt::format_string<Args...> format, Args&&... args) {
        if (debugMode) {
            std::string msg = fmt::format(format, std::forward<Args>(args)...);
            fmt::print(style, "{}", msg);
        }
    }
};

} // namespace fin