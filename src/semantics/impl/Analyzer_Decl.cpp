#include "../SemanticAnalyzer.hpp"
#include "../../utils/ModuleLoader.hpp"
#include "../../types/TypeImpl.hpp"
#include <fmt/core.h>
#include <fmt/color.h>
#include <filesystem>

namespace fin {

void SemanticAnalyzer::visit(Program& node) {
    for (auto& stmt : node.statements) stmt->accept(*this);
}

void SemanticAnalyzer::visit(VariableDeclaration& node) {
    auto type = resolveTypeFromAST(node.type.get());
    if (!type) return; 

    if (node.initializer) {
        node.initializer->accept(*this);
        if (lastExprType) {
            // Type inference for auto keyword
            if (type->toString() == "auto") {
                type = lastExprType;
                debugLog(fg(fmt::color::green), "      [Inference] Inferred type '{}' for variable '{}'\n", type->toString(), node.name);
            } else {
                checkType(*node.initializer, lastExprType, type);
            }
        }
    }

    Symbol sym{node.name, type, node.is_mutable, node.initializer != nullptr};
    currentScope->define(sym);
    
    debugLog(fg(fmt::color::gray), "[DEBUG] Defined variable '{}' of type '{}'\n", node.name, type->toString());
}

void SemanticAnalyzer::visit(FunctionDeclaration& node) {
    debugLog(fg(fmt::color::cyan), "[INFO] Analyzing function '{}'\n", node.name);
    
    auto prevRet = context.currentFuncReturnType;
    
    // 1. Enter Scope for the function body (to hold generics and params)
    enterScope();

    // 2. Register Generics (e.g. <T>)
    for (auto& gen : node.generic_params) {
        currentScope->defineType(gen->name, std::make_shared<GenericType>(gen->name));
    }

    // 3. Resolve Parameters & Build Signature
    std::vector<std::shared_ptr<Type>> paramTypes;
    bool hasSelf = false;
    
    for (auto& param : node.params) {
        if (param->name == "self") hasSelf = true;
        
        auto type = resolveTypeFromAST(param->type.get());
        if (type) {
            // Define in body scope so the code can use the param
            currentScope->define({param->name, type, false, true});
            // Register in signature
            paramTypes.push_back(type);
        }
    }

    // 4. Implicit Self Injection
    if (currentStructContext && !node.is_static && !hasSelf) {
        auto selfType = currentScope->resolveType("Self");
        if (selfType) {
            currentScope->define({"self", selfType, true, true});
            debugLog(fg(fmt::color::gray), "      [Magic] Injected implicit 'self' into '{}'\n", node.name);
        }
    }
    
    // 5. Resolve Return Type
    std::shared_ptr<Type> retType;
    if (node.return_type) {
        retType = resolveTypeFromAST(node.return_type.get());
    } else {
        retType = currentScope->resolveType("void");
    }
    context.currentFuncReturnType = retType;

    // 6. REGISTER FUNCTION IN PARENT SCOPE (CRITICAL FIX)
    // Register 'add' and 'compute' in the Global Scope (currentScope->parent)
    // so that 'main' can find them later.
    if (currentScope->parent) {
        auto funcType = std::make_shared<FunctionType>(paramTypes, retType);
        // Mark as immutable and initialized
        currentScope->parent->define({node.name, funcType, false, true});
        debugLog(fg(fmt::color::gray), "      [Register] Registered function '{}' in parent scope\n", node.name);
    }

    // 7. Analyze Body
    if (node.body) node.body->accept(*this);
    
    if (node.body && !node.return_type->name.empty() && node.return_type->name != "void" && node.return_type->name != "noret") {
        // Check if void/noret was resolved to actual void type
        if (context.currentFuncReturnType->toString() != "void") {
            if (!checkReturnPaths(node.body.get())) {
                error(node, fmt::format("Function '{}' is missing a return statement on some paths", node.name));
            }
        }
    }

    exitScope();
    context.currentFuncReturnType = prevRet;
}

void SemanticAnalyzer::visit(StructDeclaration& node) {
    debugLog(fg(fmt::color::orange), "[INFO] Analyzing struct '{}'\n", node.name);

    auto structType = std::make_shared<StructType>(node.name);
    currentScope->defineType(node.name, structType);

    enterScope();

    // --- SETUP GENERICS ---
    for (auto& gen : node.generic_params) {
        auto genType = std::make_shared<GenericType>(gen->name);
        if (gen->constraint) {
            auto constraintType = resolveTypeFromAST(gen->constraint.get());
            if (constraintType) {
                debugLog(fg(fmt::color::gray), "      [Constraint] Generic '{}' : '{}'\n", gen->name, constraintType->toString());
            }
        }
        currentScope->defineType(gen->name, genType);
        structType->generic_args.push_back(genType);
    }

    currentScope->defineType("Self", std::make_shared<SelfType>(structType));

    // --- INHERITANCE ---
    for (auto& parentNode : node.parents) {
        auto parentType = resolveTypeFromAST(parentNode.get());
        if (parentType) {
            if (auto p = std::dynamic_pointer_cast<StructType>(parentType)) {
                structType->parents.push_back(p);
                debugLog(fg(fmt::color::gray), "      [Inheritance] Inherits/Implements '{}'\n", p->toString());
            } else {
                error(*parentNode, "Parent type '" + parentType->toString() + "' is not a struct/interface");
            }
        }
    }

    // =========================================================
    // PASS 1: REGISTRATION (Signatures Only)
    // =========================================================
    
    // 1. Members
    for (auto& member : node.members) {
        auto memberType = resolveTypeFromAST(member->type.get());
        if (memberType) {
            // Check pointer_depth == 0
            if (memberType->equals(*structType) && member->type->pointer_depth == 0) {
                error(*member, "Recursive struct member '" + member->name + "' must be a pointer");
            }
            structType->defineField(member->name, memberType, member->is_public);
        }
        if (member->default_value) {
            member->default_value->accept(*this);
            if (lastExprType && memberType) {
                checkType(*member->default_value, lastExprType, memberType);
            }
        }
    }

    // 2. Methods (Signatures)
    for (auto& method : node.methods) {
        std::shared_ptr<Type> retType = nullptr;
        if (method->return_type) retType = resolveTypeFromAST(method->return_type.get());
        else retType = currentScope->resolveType("void");
        
        if (retType) structType->defineMethod(method->name, retType);
        
        // Do not call accept here 
        // That triggers body analysis too early.
    }

    // 3. Operators (Signatures)
    for (auto& op : node.operators) {
        std::shared_ptr<Type> retType = nullptr;
        if (op->return_type) retType = resolveTypeFromAST(op->return_type.get());
        else retType = currentScope->resolveType("void");
        
        if (retType) structType->defineOperator((int)op->op, retType);
    }

    // 4. Constructors (Signatures)
    for (auto& ctor : node.constructors) {
        // Resolve params in a temp scope to get signature
        enterScope();
        std::vector<std::shared_ptr<Type>> paramTypes;
        for (auto& param : ctor->params) {
            auto t = resolveTypeFromAST(param->type.get());
            if (t) paramTypes.push_back(t);
        }
        exitScope();

        auto ctorType = std::make_shared<FunctionType>(paramTypes, structType);
        structType->addConstructor(ctorType);
        debugLog(fg(fmt::color::green), "      [Ctor] Registered constructor for '{}' with {} params\n", node.name, paramTypes.size());
    }

    // =========================================================
    // PASS 2: ANALYSIS (Bodies)
    // =========================================================

    auto prevContext = currentStructContext;
    currentStructContext = structType; 

    // 1. Member Defaults
    for (auto& member : node.members) {
        if (member->default_value) {
            member->default_value->accept(*this);
            auto memberType = structType->getFieldType(member->name);
            if (lastExprType && memberType) {
                checkType(*member->default_value, lastExprType, memberType);
            }
        }
    }

    // 2. Method Bodies (FIXED: Analyze bodies now)
    for (auto& method : node.methods) {
        method->accept(*this);
    }

    // 3. Operator Bodies
    for (auto& op : node.operators) {
        enterScope();
        for (auto& gen : op->generic_params) currentScope->defineType(gen->name, std::make_shared<GenericType>(gen->name));
        for (auto& param : op->params) {
            auto t = resolveTypeFromAST(param->type.get());
            if (t) currentScope->define({param->name, t, false, true});
        }
        currentScope->define({"self", structType, true, true});
        
        std::shared_ptr<Type> retType = nullptr;
        if (op->return_type) retType = resolveTypeFromAST(op->return_type.get());
        else retType = currentScope->resolveType("void");

        if (op->body) {
            auto prevRet = context.currentFuncReturnType;
            context.currentFuncReturnType = retType;
            op->body->accept(*this);
            context.currentFuncReturnType = prevRet;
        }
        exitScope();
    }

    // 4. Constructor Bodies
    for (auto& ctor : node.constructors) {
        enterScope();
        for (auto& param : ctor->params) {
            auto t = resolveTypeFromAST(param->type.get());
            if (t) currentScope->define({param->name, t, false, true});
        }
        // Inject Self
        currentScope->define({"self", structType, true, true});
        
        if (ctor->body) ctor->body->accept(*this);
        exitScope();
    }

    // 5. Destructor Body
    if (node.destructor) {
        structType->has_destructor = true;
        enterScope();
        currentScope->define({"self", structType, true, true});
        if (node.destructor->body) node.destructor->body->accept(*this);
        exitScope();
    }

    // --- CONFORMANCE CHECK ---
    for (auto& parent : structType->parents) {
        if (auto p = std::dynamic_pointer_cast<StructType>(parent)) {
            if (p->is_interface) {
                debugLog(fg(fmt::color::gray), "[DEBUG] Checking if '{}' implements '{}'\n", node.name, p->name);
                if (!structType->implements(p.get())) {
                    error(node, fmt::format("Struct '{}' does not implement interface '{}'", node.name, p->name));
                }
            }
        }
    }

    currentStructContext = prevContext;
    exitScope();
}


void SemanticAnalyzer::visit(OperatorDeclaration& node) {
    // Operators are always inside structs (for now)
    if (!currentStructContext) {
        error(node, "Operator declaration outside of struct");
        return;
    }
    
    auto structType = std::dynamic_pointer_cast<StructType>(currentStructContext);
    if (!structType) return;

    debugLog(fg(fmt::color::cyan), "[INFO] Analyzing operator '{}' for {}\n", (int)node.op, structType->name);

    enterScope();
    
    // 1. Generics
    for (auto& gen : node.generic_params) {
        currentScope->defineType(gen->name, std::make_shared<GenericType>(gen->name));
    }
    
    // 2. Params
    for (auto& param : node.params) {
        auto t = resolveTypeFromAST(param->type.get());
        if (t) currentScope->define({param->name, t, false, true});
    }
    
    // 3. Inject Self
    currentScope->define({"self", structType, true, true});

    // 4. Return Type
    std::shared_ptr<Type> retType = nullptr;
    if (node.return_type) {
        retType = resolveTypeFromAST(node.return_type.get());
    } else {
        retType = currentScope->resolveType("void");
    }
    
    // 5. Register in Struct
    if (retType) {
        structType->defineOperator((int)node.op, retType);
    }

    // 6. Body
    if (node.body) {
        auto prevRet = context.currentFuncReturnType;
        context.currentFuncReturnType = retType;
        node.body->accept(*this);
        context.currentFuncReturnType = prevRet;
    }
    
    exitScope();
}

void SemanticAnalyzer::visit(MacroDeclaration& node) {
    debugLog(fg(fmt::color::magenta), "[INFO] Registering macro '{}'\n", node.name);
    // Macros are handled in a separate expansion pass.
    // Validate no symbol clashes that it doesn't clash with existing symbols if we wanted to.
}

void SemanticAnalyzer::visit(ConstructorDeclaration& node) {
    // Logic is primarily handled inside StructDeclaration to manage 'self' and type registration.
    // Validate body
    if (node.body) node.body->accept(*this);
}

void SemanticAnalyzer::visit(DestructorDeclaration& node) {
    if (node.body) node.body->accept(*this);
}

void SemanticAnalyzer::visit(InterfaceDeclaration& node) {
    debugLog(fg(fmt::color::magenta), "[INFO] Analyzing interface '{}'\n", node.name);
    auto ifaceType = std::make_shared<StructType>(node.name);
    ifaceType->is_interface = true;
    currentScope->defineType(node.name, ifaceType);
    
    enterScope();
    for (auto& gen : node.generic_params) {
        currentScope->defineType(gen->name, std::make_shared<GenericType>(gen->name));
    }
    currentScope->defineType("Self", ifaceType);

    for (auto& member : node.members) resolveTypeFromAST(member->type.get());
    
    for (auto& method : node.methods) {
        enterScope();
        for (auto& param : method->params) resolveTypeFromAST(param->type.get());
        std::shared_ptr<Type> retType = nullptr;
        if(method->return_type) retType = resolveTypeFromAST(method->return_type.get());
        else retType = currentScope->resolveType("void");
        
        ifaceType->defineMethod(method->name, retType);
        exitScope();
    }
    
    for (auto& op : node.operators) {
        enterScope();
        for (auto& gen : op->generic_params) currentScope->defineType(gen->name, std::make_shared<GenericType>(gen->name));
        for (auto& param : op->params) resolveTypeFromAST(param->type.get());
        std::shared_ptr<Type> retType = nullptr;
        if(op->return_type) retType = resolveTypeFromAST(op->return_type.get());
        else retType = currentScope->resolveType("void");
        
        ifaceType->defineOperator((int)op->op, retType);
        exitScope();
    }

    for (auto& ctor : node.constructors) {
        enterScope();
        std::vector<std::shared_ptr<Type>> paramTypes;
        for (auto& param : ctor->params) {
            auto t = resolveTypeFromAST(param->type.get());
            if (t) paramTypes.push_back(t);
        }
        auto ctorType = std::make_shared<FunctionType>(paramTypes, ifaceType);
        ifaceType->addConstructor(ctorType);
        debugLog(fg(fmt::color::gray), "      [Interface] Added constructor requirement\n");
        exitScope();
    }

    if (node.destructor) {
        ifaceType->has_destructor = true;
        debugLog(fg(fmt::color::gray), "      [Interface] Added destructor requirement\n");
    }
    
    exitScope();
}

void SemanticAnalyzer::visit(EnumDeclaration& node) {
    debugLog(fg(fmt::color::yellow), "[INFO] Analyzing enum '{}'\n", node.name);
    auto enumType = std::make_shared<PrimitiveType>(node.name); 
    currentScope->defineType(node.name, enumType);
    
    for (auto& val : node.values) {
        if (val.second) {
            val.second->accept(*this);
            auto intType = currentScope->resolveType("int");
            checkType(*val.second, lastExprType, intType);
        }
        currentScope->define({val.first, enumType, false, true});
        debugLog(fg(fmt::color::gray), "      [Enum] Member '{}'\n", val.first);
    }
}

void SemanticAnalyzer::visit(ImportModule& node) {
    if (!loader) return;

    auto moduleScope = loader->loadModule(node.source, node.is_package);
    
    if (!moduleScope) {
        error(node, "Failed to load module '" + node.source + "'");
        return;
    }

    // Case 1: Specific Imports: import { A, B } from "lib"
    if (!node.targets.empty()) {
        for (const auto& target : node.targets) {
            bool found = false;
            if (auto* sym = moduleScope->resolve(target)) {
                currentScope->define(*sym); // Copy symbol
                found = true;
            }
            if (auto type = moduleScope->resolveType(target)) {
                currentScope->defineType(target, type); // Copy type
                found = true;
            }
            if (!found) error(node, "Module '" + node.source + "' does not export '" + target + "'");
        }
        return;
    }

    // Case 2: Aliased Import: import "lib" as L
    // OR Default Alias: import "lib/foo.fin" (becomes 'foo')
    std::string alias = node.alias;
    if (alias.empty()) {
        // Derive alias from filename: "tests/samples/macros.fin" -> "macros"
        std::filesystem::path p(node.source);
        alias = p.stem().string();
    }

    // Create a Namespace Symbol
    auto nsType = std::make_shared<NamespaceType>(alias, moduleScope);
    
    // Define the namespace as a variable in the current scope
    // This allows 'alias.member' to work via MemberAccess
    currentScope->define({alias, nsType, false, true});
    
    debugLog(fg(fmt::color::blue), "      [Import] Module '{}' bound to namespace '{}'\n", node.source, alias);
}

void SemanticAnalyzer::visit(DefineDeclaration& node) {
    debugLog(fg(fmt::color::magenta), "[INFO] Registering extern '{}'\n", node.name);
    auto retType = resolveTypeFromAST(node.return_type.get());
    if (!retType) return;

    std::vector<std::shared_ptr<Type>> paramTypes;
    for (auto& param : node.params) {
        auto t = resolveTypeFromAST(param->type.get());
        if (t) paramTypes.push_back(t);
    }

    auto funcType = std::make_shared<FunctionType>(paramTypes, retType, node.is_vararg);
    currentScope->define({node.name, funcType, false, true});
}

}
