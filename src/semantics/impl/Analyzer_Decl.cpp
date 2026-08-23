#include "../SemanticAnalyzer.hpp"
#include "../../utils/ModuleLoader.hpp"
#include "../../types/TypeImpl.hpp"
#include <fmt/core.h>
#include <fmt/color.h>
#include <filesystem>

namespace fin {

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
                // checkInitializer, not checkType: `let x <int> = null;` is a
                // declaration and is legal (deeptest4.fin:6), while `x = null;`
                // is an assignment and is not.
                checkInitializer(*node.initializer, lastExprType, type);
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

    // 2. Register Generics (e.g. <T>) and resolve their constraints
    declareGenericParams(node.generic_params);

    // 3. Resolve Parameters & Build Signature
    std::vector<std::shared_ptr<Type>> paramTypes;
    bool hasSelf = false;
    // A parameter whose type did not resolve is left out of paramTypes, which
    // silently changes the function's arity. Tracked so that a signature which
    // did not resolve is not registered as though it had; see step 6.
    bool signatureResolved = true;
    
    for (auto& param : node.params) {
        if (param->name == "self") hasSelf = true;
        
        auto type = resolveTypeFromAST(param->type.get());
        if (type) {
            // Define in body scope so the code can use the param
            currentScope->define({param->name, type, false, true});
            // Register in signature
            paramTypes.push_back(type);
        } else {
            signatureResolved = false;
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
    // nullptr here means "the return type is unknown", which every other reader
    // of this field already treats as "stop asking" (Analyzer_Stmt.cpp:15,21).
    context.currentFuncReturnType = retType;
    if (!retType) signatureResolved = false;

    // 6. REGISTER FUNCTION IN PARENT SCOPE (CRITICAL FIX)
    // Register 'add' and 'compute' in the Global Scope (currentScope->parent)
    // so that 'main' can find them later.
    //
    // Only when the signature resolved. FunctionType dereferences its return
    // type and its parameters in toString(), so registering one built over an
    // unresolved part hands a live null to every later caller -- and registering
    // one whose unresolved parameters were quietly dropped advertises the wrong
    // arity, which made `fun f(p: NoSuchType)` called as `f(1)` report a second,
    // false error about expecting 0 arguments. Calls to such a function still
    // cascade, as KnownDefect_TypeResolution records.
    if (currentScope->parent && signatureResolved) {
        auto funcType = std::make_shared<FunctionType>(paramTypes, retType);
        // Mark as immutable and initialized
        currentScope->parent->define({node.name, funcType, false, true});
        debugLog(fg(fmt::color::gray), "      [Register] Registered function '{}' in parent scope\n", node.name);
    }

    // 7. Analyze Body
    if (node.body) node.body->accept(*this);
    
    // `node.return_type` is null for a function that declared none -- the
    // grammar does not currently admit one, but the AST does, and step 5 above
    // already tests it before use. `currentFuncReturnType` is null when the
    // declared return type did not resolve; the cause has been reported, and
    // there is no type here to ask whether it is void.
    //
    // `fun?` is exempt. nullifier.fin:23 -- "Automatically returns null even
    // without an else statement" -- and undefined_behavior.fin:9, whose comment
    // says of a `fun?` with one conditional return "this function compiles". It
    // did not: finc reported the missing-return error on that very line. The
    // sample stayed green because `//@ error` means "at least this diagnostic"
    // (tests/test_expectations.cpp), so the extra one was invisible to the
    // harness -- which is the whole argument for making that form exhaustive.
    if (node.body && node.return_type && context.currentFuncReturnType
        && !node.return_type->is_nullable
        && !node.return_type->name.empty() && node.return_type->name != "void"
        && node.return_type->name != "noret") {
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
    declareGenericParams(node.generic_params, &structType->generic_args);

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
                checkInitializer(*member->default_value, lastExprType, memberType);
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
                checkInitializer(*member->default_value, lastExprType, memberType);
            }
        }
    }

    // 2. Method Bodies (FIXED: Analyze bodies now)
    for (auto& method : node.methods) {
        method->accept(*this);
    }

