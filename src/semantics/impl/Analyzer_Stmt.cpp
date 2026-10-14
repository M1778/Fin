#include "../SemanticAnalyzer.hpp"
#include "../ComptimeInterp.hpp"
#include "../EventPayloads.hpp"
#include "../../ast/decls/Program.hpp"
#include "../../ast/exprs/BinaryOp.hpp"
#include "../../ast/exprs/ArrayExpr.hpp"
#include "../../ast/exprs/FunctionCall.hpp"
#include "../../ast/exprs/Identifier.hpp"
#include "../../ast/exprs/MiscExpr.hpp"
#include "../../ast/exprs/StructureExpr.hpp"
#include "../../ast/exprs/UnaryOp.hpp"
#include "../../ast/exprs/Lambda.hpp"
#include "../../ast/stmts/ControlFlow.hpp"
#include "../../ast/stmts/VariableDecl.hpp"
#include "../../types/TypeImpl.hpp"
#include <optional>
#include <unordered_set>
#include <vector>
namespace fin {

namespace {
// What a returned expression addresses: the variable whose storage it names,
// and whether it names that variable's own slot rather than storage reached
// from it. `d` in `return &d` and `return d` is the slot itself; `d` in
// `&d.n` and `a` in `&a[0]` are storage inside one. A call, `new` or a
// literal names no variable, so there is no lifetime to ask about.
struct ReturnedStorage {
    const Identifier* root = nullptr;
    bool wholeSlot = false;
};

ReturnedStorage returnedStorage(const Expression* e) {
    if (!e) return {};
    if (const auto* id = dynamic_cast<const Identifier*>(e)) return {id, true};
    if (const auto* mem = dynamic_cast<const MemberAccess*>(e))
        return {returnedStorage(mem->object.get()).root, false};
    if (const auto* idx = dynamic_cast<const ArrayAccess*>(e))
        return {returnedStorage(idx->array.get()).root, false};
    return {};
}

// #105 (PROVENANCE-LITE): what a pointer local was initialised from. `let p
// <&D> = &d` hands `p` the frame slot of `d`, so `return p` and `return &p.n`
// escape `d` through a name whose declared type reads as foreign storage.
// One entry per pointer-local declaration: the referent symbol, or null where
// the initializer names foreign or unknown storage (a parameter, a global, a
// call, `new`) -- which keeps today's silent accept for exactly those. A null
// entry is still an entry: it stops the lookup, so an inner `let p`
// shadowing an outer tracked one does not inherit the outer's referent.
//
// One frame per open Block, pushed and popped by visit(Block&), so entries
// die with the scope that owns the binding and sibling blocks never see each
// other. File-static because the owning slice is this file alone; every push
// is popped on the same fall-through path, so no frame outlives its walk.
using PtrProvFrame = std::unordered_map<std::string, const Symbol*>;
std::vector<PtrProvFrame> ptrProvFrames_;

bool lookupPtrProvenance(const std::string& name, const Symbol*& out) {
    for (auto it = ptrProvFrames_.rbegin(); it != ptrProvFrames_.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) {
            out = f->second;
            return true;
        }
    }
    out = nullptr;
    return false;
}

// Whether a referent outlives any call: a global, `$Fin`-pinned storage, or
// the receiver. Anything failing these is this frame's, however many pointer
// hops intervene -- and null (unknown) reads as foreign, which keeps today's
// silent accept for the shapes this slice does not track.
bool ptrProvForeign(const Symbol* ref, const std::shared_ptr<Scope>& global,
                    const std::shared_ptr<Type>& structCtx) {
    if (!ref || !global) return true;
    if (ref == global->resolve(ref->name)) return true;
    if (ref->slaveof == "$Fin") return true;
    if (ref->name == "self" && structCtx) return true;
    return false;
}

// Follows one initializer to the frame slot it names, or null for foreign or
// unknown storage. `&e` reads the root of `e`; a bare Identifier / member /
// index chain is a pointer copy and reads through an existing entry, so
// `let q = p` inherits what `p` was recorded with -- null included, because
// foreign stays foreign through a copy. Anything else (a call, `new`, a
// literal) is storage this rule already accepts, hence null.
const Symbol* ptrProvReferent(const Expression* init, const std::shared_ptr<Scope>& scope,
                              const std::shared_ptr<Scope>& global,
                              const std::shared_ptr<Type>& structCtx) {
    if (!init || !scope) return nullptr;
    const Identifier* root = nullptr;
    if (const auto* u = dynamic_cast<const UnaryOp*>(init)) {
        if (u->op != ASTTokenKind::AMPERSAND || !u->operand) return nullptr;
        root = returnedStorage(u->operand.get()).root;
    } else {
        root = returnedStorage(init).root;
    }
    if (!root) return nullptr;
    Symbol* rsym = scope->resolve(root->name);
    if (!rsym) return nullptr;
    const Symbol* known = nullptr;
    if (lookupPtrProvenance(rsym->name, known)) return known;
    if (ptrProvForeign(rsym, global, structCtx)) return nullptr;
    // A pointer with no entry of its own points at storage someone else owns:
    // a parameter is never declared and so never recorded, and recording it
    // here would move the bug onto the accepted parameter case (#105
    // non-goal; AcceptsReturningTheAddressOfAParameterField).
    if (rsym->type && rsym->type->as<PointerType>()) return nullptr;
    return rsym;
}

// Records one statement's effect on the tracking: a pointer-local
// declaration writes its referent (or a null tombstone for foreign storage,
// which still shadows an outer entry), and a straight-line `p = ...`
// re-derives the innermost entry for the name. CEILING (#105): an assignment
// inside one arm of a conditional leaves the join holding whichever entry
// ran last, so branch-conditional reassignment defeats this tracking; that
// needs real alias analysis, an owner-level call, not this slice.
void notePtrProvenance(Statement* stmt, const std::shared_ptr<Scope>& scope,
                       const std::shared_ptr<Scope>& global,
                       const std::shared_ptr<Type>& structCtx) {
    if (!stmt || !scope || ptrProvFrames_.empty()) return;
    const auto* vd = dynamic_cast<const VariableDeclaration*>(stmt);
    const Expression* init = nullptr;
    const std::string* name = nullptr;
    bool isDecl = false;
    if (vd) {
        isDecl = true;
        name = &vd->name;
        init = vd->initializer ? vd->initializer.get() : nullptr;
    } else {
        const auto* es = dynamic_cast<const ExpressionStatement*>(stmt);
        const auto* b = (es && es->expr) ? dynamic_cast<const BinaryOp*>(es->expr.get()) : nullptr;
        if (!b || b->op != ASTTokenKind::EQUAL || !b->left || !b->right) return;
        const auto* id = dynamic_cast<const Identifier*>(b->left.get());
        if (!id) return;
        name = &id->name;
        init = b->right.get();
    }
    Symbol* sym = scope->resolve(*name);
    // Only pointer bindings are tracked; a bare `return p` for a value local
    // is a copy, not an escape, so a non-pointer shadowing cannot reach the
    // check either way.
    if (!sym || !sym->type || !sym->type->as<PointerType>()) return;
    const Symbol* ref = ptrProvReferent(init, scope, global, structCtx);
    if (isDecl) {
        ptrProvFrames_.back()[*name] = ref;
        return;
    }
    for (auto it = ptrProvFrames_.rbegin(); it != ptrProvFrames_.rend(); ++it) {
        auto f = it->find(*name);
        if (f != it->end()) {
            f->second = ref;
            return;
        }
    }
}

// The frame-local slot a returned expression smuggles out through a pointer
// local, or null. Two shapes `held` never sees: a bare `return p`, which
// takes no address, and a returned closure, whose env copies the capture --
// a dead address survives in the lambda, because captures copy.
const Symbol* launderedReferent(const Expression* value, const std::shared_ptr<Scope>& scope,
                                const std::shared_ptr<Scope>& global,
                                const std::shared_ptr<Type>& structCtx) {
    if (!value || !scope) return nullptr;
    if (const auto* lam = dynamic_cast<const LambdaExpression*>(value)) {
        for (const auto& c : lam->captures) {
            const Symbol* ref = nullptr;
            if (lookupPtrProvenance(c.name, ref) && ref &&
                !ptrProvForeign(ref, global, structCtx))
                return ref;
        }
        return nullptr;
    }
    ReturnedStorage bare = returnedStorage(value);
    if (!bare.root) return nullptr;
    if (!scope->resolve(bare.root->name)) return nullptr;
    const Symbol* ref = nullptr;
    if (lookupPtrProvenance(bare.root->name, ref) && ref &&
        !ptrProvForeign(ref, global, structCtx))
        return ref;
    return nullptr;
}
// B4: whether the guard mentions `@defined` (or `compiler.symbols.defined`),
// the only shape this slice folds. Anything else keeps the both-arms walk.
bool mentionsDefined(const Expression* e) {
    if (!e) return false;
    if (const auto* call = dynamic_cast<const FunctionCall*>(e)) {
        if (call->is_special && call->name == "defined") return true;
        for (const auto& a : call->args) {
            if (a && mentionsDefined(a.get())) return true;
        }
        return false;
    }
    if (const auto* m = dynamic_cast<const MethodCall*>(e)) {
        if (m->method_name == "defined") return true;
        if (m->object && mentionsDefined(m->object.get())) return true;
        for (const auto& a : m->args) {
            if (a && mentionsDefined(a.get())) return true;
        }
        return false;
    }
    if (const auto* u = dynamic_cast<const UnaryOp*>(e)) {
        return u->operand && mentionsDefined(u->operand.get());
    }
    if (const auto* b = dynamic_cast<const BinaryOp*>(e)) {
        return (b->left && mentionsDefined(b->left.get())) ||
               (b->right && mentionsDefined(b->right.get()));
    }
    if (const auto* t = dynamic_cast<const TernaryOp*>(e)) {
        return (t->condition && mentionsDefined(t->condition.get())) ||
               (t->true_expr && mentionsDefined(t->true_expr.get())) ||
               (t->false_expr && mentionsDefined(t->false_expr.get()));
    }
    if (const auto* m = dynamic_cast<const MatchExpr*>(e)) {
        if (m->scrutinee && mentionsDefined(m->scrutinee.get())) return true;
        for (const auto& arm : m->arms) {
            if (arm.body && mentionsDefined(arm.body.get())) return true;
        }
        return false;
    }
    if (const auto* mem = dynamic_cast<const MemberAccess*>(e)) {
        return mem->object && mentionsDefined(mem->object.get());
    }
    return false;
}

// Wave-4 `@implements` lowering, stage A: whether the guard mentions the
// query in either spelling -- `@implements(S, I)` or
// `compiler.types.implements(S, I)`. A `MethodCall` named `implements` on
// any other receiver counts too, exactly as `mentionsDefined` counts any
// method named `defined`: a false positive only costs the optimization,
// never correctness, because what does not fold keeps the both-arms walk.
bool mentionsImplements(const Expression* e) {
    if (!e) return false;
    if (const auto* call = dynamic_cast<const FunctionCall*>(e)) {
        if (call->is_special && call->name == "implements") return true;
        for (const auto& a : call->args) {
            if (a && mentionsImplements(a.get())) return true;
        }
        return false;
    }
    if (const auto* m = dynamic_cast<const MethodCall*>(e)) {
        if (m->method_name == "implements") return true;
        if (m->object && mentionsImplements(m->object.get())) return true;
        for (const auto& a : m->args) {
            if (a && mentionsImplements(a.get())) return true;
        }
        return false;
    }
    if (const auto* u = dynamic_cast<const UnaryOp*>(e)) {
        return u->operand && mentionsImplements(u->operand.get());
    }
    if (const auto* b = dynamic_cast<const BinaryOp*>(e)) {
        return (b->left && mentionsImplements(b->left.get())) ||
               (b->right && mentionsImplements(b->right.get()));
    }
    if (const auto* t = dynamic_cast<const TernaryOp*>(e)) {
        return (t->condition && mentionsImplements(t->condition.get())) ||
                (t->true_expr && mentionsImplements(t->true_expr.get())) ||
                (t->false_expr && mentionsImplements(t->false_expr.get()));
    }
    if (const auto* m = dynamic_cast<const MatchExpr*>(e)) {
        if (m->scrutinee && mentionsImplements(m->scrutinee.get())) return true;
        for (const auto& arm : m->arms) {
            if (arm.body && mentionsImplements(arm.body.get())) return true;
        }
        return false;
    }
    if (const auto* mem = dynamic_cast<const MemberAccess*>(e)) {
        return mem->object && mentionsImplements(mem->object.get());
    }
    return false;
}

// A `getkeyid(v) == keyidof(M)` guard, in either operand order: the variable
// whose payloads narrow, and the member they narrow to. `==` only -- `!=`
// narrows the else arm, which is a second ruling and not this one. Bare
// identifiers only: a member read through anything else is not a name this
// can key on, and a qualified `R::M` is not admittable as a `$enum_member`
// argument (Soundness_EnumMemberValue), so there is nothing to narrow to.
std::optional<std::pair<std::string, std::string>> matchKeyidGuard(const Expression* cond) {
    const auto* cmp = dynamic_cast<const BinaryOp*>(cond);
    if (!cmp || cmp->op != ASTTokenKind::EQEQ) return std::nullopt;
    const Expression* sides[2][2] = {{cmp->left.get(), cmp->right.get()},
                                     {cmp->right.get(), cmp->left.get()}};
    for (const auto& side : sides) {
        const auto* get = dynamic_cast<const FunctionCall*>(side[0]);
        const auto* key = dynamic_cast<const FunctionCall*>(side[1]);
        if (!get || get->name != "getkeyid" || get->args.size() != 1) continue;
        if (!key || key->name != "keyidof" || key->args.size() != 1) continue;
        const auto* var = dynamic_cast<const Identifier*>(get->args[0].get());
        const auto* member = dynamic_cast<const Identifier*>(key->args[0].get());
        if (!var || !member) continue;
        return std::make_pair(var->name, member->name);
    }
    return std::nullopt;
}

// Holds one narrowing for exactly one taken-arm walk. Balanced by
// construction: every then-block walk below goes through one of these.
class ScopedKeyidNarrowing {
public:
    ScopedKeyidNarrowing(SemanticAnalyzer& analyzer,
                         const std::optional<std::pair<std::string, std::string>>& guard)
        : analyzer_(analyzer), active_(guard.has_value()) {
        if (active_) analyzer_.pushKeyidNarrowing(guard->first, guard->second);
    }
    ~ScopedKeyidNarrowing() {
        if (active_) analyzer_.popKeyidNarrowing();
    }

private:
    SemanticAnalyzer& analyzer_;
    bool active_;
};

// Whether a raised struct counts as error-like: the `Error` it names, inherits,
// or implements (ADR 0053). `parents` holds base structs and implemented
// interfaces alike, so one transitive walk covers both halves of the ruling;
// matched by name as well as identity because the parent entry and the scope's
// `Error` can be distinct objects for the same declaration across modules.
bool isErrorRelated(const StructType* raised, const StructType* error) {
    if (!raised || !error) return false;
    std::vector<const StructType*> stack{raised};
    std::unordered_set<const StructType*> seen;
    while (!stack.empty()) {
        const StructType* cur = stack.back();
        stack.pop_back();
        if (!seen.insert(cur).second) continue;
        if (cur == error || cur->name == error->name) return true;
        for (const auto& p : cur->parents) {
            if (p) {
                if (auto* ps = p->as<StructType>()) stack.push_back(ps);
            }
        }
    }
    return false;
}
}  // namespace

