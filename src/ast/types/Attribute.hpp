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
#include <string>
#include <vector>

namespace fin {

class Statement;

// The attribute that makes a declaration visible with no import (ADR 0021's
// ruling). Spelled once, here, because the parser marks it, the analyzer refuses
// a misplaced one and the module loader harvests it -- three passes that have to
// agree on a string.
inline constexpr const char* kGlobalAttribute = "global";

class Attribute : public ASTNode {
public:
    std::string name;
    std::string value_str;
    bool is_flag = true;

    // Whether this attribute was written inside a `namespace std { ... }` block.
    //
    // It lives on the attribute rather than on the declaration because the parser
    // is the only pass that can answer it: `namespace_block` splices its contents
    // into the enclosing statement list and discards the name (parser.y), so by
    // the time the analyzer sees a declaration there is nothing left to say which
    // namespace it was written in. The parser sets this while the name is still in
    // hand; whoever enforces a namespace-restricted attribute reads it.
    bool std_scoped = false;

    Attribute(std::string n, bool flag);
    Attribute(std::string n, std::string v);
    void accept(Visitor& v) override;
};

// Marks every `#[global]` reachable from `body` as written inside a `namespace
// std`. Called from the `namespace_block` production for a block named `std`.
//
// Deep rather than one level, because the grammar accepts an attribute on a
// declaration anywhere a statement is accepted -- including inside a function
// body -- so a rule enforced only over a namespace's immediate statements would
// let a nested one through. The traversal is StructuralWalk's, which is the one
// place that knows every node's children (ADR 0004).
void markStdScopedGlobals(const std::vector<std::unique_ptr<Statement>>& body);

// The names every `namespace_block` declared in the parse now running, in
// source order. The blocks splice their contents and discard their names
// (parser.y), so the analyzer would otherwise never know which namespaces a
// file declares -- and both halves of the #90 strict rule need exactly that:
// an import's `::` tail is checked against the *module's* names, an extern's
// qualifier against the *importing file's*.
//
// Collected rather than stamped: `markStdScopedGlobals` above can walk a
// block's statements because it only needs what is inside one block, but the
// names live one per block across the whole file, so they accumulate here and
// the `program` production moves them onto the Program node in one go.
// Cleared at every parse start (`%initial-action` in parser.y): parses are
// sequential but many run per process (one per imported module), and a failed
// parse must not leak its names into the next file's Program.
void noteNamespaceName(const std::string& name);
std::vector<std::string> takeNamespaceNames();

} // namespace fin
