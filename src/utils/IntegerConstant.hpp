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

#include <cstdint>

// Reading an integer constant out of the AST, for the two passes that need one.
//
// It lives here rather than inside either of them because both read the *same*
// constants and must agree about them: the analyzer decides `[int, 5]`'s extent
// and whether `a[7]` is inside it, and the backend decides how many elements to
// allocate and which one a GEP lands on. Two readers that agree today are two
// readers that disagree after one edit, and the disagreement is a program that
// compiles and indexes past its own array.

namespace fin {

class ASTNode;

// Whether `node` is an integer constant, and whether it is negative.
//
// Fin spells a negative constant as a UnaryOp over a Literal -- the lexer never
// produces a signed INTEGER token (parser.y:2139) -- so this is answerable from
// the syntax alone, with no evaluation and no constant folder.
//
// `1 + 1` is deliberately *not* a constant. Whether an int-typed expression
// converts to an unsigned type is a language decision rather than a defect, and
// KnownDefect_IntegerConstants holds it open; folding arithmetic would answer it
// by accident for the subset that happens to be foldable. A `const` variable is
// not read either: a `const` in Fin is a variable whose mutability is checked,
// not a compile-time value the type system may substitute.
bool integerConstant(const ASTNode& node, bool& negative);

// Why a constant could not be read as a count, so the caller can say which.
enum class ConstantRead { Ok, NotConstant, Negative, TooLarge };

// The magnitude of a non-negative integer constant.
//
// A Literal's `value` is the text it was spelled with, which {DIGIT}+ in the lexer
// makes pure digits with no suffix and no separators. Accumulated a digit at a
// time rather than through stoull, which stops at the first character it does not
// like and reports success for the prefix: a spelling this reader does not know
// would come back as a *number*, and a number is what the layout pass and the
// backend both believe.
ConstantRead readConstant(const ASTNode& node, uint64_t& out);

// The value of a *signed* integer constant, for the one caller whose constants may
// be negative: an enum's written member values, which the analyzer checks against
// `int` (`enum Sign { Neg = -1 }`). An extent cannot be negative and an index that
// is has its own diagnostic, so neither of those uses this.
//
// Never returns ConstantRead::Negative -- a negative constant is the answer here
// rather than a reason to refuse. TooLarge covers a magnitude that does not fit an
// int64, in either direction.
ConstantRead readSignedConstant(const ASTNode& node, int64_t& out);

}  // namespace fin
