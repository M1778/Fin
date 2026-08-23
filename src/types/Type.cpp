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

    // Every type fits `any` and `object`. Here rather than as an override on
    // DynamicType because the rule is about the *target*: an override can only speak
    // for the source, and it is every other type that has to accept this target.
    //
    // Here rather than in checkType, too, and that is the load-bearing choice: this
    // is reached recursively -- ArrayType::isAssignableTo asks its element types, so
    // `[int]` fits `[any]` through this line. A copy in checkType would answer for
    // `any` and not for `[any]`, and stdlib/types.fin:102 takes `const &arr: [any]`.
    //
    // `null` is excluded, and the corpus is explicit about why. nullifier.fin:36
    // writes `let _ <any> = mibombo?;` and comments "this should be an error since
    // type any cannot be null". `null` is not a value of some type that is being
    // erased; it is the absence of one, and NullType::isAssignableTo falls through to
    // here -- so a rule stated as "a dynamic target accepts anything" accepts null
    // too, silently. A program that means it writes `any?`, which is a NullableType
    // and is answered before this.
    if (other.as<DynamicType>() && !this->as<NullType>()) return true;

    // No rule for the error sentinel here, deliberately. checkType short-circuits on
    // isErrorType before it ever calls this (Analyzer_Core.cpp:361), and checkType is
    // the only caller outside this directory -- so a rule here could not fire, and a
    // mutation matrix proved it: removing this line, removing ErrorType's own
    // override, and removing the short-circuit each killed nothing, because any two
    // covered for the third. One mechanism, in the one place that also suppresses the
    // message. If a second caller of isAssignableTo ever appears it must short-circuit
    // the same way rather than reinstate this.

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
