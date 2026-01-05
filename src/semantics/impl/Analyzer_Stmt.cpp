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
    node.error_expr->accept(*this);
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
