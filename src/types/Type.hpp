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
    virtual TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) = 0;

    // Cloning
    virtual TypePtr clone() const = 0;

    template <typename T>
    const T* as() const { return dynamic_cast<const T*>(this); }
    
    template <typename T>
    T* as() { return dynamic_cast<T*>(this); }
};

bool typesEqual(const TypePtr& a, const TypePtr& b);

// The question a *mutable container* asks about its element -- a pointee, an array
// element, a prototype's key or value. Not the same question as `isAssignableTo`,
// and the difference is the point: an assignment copies a value and may convert it
// on the way, while a container conversion keeps one object and hands out a second
// name for it. A conversion that changes a value's representation is sound in the
// first case and is a lie in the second.
//
// `&int -> &long` is that lie. Both are integers, `long` is wider, so ADR 0022's
// widening rule says yes -- and the object is still four bytes, so a store through
// the `&long` writes eight. It compiled clean and segfaulted.
bool isAssignableThrough(const TypePtr& from, const TypePtr& to);

} // namespace fin
