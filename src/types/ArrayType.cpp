#include "ArrayType.hpp"
#include "TypeImpl.hpp"

namespace fin {

std::string ArrayType::toString() const { 
    if (is_fixed_size) return "[" + element_type->toString() + "; fixed]";
    return "[" + element_type->toString() + "]"; 
}
bool ArrayType::equals(const Type& other) const {
    if (auto* o = other.as<ArrayType>()) return typesEqual(element_type, o->element_type) && is_fixed_size == o->is_fixed_size;
    return false;
}
TypePtr ArrayType::clone() const { return std::make_shared<ArrayType>(element_type->clone(), is_fixed_size); }
TypePtr ArrayType::substitute(const TypeMap& mapping, TypePtr selfReplacement) { 
    return std::make_shared<ArrayType>(element_type->substitute(mapping, selfReplacement), is_fixed_size); 
}
bool ArrayType::isAssignableTo(const Type& other) const {
    if (Type::isAssignableTo(other)) return true;
    if (auto* otherArr = other.as<ArrayType>()) {
        if (!element_type->isAssignableTo(*otherArr->element_type)) return false;

        // A fixed-size array decays into a dynamic one. Not the reverse: the size is
        // what the target promises and a dynamic source cannot promise it.
        if (is_fixed_size != otherArr->is_fixed_size) return is_fixed_size;

        // Same shape, and the elements already agreed. What stood here returned false,
        // so `[int]` did not fit `[auto]` or `[any]` -- two dynamic arrays only ever
        // matched through `equals` in the base, which is exact. The element check
        // above is the whole rule; reaching it and then discarding its answer meant
        // every permissive element type stopped at the array boundary.
        return true;
    }
    return false;
}

}
