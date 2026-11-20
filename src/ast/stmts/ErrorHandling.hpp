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
#include "Statement.hpp"
#include "../types/TypeNode.hpp"
#include <string>
#include <memory>

namespace fin {

class TryCatch : public Statement {
public:
    std::unique_ptr<Block> try_block;
    std::string catch_var;
    std::unique_ptr<TypeNode> catch_type;
    std::unique_ptr<Block> catch_block;
    TryCatch(std::unique_ptr<Block> t, std::string cv, std::unique_ptr<TypeNode> ct, std::unique_ptr<Block> cb);
    void accept(Visitor& v) override;
};

class BlameStatement : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> message;
    BlameStatement(std::unique_ptr<Expression> c, std::unique_ptr<Expression> m = nullptr);
    void accept(Visitor& v) override;
};

}
