#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include "../types/Type.hpp"

// Forward decl
namespace fin { class MacroDeclaration; }

namespace fin {

struct Symbol {
    std::string name;
    std::shared_ptr<Type> type;
    bool is_mutable;
    bool is_initialized;

    // Whether a prototype for this name was retained for the splice into the root
    // program (ADR 0021's backend half: ModuleLoader::retainAmbientPrototype).
    //
    // It is not "is this declaration marked #[global]", which is a weaker fact. The
    // mark is what asks; the retention is what the backend will actually be given, and
    // the two differ when a name is published twice -- first retention wins, so the
    // second module's declaration is marked and is *not* the one the root program will
    // declare. Only the retained one may be rewritten into a call on its Fin name
    // (SemanticAnalyzer::visit(MethodCall&)'s namespace branch), because only for that
    // one is the name in the root program bound to the symbol the module named.
    //
    // Defaulted, so the twenty-two aggregate initialisations of Symbol elsewhere mean
    // what they meant: a symbol is not ambient unless something says it is.
    bool is_ambient = false;
};

class Scope {
public:
    Scope* parent;
    std::unordered_map<std::string, Symbol> symbols;
    std::unordered_map<std::string, std::shared_ptr<Type>> types;
    
    std::unordered_map<std::string, MacroDeclaration*> macros;

    Scope(Scope* p = nullptr) : parent(p) {}

    void define(Symbol sym) { symbols[sym.name] = sym; }
    void defineType(std::string name, std::shared_ptr<Type> type) { types[name] = type; }
    
    void defineMacro(std::string name, MacroDeclaration* macro) { macros[name] = macro; }

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
    
    MacroDeclaration* resolveMacro(const std::string& name) {
        if (macros.count(name)) return macros[name];
        if (parent) return parent->resolveMacro(name);
        return nullptr;
    }
};

} // namespace fin