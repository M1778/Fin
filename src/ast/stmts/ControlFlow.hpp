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
#include "Statement.hpp" // For Block
#include "../types/TypeNode.hpp" // For ForeachLoop type
#include <memory>
#include <string>

namespace fin {

class ReturnStatement : public Statement {
public:
    std::unique_ptr<Expression> value;
    ReturnStatement(std::unique_ptr<Expression> v);
    void accept(Visitor& v) override;
};

class IfStatement : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> then_block;
    std::unique_ptr<Statement> else_stmt; 
    IfStatement(std::unique_ptr<Expression> c, std::unique_ptr<Block> t, std::unique_ptr<Statement> e);
    void accept(Visitor& v) override;
};

class WhileLoop : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> body;
    // `do { ... } while (c);` rather than `while (c) { ... }`. The two forms
    // share every field and differ only in whether the first pass tests the
    // condition, so this is a bit and not a class: a DoWhileLoop would need an
    // `accept` override in src/ast/Visitor.hpp, which wave 2 does not own.
    bool is_do_while = false;
    WhileLoop(std::unique_ptr<Expression> c, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class ForLoop : public Statement {
public:
    std::unique_ptr<Statement> init;
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> increment;
    std::unique_ptr<Block> body;
    ForLoop(std::unique_ptr<Statement> i, std::unique_ptr<Expression> c, std::unique_ptr<Expression> inc, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class ForeachLoop : public Statement {
public:
    std::string var_name;
    std::unique_ptr<TypeNode> var_type;
    std::unique_ptr<Expression> iterable;
    std::unique_ptr<Block> body;
    // The optional index binding of `foreach (idx <int>, element <int> in a)`
    // (tests/samples/loops.fin:19). Empty name means the one-binding form.
    std::string index_name;
    std::unique_ptr<TypeNode> index_type;
    ForeachLoop(std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> i, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class BreakStatement : public Statement {
public:
    void accept(Visitor& v) override;
};

class ContinueStatement : public Statement {
public:
    void accept(Visitor& v) override;
};

}