void SemanticAnalyzer::pushKeyidNarrowing(std::string var, std::string member) {
    keyidNarrowings_.emplace_back(std::move(var), std::move(member));
}

void SemanticAnalyzer::popKeyidNarrowing() {
    if (!keyidNarrowings_.empty()) keyidNarrowings_.pop_back();
}

const std::string* SemanticAnalyzer::narrowedMemberFor(const std::string& var) const {
    for (auto it = keyidNarrowings_.rbegin(); it != keyidNarrowings_.rend(); ++it)
        if (it->first == var) return &it->second;
    return nullptr;
}

void SemanticAnalyzer::warnOnAssignmentInCondition(Expression& cond) {
    const auto* bin = dynamic_cast<const BinaryOp*>(&cond);
    if (!bin || bin->op != ASTTokenKind::EQUAL || cond.parenthesized) return;
    warning(cond, "assignment in condition: did you mean '==' instead of '='? "
                  "If the assignment is intentional, wrap it in extra parentheses");
}

void SemanticAnalyzer::visit(Block& node) {
    enterScope();
    // Wave-4 step 17 (W7): a bare brace opens a scope (ADR 0011), and leaving
    // it is a scope exit for its locals. The frame unwinds here with a
    // fallthrough point per variable; jumps out recorded their own exits on
    // the way. Skipped on the check walk (§3.3).
    if (!injectedWalk_) moved_.enterBlock();
    // #105 provenance-lite: pointer-local entries live and die with this scope.
    ptrProvFrames_.emplace_back();
    // Issue #46: everything after a `return` in the same block is dead (the
    // backend drops it in silence), so each statement past one warns, spanned
    // on itself. Still walked, so errors inside still report. Warning-level
    // only, never failing the build (the warnOnHostBranch precedent).
    bool afterReturn = false;
    for (auto& stmt : node.statements) {
        // Skipped on the check walk like the scope-exit warnings in exitScope:
        // injected code is not the user's to fix.
        if (afterReturn && !injectedWalk_) {
            // An expression statement carries the parser's default location,
            // so the call inside it is the span a reader can find.
            if (auto* es = dynamic_cast<ExpressionStatement*>(stmt.get());
                es && es->expr)
                warning(*es->expr, "unreachable statement after 'return' never runs; remove it");
            else
                warning(*stmt, "unreachable statement after 'return' never runs; remove it");
        }
        stmt->accept(*this);
        notePtrProvenance(stmt.get(), currentScope, globalScope, currentStructContext);
        if (!afterReturn && dynamic_cast<ReturnStatement*>(stmt.get())) afterReturn = true;
    }
    ptrProvFrames_.pop_back();
    if (!injectedWalk_) {
        auto w7report = [this](ASTNode& at, const std::string& msg) { error(at, msg); };
        moved_.exitBlock(node, w7report);
    }
    exitScope();
}

