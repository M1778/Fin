#include "../SemanticAnalyzer.hpp"
#include <fmt/core.h>
#include <fmt/color.h>

namespace fin {

SemanticAnalyzer::SemanticAnalyzer() {
    currentScope = new Scope(nullptr);
    // Builtins
    currentScope->defineType("int", std::make_shared<PrimitiveType>("int"));
    currentScope->defineType("float", std::make_shared<PrimitiveType>("float"));
    currentScope->defineType("void", std::make_shared<PrimitiveType>("void"));
    currentScope->defineType("bool", std::make_shared<PrimitiveType>("bool"));
    currentScope->defineType("string", std::make_shared<PrimitiveType>("string"));
    currentScope->defineType("auto", std::make_shared<PrimitiveType>("auto"));
    
    // Mock Castable
    currentScope->defineType("Castable", std::make_shared<StructType>("Castable"));
}

SemanticAnalyzer::~SemanticAnalyzer() {
    Scope* s = currentScope;
    while(s) {
        Scope* p = s->parent;
        delete s;
        s = p;
    }
}

void SemanticAnalyzer::enterScope() {
    currentScope = new Scope(currentScope);
}

void SemanticAnalyzer::exitScope() {
    Scope* old = currentScope;
    currentScope = currentScope->parent;
    delete old; 
}

std::shared_ptr<Type> SemanticAnalyzer::resolveTypeFromAST(TypeNode* node) {
    if (!node) return nullptr;
    auto type = currentScope->resolveType(node->name);
    if (!type) {
        error(*node, "Undefined type '" + node->name + "'");
        return nullptr;
    }
    if (!node->generics.empty()) {
        std::vector<std::shared_ptr<Type>> args;
        for(auto& g : node->generics) {
            auto argType = resolveTypeFromAST(g.get());
            if(argType) args.push_back(argType);
        }
        return std::make_shared<StructType>(node->name, args);
    }
    return type;
}

void SemanticAnalyzer::error(ASTNode& node, const std::string& msg) {
    fmt::print(fg(fmt::color::red), "[ERROR] Line {}: {}\n", node.line, msg);
    hasError = true;
}

bool SemanticAnalyzer::checkType(ASTNode& node, std::shared_ptr<Type> actual, std::shared_ptr<Type> expected) {
    if (!actual || !expected) return false;
    if (expected->toString() == "auto") return true; // Auto matches anything
    
    if (!actual->equals(*expected)) {
        error(node, fmt::format("Type mismatch: expected '{}', got '{}'", expected->toString(), actual->toString()));
        return false;
    }
    return true;
}

}
