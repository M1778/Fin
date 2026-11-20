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

#pragma once
#include "Type.hpp"

namespace fin {

class Scope; // Forward decl

class NamespaceType : public Type {
public:
    std::string name;
    std::shared_ptr<Scope> scope;
    NamespaceType(std::string n, std::shared_ptr<Scope> s) 
        : name(std::move(n)), scope(std::move(s)) {}
    std::string toString() const override { return "module<" + name + ">"; }
    bool equals(const Type& other) const override {
        if (auto* o = other.as<NamespaceType>()) return name == o->name;
        return false;
    }
    TypePtr substitute(const TypeMap&, TypePtr = nullptr) override { 
        return std::make_shared<NamespaceType>(name, scope); 
    }
    TypePtr clone() const override { return std::make_shared<NamespaceType>(name, scope); }
};

}
