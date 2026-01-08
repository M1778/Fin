#include "GenericType.hpp"
#include "TypeImpl.hpp"

namespace fin {

bool GenericType::equals(const Type& other) const {
    if (auto* o = other.as<GenericType>()) return name == o->name;
    return false;
}
TypePtr GenericType::clone() const { 
    return std::make_shared<GenericType>(name, constraint ? constraint->clone() : nullptr); 
}
TypePtr GenericType::substitute(const TypeMap& mapping, TypePtr selfReplacement) {
    if (mapping.count(name)) return mapping.at(name);
    return std::make_shared<GenericType>(name, constraint ? constraint->substitute(mapping, selfReplacement) : nullptr);
}

}
