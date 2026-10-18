// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include "../types/Type.hpp"

// Forward decl
namespace fin { class MacroDeclaration; }

namespace fin {

// The `#[slaveof]` referent meaning "pinned to program exit", spelled in one
// place and used where it means that. A typo in any single use would compile
// and simply never match: storage the programmer pinned would stop being
// recognised as pinned (ADR 0055's read) and the attribute validator would
// start refusing it. The spelling itself is language surface
// (tests/samples/variables.fin:35) and does not change.
inline constexpr std::string_view kFinLifetimeRoot{"$Fin"};

struct Symbol {
    std::string name;
    std::shared_ptr<Type> type;
    bool is_mutable;
    bool is_initialized;

    // Wave-5 slice 4: `#[slaveof(...)]` on the declaration that defined this
    // binding. Empty when untied, "$Fin" when pinned to program exit, else
    // the referent variable whose scope exit ends this storage. Recorded at
    // declaration (after the referent resolved in scope) so a use can tell a
    // live tie from a dangling one; the scope owns the entry, so the tie
    // dies with the binding and shadowing resolves by construction.
    //
    // Defaulted, so the one aggregate initialisation of Symbol (in `bound`,
    // below) means what it meant: a symbol is untied unless something says
    // otherwise.
    std::string slaveof;

    // Whether this binding is a compile-time callable rather than frame data.
    //
    // `is_function` marks names bound by a `fun` declaration (nested or module):
    // calling one needs its symbol, never a frame slot. `is_template` marks a
    // variable bound to a generic recipe (`let id <auto> = fun <T>...`): it has
    // no value until a call instantiates it. Both are excluded from lambda
    // capture analysis -- capturing one would snapshot a slot that does not
    // exist -- while an `fn`-typed parameter or closure variable stays
    // capturable: it holds a runtime closure pair.
    //
    // Defaulted, so the one aggregate initialisation of Symbol (in `bound`,
    // below) means what it meant: a symbol is data unless something says
    // otherwise.
    bool is_function = false;
    bool is_template = false;

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
    // Defaulted, so the one aggregate initialisation of Symbol (in `bound`,
    // below) means what it meant: a symbol is not ambient unless something
    // says it is.
    bool is_ambient = false;

    // Whether this binding was pre-registered by the file-scope hoist
    // (Analyzer_Core.cpp passes 3-4) rather than by the in-order walk. The hoist
    // runs before any statement is analysed so forward references resolve; the
    // in-order declaration then re-registers the same name in the same scope,
    // which is not a redeclaration. The re-registration overwrites this entry
    // with a fresh Symbol, so the flag consumes itself: a second textual
    // declaration finds it cleared and is refused.
    //
    // Defaulted, so the one aggregate initialisation of Symbol (in `bound`,
    // below) means what it meant: a symbol is walked unless the hoist says
    // otherwise.
    bool is_hoisted = false;

    // Whether this binding is a prototype (`@define`) rather than a definition.
    // A `fun` of the same name in the same scope is then the definition the
    // prototype declared, not a redeclaration -- `@define` is the corpus's
    // forward declaration (eight samples open with one) -- and a second
    // identical `@define` states one fact twice, not a conflict (fourteen
    // samples repeat the bundled module's `printf` line verbatim). Anything
    // else after a prototype is a duplicate like anywhere else.
    //
    // Defaulted: a symbol defines unless an `@define` says it prototypes.
    bool is_prototype = false;

    // The one way a binding is registered. Every Scope::define call states the
    // four fields a variable has -- name, type, mutability, and whether an
    // initialiser ran -- and every defaulted field stays defaulted here, in
    // this one list. Stated once on purpose: a field added to Symbol surfaces
    // at this one construction instead of defaulting silently at every call
    // site, and the warning for a field with no default member initialiser
    // points here and nowhere else.
    static Symbol bound(std::string name, std::shared_ptr<Type> type,
                        bool is_mutable, bool is_initialized = true) {
        return Symbol{.name = std::move(name), .type = type, .is_mutable = is_mutable,
                      .is_initialized = is_initialized, .slaveof = {}};
    }
};

class Scope {
public:
    Scope* parent;
    std::unordered_map<std::string, Symbol> symbols;
    std::unordered_map<std::string, std::shared_ptr<Type>> types;
    
    std::unordered_map<std::string, MacroDeclaration*> macros;

    // Names this scope declares without `pub` (or `@special(pub)`). A package
    // import (`import { X } from m`, `import * from m`) binds only public
    // names; a quoted file import is exempt. Absent means public: builtins and
    // externs predate the record, and `pub @macro` is a syntax error so macros
    // need none. Populated where declarations register, propagated where
    // imports copy -- never consulted for anything but import gating.
    std::unordered_set<std::string> privateSymbols;
    std::unordered_set<std::string> privateTypes;

    // Namespaces this file declares (`Program::declared_namespaces`,
    // published by the analyzer at Program entry). A namespace block splices
    // its contents and keeps no scope, so this record is what the #90 strict
    // rule checks: an import's `::` tail against the module scope's record,
    // an extern qualifier against the importing scope's own.
    std::unordered_set<std::string> declaredNamespaces;

    bool isNamespaceDeclared(const std::string& name) const {
        if (declaredNamespaces.count(name)) return true;
        if (parent) return parent->isNamespaceDeclared(name);
        return false;
    }

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