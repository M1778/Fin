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
#include "parser.hpp" 

struct yy_buffer_state;
typedef yy_buffer_state* YY_BUFFER_STATE;

// Flex functions (Global)
fin::parser::symbol_type yylex();
YY_BUFFER_STATE yy_scan_string(const char *str);
// Byte-counted, so source holding an embedded NUL is scanned whole. `yy_scan_string`
// is `yy_scan_bytes(s, strlen(s))`, which ends the input at the first NUL.
YY_BUFFER_STATE yy_scan_bytes(const char *bytes, int len);
void yy_delete_buffer(YY_BUFFER_STATE buffer);

// Helper functions (Namespaced)
namespace fin {
    void reset_lexer_location();
}