void SemanticAnalyzer::visit(ReturnStatement& node) {
    // Wave-4 step 17 (W6 floor): a `return` is a function_exit fire point
    // (docs/compiler-api.md §3.2). Recorded with no diagnostic of its own.
    // Nothing is recorded outside a FunctionDeclaration body: returns in
    // constructors, operators, destructors and lambdas wait until those
    // bodies track the site too (see currentFunction()).
    if (current_function_ != "<root>")
        noteFirePoint({"function_exit", current_function_, node.loc.begin.line, "return"});
    if (node.value) {
        // The declared return type is a hint for the expression, which is what a
        // declaration's annotation is: `return Err("File don't exists");` inside
        // `static fun open(path: string) <IOResult<Stream>>`
        // (tests/samples/stdlib/stdio.fin:154) says which `IOResult` is being built in
        // the only place the function has to say it. Installed before the walk, because
        // inference reads it while typing the expression, and cleared after, because it
        // belongs to this expression and no other.
        if (context.currentFuncReturnType && !isErrorType(context.currentFuncReturnType)) {
            typeHintFor = node.value.get();
            typeHint = context.currentFuncReturnType;
        }
        node.value->accept(*this);
        typeHintFor = nullptr;
        typeHint = nullptr;
        // Check return type
        if (context.currentFuncReturnType) {
            // Constructors conventionally return a heap-allocated instance (`new S{}`),
            // while their declared result is the value type S. The allocation is the
            // constructor's storage operation, so accept that pointer form here; all
            // other return expressions still undergo the ordinary exact check.
            auto expectedStruct = std::dynamic_pointer_cast<StructType>(context.currentFuncReturnType);
            auto returnedPtr = lastExprType ? std::dynamic_pointer_cast<PointerType>(lastExprType) : nullptr;
            const bool ctorPointerResult =
                expectedStruct && returnedPtr && returnedPtr->pointee &&
                typesEqual(returnedPtr->pointee, expectedStruct);
            bool typeOk = true;
            if (!ctorPointerResult) {
                typeOk = checkType(*node.value, lastExprType, context.currentFuncReturnType);
            }
            if (typeOk) refuseEscapingReturn(node, ctorPointerResult);
        }
    } else {
        // Return void
        auto voidType = currentScope->resolveType("void");
        if (context.currentFuncReturnType) {
            checkType(node, voidType, context.currentFuncReturnType);
        }
    }
    // Wave-4 step 17 (W7): a `return` unwinds every scope to the function
    // boundary with ExitNormal. After the value walk: `@move(x)` in the
    // value marks x before the exit is recorded. Skipped on the check walk.
    if (!injectedWalk_) {
        auto w7report = [this](ASTNode& at, const std::string& msg) { error(at, msg); };
        moved_.onReturn(node, w7report);
    }
}

