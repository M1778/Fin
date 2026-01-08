#include "../MacroExpander.hpp"

namespace fin {

void MacroExpander::visit(Block& node) { for (auto& stmt : node.statements) stmt->accept(*this); }

void MacroExpander::visit(VariableDeclaration& node) {
    if (node.type) {
        node.type->accept(*this);
    }

    if (node.initializer) {
        node.initializer->accept(*this);
        if (expandedExpression) {
            node.initializer = std::move(expandedExpression);
            expandedExpression = nullptr;
        }
    }
}
void MacroExpander::visit(ReturnStatement& node) {
    if (node.value) {
        node.value->accept(*this);
        if (expandedExpression) {
            node.value = std::move(expandedExpression);
            expandedExpression = nullptr;
        }
    }
}
void MacroExpander::visit(ExpressionStatement& node) {
    node.expr->accept(*this);
    if (expandedExpression) {
        node.expr = std::move(expandedExpression);
        expandedExpression = nullptr;
    }
}
void MacroExpander::visit(IfStatement& node) {
    node.condition->accept(*this);
    if (expandedExpression) { node.condition = std::move(expandedExpression); expandedExpression = nullptr; }
    node.then_block->accept(*this);
    if (node.else_stmt) node.else_stmt->accept(*this);
}
void MacroExpander::visit(WhileLoop& node) {
    node.condition->accept(*this);
    if (expandedExpression) { node.condition = std::move(expandedExpression); expandedExpression = nullptr; }
    node.body->accept(*this);
}
void MacroExpander::visit(ForLoop& node) {
    if (node.init) node.init->accept(*this);
    if (node.condition) {
        node.condition->accept(*this);
        if (expandedExpression) { node.condition = std::move(expandedExpression); expandedExpression = nullptr; }
    }
    if (node.increment) {
        node.increment->accept(*this);
        if (expandedExpression) { node.increment = std::move(expandedExpression); expandedExpression = nullptr; }
    }
    node.body->accept(*this);
}
void MacroExpander::visit(ForeachLoop& node) {
    node.iterable->accept(*this);
    if (expandedExpression) { node.iterable = std::move(expandedExpression); expandedExpression = nullptr; }
    node.body->accept(*this);
}
void MacroExpander::visit(DeleteStatement& node) {
    node.expr->accept(*this);
    if (expandedExpression) { node.expr = std::move(expandedExpression); expandedExpression = nullptr; }
}
void MacroExpander::visit(TryCatch& node) {
    node.try_block->accept(*this);
    node.catch_block->accept(*this);
}
void MacroExpander::visit(BlameStatement& node) {
    node.error_expr->accept(*this);
    if (expandedExpression) { node.error_expr = std::move(expandedExpression); expandedExpression = nullptr; }
}

void MacroExpander::visit(BreakStatement&) {}
void MacroExpander::visit(ContinueStatement&) {}

}
