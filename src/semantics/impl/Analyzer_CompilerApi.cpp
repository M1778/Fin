#include "../SemanticAnalyzer.hpp"
#include "../../ast/types/Attribute.hpp"
#include "../../ast/exprs/FunctionCall.hpp"
#include "../../types/FunctionType.hpp"
#include "../../types/NullableType.hpp"
#include <algorithm>

// The compiler API's use site: `#[use(...)]` on a declaration, and the three layers
// reached through the name it grants.
//
// docs/compiler-api.md is the design and ADR 0012 is the ratified split. What lives
// here is only the walk; the inventory is CompilerApi.cpp, deliberately, because the
// next component should be a table row and not a case in this file.
namespace fin {

namespace {
const char* kComponents = "components";
const char* kGrantPrefix = "compiler.components.";
} // namespace

// Reads the grants off a declaration and binds the name they ask for.
//
// Called from *inside* the declaration's own scope, which is what makes a grant
// per-declaration: `compiler` is a Symbol in that scope and leaves with it, and
// `currentGrants` is saved and restored by the caller for the same reason. A grant
// that outlived its declaration would turn `#[use]` on the one function that needs
// the API into a file-level switch arming every function under it --
// Soundness_CompilerApi.TheGrantDoesNotReachTheNextDeclaration.
void SemanticAnalyzer::applyUseAttributes(
    ASTNode& node, const std::vector<std::unique_ptr<Attribute>>& attrs) {

    bool grantsCompiler = false;
    std::vector<std::string> comps;

    for (const auto& a : attrs) {
        if (!a || a->name != "use") continue;
        const std::string& v = a->value_str;

        if (v == "compiler") { grantsCompiler = true; continue; }

        // `#[use(...)]` of something that is not the compiler API is left alone. The
        // attribute is a general "this declaration uses X" and the corpus writes only
        // the two compiler spellings; claiming the whole attribute here would reject
        // a use of it that has nothing to do with this.
        if (v.rfind("compiler", 0) != 0) continue;

        if (v.rfind(kGrantPrefix, 0) != 0) {
            error(node, "'" + v + "' is not a component reference: a grant is written "
                        "#[use(compiler)] or #[use(compiler.components.<name>)]");
            continue;
        }

        const std::string name = v.substr(std::string(kGrantPrefix).size());
        if (name.empty() || name.find('.') != std::string::npos) {
            error(node, "'" + v + "' is not a component reference: a component name has "
                        "no dot in it (ADR 0012)");
            continue;
        }

        // A misspelled grant is reported here rather than left to say nothing until the
        // use site. The blind spot it closes is the same one an unenforced `implements`
        // bound had: a bound that also fails to reject a misspelling is not a partial
        // implementation.
        //
        // The cost is that a library cannot grant a component this compiler does not
        // have, so the forward-compatible shape -- grant `gc`, guard every use with
        // `compiler.components.gc.present()` -- is unwritable until there is a way to
        // *not analyse* the guarded branch. docs/compiler-api.md §2.1a requires only
        // that `present()` answer, and it does (Soundness_CompilerApi
        // .AnAbsentComponentIsStillAskable); the conditional-use half needs a ruling
        // and is booked in docs/plan.md.
        if (!compilerapi::findComponent(name)) {
            error(node, "The compiler has no component '" + name + "'");
            continue;
        }
        comps.push_back(name);
    }

    currentGrants = std::move(comps);

    if (grantsCompiler) {
        // Not mutable and not assignable-to: `compiler` is the API, not a variable
        // holding it.
        currentScope->define({"compiler", std::make_shared<CompilerApiType>(), false, true});
    }
}

// One table row as a type. `R` is the turbofish argument -- `select_field::<R>(s, name)
// <?R>` is generic in its result, which is the only generic shape the corpus writes.
std::shared_ptr<Type> SemanticAnalyzer::compilerApiMemberType(
    const compilerapi::Member& m, const std::shared_ptr<Type>& turbofish) {

    auto named = [&](const std::string& spelling) -> std::shared_ptr<Type> {
        if (spelling == "R") return turbofish;
        return currentScope->resolveType(spelling);
    };

    auto result = named(m.result);
    if (!result) return nullptr;
    if (m.result_nullable) result = std::make_shared<NullableType>(result);
    if (m.is_constant) return result;

    std::vector<std::shared_ptr<Type>> params;
    for (const auto& p : m.params) {
        auto t = named(p);
        if (!t) return nullptr;
        params.push_back(t);
    }
    return std::make_shared<FunctionType>(params, result);
}

std::shared_ptr<Type> SemanticAnalyzer::resolveCompilerApi(
    ASTNode& node, const CompilerApiType& base, const std::string& member,
    std::vector<std::unique_ptr<Expression>>* args,
    std::vector<std::unique_ptr<TypeNode>>* generic_args) {

    const std::string& path = base.path;
    const std::string componentsPrefix = std::string(kComponents) + ".";

    auto walkArgs = [&] { if (args) for (auto& a : *args) a->accept(*this); };
    auto isGranted = [&](const std::string& n) {
        return std::find(currentGrants.begin(), currentGrants.end(), n) != currentGrants.end();
    };

    // ---- Layer 1: off `compiler` itself -----------------------------------------
    if (path.empty()) {
        if (member == kComponents) {
            if (args) {
                error(node, "'compiler.components' is the grant layer, not a function");
                walkArgs();
                return nullptr;
            }
            return std::make_shared<CompilerApiType>(kComponents);
        }
        if (!compilerapi::findComponent(member)) {
            error(node, "The compiler has no component '" + member + "'");
            walkArgs();
            return nullptr;
        }
        // ADR 0012 puts enforcement on the operations layer, and this is where it
        // bites: the grant layer above needs nothing, reaching *through* a component
        // needs the grant. It is what makes `#[use(compiler.components.<name>)]`
        // carry information rather than decorate.
        if (!isGranted(member)) {
            error(node, "Component '" + member + "' is not granted here: add "
                        "#[use(compiler.components." + member + ")]");
            walkArgs();
            return nullptr;
        }
        if (args) {
            error(node, "'compiler." + member + "' is a component, not a function");
            walkArgs();
            return nullptr;
        }
        return std::make_shared<CompilerApiType>(member);
    }

    // ---- Layer 2: a component reference, whether or not the component exists ------
    //
    // docs/compiler-api.md §2.1a: `compiler.components.gc.present()` must *evaluate*
    // to false rather than fail to resolve, or no library can ask what the compiler it
    // is being built by can do. So this layer never reports an unknown name -- the
    // reference exists, and what it answers is where the difference shows.
    if (path == kComponents) {
        if (args) {
            error(node, "'compiler.components." + member + "' is a component reference, "
                        "not a function (ADR 0012: the segment after `components` is a "
                        "component name)");
            walkArgs();
            return nullptr;
        }
        return std::make_shared<CompilerApiType>(componentsPrefix + member);
    }

    // ---- Layer 3: the four operations every reference answers ---------------------
    if (path.rfind(componentsPrefix, 0) == 0) {
        const std::string comp = path.substr(componentsPrefix.size());
        for (const auto& m : compilerapi::referenceOps()) {
            if (m.name != member) continue;
            auto type = compilerApiMemberType(m, nullptr);
            auto* fn = type ? type->as<FunctionType>() : nullptr;
            if (!fn) return nullptr;
            if (!args) return type;
            checkCallArguments(node, "Operation",
                               "compiler.components." + comp + "." + member, *fn, *args);
            return fn->return_type;
        }
        // ADR 0012's title, enforced: the two namespaces are different. An operation
        // of the component is not an operation of the reference to it.
        error(node, "A component reference has no member '" + member +
                    "': `compiler.components." + comp + "` answers present, version, "
                    "granted and name (ADR 0012)");
        walkArgs();
        return nullptr;
    }

    // ---- Layer 4: a component's own operations and constants ----------------------
    const auto* c = compilerapi::findComponent(path);
    if (!c) return nullptr;  // Layer 1 refused every path that is not a component.

    const auto* m = compilerapi::findMember(*c, member);
    if (!m) {
        error(node, "Component '" + path + "' has no member '" + member + "'");
        walkArgs();
        return nullptr;
    }

    const std::string full = "compiler." + path + "." + member;

    std::shared_ptr<Type> turbofish;
    if (generic_args && !generic_args->empty())
        turbofish = resolveTypeOrError((*generic_args)[0].get());
    if (m->generics > 0 && !turbofish) {
        error(node, "'" + full + "' needs a type argument: it is written " + member +
                    "::<T>(...)");
        walkArgs();
        return nullptr;
    }

    auto type = compilerApiMemberType(*m, turbofish);
    if (!type) { walkArgs(); return nullptr; }

    if (m->is_constant) {
        if (args) {
            error(node, "'" + full + "' is a constant, not a function");
            walkArgs();
            return nullptr;
        }
        return type;
    }

    // Read and not called: the signature, which is what a function value is
    // everywhere else in the language.
    if (!args) return type;

    auto* fn = type->as<FunctionType>();
    if (!fn) { walkArgs(); return nullptr; }
    checkCallArguments(node, "Operation", full, *fn, *args);
    return fn->return_type;
}

} // namespace fin