// A returned reference must not point into the frame it leaves (#99, ADR
// 0055). `&d` hands out the slot, and converting a local to an interface hands
// out that slot's address as the pair's data word; both dangle the moment the
// call ends. One rule for both spellings -- the defect was never
// interface-specific, so the check lives where the two meet rather than in
// either type's own check. What decides is whose frame owns the storage and
// not which spelling asked, so the receiver, a pointer's referent, a global
// and `#[slaveof($Fin)]`-pinned storage all stay legal: each outlives the call
// that returns it.
void SemanticAnalyzer::refuseEscapingReturn(ReturnStatement& node, bool ctorPointerResult) {
    if (!node.value || !context.currentFuncReturnType) return;

    const bool ptrExpected = context.currentFuncReturnType->as<PointerType>() != nullptr;
    const auto* expectedIface = context.currentFuncReturnType->as<StructType>();
    const bool ifaceExpected = expectedIface && expectedIface->is_interface;
    // The constructor waiver drops the *type* check for a returned pointer,
    // never this one: `&local` handed back where the value type is declared is
    // the same dangle the `<&D>` spelling refuses, reached through the waiver.
    const bool addressForm = ptrExpected || ctorPointerResult;

    ReturnedStorage held;
    if (addressForm) {
        if (const auto* u = dynamic_cast<const UnaryOp*>(node.value.get()))
            if (u->op == ASTTokenKind::AMPERSAND)
                held = returnedStorage(u->operand.get());
    } else if (ifaceExpected) {
        // `return s` where s is already a pair copies the pair; the address
        // (if any) was taken where the pair was formed.
        const auto* actualIface = lastExprType ? lastExprType->as<StructType>() : nullptr;
        if (!actualIface || !actualIface->is_interface)
            held = returnedStorage(node.value.get());
    }

    Symbol* sym = (held.root && currentScope) ? currentScope->resolve(held.root->name) : nullptr;
    if (!sym || !globalScope) {
        // `return p` takes no address and a returned closure names none, so
        // `held` is empty for both -- but either can still hand out this
        // frame through a pointer local (#105). Untracked pointers (a
        // parameter, a global, a call's result) stay silent here, as today.
        const auto* lam = node.value ? dynamic_cast<const LambdaExpression*>(node.value.get())
                                     : nullptr;
        if (node.value && (addressForm || lam)) {
            if (const Symbol* ref =
                    launderedReferent(node.value.get(), currentScope, globalScope,
                                       currentStructContext))
                error(*node.value,
                      fmt::format("cannot return the address of local variable '{}'", ref->name),
                      "return caller-owned storage or an owned heap allocation (`new`) "
                      "instead; this slot ends with the call");
        }
        return;
    }
    const std::string& name = held.root->name;

    // A global's storage is the program's, so no call can end it.
    if (sym == globalScope->resolve(name)) return;
    // `$Fin` pins storage to program exit (Scope.hpp:19-27).
    if (sym->slaveof == "$Fin") return;
    // A method's receiver is the caller's object, not this frame: `self` is by
    // reference, so a mutation through it is visible to the caller. The
    // implicit injection types it `Self` or the struct itself
    // (Analyzer_Decl.cpp:476, :865), neither a pointer, so the dereference
    // test below cannot see it; an explicit `self: &Self` parameter is a
    // pointer and is exempt there instead.
    if (name == "self" && currentStructContext) return;
    // Reaching *through* a pointer lands in storage someone else owns, while
    // the pointer's own slot is this frame's either way -- unless the pointer
    // is a local initialised from `&local`, which launders this frame's
    // address through a foreign-looking type (#105). The recorded referent
    // decides those; everything else keeps the declared-type reading, so the
    // accepted pointer-parameter case stays accepted.
    if (!held.wholeSlot && sym->type && sym->type->as<PointerType>()) {
        const Symbol* ref = nullptr;
        if (lookupPtrProvenance(name, ref) && ref &&
            !ptrProvForeign(ref, globalScope, currentStructContext)) {
            error(*node.value,
                  fmt::format("cannot return the address of local variable '{}'", ref->name),
                  "return caller-owned storage or an owned heap allocation (`new`) "
                  "instead; this slot ends with the call");
        }
        return;
    }

    // The remedy is the same for both spellings, and the compiler is sure of
    // it, so it earns the `= help:` row over the typo heuristic (the 3-arg
    // `error` contract, SemanticAnalyzer.hpp).
    error(*node.value,
          addressForm
              ? fmt::format("cannot return the address of local variable '{}'", name)
              : fmt::format("cannot return local variable '{}' as an interface", name),
          "return caller-owned storage or an owned heap allocation (`new`) "
          "instead; this slot ends with the call");
}

