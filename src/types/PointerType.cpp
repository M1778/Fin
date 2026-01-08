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
        return pointee->isAssignableTo(*otherPtr->pointee);
    }
    return false;
}

}