    // 3. Operator Bodies
    for (auto& op : node.operators) {
        op->accept(*this);
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
    declareGenericParams(node.generic_params);
    
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
    
    if (node.implements_type) {
        auto implType = resolveTypeFromAST(node.implements_type.get());
        if (implType) {
            auto targetStruct = std::dynamic_pointer_cast<StructType>(implType);
            if (targetStruct) {
                int opKey = static_cast<int>(node.op);
                if (targetStruct->operators.count(opKey)) {
                    auto sourceOpType = targetStruct->operators[opKey];
                    if (retType && !sourceOpType->equals(*retType)) {
                        error(node, "Implemented operator return type mismatch");
                    }
                } else {
                    error(node, fmt::format("Type '{}' does not implement operator '{}'", targetStruct->name, (int)node.op));
                }
            }
        }
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
    declareGenericParams(node.generic_params);
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
        declareGenericParams(op->generic_params);
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

    // Case 0: `import * from m;` -- every symbol the module has, not a symbol
    // literally named `*`. The parser records the star as a `*` target
    // (parser.y:740), and looking that up as an identifier is what produced
    // `Module 'm' does not export '*'` followed by an `Undefined ...` for every
    // use of a symbol the import was supposed to bind.
    //
    // Explicit beats wildcard: a name is taken from the module only if this scope
    // does not already have one. That is the rule Python, Java and C# all use, and
    // here it does two separate jobs. The one that matters: without it a library can
    // rename a type out from under the file importing it, because a star import would
    // outrank a declaration the importer wrote itself
    // (Soundness_Imports.ImportStarDoesNotShadowTheImportersOwnDeclaration). The
    // other is hygiene -- a module's scope IS an analyzer's global scope
    // (ModuleLoader.cpp:308), so it carries the fourteen builtin types every analyzer
    // registers (Analyzer_Core.cpp:79-94), and copying it wholesale would replace
    // this file's `int` and `string` with another analyzer's copies of them. Harmless
    // today, because types compare by name and not by identity; not worth relying on.
    //
    // Only the module scope's own maps are read, never `resolve`, which walks
    // parents: the module scope has no parent today and this must not start
    // importing a parent's contents if it ever gains one.
    if (node.targets.size() == 1 && node.targets[0] == "*") {
        for (const auto& kv : moduleScope->symbols)
            if (!currentScope->resolve(kv.first)) currentScope->define(kv.second);
        for (const auto& kv : moduleScope->types)
            if (!currentScope->resolveType(kv.first)) currentScope->defineType(kv.first, kv.second);
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

void SemanticAnalyzer::visit(TypeDefinition& node) {
    debugLog(fg(fmt::color::cyan), "[INFO] Analyzing type definition '{}'\n", node.name);
    
    // 1. If it has generics, we need a way to store a "Generic Type Alias"
    // For now, we support simple aliases or concrete types.
    // Enhanced support requires a TypeAlias type in the type system.
    
    if (!node.generic_params.empty()) {
        debugLog(fg(fmt::color::yellow), "      [Warning] Generic type aliases are partially supported.\n");
        // We can't fully resolve it yet without instantiation.
        // We should define a placeholder or template.
        // For this immediate task, we'll verify the aliased type structure.
        
        enterScope();
        declareGenericParams(node.generic_params);
        if (node.aliased_type) resolveTypeFromAST(node.aliased_type.get());
        exitScope();
        return;
    }

    // 2. Resolve Aliased Type
    std::shared_ptr<Type> type = nullptr;
    if (node.has_implements) {
         // Handle "type Any<...> = any implements <...>"
         // This creates a special "Any" type with constraints.
         // For now, treat as 'any' (if we had it) or 'void*' equivalent, or just 'AnyType'.
         // We'll define a struct type "any" or similar.
         auto anyType = std::make_shared<StructType>("any"); // Placeholder
         type = anyType;
    } else {
        type = resolveTypeFromAST(node.aliased_type.get());
    }

    if (type) {
        currentScope->defineType(node.name, type);
        debugLog(fg(fmt::color::gray), "      [Type] Defined alias '{}' -> '{}'\n", node.name, type->toString());
    }
}

void SemanticAnalyzer::visit(SpecialDeclaration& node) {
    debugLog(fg(fmt::color::magenta), "[INFO] Analyzing special decl '{}'\n", node.name);
    // Similar to function but used for compile-time/macros
    
    enterScope();
    for (auto& param : node.params) {
         auto t = resolveTypeFromAST(param->type.get());
         if (t) currentScope->define({param->name, t, false, true});
    }
    
    if (node.return_type) resolveTypeFromAST(node.return_type.get());
    
    if (node.body) node.body->accept(*this);
    
    exitScope();
}

void SemanticAnalyzer::visit(ClassDeclaration& node) {
    debugLog(fg(fmt::color::orange), "[INFO] Analyzing class '{}'\n", node.name);

    auto structType = std::make_shared<StructType>(node.name);
    // structType->is_class = true; // Placeholder for future
    currentScope->defineType(node.name, structType);

    enterScope();

    // --- SETUP GENERICS ---
    declareGenericParams(node.generic_params, &structType->generic_args);

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
            if (memberType->equals(*structType) && member->type->pointer_depth == 0) {
                error(*member, "Recursive class member '" + member->name + "' must be a pointer");
            }
            structType->defineField(member->name, memberType, member->is_public);
        }
        if (member->default_value) {
            member->default_value->accept(*this);
            if (lastExprType && memberType) {
                checkInitializer(*member->default_value, lastExprType, memberType);
            }
        }
    }

    // 2. Methods
    for (auto& method : node.methods) {
        std::shared_ptr<Type> retType = nullptr;
        if (method->return_type) retType = resolveTypeFromAST(method->return_type.get());
        else retType = currentScope->resolveType("void");
        
        if (retType) structType->defineMethod(method->name, retType);
    }

    // 3. Operators
    for (auto& op : node.operators) {
        std::shared_ptr<Type> retType = nullptr;
        if (op->return_type) retType = resolveTypeFromAST(op->return_type.get());
        else retType = currentScope->resolveType("void");
        
        if (retType) structType->defineOperator((int)op->op, retType);
    }

    // 4. Constructors
    for (auto& ctor : node.constructors) {
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
                checkInitializer(*member->default_value, lastExprType, memberType);
            }
        }
    }