void SemanticAnalyzer::visit(ExpressionStatement& node) {
    node.expr->accept(*this);
}

void SemanticAnalyzer::visit(IfStatement& node) {
    node.condition->accept(*this);
    if (node.condition) warnOnAssignmentInCondition(*node.condition);
    // A `getkeyid(v) == keyidof(M)` guard narrows `v`'s payload slots to M's
    // in the taken arm. Matched after the condition's own walk, which is what
    // type-checks the two calls; the use site re-validates the member against
    // the value's enum, so this stays syntactic.
    const auto keyidGuard = matchKeyidGuard(node.condition.get());
    // B4 (ADR 0042): a `@defined`-mentioned guard that folds decides the
    // arm through ComptimeInterp (never around it). The untaken arm is
    // eliminated — its `@define`s never elaborate — and a taken `@define`
    // lifts by the ordinary visit below. Anything that does not fold keeps
    // the both-arms walk.
    //
    // Wave-4 `@implements` lowering, stage A: an `@implements`-mentioned
    // guard folds the same way, through the same interpreter, with the fold
    // answering through the compiler-API op (`compiler.types.implements`,
    // installed by installComptimeHooks -- the `symbols.defined` precedent).
    // A `$struct`/`$interface` parameter never folds (the hook answers
    // nullopt), so `compatible()` keeps its both-arms walk.
    if (w5_program_ && node.condition &&
        (mentionsDefined(node.condition.get()) ||
         mentionsImplements(node.condition.get()))) {
        comptime::Interpreter interp(*w5_program_);
        interp.setDefinedHook([this](const std::string& name) -> std::optional<bool> {
            if (currentScope->resolve(name)) return true;
            if (currentScope->resolveType(name)) return true;
            if (currentScope->resolveMacro(name)) return true;
            return false;
        });
        installComptimeHooks(interp);
        comptime::Env env;
        comptime::ExprResult folded = interp.evaluateExpression(*node.condition, env);
        if (folded.status == comptime::ExprStatus::Ok &&
            folded.value.kind == comptime::ValueKind::Bool) {
            const bool take = (folded.value.text == "true");
            if (take) {
                // The guard folded true: the else arm is dead for every
                // downstream consumer, so prune it to an empty block. Analysis
                // already skipped it; without the prune codegen still walks it
                // and refuses the hard error inside. Recorded in prunedArms_
                // so checkReturnPaths answers from the taken arm.
                if (node.else_stmt) {
                    auto pruned = std::make_unique<Block>(
                        std::vector<std::unique_ptr<Statement>>{});
                    prunedArms_.insert(pruned.get());
                    node.else_stmt = std::move(pruned);
                }
                if (injectedWalk_) {
                    ScopedKeyidNarrowing narrow(*this, keyidGuard);
                    node.then_block->accept(*this);
                    return;
                }
                auto snap = moved_.snapshot();
                {
                    ScopedKeyidNarrowing narrow(*this, keyidGuard);
                    node.then_block->accept(*this);
                }
                auto thenEnd = moved_.snapshot();
                moved_.installJoin(thenEnd, snap);
                return;
            }
            // The guard folded false: the then arm is dead -- prune it to an
            // empty block (see the take-true prune above) and walk the else.
            // Pruned before the no-else return so a dead then with no else
            // still lowers to nothing.
            {
                auto pruned = std::make_unique<Block>(
                    std::vector<std::unique_ptr<Statement>>{});
                prunedArms_.insert(pruned.get());
                node.then_block = std::move(pruned);
            }
            if (!node.else_stmt) return;
            if (injectedWalk_) {
                node.else_stmt->accept(*this);
                return;
            }
            auto snap = moved_.snapshot();
            moved_.restore(snap);
            node.else_stmt->accept(*this);
            moved_.installJoin(snap, moved_.snapshot());
            return;
        }
    }
    // Wave-4 step 17 (W7): the two branches fork the moved state and join it
    // after. A move on one side only is MovedMaybe past the join; agreement
    // holds. The walk order is unchanged: only the state forks.
    if (injectedWalk_) {
        ScopedKeyidNarrowing narrow(*this, keyidGuard);
        node.then_block->accept(*this);
        if (node.else_stmt) node.else_stmt->accept(*this);
        return;
    }
    auto snap = moved_.snapshot();
    {
        ScopedKeyidNarrowing narrow(*this, keyidGuard);
        node.then_block->accept(*this);
    }
    auto thenEnd = moved_.snapshot();
    if (node.else_stmt) {
        moved_.restore(snap);
        node.else_stmt->accept(*this);
        moved_.installJoin(thenEnd, moved_.snapshot());
    } else {
        moved_.installJoin(thenEnd, snap);
    }
}

