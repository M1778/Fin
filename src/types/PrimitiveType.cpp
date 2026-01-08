#include "PrimitiveType.hpp"
#include "TypeImpl.hpp" // For typesEqual and others if needed

namespace fin {

bool PrimitiveType::equals(const Type& other) const {
    if (auto* o = other.as<PrimitiveType>()) return name == o->name;
    return false;
}
bool PrimitiveType::isAssignableTo(const Type& other) const {
    if (Type::isAssignableTo(other)) return true;
    if (name == "int" && other.toString() == "float") return true;
    return false;
}

}
