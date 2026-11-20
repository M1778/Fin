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