void SemanticAnalyzer::visit(WhileLoop& node) {
    bool prevLoop = context.inLoop;
    context.inLoop = true;
    // Wave-4 step 20 (W10): the loop's back edge. One static latch point per
    // loop statement, recorded before the body walk so outer points precede
    // inner ones in fire order. Skipped on the check walk: injected code does
    // not fire events (§3.3).
    if (!injectedWalk_) {
        events::LoopBackEdgePoint point;
        point.kind = node.is_do_while ? "do-while" : "while";
        point.line = node.loc.begin.line;
        point.depth = ++loopDepth_;
        point.body = node.body.get();
        w10_points_.push_back(point);
    }

    node.condition->accept(*this);
    if (node.condition) warnOnAssignmentInCondition(*node.condition);
    // Wave-4 step 17 (W7): the body may run zero or more times, so its end
    // joins its start. A move in the body is Maybe past the loop.
    if (injectedWalk_) {
        node.body->accept(*this);
    } else {
        moved_.enterLoop();
        auto pre = moved_.snapshot();
        node.body->accept(*this);
        moved_.installJoin(pre, moved_.snapshot());
        moved_.exitLoop();
    }

    if (!injectedWalk_) --loopDepth_;
    context.inLoop = prevLoop;
}

void SemanticAnalyzer::visit(ForLoop& node) {
    bool prevLoop = context.inLoop;
    context.inLoop = true;

    // Wave-4 step 20 (W10): the loop's back edge (see visit(WhileLoop&)).
    const bool w10track = !injectedWalk_;
    if (w10track) {
        events::LoopBackEdgePoint point;
        point.kind = "for";
        point.line = node.loc.begin.line;
        point.depth = ++loopDepth_;
        point.body = node.body.get();
        w10_points_.push_back(point);
    }

    enterScope(); // For loop var
    // Wave-4 step 17 (W7): the header scope holds the counter, which leaves
    // scope when the loop does -- so its fallthrough point anchors after the
    // loop statement, where the scope ends, rather than at a block end.
    const bool track = !injectedWalk_;
    if (track) {
        moved_.enterLoop();
        moved_.enterBlock();
    }
    if(node.init) node.init->accept(*this);
    if(node.condition) {
        node.condition->accept(*this);
        warnOnAssignmentInCondition(*node.condition);
    }
    events::MovedAnalysis::Snapshot pre;
    if (track) pre = moved_.snapshot();
    if(node.increment) node.increment->accept(*this);
    if(node.body) node.body->accept(*this);
    if (track) {
        moved_.installJoin(pre, moved_.snapshot());
        auto w7report = [this](ASTNode& at, const std::string& msg) { error(at, msg); };
        moved_.exitBlockAfter(node, w7report);
        moved_.exitLoop();
    }
    exitScope();

    if (w10track) --loopDepth_;
    context.inLoop = prevLoop;
}

