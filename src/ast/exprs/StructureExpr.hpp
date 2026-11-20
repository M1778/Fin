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
#include "../nodes/ASTNode.hpp"
#include "../types/TypeNode.hpp"
#include <string>
#include <vector>
#include <memory>
#include <utility>

namespace fin {

class StructInstantiation : public Expression {
public:
    std::string struct_name;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields;
    std::vector<std::unique_ptr<TypeNode>> generic_args;
    // The type arguments inference found, when the literal wrote none:
    // `Box<int>` for `Box{ val: 7 }` under a `Box<int>` annotation. The
    // literal half of FunctionCall::resolved_args, under the same
    // record-don't-replace rule and the same spelling guards -- see it, and
    // spellType. The backend instantiates what was recorded where it would
    // otherwise refuse a bare generic name; empty everywhere else.
    std::vector<std::unique_ptr<TypeNode>> resolved_args;
    StructInstantiation(std::string n, 
                        std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f,
                        std::vector<std::unique_ptr<TypeNode>> g = {});
    void accept(Visitor& v) override;
};

class MemberAccess : public Expression {
public:
    std::unique_ptr<Expression> object;
    std::string member;
    // Written `Type::name` rather than `value.name`: the qualifier is a type and
    // the member is an enum member, not a field of an object
    // (tests/samples/literal_interface.fin:19, enums.fin:19, extern_as.fin:44).
    bool is_static = false;
    MemberAccess(std::unique_ptr<Expression> obj, std::string m);
    void accept(Visitor& v) override;
};

class NewExpression : public Expression {
public:
    std::unique_ptr<TypeNode> type;
    std::vector<std::unique_ptr<Expression>> args;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> init_fields;
    NewExpression(std::unique_ptr<TypeNode> t, std::vector<std::unique_ptr<Expression>> a = {});
    NewExpression(std::unique_ptr<TypeNode> t, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f);
    void accept(Visitor& v) override;
};

}
