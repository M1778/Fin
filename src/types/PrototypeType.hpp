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
    // Then key and value by *assignability*, not equality. Comparing them with `equals`
    // made a prototype fit only a prototype of exactly its own key and value types, so
    // `<{object, object}>` -- prototype_test.fin:40's own annotation, "object type is an
    // expensive type but can fit any datatype in it" -- accepted no literal at all.
    // ArrayType::isAssignableTo already asks its elements this question; this is the
    // same lesson at the second container boundary.
    //
    // Invariantly, deliberately: `<{int, int}>` does not fit `<{int, any}>` and the
    // reverse does not either, both by way of the key/value checks. Prototypes are
    // mutable containers, so a covariant value would let a write through the wider
    // alias put a `string` where the narrower one promises `int`. `[int]` into `[any]`
    // has the same hole and is what the corpus asks for (stdlib/types.fin:102), so it
    // is not settled tree-wide -- but nothing in the corpus asks for it here, and the
    // narrower rule is the one that can be widened later without breaking a program.
    bool isAssignableTo(const Type& other) const override {
        if (Type::isAssignableTo(other)) return true;
        if (auto* p = dynamic_cast<const PrototypeType*>(&other)) {
            return keyType->isAssignableTo(*p->keyType) &&
                   valueType->isAssignableTo(*p->valueType);
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