void SemanticAnalyzer::visit(ForeachLoop& node) {
    bool prevLoop = context.inLoop;
    context.inLoop = true;

    // Wave-4 step 20 (W10): the loop's back edge (see visit(WhileLoop&)).
    const bool w10track = !injectedWalk_;
    if (w10track) {
        events::LoopBackEdgePoint point;
        point.kind = "foreach";
        point.line = node.loc.begin.line;
        point.depth = ++loopDepth_;
        point.body = node.body.get();
        w10_points_.push_back(point);
    }

    enterScope();
    // Wave-4 step 17 (W7): the element/index scope, like the for header. Its
    // fallthrough anchors after the loop statement.
    const bool track = !injectedWalk_;
    if (track) {
        moved_.enterLoop();
        moved_.enterBlock();
    }
    // Define loop variable
    auto type = resolveTypeFromAST(node.var_type.get());
    if(type) currentScope->define({node.var_name, type, false, true});
    if (track && type) moved_.declare(node.var_name, type->toString());

    // And the index binding of the two-binding form. The parser has stored it on the
    // node since `foreach (idx <int>, element <int> in a)` began to parse (parser.y:2047
    // and :2058, one production per spelling) and nothing here read it, so loops.fin:19 --
    // whose body is `blame element == a[idx];` -- reported `Undefined variable 'idx'`.
    // That was the last diagnostic standing between loops.fin and `//@ ok`.
    //
    // An empty name means the one-binding form and not a nameless binding; ControlFlow.hpp
    // says so where the fields are declared, and the grammar cannot produce an empty
    // IDENTIFIER. Defined non-const to match the element beside it: assigning to either
    // is meaningless, but immutability is not enforced anywhere yet
    // (KnownDefect_Declarations), and making the index the one place it bites would be a
    // rule invented here rather than one the corpus asked for.
    //
    // The written type is checked below, exactly as the element's is: the
    // iterable is walked first and each binding is compared against what it
    // yields (Soundness_Foreach.ABindingTypeIsCheckedAgainstTheIterable).
    // The written index type, kept for the iterable check below rather than
    // resolved twice: resolving reports, so a second call would double the
    // diagnostic on an unresolvable annotation.
    std::shared_ptr<Type> indexWritten;
    if (!node.index_name.empty()) {
        indexWritten = resolveTypeFromAST(node.index_type.get());
        if (indexWritten) currentScope->define({node.index_name, indexWritten, false, true});
        if (track && indexWritten) moved_.declare(node.index_name, indexWritten->toString());
    }

    events::MovedAnalysis::Snapshot pre;
    if (track) pre = moved_.snapshot();
    if(node.iterable) node.iterable->accept(*this);
    // #98: the iterable is asked what it is, and the bindings what they claim.
    // Iterable is an array and nothing else -- the backend lowers only arrays
    // ("a 'foreach' over something that is not an array"), and the library says
    // there is no iteration protocol (lib/std/collection.fin:59,
    // lib/std/hashmap.fin:365-367), so the front end refuses the rest rather
    // than checking a program the backend cannot lower. A pointer to an array
    // walks the array, the way a subscript does.
    //
    // An array yields its elements and an index that is an `int`-family count:
    // the element binding must *be* the element type (converting it would read
    // one type's bytes as another's), and the index binding must be an integer
    // (checkIntegerIndex carries that rule; `char` counts, `bool` does not).
    // The bindings keep their written types either way, so the body reports
    // nothing further about a mismatch announced here.
    if (auto iterableType = lastExprType; iterableType && !isErrorType(iterableType)) {
        const ArrayType* walked = nullptr;
        if (auto* arr = dynamic_cast<const ArrayType*>(iterableType.get())) {
            walked = arr;
        } else if (auto* ptr = dynamic_cast<const PointerType*>(iterableType.get())) {
            walked = dynamic_cast<const ArrayType*>(ptr->pointee.get());
        }
        if (!walked) {
            error(*node.iterable,
                  fmt::format("Cannot iterate over type '{}'", iterableType->toString()));
        } else {
            if (type && !isErrorType(type) && walked->element_type &&
                !isErrorType(walked->element_type) &&
                !walked->element_type->equals(*type)) {
                error(*node.var_type,
                      fmt::format("Type mismatch: expected '{}', got '{}'", type->toString(),
                                  walked->element_type->toString()));
            }
            if (!node.index_name.empty()) {
                checkIntegerIndex(*node.index_type, indexWritten);
            }
        }
    }
    if(node.body) node.body->accept(*this);
    if (track) {
        moved_.installJoin(pre, moved_.snapshot());
        auto w7report = [this](ASTNode& at, const std::string& msg) { error(at, msg); };
        moved_.exitBlockAfter(node, w7report);
        moved_.exitLoop();
    }
    exitScope();

    if (w10track) --loopDepth_;
    context.inLoop = prevLoop;
}

void SemanticAnalyzer::visit(BreakStatement& node) {
    if (!context.inLoop) {
        error(node, "'break' used outside of loop");
    }
    // Wave-4 step 17 (W7): a `break` unwinds to the innermost loop boundary
    // with ExitNormal. On the check walk, or outside any loop, this records
    // nothing.
    if (!injectedWalk_) {
        auto w7report = [this](ASTNode& at, const std::string& msg) { error(at, msg); };
        moved_.onBreak(node, w7report);
    }
}

void SemanticAnalyzer::visit(ContinueStatement& node) {
    if (!context.inLoop) {
        error(node, "'continue' used outside of loop");
    }
    // Wave-4 step 17 (W7): like `break`.
    if (!injectedWalk_) {
        auto w7report = [this](ASTNode& at, const std::string& msg) { error(at, msg); };
        moved_.onContinue(node, w7report);
    }
}

void SemanticAnalyzer::visit(DeleteStatement& node) {
    node.expr->accept(*this);
    auto type = lastExprType;

    if (!type) return;

    // A pointer, or a dynamic array. The array half is not a concession: `new [T, n]`
    // yields a `[T]` (Soundness_HeapArrays), and stdlib/collection.fin allocates the
    // buffer that way on 54 and frees it with `delete self._arr` on 46, where `_arr` is
    // declared `[T]`. One buffer, both ends, one file.
    //
    // A *fixed*-extent array is refused. `[int, 3]` is what an annotation carrying an
    // extent and an array literal both produce, neither of which came from an
    // allocator, and no corpus line deletes one. What the type cannot tell us is
    // provenance -- a `[T]` that decayed from a literal is accepted here -- but that is
    // the latitude `delete p` already has over a pointer to a local.
    if (dynamic_cast<PointerType*>(type.get())) {
        // Wave-4 step 17 (W6 floor): each `delete` is a delete_site fire
        // point carrying the deleted type (docs/compiler-api.md §3.2).
        noteFirePoint({"delete_site", current_function_, node.loc.begin.line, type->toString()});
        return;
    }
    if (auto* arr = dynamic_cast<ArrayType*>(type.get()); arr && !arr->isFixed()) {
        noteFirePoint({"delete_site", current_function_, node.loc.begin.line, type->toString()});
        return;
    }

    error(node, fmt::format("Cannot delete non-pointer type '{}'", type->toString()));
}

