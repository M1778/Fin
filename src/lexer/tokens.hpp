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
#include <variant>

namespace fin {

enum class TokenKind {
    END_OF_FILE = 0,

    // --- Keywords ---
    KW_LET, KW_CONST, KW_AUTO,
    KW_FUN, KW_RETURN,
    KW_PUB, KW_PRIV,
    KW_STRUCT, KW_ENUM, KW_INTERFACE,
    KW_MACRO, KW_STATIC, KW_NULL,
    KW_WHILE, KW_FOR, KW_FOREACH, KW_BREAK, KW_CONTINUE,
    KW_IF, KW_ELSE, KW_ELSEIF, KW_IN,
    KW_TRY, KW_CATCH, KW_BLAME, KW_SUPER, KW_SELF_TYPE, // 'Self'
    KW_IMPORT, KW_AS, KW_FROM,
    KW_NEW, KW_DELETE, KW_SIZEOF, KW_TYPEOF, KW_AS_PTR,
    KW_STD_CONV, KW_OPERATOR,
    KW_SPECIAL, KW_FN_TYPE, KW_DEFINE,
    KW_M1778, // Signature token

    // --- Primitive Types ---
    TYPE_INT, TYPE_FLOAT, TYPE_DOUBLE, TYPE_BOOL, 
    TYPE_STRING, TYPE_CHAR, TYPE_VOID, TYPE_LONG,

    // --- Punctuation ---
    LPAREN, RPAREN,       // ( )
    LBRACE, RBRACE,       // { }
    LBRACKET, RBRACKET,   // [ ]
    SEMICOLON, COLON,     // ; :
    DOUBLE_COLON,         // ::
    COMMA, DOT, ARROW,    // , . =>
    ELLIPSIS,             // ...
    AT, DOLLAR, HASH,     // @ $ #

    // --- Operators ---
    PLUS, MINUS, MULT, DIV, MOD,
    EQUAL,                // =
    EQEQ, NOTEQ,          // == !=
    LT, GT, LTEQ, GTEQ,   // < > <= >=
    AND, OR, NOT,         // && || !
    AMPERSAND,            // &
    INCREMENT, DECREMENT, // ++ --
    PLUSEQUAL, MINUSEQUAL, MULTEQUAL, DIVEQUAL, // += -= *= /=

    // --- Literals ---
    IDENTIFIER,
    INTEGER,
    FLOAT,
    STRING_LITERAL,
    CHAR_LITERAL,

    // --- Error ---
    UNKNOWN
};

// A structure to pass data from Flex to C++
struct Token {
    TokenKind kind;
    std::string text;     // Raw lexeme text
    int line;
    int column;
};

// Helper to get a string representation of the token kind (for debugging)
inline const char* tokenKindToString(TokenKind k) {
    switch(k) {
        case TokenKind::KW_LET: return "LET";
        case TokenKind::KW_FUN: return "FUN";
        case TokenKind::IDENTIFIER: return "IDENTIFIER";
        case TokenKind::INTEGER: return "INTEGER";
        case TokenKind::LBRACE: return "LBRACE";
        case TokenKind::RBRACE: return "RBRACE";
        // ... add more as needed for debug
        default: return "TOKEN";
    }
}

} // namespace fin
