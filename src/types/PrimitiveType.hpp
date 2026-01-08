#pragma once
#include "Type.hpp"

namespace fin {

class PrimitiveType : public Type {
public:
    std::string name;
    PrimitiveType(std::string n) : name(std::move(n)) {}
    std::string toString() const override { return name; }
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap&, TypePtr = nullptr) override { return std::make_shared<PrimitiveType>(name); }
    TypePtr clone() const override { return std::make_shared<PrimitiveType>(name); }
    bool isAssignableTo(const Type& other) const override;
};

}
