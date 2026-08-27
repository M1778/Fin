#include "DynamicType.hpp"
#include "TypeImpl.hpp"

namespace fin {

std::string DynamicType::toString() const {
    if (bounds.empty()) return name;
    // Rendered the way the corpus writes it, so a diagnostic quoting the type is a
    // string the reader can search for in their own source.
    std::string out = name + " implements <";
    for (size_t i = 0; i < bounds.size(); ++i) {
        if (i) out += ", ";
        out += bounds[i] ? bounds[i]->toString() : "?";
    }
    return out + ">";
}

bool DynamicType::equals(const Type& other) const {
    auto* o = other.as<DynamicType>();
    if (!o || o->name != name || o->bounds.size() != bounds.size()) return false;
    for (size_t i = 0; i < bounds.size(); ++i)
        if (!typesEqual(bounds[i], o->bounds[i])) return false;
    return true;
}

TypePtr DynamicType::substitute(const TypeMap& mapping, TypePtr selfReplacement) {
    // The bounds are types and can name a generic parameter -- `type Any<...> = any
    // implements <...>` is instantiated per use -- so they are substituted rather
    // than copied. The name never is: `any` does not become the type it erased.
    std::vector<TypePtr> sub;
    sub.reserve(bounds.size());
    for (const auto& b : bounds)
        sub.push_back(b ? b->substitute(mapping, selfReplacement) : nullptr);
    return std::make_shared<DynamicType>(name, std::move(sub));
}

TypePtr DynamicType::clone() const {
    std::vector<TypePtr> copy;
    copy.reserve(bounds.size());
    for (const auto& b : bounds) copy.push_back(b ? b->clone() : nullptr);
    return std::make_shared<DynamicType>(name, std::move(copy));
}

} // namespace fin
