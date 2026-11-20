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
#include "../nodes/Parameter.hpp"
#include "../stmts/Statement.hpp" // For Block
#include "../types/TypeNode.hpp"
#include "../types/GenericParam.hpp"
#include "../types/Attribute.hpp"
#include <vector>
#include <string>
#include <memory>

namespace fin {
class Expression;

class FunctionDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<TypeNode> return_type;
    std::unique_ptr<Block> body;
    bool is_public = false;
    bool is_static = false;
    std::vector<std::unique_ptr<GenericParam>> generic_params;
    std::vector<std::unique_ptr<Attribute>> attributes;

    FunctionDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class OperatorDeclaration : public Statement {
public:
    ASTTokenKind op;
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<TypeNode> return_type;
    std::unique_ptr<Block> body;
    bool is_public = false;
    std::vector<std::unique_ptr<GenericParam>> generic_params;

    std::unique_ptr<TypeNode> implements_type;
    // `operator[] implements cast<fn(Self, T)>(__get)` --
    // tests/samples/stdlib/hashmap.fin:50. The operator is implemented by an
    // expression rather than by a body or by a type: the cast holds both the
    // signature and the function bound to the operator. Set instead of
    // `implements_type`, never alongside it.
    std::unique_ptr<Expression> implements_expr;

    OperatorDeclaration(ASTTokenKind o, std::vector<std::unique_ptr<Parameter>> p, 
                        std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b, bool pub);
    void accept(Visitor& v) override;
};

// One entry of a constructor's member-initialiser list: `age(10)` in
// `Person():age(10), name("Hello")` (tests/samples/deeptest2.fin:40). The
// arguments are a list because a member can be built from more than one.
struct MemberInit {
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
};

class ConstructorDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<Block> body;
    std::unique_ptr<TypeNode> return_type;
    // `Person():age(10), name("Hello") { ... }` -- tests/samples/deeptest2.fin:40.
    // Empty for a constructor written without a member-initialiser list.
    std::vector<MemberInit> member_inits;
    
    ConstructorDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<Block> b, std::unique_ptr<TypeNode> rt = nullptr);
    void accept(Visitor& v) override;
};

class DestructorDeclaration : public Statement {
public:
    std::string name;
    std::unique_ptr<Block> body;
    
    DestructorDeclaration(std::string n, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

}
