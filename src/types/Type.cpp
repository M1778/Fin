#include "TypeImpl.hpp"
#include <iostream>

namespace fin {

// --- Helper ---
bool typesEqual(const TypePtr& a, const TypePtr& b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    return a->equals(*b);
}

// --- Base Type ---
bool Type::isAssignableTo(const Type& other) const {
    if (this->equals(other)) return true;
    if (other.toString() == "auto") return true;

    if (auto* self = this->as<SelfType>()) {
      return self->originalStruct->isAssignableTo(other);
    }
    if (auto* otherSelf = other.as<SelfType>()){
      return this->isAssignableTo(*otherSelf->originalStruct);
    }
    
    if (auto* thisArr = this->as<ArrayType>()) {
        if (auto* otherArr = other.as<ArrayType>()) {
            if (thisArr->element_type->equals(*otherArr->element_type)) {
                if (thisArr->is_fixed_size && !otherArr->is_fixed_size) return true;
            }
        }
    }
    
    if (auto* thisPtr = this->as<PointerType>()) {
        if (auto* otherPtr = other.as<PointerType>()) {
            if (otherPtr->pointee->toString() == "void") return true;
        }
    }
    
    if (dynamic_cast<const GenericType*>(&other)) return true;
    return false;
}

}
