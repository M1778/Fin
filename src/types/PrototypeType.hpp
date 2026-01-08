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

    bool isAssignableTo(const Type& other) const override {
        if (equals(other)) return true;
        if (other.toString() == "auto") return true;
        // Prototypes are assignable to types that "implement" them or Map-like types
        // For now, let's keep it simple.
        return false;
    }

    std::shared_ptr<Type> clone() const override {
        return std::make_shared<PrototypeType>(keyType->clone(), valueType->clone());
    }
};

}
