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

    // A `T?` slot accepts a plain `T`. nullifier.fin:7 returns an `int?` member
    // from a body whose declared return type is `int` under `fun?`, and :27
    // assigns an `A?` call result into an `A?` binding.
    //
    // The two sources this must not answer for are a NullableType (`int?` into
    // `string?` is a question about the inner types, not about both sides being
    // nullable) and NullType (`null` fits *every* `T?`, whatever the inner type
    // is). Both have their own overrides, and both call back into here, so
    // stepping over them is also what stops the recursion.
    if (auto* otherNullable = other.as<NullableType>()) {
        if (!this->as<NullableType>() && !this->as<NullType>())
            return this->isAssignableTo(*otherNullable->inner);
    }

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
