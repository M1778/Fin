#pragma once
#include "Type.hpp"
#include <vector>
#include <unordered_map>

namespace fin {
class Scope;

// --- Namespace Type (for Modules) ---
class NamespaceType : public Type {
public:
    std::string name;
    std::shared_ptr<Scope> scope; // Imported module scope

    NamespaceType(std::string n, std::shared_ptr<Scope> s) 
        : name(std::move(n)), scope(std::move(s)) {}

    std::string toString() const override { return "module<" + name + ">"; }
    
    bool equals(const Type& other) const override {
        // Namespaces are singletons effectively, compare pointers or names
        if (auto* o = other.as<NamespaceType>()) return name == o->name;
        return false;
    }
    
    TypePtr substitute(const TypeMap&) override { return std::make_shared<NamespaceType>(name, scope); }
    TypePtr clone() const override { return std::make_shared<NamespaceType>(name, scope); }
};

// --- Primitive Types ---
class PrimitiveType : public Type {
public:
    std::string name;
    PrimitiveType(std::string n) : name(std::move(n)) {}
    std::string toString() const override { return name; }
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap&) override { return std::make_shared<PrimitiveType>(name); }
    TypePtr clone() const override { return std::make_shared<PrimitiveType>(name); }
    bool isAssignableTo(const Type& other) const override;
};

// --- Pointer Type ---
class PointerType : public Type {
public:
    TypePtr pointee;
    PointerType(TypePtr p) : pointee(std::move(p)) {}
    std::string toString() const override;
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping) override;
    TypePtr clone() const override;
    bool isCastableTo(const Type& other) const override;
    
    // Added override
    bool isAssignableTo(const Type& other) const override;
};

// --- Array Type ---
class ArrayType : public Type {
public:
    TypePtr element_type;
    bool is_fixed_size;
    ArrayType(TypePtr elem, bool fixed = false) 
        : element_type(std::move(elem)), is_fixed_size(fixed) {}
    std::string toString() const override;
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping) override;
    TypePtr clone() const override;
    
    // Added override
    bool isAssignableTo(const Type& other) const override;
};

// --- Generic Type ---
class GenericType : public Type {
public:
    std::string name;
    TypePtr constraint = nullptr;
    GenericType(std::string n, TypePtr c = nullptr) : name(std::move(n)), constraint(std::move(c)) {}
    std::string toString() const override { return name; }
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping) override;
    TypePtr clone() const override;
};

// --- Function Type ---
class FunctionType : public Type {
public:
    std::vector<TypePtr> param_types;
    TypePtr return_type;
    bool is_vararg;
    FunctionType(std::vector<TypePtr> params, TypePtr ret, bool vararg = false)
        : param_types(std::move(params)), return_type(std::move(ret)), is_vararg(vararg) {}
    std::string toString() const override;
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping) override;
    TypePtr clone() const override;
};

// --- Struct / Interface Type ---
struct FieldInfo {
    TypePtr type;
    bool is_public;
};

class StructType : public Type {
public:
    std::string name;
    std::vector<TypePtr> generic_args;
    
    std::vector<TypePtr> parents;
    bool is_interface = false;
    
    // Registry using FieldInfo
    std::unordered_map<std::string, FieldInfo> fields;
    std::unordered_map<std::string, TypePtr> methods;
    std::unordered_map<int, TypePtr> operators;
    
    std::vector<TypePtr> constructors; 
    bool has_destructor = false;

    StructType(std::string n, std::vector<TypePtr> args = {}) 
        : name(std::move(n)), generic_args(std::move(args)) {}

    void defineField(std::string n, TypePtr t, bool pub = false) { fields[n] = {t, pub}; }
    void defineMethod(std::string n, TypePtr t) { methods[n] = t; }
    void defineOperator(int op, TypePtr t) { operators[op] = t; }
    void addConstructor(TypePtr t) { constructors.push_back(t); }

    TypePtr getFieldType(const std::string& n);
    bool isFieldPublic(const std::string& n);
    TypePtr getMethodReturnType(const std::string& n);

    std::string toString() const override;
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping) override;
    TypePtr clone() const override;

    TypePtr instantiate(const std::vector<TypePtr>& concreteArgs);
    bool implements(const StructType* interface) const;
};


} // namespace fin