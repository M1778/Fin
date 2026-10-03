#include "../SemanticAnalyzer.hpp"
#include "../ComptimeInterp.hpp"
#include "../../ast/StructuralWalk.hpp"
#include "../../ast/decls/Program.hpp"
#include "../../ast/decls/TypeDef.hpp"
#include "../../ast/exprs/BinaryOp.hpp"
#include "../../ast/exprs/FunctionCall.hpp"
#include "../../ast/exprs/Identifier.hpp"
#include "../../ast/exprs/Lambda.hpp"
#include "../../ast/exprs/MiscExpr.hpp"
#include "../../ast/exprs/StructureExpr.hpp"
#include "../../ast/stmts/ControlFlow.hpp"
#include "../../ast/stmts/Statement.hpp"
#include "../../ast/stmts/VariableDecl.hpp"
#include "../../ast/types/Attribute.hpp"
#include "../../types/FunctionType.hpp"
#include "../../types/NullableType.hpp"
#include "../../types/PrimitiveType.hpp"
#include "../../types/StructType.hpp"
#include "../EventRegistry.hpp"
#include <algorithm>
#include <optional>
#include <unordered_set>

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

// The operations-layer gate (ADR 0012), in one place so the component-call
// path and the hybrid member-read path cannot disagree about what "granted"
// means: reaching *through* a component needs its grant, asking *about* one
// never does. True when granted; errors naming the missing grant otherwise,
// with the words Soundness_CompilerApi pins.
bool SemanticAnalyzer::hasComponentGrant(const std::string& component) const {
    return std::find(currentGrants.begin(), currentGrants.end(), component) !=
           currentGrants.end();
}

bool SemanticAnalyzer::requireComponentGrant(ASTNode& node, const std::string& component) {
    if (hasComponentGrant(component)) return true;
    error(node, "Component '" + component + "' is not granted here: add "
                "#[use(compiler.components." + component + ")]");
    return false;
}

// The grant-path checks, shared by applyUseAttributes (which binds) and
// visit(Attribute&) (which validates positions that bind nothing). One function
// so the two cannot disagree about what a grant is; the messages are the ones
// Soundness_CompilerApi pins. Nullopt means "no diagnostic": either a valid
// grant or a `#[use(...)]` of something that is not the compiler API, which is
// a general "this declaration uses X" and not ours to judge.
static std::optional<std::string> useGrantError(const std::string& v) {
    if (v == "compiler") return std::nullopt;
    if (v.rfind("compiler", 0) != 0) return std::nullopt;
    if (v.rfind(kGrantPrefix, 0) != 0) {
        return "'" + v + "' is not a component reference: a grant is written "
               "#[use(compiler)] or #[use(compiler.components.<name>)]";
    }
    const std::string name = v.substr(std::string(kGrantPrefix).size());
    if (name.empty() || name.find('.') != std::string::npos) {
        return "'" + v + "' is not a component reference: a component name has "
               "no dot in it (ADR 0012)";
    }
    if (!compilerapi::findComponent(name)) {
        return "The compiler has no component '" + name + "'";
    }
    return std::nullopt;
}

// The event set of docs/compiler-api.md §3.2. Answered from the events module,
// which owns the list: collection reads the same answer when it decides what to
// gather, so the two cannot disagree about what an event is. Collection and
// arming live in EventRegistry.cpp; this only decides what the name may be.
static bool isKnownEvent(const std::string& name) {
    return events::isKnownEvent(name);
}

