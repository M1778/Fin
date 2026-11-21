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

namespace fin::utils {

// Decodes the escape sequences of a Fin string or char literal.
//
// ONE DECODER, because there were two copies of the same switch -- one in codegen, one in
// the comptime interpreter -- and a table duplicated is a table that drifts. The codegen copy
// saw more traffic, so a literal that behaved one way at run time could behave another way
// under `comptime` evaluation of the same source.
//
// Supported: \n \t \r \0 \\ \' \" \xNN \uNNNN \u{NNNNNN}
//
// `\xNN` is a raw byte, so `"\xc3\xa9"` is the two UTF-8 bytes of e-acute and `len` answers 2.
// That is what the issue asked for: the byte form is the one that lets a source file spell
// UTF-8 without depending on the editor's encoding, which is why every other systems language
// with this escape has it.
//
// `\uNNNN` is a code POINT, encoded to UTF-8 -- not the two-byte number written literally.
// `"\u00e9"` is e-acute as two bytes. An unpaired surrogate is refused: it cannot be encoded,
// and substituting U+FFFD would be a silent wrong answer.
//
// An unknown escape keeps its character and drops the backslash, which is the rule this
// project already shipped and the corpus depends on. A malformed `\x`/`\u` (too few hex
// digits, an out-of-range value) follows the same rule rather than producing half a
// character -- and `ok` reports it so a caller that wants to refuse can.
struct DecodedLiteral {
    std::string text;
    // False when a `\x` or `\u` escape was malformed. The text still holds the same
    // keep-the-character decoding so a caller that does not care keeps today's behaviour.
    bool ok = true;
};

int hexDigitValue(char c);
void appendUtf8(std::string& out, unsigned int codePoint);

DecodedLiteral decodeStringLiteral(const std::string& body);

// Convenience for the many callers that only want the bytes.
std::string decodeEscapes(const std::string& body);

}  // namespace fin::utils
