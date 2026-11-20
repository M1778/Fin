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

#include "PointerType.hpp"
#include "TypeImpl.hpp"

namespace fin {

std::string PointerType::toString() const { return "&" + pointee->toString(); }
bool PointerType::equals(const Type& other) const {
    if (auto* o = other.as<PointerType>()) return typesEqual(pointee, o->pointee);
    return false;
}
TypePtr PointerType::clone() const { return std::make_shared<PointerType>(pointee->clone()); }
TypePtr PointerType::substitute(const TypeMap& mapping, TypePtr selfReplacement) { 
    return std::make_shared<PointerType>(pointee->substitute(mapping, selfReplacement)); 
}
bool PointerType::isCastableTo(const Type& other) const {
    if (other.as<PointerType>()) return true;
    if (auto* prim = other.as<PrimitiveType>()) {
        if (prim->name == "int" || prim->name == "long" || prim->name == "ulong") return true;
    }
    return false;
}
bool PointerType::isAssignableTo(const Type& other) const {
    if (Type::isAssignableTo(other)) return true;
    if (auto* otherPtr = other.as<PointerType>()) {
        if (pointee->toString() == "void") return true;
        if (otherPtr->pointee->toString() == "void") return true;

        // isAssignableThrough and not isAssignableTo, because a pointer hands out a
        // second name for one object rather than copying a value: whatever the pointee
        // conversion costs, nobody pays it, and the object keeps the size and the
        // interpretation the *source* gave it.
        //
        // `&int -> &long` is the shape that made this necessary. Both are integers and
        // `long` is wider, so ADR 0022's widening rule said yes -- and
        // `let x <int> = 1; let p <*long> = &x; *p = 4294967297;` compiled clean and
        // exited 139. `&int -> &float` is the same mistake without the crash: it wrote
        // a float's bits into an int's slot and printed 1069547520.
        //
        // The void arms above are deliberately in front of it. Every pointer is one
        // word, so a void pointer is the one conversion that changes nothing about the
        // storage, and Soundness_Pointers.AVoidPointerIsAssignableInBothDirections is
        // what holds it there.
        return isAssignableThrough(pointee, otherPtr->pointee);
    }
    return false;
}

}
