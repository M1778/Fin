#include "MacroExpander.hpp"
#include "SubstitutionVisitor.hpp"
#include "../ast/CloneVisitor.hpp"
#include "../utils/ModuleLoader.hpp" // Ensure this is included for loader->loadModule
#include "../types/TypeImpl.hpp"     // Needed for NamespaceType
#include <fmt/core.h>
#include <fmt/color.h>
#include <filesystem>

namespace fin {

MacroExpander::MacroExpander(DiagnosticEngine& d, Scope* s) : diag(d), currentScope(s) {}

void MacroExpander::expand(Program& node) {
    node.accept(*this);
}

// --- Registration ---
void MacroExpander::visit(MacroDeclaration& node) {
    if (currentScope) {
        currentScope->defineMacro(node.name, &node);
    }
}

void MacroExpander::visit(PointerTypeNode& node) {
    node.pointee->accept(*this);
}

void MacroExpander::visit(ArrayTypeNode& node) {
    node.element_type->accept(*this);
    if (node.size) {
        node.size->accept(*this);
        if (expandedExpression) {
            node.size = std::move(expandedExpression);
            expandedExpression = nullptr;
        }
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
    
    // 2. Check Args
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

// --- Traversal Boilerplate ---

void MacroExpander::visit(Program& node) { for (auto& stmt : node.statements) stmt->accept(*this); }
void MacroExpander::visit(Block& node) { for (auto& stmt : node.statements) stmt->accept(*this); }
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
void MacroExpander::visit(OperatorDeclaration& node) {
    for (auto& param : node.params) param->accept(*this);
    if (node.return_type) node.return_type->accept(*this);
    
    if (node.body) node.body->accept(*this);
}

void MacroExpander::visit(ConstructorDeclaration& node) { if (node.body) node.body->accept(*this); }
void MacroExpander::visit(DestructorDeclaration& node) { if (node.body) node.body->accept(*this); }

void MacroExpander::visit(VariableDeclaration& node) {
    if (node.type) {
        node.type->accept(*this);
    }

    if (node.initializer) {
        node.initializer->accept(*this);
        if (expandedExpression) {
            node.initializer = std::move(expandedExpression);
            expandedExpression = nullptr;
        }
    }
}
void MacroExpander::visit(ReturnStatement& node) {
    if (node.value) {
        node.value->accept(*this);
        if (expandedExpression) {
            node.value = std::move(expandedExpression);
            expandedExpression = nullptr;
        }
    }
}
void MacroExpander::visit(ExpressionStatement& node) {
    node.expr->accept(*this);
    if (expandedExpression) {
        node.expr = std::move(expandedExpression);
        expandedExpression = nullptr;
    }
}
void MacroExpander::visit(IfStatement& node) {
    node.condition->accept(*this);
    if (expandedExpression) { node.condition = std::move(expandedExpression); expandedExpression = nullptr; }
    node.then_block->accept(*this);
    if (node.else_stmt) node.else_stmt->accept(*this);
}
void MacroExpander::visit(WhileLoop& node) {
    node.condition->accept(*this);
    if (expandedExpression) { node.condition = std::move(expandedExpression); expandedExpression = nullptr; }
    node.body->accept(*this);
}
void MacroExpander::visit(ForLoop& node) {
    if (node.init) node.init->accept(*this);
    if (node.condition) {
        node.condition->accept(*this);
        if (expandedExpression) { node.condition = std::move(expandedExpression); expandedExpression = nullptr; }
    }
    if (node.increment) {
        node.increment->accept(*this);
        if (expandedExpression) { node.increment = std::move(expandedExpression); expandedExpression = nullptr; }
    }
    node.body->accept(*this);
}
void MacroExpander::visit(ForeachLoop& node) {
    node.iterable->accept(*this);
    if (expandedExpression) { node.iterable = std::move(expandedExpression); expandedExpression = nullptr; }
    node.body->accept(*this);
}
void MacroExpander::visit(DeleteStatement& node) {
    node.expr->accept(*this);
    if (expandedExpression) { node.expr = std::move(expandedExpression); expandedExpression = nullptr; }
}
void MacroExpander::visit(TryCatch& node) {
    node.try_block->accept(*this);
    node.catch_block->accept(*this);
}
void MacroExpander::visit(BlameStatement& node) {
    node.error_expr->accept(*this);
    if (expandedExpression) { node.error_expr = std::move(expandedExpression); expandedExpression = nullptr; }
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

void MacroExpander::visit(EnumDeclaration& node) {
    // Enums can have values like: Alive = CONST_VAL!
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
    // Externs might have types with expressions (e.g. array sizes)
    for (auto& param : node.params) {
        param->accept(*this);
    }
    if (node.return_type) {
        node.return_type->accept(*this);
    }
}

void MacroExpander::visit(InterfaceDeclaration& node) {
    // Interfaces have members (types) and methods (params/return types)
    for (auto& member : node.members) {
        member->accept(*this);
    }
    for (auto& method : node.methods) {
        method->accept(*this);
    }
}

void MacroExpander::visit(TypeNode& node) {
    // Types can have array sizes: [int, MAX_SIZE!]
    if (node.array_size) {
        node.array_size->accept(*this);
        if (expandedExpression) {
            node.array_size = std::move(expandedExpression);
            expandedExpression = nullptr;
        }
    }
    // Traverse generics: Box<T, Array<int, SIZE!>>
    for (auto& g : node.generics) {
        g->accept(*this);
    }
}

void MacroExpander::visit(FunctionTypeNode& node) {
    for (auto& p : node.param_types) {
        p->accept(*this);
    }
    if (node.return_type) {
        node.return_type->accept(*this);
    }
}

void MacroExpander::visit(MacroCall& node) {
    // Legacy macro syntax $name(...) - treat args as expressions
    for (auto& arg : node.args) {
        arg->accept(*this);
        if (expandedExpression) {
            arg = std::move(expandedExpression);
            expandedExpression = nullptr;
        }
    }
}

// Parameter traversal (helper for declarations)
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

// StructMember traversal (helper)
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


void MacroExpander::visit(Literal&) {}
void MacroExpander::visit(Identifier&) {}
void MacroExpander::visit(QuoteExpression&) {}
void MacroExpander::visit(BreakStatement&) {}
void MacroExpander::visit(ContinueStatement&) {}


} // namespace fin