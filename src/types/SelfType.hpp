#pragma once
#include "Type.hpp"

namespace fin {

class SelfType : public Type {
public:
    TypePtr originalStruct;
    SelfType(TypePtr s) : originalStruct(s) {}
    std::string toString() const override { return "Self"; }
    bool equals(const Type& other) const override { return other.as<SelfType>(); }
    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override { return std::make_shared<SelfType>(originalStruct); }
    bool isAssignableTo(const Type& other) const override { return originalStruct->isAssignableTo(other); }
};

}
