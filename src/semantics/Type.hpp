#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map> // Add this

namespace fin {

class Type {
public:
    virtual ~Type() = default;
    virtual std::string toString() const = 0;
    
    // Check if two types are effectively the same
    virtual bool equals(const Type& other) const {
        return this->toString() == other.toString();
    }
    
    // Helper for casting
    template <typename T>
    const T* as() const { return dynamic_cast<const T*>(this); }
};

class PrimitiveType : public Type {
public:
    std::string name;
    PrimitiveType(std::string n) : name(std::move(n)) {}
    std::string toString() const override { return name; }
};

class GenericType : public Type {
public:
    std::string name;
    GenericType(std::string n) : name(std::move(n)) {}
    std::string toString() const override { return name; }
};

class StructType : public Type {
public:
    std::string name;
    std::vector<std::shared_ptr<Type>> generic_args;
    // Registry of methods: Name -> Return Type
    std::unordered_map<std::string, std::shared_ptr<Type>> methods;

    StructType(std::string n, std::vector<std::shared_ptr<Type>> args = {}) 
        : name(std::move(n)), generic_args(std::move(args)) {}
        
    void defineMethod(std::string methodName, std::shared_ptr<Type> returnType) {
        methods[methodName] = returnType;
    }

    std::shared_ptr<Type> getMethodReturnType(const std::string& methodName) {
        if (methods.count(methodName)) return methods[methodName];
        return nullptr;
    }
    
    std::string toString() const override {
        if (generic_args.empty()) return name;
        std::string s = name + "<";
        for (size_t i = 0; i < generic_args.size(); ++i) {
            s += generic_args[i]->toString();
            if (i < generic_args.size() - 1) s += ", ";
        }
        s += ">";
        return s;
    }
};

// Helper to compare shared_ptrs safely
inline bool typesEqual(const std::shared_ptr<Type>& a, const std::shared_ptr<Type>& b) {
    if (!a || !b) return false;
    return a->equals(*b);
}

} // namespace fin