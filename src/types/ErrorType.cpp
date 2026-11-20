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

#include "TypeImpl.hpp"

namespace fin {

const TypePtr& errorType() {
    static const TypePtr instance = std::make_shared<ErrorType>();
    return instance;
}

bool isErrorType(const TypePtr& t) {
    if (!t) return false;
    if (t->as<ErrorType>()) return true;
    if (auto* p = t->as<PointerType>()) return isErrorType(p->pointee);
    if (auto* a = t->as<ArrayType>()) return isErrorType(a->element_type);
    if (auto* n = t->as<NullableType>()) return isErrorType(n->inner);
    // A prototype whose key or value did not type. `{ nosuchvar : 1 }` is typed
    // `<{<error>, int}>` for the same reason an array literal is typed `[<error>]`,
    // and without this the container boundary stopped the suppression: checkType
    // compared `<{<error>, int}>` against the annotation, found no match, and printed
    // the sentinel's own spelling into a diagnostic.
    if (auto* p = t->as<PrototypeType>()) return isErrorType(p->keyType) || isErrorType(p->valueType);
    // A function whose return type did not resolve is registered as
    // `fn(int) -> <error>` (Analyzer_Decl.cpp step 6 puts the sentinel there rather
    // than dropping the whole declaration). Naming that function as a value then
    // reached checkType wrapped, and `let g <int> = f;` printed
    // `expected 'int', got 'fn(int) -> <error>'`.
    if (auto* f = t->as<FunctionType>()) {
        if (isErrorType(f->return_type)) return true;
        for (const auto& p : f->param_types) if (isErrorType(p)) return true;
    }
    // A union with an alternative that did not resolve: the undefined member
    // was already reported where it was written, so the alias suppresses like
    // any other declaration carrying the sentinel.
    if (auto* u = t->as<UnionType>()) {
        for (const auto& a : u->alternatives) if (isErrorType(a)) return true;
    }
    return false;
}

} // namespace fin
