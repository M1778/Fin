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

#include "StructDecl.hpp"
#include "../Visitor.hpp"

namespace fin {

StructMember::StructMember(std::string n, std::unique_ptr<TypeNode> t, bool pub)
    : name(std::move(n)), type(std::move(t)), is_public(pub) {}
void StructMember::accept(Visitor& v) { v.visit(*this); }

StructDeclaration::StructDeclaration(std::string n, std::vector<std::unique_ptr<StructMember>> m, bool pub)
    : name(std::move(n)), members(std::move(m)), is_public(pub) {}
void StructDeclaration::accept(Visitor& v) { v.visit(*this); }

InterfaceDeclaration::InterfaceDeclaration(std::string n, 
                     std::vector<std::unique_ptr<StructMember>> m, 
                     std::vector<std::unique_ptr<FunctionDeclaration>> f, 
                     std::vector<std::unique_ptr<OperatorDeclaration>> o,
                     std::vector<std::unique_ptr<ConstructorDeclaration>> c,
                     std::unique_ptr<DestructorDeclaration> d,
                     bool pub)
    : name(std::move(n)), members(std::move(m)), methods(std::move(f)), 
      operators(std::move(o)), constructors(std::move(c)), destructor(std::move(d)), 
      is_public(pub) {}
void InterfaceDeclaration::accept(Visitor& v) { v.visit(*this); }

EnumDeclaration::EnumDeclaration(std::string n, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> v, bool pub)
    : name(std::move(n)), values(std::move(v)), is_public(pub) {}
void EnumDeclaration::accept(Visitor& v) { v.visit(*this); }

}
