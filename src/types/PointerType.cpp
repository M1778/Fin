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
