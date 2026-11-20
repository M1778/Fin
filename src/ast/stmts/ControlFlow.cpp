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

#include "ControlFlow.hpp"
#include "../Visitor.hpp"

namespace fin {

ReturnStatement::ReturnStatement(std::unique_ptr<Expression> v) : value(std::move(v)) {}
void ReturnStatement::accept(Visitor& v) { v.visit(*this); }

IfStatement::IfStatement(std::unique_ptr<Expression> c, std::unique_ptr<Block> t, std::unique_ptr<Statement> e)
    : condition(std::move(c)), then_block(std::move(t)), else_stmt(std::move(e)) {}
void IfStatement::accept(Visitor& v) { v.visit(*this); }

WhileLoop::WhileLoop(std::unique_ptr<Expression> c, std::unique_ptr<Block> b)
    : condition(std::move(c)), body(std::move(b)) {}
void WhileLoop::accept(Visitor& v) { v.visit(*this); }

ForLoop::ForLoop(std::unique_ptr<Statement> i, std::unique_ptr<Expression> c, std::unique_ptr<Expression> inc, std::unique_ptr<Block> b)
    : init(std::move(i)), condition(std::move(c)), increment(std::move(inc)), body(std::move(b)) {}
void ForLoop::accept(Visitor& v) { v.visit(*this); }

ForeachLoop::ForeachLoop(std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> i, std::unique_ptr<Block> b)
    : var_name(std::move(n)), var_type(std::move(t)), iterable(std::move(i)), body(std::move(b)) {}
void ForeachLoop::accept(Visitor& v) { v.visit(*this); }

void BreakStatement::accept(Visitor& v) { v.visit(*this); }
void ContinueStatement::accept(Visitor& v) { v.visit(*this); }

}
