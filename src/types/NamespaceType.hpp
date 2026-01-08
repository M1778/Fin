#pragma once
#include "Type.hpp"

namespace fin {

class Scope; // Forward decl

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

}
