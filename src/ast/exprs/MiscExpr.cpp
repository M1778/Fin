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

#include "MiscExpr.hpp"
#include "../Visitor.hpp"

namespace fin {

CastExpression::CastExpression(std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> e)
    : target_type(std::move(t)), expr(std::move(e)) {}
void CastExpression::accept(Visitor& v) { v.visit(*this); }

TypeLiteralExpression::TypeLiteralExpression(std::unique_ptr<Statement> d, bool iface)
    : decl(std::move(d)), is_interface(iface) {}
void TypeLiteralExpression::accept(Visitor& v) { v.visit(*this); }

SizeofExpression::SizeofExpression(std::unique_ptr<TypeNode> t) : type_target(std::move(t)) {}
SizeofExpression::SizeofExpression(std::unique_ptr<Expression> e) : expr_target(std::move(e)) {}
void SizeofExpression::accept(Visitor& v) { v.visit(*this); }

TernaryOp::TernaryOp(std::unique_ptr<Expression> c, std::unique_ptr<Expression> t, std::unique_ptr<Expression> f)
    : condition(std::move(c)), true_expr(std::move(t)), false_expr(std::move(f)) {}
void TernaryOp::accept(Visitor& v) { v.visit(*this); }

MatchExpr::MatchExpr(std::unique_ptr<Expression> s, std::vector<MatchArm> a)
    : scrutinee(std::move(s)), arms(std::move(a)) {}
void MatchExpr::accept(Visitor& v) { v.visit(*this); }

// Case 1: super { ... }
SuperExpression::SuperExpression(std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f)
    : init_fields(std::move(f)) {}

// Case 2: super::Parent(...) or super(...)
SuperExpression::SuperExpression(std::string p, std::vector<std::unique_ptr<Expression>> a)
    : parent_name(std::move(p)), args(std::move(a)) {}

// Case 3: super::Parent { ... }
SuperExpression::SuperExpression(std::string p, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f)
    : init_fields(std::move(f)), parent_name(std::move(p)) {}

void SuperExpression::accept(Visitor& v) { v.visit(*this); }

MacroInvocation::MacroInvocation(std::string n, std::vector<std::unique_ptr<Expression>> a)
    : name(std::move(n)), args(std::move(a)) {}
void MacroInvocation::accept(Visitor& v) { v.visit(*this); }

}
