#include "FunctionType.hpp"
#include "TypeImpl.hpp"

namespace fin {

std::string FunctionType::toString() const {
    std::string s = "fn(";
    for (size_t i = 0; i < param_types.size(); ++i) {
        s += param_types[i]->toString();
        if (i < param_types.size() - 1) s += ", ";
    }
    if (is_vararg) { if (!param_types.empty()) s += ", "; s += "..."; }
    s += ") -> " + return_type->toString();
    return s;
}
bool FunctionType::equals(const Type& other) const {
    auto* o = other.as<FunctionType>();
    if (!o) return false;
    if (is_vararg != o->is_vararg) return false;
    if (param_types.size() != o->param_types.size()) return false;
    if (!typesEqual(return_type, o->return_type)) return false;
    for (size_t i = 0; i < param_types.size(); ++i) {
        if (!typesEqual(param_types[i], o->param_types[i])) return false;
    }
    return true;
}
TypePtr FunctionType::clone() const {
    std::vector<TypePtr> newParams;
    for (auto& p : param_types) newParams.push_back(p->clone());
    return std::make_shared<FunctionType>(newParams, return_type->clone(), is_vararg);
}
TypePtr FunctionType::substitute(const TypeMap& mapping, TypePtr selfReplacement) {
    std::vector<TypePtr> newParams;
    for (auto& p : param_types) newParams.push_back(p->substitute(mapping, selfReplacement));
    return std::make_shared<FunctionType>(newParams, return_type->substitute(mapping, selfReplacement), is_vararg);
}

}
