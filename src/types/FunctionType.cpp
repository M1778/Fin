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
    // `param_defaults` deliberately does not participate. A default is a fact about the
    // declaration, not about the type: `fn(int) -> int` is one type whether or not the
    // function behind it wrote `= 2`, and a value of that type is assignable to it
    // either way. Comparing them would make two identically-typed functions
    // unassignable to the same variable over a difference no call site can observe
    // except by omitting an argument, which is arity and is checked separately.
    // Soundness_ParameterDefaults.ADefaultIsNotPartOfTheFunctionType pins this.
    if (param_types.size() != o->param_types.size()) return false;
    if (!typesEqual(return_type, o->return_type)) return false;
    for (size_t i = 0; i < param_types.size(); ++i) {
        if (!typesEqual(param_types[i], o->param_types[i])) return false;
    }
    return true;
}
// Both of these carry `param_defaults` through unchanged, and that is the whole reason
// the field is a vector of flags rather than anything richer: substituting a generic
// parameter changes what a parameter's *type* is and cannot change whether one was
// written with a default, so there is nothing per-entry to transform. A generic method
// with a defaulted parameter keeps it optional in every instantiation
// (Soundness_ParameterDefaults.AGenericMethodsDefaultSurvivesInstantiation), which is
// what would silently break if either of these dropped the field -- and dropped is what
// omitting the fourth argument means, since it defaults to empty.
TypePtr FunctionType::clone() const {
    std::vector<TypePtr> newParams;
    for (auto& p : param_types) newParams.push_back(p->clone());
    return std::make_shared<FunctionType>(newParams, return_type->clone(), is_vararg,
                                          param_defaults);
}
TypePtr FunctionType::substitute(const TypeMap& mapping, TypePtr selfReplacement) {
    std::vector<TypePtr> newParams;
    for (auto& p : param_types) newParams.push_back(p->substitute(mapping, selfReplacement));
    return std::make_shared<FunctionType>(newParams, return_type->substitute(mapping, selfReplacement),
                                          is_vararg, param_defaults);
}

}
