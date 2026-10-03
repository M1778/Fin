#include "../SemanticAnalyzer.hpp"
#include "../ComptimeInterp.hpp"
#include "../EventPayloads.hpp"
#include "../../ast/decls/Program.hpp"
#include "../../ast/exprs/BinaryOp.hpp"
#include "../../ast/exprs/FunctionCall.hpp"
#include "../../ast/exprs/Identifier.hpp"
#include "../../ast/exprs/MiscExpr.hpp"
#include "../../ast/exprs/StructureExpr.hpp"
#include "../../ast/exprs/UnaryOp.hpp"
#include "../../ast/stmts/ControlFlow.hpp"
#include "../../types/TypeImpl.hpp"
namespace fin {

namespace {
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
    if (const auto* mem = dynamic_cast<const MemberAccess*>(e)) {
        return mem->object && mentionsImplements(mem->object.get());
    }
    return false;
}
}  // namespace

void SemanticAnalyzer::visit(Block& node) {
    enterScope();
    // Wave-4 step 17 (W7): a bare brace opens a scope (ADR 0011), and leaving
    // it is a scope exit for its locals. The frame unwinds here with a
    // fallthrough point per variable; jumps out recorded their own exits on
    // the way. Skipped on the check walk (§3.3).
    if (!injectedWalk_) moved_.enterBlock();
    for (auto& stmt : node.statements) stmt->accept(*this);
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
            if (!(expectedStruct && returnedPtr && returnedPtr->pointee &&
                  typesEqual(returnedPtr->pointee, expectedStruct))) {
                checkType(*node.value, lastExprType, context.currentFuncReturnType);
            }
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

void SemanticAnalyzer::visit(ExpressionStatement& node) {
    node.expr->accept(*this);
}

void SemanticAnalyzer::visit(IfStatement& node) {
    node.condition->accept(*this);
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
                if (injectedWalk_) {
                    node.then_block->accept(*this);
                    return;
                }
                auto snap = moved_.snapshot();
                node.then_block->accept(*this);
                auto thenEnd = moved_.snapshot();
                moved_.installJoin(thenEnd, snap);
                return;
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
        node.then_block->accept(*this);
        if (node.else_stmt) node.else_stmt->accept(*this);
        return;
    }
    auto snap = moved_.snapshot();
    node.then_block->accept(*this);
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
    if(node.condition) node.condition->accept(*this);
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
    // The written type is trusted, exactly as the element's is --
    // KnownDefect_Foreach.ABindingTypeIsNeverCheckedAgainstTheIterable holds that, and it
    // is one defect for both bindings rather than a new one introduced here.
    if (!node.index_name.empty()) {
        auto indexType = resolveTypeFromAST(node.index_type.get());
        if (indexType) currentScope->define({node.index_name, indexType, false, true});
        if (track && indexType) moved_.declare(node.index_name, indexType->toString());
    }

    events::MovedAnalysis::Snapshot pre;
    if (track) pre = moved_.snapshot();
    if(node.iterable) node.iterable->accept(*this);
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
        // A StructType is a raise: a struct, a class, an enum, or a value of interface
        // type. Everything else keeps the bool comparison, which is what leaves
        // `blame 1;` an error with the message it always had
        // (Soundness_Conditions.BlameStillRejectsAnInteger is a control for a separate
        // argument and reads that message).
        //
        // No unwrapping, as in isEnumType: a `&CollectionError` is a pointer and this
        // asks about a value. Nothing in the corpus raises through one, and a reader
        // that needs it should say so at its own call site.
        //
        // What is *not* checked is whether the raised value is error-like at all --
        // KnownDefect_Blame.RaisingAValueIsNotCheckedForBeingAnError records why: the
        // narrowing needs either the library's `Error` hardcoded here or the union
        // machinery behind `ErrorLike`, and `blame enum_.0` raises a bare `T`.
        // A value whose static type is erased is a raise too. `any` is the type of a
        // payload slot an enum's members disagree at (visit(MemberAccess&)), and the
        // corpus raises exactly that four times -- `blame enum_.0` at
        // stdlib/typing.fin:32 and :38, stdlib/stdio.fin:60 and :66. The reason it is
        // the raise form and not the assert form: an assert's operand is a comparison,
        // whose type is `bool`, and nothing erases a `bool`.
        const bool isRaise = lastExprType &&
                             (lastExprType->as<StructType>() || lastExprType->as<DynamicType>());
        raises = isRaise;
        auto boolType = currentScope->resolveType("bool");
        if (lastExprType && !isRaise) {
            checkType(*node.condition, lastExprType, boolType);
        }
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
        if (ifStmt->else_stmt) {
            return checkReturnPaths(ifStmt->then_block.get()) && 
                   checkReturnPaths(ifStmt->else_stmt.get());
        }
        return false;
    }

    return false;
}

}