void SemanticAnalyzer::visit(TryCatch& node) {
    node.try_block->accept(*this);
    enterScope();
    // Define catch var
    auto type = resolveTypeFromAST(node.catch_type.get());
    if(type) currentScope->define({node.catch_var, type, false, true});
    // Wave-4 step 17 (W7): the catch variable lives in a scope with no Block
    // node of its own, so it is tracked in the enclosing frame and its exit
    // is recorded when that frame ends -- an approximation the exceptional
    // path owes, not a scope the walk can name.
    if (!injectedWalk_ && type) moved_.declare(node.catch_var, type->toString());
    node.catch_block->accept(*this);
    exitScope();
}

void SemanticAnalyzer::visit(BlameStatement& node) {
    // Wave-4 step 17 (W7): only the raising form unwinds. Read off the same
    // operand-type rule below that tells the two `blame` statements apart.
    bool raises = false;
    if (node.condition) {
        node.condition->accept(*this);
        // One keyword, two statements, told apart by the operand's type -- they are
        // written identically, so there is nothing else to tell them apart by.
        // `blame val > 0` asserts (blame_assert.fin:5) and `blame CollectionError("Index
        // out of bounds")` raises (stdlib/collection.fin:63), and comparing the second
        // against bool reported `expected 'bool', got 'CollectionError'` about a
        // statement the language has. Soundness_Blame carries the whole rule.
        //
        // What raises is `string | Error-subtype-or-implements | any/object`
        // (ADR 0053, the owner's ruling on #97). A `string`, because `ErrorLike`
        // reads one as error-like -- and it is the raise form, not the assert
        // form's message in the wrong position, by ruling rather than by type. A
        // struct, when it is `Error`, inherits it, or implements it; any other
        // struct is refused as not error-like rather than compared against bool,
        // which would misreport a raise as a failed assert. Everything else keeps
        // the bool comparison, which is what leaves `blame 1;` an error with the
        // message it always had (Soundness_Conditions.BlameStillRejectsAnInteger
        // is a control for a separate argument and reads that message).
        //
        // No unwrapping, as in isEnumType: a `&CollectionError` is a pointer and this
        // asks about a value. Nothing in the corpus raises through one, and a reader
        // that needs it should say so at its own call site.
        //
        // A value whose static type is erased is a raise too. `any` is the type of a
        // payload slot an enum's members disagree at (visit(MemberAccess&)), and the
        // corpus raises exactly that four times -- `blame enum_.0` at
        // stdlib/typing.fin:32 and :38, stdlib/stdio.fin:60 and :66. The reason it is
        // the raise form and not the assert form: an assert's operand is a comparison,
        // whose type is `bool`, and nothing erases a `bool`.
        bool isRaise = false;
        if (lastExprType) {
            if (lastExprType->as<DynamicType>()) {
                isRaise = true;
            } else if (auto* raised = lastExprType->as<StructType>()) {
                auto errorType = currentScope->resolveType("Error");
                const StructType* errorStruct =
                    errorType ? errorType->as<StructType>() : nullptr;
                if (isErrorRelated(raised, errorStruct)) {
                    isRaise = true;
                } else {
                    error(*node.condition,
                          "blame raises an error-like value ('string', a subtype of "
                          "'Error', 'any'/'object'); got '" +
                              lastExprType->toString() + "'");
                }
            } else if (auto stringType = currentScope->resolveType("string");
                       stringType && typesEqual(lastExprType, stringType)) {
                isRaise = true;
            } else {
                auto boolType = currentScope->resolveType("bool");
                checkType(*node.condition, lastExprType, boolType);
            }
        }
        raises = isRaise;
    }

    if (node.message) {
        node.message->accept(*this);
        auto stringType = currentScope->resolveType("string");
        if (lastExprType) {
            checkType(*node.message, lastExprType, stringType);
        }
    }
    // A raising `blame` unwinds the function with ExitBlamed (§3.2's blame
    // unwind; ADR 0030's destructors do not run there, but the event still
    // fires so a handler can tell the paths apart). The asserting form
    // continues execution and is no exit. Skipped on the check walk.
    if (!injectedWalk_ && raises) {
        auto w7report = [this](ASTNode& at, const std::string& msg) { error(at, msg); };
        moved_.onBlame(node, w7report);
    }
}

bool SemanticAnalyzer::checkReturnPaths(Statement* node) {
    if (!node) return false;

    if (dynamic_cast<ReturnStatement*>(node)) return true;
    if (dynamic_cast<BlameStatement*>(node)) return true;

    if (auto* block = dynamic_cast<Block*>(node)) {
        for (auto& stmt : block->statements) {
            if (checkReturnPaths(stmt.get())) return true;
        }
        return false;
    }

    if (auto* ifStmt = dynamic_cast<IfStatement*>(node)) {
        // A folded guard's pruned arm is unreachable, so the taken arm
        // decides: without this a both-arms-return `if` over a folded guard
        // reports a missing return once the dead arm is an empty block.
        if (ifStmt->else_stmt) {
            const auto* elseBlock = dynamic_cast<const Block*>(ifStmt->else_stmt.get());
            if (elseBlock && prunedArms_.count(elseBlock) != 0)
                return checkReturnPaths(ifStmt->then_block.get());
        }
        if (prunedArms_.count(ifStmt->then_block.get()) != 0) {
            if (ifStmt->else_stmt) return checkReturnPaths(ifStmt->else_stmt.get());
            return false;
        }
        if (ifStmt->else_stmt) {
            return checkReturnPaths(ifStmt->then_block.get()) && 
                   checkReturnPaths(ifStmt->else_stmt.get());
        }
        return false;
    }

    return false;
}

}
