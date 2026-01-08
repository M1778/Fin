#pragma once
#include "Type.hpp"

namespace fin {

class PointerType : public Type {
public:
    TypePtr pointee;
    PointerType(TypePtr p) : pointee(std::move(p)) {}
    std::string toString() const override;
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override;
    bool isCastableTo(const Type& other) const override;
    bool isAssignableTo(const Type& other) const override;
};

}
