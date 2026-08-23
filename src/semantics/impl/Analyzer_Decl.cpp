#include "../SemanticAnalyzer.hpp"
#include "../../utils/ModuleLoader.hpp"
#include "../../types/TypeImpl.hpp"
#include <fmt/core.h>
#include <fmt/color.h>
#include <filesystem>

namespace fin {

void SemanticAnalyzer::visit(VariableDeclaration& node) {
    auto type = resolveTypeFromAST(node.type.get());

    // Not an early return. It was one, and that made a single unresolved annotation
    // silence its whole initialiser: `let x <NoSuchType> = nosuchvar;` reported the type
    // and never mentioned `nosuchvar`. Two real errors in prototype_test.fin were hidden
    // by exactly this. Carrying the sentinel instead keeps the initialiser analysed and
    // defines the variable, so uses of it do not each report again.
    //
    // It must not be `auto`: the inference branch below would then adopt the
    // initialiser's type and the program would compile clean.
    if (!type) type = errorType();

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

std::shared_ptr<FunctionType> SemanticAnalyzer::buildMethodSignature(FunctionDeclaration& method) {
    // A scope of its own, holding the method's generics: `fun set_x<U>(new_x: U)`
    // (struct_methods.fin:14) declares U itself, and the interface loop below used to
    // resolve its parameters without one -- which is why `pub fun m<T>(a: T) <T>;`
    // reported "Undefined type 'T'" twice. The parameters are defined in it too, so a
    // default may name a sibling, as it may in a free function.
    enterScope();
    declareGenericParams(method.generic_params);

    std::vector<std::shared_ptr<Type>> paramTypes;
    for (auto& param : method.params) {
        auto type = resolveTypeOrError(param->type.get());
        currentScope->define({param->name, type, false, true});
        // The receiver is not a parameter of the call. struct_methods.fin writes both
        // spellings in one struct -- `fun print_point(self: &Self)` at :10 and
        // `fun set_x<U>(new_x: U)` at :14 -- and :10 says the compiler injects it
        // either way, so a signature that kept a written `self` would make the two
        // spellings disagree about arity.
        if (param->name == "self") continue;
        // The sentinel goes in, exactly as in visit(FunctionDeclaration&): dropping an
        // unresolved parameter would make `pub fun m(a: NoSuchType)` called `s.m(1)`
        // report "expects 0 arguments, got 1" on top of the one real diagnostic.
        paramTypes.push_back(type);
    }
    // Walked here rather than at the call sites because this is the only scope that has
    // the method's generics and parameters in it. At the struct and class sites this
    // pass is quiet and visit(FunctionDeclaration&) walks them again, reporting; at the
    // interface, where there is no body pass, this walk is the one that reports.
    visitParameterDefaults(method.params);

    std::shared_ptr<Type> retType;
    if (method.return_type) retType = resolveTypeOrError(method.return_type.get());
    else retType = currentScope->resolveType("void");

    exitScope();

    // Null only when `void` itself failed to resolve, which means the primitive table
    // is broken; the callers gate on it as they always did.
    if (!retType) return nullptr;
    return std::make_shared<FunctionType>(paramTypes, retType);
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
    
    for (auto& param : node.params) {
        if (param->name == "self") hasSelf = true;
        
        auto type = resolveTypeOrError(param->type.get());
        // Define in body scope so the code can use the param
        currentScope->define({param->name, type, false, true});
        // Register in signature. The sentinel goes in too: dropping an unresolved
        // parameter is what made `fun f(p: NoSuchType)` called as `f(1)` report
        // "expects 0 arguments, got 1" -- a claim about a signature nobody wrote.
        paramTypes.push_back(type);
    }
    visitParameterDefaults(node.params);

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
    // Deliberately still null rather than the sentinel: those readers are the
    // missing-return check, and telling it the return type is `<error>` would
    // make it demand a return statement the program cannot satisfy. Retiring
    // that convention is its own step.
    context.currentFuncReturnType = retType;

    // 6. REGISTER FUNCTION IN PARENT SCOPE (CRITICAL FIX)
    // Register 'add' and 'compute' in the Global Scope (currentScope->parent)
    // so that 'main' can find them later.
    //
    // Registered even when part of the signature did not resolve. This used to be
    // gated on the whole signature resolving, for two reasons that the error
    // sentinel answers: FunctionType dereferences its return type and parameters
    // in toString(), which a live null made a crash, and dropping the unresolved
    // parameters advertised the wrong arity. The sentinel is neither null nor
    // absent, so the signature keeps the shape the program wrote and the call is
    // checked against it. Not registering was no kinder than the wrong arity: it
    // reported "Undefined function or type 'f'" about a function that is defined.
    if (currentScope->parent) {
        auto funcType = std::make_shared<FunctionType>(
            paramTypes, retType ? retType : errorType());
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
        auto memberType = resolveTypeOrError(member->type.get());
        if (memberType->equals(*structType) && member->type->pointer_depth == 0) {
            error(*member, "Recursive struct member '" + member->name + "' must be a pointer");
        }
        // Defined even when the type did not resolve, so `s.field` says nothing
        // further: the annotation is the diagnostic, not every use of the field.
        structType->defineField(member->name, memberType, member->is_public);
        // The default is NOT walked here. PASS 2 below walks it again, with
        // currentStructContext set and the field type read back from the struct,
        // and both walks reported -- `pub v <int> = nosuchvar` said
        // "Undefined variable 'nosuchvar'" twice. PASS 2's is the well-formed
        // one, so this pass registers the field and nothing more.
    }

    // 2-4. Signatures: methods, operators, constructors.
    //
    // Quiet, and this is the block QuietPass exists for. Every TypeNode resolved below
    // is resolved again by PASS 2 -- method->accept, op->accept and the constructor
    // parameter loop are all a few lines down -- and that pass reports. Resolving twice
    // and reporting twice is what made `S(p: NoSuchType)` say "Undefined type" twice,
    // booked for a long time as KnownDefect_ErrorRecovery.AnAnnotationInAStructSignature-
    // IsReportedOncePerPass and fixable only once one pass could be made to own the
    // diagnostic. PASS 2 owns it, because it is the pass with a scope to populate.
    {
        QuietPass quiet(*this);

        for (auto& method : node.methods) {
            // The whole signature, not just the return type: a method call is checked
            // for arity and argument types (Soundness_MethodCalls), and it can only be
            // checked against something the type records. The guard is still only about
            // `void` failing to resolve. An unresolved *written* return type arrives as
            // the sentinel and the method is still declared, so calling it reports
            // nothing beyond the annotation.
            //
            // Do not call accept here -- that triggers body analysis too early.
            if (auto sig = buildMethodSignature(*method))
                structType->defineMethod(method->name, sig);
        }

        for (auto& op : node.operators) {
            // A scope with the operator's own generics in it, which this loop did not
            // have: `operator + : <T>(other: <T>) <T>` reported "Undefined type 'T'"
            // about a parameter it declares one token earlier, and registered the
            // operator returning the sentinel. Booked as KnownDefect_Generics.AnOperators-
            // OwnGenericParameterIsNotInScopeInPassOne, whose text asked for exactly
            // these three lines once something had collapsed the method copy of the same
            // hole -- buildMethodSignature above is that something.
            //
            // The QuietPass around this whole block had already removed the *diagnostic*
            // half of that defect, which is why the fix cannot be verified by compiling
            // the program and counting: silence and a correct scope produce the same zero.
            // What is left to verify is the registration, and the only way to see it is to
            // call the operator and read the type of the call --
            // Soundness_Generics.AnOperatorsRegisteredReturnTypeIsWhatItsCallIsTyped.
            // Mutant D-opgen reverts these three lines and that test is what kills it.
            enterScope();
            declareGenericParams(op->generic_params);

            std::shared_ptr<Type> retType = nullptr;
            if (op->return_type) retType = resolveTypeOrError(op->return_type.get());
            else retType = currentScope->resolveType("void");

            exitScope();

            // Ungated for the same reason as the methods above, and it mattered more
            // here: with the operator undeclared, `s + 1` fell through to the built-in
            // rule and reported `Type mismatch: expected 'S', got 'int'` -- a claim
            // about an operator the program did declare.
            //
            // A return type and nothing else, unlike the methods: an operator call has
            // no arity check yet, so there is no consumer for its parameters and
            // building a signature here would be dead. Booked, not done.
            structType->defineOperator((int)op->op, retType);
        }

        for (auto& ctor : node.constructors) {
            // Resolve params in a temp scope to get signature
            enterScope();
            std::vector<std::shared_ptr<Type>> paramTypes;
            for (auto& param : ctor->params) {
                // Sentinel, not dropped: the constructor keeps its written arity.
                paramTypes.push_back(resolveTypeOrError(param->type.get()));
            }
            exitScope();

            auto ctorType = std::make_shared<FunctionType>(paramTypes, structType);
            structType->addConstructor(ctorType);
            debugLog(fg(fmt::color::green), "      [Ctor] Registered constructor for '{}' with {} params\n", node.name, paramTypes.size());
        }
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
            currentScope->define({param->name, resolveTypeOrError(param->type.get()), false, true});
        }
        visitParameterDefaults(ctor->params);
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
        currentScope->define({param->name, resolveTypeOrError(param->type.get()), false, true});
    }
    visitParameterDefaults(node.params);
    
    // 3. Inject Self
    currentScope->define({"self", structType, true, true});

    // 4. Return Type
    std::shared_ptr<Type> retType = nullptr;
    if (node.return_type) {
        retType = resolveTypeOrError(node.return_type.get());
    } else {
        retType = currentScope->resolveType("void");
    }
    
    // 5. NOT registered here. This used to be a second defineOperator, needed because
    // the signature pass resolved the return type without the operator's own generic
    // parameters in scope and so registered the sentinel for `operator + : <T>(...) <T>`.
    // All four containers now declare those generics before they resolve
    // (visit(StructDeclaration&), visit(ClassDeclaration&), visit(InterfaceDeclaration&)
    // and visit(ImplementsBlock&)), and every path that reaches this function comes
    // through one of them, so the write here could only ever repeat what is already
    // stored. Deleting it is what makes dropping the signature pass's registration
    // observable at all -- with two writers, the mutation matrix measured zero kills for
    // dropping either one alone.
    //
    // retType is still resolved above: the implements-check below compares against it,
    // and the body walk needs the scope this function opened.
    
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
        // The one signature pass that is NOT quiet, and the reason QuietPass is a
        // guard and not a policy: an interface method has no body, so nothing resolves
        // these TypeNodes a second time and this is the only chance to report. The
        // asymmetry is stated in Soundness_ErrorRecovery.AMethodParameterAnnotationIs-
        // StillReportedOnce, which counts both shapes.
        //
        // The parameters used to be resolved here and thrown away -- and resolved
        // without the method's own generic scope, so `pub fun m<T>(a: T) <T>;` reported
        // "Undefined type 'T'" twice. buildMethodSignature keeps them, and declares T.
        if (auto sig = buildMethodSignature(*method)) ifaceType->defineMethod(method->name, sig);
    }
    
