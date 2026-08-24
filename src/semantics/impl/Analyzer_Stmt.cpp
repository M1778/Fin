#include "../SemanticAnalyzer.hpp"
#include "../../types/TypeImpl.hpp" 
namespace fin {

void SemanticAnalyzer::visit(Block& node) {
    enterScope();
    for (auto& stmt : node.statements) stmt->accept(*this);
    exitScope();
}

void SemanticAnalyzer::visit(ReturnStatement& node) {
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
            checkType(*node.value, lastExprType, context.currentFuncReturnType);
        }
    } else {
        // Return void
        auto voidType = currentScope->resolveType("void");
        if (context.currentFuncReturnType) {
            checkType(node, voidType, context.currentFuncReturnType);
        }
    }
}

void SemanticAnalyzer::visit(ExpressionStatement& node) {
    node.expr->accept(*this);
}

void SemanticAnalyzer::visit(IfStatement& node) {
    node.condition->accept(*this);
    // Ensure condition is bool (optional, C++ allows int)
    // checkType(*node.condition, lastExprType, currentScope->resolveType("bool"));
    
    node.then_block->accept(*this);
    if(node.else_stmt) node.else_stmt->accept(*this);
}

void SemanticAnalyzer::visit(WhileLoop& node) {
    bool prevLoop = context.inLoop;
    context.inLoop = true;
    
    node.condition->accept(*this);
    node.body->accept(*this);
    
    context.inLoop = prevLoop;
}

void SemanticAnalyzer::visit(ForLoop& node) {
    bool prevLoop = context.inLoop;
    context.inLoop = true;
    
    enterScope(); // For loop var
    if(node.init) node.init->accept(*this);
    if(node.condition) node.condition->accept(*this);
    if(node.increment) node.increment->accept(*this);
    if(node.body) node.body->accept(*this);
    exitScope();
    
    context.inLoop = prevLoop;
}

void SemanticAnalyzer::visit(ForeachLoop& node) {
    bool prevLoop = context.inLoop;
    context.inLoop = true;
    
    enterScope();
    // Define loop variable
    auto type = resolveTypeFromAST(node.var_type.get());
    if(type) currentScope->define({node.var_name, type, false, true});

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
    }

    if(node.iterable) node.iterable->accept(*this);
    if(node.body) node.body->accept(*this);
    exitScope();
    
    context.inLoop = prevLoop;
}

void SemanticAnalyzer::visit(BreakStatement& node) {
    if (!context.inLoop) {
        error(node, "'break' used outside of loop");
    }
}

void SemanticAnalyzer::visit(ContinueStatement& node) {
    if (!context.inLoop) {
        error(node, "'continue' used outside of loop");
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
    if (dynamic_cast<PointerType*>(type.get())) return;
    if (auto* arr = dynamic_cast<ArrayType*>(type.get()); arr && !arr->isFixed()) return;

    error(node, fmt::format("Cannot delete non-pointer type '{}'", type->toString()));
}

void SemanticAnalyzer::visit(TryCatch& node) {
    node.try_block->accept(*this);
    enterScope();
    // Define catch var
    auto type = resolveTypeFromAST(node.catch_type.get());
    if(type) currentScope->define({node.catch_var, type, false, true});
    node.catch_block->accept(*this);
    exitScope();
}

void SemanticAnalyzer::visit(BlameStatement& node) {
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
