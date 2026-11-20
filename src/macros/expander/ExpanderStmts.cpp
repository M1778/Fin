// =============================================================================
// Fin Programming Language Compiler
//
// Made with ❤️ for someone special.
//
// This project is genuinely built on love and dedication, a way to express your love can be done through Fin.
// Fin is a semi-object-orientated programming language that specialized in control, giving so much control to the user while asking for the bare minimum.
// Fin is the name of the Angel from BlackNightmare-Stories not yet published.
// “Love is the reason this project exists”
// -----------------------------------------------------------------------------
// Author: M1778
// Repository: https://github.com/M1778/Fin
// Profile: https://github.com/M1778M/
// -----------------------------------------------------------------------------
// Copyright (C) 2027 M1778
//
// This file is part of the Fin Programming Language Compiler.
//
// Fin is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Fin is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Fin.  If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================

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
    if (node.condition) {
        node.condition->accept(*this);
        if (expandedExpression) { node.condition = std::move(expandedExpression); expandedExpression = nullptr; }
    }
    if (node.message) {
        node.message->accept(*this);
        if (expandedExpression) { node.message = std::move(expandedExpression); expandedExpression = nullptr; }
    }
}

void MacroExpander::visit(BreakStatement&) {}
void MacroExpander::visit(ContinueStatement&) {}

}
