#pragma once
#include "Type.hpp"

namespace fin {

class GenericType : public Type {
public:
    std::string name;
    TypePtr constraint = nullptr;
    GenericType(std::string n, TypePtr c = nullptr) : name(std::move(n)), constraint(std::move(c)) {}
    std::string toString() const override { return name; }
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override;
};

}
