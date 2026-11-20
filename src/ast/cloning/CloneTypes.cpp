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

void CloneVisitor::visit(TypeNode& node) {
    auto res = std::make_unique<TypeNode>(node.name);
    res->generics = cloneVector(node.generics);
    res->annotations = cloneVector(node.annotations);
    res->is_prototype = node.is_prototype;
    res->pointer_depth = node.pointer_depth; // Copy depth
    res->is_array = node.is_array;
    if (node.array_size) {
        res->array_size = clone(node.array_size.get());
    }
    // The declaring module travels with the clone (ADR 0023 step 4): the expander clones
    // a macro's quote body rather than consuming it, so a stamp dropped here is a stamp
    // that never reaches an expansion at all.
    res->declaringScope = node.declaringScope;
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(FunctionTypeNode& node) {
    auto res = std::make_unique<FunctionTypeNode>(
        cloneVector(node.param_types),
        clone(node.return_type.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(PointerTypeNode& node) {
    auto res = std::make_unique<PointerTypeNode>(clone(node.pointee.get()));
    res->setLoc(node.loc); result = std::move(res);
}

void CloneVisitor::visit(ArrayTypeNode& node) {
    auto res = std::make_unique<ArrayTypeNode>(clone(node.element_type.get()), clone(node.size.get()));
    res->setLoc(node.loc); result = std::move(res);
}

void CloneVisitor::visit(Attribute& node) {
    // Attribute has two constructors -- flag form and key=value form -- and
    // is_flag is what says which one this node came from.
    auto res = node.is_flag ? std::make_unique<Attribute>(node.name, true)
                            : std::make_unique<Attribute>(node.name, node.value_str);
    res->is_flag = node.is_flag;
    res->value_str = node.value_str;
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(GenericParam& node) {
    auto res = std::make_unique<GenericParam>(node.name, clone(node.constraint.get()));
    res->setLoc(node.loc);
    result = std::move(res);
}

}
