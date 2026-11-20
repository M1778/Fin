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

#include "FunctionType.hpp"
#include "TypeImpl.hpp"

namespace fin {

std::string FunctionType::toString() const {
    std::string s = "fn(";
    for (size_t i = 0; i < param_types.size(); ++i) {
        s += param_types[i]->toString();
        if (i < param_types.size() - 1) s += ", ";
    }
    if (is_vararg) { if (!param_types.empty()) s += ", "; s += "..."; }
    s += ") -> " + return_type->toString();
    return s;
}
bool FunctionType::equals(const Type& other) const {
    auto* o = other.as<FunctionType>();
    if (!o) return false;
    if (is_vararg != o->is_vararg) return false;
    // `param_defaults` deliberately does not participate. A default is a fact about the
    // declaration, not about the type: `fn(int) -> int` is one type whether or not the
    // function behind it wrote `= 2`, and a value of that type is assignable to it
    // either way. Comparing them would make two identically-typed functions
    // unassignable to the same variable over a difference no call site can observe
    // except by omitting an argument, which is arity and is checked separately.
    // Soundness_ParameterDefaults.ADefaultIsNotPartOfTheFunctionType pins this.
    if (param_types.size() != o->param_types.size()) return false;
    if (!typesEqual(return_type, o->return_type)) return false;
    for (size_t i = 0; i < param_types.size(); ++i) {
        if (!typesEqual(param_types[i], o->param_types[i])) return false;
    }
    return true;
}
// Both of these carry `param_defaults` through unchanged, and that is the whole reason
// the field is a vector of flags rather than anything richer: substituting a generic
// parameter changes what a parameter's *type* is and cannot change whether one was
// written with a default, so there is nothing per-entry to transform. A generic method
// with a defaulted parameter keeps it optional in every instantiation
// (Soundness_ParameterDefaults.AGenericMethodsDefaultSurvivesInstantiation), which is
// what would silently break if either of these dropped the field -- and dropped is what
// omitting the fourth argument means, since it defaults to empty.
TypePtr FunctionType::clone() const {
    std::vector<TypePtr> newParams;
    for (auto& p : param_types) newParams.push_back(p->clone());
    return std::make_shared<FunctionType>(newParams, return_type->clone(), is_vararg,
                                          param_defaults);
}
TypePtr FunctionType::substitute(const TypeMap& mapping, TypePtr selfReplacement) {
    std::vector<TypePtr> newParams;
    for (auto& p : param_types) newParams.push_back(p->substitute(mapping, selfReplacement));
    return std::make_shared<FunctionType>(newParams, return_type->substitute(mapping, selfReplacement),
                                          is_vararg, param_defaults);
}

}
