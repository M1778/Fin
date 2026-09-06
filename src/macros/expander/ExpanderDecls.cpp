#include "../MacroExpander.hpp"
#include "../../types/TypeImpl.hpp" 
#include "../../utils/ModuleLoader.hpp"
#include "../../ast/StructuralWalk.hpp"
#include "../../ast/exprs/Identifier.hpp"
#include "../../ast/exprs/FunctionCall.hpp"
#include "../../ast/exprs/StructureExpr.hpp"
#include <filesystem>
#include <fmt/core.h>

namespace fin {

namespace {

// The hygiene refusal (ADR 0023 step 4): a macro body may spell only `$param`
// unquotes, literals, operators, and qualified paths.
//
// A bare unqualified name in a body is refused *at the declaration*, with no call site
// present, because the alternative is what ADR 0020 calls the C-preprocessor failure:
// a body that says `temp` collides with a caller's `temp`, and one that reads `count`
// binds whatever `count` the call site happened to have. Before this, `@macro twice(a)
// { return quote { a + a; }; }` -- bare `a`, no `$` -- reported `Undefined variable
// 'a'` at the *body's* line, which is only possible because the body's names were
// looked up in the caller's scope.
//
// The rule costs almost nothing here, and that is an argument for the single-expression
// form as much as for the rule: a body is one expression and so cannot declare a
// binding at all, so there is no `let temp` for the ban to get in the way of.
//
// What counts as qualified, and why each is not refused:
//   - `Type::method(...)` is a StaticMethodCall, whose target is a TypeNode and not an
//     Identifier, so it never reaches the refusal.
//   - `Type::MEMBER` is a MemberAccess with `is_static`, whose object *is* an
//     Identifier -- the type qualifier. Skipped explicitly; it is the one place a bare
//     Identifier node is a qualifier rather than a free name.
//   - `ns.name` is a MemberAccess on an Identifier that is *not* static, so the
//     qualifier is refused like any other free name. That is deliberate: a namespace
//     alias is the importing file's local name for a module (`ExpanderDecls.cpp`'s
//     namespace branch aliases by file stem), so a body naming one would be reaching
//     for a binding in whichever file happened to import it -- exactly the capture
//     being closed.
//
// A free call is refused through `FunctionCall::name`, which is a string and not an
// Identifier node: `from_prototype($items)` names something in no namespace, and ADR
// 0023 spells the two macros the corpus asks for as `Collection::from_prototype` and
// `HashMap::from_prototype` precisely because of this rule.
//
// Type names in `new`/cast/annotation position are NOT refused. A type cannot be a
// caller's local -- `Scope` keeps types in a map of their own -- so no capture is
// possible through one, and the sketch at macro_definitions.fin:11 writes
// `new Collection::<int>{}`. Resolution of those names is the other half of step 4:
// they resolve in the declaring module.
class HygieneWalk : public StructuralWalk {
public:
    HygieneWalk(DiagnosticEngine& d, const std::string& macroName)
        : diag(d), macro(macroName) {}

    int refusals = 0;

protected:
    bool enter(ASTNode& node) override {
        if (auto* access = dynamic_cast<MemberAccess*>(&node)) {
            if (access->is_static && dynamic_cast<Identifier*>(access->object.get())) {
                return false;   // `Type::MEMBER` -- the object is a qualifier
            }
        }
        if (auto* call = dynamic_cast<FunctionCall*>(&node)) {
            refuse(node, call->name);
            // Children still walked: the arguments are ordinary expressions and a bare
            // name in one of them is refused on its own account.
            return true;
        }
        if (auto* id = dynamic_cast<Identifier*>(&node)) {
            refuse(node, id->name);
        }
        return true;
    }

private:
    void refuse(ASTNode& at, const std::string& name) {
        // `$param` is the whole point of the form, and a name the parser produced for an
        // unquote always carries the sigil (`parser.y:2853`).
        if (!name.empty() && name[0] == '$') return;
        // `m1778` is a Literal kind rather than an Identifier (ASTNode.hpp), so it does
        // not arrive here; nothing else is exempt.
        ++refusals;
        diag.reportError(at.loc,
            fmt::format("macro '{}' names '{}' with no qualifier", macro, name),
            fmt::format("a macro body may spell only `$parameter` unquotes, literals, "
                        "operators and qualified paths such as `Type::{}` -- an "
                        "unqualified name would bind whatever the call site happened to "
                        "have (ADR 0023)", name));
    }

    DiagnosticEngine& diag;
    std::string macro;
};

} // namespace

// --- Registration ---
void MacroExpander::visit(MacroDeclaration& node) {
    if (currentScope) {
        currentScope->defineMacro(node.name, &node);
    }

    // Refused here rather than at expansion, which is what "with no call site present"
    // in ADR 0023 step 4's verification clause means: a library whose macro is never
    // called is still a library with a broken macro in it, and the diagnostic belongs on
    // the line the author can fix.
    //
    // A bodyless declaration -- `@define format!(fmt: string, ...) <string>;` -- has no
    // body to check, and a macro the compiler implements has no Fin names to capture
    // with.
    if (node.body) {
        HygieneWalk walk(diag, node.name);
        walk.walk(*node.body);
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
                macro->declaringScope = moduleScope.get();
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
    
    // Every macro the module declares, marked with the scope it was declared in (ADR
    // 0023 step 4). The namespace branch as well as the named one, because
    // `mymacros.magic_add!(10, 20)` reaches a macro through `resolveMacro`'s dotted
    // path (`ExpanderExprs.cpp`) and that macro's body has the same claim on its own
    // module's imports as a named-imported one does.
    //
    // `moduleScope.get()` and not the expander's own scope: this shared_ptr is
    // `ModuleLoader::moduleCache`'s entry and lives as long as the loader, while the
    // `macroScope` `loadModule` built for its own expansion pass dies at its return.
    // Storing that one would leave a dangling pointer on every macro in the module.
    for (auto& kv : moduleScope->macros) {
        if (kv.second) kv.second->declaringScope = moduleScope.get();
    }

    // Define a symbol with NamespaceType that holds the scope
    auto nsType = std::make_shared<NamespaceType>(alias, moduleScope);
    
    Symbol sym{alias, nsType, false, true};
    currentScope->define(sym);
}

void MacroExpander::visit(Program& node) { for (auto& stmt : node.statements) stmt->accept(*this); }

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

void MacroExpander::visit(ClassDeclaration& node) {
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

void MacroExpander::visit(EnumDeclaration& node) {
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
    for (auto& param : node.params) {
        param->accept(*this);
    }
    if (node.return_type) {
        node.return_type->accept(*this);
    }
}

void MacroExpander::visit(InterfaceDeclaration& node) {
    for (auto& member : node.members) {
        member->accept(*this);
    }
    for (auto& method : node.methods) {
        method->accept(*this);
    }
}

// Helpers
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

void MacroExpander::visit(TypeDefinition& node) {
    if (node.aliased_type) node.aliased_type->accept(*this);
    for (auto& impl : node.implements_list) {
        impl->accept(*this);
    }
}

void MacroExpander::visit(SpecialDeclaration& node) {
    for (auto& param : node.params) {
        param->accept(*this);
    }
    if (node.return_type) node.return_type->accept(*this);
    if (node.body) node.body->accept(*this);
}

void MacroExpander::visit(ImplementsBlock& node) {
    if (node.interface_type) node.interface_type->accept(*this);
    for (auto& method : node.methods) method->accept(*this);
    for (auto& op : node.operators) op->accept(*this);
}

}
