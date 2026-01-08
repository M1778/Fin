#include "../MacroExpander.hpp"

namespace fin {

void MacroExpander::visit(PointerTypeNode& node) {
    node.pointee->accept(*this);
}

void MacroExpander::visit(ArrayTypeNode& node) {
    node.element_type->accept(*this);
    if (node.size) {
        node.size->accept(*this);
        if (expandedExpression) {
            node.size = std::move(expandedExpression);
            expandedExpression = nullptr;
        }
    }
}

void MacroExpander::visit(TypeNode& node) {
    if (node.array_size) {
        node.array_size->accept(*this);
        if (expandedExpression) {
            node.array_size = std::move(expandedExpression);
            expandedExpression = nullptr;
        }
    }
    for (auto& g : node.generics) {
        g->accept(*this);
    }
}

void MacroExpander::visit(FunctionTypeNode& node) {
    for (auto& p : node.param_types) {
        p->accept(*this);
    }
    if (node.return_type) {
        node.return_type->accept(*this);
    }
}

}
