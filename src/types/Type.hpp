#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <iostream>

namespace fin {

class Type;
using TypePtr = std::shared_ptr<Type>;
using TypeMap = std::unordered_map<std::string, TypePtr>;

class Type {
public:
    virtual ~Type() = default;
    virtual std::string toString() const = 0;
    
    // Equality (Strict)
    virtual bool equals(const Type& other) const = 0;

    // Compatibility (Assignment)
    virtual bool isAssignableTo(const Type& other) const;

    // Explicit casting with virtual method
    virtual bool isCastableTo(const Type& other) const {
        return this->equals(other);
    }

    // Generics Substitution
    virtual TypePtr substitute(const TypeMap& mapping) = 0;

    // Cloning
    virtual TypePtr clone() const = 0;

    template <typename T>
    const T* as() const { return dynamic_cast<const T*>(this); }
    
    template <typename T>
    T* as() { return dynamic_cast<T*>(this); }
};

bool typesEqual(const TypePtr& a, const TypePtr& b);

} // namespace fin