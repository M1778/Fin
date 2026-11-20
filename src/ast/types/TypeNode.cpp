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

#include "TypeNode.hpp"
#include "../Visitor.hpp"
// For the out-of-line FunctionTypeNode destructor: its `generic_params` holds
// GenericParams, which TypeNode.hpp can only forward-declare.
#include "GenericParam.hpp"

namespace fin {

TypeNode::TypeNode(std::string n) : name(std::move(n)) {}
void TypeNode::accept(Visitor& v) { v.visit(*this); }

FunctionTypeNode::FunctionTypeNode(std::vector<std::unique_ptr<TypeNode>> params, std::unique_ptr<TypeNode> ret)
    : TypeNode("fn"), param_types(std::move(params)), return_type(std::move(ret)) {}
FunctionTypeNode::~FunctionTypeNode() = default;
void FunctionTypeNode::accept(Visitor& v) { v.visit(*this); }

PointerTypeNode::PointerTypeNode(std::unique_ptr<TypeNode> p) 
    : TypeNode("ptr"), pointee(std::move(p)) {}
void PointerTypeNode::accept(Visitor& v) { v.visit(*this); }

ArrayTypeNode::ArrayTypeNode(std::unique_ptr<TypeNode> elem, std::unique_ptr<Expression> s)
    : TypeNode("array"), element_type(std::move(elem)), size(std::move(s)) {}
void ArrayTypeNode::accept(Visitor& v) { v.visit(*this); }

} // namespace fin
