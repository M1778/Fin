#pragma once
#include "Type.hpp"

namespace fin {

class FunctionType : public Type {
public:
    std::vector<TypePtr> param_types;
    TypePtr return_type;
    bool is_vararg;
    FunctionType(std::vector<TypePtr> params, TypePtr ret, bool vararg = false)
        : param_types(std::move(params)), return_type(std::move(ret)), is_vararg(vararg) {}
    std::string toString() const override;
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override;
};

}
