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

#include "StringEscapes.hpp"

namespace fin::utils {

int hexDigitValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// UTF-8 for one code point, refusing what cannot be encoded.
void appendUtf8(std::string& out, unsigned int cp) {
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        // Surrogates are not code points. `appendUtf8` is called only after the caller has
        // checked, so reaching here with one would be a bug rather than a bad literal.
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

namespace {

// `body` is the literal's bytes without its surrounding quotes.
//
// The loop is one pass and never backs up: every escape consumes what it needs and a
// malformed one consumes only its introducer, so the walk cannot loop or read past the end.
DecodedLiteral decode(const std::string& body) {
    DecodedLiteral result;
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] != '\\' || i + 1 >= body.size()) {
            result.text += body[i];
            continue;
        }
        const char c = body[++i];
        switch (c) {
            case 'n': result.text += '\n'; break;
            case 't': result.text += '\t'; break;
            case 'r': result.text += '\r'; break;
            case '0': result.text += '\0'; break;
            case '\\': result.text += '\\'; break;
            case '\'': result.text += '\''; break;
            case '"': result.text += '"'; break;
            case 'x': {
                // Exactly two hex digits, so `"\x9"` is malformed rather than the byte 9 --
                // a single-digit form is how a following hex-looking character gets eaten.
                if (i + 2 < body.size()) {
                    const int hi = hexDigitValue(body[i + 1]);
                    const int lo = hexDigitValue(body[i + 2]);
                    if (hi >= 0 && lo >= 0) {
                        result.text += static_cast<char>((hi << 4) | lo);
                        i += 2;
                        break;
                    }
                }
                result.ok = false;
                result.text += c;
                break;
            }
            case 'u': {
                unsigned int cp = 0;
                bool good = false;
                if (i + 1 < body.size() && body[i + 1] == '{') {
                    // `\u{1F600}`: one to six digits, which is how a code point above the
                    // BMP is written without a surrogate pair.
                    std::size_t j = i + 2;
                    unsigned int value = 0;
                    int digits = 0;
                    while (j < body.size() && body[j] != '}' && digits < 6) {
                        const int d = hexDigitValue(body[j]);
                        if (d < 0) break;
                        value = (value << 4) | static_cast<unsigned int>(d);
                        ++j;
                        ++digits;
                    }
                    if (digits > 0 && j < body.size() && body[j] == '}' && value <= 0x10FFFF &&
                        !(value >= 0xD800 && value <= 0xDFFF)) {
                        cp = value;
                        i = j;
                        good = true;
                    }
                } else if (i + 4 < body.size()) {
                    unsigned int value = 0;
                    bool allHex = true;
                    for (int k = 1; k <= 4; ++k) {
                        const int d = hexDigitValue(body[i + static_cast<std::size_t>(k)]);
                        if (d < 0) {
                            allHex = false;
                            break;
                        }
                        value = (value << 4) | static_cast<unsigned int>(d);
                    }
                    // A surrogate pair written as two `\u` escapes is not assembled: an
                    // unpaired half cannot be encoded, and pairing them would need lookahead
                    // the single pass does not do. `\u{...}` is the spelling that works.
                    if (allHex && !(value >= 0xD800 && value <= 0xDFFF)) {
                        cp = value;
                        i += 4;
                        good = true;
                    }
                }
                if (good) {
                    appendUtf8(result.text, cp);
                } else {
                    result.ok = false;
                    result.text += c;
                }
                break;
            }
            // An unknown escape keeps the character it introduced rather than the backslash,
            // which is the rule this project already shipped.
            default: result.text += c; break;
        }
    }
    return result;
}

}  // namespace

DecodedLiteral decodeStringLiteral(const std::string& body) { return decode(body); }

std::string decodeEscapes(const std::string& body) { return decode(body).text; }

}  // namespace fin::utils
