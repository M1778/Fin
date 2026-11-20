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

// The type of `compiler` and of everything reached through it.
//
// One type with a path rather than three types, because the three layers differ
// only in what a member access off them means and a walk that switches on a string
// is the same walk either way. The paths, and they are the whole of ADR 0012's
// split:
//
//   ""                  the `compiler` object, granted by `#[use(compiler)]`
//   "components"        the grant layer
//   "components.<name>" one component reference -- `present()`, `version()`,
//                       `granted()`, `name()`. Exists whether or not the compiler
//                       has the component (§2.1a).
//   "<name>"            one component's operations layer, reachable only with the
//                       matching `#[use(compiler.components.<name>)]`
//
// Not a NamespaceType: a namespace holds a Scope of Symbols, and these members are
// a static table with grant checking on the way through, not symbols anyone can
// define. Nothing is assignable to it and it is assignable to nothing -- storing
// `compiler.types` in a variable is not a thing the corpus does and not a thing
// this buys.
class CompilerApiType : public Type {
public:
    std::string path;
    explicit CompilerApiType(std::string p = "") : path(std::move(p)) {}
    std::string toString() const override {
        return path.empty() ? "compiler" : "compiler." + path;
    }
    bool equals(const Type& other) const override {
        if (auto* o = other.as<CompilerApiType>()) return path == o->path;
        return false;
    }
    TypePtr substitute(const TypeMap&, TypePtr = nullptr) override {
        return std::make_shared<CompilerApiType>(path);
    }
    TypePtr clone() const override { return std::make_shared<CompilerApiType>(path); }
};

} // namespace fin
