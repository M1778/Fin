#include "TypeImpl.hpp"
#include <iostream>

namespace fin {

// --- Helper ---
bool typesEqual(const TypePtr& a, const TypePtr& b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    return a->equals(*b);
}

// --- Base Type ---
bool Type::isAssignableTo(const Type& other) const {
    if (this->equals(other)) return true;
    if (other.toString() == "auto") return true;
    
    // Array type simplification
    if (auto* thisArr = this->as<ArrayType>()) {
        if (auto* otherArr = other.as<ArrayType>()) {
            if (thisArr->element_type->equals(*otherArr->element_type)) {
                if (thisArr->is_fixed_size && !otherArr->is_fixed_size) return true;
            }
        }
    }
    
    // Allow T* -> void* (Base check handles simple cases, PointerType overrides for details)
    
    if (dynamic_cast<const GenericType*>(&other)) return true;
    return false;
}

// --- PrimitiveType ---
bool PrimitiveType::equals(const Type& other) const {
    if (auto* o = other.as<PrimitiveType>()) return name == o->name;
    return false;
}
bool PrimitiveType::isAssignableTo(const Type& other) const {
    if (Type::isAssignableTo(other)) return true;
    if (name == "int" && other.toString() == "float") return true; // Implicit int->float
    return false;
}

// --- PointerType ---
std::string PointerType::toString() const { return "&" + pointee->toString(); }
bool PointerType::equals(const Type& other) const {
    if (auto* o = other.as<PointerType>()) return typesEqual(pointee, o->pointee);
    return false;
}
TypePtr PointerType::clone() const { return std::make_shared<PointerType>(pointee->clone()); }
TypePtr PointerType::substitute(const TypeMap& mapping) { return std::make_shared<PointerType>(pointee->substitute(mapping)); }
bool PointerType::isCastableTo(const Type& other) const {
    if (other.as<PointerType>()) return true;
    if (auto* prim = other.as<PrimitiveType>()) {
        if (prim->name == "int" || prim->name == "long" || prim->name == "ulong") return true;
    }
    return false;
}

// Recursive check for pointers and null compatibility
bool PointerType::isAssignableTo(const Type& other) const {
    if (Type::isAssignableTo(other)) return true;

    if (auto* otherPtr = other.as<PointerType>()) {
        // 1. Allow &void (null) -> &T
        if (pointee->toString() == "void") return true;
        
        // 2. Allow T* -> void*
        if (otherPtr->pointee->toString() == "void") return true;
        
        // 3. Recursive check: &int -> &T
        return pointee->isAssignableTo(*otherPtr->pointee);
    }
    return false;
}

// --- ArrayType ---
std::string ArrayType::toString() const { 
    if (is_fixed_size) return "[" + element_type->toString() + "; fixed]";
    return "[" + element_type->toString() + "]"; 
}
bool ArrayType::equals(const Type& other) const {
    if (auto* o = other.as<ArrayType>()) return typesEqual(element_type, o->element_type) && is_fixed_size == o->is_fixed_size;
    return false;
}
TypePtr ArrayType::clone() const { return std::make_shared<ArrayType>(element_type->clone(), is_fixed_size); }
TypePtr ArrayType::substitute(const TypeMap& mapping) { return std::make_shared<ArrayType>(element_type->substitute(mapping), is_fixed_size); }

bool ArrayType::isAssignableTo(const Type& other) const {
    if (Type::isAssignableTo(other)) return true;
    if (auto* otherArr = other.as<ArrayType>()) {
        if (!element_type->isAssignableTo(*otherArr->element_type)) return false;
        if (is_fixed_size && !otherArr->is_fixed_size) return true;
        return false;
    }
    return false;
}

// --- GenericType ---
bool GenericType::equals(const Type& other) const {
    if (auto* o = other.as<GenericType>()) return name == o->name;
    return false;
}
TypePtr GenericType::clone() const { return std::make_shared<GenericType>(name, constraint ? constraint->clone() : nullptr); }
TypePtr GenericType::substitute(const TypeMap& mapping) {
    if (mapping.count(name)) return mapping.at(name);
    return std::make_shared<GenericType>(name, constraint ? constraint->substitute(mapping) : nullptr);
}

// --- FunctionType ---
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
TypePtr FunctionType::substitute(const TypeMap& mapping) {
    std::vector<TypePtr> newParams;
    for (auto& p : param_types) newParams.push_back(p->substitute(mapping));
    return std::make_shared<FunctionType>(newParams, return_type->substitute(mapping), is_vararg);
}

// --- StructType ---
std::string StructType::toString() const {
    if (generic_args.empty()) return name;
    std::string s = name + "<";
    for (size_t i = 0; i < generic_args.size(); ++i) {
        s += generic_args[i]->toString();
        if (i < generic_args.size() - 1) s += ", ";
    }
    s += ">";
    return s;
}

TypePtr StructType::getFieldType(const std::string& n) {
    if (fields.count(n)) return fields.at(n).type;
    for (const auto& parent : parents) {
        if (auto p = std::dynamic_pointer_cast<StructType>(parent)) {
            if (auto t = p->getFieldType(n)) return t;
        }
    }
    return nullptr;
}

bool StructType::isFieldPublic(const std::string& n) {
    if (fields.count(n)) return fields.at(n).is_public;
    for (const auto& parent : parents) {
        if (auto p = std::dynamic_pointer_cast<StructType>(parent)) {
            if (p->getFieldType(n)) return p->isFieldPublic(n);
        }
    }
    return false;
}

TypePtr StructType::getMethodReturnType(const std::string& n) {
    if (methods.count(n)) return methods.at(n);
    for (const auto& parent : parents) {
        if (auto p = std::dynamic_pointer_cast<StructType>(parent)) {
            if (auto t = p->getMethodReturnType(n)) return t;
        }
    }
    return nullptr;
}

bool StructType::equals(const Type& other) const {
    auto* o = other.as<StructType>();
    if (!o) return false;
    if (name != o->name) return false;
    if (generic_args.size() != o->generic_args.size()) return false;
    for (size_t i = 0; i < generic_args.size(); ++i) {
        if (!typesEqual(generic_args[i], o->generic_args[i])) return false;
    }
    return true;
}

TypePtr StructType::clone() const {
    std::vector<TypePtr> newArgs;
    for (auto& arg : generic_args) newArgs.push_back(arg->clone());
    
    auto s = std::make_shared<StructType>(name, newArgs);
    for(auto& kv : fields) s->defineField(kv.first, kv.second.type->clone(), kv.second.is_public);
    for(auto& kv : methods) s->defineMethod(kv.first, kv.second->clone());
    for(auto& kv : operators) s->defineOperator(kv.first, kv.second->clone());
    for(const auto& p : parents) s->parents.push_back(p->clone());
    
    s->constructors = constructors;
    s->has_destructor = has_destructor;
    s->is_interface = is_interface;
    
    return s;
}

TypePtr StructType::substitute(const TypeMap& mapping) {
    std::vector<TypePtr> newArgs;
    for(auto& arg : generic_args) newArgs.push_back(arg->substitute(mapping));
    
    auto newStruct = std::make_shared<StructType>(name, newArgs);
    
    for(auto& kv : fields) newStruct->defineField(kv.first, kv.second.type->substitute(mapping), kv.second.is_public);
    for(auto& kv : methods) newStruct->defineMethod(kv.first, kv.second->substitute(mapping));
    for(auto& kv : operators) newStruct->defineOperator(kv.first, kv.second->substitute(mapping));
    for(const auto& p : parents) newStruct->parents.push_back(p->substitute(mapping));

    newStruct->is_interface = is_interface;
    newStruct->has_destructor = has_destructor;
    for(auto& c : constructors) newStruct->addConstructor(c->substitute(mapping));

    return newStruct;
}

TypePtr StructType::instantiate(const std::vector<TypePtr>& concreteArgs) {
    if (concreteArgs.size() != generic_args.size()) return nullptr;
    TypeMap mapping;
    for(size_t i=0; i<generic_args.size(); ++i) {
        mapping[generic_args[i]->toString()] = concreteArgs[i];
    }
    return substitute(mapping);
}

bool StructType::implements(const StructType* interface) const {
    for (const auto& [methodName, retType] : interface->methods) {
        if (methods.find(methodName) == methods.end()) return false;
    }
    for (const auto& [op, retType] : interface->operators) {
        if (operators.find(op) == operators.end()) return false;
    }
    if (interface->has_destructor && !this->has_destructor) return false;
    for (const auto& ifaceCtor : interface->constructors) {
        bool found = false;
        for (const auto& myCtor : this->constructors) {
            if (myCtor->equals(*ifaceCtor)) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

} // namespace fin