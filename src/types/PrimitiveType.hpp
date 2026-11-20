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
#include "Type.hpp"

namespace fin {

class PrimitiveType : public Type {
public:
    std::string name;

    // The width the source wrote inside the type, and 0 for "none written".
    //
    // `int{64}` is a written width and it *is* the type (ADR 0022), so the bits
    // live on the type rather than beside it. Before this field the annotation was
    // walked for its own diagnostics in resolveTypeUnwrapped and then dropped, so
    // `int{8}` and `int` were one semantic type -- which is why a narrowing
    // assignment could not be refused (there was nothing narrower to refuse), why
    // the layout pass answered four bytes for a one-byte field, and why a
    // diagnostic reading `expected 'uint'` was shown to someone who had written
    // `uint{8}`. All three are the same missing number, and this is where it goes.
    //
    // Not encoded into `name`, and that is the load-bearing half of the decision.
    // Six sites compare a type's spelling against a bare name -- `toString() ==
    // "auto"` in Type.cpp twice, `"void"` in PointerType.cpp twice,
    // Analyzer_Decl.cpp:41 -- and two
    // more use a spelling as a substitution key (StructType.cpp:213,
    // Analyzer_Expr.cpp:1258). A name of `int{64}` would silently fail every one
    // of them, so `name` stays what the base name was and `toString()` is what
    // grows.
    //
    // 0 rather than std::optional because a zero-bit integer is not a thing the
    // grammar can hand over: `int{0}` is a front-end diagnostic ("A bit width
    // cannot be zero"), so the sentinel cannot collide with a real width. What a
    // width means for a name that is not an integer -- `float{128}`, `bool{1}` --
    // is nothing: the front end drops it and `scalarOf` drops it again, so the two
    // agree without either trusting the other.
    unsigned bits = 0;

    PrimitiveType(std::string n) : name(std::move(n)) {}
    // `int{64}`. The width the source wrote, whether or not it is one the name
    // already means: `int{32}` is `int` and says so through equals(), not by
    // being normalised away on the way in, so a diagnostic can name the spelling
    // the program used.
    PrimitiveType(std::string n, unsigned width) : name(std::move(n)), bits(width) {}

    std::string toString() const override;
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap&, TypePtr = nullptr) override {
        return std::make_shared<PrimitiveType>(name, bits);
    }
    TypePtr clone() const override { return std::make_shared<PrimitiveType>(name, bits); }
    bool isAssignableTo(const Type& other) const override;
};

}
