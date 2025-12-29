#include "../SemanticAnalyzer.hpp"

namespace fin {

void SemanticAnalyzer::visit(Literal& node) {
    switch(node.kind) {
        case ASTTokenKind::INTEGER: lastExprType = currentScope->resolveType("int"); break;
        case ASTTokenKind::FLOAT:   lastExprType = currentScope->resolveType("float"); break;
        case ASTTokenKind::STRING_LITERAL:  lastExprType = currentScope->resolveType("string"); break;
        case ASTTokenKind::BOOL:    lastExprType = currentScope->resolveType("bool"); break;
        case ASTTokenKind::KW_NULL: lastExprType = currentScope->resolveType("void"); break; // Null is tricky
        default: lastExprType = nullptr;
    }
}

void SemanticAnalyzer::visit(Identifier& node) {
    Symbol* sym = currentScope->resolve(node.name);
    if (!sym) {
        error(node, "Undefined variable '" + node.name + "'");
        lastExprType = nullptr;
    } else {
        lastExprType = sym->type;
    }
}

void SemanticAnalyzer::visit(BinaryOp& node) {
    node.left->accept(*this);
    auto leftType = lastExprType;
    
    node.right->accept(*this);
    auto rightType = lastExprType;
    
    if (leftType && rightType) {
        // Strict type checking: types must match
        if (!checkType(node, rightType, leftType)) {
            lastExprType = nullptr;
        } else {
            lastExprType = leftType; // Result type is same as operands (simplified)
        }
    }
}

void SemanticAnalyzer::visit(FunctionCall& node) {
    // 1. Check if function exists (simplified: just check if name is in scope?)
    // In a real compiler, functions are symbols too.
    // For now, we assume global functions are not in the symbol table yet unless we add them.
    // Let's just check args.
    for(auto& arg : node.args) arg->accept(*this);
    
    // TODO: Look up function signature and return actual return type
    lastExprType = currentScope->resolveType("void"); // Placeholder
}

void SemanticAnalyzer::visit(CastExpression& node) {
    node.expr->accept(*this);
    // Cast result is the target type
    lastExprType = resolveTypeFromAST(node.target_type.get());
}

void SemanticAnalyzer::visit(NewExpression& node) {
    for(auto& arg : node.args) arg->accept(*this);
    for(auto& f : node.init_fields) f.second->accept(*this);
    lastExprType = resolveTypeFromAST(node.type.get());
}

void SemanticAnalyzer::visit(MemberAccess& node) {
    node.object->accept(*this);
    // TODO: Look up member in struct type
    lastExprType = currentScope->resolveType("auto"); // Placeholder
}

void SemanticAnalyzer::visit(StructInstantiation& node) {
    // 1. Analyze fields
    for(auto& f : node.fields) f.second->accept(*this);

    // 2. Resolve base type
    auto baseType = currentScope->resolveType(node.struct_name);
    if (!baseType) {
        error(node, "Undefined struct '" + node.struct_name + "'");
        lastExprType = nullptr;
        return;
    }

    // 3. Handle Generics (Box::<int>)
    if (!node.generic_args.empty()) {
        std::vector<std::shared_ptr<Type>> args;
        for (auto& arg : node.generic_args) {
            auto t = resolveTypeFromAST(arg.get());
            if (t) args.push_back(t);
        }
        // Create a specialized StructType with the resolved arguments
        lastExprType = std::make_shared<StructType>(node.struct_name, args);
    } else {
        // Non-generic or inferred
        lastExprType = baseType;
    }
}

// Stubs
void SemanticAnalyzer::visit(UnaryOp& node) { node.operand->accept(*this); }
void SemanticAnalyzer::visit(MacroCall&) {}
void SemanticAnalyzer::visit(ArrayLiteral&) {}
void SemanticAnalyzer::visit(SizeofExpression&) { lastExprType = currentScope->resolveType("int"); }

}