// Every attribute the corpus or the standard library writes that this pass does
// not otherwise own. Each is read elsewhere -- the backend, the module loader,
// or another analyzer pass (`global`) -- so this pass accepts it silently and
// leaves its shape to its reader. Anything outside this set plus the wave-4
// names above is refused as unknown.
static bool isReaderOwnedAttribute(const std::string& name) {
    static const std::unordered_set<std::string> owned = {
        "llvm_name", "stdimport", "global", "type", "uncastable", "RT",
        "overwrite", "implements", "class", "stderror", "slaveof", "future",
        "anytype", "typeinfo_class", "public", "private", "debug", "attribute",
    };
    return owned.count(name) != 0;
}

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

        // The path checks live in useGrantError, shared with visit(Attribute&):
        // a misspelled grant is reported here rather than left to say nothing until the
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
        if (auto msg = useGrantError(v)) {
            error(node, *msg);
            continue;
        }
        comps.push_back(v.substr(std::string(kGrantPrefix).size()));
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
        // The table spells an effect's answer the way a declaration writes it
        // (`<noret>`), but the lexer folds that spelling to void before any
        // TypeNode exists, so no scope ever binds the name. Translated here
        // rather than defined as one more type: without it every `noret`
        // member resolves to null and its whole call — arity and argument
        // types — is walked and returned unchecked, in silence.
        if (spelling == "noret") return currentScope->resolveType("void");
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
        if (!requireComponentGrant(node, member)) {
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

// Wave-4 slice 1b: attributes are checkable. One attribute, validated by name
// and shape. The wave-4 names (`use`, `export`, `on`, `provides`, `protocol`)
// are checked here; everything the corpus writes that another pass owns is
// accepted silently, leaving its shape to its reader; anything else names the
// attribute it is.
void SemanticAnalyzer::visit(Attribute& node) {
    const std::string& name = node.name;

    if (name == "use") {
        if (auto msg = useGrantError(node.value_str)) error(node, *msg);
        return;
    }

    if (name == "export") {
        // A flag in every corpus site; an argument names nothing.
        if (!node.is_flag) error(node, "Attribute 'export' takes no argument");
        return;
    }

    if (name == "on") {
        if (node.is_flag || node.value_str.empty()) {
            error(node, "Attribute 'on' needs an event name: #[on(<event>)]");
            return;
        }
        // Shape only: collection and arming are a later slice, so a known event
        // is accepted and nothing is collected.
        if (!isKnownEvent(node.value_str))
            error(node, "Unknown event '" + node.value_str + "'");
        return;
    }

    if (name == "provides" || name == "protocol") {
        if (node.is_flag || node.value_str.empty()) {
            error(node, "Attribute '" + name + "' needs a slot: #[" + name + "(<slot>)]");
            return;
        }
        // `protocol` is wave 5: shape only here (a known slot). The bearer
        // contract -- `@special` only, one subject, one answer -- is collected
        // at the declaration by collectProtocol; anything elsewhere is refused
        // by validateAttributes before it ever reaches this accept. Mirrors
        // `provides` above, which splits the same way.
        if (name == "protocol") {
            if (!compilerapi::findProtocolSlot(node.value_str))
                error(node, "unknown protocol slot '" + node.value_str +
                            "' (expected move_or_copy, deallocate, lifetime, or destructor)");
            return;
        }
        // `provides`: shape only here (a known slot). The bearer contract --
        // `@special` only, one subject, one answer -- is collected at the
        // declaration by collectProvider; anything elsewhere is refused by
        // validateAttributes before it ever reaches this accept.
        if (!compilerapi::findProviderSlot(node.value_str))
            error(node, "Unknown provider slot '" + node.value_str + "'");
        return;
    }

    // Wave-5 slice 4: `#[slaveof(...)]` is a real rule, so its shape is
    // checked where every other attribute's is. The referent itself is
    // resolved at the variable declaration, which is the only bearer with a
    // scope to resolve it in; anything elsewhere keeps its reader-owned
    // silence below.
    if (name == "slaveof") {
        if (node.is_flag || node.value_str.empty()) {
            error(node, "Attribute 'slaveof' needs a variable: #[slaveof(<variable>)] or #[slaveof($Fin)]");
            return;
        }
        if (node.value_str[0] == '$' && node.value_str != "$Fin") {
            error(node, "Unknown lifetime '" + node.value_str +
                        "' in #[slaveof(...)]: only $Fin pins to program exit");
            return;
        }
        if (node.value_str.find('.') != std::string::npos) {
            error(node, "Attribute 'slaveof' names a variable: '" + node.value_str + "' is not one");
            return;
        }
        return;
    }

    if (isReaderOwnedAttribute(name)) return;

    error(node, "Unknown attribute '" + name + "'");
}

void SemanticAnalyzer::validateAttributes(
        const std::vector<std::unique_ptr<Attribute>>& attrs, bool withUse,
        bool isSpecialBearer) {
    for (const auto& a : attrs) {
        if (!a) continue;
        if (!withUse && a->name == "use") continue;
        if (a->name == "provides") {
            // A provider answers a question the compiler asks, and only a
            // `@special` function runs inside the compiler -- so the attribute
            // means nothing anywhere else, and meaning nothing loudly is what
            // keeps a misplaced one from reading as an accepted one. On a
            // `@special` it is collected whole by collectProvider (contract,
            // exclusivity, body shape), never walked here.
            if (isSpecialBearer) continue;
            error(*a, "Only a `@special` function may provide a compiler slot");
            continue;
        }
        if (a->name == "protocol") {
            // A protocol replaces a compiler operation, and only a `@special`
            // function runs inside the compiler -- so the attribute means
            // nothing anywhere else, for the same reason as `provides` above.
            // On a `@special` it is collected whole by collectProtocol
            // (contract, exclusivity), never walked here.
            if (isSpecialBearer) continue;
            error(*a, "Only a `@special` function may claim a compiler protocol slot");
            continue;
        }
        a->accept(*this);
    }
}

// Wave-4 `@implements` lowering, stage A: the fold behind the compiler-API
// op. One hook behind both spellings -- `@implements(S, I)` and
// `compiler.types.implements(S, I)` -- the `symbols.defined` precedent (one
// predicate behind `@defined` and `compiler.symbols.defined`, answered by
// answerDefined in ComptimeInterp.cpp): the hook answers from the analyzer's
// own scopes through `StructType::implements`, the normative predicate, so a
// guard folds the same pair the query later lowers. Anything but a concrete
// struct/interface pair answers nullopt and stays a gap, never a guessed
// false: a `$struct`/`$interface` parameter is a runtime tid word (Stage C
// owns it), `any`/nullable/union are not StructTypes at all, an enum is a
// StructType but not a struct (is_enum), and an open generic template is not
// an answer about any instantiation.
void SemanticAnalyzer::installComptimeHooks(comptime::Interpreter& interp) {
    interp.setImplementsHook([this](const std::string& sName,
                                    const std::string& iName) -> std::optional<bool> {
        auto sType = currentScope->resolveType(sName);
        auto iType = currentScope->resolveType(iName);
        auto sStruct = std::dynamic_pointer_cast<StructType>(sType);
        auto iStruct = std::dynamic_pointer_cast<StructType>(iType);
        if (!sStruct || sStruct->is_interface || sStruct->is_enum ||
            !sStruct->generic_args.empty())
            return std::nullopt;
        if (!iStruct || !iStruct->is_interface || iStruct->is_enum ||
            !iStruct->generic_args.empty())
            return std::nullopt;
        return sStruct->implements(iStruct.get());
    });
}
// --- the provider mechanism (§3.9, ADR 0014) ---------------------------------
//
// A provider is a `@special` the compiler calls once per subject, whose
// returned value the compiler stores and emits, declared `#[provides(<slot>)]`.
// Exclusive per slot, memoised per subject: a provider is a pure function of
// its subject, and the C++-first body rule below makes that true by
// construction rather than by checking -- the only expressible body has no
// state, no second parameter and no other call.
//
// What the compiler does with a collected provider: the analyzer validates the
// contract here (slot, signature, exclusivity, body shape); codegen finds the
// one declaration, calls it once per lowered struct subject by computing the
// projection its body names from the finalised layout, and emits one metadata
// global per subject. General `@special` execution -- running an arbitrary
// straight-line body -- waits on the comptime interpreter (ADR 0006); until
// then the body shape is the call.

namespace {

bool isMetaSpelling(const std::shared_ptr<Type>& type, const std::string& name) {
    auto* prim = type ? type->as<PrimitiveType>() : nullptr;
    return prim && prim->name == name;
}

// Whether `body` is the one answer a C++-first provider may give: a single
// `return compiler.layout.pointer_map_quote(<subject>)`, where `<subject>` is
// the provider's own parameter. Syntactic, deliberately: the interpretability
// line is held (ADR 0006 -- no loops, no arithmetic, no second call), and a
// body with any other shape is refused naming the line rather than evaluated
// into something the compiler did not promise to run.
bool isTypeMetadataAnswer(const Block& body, const std::string& subjectName) {
    if (body.statements.size() != 1) return false;
    const auto* ret = dynamic_cast<const ReturnStatement*>(body.statements[0].get());
    if (!ret || !ret->value) return false;
    const auto* call = dynamic_cast<const MethodCall*>(ret->value.get());
    if (!call || call->method_name != "pointer_map_quote") return false;
    if (!call->generic_args.empty() || call->args.size() != 1) return false;
    const auto* inner = dynamic_cast<const MemberAccess*>(call->object.get());
    if (!inner || inner->member != "layout") return false;
    const auto* root = dynamic_cast<const Identifier*>(inner->object.get());
    if (!root || root->name != "compiler") return false;
    const auto* arg = dynamic_cast<const Identifier*>(call->args[0].get());
    return arg && arg->name == subjectName;
}

}  // namespace

// --- I-G1/I-G3: the threaded scans share one arm rule ------------------------
//
// An `if` condition to a known bool in `env`: helper calls in the condition
// run under the interpreter's depth and acyclicity guards. False when unknown
// or non-bool — the caller keeps its named refusal.
bool knownBranch(const Expression& condition, comptime::Interpreter& interp,
                 comptime::Env& env, bool* take) {
    if (!take) return false;
    comptime::ExprResult cond = interp.evaluateExpression(condition, env);
    if (cond.status != comptime::ExprStatus::Ok) return false;
    if (cond.value.kind != comptime::ValueKind::Bool) return false;
    if (cond.value.text == "true") {
        *take = true;
        return true;
    }
    if (cond.value.text == "false") {
        *take = false;
        return true;
    }
    return false;
}

// One `if`/`else-if` chain to its taken arm (I-G3): each condition evaluates
// through knownBranch above. `*refused` is set when a condition is unknown
// or non-bool, a block is missing, or an `else` holds neither a block nor an
// `if` — evaluateIf's traversal, restated for a structural scan, because the
// projection/quote answer is shape-checked here, never executed. A null arm
// with `*refused` clear falls through: the statements after the `if` run.
const Block* takenBranchArm(const IfStatement& node, comptime::Interpreter& interp,
                            comptime::Env& env, bool* refused) {
    const IfStatement* cur = &node;
    while (true) {
        if (!cur->condition || !cur->then_block) {
            *refused = true;
            return nullptr;
        }
        bool take = false;
        if (!knownBranch(*cur->condition, interp, env, &take)) {
            *refused = true;
            return nullptr;
        }
        if (take) return cur->then_block.get();
        if (!cur->else_stmt) return nullptr;
        if (const auto* elseBlock = dynamic_cast<const Block*>(cur->else_stmt.get()))
            return elseBlock;
        if (const auto* elseIf = dynamic_cast<const IfStatement*>(cur->else_stmt.get())) {
            cur = elseIf;
            continue;
        }
        *refused = true;
        return nullptr;
    }
}

// The scan outcome for one statement list: FallsThrough reached the end
// without answering, Answered found the slot's answer, Refused met a shape
// outside the line.
enum class ThreadedScan { FallsThrough, Answered, Refused };

bool isProjectionReturn(const ReturnStatement& ret, const std::string& subjectName,
                        comptime::Interpreter& interp, comptime::Env& env) {
    if (!ret.value) return false;
    const auto* call = dynamic_cast<const MethodCall*>(ret.value.get());
    if (!call || call->method_name != "pointer_map_quote") return false;
    if (!call->generic_args.empty() || call->args.size() != 1) return false;
    const auto* inner = dynamic_cast<const MemberAccess*>(call->object.get());
    if (!inner || inner->member != "layout") return false;
    const auto* root = dynamic_cast<const Identifier*>(inner->object.get());
    if (!root || root->name != "compiler") return false;
    comptime::ExprResult arg = interp.evaluateExpression(*call->args[0], env);
    return arg.status == comptime::ExprStatus::Ok &&
           arg.value.kind == comptime::ValueKind::Opaque && arg.value.text == subjectName;
}

ThreadedScan scanThreadedAnswer(const Block& body, const std::string& subjectName,
                                comptime::Interpreter& interp, comptime::Env& env) {
    for (std::size_t i = 0; i < body.statements.size(); ++i) {
        const bool last = (i + 1 == body.statements.size());
        const Statement* stmt = body.statements[i].get();
        if (const auto* decl = dynamic_cast<const VariableDeclaration*>(stmt)) {
            if (last) return ThreadedScan::Refused;
            std::string detail;
            if (interp.evaluateDeclaration(*decl, env, &detail) != comptime::ExprStatus::Ok)
                return ThreadedScan::Refused;
            continue;
        }
        if (const auto* ret = dynamic_cast<const ReturnStatement*>(stmt)) {
            if (!last) return ThreadedScan::Refused;
            return isProjectionReturn(*ret, subjectName, interp, env) ? ThreadedScan::Answered
                                                                      : ThreadedScan::Refused;
        }
        if (const auto* ifStmt = dynamic_cast<const IfStatement*>(stmt)) {
            bool refused = false;
            const Block* arm = takenBranchArm(*ifStmt, interp, env, &refused);
            if (refused) return ThreadedScan::Refused;
            if (!arm) continue;
            const ThreadedScan sub = scanThreadedAnswer(*arm, subjectName, interp, env);
            if (sub != ThreadedScan::FallsThrough) return sub;
            continue;
        }
        if (const auto* exprStmt = dynamic_cast<const ExpressionStatement*>(stmt)) {
            if (last || !exprStmt->expr) return ThreadedScan::Refused;
            const auto* assign = dynamic_cast<const BinaryOp*>(exprStmt->expr.get());
            if (assign && assign->op == ASTTokenKind::EQUAL &&
                dynamic_cast<const Identifier*>(assign->left.get())) {
                std::string detail;
                if (interp.evaluateAssign(*assign, env, &detail) != comptime::ExprStatus::Ok)
                    return ThreadedScan::Refused;
                continue;
            }
            if (interp.evaluateExpression(*exprStmt->expr, env).status !=
                comptime::ExprStatus::Ok)
                return ThreadedScan::Refused;
            continue;
        }
        return ThreadedScan::Refused;
    }
    return ThreadedScan::FallsThrough;
}

// Whether `body` answers the slot by threading the subject to the
// projection: straight-line lets, rebinds and bare calls (literals + lets +
// calls, parameters bound) plus `if` over a comptime-known bool (I-G3: the
// taken arm must thread to the projection), ending in
// `return compiler.layout.pointer_map_quote(<subject>)`, where the argument
// evaluates to the subject parameter rather than merely spelling it. The
// single-return projection (isTypeMetadataAnswer) is the base case; `let t =
// s; return ...(t);` and helper-threaded equivalents are the unlocked
// shapes. The projection call itself is never executed here -- codegen
// computes the map from the finalised layout -- so this validates the
// threading and the shape, and anything else answers false for the existing
// refusal below. Loops stay refused per ADR 0006, and every threaded step
// still runs under the interpreter's depth and acyclicity guards.
bool isThreadedTypeMetadataAnswer(const Block& body, const std::string& subjectName,
                                  const Program& program) {
    if (body.statements.empty()) return false;    comptime::Interpreter interp(program);
    comptime::Env env;
    env.bind(subjectName, comptime::Value::makeOpaque(subjectName));
    return scanThreadedAnswer(body, subjectName, interp, env) == ThreadedScan::Answered;
}

void SemanticAnalyzer::collectProvider(SpecialDeclaration& node) {
    std::vector<Attribute*> provides;
    for (const auto& a : node.attributes) {
        if (a && a->name == "provides") provides.push_back(a.get());
    }
    if (provides.empty()) return;

    if (provides.size() > 1) {
        error(node, "A provider claims exactly one slot: '@" + node.name +
                    "' provides '" + provides[0]->value_str + "' and '" +
                    provides[1]->value_str + "'");
        return;
    }
    Attribute& attr = *provides[0];
    if (attr.is_flag || attr.value_str.empty()) {
        error(attr, "Attribute 'provides' needs a slot: #[provides(<slot>)]");
        return;
    }
    const compilerapi::ProviderSlot* slot = compilerapi::findProviderSlot(attr.value_str);
    if (!slot) {
        error(attr, "Unknown provider slot '" + attr.value_str + "'");
        return;
    }

    // The contract, for `type_metadata`: one `$struct` subject in, one `quote`
    // out. Re-resolved here rather than carried from the parameter walk above,
    // because that walk's types are locals and this is where the contract is
    // checked. A null resolution was already reported resolving the parameter,
    // so it returns rather than reporting the same type twice.
    if (node.params.size() != 1) {
        error(node, "A provider for slot '" + slot->slot +
                    "' takes exactly one subject: '@" + node.name + "' takes " +
                    std::to_string(node.params.size()));
        return;
    }
    auto subject = resolveTypeFromAST(node.params[0]->type.get());
    if (!subject) return;
    if (!isMetaSpelling(subject, slot->subject)) {
        error(node, "A provider for slot '" + slot->slot + "' takes its subject as '<" +
                    slot->subject + ">'");
        return;
    }
    std::shared_ptr<Type> answer;
    if (node.return_type) answer = resolveTypeFromAST(node.return_type.get());
    if (!answer) return;
    if (!isMetaSpelling(answer, slot->result)) {
        error(node, "A provider for slot '" + slot->slot + "' returns '<" +
                    slot->result + ">'");
        return;
    }

    // Exclusive per slot: the second claimant names the first, and both sites
    // are in the message -- the first claim's line travels in text because a
    // diagnostic points at one node and there are two declarations to find.
    for (const auto& claim : providerClaims_) {
        if (claim.slot != slot->slot) continue;
        error(node, "Slot '" + slot->slot + "' already has a provider: '@" + claim.name +
                    "' provides it, so '@" + node.name +
                    "' cannot. A slot has exactly one provider (ADR 0014)");
        return;
    }

    // The body is what makes the provider pure by construction. Anything but
    // the projection return is refused with the line it breaks, never run.
    // The single-return projection is the base case; a straight-line body
    // threading the subject to it (lets, rebinds, helper calls, known-bool
    // branches, parameters bound) answers alike. `w5_program_` is the program
    // being walked, which is what helper lookup reads; null outside the walk
    // keeps the base case.
    const bool projection =
        node.body && (isTypeMetadataAnswer(*node.body, node.params[0]->name) ||
                      (w5_program_ && isThreadedTypeMetadataAnswer(*node.body,
                                                                   node.params[0]->name,
                                                                   *w5_program_)));
    if (!projection) {
        error(node, "A provider for slot '" + slot->slot + "' is a straight-line answer: "
                    "'return compiler.layout.pointer_map_quote(<subject>);' and nothing else "
                    "(the interpretability line is held: no loops, no arithmetic)");
        return;
    }

    providerClaims_.push_back({slot->slot, node.name});
}

// --- the protocol claim registry (wave-5 slice 0, ADR 0014) -----------------
//
// A protocol is a `@special` that *replaces* one compiler operation, declared
// `#[protocol(<slot>)]`. Exclusive per slot like a provider (two claimants
// name both), armed by default (collecting IS the claim). Slice 0 builds the
// registry only: a well-formed claimant is recorded here and refused later by
// reportSingleProtocolClaims (replacement is not lowered yet), so the default
// lowering stays byte-identical while any slot is unclaimed.
//
// Structure mirrors collectProvider above verbatim: one attribute, slot,
// signature, then the claim. Two deliberate differences: the subject type is
// unchecked (the four operations take different subjects and a uniform pin
// now would be a wrong contract for a later slice to unwind -- arity of one
// plus a `quote` answer is the whole of slice 0's signature), and the body is
// unchecked (nothing is lowered, so there is no projection to hold).

// --- wave-5 slice 2: the `destructor` generation subset ----------------------
//
// A `destructor` claimant supplies the cleanup for structs that declare no
// `~T()`. The evaluable subset is handler-eval's shape with threading: an
// empty body (no custom cleanup; composition still runs), one
// `return quote { ... };` whose quote codegen lowers once as a shared
// function, or a straight-line body (literals + lets + rebinds + calls with
// known-bool branches, the subject parameter bound) threading to such a
// quote. Anything else is refused at collection naming the gap rather than
// silently dropped in codegen. Null
// when the body is absent or empty (no generation); the inner quote block
// when the body threads to a quote return; null with `*valid` cleared when
// the shape is outside the subset. A null `program` (outside the program
// walk) holds only the base case: one literal quote return.
// The destructor/deallocate scan outcome for one statement list, mirroring
// ThreadedScan above: the answer here is the generation quote block.
enum class DtorScan { FallsThrough, Answered, Refused };

DtorScan scanDestructorGeneration(const Block& body, comptime::Interpreter& interp,
                                  comptime::Env& env, Block** generation) {
    for (std::size_t i = 0; i < body.statements.size(); ++i) {
        const bool last = (i + 1 == body.statements.size());
        const Statement* stmt = body.statements[i].get();
        if (const auto* decl = dynamic_cast<const VariableDeclaration*>(stmt)) {
            if (last) return DtorScan::Refused;
            std::string detail;
            if (interp.evaluateDeclaration(*decl, env, &detail) !=
                comptime::ExprStatus::Ok)
                return DtorScan::Refused;
            continue;
        }
        if (const auto* ret = dynamic_cast<const ReturnStatement*>(stmt)) {
            if (!last || !ret->value) return DtorScan::Refused;
            if (const auto* quote = dynamic_cast<const QuoteExpression*>(ret->value.get())) {
                if (quote->block) {
                    *generation = quote->block.get();
                    return DtorScan::Answered;
                }
                return DtorScan::Refused;
            }
            comptime::ExprResult threaded = interp.evaluateExpression(*ret->value, env);
            if (threaded.status == comptime::ExprStatus::Ok &&
                threaded.value.kind == comptime::ValueKind::Quote &&
                threaded.value.quote && threaded.value.quote->block) {
                *generation = threaded.value.quote->block.get();
                return DtorScan::Answered;
            }
            return DtorScan::Refused;
        }
        if (const auto* ifStmt = dynamic_cast<const IfStatement*>(stmt)) {
            bool refused = false;
            const Block* arm = takenBranchArm(*ifStmt, interp, env, &refused);
            if (refused) return DtorScan::Refused;
            if (!arm) continue;
            const DtorScan sub = scanDestructorGeneration(*arm, interp, env, generation);
            if (sub != DtorScan::FallsThrough) return sub;
            continue;
        }
        if (const auto* exprStmt = dynamic_cast<const ExpressionStatement*>(stmt)) {
            if (last || !exprStmt->expr) return DtorScan::Refused;
            const auto* assign = dynamic_cast<const BinaryOp*>(exprStmt->expr.get());
            if (assign && assign->op == ASTTokenKind::EQUAL &&
                dynamic_cast<const Identifier*>(assign->left.get())) {
                std::string detail;
                if (interp.evaluateAssign(*assign, env, &detail) !=
                    comptime::ExprStatus::Ok)
                    return DtorScan::Refused;
                continue;
            }
            if (interp.evaluateExpression(*exprStmt->expr, env).status !=
                comptime::ExprStatus::Ok)
                return DtorScan::Refused;
            continue;
        }
        return DtorScan::Refused;
    }
    return DtorScan::FallsThrough;
}

static Block* destructorGenerationBody(SpecialDeclaration& node, bool* valid,
                                       const Program* program) {
    *valid = true;
    if (!node.body || node.body->statements.empty()) return nullptr;
    // The base case, byte-identical: one literal quote return.
    if (node.body->statements.size() == 1) {
        if (const auto* ret =
                dynamic_cast<const ReturnStatement*>(node.body->statements[0].get())) {
            if (ret->value) {
                if (const auto* quote =
                        dynamic_cast<const QuoteExpression*>(ret->value.get())) {
                    if (quote->block) return quote->block.get();
                }
            }
        }
        if (!program) { *valid = false; return nullptr; }
    } else if (!program) { *valid = false; return nullptr; }
    // Straight-line threading to the generation quote: lets bind, rebinds
    // rebind (I-G1), bare calls run for their threading, `if` over a
    // comptime-known bool scans its taken arm (I-G3, takenBranchArm above),
    // and the subject parameter is bound (the unlock), so `let t <$struct> =
    // s;` and helper-threaded equivalents reach the same quote. The final
    // statement must return the quote; the generation itself stays uniform
    // (it names no `self`, checked below). Loops stay refused per ADR 0006.
    comptime::Interpreter interp(*program);
    comptime::Env env;
    if (!node.params.empty())
        env.bind(node.params[0]->name,
                 comptime::Value::makeOpaque(node.params[0]->name));
    Block* generation = nullptr;
    if (scanDestructorGeneration(*node.body, interp, env, &generation) !=
        DtorScan::Answered) {
        *valid = false;
        return nullptr;
    }
    return generation;
}

// A subject-relative name inside destructor generation: the generation
// lowers once, as a shared function with no receiver, for every claimed type
// alike, so `self` (and any bare field it would carry) has nothing to bind
// to. Any spelling refuses -- even a would-be shadowing local, which would
// compile but mean something its reader did not write.
class SelfNamingWalker : public StructuralWalk {
public:
    bool found = false;
    bool enter(ASTNode& node) override {
        if (node.kind() == NodeKind::Identifier &&
            static_cast<Identifier&>(node).name == "self") {
            found = true;
            return false;
        }
        return true;
    }
};

void SemanticAnalyzer::collectProtocol(SpecialDeclaration& node) {
    std::vector<Attribute*> protocols;
    for (const auto& a : node.attributes) {
        if (a && a->name == "protocol") protocols.push_back(a.get());
    }
    if (protocols.empty()) return;

    if (protocols.size() > 1) {
        error(node, "A protocol claims exactly one slot: '@" + node.name +
                    "' claims '" + protocols[0]->value_str + "' and '" +
                    protocols[1]->value_str + "'");
        return;
    }
    Attribute& attr = *protocols[0];
    if (attr.is_flag || attr.value_str.empty()) {
        error(attr, "Attribute 'protocol' needs a slot: #[protocol(<slot>)]");
        return;
    }
    const compilerapi::ProtocolSlot* slot = compilerapi::findProtocolSlot(attr.value_str);
    if (!slot) {
        error(attr, "unknown protocol slot '" + attr.value_str +
                    "' (expected move_or_copy, deallocate, lifetime, or destructor)");
        return;
    }

    // The contract, slice 0: one subject in, one `quote` out (the replacement
    // node per §3.7's table: a protocol returns a quote to substitute).
    // Re-resolved here rather than carried from the parameter walk above, for
    // collectProvider's reason: that walk's types are locals and this is where
    // the contract is checked.
    if (node.params.size() != 1) {
        error(node, "A protocol for slot '" + slot->slot +
                    "' takes exactly one subject: '@" + node.name + "' takes " +
                    std::to_string(node.params.size()));
        return;
    }
    std::shared_ptr<Type> answer;
    if (node.return_type) answer = resolveTypeFromAST(node.return_type.get());
    if (!answer) return;
    if (!isMetaSpelling(answer, "quote")) {
        error(node, "A protocol for slot '" + slot->slot + "' returns '<quote>'");
        return;
    }

    // Exclusive per slot: the second claimant names the first, and both sites
    // are in the message -- the first claim's line travels in text because a
    // diagnostic points at one node (the second claim, below) and there are
    // two declarations to find. Mirrors the provider exclusivity above, which
    // names both providers the same way.
    for (const auto& claim : protocolClaims_) {
        if (claim.slot != slot->slot) continue;
        error(node, "protocol slot '" + slot->slot + "' is already claimed: '@" +
                    claim.name + "' (line " + std::to_string(claim.line) +
                    ") claimed it, so '@" + node.name + "' (line " +
                    std::to_string(node.loc.begin.line) + ") cannot. " +
                    "A slot has exactly one claimant (ADR 0014)");
        return;
    }

    // Slice 1 (wave-5 slice 1): a `move_or_copy` claim gates moved-from
    // cleanup skipping in codegen, but the replacement body is not executed
    // there -- so a non-empty body is refused naming the claimant rather
    // than silently dropped (the backend never drops runtime code). Declare
    // the claim with an empty body. Other slots keep slice 0's shape: their
    // refusal below already covers any body.
    if (slot->slot == "move_or_copy" && node.body && !node.body->statements.empty()) {
        error(node, "A protocol for slot 'move_or_copy' is declared with an empty body: '@" +
                    node.name + "' has " +
                    std::to_string(node.body->statements.size()) +
                    " statement(s), and a replacement body is not run in this slice");
        return;
    }

    // Slice 2 (wave-5 slice 2): a `destructor` claim supplies generation --
    // the quote its body returns becomes the cleanup for structs that declare
    // no `~T()` (an explicit `~T()` keeps precedence in codegen, and
    // fields-then-bases composition still runs after either -- the
    // composition-preserving default, ADR 0016's body-after-fields rule,
    // which the claim shape gives no channel to opt out of). The body holds
    // to the threaded subset above: straight-line lets and calls with the
    // subject bound, so the one generation still serves every claimed type
    // and names no `self`. `w5_program_` is the program being walked, which
    // is what helper lookup reads; null outside the walk holds the base
    // case.
    if (slot->slot == "destructor") {
        bool valid = true;
        Block* generation = destructorGenerationBody(node, &valid, w5_program_);
        if (!valid) {
            error(node, "A protocol for slot 'destructor' supplies generation as one "
                        "'return quote { ... };' (or an empty body): '@" +
                        node.name + "' is not in that shape, and a replacement body "
                        "outside it is not run in this slice "
                        "(the comptime interpreter gap: no @special execution yet, "
                        "so the generation must be a quote literal)");
            return;
        }
        if (generation) {
            SelfNamingWalker self;
            self.walk(generation);
            if (self.found) {
                error(node, "A protocol for slot 'destructor' generates one cleanup for "
                            "every claimed type alike, so its quote names no subject: '@" +
                            node.name + "' uses 'self', which has no receiver to bind to "
                            "(the generation lowers as a shared function with no receiver)");
                return;
            }
        }
    }

    // Slice 3 (wave-5 slice 3): a `deallocate` claim supplies generation --
    // the quote its body returns substitutes the deallocation call `delete`
    // emits. Slice 2's threaded shape carried over verbatim: straight-line
    // lets and calls with the subject bound, so the one generation serves
    // every claimed subject and names no `self`. The destructor still runs
    // first in codegen (the claimant replaces ONLY the `free`), so this slot
    // never touches it.
    if (slot->slot == "deallocate") {
        bool valid = true;
        Block* generation = destructorGenerationBody(node, &valid, w5_program_);
        if (!valid) {
            error(node, "A protocol for slot 'deallocate' supplies generation as one "
                        "'return quote { ... };' (or an empty body): '@" +
                        node.name + "' is not in that shape, and a replacement body "
                        "outside it is not run in this slice "
                        "(the comptime interpreter gap: no @special execution yet, "
                        "so the generation must be a quote literal)");
            return;
        }
        if (generation) {
            SelfNamingWalker self;
            self.walk(generation);
            if (self.found) {
                error(node, "A protocol for slot 'deallocate' generates one deallocation for "
                            "every claimed subject alike, so its quote names no subject: '@" +
                            node.name + "' uses 'self', which has no receiver to bind to "
                            "(the generation lowers as a shared function with no receiver)");
                return;
            }
        }
    }

    protocolClaims_.push_back({slot->slot, node.name, node.loc.begin.line, &node});
}

// Slice 0's replacement refusal: a lone well-formed claimant is recognized
// and recorded, but the operation is still lowered by the default --
// reporting success would claim the library does something it does not.
// Deferred to the end of the program so a second claimant reports
// exclusivity instead of this; slots with zero claimants report nothing.
//
// Slice 1 exempts `move_or_copy`: its claim lowers (it gates moved-from
// cleanup skipping in codegen, ADR 0030's moved-from case), so refusing here
// would keep every claimant program from compiling. Slice 2 exempts
// `destructor` the same way: its claim lowers (the claimant supplies the
// generation codegen runs for structs with no `~T()`). Slice 3 exempts
// `deallocate` the same way: its claim lowers (the claimant substitutes the
// deallocation call `delete` emits). The last slot still refuses until its
// slice lands.
void SemanticAnalyzer::reportSingleProtocolClaims() {
    for (const auto& claim : protocolClaims_) {
        if (claim.slot == "move_or_copy" || claim.slot == "destructor" ||
            claim.slot == "deallocate")
            continue;
        size_t holders = 0;
        for (const auto& other : protocolClaims_) {
            if (other.slot == claim.slot) ++holders;
        }
        if (holders != 1) continue;
        error(*claim.node, "protocol slot '" + claim.slot +
                           "' is recognized, claimant recorded, replacement not lowered yet: '@" +
                           claim.name + "' (line " + std::to_string(claim.line) + ")");
    }
}

// --- Round 3, Q11: branching on a host read ---------------------------------
//
// `compiler.system.get_total_memory` / `get_available_memory` /
// `get_memorycard_model` read the machine doing the compiling, not the target.
// A compile-time branch on one emits a different program on a different build
// machine, breaking the reproducibility ADR 0010 exists to guarantee. Reading
// is legal (stdlib/memory.fin's `mem_info` only formats and returns);
// branching is a warning naming the host operation it came from.
//
// What this catches is syntactic and deliberately narrow: a host read sitting
// in a branch condition, or a `let` in the same body initialised from one and
// then branched on. Taint through an `@special` call -- `@special limit() {
// return compiler.system.get_...; }` branched on by its caller -- is caught
// below by the interpreter's value model carrying taint alongside every value
// (ADR 0017's stated consequence). Target facts (`pointer_size`,
// `target_triple`) are not host reads and never warn, however they are
// branched on.

namespace {

bool isHostReadOp(const std::string& method) {
    return method == "get_total_memory" || method == "get_available_memory" ||
           method == "get_memorycard_model";
}

// Whether `expr` is `compiler.system.get_*(...)`: the object chain is exactly
// `compiler` then `system`, the method one of the three host reads above.
bool asHostRead(const Expression& expr, std::string* op) {
    const auto* call = dynamic_cast<const MethodCall*>(&expr);
    if (!call || !isHostReadOp(call->method_name)) return false;
    const auto* sys = dynamic_cast<const MemberAccess*>(call->object.get());
    if (!sys || sys->member != "system") return false;
    const auto* root = dynamic_cast<const Identifier*>(sys->object.get());
    if (!root || root->name != "compiler") return false;
    if (op) *op = "compiler.system." + call->method_name;
    return true;
}

// The first host read in a subtree, by source order. A walker rather than a
// hand-rolled recursion so a new expression form is visited rather than
// silently missed (StructuralWalk throws on unregistered nodes).
class HostReadFinder : public StructuralWalk {
public:
    std::string op;
    bool enter(ASTNode& node) override {
        if (!op.empty()) return false;
        // Quote and lambda bodies are runtime code, not compile-time reads
        // (see HostBranchWalk): a host spelling inside one is injected or
        // deferred, never evaluated while compiling.
        if (dynamic_cast<QuoteExpression*>(&node)) return false;
        if (dynamic_cast<LambdaExpression*>(&node)) return false;
        if (auto* e = dynamic_cast<Expression*>(&node)) {
            std::string found;
            if (asHostRead(*e, &found)) {
                op = found;
                return false;
            }
        }
        return true;
    }
};

bool subtreeReadsHost(ASTNode* root, std::string* op) {
    if (!root) return false;
    HostReadFinder finder;
    finder.walk(root);
    if (op) *op = finder.op;
    return !finder.op.empty();
}

// Whether `name` is mentioned in a subtree (a tainted binding flowing into a
// condition). Stops at the first mention; nested bodies are walked the same as
// straight-line code, which is the documented approximation.
class NameMentionFinder : public StructuralWalk {
public:
    explicit NameMentionFinder(std::string name) : name_(std::move(name)) {}
    bool found = false;
    bool enter(ASTNode& node) override {
        if (found) return false;
        if (dynamic_cast<QuoteExpression*>(&node)) return false;
        if (dynamic_cast<LambdaExpression*>(&node)) return false;
        if (auto* id = dynamic_cast<Identifier*>(&node)) {
            if (id->name == name_) {
                found = true;
                return false;
            }
        }
        return true;
    }

private:
    std::string name_;
};

bool subtreeMentions(ASTNode* root, const std::string& name) {
    if (!root || name.empty()) return false;
    NameMentionFinder finder(name);
    finder.walk(root);
    return finder.found;
}

class HostBranchWalk : public StructuralWalk {
public:
    using WarnReporter = std::function<void(ASTNode&, const std::string&)>;
    explicit HostBranchWalk(WarnReporter warn) : warn_(std::move(warn)) {}

    bool enter(ASTNode& node) override {
        // A quote body is data, not a compile-time branch: `return quote {
        // if (compiler.system.get_...(...) == 1) {...} }` injects a *runtime*
        // branch into the program, which decides nothing about what gets
        // compiled. A lambda body is runtime code for the same reason. Neither
        // subtree is walked.
        if (dynamic_cast<QuoteExpression*>(&node)) return false;
        if (dynamic_cast<LambdaExpression*>(&node)) return false;
        // A binding initialised from a host read taints its name for the rest
        // of the body. Ordered and single-pass: a `let` after its use does not
        // taint it, which matches how straight-line `@special` bodies read.
        if (auto* decl = dynamic_cast<VariableDeclaration*>(&node)) {
            std::string op;
            if (decl->initializer && subtreeReadsHost(decl->initializer.get(), &op))
                tainted_[decl->name] = op;
            return true;
        }
        // Every branch point the grammar gives a `@special` body: the four
        // loop/if conditions, the foreach source, and the ternary condition
        // (which the interpretability line keeps as an expression).
        Expression* condition = nullptr;
        if (auto* s = dynamic_cast<IfStatement*>(&node)) condition = s->condition.get();
        else if (auto* s = dynamic_cast<WhileLoop*>(&node)) condition = s->condition.get();
        else if (auto* s = dynamic_cast<ForLoop*>(&node)) condition = s->condition.get();
        else if (auto* s = dynamic_cast<ForeachLoop*>(&node)) condition = s->iterable.get();
        else if (auto* e = dynamic_cast<TernaryOp*>(&node)) condition = e->condition.get();
        if (!condition) return true;
        std::string op;
        if (subtreeReadsHost(condition, &op)) {
            warn(*condition, op);
            return true;
        }
        for (const auto& [name, source] : tainted_) {
            if (subtreeMentions(condition, name)) {
                warn(*condition, source);
                break;
            }
        }
        return true;
    }

private:
    void warn(ASTNode& at, const std::string& op) {
        warn_(at, "branching on '" + op +
                      "' makes the compiled program depend on the machine "
                      "that builds it (docs/compiler-api.md Q11: reading the "
                      "host is legal, branching on it is warned; ADR 0017)");
    }

    WarnReporter warn_;
    std::unordered_map<std::string, std::string> tainted_;
};

// Call-through taint (ADR 0017): a branch condition evaluated by the comptime
// interpreter that folds host-tainted -- through helper calls, lets, and ops
// -- warns naming the originating read. Syntactic cases are skipped here
// (the walk above already warned them). Gaps never warn: only proven taint
// does, so pure-comptime branches stay silent. Single-pass and ordered like
// the syntactic walk: a `let` binds as it is visited, quotes and lambdas are
// runtime code and never entered.
class HostTaintWalk : public StructuralWalk {
public:
    using WarnReporter = std::function<void(ASTNode&, const std::string&)>;

    HostTaintWalk(comptime::Interpreter& interp, WarnReporter warn)
        : interp_(interp), warn_(std::move(warn)) {}

    bool enter(ASTNode& node) override {
        if (dynamic_cast<QuoteExpression*>(&node)) return false;
        if (dynamic_cast<LambdaExpression*>(&node)) return false;
        if (auto* decl = dynamic_cast<VariableDeclaration*>(&node)) {
            std::string op;
            if (decl->initializer && subtreeReadsHost(decl->initializer.get(), &op))
                synTainted_[decl->name] = op;
            if (decl->initializer) {
                comptime::ExprResult bound =
                    interp_.evaluateExpression(*decl->initializer, env_);
                if (bound.status == comptime::ExprStatus::Ok)
                    env_.bind(decl->name, std::move(bound.value));
            }
            return true;
        }
        Expression* condition = nullptr;
        if (auto* s = dynamic_cast<IfStatement*>(&node)) condition = s->condition.get();
        else if (auto* s = dynamic_cast<WhileLoop*>(&node)) condition = s->condition.get();
        else if (auto* s = dynamic_cast<ForLoop*>(&node)) condition = s->condition.get();
        else if (auto* s = dynamic_cast<ForeachLoop*>(&node)) condition = s->iterable.get();
        else if (auto* e = dynamic_cast<TernaryOp*>(&node)) condition = e->condition.get();
        if (!condition) return true;
        // Syntactic: the walk above owns these.
        std::string direct;
        if (subtreeReadsHost(condition, &direct)) return true;
        for (const auto& [name, source] : synTainted_) {
            if (subtreeMentions(condition, name)) return true;
        }
        comptime::ExprResult folded = interp_.evaluateExpression(*condition, env_);
        if (folded.status != comptime::ExprStatus::Ok) return true;
        if (!folded.value.host_tainted) return true;
        warn(*condition, folded.value.host_op);
        return true;
    }

private:
    void warn(ASTNode& at, const std::string& op) {
        warn_(at, "branching on '" + op +
                      "' makes the compiled program depend on the machine "
                      "that builds it (docs/compiler-api.md Q11: reading the "
                      "host is legal, branching on it is warned; ADR 0017)");
    }

    comptime::Interpreter& interp_;
    comptime::Env env_;
    WarnReporter warn_;
    std::unordered_map<std::string, std::string> synTainted_;
};

}  // namespace

void SemanticAnalyzer::warnOnHostBranch(SpecialDeclaration& node, bool hasSystemGrant) {
    // Without the grant the read itself is already an error, and a warning on
    // top would report the same line twice for one mistake.
    if (!hasSystemGrant || !node.body) return;
    auto report = [this](ASTNode& at, const std::string& msg) { warning(at, msg); };
    HostBranchWalk walk(report);
    walk.walk(node.body.get());
    // The interpreter needs the program for helper lookup; outside the walk
    // there is none, and the syntactic walk above is the whole check.
    if (!w5_program_) return;
    comptime::Interpreter interp(*w5_program_);
    HostTaintWalk tainted(interp, report);
    tainted.walk(node.body.get());
}

// --- hybrid layout members ---------------------------------------------------
//
// The owner hybrid rule: layout reads through EITHER the component call
// (`compiler.layout.size_of(t)`) or a member read on the value (`t.size`),
// and the member still requires the grant. Checked here, at the member-access
// site, through the same gate as the call -- one gate, two spellings, no
// drift. Only the argument-free scalar reads have a member spelling (`size`,
// `align`, `pointer_count`); everything taking an argument is component-call
// only, because a member read takes none.
bool SemanticAnalyzer::tryLayoutMember(MemberAccess& node,
                                       const std::shared_ptr<Type>& objType) {
    const auto* prim = objType ? objType->as<PrimitiveType>() : nullptr;
    if (!prim || (prim->name != "$type" && prim->name != "$struct")) return false;
    const char* op = nullptr;
    if (node.member == "size") op = "size_of";
    else if (node.member == "align") op = "align_of";
    else if (node.member == "pointer_count") op = "pointer_count";
    else return false;

    const compilerapi::Component* layout = compilerapi::findComponent("layout");
    const compilerapi::Member* member = layout ? compilerapi::findMember(*layout, op) : nullptr;
    if (!member) return false;  // the table owns the operation; without it the
                                // old "not a struct" diagnostic still fires below.
    if (!requireComponentGrant(node, "layout")) {
        lastExprType = nullptr;
        return true;
    }
    auto type = compilerApiMemberType(*member, nullptr);
    const auto* fn = type ? type->as<FunctionType>() : nullptr;
    lastExprType = fn ? fn->return_type : nullptr;
    return true;
}

} // namespace fin
