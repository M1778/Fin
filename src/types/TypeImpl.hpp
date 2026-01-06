#pragma once
#include "Type.hpp"
#include <vector>
#include <unordered_map>

namespace fin {

class Scope; // Forward decl

// --- Namespace Type ---
class NamespaceType : public Type {
public:
    std::string name;
    std::shared_ptr<Scope> scope;
    NamespaceType(std::string n, std::shared_ptr<Scope> s) 
        : name(std::move(n)), scope(std::move(s)) {}
    std::string toString() const override { return "module<" + name + ">"; }
    bool equals(const Type& other) const override {
        if (auto* o = other.as<NamespaceType>()) return name == o->name;
        return false;
    }
    TypePtr substitute(const TypeMap&, TypePtr = nullptr) override { 
        return std::make_shared<NamespaceType>(name, scope); 
    }
    TypePtr clone() const override { return std::make_shared<NamespaceType>(name, scope); }
};

// --- SelfType ---
class SelfType : public Type {
public:
    TypePtr originalStruct;
    SelfType(TypePtr s) : originalStruct(s) {}
    std::string toString() const override { return "Self"; }
    bool equals(const Type& other) const override { return other.as<SelfType>(); }
    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override { return std::make_shared<SelfType>(originalStruct); }
    bool isAssignableTo(const Type& other) const override { return originalStruct->isAssignableTo(other); }
};

// --- PrimitiveType ---
class PrimitiveType : public Type {
public:
    std::string name;
    PrimitiveType(std::string n) : name(std::move(n)) {}
    std::string toString() const override { return name; }
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap&, TypePtr = nullptr) override { return std::make_shared<PrimitiveType>(name); }
    TypePtr clone() const override { return std::make_shared<PrimitiveType>(name); }
    bool isAssignableTo(const Type& other) const override;
};

// --- PointerType ---
class PointerType : public Type {
public:
    TypePtr pointee;
    PointerType(TypePtr p) : pointee(std::move(p)) {}
    std::string toString() const override;
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override;
    bool isCastableTo(const Type& other) const override;
    bool isAssignableTo(const Type& other) const override;
};

// --- ArrayType ---
class ArrayType : public Type {
public:
    TypePtr element_type;
    bool is_fixed_size;
    ArrayType(TypePtr elem, bool fixed = false) 
        : element_type(std::move(elem)), is_fixed_size(fixed) {}
    std::string toString() const override;
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override;
    bool isAssignableTo(const Type& other) const override;
};

// --- GenericType ---
class GenericType : public Type {
public:
    std::string name;
    TypePtr constraint = nullptr;
    GenericType(std::string n, TypePtr c = nullptr) : name(std::move(n)), constraint(std::move(c)) {}
    std::string toString() const override { return name; }
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override;
};

// --- FunctionType ---
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

struct FieldInfo {
    TypePtr type;
    bool is_public;
};

// --- StructType ---
class StructType : public Type {
public:
    std::string name;
    std::vector<TypePtr> generic_args;
    std::vector<TypePtr> parents;
    bool is_interface = false;
    
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
    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override;

    TypePtr instantiate(const std::vector<TypePtr>& concreteArgs);
    bool implements(const StructType* interface) const;
};

} // namespace fin
