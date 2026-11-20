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

#include "TypeDef.hpp"
#include "../Visitor.hpp"

namespace fin {

TypeDefinition::TypeDefinition(std::string n, std::unique_ptr<TypeNode> t)
    : name(std::move(n)), aliased_type(std::move(t)) {}

void TypeDefinition::accept(Visitor& v) { v.visit(*this); }

SpecialDeclaration::SpecialDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p,
                                       std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b)
    : name(std::move(n)), params(std::move(p)), return_type(std::move(rt)), body(std::move(b)) {}

void SpecialDeclaration::accept(Visitor& v) { v.visit(*this); }

}
