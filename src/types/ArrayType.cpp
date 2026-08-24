#include "ArrayType.hpp"
#include "TypeImpl.hpp"

namespace fin {

std::string ArrayType::toString() const {
    // The source spelling, extent included. What stood here was `[int; fixed]`,
    // which was honest about a flag and is a lie about a number: two arrays of
    // different lengths printed identically, so `expected '[int; fixed]', got
    // '[int; fixed]'` was a diagnostic a reader could do nothing with.
    if (extent) return "[" + element_type->toString() + ", " + std::to_string(*extent) + "]";
    return "[" + element_type->toString() + "]";
}

bool ArrayType::equals(const Type& other) const {
    if (auto* o = other.as<ArrayType>())
        return typesEqual(element_type, o->element_type) && extent == o->extent;
    return false;
}

TypePtr ArrayType::clone() const {
    return std::make_shared<ArrayType>(element_type->clone(), extent);
}

TypePtr ArrayType::substitute(const TypeMap& mapping, TypePtr selfReplacement) {
    return std::make_shared<ArrayType>(element_type->substitute(mapping, selfReplacement), extent);
}

bool ArrayType::isAssignableTo(const Type& other) const {
    if (Type::isAssignableTo(other)) return true;
    if (auto* otherArr = other.as<ArrayType>()) {
        if (!element_type->isAssignableTo(*otherArr->element_type)) return false;

        // A fixed-size array decays into a dynamic one. Not the reverse: the size is
        // what the target promises and a dynamic source cannot promise it.
        if (isFixed() != otherArr->isFixed()) return isFixed();

        // Two fixed arrays have to agree on how many. This is what the extent buys
        // that the old `bool` could not: `let a <[int, 3]> = [1, 2];` was accepted,
        // and every read of `a[2]` after it was a word the initialiser never wrote.
        if (isFixed() && *extent != *otherArr->extent) return false;

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
