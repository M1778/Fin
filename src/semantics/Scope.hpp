#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include "Type.hpp"

namespace fin {

struct Symbol {
    std::string name;
    std::shared_ptr<Type> type;
    bool is_mutable;
    bool is_initialized;
};

class Scope {
public:
    Scope* parent;
    std::unordered_map<std::string, Symbol> symbols;
    std::unordered_map<std::string, std::shared_ptr<Type>> types;

    Scope(Scope* p = nullptr) : parent(p) {}

    void define(Symbol sym) { symbols[sym.name] = sym; }
    void defineType(std::string name, std::shared_ptr<Type> type) { types[name] = type; }

    Symbol* resolve(const std::string& name) {
        if (symbols.count(name)) return &symbols[name];
        if (parent) return parent->resolve(name);
        return nullptr;
    }

    std::shared_ptr<Type> resolveType(const std::string& name) {
        if (types.count(name)) return types[name];
        if (parent) return parent->resolveType(name);
        return nullptr;
    }
};

} // namespace fin