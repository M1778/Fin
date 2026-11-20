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

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <variant>
#include "location.hh"
#include "../NodeKind.hpp"

namespace fin {

class Visitor;

enum class ASTTokenKind {
    INTEGER, FLOAT, STRING_LITERAL, CHAR_LITERAL, BOOL, KW_NULL,
    // `m1778` is an expression meaning "not implemented" and nothing else
    // (ADR 0001). It is a Literal kind rather than an Identifier so it never
    // resolves against a scope: `blame m1778;` is the canonical idiom across
    // the standard library and must not depend on a variable of that name.
    M1778,
    PLUS, MINUS, MULT, DIV, MOD,
    PLUSEQUAL, MINUSEQUAL, MULTEQUAL, DIVEQUAL,
    AND, OR, NOT,
    EQUAL, EQEQ, NOTEQ, LT, GT, LTEQ, GTEQ,
    AMPERSAND,
    INCREMENT, DECREMENT,
    ARROW, RARROW,
    QUESTION,
    TILDE,
    // Extended operators
    PIPE, CARET, SHIFTLEFT, SHIFTRIGHT, SHIFTLEFTEQUAL, SHIFTRIGHTEQUAL,
    // `operator %=`, `operator &=` and `operator |=` -- declared by
    // tests/samples/stdlib/operators.fin:71,77,83. The other seven compound
    // assignments were already kinds; these three were missing, so the three
    // operators could not be named.
    MODEQUAL, AMPERSANDEQUAL, PIPEEQUAL,
    // `...objects` -- forwarding a variadic parameter into another call
    // (tests/samples/stdlib/stdio.fin:36, `format!(fmt, ...objects)`). A UnaryOp
    // kind because the spread applies to one operand.
    SPREAD,
    INDEX, INDEX_ASSIGN, DEREF, UNARY_MINUS, VARIADIC_CALL,
    UNKNOWN
};

class ASTNode {
public:
    fin::location loc;
    // The file whose source this node was written in, when that is not the
    // file being compiled: set only on clones of an imported macro's quote
    // body, whose locations are in the defining module's coordinates while the
    // engine rendering them holds the caller's source. Empty everywhere else,
    // which is every node a programmer wrote directly.
    std::string originFile;
    virtual ~ASTNode() = default;
    virtual void accept(Visitor& v) = 0;
    void setLoc(const fin::location& l) { loc = l; }

    // The kind discriminator (ADR 0004).  Resolved from the dynamic type through
    // the registry generated from FIN_NODE_LIST, which is why no node class
    // carries a `kind()` override: registration happens in one place
    // (src/ast/NodeKind.hpp) instead of 57.
    //
    // `NodeKind::Unknown` means the node's C++ type was never registered.
    NodeKind kind() const noexcept;
};

class Expression : public ASTNode {
public:
    // True when the source wrapped this expression in parentheses. Recorded by
    // the one parenthesised-primary rule (parser.y) rather than kept as a node,
    // so the tree shape is unchanged: only the issue-#41 condition check reads
    // it, as the "the assignment was intentional" escape.
    bool parenthesized = false;
};
class Statement : public ASTNode {};

} // namespace fin
