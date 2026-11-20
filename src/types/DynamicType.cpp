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

#include "DynamicType.hpp"
#include "TypeImpl.hpp"

namespace fin {

std::string DynamicType::toString() const {
    if (bounds.empty()) return name;
    // Rendered the way the corpus writes it, so a diagnostic quoting the type is a
    // string the reader can search for in their own source.
    std::string out = name + " implements <";
    for (size_t i = 0; i < bounds.size(); ++i) {
        if (i) out += ", ";
        out += bounds[i] ? bounds[i]->toString() : "?";
    }
    return out + ">";
}

bool DynamicType::equals(const Type& other) const {
    auto* o = other.as<DynamicType>();
    if (!o || o->name != name || o->bounds.size() != bounds.size()) return false;
    for (size_t i = 0; i < bounds.size(); ++i)
        if (!typesEqual(bounds[i], o->bounds[i])) return false;
    return true;
}

TypePtr DynamicType::substitute(const TypeMap& mapping, TypePtr selfReplacement) {
    // The bounds are types and can name a generic parameter -- `type Any<...> = any
    // implements <...>` is instantiated per use -- so they are substituted rather
    // than copied. The name never is: `any` does not become the type it erased.
    std::vector<TypePtr> sub;
    sub.reserve(bounds.size());
    for (const auto& b : bounds)
        sub.push_back(b ? b->substitute(mapping, selfReplacement) : nullptr);
    return std::make_shared<DynamicType>(name, std::move(sub));
}

TypePtr DynamicType::clone() const {
    std::vector<TypePtr> copy;
    copy.reserve(bounds.size());
    for (const auto& b : bounds) copy.push_back(b ? b->clone() : nullptr);
    return std::make_shared<DynamicType>(name, std::move(copy));
}

} // namespace fin
