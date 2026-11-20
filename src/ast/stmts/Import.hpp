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
#include "../types/Attribute.hpp"
#include <string>
#include <vector>

namespace fin {

class ImportModule : public Statement {
public:
    std::string source;
    bool is_package;
    std::string alias;
    std::vector<std::string> targets;
    // The `::`-separated tail of a module path: `import { Error } from error::std`
    // names the module `error` and the namespace `std` inside it
    // (tests/samples/importing.fin:14 states this). Empty when the path was
    // written without `::`.
    std::string namespace_path;
    // `#[stdimport]` above an import. An attribute attaches to an import the
    // same way it attaches to a declaration; four stdlib samples open with one.
    std::vector<std::unique_ptr<Attribute>> attributes;
    // Set once every name this import brings in has been bound into the importing
    // scope. An import is a compile-time name-binding directive and nothing else,
    // so once it has bound its names it has no further meaning -- which is why the
    // backend refuses one that reaches it (`an import (the module loader did not
    // consume it)`). The front end is what takes it out of the tree, and it does so
    // only on this flag: an import that named a module or a symbol that does not
    // exist stays in the tree behind its own diagnostic, because a construct the
    // compiler could not handle is refused and never quietly dropped.
    bool consumed = false;
    ImportModule(std::string src, bool pkg, std::string al, std::vector<std::string> tgts);
    void accept(Visitor& v) override;
};

}
