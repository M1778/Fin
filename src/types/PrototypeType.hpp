#pragma once
#include "Type.hpp"
#include <vector>
#include <memory>

namespace fin {

class PrototypeType : public Type {
public:
    std::shared_ptr<Type> keyType;
    std::shared_ptr<Type> valueType;

    PrototypeType(std::shared_ptr<Type> k, std::shared_ptr<Type> v)
        : keyType(k), valueType(v) {}

    std::string toString() const override {
        return "<{" + keyType->toString() + ", " + valueType->toString() + "}>";
    }

    bool equals(const Type& other) const override {
        if (auto* p = dynamic_cast<const PrototypeType*>(&other)) {
            return keyType->equals(*p->keyType) && valueType->equals(*p->valueType);
        }
        return false;
    }

    // The base first, like every other override in this directory. This one used to
    // restate a single one of the base's target rules -- `-> auto` -- and so lost the
    // other two: a prototype did not fit `any` or `object` while `[int]` did, because
    // ArrayType asks the base and this did not. The rule "every type fits a dynamic
    // target" is about the target, so no source may opt out of it by omission.
    //
    // Then key and value, and by the *container* question rather than the assignment
    // one. Comparing them with `equals` made a prototype fit only a prototype of
    // exactly its own key and value types, so `<{object, object}>` --
    // prototype_test.fin:40's own annotation, "object type is an expensive type but can
    // fit any datatype in it" -- accepted no literal at all. Comparing them with
    // `isAssignableTo` fixed that and bought ADR 0022's integer widening with it, which
    // a prototype must not have: `<{int, int}>` fitting `<{int, long}>` would index a
    // four-byte value as eight. isAssignableThrough is the same predicate ArrayType and
    // PointerType now ask, and Type.cpp lists what it permits.
    //
    // A dynamic half is covariant and nothing else is: `<{int, int}>` fits
    // `<{int, any}>` -- Soundness_Prototypes.APrototypeOfConcreteTypesFitsAPrototype-
    // OfADynamicType writes exactly that and is what prototype_test.fin:40 needs -- and
    // the reverse is refused, `expected '<{int, int}>', got '<{int, any}>'`. That
    // direction is a real hole in a mutable container, the same one `[int]` into `[any]`
    // has, and it is the corpus's to close: both are booked on
    // KnownDefect_ContainerVariance.ADynamicElementTypeStillAcceptsAConcreteOne rather
    // than fixed here, because the corpus asks for the array form and would break.
    bool isAssignableTo(const Type& other) const override {
        if (Type::isAssignableTo(other)) return true;
        if (auto* p = dynamic_cast<const PrototypeType*>(&other)) {
            return isAssignableThrough(keyType, p->keyType) &&
                   isAssignableThrough(valueType, p->valueType);
        }
        return false;
    }

    std::shared_ptr<Type> clone() const override {
        return std::make_shared<PrototypeType>(keyType->clone(), valueType->clone());
    }

    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override {
        return std::make_shared<PrototypeType>(
            keyType->substitute(mapping, selfReplacement),
            valueType->substitute(mapping, selfReplacement)
        );
    }
};

}
