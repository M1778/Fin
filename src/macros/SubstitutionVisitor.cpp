#include "SubstitutionVisitor.hpp"
#include "../ast/CloneVisitor.hpp"
#include <iostream>

namespace fin {

SubstitutionVisitor::SubstitutionVisitor(const std::unordered_map<std::string, std::unique_ptr<Expression>>& a) 
    : args(a) {}

void SubstitutionVisitor::substitute(ASTNode* node) {
    if (node) node->accept(*this);
}

std::unique_ptr<Expression> SubstitutionVisitor::cloneArg(const std::string& name) {
    if (args.count(name)) {
        CloneVisitor cloner;
        return cloner.clone(args.at(name).get());
    }
    return nullptr;
}

void SubstitutionVisitor::visit(SuperExpression& node) {
    for (auto& f : node.init_fields) {
        f.second->accept(*this);
        if (replacementExpr) { f.second = std::move(replacementExpr); replacementExpr = nullptr; }
    }
    for (auto& arg : node.args) {
        arg->accept(*this);
        if (replacementExpr) { arg = std::move(replacementExpr); replacementExpr = nullptr; }
    }
}

void SubstitutionVisitor::visit(Identifier& node) {
    // Check if identifier starts with $
    if (node.name.size() > 1 && node.name[0] == '$') {
        std::string paramName = node.name.substr(1); // Remove prefix
        auto replacement = cloneArg(paramName);
        if (replacement) {
            replacementExpr = std::move(replacement);
        }
    }
}

// --- Traversal Logic (Propagate replacement up) ---

void SubstitutionVisitor::visit(BinaryOp& node) {
    node.left->accept(*this);
    if (replacementExpr) { node.left = std::move(replacementExpr); replacementExpr = nullptr; }
    
    node.right->accept(*this);
    if (replacementExpr) { node.right = std::move(replacementExpr); replacementExpr = nullptr; }
}

void SubstitutionVisitor::visit(UnaryOp& node) {
    node.operand->accept(*this);
    if (replacementExpr) { node.operand = std::move(replacementExpr); replacementExpr = nullptr; }
}

void SubstitutionVisitor::visit(FunctionCall& node) {
    for (auto& arg : node.args) {
        arg->accept(*this);
        if (replacementExpr) { arg = std::move(replacementExpr); replacementExpr = nullptr; }
    }
}

void SubstitutionVisitor::visit(VariableDeclaration& node) {
    if (node.initializer) {
        node.initializer->accept(*this);
        if (replacementExpr) { node.initializer = std::move(replacementExpr); replacementExpr = nullptr; }
    }
}

void SubstitutionVisitor::visit(ReturnStatement& node) {
    if (node.value) {
        node.value->accept(*this);
        if (replacementExpr) { node.value = std::move(replacementExpr); replacementExpr = nullptr; }
    }
}

void SubstitutionVisitor::visit(ExpressionStatement& node) {
    node.expr->accept(*this);
    if (replacementExpr) { node.expr = std::move(replacementExpr); replacementExpr = nullptr; }
}

void SubstitutionVisitor::visit(Block& node) {
    for (auto& stmt : node.statements) {
        stmt->accept(*this);
        // Statements cannot be replaced by Expressions directly here, 
        // unless we support ExpressionStatement replacement, which is tricky.
        // For now, we assume macros substitute Expressions into Expressions.
    }
}

// ... (Implement other traversals similarly) ...
void SubstitutionVisitor::visit(Program& node) { for(auto& s : node.statements) s->accept(*this); }
void SubstitutionVisitor::visit(IfStatement& node) {
    node.condition->accept(*this);
    if(replacementExpr) { node.condition = std::move(replacementExpr); replacementExpr = nullptr; }
    node.then_block->accept(*this);
    if(node.else_stmt) node.else_stmt->accept(*this);
}
void SubstitutionVisitor::visit(WhileLoop& node) {
    node.condition->accept(*this);
    if(replacementExpr) { node.condition = std::move(replacementExpr); replacementExpr = nullptr; }
    node.body->accept(*this);
}
void SubstitutionVisitor::visit(ForLoop& node) {
    if(node.init) node.init->accept(*this);
    if(node.condition) {
        node.condition->accept(*this);
        if(replacementExpr) { node.condition = std::move(replacementExpr); replacementExpr = nullptr; }
    }
    if(node.increment) {
        node.increment->accept(*this);
        if(replacementExpr) { node.increment = std::move(replacementExpr); replacementExpr = nullptr; }
    }
    node.body->accept(*this);
}
void SubstitutionVisitor::visit(ForeachLoop& node) {
    node.iterable->accept(*this);
    if(replacementExpr) { node.iterable = std::move(replacementExpr); replacementExpr = nullptr; }
    node.body->accept(*this);
}
void SubstitutionVisitor::visit(DeleteStatement& node) {
    node.expr->accept(*this);
    if(replacementExpr) { node.expr = std::move(replacementExpr); replacementExpr = nullptr; }
}
void SubstitutionVisitor::visit(BlameStatement& node) {
    node.error_expr->accept(*this);
    if(replacementExpr) { node.error_expr = std::move(replacementExpr); replacementExpr = nullptr; }
}
void SubstitutionVisitor::visit(MethodCall& node) {
    node.object->accept(*this);
    if(replacementExpr) { node.object = std::move(replacementExpr); replacementExpr = nullptr; }
    for(auto& arg : node.args) {
        arg->accept(*this);
        if(replacementExpr) { arg = std::move(replacementExpr); replacementExpr = nullptr; }
    }
}
void SubstitutionVisitor::visit(CastExpression& node) {
    node.expr->accept(*this);
    if(replacementExpr) { node.expr = std::move(replacementExpr); replacementExpr = nullptr; }
}
void SubstitutionVisitor::visit(NewExpression& node) {
    for(auto& arg : node.args) {
        arg->accept(*this);
        if(replacementExpr) { arg = std::move(replacementExpr); replacementExpr = nullptr; }
    }
    for(auto& f : node.init_fields) {
        f.second->accept(*this);
        if(replacementExpr) { f.second = std::move(replacementExpr); replacementExpr = nullptr; }
    }
}
void SubstitutionVisitor::visit(MemberAccess& node) {
    node.object->accept(*this);
    if(replacementExpr) { node.object = std::move(replacementExpr); replacementExpr = nullptr; }
}
void SubstitutionVisitor::visit(StructInstantiation& node) {
    for(auto& f : node.fields) {
        f.second->accept(*this);
        if(replacementExpr) { f.second = std::move(replacementExpr); replacementExpr = nullptr; }
    }
}
void SubstitutionVisitor::visit(ArrayLiteral& node) {
    for(auto& e : node.elements) {
        e->accept(*this);
        if(replacementExpr) { e = std::move(replacementExpr); replacementExpr = nullptr; }
    }
}
void SubstitutionVisitor::visit(ArrayAccess& node) {
    node.array->accept(*this);
    if(replacementExpr) { node.array = std::move(replacementExpr); replacementExpr = nullptr; }
    node.index->accept(*this);
    if(replacementExpr) { node.index = std::move(replacementExpr); replacementExpr = nullptr; }
}
void SubstitutionVisitor::visit(TernaryOp& node) {
    node.condition->accept(*this);
    if(replacementExpr) { node.condition = std::move(replacementExpr); replacementExpr = nullptr; }
    node.true_expr->accept(*this);
    if(replacementExpr) { node.true_expr = std::move(replacementExpr); replacementExpr = nullptr; }
    node.false_expr->accept(*this);
    if(replacementExpr) { node.false_expr = std::move(replacementExpr); replacementExpr = nullptr; }
}
void SubstitutionVisitor::visit(LambdaExpression& node) {
    if(node.body) node.body->accept(*this);
    if(node.expression_body) {
        node.expression_body->accept(*this);
        if(replacementExpr) { node.expression_body = std::move(replacementExpr); replacementExpr = nullptr; }
    }
}

void SubstitutionVisitor::visit(StaticMethodCall& node) {
    // Type substitution? Maybe.
    for (auto& arg : node.args) {
        arg->accept(*this);
        if (replacementExpr) { arg = std::move(replacementExpr); replacementExpr = nullptr; }
    }
}

void SubstitutionVisitor::visit(Parameter& node) {
    if (node.type) node.type->accept(*this);
    if (node.default_value) {
        node.default_value->accept(*this);
        if (replacementExpr) { node.default_value = std::move(replacementExpr); replacementExpr = nullptr; }
    }
}

void SubstitutionVisitor::visit(StructMember& node) {
    if (node.type) node.type->accept(*this);
    if (node.default_value) {
        node.default_value->accept(*this);
        if (replacementExpr) { node.default_value = std::move(replacementExpr); replacementExpr = nullptr; }
    }
}

void SubstitutionVisitor::visit(ConstructorDeclaration& node) {
    if (node.body) node.body->accept(*this);
}

void SubstitutionVisitor::visit(DestructorDeclaration& node) {
    if (node.body) node.body->accept(*this);
}
void SubstitutionVisitor::visit(PointerTypeNode& node) {
    node.pointee->accept(*this);
}

void SubstitutionVisitor::visit(ArrayTypeNode& node) {
    node.element_type->accept(*this);
    if (node.size) {
        node.size->accept(*this);
        if (replacementExpr) {
            node.size = std::move(replacementExpr);
            replacementExpr = nullptr;
        }
    }
}

}