    for (auto& op : node.operators) {
        enterScope();
        declareGenericParams(op->generic_params);
        for (auto& param : op->params) resolveTypeFromAST(param->type.get());
        visitParameterDefaults(op->params);
        std::shared_ptr<Type> retType = nullptr;
        if(op->return_type) retType = resolveTypeOrError(op->return_type.get());
        else retType = currentScope->resolveType("void");
        
        // Unguarded, like defineMethod above it, so this stored a live null. Every
        // reader dereferences it: StructType.cpp:65 clones it, :86 substitutes it.
        ifaceType->defineOperator((int)op->op, retType);
        exitScope();
    }

    for (auto& ctor : node.constructors) {
        enterScope();
        std::vector<std::shared_ptr<Type>> paramTypes;
        for (auto& param : ctor->params) {
            paramTypes.push_back(resolveTypeOrError(param->type.get()));
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
    // Was `if (!retType) return;`, which abandoned the whole declaration: the
    // extern went unregistered so every call to it reported an undefined name,
    // and -- because the bare return also skipped the walk below -- its parameter
    // defaults went unanalysed as well.
    auto retType = resolveTypeOrError(node.return_type.get());

    std::vector<std::shared_ptr<Type>> paramTypes;
    for (auto& param : node.params) {
        paramTypes.push_back(resolveTypeOrError(param->type.get()));
    }
    visitParameterDefaults(node.params);

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
    // `type EnumType = any implements <Enum>;` (stdlib/enums.fin:6),
    // `type nullptr = any implements <&void>;` (stdlib/types.fin:78).
    //
    // What stood here fabricated `std::make_shared<StructType>("any")` and threw both
    // the aliased type and the bound away. It was the worst answer available, because
    // it was an authoritative one: the fiction rendered as `any`, so
    // `let x <EnumType> = 5;` reported `Type mismatch: expected 'any', got 'int'` -- a
    // claim about a type the program never wrote as a concrete type -- and `v.nosuch`
    // reported `Struct 'any' has no member 'nosuch'` about a struct that does not
    // exist. An unresolved name at least sends the reader to the right line.
    //
    // The target is now resolved like any other alias target, which is what makes
    // `any` reaching here mean the builtin (Analyzer_Core.cpp) rather than a name this
    // branch invents.
    type = resolveTypeOrError(node.aliased_type.get());

    if (node.has_implements) {
        // The bounds are resolved whether or not anything reads them yet, because
        // resolution is what reports a typo in one. They were never looked at before,
        // so `type E = any implements <NoSuchInterface>;` was silently accepted -- and
        // an unenforced bound that also fails to reject a misspelling is not a partial
        // implementation, it is a blind spot.
        std::vector<TypePtr> bounds;
        for (auto& bnd : node.implements_list) bounds.push_back(resolveTypeOrError(bnd.get()));

        // Attached only to a dynamic target, which is the only shape the corpus writes
        // a bound on. On anything else the bound has nowhere to live yet and the alias
        // is still worth defining -- see
        // KnownDefect_DynamicTypes.AnImplementsBoundOnANonDynamicAliasIsDropped.
        if (auto* dyn = type ? type->as<DynamicType>() : nullptr)
            type = std::make_shared<DynamicType>(dyn->name, std::move(bounds));
    }

    // The twenty-first site of the same rule, and the one that cost the corpus most:
    // an alias whose target did not resolve left its *name* undefined, so
    // `stdlib/operators.fin`'s `type Output = Any<...>;` on line 6 was charged once for
    // the annotation and then once more for each of the thirty operator requirements
    // that name `Output` -- thirty diagnostics about a declaration that is written
    // right there, none of them about the one that failed.
    //
    // An alias is a declaration like any other: the name was written, so the name
    // exists. What it names is the sentinel, and that is what ends the cascade.
    currentScope->defineType(node.name, type);
    debugLog(fg(fmt::color::gray), "      [Type] Defined alias '{}' -> '{}'\n", node.name, type->toString());
}

void SemanticAnalyzer::visit(SpecialDeclaration& node) {
    debugLog(fg(fmt::color::magenta), "[INFO] Analyzing special decl '{}'\n", node.name);
    // Similar to function but used for compile-time/macros
    
    enterScope();
    for (auto& param : node.params) {
         currentScope->define({param->name, resolveTypeOrError(param->type.get()), false, true});
    }
    visitParameterDefaults(node.params);
    
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
        auto memberType = resolveTypeOrError(member->type.get());
        if (memberType->equals(*structType) && member->type->pointer_depth == 0) {
            error(*member, "Recursive class member '" + member->name + "' must be a pointer");
        }
        // Defined even when the type did not resolve, so `s.field` says nothing
        // further: the annotation is the diagnostic, not every use of the field.
        structType->defineField(member->name, memberType, member->is_public);
        // The default is NOT walked here. PASS 2 below walks it again, with
        // currentStructContext set and the field type read back from the struct,
        // and both walks reported -- `pub v <int> = nosuchvar` said
        // "Undefined variable 'nosuchvar'" twice. PASS 2's is the well-formed
        // one, so this pass registers the field and nothing more.
    }

    // 2-4. Signatures: methods, operators, constructors. Quiet, for the reason spelled
    // out at the same block in visit(StructDeclaration) -- PASS 2 below resolves every
    // one of these TypeNodes again and reports.
    {
        QuietPass quiet(*this);

        for (auto& method : node.methods) {
            if (auto sig = buildMethodSignature(*method))
                structType->defineMethod(method->name, sig);
        }

        for (auto& op : node.operators) {
            // The operator's own generics, as in visit(StructDeclaration) above.
            enterScope();
            declareGenericParams(op->generic_params);

            std::shared_ptr<Type> retType = nullptr;
            if (op->return_type) retType = resolveTypeOrError(op->return_type.get());
            else retType = currentScope->resolveType("void");

            exitScope();

            // Ungated for the same reason as the methods above, and it mattered more
            // here: with the operator undeclared, `s + 1` fell through to the built-in
            // rule and reported `Type mismatch: expected 'S', got 'int'` -- a claim
            // about an operator the program did declare.
            structType->defineOperator((int)op->op, retType);
        }

        for (auto& ctor : node.constructors) {
            enterScope();
            std::vector<std::shared_ptr<Type>> paramTypes;
            for (auto& param : ctor->params) {
                // Sentinel, not dropped: the constructor keeps its written arity.
                paramTypes.push_back(resolveTypeOrError(param->type.get()));
            }
            exitScope();

            auto ctorType = std::make_shared<FunctionType>(paramTypes, structType);
            structType->addConstructor(ctorType);
            debugLog(fg(fmt::color::green), "      [Ctor] Registered constructor for '{}' with {} params\n", node.name, paramTypes.size());
        }
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
            currentScope->define({param->name, resolveTypeOrError(param->type.get()), false, true});
        }
        visitParameterDefaults(ctor->params);
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
        // The parameters ARE resolved here now: StructType::methods holds a whole
        // FunctionType and a method call is checked against it (Soundness_MethodCalls),
        // so the signature this loop builds has a reader. It is quiet, which is what
        // keeps Soundness_ErrorRecovery.AnImplementsBlockMethodParameterIsReportedOnce
        // at one -- method->accept below resolves the same TypeNodes and reports.
        //
        // buildMethodSignature opens the signature scope and declares the method's own
        // generics, which is what this loop did by hand.
        std::shared_ptr<FunctionType> sig;
        {
            QuietPass quiet(*this);
            sig = buildMethodSignature(*method);
        }
        if (sig) structType->defineMethod(method->name, sig);

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
        if (op->return_type) retType = resolveTypeOrError(op->return_type.get());
        else retType = currentScope->resolveType("void");

        exitScope();
        
        structType->defineOperator((int)op->op, retType);
        
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
