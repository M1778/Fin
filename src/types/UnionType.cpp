#include "UnionType.hpp"
#include "TypeImpl.hpp"

namespace fin {

std::string UnionType::toString() const {
    if (alternatives.empty()) return alias;
    std::string out;
    for (size_t i = 0; i < alternatives.size(); ++i) {
        if (i) out += " | ";
        out += alternatives[i] ? alternatives[i]->toString() : "?";
    }
    return out;
}

bool UnionType::equals(const Type& other) const {
    auto* o = other.as<UnionType>();
    if (!o || o->alias != alias || o->alternatives.size() != alternatives.size())
        return false;
    for (size_t i = 0; i < alternatives.size(); ++i)
        if (!typesEqual(alternatives[i], o->alternatives[i])) return false;
    return true;
}

bool UnionType::isAssignableTo(const Type& other) const {
    // A union is its own type for equality (above) but its first alternative
    // for assignment: that is what the alias resolved to before this class
    // existed, so every value that stored then still stores now.
    if (alternatives.empty()) return false;
    if (!alternatives[0]) return false;
    return alternatives[0]->isAssignableTo(other);
}

TypePtr UnionType::substitute(const TypeMap& mapping, TypePtr selfReplacement) {
    std::vector<TypePtr> newArgs;
    newArgs.reserve(generic_args.size());
    for (const auto& g : generic_args)
        newArgs.push_back(g ? g->substitute(mapping, selfReplacement) : nullptr);
    std::vector<TypePtr> sub;
    sub.reserve(alternatives.size());
    for (const auto& a : alternatives)
        sub.push_back(a ? a->substitute(mapping, selfReplacement) : nullptr);
    auto nu = std::make_shared<UnionType>(alias, std::move(sub));
    nu->generic_args = std::move(newArgs);
    return nu;
}

TypePtr UnionType::instantiate(const std::vector<TypePtr>& concreteArgs) {
    if (concreteArgs.size() != generic_args.size()) return nullptr;
    TypeMap mapping;
    for (size_t i = 0; i < generic_args.size(); ++i) {
        mapping[generic_args[i]->toString()] = concreteArgs[i];
    }
    return substitute(mapping);
}

TypePtr UnionType::clone() const {
    std::vector<TypePtr> args;
    args.reserve(generic_args.size());
    for (const auto& g : generic_args) args.push_back(g ? g->clone() : nullptr);
    std::vector<TypePtr> copy;
    copy.reserve(alternatives.size());
    for (const auto& a : alternatives) copy.push_back(a ? a->clone() : nullptr);
    auto nu = std::make_shared<UnionType>(alias, std::move(copy));
    nu->generic_args = std::move(args);
    return nu;
}

} // namespace fin
