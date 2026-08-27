#include "NullableType.hpp"
#include "TypeImpl.hpp"

namespace fin {

bool isNullLiteral(const TypePtr& t) { return t && t->as<NullType>() != nullptr; }

std::string NullableType::toString() const { return inner->toString() + "?"; }

bool NullableType::equals(const Type& other) const {
    if (auto* o = other.as<NullableType>()) return typesEqual(inner, o->inner);
    return false;
}

bool NullableType::isAssignableTo(const Type& other) const {
    // `auto`, a generic target, and an equal type. Reached before the nullable
    // arm below so that `T? -> auto` keeps inferring `T?` rather than `T`.
    if (Type::isAssignableTo(other)) return true;

    // `int? -> string?` is as wrong as `int -> string`: the question is asked of
    // the inner types, not answered by both sides being nullable.
    if (auto* o = other.as<NullableType>()) return inner->isAssignableTo(*o->inner);

    // Everything else needs a denullify. nullifier.fin:31 writes the `?` on
    // `make_A(-1)?` and its comment says the `?` is what "makes any null value
    // raise a panic error OR just returns the normal value" -- so without it the
    // assignment has to fail, or the sample is explaining decoration.
    return false;
}

bool NullableType::isCastableTo(const Type& other) const {
    // A cast is the explicit form of "I accept the panic", so it sees through the
    // wrapper. nullifier.fin:13 `cast<T>(v)`.
    if (Type::isCastableTo(other)) return true;
    if (auto* o = other.as<NullableType>()) return inner->isCastableTo(*o->inner);
    return inner->isCastableTo(other);
}

TypePtr NullableType::substitute(const TypeMap& mapping, TypePtr selfReplacement) {
    // `value? <T>` in `struct maybe<T: Castable>` (nullifier.fin:11) stays
    // nullable after T is bound.
    return std::make_shared<NullableType>(inner->substitute(mapping, selfReplacement));
}

TypePtr NullableType::clone() const { return std::make_shared<NullableType>(inner->clone()); }

bool NullType::isAssignableTo(const Type& other) const {
    if (other.as<NullableType>()) return true;
    if (other.as<PointerType>()) return true;
    // `auto` and a generic target, via the base. Not the nullable-widening arm
    // there -- that one steps over NullType, because "does `null` fit a `T?`" is
    // answered above and is not a question about the inner type.
    return Type::isAssignableTo(other);
}

bool NullType::isCastableTo(const Type& other) const {
    if (other.as<NullableType>()) return true;
    if (other.as<PointerType>()) return true;
    return Type::isCastableTo(other);
}

}
