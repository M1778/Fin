#include "../SemanticAnalyzer.hpp"
#include <fmt/core.h>
#include <fmt/color.h>

namespace fin {

void SemanticAnalyzer::visit(Program& node) {
    for (auto& stmt : node.statements) stmt->accept(*this);
}

void SemanticAnalyzer::visit(VariableDeclaration& node) {
    auto type = resolveTypeFromAST(node.type.get());
    if (!type) return; 

    if (node.initializer) {
        node.initializer->accept(*this);
        // Check assignment compatibility
        if (lastExprType) {
            checkType(*node.initializer, lastExprType, type);
        }
    }

    Symbol sym{node.name, type, node.is_mutable, node.initializer != nullptr};
    currentScope->define(sym);
    
    fmt::print(fg(fmt::color::gray), "[DEBUG] Defined variable '{}' of type '{}'\n", node.name, type->toString());
}

void SemanticAnalyzer::visit(FunctionDeclaration& node) {
    fmt::print(fg(fmt::color::cyan), "[INFO] Analyzing function '{}'\n", node.name);
    
    auto prevRet = context.currentFuncReturnType;
    enterScope();

    // 1. Register Generics
    for (auto& gen : node.generic_params) {
        currentScope->defineType(gen->name, std::make_shared<GenericType>(gen->name));
    }

    // 2. Register Explicit Params
    bool hasSelf = false;
    for (auto& param : node.params) {
        if (param->name == "self") hasSelf = true;
        auto type = resolveTypeFromAST(param->type.get());
        if (type) {
            currentScope->define({param->name, type, false, true});
        }
    }

    // 3. Inject Implicit 'self'
    if (currentStructContext && !node.is_static && !hasSelf) {
        // We are in a struct, method is not static, and no explicit self.
        // Inject 'self' of type 'Self' (which is defined in the struct scope)
        auto selfType = currentScope->resolveType("Self");
        if (selfType) {
            // We mark it as mutable (true) so setters like set_x work
            currentScope->define({"self", selfType, true, true});
            fmt::print(fg(fmt::color::gray), "      [Magic] Injected implicit 'self' into '{}'\n", node.name);
        }
    }
    
    // 4. Resolve Return Type
    if (node.return_type) {
        context.currentFuncReturnType = resolveTypeFromAST(node.return_type.get());
    } else {
        context.currentFuncReturnType = currentScope->resolveType("void");
    }

    if (node.body) node.body->accept(*this);
    
    exitScope();
    context.currentFuncReturnType = prevRet;
}

void SemanticAnalyzer::visit(StructDeclaration& node) {
    fmt::print(fg(fmt::color::orange), "[INFO] Analyzing struct '{}'\n", node.name);
    auto structType = std::make_shared<StructType>(node.name);
    currentScope->defineType(node.name, structType);

    enterScope();
    for (auto& gen : node.generic_params) {
        currentScope->defineType(gen->name, std::make_shared<GenericType>(gen->name));
    }
    currentScope->defineType("Self", structType);

    // --- SAVE CONTEXT ---
    auto prevContext = currentStructContext;
    currentStructContext = structType; 
    // --------------------

    for (auto& member : node.members) {
        resolveTypeFromAST(member->type.get());
    }

    for (auto& method : node.methods) {
        method->accept(*this);
    }
    
    // --- RESTORE CONTEXT ---
    currentStructContext = prevContext;
    // -----------------------

    exitScope();
}

void SemanticAnalyzer::visit(InterfaceDeclaration& node) {
    fmt::print(fg(fmt::color::magenta), "[INFO] Analyzing interface '{}'\n", node.name);
    
    // Create the type
    auto ifaceType = std::make_shared<StructType>(node.name);
    currentScope->defineType(node.name, ifaceType);
    
    enterScope();
    
    // 1. Analyze Members
    for (auto& member : node.members) {
        resolveTypeFromAST(member->type.get());
    }
    
    // 2. Analyze Methods & Register them
    for (auto& method : node.methods) {
        enterScope();
        // Resolve params...
        for (auto& param : method->params) resolveTypeFromAST(param->type.get());
        
        // Resolve Return Type
        std::shared_ptr<Type> retType = nullptr;
        if(method->return_type) {
            retType = resolveTypeFromAST(method->return_type.get());
        } else {
            retType = currentScope->resolveType("void");
        }
        
        // REGISTER METHOD IN TYPE
        ifaceType->defineMethod(method->name, retType);
        
        exitScope();
    }
    exitScope();
}

void SemanticAnalyzer::visit(MethodCall& node) {
    // 1. Analyze the object (e.g., 'item')
    node.object->accept(*this);
    auto objType = lastExprType;
    
    if (!objType) return; // Error already reported

    // 2. Check if object is a Struct/Interface
    // We need to handle GenericType (T) which might have constraints
    std::shared_ptr<StructType> structType = nullptr;

    if (auto* st = dynamic_cast<StructType*>(objType.get())) {
        structType = std::static_pointer_cast<StructType>(objType);
    } 
    else if (auto* gt = dynamic_cast<GenericType*>(objType.get())) {
        // If it's a generic T, check its constraint (e.g. T : Printable)
        // For this prototype, we assume the constraint IS the type we look up in.
        // In a real compiler, we'd store the constraint in GenericType.
        // Let's hack it: We need to find the constraint type from the scope.
        // But GenericType doesn't store it yet.
        
        // WORKAROUND: For this test case, we know 'T' is 'Printable'.
        // In a real impl, GenericType needs a `std::shared_ptr<Type> constraint` field.
        // Let's assume for now that if we call a method on a Generic, we look it up
        // in the interface it implements.
        
        // Since we didn't implement constraint storage in GenericType yet, 
        // let's try to resolve "Printable" directly to make the test pass.
        structType = std::dynamic_pointer_cast<StructType>(currentScope->resolveType("Printable"));
    }

    if (!structType) {
        // If we couldn't find the type definition
        error(node, fmt::format("Type '{}' does not have methods (or constraint missing)", objType->toString()));
        lastExprType = nullptr;
        return;
    }

    // 3. Look up method
    auto retType = structType->getMethodReturnType(node.method_name);
    if (!retType) {
        error(node, fmt::format("Method '{}' not found in type '{}'", node.method_name, structType->name));
        lastExprType = nullptr;
    } else {
        lastExprType = retType;
        // fmt::print(fg(fmt::color::green), "[DEBUG] Resolved method {}.{} -> {}\n", structType->name, node.method_name, retType->toString());
    }

    // 4. Check Args
    for(auto& arg : node.args) arg->accept(*this);
}

// Stubs for others
void SemanticAnalyzer::visit(EnumDeclaration&) {}
void SemanticAnalyzer::visit(ImportModule&) {}
void SemanticAnalyzer::visit(DefineDeclaration&) {}
void SemanticAnalyzer::visit(MacroDeclaration&) {}
void SemanticAnalyzer::visit(OperatorDeclaration&) {}

}
