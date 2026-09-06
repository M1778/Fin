#include "../MacroExpander.hpp"
#include "../SubstitutionVisitor.hpp"
#include "../../ast/CloneVisitor.hpp"
#include "../../ast/StructuralWalk.hpp"
#include "../../ast/types/TypeNode.hpp"
#include "../../types/NamespaceType.hpp"
// <fmt/format.h> and not <fmt/core.h>, because fmt::format is used below and this
// is the header that declares it.  From fmt 11 core.h carries only the base API
// and fmt::format is behind FMT_DEPRECATED_HEAVY_CORE, so `#include <fmt/core.h>`
// plus `fmt::format` is `'format' is not a member of 'fmt'` -- measured against the
// system fmt 12.2.0, clean against the conanfile's fmt 10.2.1, and format.h is
// correct against both.  Every other caller in this tree reaches fmt::format
// through <fmt/color.h>, which includes format.h; this translation unit had no such
// include, so it is the one that a non-Conan configure breaks on.
#include <fmt/format.h>

namespace fin {

namespace {

// Writes the declaring module onto every type in a macro's expansion (ADR 0023 step 4).
//
// The expansion is a clone of the quote body, so these nodes are new and unshared and
// the stamp reaches nothing a programmer wrote. It is what lets `Held::make($n)` in
// `lib/std`'s macro name `Held` in a caller that never imported it: the analyzer looks
// the name up where the macro was written rather than where it was called, which is the
// difference between a library macro and a C macro.
//
// Only unstamped types are written. A macro whose body invokes another macro expands the
// inner one first, so the inner expansion arrives already carrying *its* declaring
// module -- the nearer answer, and the right one.
//
// Run before substitution, never after: the arguments are the caller's own expressions
// and their types resolve where the caller wrote them. Stamping the merged tree would
// hand the callee's imports to the caller's types, which is the mirror of the bug this
// closes.
class DeclaringScopeStamp : public StructuralWalk {
public:
    explicit DeclaringScopeStamp(Scope* scope) : scope(scope) {}

protected:
    bool enter(ASTNode& node) override {
        if (auto* type = dynamic_cast<TypeNode*>(&node)) {
            if (!type->declaringScope) type->declaringScope = scope;
        }
        return true;
    }

private:
    Scope* scope;
};

} // namespace

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
    //
    // Guarded, because a macro does not always have a body. A bodyless declaration --
    // `@define format!(fmt: string, ...) <string>;` -- is a macro the *compiler*
    // implements (ADR 0023), so there is no template here to substitute into and the
    // expander is not the pass that answers the call. The analyzer's builtin table is.
    //
    // No form the grammar accepts builds a MacroDeclaration without a block today: the
    // arms form that did -- whose constructor filled `rules` and left `body` null -- is
    // deleted with `MacroRule`, so this is unreachable rather than a refusal a program
    // can provoke. It stays because what it prevents is a crash and not a mistake:
    // `def->body->statements` on a null body exited 139, which is not one of the four
    // codes ADR 0009 gives finc, so a caller reading the status learned neither that
    // the build succeeded nor that it was rejected.
    //
    // ADR 0023 step 5 makes it reachable and changes the answer: a bodyless
    // `@define format!(fmt: string, ...) <string>;` is a macro the *compiler*
    // implements, so the call is answered downstream and this pass leaves it alone.
    // Until that exists, a bodyless macro is a macro with nothing to expand.
    if (!def->body) {
        diag.reportError(node.loc,
            fmt::format("Macro '{}' has no body to expand", node.name));
        return;
    }

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
    
    // 6. Stamp the declaring module, then substitute
    //
    // Nothing to stamp for a macro declared in the file being compiled: `declaringScope`
    // is null there, its body already resolves where it was written, and a stamp would
    // only add a fallback that changes no answer.
    if (def->declaringScope) {
        DeclaringScopeStamp stamp(def->declaringScope);
        stamp.walk(*resultExpr);
    }

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