    // 2. Method Bodies
    for (auto& method : node.methods) {
        method->accept(*this);
    }

    // 3. Operator Bodies
    for (auto& op : node.operators) {
        op->accept(*this);
    }

    // 4. Constructor Bodies
    for (auto& ctor : node.constructors) {
        enterScope();
        for (auto& param : ctor->params) {
            auto t = resolveTypeFromAST(param->type.get());
            if (t) currentScope->define({param->name, t, false, true});
        }
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
                if (!structType->implements(p.get())) {
                    error(node, fmt::format("Class '{}' does not implement interface '{}'", node.name, p->name));
                }
            }
        }
    }

    currentStructContext = prevContext;
    exitScope();
}

void SemanticAnalyzer::visit(ImplementsBlock& node) {
    debugLog(fg(fmt::color::cyan), "[INFO] Analyzing implements block: {} implements <{}>\n", 
             node.target_type, node.interface_type ? node.interface_type->name : "?");
    
    auto targetType = currentScope->resolveType(node.target_type);
    if (!targetType) {
        error(node, fmt::format("Unknown type '{}' in implements block", node.target_type));
        return;
    }
    
    auto structType = std::dynamic_pointer_cast<StructType>(targetType);
    if (!structType) {
        error(node, fmt::format("Type '{}' is not a struct/class and cannot implement interfaces", node.target_type));
        return;
    }
    
    std::shared_ptr<Type> interfaceType = nullptr;
    if (node.interface_type) {
        interfaceType = resolveTypeFromAST(node.interface_type.get());
        if (!interfaceType) {
            error(node, fmt::format("Unknown interface '{}'", node.interface_type->name));
            return;
        }
    }
    
    enterScope();
    
    currentScope->defineType("Self", structType);
    auto prevContext = currentStructContext;
    currentStructContext = structType;
    
    for (auto& method : node.methods) {
        // The signature is resolved in a scope of the method's own, because a
        // generic method carries type parameters that exist only for the length of
        // its declaration. Without this, `pub fun m<T>(item: T)` inside an
        // implements block reported `Undefined type 'T'` -- the method's own
        // parameter, undefined in its own signature. The scope is also what keeps
        // one method's generics from leaking into the next.
        // Soundness_GenericConstraints.AMethodInAnImplementsBlockDeclaresItsOwnGenerics.
        enterScope();
        declareGenericParams(method->generic_params);

        std::vector<std::shared_ptr<Type>> paramTypes;
        for (auto& param : method->params) {
            auto t = resolveTypeFromAST(param->type.get());
            if (t) paramTypes.push_back(t);
        }
        
        std::shared_ptr<Type> retType = nullptr;
        if (method->return_type) retType = resolveTypeFromAST(method->return_type.get());
        else retType = currentScope->resolveType("void");
        
        exitScope();

        if (retType) {
            structType->defineMethod(method->name, retType);
        }
        
        // Outside the signature scope: visit(FunctionDeclaration&) opens its own
        // and declares the same parameters there, so declaring them twice in
        // nested scopes would be harmless but says something untrue about which
        // scope owns them.
        method->accept(*this);
    }
    
    for (auto& op : node.operators) {
        enterScope();
        declareGenericParams(op->generic_params);

        std::shared_ptr<Type> retType = nullptr;
        if (op->return_type) retType = resolveTypeFromAST(op->return_type.get());
        else retType = currentScope->resolveType("void");

        exitScope();
        
        if (retType) structType->defineOperator((int)op->op, retType);
        
        op->accept(*this);
    }
    
    if (interfaceType) {
        auto ifaceStruct = std::dynamic_pointer_cast<StructType>(interfaceType);
        if (ifaceStruct && ifaceStruct->is_interface) {
            structType->parents.push_back(ifaceStruct);
            
            if (!structType->implements(ifaceStruct.get())) {
                error(node, fmt::format("'{}' does not fully implement interface '{}'", 
                                       node.target_type, ifaceStruct->name));
            }
        }
    }
    
    currentStructContext = prevContext;
    exitScope();
    
    debugLog(fg(fmt::color::green), "      [OK] Implements block for '{}' analyzed successfully\n", node.target_type);
}

}
