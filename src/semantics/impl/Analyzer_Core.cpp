#include "../SemanticAnalyzer.hpp"
#include "../../types/TypeImpl.hpp"
#include <fmt/core.h>
#include <fmt/color.h>

namespace fin {

SemanticAnalyzer::SemanticAnalyzer(DiagnosticEngine& d, bool debug) 
    : diag(d), debugMode(debug) {
    
    // Create Global Scope
    globalScope = std::make_shared<Scope>(nullptr);
    currentScope = globalScope;
    scopeStack.push_back(globalScope);
    
    // Builtins
    currentScope->defineType("int", std::make_shared<PrimitiveType>("int"));
    currentScope->defineType("float", std::make_shared<PrimitiveType>("float"));
    currentScope->defineType("void", std::make_shared<PrimitiveType>("void"));
    currentScope->defineType("bool", std::make_shared<PrimitiveType>("bool"));
    currentScope->defineType("string", std::make_shared<PrimitiveType>("string"));
    currentScope->defineType("auto", std::make_shared<PrimitiveType>("auto"));
    
    // Extended Primitives
    currentScope->defineType("char", std::make_shared<PrimitiveType>("char"));
    currentScope->defineType("long", std::make_shared<PrimitiveType>("long"));
    currentScope->defineType("double", std::make_shared<PrimitiveType>("double"));
    currentScope->defineType("short", std::make_shared<PrimitiveType>("short"));
    currentScope->defineType("uint", std::make_shared<PrimitiveType>("uint"));
    currentScope->defineType("ulong", std::make_shared<PrimitiveType>("ulong"));
    currentScope->defineType("ushort", std::make_shared<PrimitiveType>("ushort"));
    
    // Mock Castable
    currentScope->defineType("Castable", std::make_shared<StructType>("Castable"));
}

SemanticAnalyzer::~SemanticAnalyzer() {}

void SemanticAnalyzer::enterScope() {
    auto newScope = std::make_shared<Scope>(currentScope.get());
    currentScope = newScope;
    scopeStack.push_back(newScope);
}

void SemanticAnalyzer::exitScope() {
    if (scopeStack.size() > 1) {
        scopeStack.pop_back();
        currentScope = scopeStack.back();
    }
}

std::shared_ptr<Type> SemanticAnalyzer::resolveTypeFromAST(TypeNode* node) {
    if (!node) return nullptr;
    
    // 1. Pointer Type
    if (auto* ptrNode = dynamic_cast<PointerTypeNode*>(node)) {
        auto inner = resolveTypeFromAST(ptrNode->pointee.get());
        return std::make_shared<PointerType>(inner);
    }

    // 2. Array Type (FIXED: Validate Size)
    if (auto* arrNode = dynamic_cast<ArrayTypeNode*>(node)) {
        auto inner = resolveTypeFromAST(arrNode->element_type.get());
        bool fixed = (arrNode->size != nullptr);
        
        if (fixed) {
            // Analyze the size expression
            arrNode->size->accept(*this);
            
            // Ensure it evaluates to an integer
            auto intType = currentScope->resolveType("int");
            if (lastExprType) {
                if (!checkType(*arrNode->size, lastExprType, intType)) {
                    error(*arrNode->size, "Array size must be an integer");
                }
            }
        }
        
        return std::make_shared<ArrayType>(inner, fixed);
    }

    // 3. Function Type
    if (auto* fnNode = dynamic_cast<FunctionTypeNode*>(node)) {
        std::vector<std::shared_ptr<Type>> pTypes;
        for(auto& p : fnNode->param_types) {
            pTypes.push_back(resolveTypeFromAST(p.get()));
        }
        auto rType = resolveTypeFromAST(fnNode->return_type.get());
        return std::make_shared<FunctionType>(pTypes, rType);
    }

    // 4. Base Type (Identifier)
    auto type = currentScope->resolveType(node->name);
    if (!type) {
        error(*node, "Undefined type '" + node->name + "'");
        return nullptr;
    }
    
    // 5. Generics
    if (!node->generics.empty()) {
        std::vector<std::shared_ptr<Type>> args;
        auto structDef = std::dynamic_pointer_cast<StructType>(type);
        
        for(size_t i = 0; i < node->generics.size(); ++i) {
            auto argType = resolveTypeFromAST(node->generics[i].get());
            args.push_back(argType);
            
            if (structDef && i < structDef->generic_args.size()) {
                auto genParam = std::dynamic_pointer_cast<GenericType>(structDef->generic_args[i]);
                if (genParam && genParam->constraint) {
                    checkConstraint(node->generics[i].get(), argType, genParam->constraint);
                }
            }
        }
        
        if (structDef) {
             auto instantiated = structDef->instantiate(args);
             if (instantiated) type = instantiated;
             else error(*node, "Generic count mismatch");
        } else {
             type = std::make_shared<StructType>(node->name, args);
        }
    }
    
    return type;
}

bool SemanticAnalyzer::checkConstraint(TypeNode* typeNode, std::shared_ptr<Type> actualType, std::shared_ptr<Type> constraint) {
    if (!constraint) return true;

    if (auto* iface = dynamic_cast<StructType*>(constraint.get())) {
        if (auto* st = dynamic_cast<StructType*>(actualType.get())) {
            if (!st->implements(iface)) {
                error(*typeNode, fmt::format("Type '{}' does not implement interface '{}'", 
                    actualType->toString(), iface->toString()));
                return false;
            }
        }
    }
    return true;
}

void SemanticAnalyzer::error(ASTNode& node, const std::string& msg) {
    diag.reportError(node.loc, msg);
    hasError = true;
}

bool SemanticAnalyzer::checkType(ASTNode& node, std::shared_ptr<Type> actual, std::shared_ptr<Type> expected) {
    if (!actual || !expected) return false;
    
    if (!actual->isAssignableTo(*expected)) {
        error(node, fmt::format("Type mismatch: expected '{}', got '{}'", expected->toString(), actual->toString()));
        return false;
    }
    return true;
}

void SemanticAnalyzer::visit(Parameter& node) {
    resolveTypeFromAST(node.type.get());
    if (node.default_value) node.default_value->accept(*this);
}

void SemanticAnalyzer::visit(StructMember& node) {
    resolveTypeFromAST(node.type.get());
    if (node.default_value) node.default_value->accept(*this);
}

void SemanticAnalyzer::visit(PointerTypeNode& node) { resolveTypeFromAST(&node); }
void SemanticAnalyzer::visit(ArrayTypeNode& node) { resolveTypeFromAST(&node); }


void SemanticAnalyzer::visit(TypeNode& node) { resolveTypeFromAST(&node); }
void SemanticAnalyzer::visit(FunctionTypeNode& node) { resolveTypeFromAST(&node); }

}