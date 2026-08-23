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
        node.value->accept(*this);
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
    
    if (!dynamic_cast<PointerType*>(type.get())) {
        error(node, fmt::format("Cannot delete non-pointer type '{}'", type->toString()));
    }
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
        auto boolType = currentScope->resolveType("bool");
        if (lastExprType) {
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
