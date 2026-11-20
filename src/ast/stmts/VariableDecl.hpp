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
#include <memory>

namespace fin {

class Expression;
class Attribute;

class VariableDeclaration : public Statement {
public:
    bool is_mutable;
    bool is_public = false;
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expression> initializer;
    std::vector<std::unique_ptr<Attribute>> attributes;
    // fin-guard: the parser accepted `let`/`const <name> = ...` with no
    // annotation and spelled the type as `<auto>`; semantics warns (or errors
    // when the guard is off) on this flag rather than on the type, so an
    // explicit `<name> <auto> = ...` stays silent.
    bool finGuardRewritten = false;

    VariableDeclaration(bool mut, std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> init);
    void accept(Visitor& v) override;
};

class DeleteStatement : public Statement {
public:
    std::unique_ptr<Expression> expr;
    DeleteStatement(std::unique_ptr<Expression> e);
    void accept(Visitor& v) override;
};

class ExpressionStatement : public Statement {
public:
    std::unique_ptr<Expression> expr;
    ExpressionStatement(std::unique_ptr<Expression> e);
    void accept(Visitor& v) override;
};

}
