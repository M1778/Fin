#pragma once
#include "Type.hpp"

namespace fin {

class ArrayType : public Type {
public:
    TypePtr element_type;
    bool is_fixed_size;
    ArrayType(TypePtr elem, bool fixed = false) 
        : element_type(std::move(elem)), is_fixed_size(fixed) {}
    std::string toString() const override;
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override;
    bool isAssignableTo(const Type& other) const override;
};

}
