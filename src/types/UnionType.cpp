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
    std::vector<TypePtr> sub;
    sub.reserve(alternatives.size());
    for (const auto& a : alternatives)
        sub.push_back(a ? a->substitute(mapping, selfReplacement) : nullptr);
    return std::make_shared<UnionType>(alias, std::move(sub));
}

TypePtr UnionType::clone() const {
    std::vector<TypePtr> copy;
    copy.reserve(alternatives.size());
    for (const auto& a : alternatives) copy.push_back(a ? a->clone() : nullptr);
    return std::make_shared<UnionType>(alias, std::move(copy));
}

} // namespace fin
