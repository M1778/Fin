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

#include "../CloneVisitor.hpp"

namespace fin {

void CloneVisitor::visit(Block& node) {
    auto res = std::make_unique<Block>(cloneVector(node.statements));
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(ReturnStatement& node) {
    auto res = std::make_unique<ReturnStatement>(clone(node.value.get()));
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(ExpressionStatement& node) {
    auto res = std::make_unique<ExpressionStatement>(clone(node.expr.get()));
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(IfStatement& node) {
    auto res = std::make_unique<IfStatement>(
        clone(node.condition.get()),
        clone(node.then_block.get()),
        clone(node.else_stmt.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(WhileLoop& node) {
    auto res = std::make_unique<WhileLoop>(
        clone(node.condition.get()),
        clone(node.body.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(ForLoop& node) {
    auto res = std::make_unique<ForLoop>(
        clone(node.init.get()),
        clone(node.condition.get()),
        clone(node.increment.get()),
        clone(node.body.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(ForeachLoop& node) {
    auto res = std::make_unique<ForeachLoop>(
        node.var_name,
        clone(node.var_type.get()),
        clone(node.iterable.get()),
        clone(node.body.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(BreakStatement& node) { 
    auto res = std::make_unique<BreakStatement>(); 
    res->setLoc(node.loc); result = std::move(res); 
}

void CloneVisitor::visit(ContinueStatement& node) { 
    auto res = std::make_unique<ContinueStatement>(); 
    res->setLoc(node.loc); result = std::move(res); 
}

void CloneVisitor::visit(DeleteStatement& node) {
    auto res = std::make_unique<DeleteStatement>(clone(node.expr.get()));
    res->setLoc(node.loc); result = std::move(res);
}

void CloneVisitor::visit(TryCatch& node) {
    auto res = std::make_unique<TryCatch>(
        clone(node.try_block.get()),
        node.catch_var,
        clone(node.catch_type.get()),
        clone(node.catch_block.get())
    );
    res->setLoc(node.loc); result = std::move(res);
}

void CloneVisitor::visit(BlameStatement& node) {
    auto res = std::make_unique<BlameStatement>(
        clone(node.condition.get()),
        clone(node.message.get())
    );
    res->setLoc(node.loc); result = std::move(res);
}

}
