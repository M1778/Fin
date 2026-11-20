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
#include <vector>
#include <memory>

namespace fin {

class ArrayLiteral : public Expression {
public:
    std::vector<std::unique_ptr<Expression>> elements;
    // The type inference found: `[int, 4]` for `[1, 2, 3, 4]`, fixed with the
    // extent the literal states by stating its elements. The array half of
    // FunctionCall::resolved_args, under the same record-don't-replace rule
    // and the same spelling guards -- see it, and spellType. The backend
    // builds what was recorded where no declaration set a hint (a comparison
    // operand, a ternary arm); a hint still wins where one is set, and empty
    // where there is nothing to spell.
    std::unique_ptr<TypeNode> resolved_type;
    ArrayLiteral(std::vector<std::unique_ptr<Expression>> e);
    void accept(Visitor& v) override;
};

class ArrayAccess : public Expression {
public:
    std::unique_ptr<Expression> array;
    std::unique_ptr<Expression> index;
    ArrayAccess(std::unique_ptr<Expression> a, std::unique_ptr<Expression> i);
    void accept(Visitor& v) override;
};

}
