#include "../MacroExpander.hpp"
#include "../SubstitutionVisitor.hpp"
#include "../../ast/CloneVisitor.hpp"
#include "../../types/NamespaceType.hpp"
#include <fmt/core.h>

namespace fin {

// --- Lookup Helper ---
MacroDeclaration* MacroExpander::resolveMacro(const std::string& name) {
    if (!currentScope) return nullptr;

    // 1. Check local scope
    if (auto* m = currentScope->resolveMacro(name)) return m;
    
    // 2. Check Namespaces (e.g. "macros.magic_add")
    size_t dotPos = name.find('.');
    if (dotPos != std::string::npos) {
        std::string nsName = name.substr(0, dotPos);
        std::string macroName = name.substr(dotPos + 1);
        
        if (auto* sym = currentScope->resolve(nsName)) {
            if (auto* ns = dynamic_cast<NamespaceType*>(sym->type.get())) {
                return ns->scope->resolveMacro(macroName);
            }
        }
    }
    
    return nullptr;
}

// --- Invocation ---
void MacroExpander::visit(MacroInvocation& node) {
    // 1. Find Macro using helper
    MacroDeclaration* def = resolveMacro(node.name);
    
    if (!def) {
        diag.reportError(node.loc, "Undefined macro '" + node.name + "!'");
        return;
    }
    
    // 2. Check args
    bool isVararg = !def->params.empty() && def->params.back().is_vararg;
    size_t minArgs = isVararg ? def->params.size() - 1 : def->params.size();
    
    if (node.args.size() < minArgs || (!isVararg && node.args.size() > minArgs)) {
        diag.reportError(node.loc, fmt::format("Macro '{}' expects {} {} args, got {}", 
            node.name, isVararg ? "at least" : "exactly", minArgs, node.args.size()));
        return;
    }
    
    // 3. Find quote
    QuoteExpression* quote = nullptr;
    for (auto& stmt : def->body->statements) {
        if (auto* ret = dynamic_cast<ReturnStatement*>(stmt.get())) {
            if (ret->value) {
                if (auto* q = dynamic_cast<QuoteExpression*>(ret->value.get())) {
                    quote = q;
                    break;
                }
            }
        }
    }
    
    if (!quote || !quote->block) {
        diag.reportError(def->loc, "Macro must return a quote { ... } block");
        return;
    }
    
    // 4. Map Arguments
    std::unordered_map<std::string, std::unique_ptr<Expression>> argsMap;
    for (size_t i = 0; i < def->params.size(); ++i) {
        if (def->params[i].is_vararg) {
            std::vector<std::unique_ptr<Expression>> varargs;
            for (size_t j = i; j < node.args.size(); ++j) {
                CloneVisitor cloner;
                varargs.push_back(cloner.clone(node.args[j].get()));
            }
            argsMap[def->params[i].name] = std::make_unique<ArrayLiteral>(std::move(varargs));
            break;
        } else {
            CloneVisitor cloner;
            argsMap[def->params[i].name] = cloner.clone(node.args[i].get());
        }
    }
    
    // 5. Clone Body
    if (quote->block->statements.empty()) {
        diag.reportError(node.loc, "Macro quote block is empty");
        return;
    }
    
    auto* firstStmt = quote->block->statements[0].get();
    std::unique_ptr<Expression> resultExpr = nullptr;
    
    if (auto* exprStmt = dynamic_cast<ExpressionStatement*>(firstStmt)) {
        CloneVisitor cloner;
        resultExpr = cloner.clone(exprStmt->expr.get());
    } else {
        diag.reportError(node.loc, "Macro quote must contain a single expression statement");
        return;
    }
    
    // 6. Substitute
    SubstitutionVisitor subVisitor(argsMap);
    resultExpr->accept(subVisitor);
    if (subVisitor.replacementExpr) {
        resultExpr = std::move(subVisitor.replacementExpr);
    }
    
    // 7. Set Result
    expandedExpression = std::move(resultExpr);
}

void MacroExpander::visit(StaticMethodCall& node) {
    node.target_type->accept(*this);
    for (auto& arg : node.args) {
        arg->accept(*this);
        if (expandedExpression) {
            arg = std::move(expandedExpression);
            expandedExpression = nullptr;
        }
    }
}

void MacroExpander::visit(BinaryOp& node) {
    node.left->accept(*this);
    if (expandedExpression) { node.left = std::move(expandedExpression); expandedExpression = nullptr; }
    node.right->accept(*this);
    if (expandedExpression) { node.right = std::move(expandedExpression); expandedExpression = nullptr; }
}
void MacroExpander::visit(UnaryOp& node) {
    node.operand->accept(*this);
    if (expandedExpression) { node.operand = std::move(expandedExpression); expandedExpression = nullptr; }
}
void MacroExpander::visit(FunctionCall& node) {
    for (auto& arg : node.args) {
        arg->accept(*this);
        if (expandedExpression) { arg = std::move(expandedExpression); expandedExpression = nullptr; }
    }
}
void MacroExpander::visit(MethodCall& node) {
    node.object->accept(*this);
    if (expandedExpression) { node.object = std::move(expandedExpression); expandedExpression = nullptr; }
    for (auto& arg : node.args) {
        arg->accept(*this);
        if (expandedExpression) { arg = std::move(expandedExpression); expandedExpression = nullptr; }
    }
}
void MacroExpander::visit(TypeLiteralExpression& node) {
    // A macro invocation inside an anonymous type's body expands, for the same
    // reason it does inside a named one.
    node.decl->accept(*this);
}
void MacroExpander::visit(CastExpression& node) {
    node.expr->accept(*this);
    if (expandedExpression) { node.expr = std::move(expandedExpression); expandedExpression = nullptr; }
}
void MacroExpander::visit(NewExpression& node) {
    for (auto& arg : node.args) {
        arg->accept(*this);
        if (expandedExpression) { arg = std::move(expandedExpression); expandedExpression = nullptr; }
    }
    for (auto& f : node.init_fields) {
        f.second->accept(*this);
        if (expandedExpression) { f.second = std::move(expandedExpression); expandedExpression = nullptr; }
    }
}
void MacroExpander::visit(MemberAccess& node) {
    node.object->accept(*this);
    if (expandedExpression) { node.object = std::move(expandedExpression); expandedExpression = nullptr; }
}
void MacroExpander::visit(StructInstantiation& node) {
    for (auto& f : node.fields) {
        f.second->accept(*this);
        if (expandedExpression) { f.second = std::move(expandedExpression); expandedExpression = nullptr; }
    }
}
void MacroExpander::visit(ArrayLiteral& node) {
    for (auto& elem : node.elements) {
        elem->accept(*this);
        if (expandedExpression) { elem = std::move(expandedExpression); expandedExpression = nullptr; }
    }
}
void MacroExpander::visit(ArrayAccess& node) {
    node.array->accept(*this);
    if (expandedExpression) { node.array = std::move(expandedExpression); expandedExpression = nullptr; }
    node.index->accept(*this);
    if (expandedExpression) { node.index = std::move(expandedExpression); expandedExpression = nullptr; }
}
void MacroExpander::visit(TernaryOp& node) {
    node.condition->accept(*this);
    if (expandedExpression) { node.condition = std::move(expandedExpression); expandedExpression = nullptr; }
    node.true_expr->accept(*this);
    if (expandedExpression) { node.true_expr = std::move(expandedExpression); expandedExpression = nullptr; }
    node.false_expr->accept(*this);
    if (expandedExpression) { node.false_expr = std::move(expandedExpression); expandedExpression = nullptr; }
}
void MacroExpander::visit(LambdaExpression& node) {
    if (node.body) node.body->accept(*this);
    if (node.expression_body) {
        node.expression_body->accept(*this);
        if (expandedExpression) { node.expression_body = std::move(expandedExpression); expandedExpression = nullptr; }
    }
}
void MacroExpander::visit(SizeofExpression& node) {
    if (node.expr_target) {
        node.expr_target->accept(*this);
        if (expandedExpression) { node.expr_target = std::move(expandedExpression); expandedExpression = nullptr; }
    }
}
void MacroExpander::visit(SuperExpression& node) {
    for (auto& f : node.init_fields) {
        f.second->accept(*this);
        if (expandedExpression) { f.second = std::move(expandedExpression); expandedExpression = nullptr; }
    }
    for (auto& arg : node.args) {
        arg->accept(*this);
        if (expandedExpression) { arg = std::move(expandedExpression); expandedExpression = nullptr; }
    }
}
void MacroExpander::visit(MacroCall& node) {
    for (auto& arg : node.args) {
        arg->accept(*this);
        if (expandedExpression) {
            arg = std::move(expandedExpression);
            expandedExpression = nullptr;
        }
    }
}

void MacroExpander::visit(PrototypeLiteral& node) {
    for (auto& element : node.elements) {
        if (element.first) {
            element.first->accept(*this);
            if (expandedExpression) {
                element.first = std::move(expandedExpression);
                expandedExpression = nullptr;
            }
        }
        if (element.second) {
            element.second->accept(*this);
            if (expandedExpression) {
                element.second = std::move(expandedExpression);
                expandedExpression = nullptr;
            }
        }
    }
}

void MacroExpander::visit(Literal&) {}
void MacroExpander::visit(Identifier&) {}
void MacroExpander::visit(QuoteExpression&) {}

}
