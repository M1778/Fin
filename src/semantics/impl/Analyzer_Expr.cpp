#include "../SemanticAnalyzer.hpp"
#include "../../types/TypeImpl.hpp"
#include <fmt/core.h>
#include <fmt/color.h>

namespace fin {

// Signature now accepts std::shared_ptr<Scope>
std::shared_ptr<StructType> getStructType(std::shared_ptr<Type> type, std::shared_ptr<Scope> scope) {
    if (!type) return nullptr;

    // 1. Unwrap Pointers (recursive)
    if (auto* ptr = dynamic_cast<const PointerType*>(type.get())) {
        return getStructType(ptr->pointee, scope);
    }

    // 2. Unwrap SelfType
    if (auto* self = dynamic_cast<const SelfType*>(type.get())) {
        return getStructType(self->originalStruct, scope);
    }

    // 3. Check if it's a StructType
    if (auto* st = dynamic_cast<StructType*>(type.get())) {
        return std::static_pointer_cast<StructType>(type);
    }

    // 4. Check GenericType (Constraint)
    if (auto* gt = dynamic_cast<GenericType*>(type.get())) {
        // If T : Interface, treat T as Interface
        if (gt->constraint) {
            return getStructType(gt->constraint, scope);
        }
    }

    return nullptr;
}

void SemanticAnalyzer::visit(PrototypeLiteral& node) {
    std::shared_ptr<Type> keyType = nullptr;
    std::shared_ptr<Type> valueType = nullptr;

    for (auto& pair : node.elements) {
        pair.first->accept(*this);
        auto kType = lastExprType;
        if (!keyType) keyType = kType;
        else if (kType && !kType->equals(*keyType)) {
            keyType = currentScope->resolveType("any");
        }

        pair.second->accept(*this);
        auto vType = lastExprType;
        if (!valueType) valueType = vType;
        else if (vType && !vType->equals(*valueType)) {
            valueType = currentScope->resolveType("any");
        }
    }

    if (!keyType) keyType = currentScope->resolveType("any");
    if (!valueType) valueType = currentScope->resolveType("any");
    
    if (!keyType) keyType = std::make_shared<PrimitiveType>("any");
    if (!valueType) valueType = std::make_shared<PrimitiveType>("any");

    lastExprType = std::make_shared<PrototypeType>(keyType, valueType);
}

void SemanticAnalyzer::visit(Literal& node) {
    switch(node.kind) {
        case ASTTokenKind::INTEGER: lastExprType = currentScope->resolveType("int"); break;
        case ASTTokenKind::FLOAT:   lastExprType = currentScope->resolveType("float"); break;
        case ASTTokenKind::STRING_LITERAL:  lastExprType = currentScope->resolveType("string"); break;
        case ASTTokenKind::BOOL:    lastExprType = currentScope->resolveType("bool"); break;
        // Was `&void`, which bought pointer assignability for free and cost
        // every diagnostic about it: `let x <int> = null` reported "got '&void'",
        // naming a pointer type in a program with no pointer in it. NullType is
        // assignable to every `T?` and every `&T` and to nothing else -- see
        // src/types/NullableType.hpp.
        case ASTTokenKind::KW_NULL: lastExprType = std::make_shared<NullType>(); break;
        default: lastExprType = nullptr;
    }
}

void SemanticAnalyzer::visit(Identifier& node) {
    // 1. Try local scope
    Symbol* sym = currentScope->resolve(node.name);
    if (sym) {
        lastExprType = sym->type;
        return;
    } 
    
    // 2. Try Implicit Field Access (self.name)
    if (currentStructContext) {
        // Use the helper to handle Self/Pointers
        auto st = std::dynamic_pointer_cast<StructType>(currentStructContext);
        if (!st) st = getStructType(currentStructContext, currentScope);

        if (st) {
            auto fieldType = st->getFieldType(node.name);
            if (fieldType) {
                lastExprType = fieldType;
                return;
            }
        }
    }
    
    error(node, "Undefined variable '" + node.name + "'");
    lastExprType = nullptr;
}

void SemanticAnalyzer::visit(BinaryOp& node) {
    node.left->accept(*this);
    auto leftType = lastExprType;
    
    node.right->accept(*this);
    auto rightType = lastExprType;
    
    if (!leftType || !rightType) {
        lastExprType = nullptr;
        return;
    }

    // Assignments
    bool isAssignment = (
        node.op == ASTTokenKind::EQUAL || 
        node.op == ASTTokenKind::PLUSEQUAL || 
        node.op == ASTTokenKind::MINUSEQUAL || 
        node.op == ASTTokenKind::MULTEQUAL || 
        node.op == ASTTokenKind::DIVEQUAL
    );

    if (isAssignment) {
        bool isLValue = false;
        if (dynamic_cast<Identifier*>(node.left.get())) isLValue = true;
        else if (dynamic_cast<MemberAccess*>(node.left.get())) isLValue = true;
        else if (dynamic_cast<ArrayAccess*>(node.left.get())) isLValue = true;
        else if (auto* unary = dynamic_cast<UnaryOp*>(node.left.get())) {
            if (unary->op == ASTTokenKind::MULT) isLValue = true;
        }
        
        if (!isLValue) error(node, "Invalid assignment target");
        
        if (auto* id = dynamic_cast<Identifier*>(node.left.get())) {
            auto* sym = currentScope->resolve(id->name);
            if (sym && !sym->is_mutable) {
                error(node, fmt::format("Cannot assign to immutable variable '{}'", id->name));
            }
        }

        checkType(*node.right, rightType, leftType);
        lastExprType = leftType;
        return;
    }

    // Operator Overloading
    if (auto structType = getStructType(leftType, currentScope)) {
        int opKey = static_cast<int>(node.op);
        if (structType->operators.count(opKey)) {
            lastExprType = structType->operators[opKey];
            return;
        }
    }

    // Standard Primitives
    if (node.op == ASTTokenKind::AND || node.op == ASTTokenKind::OR) {
        auto boolType = currentScope->resolveType("bool");
        checkType(*node.left, leftType, boolType);
        checkType(*node.right, rightType, boolType);
        lastExprType = boolType;
        return;
    }

    if (node.op == ASTTokenKind::EQEQ || node.op == ASTTokenKind::NOTEQ ||
        node.op == ASTTokenKind::LT || node.op == ASTTokenKind::GT ||
        node.op == ASTTokenKind::LTEQ || node.op == ASTTokenKind::GTEQ) {
        // `0 == a` and `a == 0` are the same question, so a constant is looked for
        // on both sides. Without this the left operand is the expectation and a
        // constant on the left makes the *variable* the error: `blame 0 == a` for a
        // `uint` a reported "expected 'int', got 'uint'".
        //
        // Only constants are treated symmetrically. Whether two differently-typed
        // variables may be compared at all is a separate question, so when neither
        // side is a constant that fits, the check runs exactly as it did before and
        // reports at the same operand with the same message.
        // `x == null` is legal whatever x is, and so is `null == x`.
        // nullifier.fin:40 compares a *denullified* `_` -- a plain `int` by then --
        // with null, and the sample's own gloss (`assert @unpacked(_) == null`)
        // makes that the intended reading rather than a slip. stdlib/error.fin:12
        // does the same with a plain `int` parameter. Assignability is the wrong
        // question to ask about the one literal every type can be compared to.
        //
        // Only for `==` and `!=`. `x < null` falls through and is still reported,
        // because no sample orders anything against null and inventing an order
        // would be a ruling.
        const bool nullComparison = (node.op == ASTTokenKind::EQEQ || node.op == ASTTokenKind::NOTEQ)
                                    && (isNullLiteral(leftType) || isNullLiteral(rightType));

        if (!nullComparison &&
            !constantFitsType(*node.right, *leftType) &&
            !constantFitsType(*node.left, *rightType)) {
            checkType(*node.right, rightType, leftType);
        }
        lastExprType = currentScope->resolveType("bool");
        return;
    }

    if (!checkType(node, rightType, leftType)) {
        lastExprType = nullptr;
    } else {
        lastExprType = leftType;
    }
}

void SemanticAnalyzer::visit(UnaryOp& node) {
    node.operand->accept(*this);
    auto type = lastExprType;
    if (!type) return;

    if (node.op == ASTTokenKind::AMPERSAND) {
        lastExprType = std::make_shared<PointerType>(type);
    } 
    else if (node.op == ASTTokenKind::QUESTION) {
        // Postfix denullify -- nullifier.fin:31 `make_A(-1)?`, :42
        // `make_A(10)?.get_b()?`, undefined_behavior.fin:16 `add2()?`. It strips
        // exactly one level: `parser.y` gives it its own DENULLIFY precedence, and
        // the grammar admits no `T??` spelling for it to have to unwrap twice.
        //
        // The panic when the value *is* null is wave 4's -- there is no code
        // generator yet -- and it does not change the type either way, which is
        // why the whole runtime half of this operator is absent here.
        if (auto* nullable = type->as<NullableType>()) {
            lastExprType = nullable->inner;
        } else {
            // A `?` on something that was not nullable. nullifier.fin:36 says the
            // `any` case "should be an error", but `any` is not a resolved type at
            // all yet (docs/plan.md, Rulings owed), and no sample denullifies an
            // ordinary value. The identity, rather than a guess: a wrong error
            // here would be worse than a missing one, because it would reject
            // programs the corpus has not ruled on.
            lastExprType = type;
        }
    }
    else if (node.op == ASTTokenKind::MULT) {
        if (auto* ptr = dynamic_cast<const PointerType*>(type.get())) {
            lastExprType = ptr->pointee;
        } 
        else if (auto* ptrToArray = dynamic_cast<const PointerType*>(type.get())) {
            if (auto* arr = dynamic_cast<const ArrayType*>(ptrToArray->pointee.get())) {
                lastExprType = arr->clone(); 
                return;
            }
        }
        else {
            error(node, fmt::format("Cannot dereference non-pointer type '{}'", type->toString()));
            lastExprType = nullptr;
        }
    }
    else {
        lastExprType = type;
    }
}

void SemanticAnalyzer::visit(FunctionCall& node) {
    std::shared_ptr<FunctionType> funcType = nullptr;
    std::string funcName = node.name;

    // Case 1: Self(...)
    if (funcName == "Self") {
      if (!currentStructContext) {
            error(node, "'Self' used outside of struct");
            lastExprType = nullptr;
            return;
        }
        auto st = std::dynamic_pointer_cast<StructType>(currentStructContext);
        if (st && !st->constructors.empty()) {
            funcType = std::dynamic_pointer_cast<FunctionType>(st->constructors[0]);
        } else {
            error(node, "Struct '" + st->name + "' has no constructors");
            lastExprType = nullptr;
            return;
        }
    }
    // Case 2: Standard Function
    else {
        auto type = currentScope->resolveType(funcName);
        if (type) {
            if (auto st = getStructType(type, currentScope)) {
                if (!st->constructors.empty()) {
                    funcType = std::dynamic_pointer_cast<FunctionType>(st->constructors[0]);
                } else {
                     std::vector<std::shared_ptr<Type>> dummyParams;
                     funcType = std::make_shared<FunctionType>(dummyParams, st);
                }
            }
        } 
        else {
            Symbol* sym = currentScope->resolve(funcName);
            if (sym) {
                funcType = std::dynamic_pointer_cast<FunctionType>(sym->type);
            }
        }
    }

    if (!funcType) {
        error(node, "Undefined function or type '" + funcName + "'");
        lastExprType = nullptr;
        return;
    }

    size_t expected = funcType->param_types.size();
    size_t actual = node.args.size();

    // A nullable parameter is optional at the call site. nullifier.fin:39 calls
    // `make_A()` with no arguments and says why: "since make_A says \"n?: int\" we
    // know that n can be null and we don't need to pass any arguments".
    //
    // The minimum is one past the *last* required parameter rather than the count
    // of required ones, because arguments bind positionally: `(a?: int, b: int)`
    // still needs both written, `(a: int, b?: int)` needs one. That falls out of
    // positional binding and needs no rule about which order the two kinds may
    // appear in -- which matters, because the corpus only ever writes the trailing
    // form and a rule invented here would be unratified.
    size_t required = 0;
    for (size_t i = 0; i < expected; ++i) {
        if (!funcType->param_types[i]->as<NullableType>()) required = i + 1;
    }

    if (!funcType->is_vararg && (actual < required || actual > expected)) {
        if (required == expected) {
            error(node, fmt::format("Function '{}' expects {} arguments, got {}", funcName, expected, actual));
        } else {
            // "expects 2 arguments, got 0" is a lie about a function one of whose
            // parameters is optional, so the range is spelled out.
            error(node, fmt::format("Function '{}' expects between {} and {} arguments, got {}",
                                    funcName, required, expected, actual));
        }
    }

    for (size_t i = 0; i < actual; ++i) {
        node.args[i]->accept(*this);
        if (i < expected) {
            checkType(*node.args[i], lastExprType, funcType->param_types[i]);
        }
    }

    lastExprType = funcType->return_type;
}

void SemanticAnalyzer::visit(MethodCall& node) {
    node.object->accept(*this);
    auto objType = lastExprType;
    
    if (!objType) return; 

    auto structType = getStructType(objType, currentScope);

    if (!structType) {
        error(node, fmt::format("Type '{}' does not have methods", objType->toString()));
        lastExprType = nullptr;
        return;
    }

    auto retType = structType->getMethodReturnType(node.method_name);
    if (!retType) {
        error(node, fmt::format("Method '{}' not found in type '{}'", node.method_name, structType->name));
        lastExprType = nullptr;
        return;
    }

    lastExprType = retType;

    for(auto& arg : node.args) arg->accept(*this);
}

void SemanticAnalyzer::visit(ArrayAccess& node) {
    node.array->accept(*this);
    auto arrExprType = lastExprType;
    
    node.index->accept(*this);
    auto idxType = lastExprType;
    
    if (!arrExprType || !idxType) {
        lastExprType = nullptr;
        return;
    }

    auto intType = currentScope->resolveType("int");
    checkType(*node.index, idxType, intType);

    if (auto* ptrToArray = dynamic_cast<const PointerType*>(arrExprType.get())) {
        if (auto* arr = dynamic_cast<const ArrayType*>(ptrToArray->pointee.get())) {
            arrExprType = ptrToArray->pointee; 
        } else {
            lastExprType = ptrToArray->pointee;
            return;
        }
    }

    if (auto* arrType = dynamic_cast<const ArrayType*>(arrExprType.get())) {
        lastExprType = arrType->element_type;
    } else {
        error(node, fmt::format("Type '{}' is not an array or pointer", arrExprType->toString()));
        lastExprType = nullptr;
    }
}

void SemanticAnalyzer::visit(MacroCall& node) {
    // Macro calls should be expanded before semantic analysis.
    // If we reach here, it's either unexpanded or inside a quote.
    for (auto& arg : node.args) {
        arg->accept(*this);
    }
    lastExprType = nullptr; // Or a placeholder type if we want to support unexpanded macros
}

void SemanticAnalyzer::visit(MacroInvocation& node) {
    // Similar to MacroCall
    for (auto& arg : node.args) {
        arg->accept(*this);
    }
    lastExprType = nullptr;
}

void SemanticAnalyzer::visit(CastExpression& node) {
    node.expr->accept(*this);
    auto sourceType = lastExprType;
    auto targetType = resolveTypeFromAST(node.target_type.get());
    
    if (!sourceType || !targetType) {
        lastExprType = nullptr;
        return;
    }

    bool valid = false;
    if (sourceType->equals(*targetType)) valid = true;
    else if (dynamic_cast<const PrimitiveType*>(sourceType.get()) && 
             dynamic_cast<const PrimitiveType*>(targetType.get())) valid = true;
    else if (dynamic_cast<const PointerType*>(sourceType.get()) && 
             dynamic_cast<const PointerType*>(targetType.get())) valid = true;
    else if (dynamic_cast<const GenericType*>(sourceType.get()) || 
             dynamic_cast<const GenericType*>(targetType.get())) valid = true;
    
    if (!valid) {
        error(node, fmt::format("Invalid cast from '{}' to '{}'", sourceType->toString(), targetType->toString()));
        lastExprType = nullptr;
    } else {
        lastExprType = targetType;
    }
}

void SemanticAnalyzer::visit(NewExpression& node) {
    for(auto& arg : node.args) arg->accept(*this);
    for(auto& f : node.init_fields) f.second->accept(*this);
    
    auto allocatedType = resolveTypeFromAST(node.type.get());
    // A PointerType over a null pointee is worse than no type at all: it is
    // non-null, so every `if (!type) return;` downstream lets it through, and the
    // first toString() -- checkType's own error message, usually -- dereferences
    // the null. `new Nope{}` died in PointerType.cpp:6 that way. resolveTypeFromAST
    // has already reported why it failed, so there is nothing to add here.
    lastExprType = allocatedType ? std::make_shared<PointerType>(allocatedType) : nullptr;
}

void SemanticAnalyzer::visit(MemberAccess& node) {
    node.object->accept(*this);
    auto objType = lastExprType;
    if (!objType) return;

    if (auto* ns = dynamic_cast<const NamespaceType*>(objType.get())) {
        Symbol* sym = ns->scope->resolve(node.member);
        if (sym) {
            lastExprType = sym->type;
            return;
        }
        error(node, fmt::format("Namespace '{}' has no exported member '{}'", ns->name, node.member));
        lastExprType = nullptr;
        return;
    }

    auto structType = getStructType(objType, currentScope);

    if (!structType) {
        error(node, fmt::format("Type '{}' is not a struct", objType->toString()));
        lastExprType = nullptr;
        return;
    }

    auto fieldType = structType->getFieldType(node.member);
    if (!fieldType) {
        error(node, fmt::format("Struct '{}' has no member '{}'", structType->name, node.member));
        lastExprType = nullptr;
    } else {
        bool isPublic = structType->isFieldPublic(node.member);
        bool isInternal = false;
        if (currentStructContext && currentStructContext->equals(*structType)) isInternal = true;
        
        if (!isPublic && !isInternal) {
            error(node, fmt::format("Cannot access private field '{}' of struct '{}'", node.member, structType->name));
        }

        lastExprType = fieldType;
    }
}

void SemanticAnalyzer::visit(StructInstantiation& node) {
    auto baseType = currentScope->resolveType(node.struct_name);
    if (!baseType) {
        error(node, "Undefined struct '" + node.struct_name + "'");
        lastExprType = nullptr;
        return;
    }
    
    auto structDef = std::dynamic_pointer_cast<StructType>(baseType);
    if (!structDef) {
        error(node, "'" + node.struct_name + "' is not a struct");
        lastExprType = nullptr;
        return;
    }

    std::shared_ptr<StructType> concreteType = structDef;

    if (!node.generic_args.empty()) {
        std::vector<std::shared_ptr<Type>> args;
        for (auto& arg : node.generic_args) {
            auto t = resolveTypeFromAST(arg.get());
            if (t) args.push_back(t);
        }
        
        auto instantiated = structDef->instantiate(args);
        if (!instantiated) {
            error(node, "Generic count mismatch in struct instantiation");
            lastExprType = nullptr;
            return;
        }
        concreteType = std::static_pointer_cast<StructType>(instantiated);
    }
    
    lastExprType = concreteType;

    for(auto& f : node.fields) {
        f.second->accept(*this);
        auto exprType = lastExprType; 
        auto fieldType = concreteType->getFieldType(f.first);
        
        if (!fieldType) {
            error(node, fmt::format("Struct '{}' has no field '{}'", concreteType->toString(), f.first));
        } else {
            checkType(*f.second, exprType, fieldType);
        }
    }
    
    lastExprType = concreteType;
}

void SemanticAnalyzer::visit(ArrayLiteral& node) {
    if (node.elements.empty()) {
        error(node, "Empty array literal cannot infer type.");
        lastExprType = nullptr;
        return;
    }

    node.elements[0]->accept(*this);
    auto firstType = lastExprType;

    if (!firstType) return;

    for (size_t i = 1; i < node.elements.size(); ++i) {
        node.elements[i]->accept(*this);
        checkType(*node.elements[i], lastExprType, firstType);
    }

    lastExprType = std::make_shared<ArrayType>(firstType, true);
}

void SemanticAnalyzer::visit(SizeofExpression& node) {
    if (node.type_target) {
        resolveTypeFromAST(node.type_target.get());
    } else if (node.expr_target) {
        node.expr_target->accept(*this);
    }
    lastExprType = currentScope->resolveType("int");
}

void SemanticAnalyzer::visit(LambdaExpression& node) {
    // The scope is entered before the signature is resolved, and the lambda's own
    // type parameters are registered in it first, because `<T>(m: T) <T> => m`
    // declares T and then immediately uses it: resolving the signature outside
    // the scope left T undefined in the one place it is introduced, and the
    // parameter it typed undefined in the body. This mirrors what
    // visit(FunctionDeclaration&) does at Analyzer_Decl.cpp:43.
    enterScope();

    declareGenericParams(node.generic_params);

    std::shared_ptr<Type> retType = nullptr;
    if (node.return_type) {
        retType = resolveTypeFromAST(node.return_type.get());
    } else {
        retType = currentScope->resolveType("void"); 
    }

    std::vector<std::shared_ptr<Type>> paramTypes;
    for(auto& param : node.params) {
        auto t = resolveTypeFromAST(param->type.get());
        if(t) {
            currentScope->define({param->name, t, false, true});
            paramTypes.push_back(t);
        }
    }
    
    auto prevRet = context.currentFuncReturnType;
    context.currentFuncReturnType = retType;
    
    if (node.body) {
        node.body->accept(*this);
    } else if (node.expression_body) {
        node.expression_body->accept(*this);
        if (lastExprType) {
            checkType(*node.expression_body, lastExprType, retType);
        }
    }
    
    context.currentFuncReturnType = prevRet;
    exitScope();
    
    // Not when the return type did not resolve: FunctionType dereferences it in
    // toString(), and nullptr already means "unknown, stop asking" to every
    // reader of lastExprType.
    lastExprType = retType ? std::make_shared<FunctionType>(paramTypes, retType) : nullptr;
}

void SemanticAnalyzer::visit(QuoteExpression& node) {
    if (node.block) node.block->accept(*this);
    lastExprType = currentScope->resolveType("auto");
}

void SemanticAnalyzer::visit(TernaryOp& node) {
    node.condition->accept(*this);
    node.true_expr->accept(*this);
    auto t = lastExprType;
    node.false_expr->accept(*this);
    auto f = lastExprType;
    if (t && f) {
        // A ternary has no expected branch either. When one branch is an integer
        // constant it takes the other branch's type, and the *other* branch's type
        // is the result -- otherwise `true : 1 ? a` for a `uint` a would report the
        // variable as the error and then hand `int` to whatever consumes it, which
        // is two wrong diagnostics from one constant.
        if (constantFitsType(*node.false_expr, *t)) {
            lastExprType = t;
        } else if (constantFitsType(*node.true_expr, *f)) {
            lastExprType = f;
        } else {
            checkType(*node.false_expr, f, t);
            lastExprType = t;
        }
    }
}

void SemanticAnalyzer::visit(StaticMethodCall& node) {
    // 1. Resolve Target Type (e.g. Vec2<float>)
    auto type = resolveTypeFromAST(node.target_type.get());
    if (!type) {
        lastExprType = nullptr;
        return;
    }

    // 2. Unwrap Self/Pointers to get StructType
    auto structType = getStructType(type, currentScope);
    if (!structType) {
        error(node, fmt::format("Type '{}' is not a struct", type->toString()));
        lastExprType = nullptr;
        return;
    }

    // 3. Look up Method
    
    auto retType = structType->getMethodReturnType(node.method_name);
    if (!retType) {
        error(node, fmt::format("Static method '{}' not found in '{}'", node.method_name, structType->toString()));
        lastExprType = nullptr;
        return;
    }

    // 4. Analyze Args
    for (auto& arg : node.args) {
        arg->accept(*this);
    }

    lastExprType = retType;
}

void SemanticAnalyzer::visit(SuperExpression& node) {
    std::shared_ptr<Type> parentType = nullptr;
    
    if (auto st = std::dynamic_pointer_cast<StructType>(currentStructContext)) {
        if (!node.parent_name.empty()) {
            for(auto& p : st->parents) {
                if (p->toString() == node.parent_name) {
                    parentType = p;
                    break;
                }
            }
            if (!parentType) parentType = currentScope->resolveType(node.parent_name);
        } else {
            if (!st->parents.empty()) {
                parentType = st->parents[0];
            }
        }
    }

    if (!parentType) {
        error(node, "Cannot resolve 'super' (no parent found)");
        lastExprType = nullptr;
        return;
    }

    if (!node.init_fields.empty()) {
        for (auto& f : node.init_fields) f.second->accept(*this);
    } else {
        for (auto& arg : node.args) arg->accept(*this);
    }

    lastExprType = parentType;
}



} // namespace fin
