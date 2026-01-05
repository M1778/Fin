#include "../SemanticAnalyzer.hpp"
#include "../../types/TypeImpl.hpp" // Ensure full type definitions are visible
#include <fmt/core.h>
#include <fmt/color.h>

namespace fin {

void SemanticAnalyzer::visit(Literal& node) {
    switch(node.kind) {
        case ASTTokenKind::INTEGER: lastExprType = currentScope->resolveType("int"); break;
        case ASTTokenKind::FLOAT:   lastExprType = currentScope->resolveType("float"); break;
        case ASTTokenKind::STRING_LITERAL:  lastExprType = currentScope->resolveType("string"); break;
        case ASTTokenKind::BOOL:    lastExprType = currentScope->resolveType("bool"); break;
        case ASTTokenKind::KW_NULL: lastExprType = std::make_shared<PointerType>(currentScope->resolveType("void")); break;
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
        if (auto st = std::dynamic_pointer_cast<StructType>(currentStructContext)) {
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
        // Check L-Value
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
    if (auto structType = std::dynamic_pointer_cast<StructType>(leftType)) {
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
        checkType(*node.right, rightType, leftType);
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
    else if (node.op == ASTTokenKind::MULT) {
        if (auto* ptr = dynamic_cast<const PointerType*>(type.get())) {
            lastExprType = ptr->pointee;
        } 
        else if (auto* ptrToArray = dynamic_cast<const PointerType*>(type.get())) {
            if (auto* arr = dynamic_cast<const ArrayType*>(ptrToArray->pointee.get())) {
                // Clone the array type
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
    // Case 2: Static Method
    else if (funcName.find("::") != std::string::npos) {
        auto delimiterPos = funcName.find("::");
        std::string typeName = funcName.substr(0, delimiterPos);
        std::string methodName = funcName.substr(delimiterPos + 2);
        
        std::shared_ptr<Type> type = nullptr;
        if (typeName == "Self") type = currentStructContext;
        else type = currentScope->resolveType(typeName);

        if (auto st = std::dynamic_pointer_cast<StructType>(type)) {
            auto methodType = st->getMethodReturnType(methodName);
            if (methodType) {
                std::vector<std::shared_ptr<Type>> dummyParams;
                funcType = std::make_shared<FunctionType>(dummyParams, methodType, true);
            } else {
                error(node, fmt::format("Static method '{}' not found in '{}'", methodName, typeName));
            }
        } else {
             error(node, "Undefined type '" + typeName + "'");
        }
    }
    // Case 3: Standard Function
    else {
        auto type = currentScope->resolveType(funcName);
        if (type) {
            if (auto st = std::dynamic_pointer_cast<StructType>(type)) {
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

    if (!funcType->is_vararg && actual != expected) {
        error(node, fmt::format("Function '{}' expects {} arguments, got {}", funcName, expected, actual));
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

    std::shared_ptr<StructType> structType = nullptr;

    if (auto* st = dynamic_cast<StructType*>(objType.get())) {
        structType = std::static_pointer_cast<StructType>(objType);
    } 
    else if (auto* ptr = dynamic_cast<PointerType*>(objType.get())) {
        if (auto* st = dynamic_cast<StructType*>(ptr->pointee.get())) {
            structType = std::static_pointer_cast<StructType>(ptr->pointee);
        }
    }
    else if (auto* gt = dynamic_cast<GenericType*>(objType.get())) {
        structType = std::dynamic_pointer_cast<StructType>(currentScope->resolveType("Castable"));
    }

    if (!structType) {
        error(node, fmt::format("Type '{}' does not have methods", objType->toString()));
        lastExprType = nullptr;
        return;
    }

    auto retType = structType->getMethodReturnType(node.method_name);
    if (!retType) {
        error(node, fmt::format("Method '{}' not found in type '{}'", node.method_name, structType->name));
        lastExprType = nullptr;
    } else {
        lastExprType = retType;
    }

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
    lastExprType = std::make_shared<PointerType>(allocatedType);
}

void SemanticAnalyzer::visit(MemberAccess& node) {
    node.object->accept(*this);
    auto objType = lastExprType;
    if (!objType) return;

    // Namespace Access
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

    std::shared_ptr<StructType> structType = nullptr;
    
    if (auto* ptr = dynamic_cast<const PointerType*>(objType.get())) {
        if (auto* st = dynamic_cast<StructType*>(ptr->pointee.get())) {
            structType = std::static_pointer_cast<StructType>(ptr->pointee);
        }
    } else if (auto* st = dynamic_cast<StructType*>(objType.get())) {
        structType = std::static_pointer_cast<StructType>(objType);
    }

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
        // Visibility Check
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
    std::shared_ptr<Type> retType = nullptr;
    if (node.return_type) {
        retType = resolveTypeFromAST(node.return_type.get());
    } else {
        retType = currentScope->resolveType("void"); 
    }

    enterScope();
    
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
    
    lastExprType = std::make_shared<FunctionType>(paramTypes, retType);
}

void SemanticAnalyzer::visit(MacroInvocation& node) {
    for(auto& arg : node.args) arg->accept(*this);
    lastExprType = currentScope->resolveType("void");
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
        checkType(*node.false_expr, f, t);
        lastExprType = t;
    }
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

void SemanticAnalyzer::visit(MacroCall&) {}

} // namespace fin