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
#include <memory>

namespace fin {

class UnaryOp : public Expression {
public:
    ASTTokenKind op;
    std::unique_ptr<Expression> operand;
    // Was the operator written *after* its operand? A fact about the source text,
    // set by whichever production matched, never inferred from `op`.
    //
    // `++` and `--` are the only operators the grammar spells in both positions, and
    // they are the reason this exists: `i++` and `++i` were the same node, so a
    // consumer that needed the expression's value -- the old value for one, the new
    // value for the other -- had nothing to read and the backend refused both. In
    // statement position the two are identical, which is why eight increments in the
    // corpus never noticed.
    //
    // Set truthfully for every operator rather than only for those two. `?` is
    // always postfix and `!` is always prefix, so neither flag can decide anything,
    // but a field that is only meaningful for some operators is a field every reader
    // has to know the exceptions to. Default false, because prefix is the position
    // that reads correctly in a statement.
    bool is_postfix = false;
    UnaryOp(ASTTokenKind o, std::unique_ptr<Expression> e, bool postfix = false);
    void accept(Visitor& v) override;
};

}
