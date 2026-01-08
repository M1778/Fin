#include "../MacroExpander.hpp"
#include "../../types/TypeImpl.hpp" 
#include "../../utils/ModuleLoader.hpp"
#include <filesystem>
#include <fmt/core.h>

namespace fin {

// --- Registration ---
void MacroExpander::visit(MacroDeclaration& node) {
    if (currentScope) {
        currentScope->defineMacro(node.name, &node);
    }
}

// --- Import Handling ---
void MacroExpander::visit(ImportModule& node) {
    if (!loader) return;

    // Load the module to get its macros
    auto moduleScope = loader->loadModule(node.source, node.is_package);
    if (!moduleScope) return; 

    // Case 1: Specific Imports
    if (!node.targets.empty()) {
        for (const auto& target : node.targets) {
            if (auto* macro = moduleScope->resolveMacro(target)) {
                currentScope->defineMacro(target, macro);
            }
        }
        return;
    }

    // Case 2: Namespace Import
    std::string alias = node.alias;
    if (alias.empty()) {
        std::filesystem::path p(node.source);
        alias = p.stem().string();
    }
    
    // Define a symbol with NamespaceType that holds the scope
    auto nsType = std::make_shared<NamespaceType>(alias, moduleScope);
    
    Symbol sym{alias, nsType, false, true};
    currentScope->define(sym);
}

void MacroExpander::visit(Program& node) { for (auto& stmt : node.statements) stmt->accept(*this); }

void MacroExpander::visit(FunctionDeclaration& node) {
    for (auto& param : node.params) {
        param->accept(*this);
    }
    if (node.return_type) {
        node.return_type->accept(*this);
    }
    if (node.body) node.body->accept(*this);
}
void MacroExpander::visit(StructDeclaration& node) {
    for (auto& member : node.members) {
        member->accept(*this);
    }

    for (auto& method : node.methods) method->accept(*this);
    for (auto& op : node.operators) op->accept(*this);
    for (auto& ctor : node.constructors) if(ctor->body) ctor->body->accept(*this);
    if (node.destructor && node.destructor->body) node.destructor->body->accept(*this);
}

void MacroExpander::visit(ClassDeclaration& node) {
    for (auto& member : node.members) {
        member->accept(*this);
    }

    for (auto& method : node.methods) method->accept(*this);
    for (auto& op : node.operators) op->accept(*this);
    for (auto& ctor : node.constructors) if(ctor->body) ctor->body->accept(*this);
    if (node.destructor && node.destructor->body) node.destructor->body->accept(*this);
}
void MacroExpander::visit(OperatorDeclaration& node) {
    for (auto& param : node.params) param->accept(*this);
    if (node.return_type) node.return_type->accept(*this);
    
    if (node.body) node.body->accept(*this);
}
void MacroExpander::visit(ConstructorDeclaration& node) { if (node.body) node.body->accept(*this); }
void MacroExpander::visit(DestructorDeclaration& node) { if (node.body) node.body->accept(*this); }

void MacroExpander::visit(EnumDeclaration& node) {
    for (auto& val : node.values) {
        if (val.second) {
            val.second->accept(*this);
            if (expandedExpression) {
                val.second = std::move(expandedExpression);
                expandedExpression = nullptr;
            }
        }
    }
}

void MacroExpander::visit(DefineDeclaration& node) {
    for (auto& param : node.params) {
        param->accept(*this);
    }
    if (node.return_type) {
        node.return_type->accept(*this);
    }
}

void MacroExpander::visit(InterfaceDeclaration& node) {
    for (auto& member : node.members) {
        member->accept(*this);
    }
    for (auto& method : node.methods) {
        method->accept(*this);
    }
}

// Helpers
void MacroExpander::visit(Parameter& node) {
    if (node.type) node.type->accept(*this);
    if (node.default_value) {
        node.default_value->accept(*this);
        if (expandedExpression) {
            node.default_value = std::move(expandedExpression);
            expandedExpression = nullptr;
        }
    }
}

void MacroExpander::visit(StructMember& node) {
    if (node.type) node.type->accept(*this);
    if (node.default_value) {
        node.default_value->accept(*this);
        if (expandedExpression) {
            node.default_value = std::move(expandedExpression);
            expandedExpression = nullptr;
        }
    }
}

void MacroExpander::visit(TypeDefinition& node) {
    if (node.aliased_type) node.aliased_type->accept(*this);
    for (auto& impl : node.implements_list) {
        impl->accept(*this);
    }
}

void MacroExpander::visit(SpecialDeclaration& node) {
    for (auto& param : node.params) {
        param->accept(*this);
    }
    if (node.return_type) node.return_type->accept(*this);
    if (node.body) node.body->accept(*this);
}

void MacroExpander::visit(ImplementsBlock& node) {
    if (node.target_type) node.target_type->accept(*this);
    for (auto& method : node.methods) method->accept(*this);
    for (auto& op : node.operators) op->accept(*this);
}

}
