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

#include "UnionType.hpp"
#include "TypeImpl.hpp"

namespace fin {

std::string UnionType::toString() const {
    if (alternatives.empty()) return alias;
    std::string out;
    for (size_t i = 0; i < alternatives.size(); ++i) {
        if (i) out += " | ";
        out += alternatives[i] ? alternatives[i]->toString() : "?";
    }
    return out;
}

bool UnionType::equals(const Type& other) const {
    auto* o = other.as<UnionType>();
    if (!o || o->alias != alias || o->alternatives.size() != alternatives.size())
        return false;
    for (size_t i = 0; i < alternatives.size(); ++i)
        if (!typesEqual(alternatives[i], o->alternatives[i])) return false;
    return true;
}

bool UnionType::isAssignableTo(const Type& other) const {
    // A union is its own type for equality (above) but its first alternative
    // for assignment: that is what the alias resolved to before this class
    // existed, so every value that stored then still stores now.
    if (alternatives.empty()) return false;
    if (!alternatives[0]) return false;
    return alternatives[0]->isAssignableTo(other);
}

TypePtr UnionType::substitute(const TypeMap& mapping, TypePtr selfReplacement) {
    std::vector<TypePtr> newArgs;
    newArgs.reserve(generic_args.size());
    for (const auto& g : generic_args)
        newArgs.push_back(g ? g->substitute(mapping, selfReplacement) : nullptr);
    std::vector<TypePtr> sub;
    sub.reserve(alternatives.size());
    for (const auto& a : alternatives)
        sub.push_back(a ? a->substitute(mapping, selfReplacement) : nullptr);
    auto nu = std::make_shared<UnionType>(alias, std::move(sub));
    nu->generic_args = std::move(newArgs);
    return nu;
}

TypePtr UnionType::instantiate(const std::vector<TypePtr>& concreteArgs) {
    if (concreteArgs.size() != generic_args.size()) return nullptr;
    TypeMap mapping;
    for (size_t i = 0; i < generic_args.size(); ++i) {
        mapping[generic_args[i]->toString()] = concreteArgs[i];
    }
    auto sub = std::dynamic_pointer_cast<UnionType>(substitute(mapping));
    if (!sub) return nullptr;
    // A plain generic alias (`type ArrayType<T> = [T]`) is one alternative, and
    // instantiating it is the alias itself: the substituted target, exactly as
    // a non-generic alias defines its name to. Transparency is what keeps
    // layout and lowering working -- they answer for `[int]` already -- where
    // a one-member union would be refused as a value for Q12's reason.
    if (sub->alternatives.size() == 1) return sub->alternatives[0];
    return sub;
}

TypePtr UnionType::clone() const {
    std::vector<TypePtr> args;
    args.reserve(generic_args.size());
    for (const auto& g : generic_args) args.push_back(g ? g->clone() : nullptr);
    std::vector<TypePtr> copy;
    copy.reserve(alternatives.size());
    for (const auto& a : alternatives) copy.push_back(a ? a->clone() : nullptr);
    auto nu = std::make_shared<UnionType>(alias, std::move(copy));
    nu->generic_args = std::move(args);
    return nu;
}

} // namespace fin
