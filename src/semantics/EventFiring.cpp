#include "EventFiring.hpp"

#include <algorithm>
#include <map>

#include "../ast/CloneVisitor.hpp"
#include "../ast/NodeKind.hpp"
#include "../ast/StructuralWalk.hpp"
#include "../ast/decls/FunctionDecl.hpp"
#include "../ast/decls/Program.hpp"
#include "../ast/decls/StructDecl.hpp"
#include "../ast/decls/TypeDef.hpp"
#include "../ast/exprs/ArrayExpr.hpp"
#include "../ast/exprs/BinaryOp.hpp"
#include "../ast/exprs/FunctionCall.hpp"
#include "../ast/exprs/Identifier.hpp"
#include "../ast/exprs/Lambda.hpp"
#include "../ast/exprs/Literal.hpp"
#include "../ast/exprs/MiscExpr.hpp"
#include "../ast/exprs/StructureExpr.hpp"
#include "../ast/nodes/Parameter.hpp"
#include "../ast/stmts/ControlFlow.hpp"
#include "../ast/stmts/ErrorHandling.hpp"
#include "../ast/stmts/Statement.hpp"
#include "../ast/stmts/VariableDecl.hpp"
#include "../ast/types/TypeNode.hpp"
#include "CompilerApi.hpp"
#include "ComptimeInterp.hpp"

// Wave-4 step 17, W5 floor: the firing loop (see the header for the contract).
//
// The spelling rule below (arity, base-name types, quote return) mirrors
// checkW6HandlerPayloads in EventPayloads.cpp deliberately: one rule, two
// owners, each owning disjoint events. Unifying the two copies is booked as
// a follow-up with W6, not done here, because that file is W6's work area.
namespace fin::events {

const std::vector<EventPayload>& w5Payloads() {
    // docs/compiler-api.md §3.2, the W5 floor. `variable_scope_exit` (with
    // its `moved` analysis, including MovedMaybe) is the hardest single
    // event and lands in a later batch, not here.
    static const std::vector<EventPayload> table = {
        {"struct_layout_finalised", {{"s", "$struct"}}, "quote"},
        {"variable_declared",
         {{"name", "string"}, {"t", "$type"}, {"is_mutable", "bool"}},
         "quote"},
    };
    return table;
}

const EventPayload* findW5Payload(const std::string& event) {
    for (const auto& p : w5Payloads())
        if (p.event == event) return &p;
    return nullptr;
}

bool isW5Event(const std::string& event) { return findW5Payload(event) != nullptr; }

namespace {

// A written type spelling is the payload's when it names the same base type
// with no structure of its own: no pointer, array, generic argument or
// nullability. `const` is exempt: it binds the parameter, it does not change
// what arrives. (W6's rule, restated: see the file note above.)
bool spellsPayloadType(const TypeNode* written, const std::string& expected) {
    if (!written) return false;
    return written->name == expected && written->pointer_depth == 0 &&
           !written->is_array && written->generics.empty() && !written->is_nullable;
}

std::string writtenSignature(const SpecialDeclaration& decl) {
    std::string out = "(";
    for (std::size_t i = 0; i < decl.params.size(); ++i) {
        if (i != 0) out += ", ";
        const auto& param = decl.params[i];
        out += param->name + ": ";
        out += param->type ? param->type->name : "<untyped>";
    }
    out += ") <";
    out += decl.return_type ? decl.return_type->name : "void";
    out += ">";
    return out;
}

bool signatureMatches(const SpecialDeclaration& decl, const EventPayload& payload) {
    if (decl.params.size() != payload.params.size()) return false;
    for (std::size_t i = 0; i < decl.params.size(); ++i)
        if (!spellsPayloadType(decl.params[i]->type.get(), payload.params[i].type))
            return false;
    return spellsPayloadType(decl.return_type.get(), payload.returns);
}

SpecialDeclaration* findSpecial(Program& program, const std::string& name) {
    for (auto& stmt : program.statements) {
        auto* special = dynamic_cast<SpecialDeclaration*>(stmt.get());
        if (special && special->name == name) return special;
    }
    return nullptr;
}

// Whether `expr` is a call shape the line may hold. The quote return is
// verified at fire time (returnsQuote); an unverifiable one is the
// interpreter gap, not a breach.
bool isLineCall(const Expression& expr) {
    return dynamic_cast<const FunctionCall*>(&expr) ||
           dynamic_cast<const MethodCall*>(&expr) ||
           dynamic_cast<const StaticMethodCall*>(&expr) ||
           dynamic_cast<const MacroCall*>(&expr) ||
           dynamic_cast<const MacroInvocation*>(&expr);
}

// The interpretability line as a walker: the first loop form found, by its
// source spelling. `if`/`else` evaluates (I-G3: the taken arm over a
// comptime-known bool) and is never a breach — only loops are. Same loop
// forms W6 holds, so the two diagnostics read alike.
class FlowWalker : public StructuralWalk {
public:
    std::string found;
    bool enter(ASTNode& node) override {
        switch (node.kind()) {
            case NodeKind::WhileLoop: found = "while"; return false;
            case NodeKind::ForLoop: found = "for"; return false;
            case NodeKind::ForeachLoop: found = "foreach"; return false;
            default: return true;
        }
    }
};

// The form breach in one top-level body statement, or "" when the statement
// is one of the seven forms (plus `blame`/`return`, which §3.8 and every
// handler need): lets, bare calls, index stores, plain `name = value`
// rebinds (I-G1), and `if` over a comptime-known bool (I-G3, the taken arm
// decides at fire time). A call's quote return is NOT checked here: that is
// evaluation's gap, and refusing it here would convict an unarmed handler
// for what only firing could know.
std::string formBreach(const Statement& stmt) {
    if (const auto* decl = dynamic_cast<const VariableDeclaration*>(&stmt)) {
        if (!decl->is_mutable && !decl->initializer)
            return "declares '" + decl->name + "' as a 'const' with no initialiser";
        return "";
    }
    if (dynamic_cast<const ReturnStatement*>(&stmt)) return "";
    if (dynamic_cast<const BlameStatement*>(&stmt)) return "";
    if (dynamic_cast<const IfStatement*>(&stmt)) return "";
    if (const auto* exprStmt = dynamic_cast<const ExpressionStatement*>(&stmt)) {
        const Expression* expr = exprStmt->expr.get();
        if (const auto* assign = dynamic_cast<const BinaryOp*>(expr)) {
            if (assign->op == ASTTokenKind::EQUAL &&
                (dynamic_cast<const ArrayAccess*>(assign->left.get()) ||
                 dynamic_cast<const Identifier*>(assign->left.get())))
                return "";
            return "uses '" + std::string(nodeKindName(expr->kind())) + "'";
        }
        if (expr && isLineCall(*expr)) return "";
        return "uses '" + std::string(expr ? nodeKindName(expr->kind()) : "Unknown") + "'";
    }
    return "uses '" + std::string(nodeKindName(stmt.kind())) + "'";
}

std::string lineBreach(const SpecialDeclaration& decl) {
    if (decl.body) {
        FlowWalker flow;
        flow.walk(decl.body.get());
        if (!flow.found.empty()) return "uses control flow ('" + flow.found + "')";
        for (const auto& stmt : decl.body->statements) {
            const std::string breach = formBreach(*stmt);
            if (!breach.empty()) return breach;
        }
    }
    return "";
}

std::string pointDetail(const W5FirePoint& point) {
    if (point.event == "struct_layout_finalised") return point.structName;
    return point.varName + ":" + point.varType;
}

} // namespace

std::set<std::string> checkW5HandlerPayloads(Program& program, const EventRegistry& registry,
                                             DiagReporter report) {
    std::set<std::string> refused;
    for (const auto& record : registry.all()) {
        const EventPayload* payload = findW5Payload(record.event);
        if (!payload) continue;
        SpecialDeclaration* decl = findSpecial(program, record.handler);
        // Another module's: its own analyzer checks it. Silence here, or one
        // handler would be diagnosed once per importing module.
        if (!decl) continue;
        if (!signatureMatches(*decl, *payload)) {
            report(*decl, "Handler '" + record.handler + "' for event '" +
                              record.event + "' has the wrong payload: expected " +
                              expectedSignature(*payload) + ", wrote " +
                              writtenSignature(*decl));
            refused.insert(record.handler);
            continue;
        }
        const std::string breach = lineBreach(*decl);
        if (!breach.empty()) {
            report(*decl, "Handler '" + record.handler + "' for event '" +
                              record.event + "' " + breach +
                              ": handlers hold lets, calls, rebinds and known-bool branches, "
                              "and no other control flow "
                              "(the interpretability line)");
            refused.insert(record.handler);
        }
    }
    return refused;
}

namespace {

// One handler run's outcome: nothing to inject, a quote to splice, or a
// diagnostic already reported (abort and gap alike inject nothing).
struct EvalOutcome {
    std::vector<std::unique_ptr<Statement>> quote;
    bool diagnosed = false;
};

// A `compiler.diag.<op>(...)` call: the object is exactly `compiler.diag`
// and the method is the operation. Syntactic, like isCompilerCallReturning
// below: whether the operation exists is the declaration walk's answer, and
// firing executes only the three severities.
const MethodCall* asDiagCall(const Expression& expr, std::string* op) {
    const auto* call = dynamic_cast<const MethodCall*>(&expr);
    if (!call) return nullptr;
    const auto* obj = dynamic_cast<const MemberAccess*>(call->object.get());
    if (!obj || obj->member != "diag") return nullptr;
    const auto* root = dynamic_cast<const Identifier*>(obj->object.get());
    if (!root || root->name != "compiler") return nullptr;
    if (op) *op = call->method_name;
    return call;
}

// Whether `call` on `compiler.<path...>` names a member whose table result
// is `result`. Syntactic: `compiler.layout.pointer_map_quote(s)` is the
// chain [layout] + `pointer_map_quote`. Anything else is not verifiable here,
// which is an evaluation gap, never a line breach.
bool isCompilerCallReturning(const MethodCall& call, const std::string& result) {
    std::vector<std::string> path;
    const Expression* obj = call.object.get();
    while (const auto* mid = dynamic_cast<const MemberAccess*>(obj)) {
        path.push_back(mid->member);
        obj = mid->object.get();
    }
    const auto* root = dynamic_cast<const Identifier*>(obj);
    if (!root || root->name != "compiler" || path.empty()) return false;
    // path collected outside-in: [member, component].
    const std::string& component = path.back();
    const compilerapi::Component* comp = compilerapi::findComponent(component);
    if (!comp) return false;
    const compilerapi::Member* member = compilerapi::findMember(*comp, call.method_name);
    return member && member->result == result;
}

// Whether `expr` is verified to return a quote: a compiler call the table
// answers `quote` for, or a @special/plain function declared `<quote>` here.
bool returnsQuote(const Expression& expr, Program& program) {
    if (const auto* call = dynamic_cast<const MethodCall*>(&expr))
        return isCompilerCallReturning(*call, "quote");
    const std::string* name = nullptr;
    if (const auto* call = dynamic_cast<const FunctionCall*>(&expr)) name = &call->name;
    if (const auto* inv = dynamic_cast<const MacroInvocation*>(&expr)) name = &inv->name;
    if (name) {
        for (auto& stmt : program.statements) {
            if (const auto* special = dynamic_cast<const SpecialDeclaration*>(stmt.get())) {
                if (special->name == *name)
                    return special->return_type && special->return_type->name == "quote";
            }
            if (const auto* fn = dynamic_cast<const FunctionDeclaration*>(stmt.get())) {
                if (fn->name == *name)
                    return fn->return_type && fn->return_type->name == "quote";
            }
        }
    }
    return false;
}

EvalOutcome evaluateHandler(SpecialDeclaration& decl, Program& program, const W5FirePoint& point,
                            const HandlerRecord& record, DiagReporter& report,
                            DiagReporter& warnReport, DiagReporter& noteReport) {
    EvalOutcome out;
    // A severity the caller did not wire up is still better reported than
    // dropped: warnings fall back to the error reporter, notes to warnings.
    // Both analyzer call sites pass all three, so the fallback only serves a
    // caller that fires without asking for severities.
    auto warn = [&](ASTNode& at, const std::string& msg) {
        if (warnReport) warnReport(at, msg);
        else report(at, msg);
    };
    auto note = [&](ASTNode& at, const std::string& msg) {
        if (noteReport) noteReport(at, msg);
        else if (warnReport) warnReport(at, msg);
        else report(at, msg);
    };
    // §3.8 row one: a handler that reports keeps reporting — compilation
    // continues so a second bad site is also reported — but what it reports
    // with `error` it does not inject at that point.
    bool errorSeen = false;
    if (!decl.body) return out;
    // Straight-line threading (ADR 0006, first step): the handler's
    // parameters bind to the event point's values — strings and booleans by
    // value, metatypes as opaque identities — so lets may name them and
    // calls may thread them. The empty, quote-literal and blame paths below
    // read exactly as before; only the two previously-refused shapes (a let
    // or bare call before the return, a threaded quote return) evaluate.
    comptime::Interpreter interp(program);
    comptime::Env env;
    if (point.event == "struct_layout_finalised") {
        if (!decl.params.empty())
            env.bind(decl.params[0]->name,
                     comptime::Value::makeOpaque("$struct:" + point.structName));
    } else {
        if (decl.params.size() > 0)
            env.bind(decl.params[0]->name, comptime::Value::makeString(point.varName));
        if (decl.params.size() > 1)
            env.bind(decl.params[1]->name,
                     comptime::Value::makeOpaque("$type:" + point.varType));
        if (decl.params.size() > 2)
            env.bind(decl.params[2]->name,
                     comptime::Value::makeBool(point.varMutable ? "true" : "false"));
    }
    for (auto& stmt : decl.body->statements) {
        // A `compiler.diag.*` call executes: the one handler effect the
        // interpreter gap does not cover, recognised syntactically the way
        // quote-returning calls are below. Anything else in the statement is
        // the gap, as before.
        if (const auto* exprStmt = dynamic_cast<const ExpressionStatement*>(stmt.get())) {
            if (exprStmt->expr) {
                std::string op;
                if (asDiagCall(*exprStmt->expr, &op)) {
                    if (op != "error" && op != "warning" && op != "note") {
                        report(*stmt, "Handler '" + record.handler + "' for event '" +
                                           record.event + "' cannot report here: 'compiler.diag." +
                                           op + "' is not a diagnostic operation (error, warning, note)");
                        out.diagnosed = true;
                        return out;
                    }
                    const auto* diag =
                        static_cast<const MethodCall*>(exprStmt->expr.get());
                    const auto* lit = diag->args.size() == 1
                                          ? dynamic_cast<const Literal*>(diag->args[0].get())
                                          : nullptr;
                    if (!lit || lit->kind != ASTTokenKind::STRING_LITERAL) {
                        report(*stmt, "Handler '" + record.handler + "' for event '" +
                                           record.event + "' cannot report here: 'compiler.diag." +
                                           op + "' takes a string literal message (the "
                                           "interpretability line holds: no string operations)");
                        out.diagnosed = true;
                        return out;
                    }
                    const std::string msg = "Handler '" + record.handler + "' for event '" +
                                            record.event + "' at '" + pointDetail(point) +
                                            "': " + lit->value;
                    if (op == "error") {
                        report(*stmt, msg);
                        errorSeen = true;
                    } else if (op == "warning") {
                        warn(*stmt, msg);
                    } else {
                        note(*stmt, msg);
                    }
                    continue;
                }
            }
        }
        if (const auto* blame = dynamic_cast<const BlameStatement*>(stmt.get())) {
            std::string msg = "Handler '" + record.handler + "' for event '" +
                              record.event + "' at '" + pointDetail(point) + "' blamed";
            if (blame->message) {
                if (const auto* lit = dynamic_cast<const Literal*>(blame->message.get()))
                    msg += ": \"" + lit->value + "\"";
            }
            report(*stmt, msg);
            out.diagnosed = true;
            return out;
        }
        if (const auto* ret = dynamic_cast<const ReturnStatement*>(stmt.get())) {
            if (!ret->value) return out;
            if (const auto* quote = dynamic_cast<const QuoteExpression*>(ret->value.get())) {
                if (errorSeen) {
                    // Reported above; what reported does not inject.
                    out.diagnosed = true;
                    return out;
                }
                if (quote->block) {
                    CloneVisitor cloner;
                    for (auto& qstmt : quote->block->statements)
                        out.quote.push_back(cloner.clone<Statement>(qstmt.get()));
                }
                return out;
            }
            // A threaded quote return evaluates alike: `return q;` where a
            // let bound a quote, or a helper call evaluating to one,
            // splices the same block. Anything else keeps the gap below.
            {
                comptime::ExprResult threaded = interp.evaluateExpression(*ret->value, env);
                if (threaded.status == comptime::ExprStatus::Ok &&
                    threaded.value.kind == comptime::ValueKind::Quote &&
                    threaded.value.quote) {
                    if (errorSeen) {
                        out.diagnosed = true;
                        return out;
                    }
                    if (threaded.value.quote->block) {
                        CloneVisitor cloner;
                        for (auto& qstmt : threaded.value.quote->block->statements)
                            out.quote.push_back(cloner.clone<Statement>(qstmt.get()));
                    }
                    return out;
                }
            }
            // A verified quote-returning call is understood but not runnable:
            // anything else is not even known to be a quote.
            const bool knownQuote = returnsQuote(*ret->value, program);
            report(*stmt, "Handler '" + record.handler + "' for event '" +
                              record.event + "' returns '" +
                              std::string(nodeKindName(ret->value->kind())) +
                              (knownQuote
                                   ? "': calls do not run yet "
                                     "(the comptime interpreter gap: no @special execution yet)"
                                   : "': only a quote literal evaluates today "
                                     "(the comptime interpreter gap: no @special execution yet, "
                                     "so handler parameters do not bind and calls do not run)"));
            out.diagnosed = true;
            return out;
        }
        // I-G3: an `if` over a comptime-known bool takes its arm here — the
        // same arm the interpreter takes (evaluateIf carries its depth and
        // acyclicity guards, so a helper-called condition folds alike). A
        // quote answered from the arm splices like a top-level one (and a
        // prior `error` suppresses it alike); falling off the arm continues
        // with the next statement. Anything else keeps the named gap below.
        if (const auto* ifStmt = dynamic_cast<const IfStatement*>(stmt.get())) {
            comptime::BodyResult branch = interp.evaluateIf(*ifStmt, env);
            if (branch.status == comptime::BodyStatus::Empty) continue;
            if (branch.status == comptime::BodyStatus::Returned &&
                branch.value.kind == comptime::ValueKind::Quote &&
                branch.value.quote) {
                if (errorSeen) {
                    out.diagnosed = true;
                    return out;
                }
                if (branch.value.quote->block) {
                    CloneVisitor cloner;
                    for (auto& qstmt : branch.value.quote->block->statements)
                        out.quote.push_back(cloner.clone<Statement>(qstmt.get()));
                }
                return out;
            }
            if (branch.status == comptime::BodyStatus::Blame) {
                std::string msg = "Handler '" + record.handler + "' for event '" +
                                  record.event + "' at '" + pointDetail(point) + "' blamed";
                const std::string prefix = "blame:";
                if (branch.detail.compare(0, prefix.size(), prefix) == 0)
                    msg += branch.detail.substr(prefix.size());
                report(*stmt, msg);
                out.diagnosed = true;
                return out;
            }
            if (branch.status == comptime::BodyStatus::Returned) {
                report(*stmt, "Handler '" + record.handler + "' for event '" +
                                  record.event +
                                  "' returns a non-quote answer from its 'if' arm: "
                                  "only a quote literal evaluates today "
                                  "(the comptime interpreter gap: no @special execution yet, "
                                  "so handler parameters do not bind and calls do not run)");
            } else {
                report(*stmt, "Handler '" + record.handler + "' for event '" +
                                  record.event + "' cannot take its 'if' arm: '" +
                                  branch.detail +
                                  "' (the comptime interpreter gap: the condition must be a "
                                  "comptime-known bool and the taken arm must evaluate)");
            }
            out.diagnosed = true;
            return out;
        }
        // A `let` whose initialiser evaluates binds and continues; a plain
        // `name = value;` rebinds through evaluateAssign (I-G1, the
        // interpreter's case) and continues; a bare call that evaluates runs
        // for its threading and continues. Anything else falls to the
        // existing gap below, word for word.
        if (const auto* let = dynamic_cast<const VariableDeclaration*>(stmt.get())) {
            std::string detail;
            if (interp.evaluateDeclaration(*let, env, &detail) == comptime::ExprStatus::Ok)
                continue;
        } else if (const auto* bare = dynamic_cast<const ExpressionStatement*>(stmt.get())) {
            bool ok = false;
            if (bare->expr) {
                const auto* assign = dynamic_cast<const BinaryOp*>(bare->expr.get());
                if (assign && assign->op == ASTTokenKind::EQUAL &&
                    dynamic_cast<const Identifier*>(assign->left.get())) {
                    std::string detail;
                    ok = interp.evaluateAssign(*assign, env, &detail) ==
                         comptime::ExprStatus::Ok;
                } else {
                    ok = interp.evaluateExpression(*bare->expr, env).status ==
                         comptime::ExprStatus::Ok;
                }
            }
            if (ok) continue;
        }
        // A `let`/`const`, an index store, or a bare call before the return:
        // within the line, but needs an environment the gap does not have.
        const char* form = dynamic_cast<const VariableDeclaration*>(stmt.get()) ? "a declaration"
                           : "a statement";
        report(*stmt, "Handler '" + record.handler + "' for event '" +
                          record.event + "' runs " + form +
                          " before its return: only an empty body or a single "
                          "'return quote { ... };' evaluates today "
                          "(the comptime interpreter gap: no @special execution yet)");
        out.diagnosed = true;
        return out;
    }
    return out;
}

// Where an anchor sits: anchors are statements, so their direct container is
// always a statement vector on a Program or a Block. Anything else is the
// compiler's own structural surprise, diagnosed rather than guessed around.
struct AnchorSite {
    std::vector<std::unique_ptr<Statement>>* vec = nullptr;
    std::size_t idx = 0;
};

class AnchorLocator : public StructuralWalk {
public:
    AnchorLocator(Program& program, const std::vector<const Statement*>& anchors)
        : program_(program) {
        for (const auto* a : anchors) pending_.insert(a);
    }

    std::map<const Statement*, AnchorSite> sites;
    std::vector<const Statement*> missing;

protected:
    bool enter(ASTNode& node) override {
        if (auto* stmt = dynamic_cast<Statement*>(&node)) {
            if (pending_.count(stmt)) {
                pending_.erase(stmt);
                locate(*stmt);
            }
        }
        stack_.push_back(&node);
        return true;
    }

    void leave(ASTNode&) override { stack_.pop_back(); }

private:
    void locate(Statement& anchor) {
        ASTNode* parent = stack_.empty() ? nullptr : stack_.back();
        std::vector<std::unique_ptr<Statement>>* vec = nullptr;
        if (!parent) {
            vec = &program_.statements;
        } else if (auto* block = dynamic_cast<Block*>(parent)) {
            vec = &block->statements;
        } else if (auto* prog = dynamic_cast<Program*>(parent)) {
            vec = &prog->statements;
        } else {
            missing.push_back(&anchor);
            return;
        }
        for (std::size_t i = 0; i < vec->size(); ++i) {
            if (vec->at(i).get() == &anchor) {
                sites[&anchor] = AnchorSite{vec, i};
                return;
            }
        }
        missing.push_back(&anchor);
    }

    Program& program_;
    std::set<const Statement*> pending_;
    std::vector<ASTNode*> stack_;
};

} // namespace

std::vector<FiredHandler> fireW5Events(Program& program, const EventRegistry& registry,
                                       const std::vector<W5FirePoint>& points,
                                       const std::set<std::string>& refused,
                                       DiagReporter report, DiagReporter warnReport,
                                       DiagReporter noteReport,
                                       std::vector<InjectedChunk>* chunks) {
    std::vector<FiredHandler> fired;
    // One batch per handler run: batches sharing an anchor splice in firing
    // order, and each keeps its handler's identity so the post-splice check
    // attributes every inserted statement to the handler that wrote it.
    struct Batch {
        HandlerRecord record;
        W5FirePoint point;
        std::vector<std::unique_ptr<Statement>> stmts;
    };
    std::map<const Statement*, std::vector<Batch>> splices;
    std::vector<const Statement*> anchors;

    for (const auto& point : points) {
        if (!isW5Event(point.event)) continue;
        const std::string detail = pointDetail(point);
        for (const auto& record : registry.orderedHandlers(point.event)) {
            if (!registry.isArmed(record.handler)) continue;
            if (refused.count(record.handler)) continue;
            SpecialDeclaration* decl = findSpecial(program, record.handler);
            // Another module's: its own analyzer fires it. Silence, as in the
            // pre-pass check, or one handler would fire once per importer.
            if (!decl) continue;
            EvalOutcome outcome =
                evaluateHandler(*decl, program, point, record, report, warnReport, noteReport);
            fired.push_back(FiredHandler{point.event, record.handler, detail});
            if (!outcome.quote.empty()) {
                if (point.event == "struct_layout_finalised") {
                    report(*decl, "Handler '" + record.handler +
                                      "' for event 'struct_layout_finalised' at '" +
                                      detail +
                                      "' returns code: the only legal answer is to "
                                      "return nothing (inject nothing)");
                } else if (point.varDecl) {
                    auto& batches = splices[point.varDecl];
                    batches.push_back(Batch{record, point, std::move(outcome.quote)});
                    anchors.push_back(point.varDecl);
                }
            }
        }
    }

    if (!anchors.empty()) {
        AnchorLocator locator(program, anchors);
        locator.walkAll(program.statements);
        // Each anchor resolves against the unmoved tree; every miss is a
        // diagnostic on the program, never a dropped injection.
        for (const auto* anchor : anchors) {
            if (locator.sites.count(anchor)) continue;
            // The anchor is recorded from this same tree, so absence is the
            // compiler surprising itself.
            for (const auto& point : points) {
                if (point.varDecl == anchor)
                    report(program, "Event 'variable_declared' at '" + pointDetail(point) +
                                        "' has nowhere to inject: the declaration moved "
                                        "during analysis");
            }
        }
        // Grouped by vector, descending index, so each insert holds the
        // indices of the ones below it still.
        std::map<std::vector<std::unique_ptr<Statement>>*, std::vector<std::size_t>> byVec;
        for (const auto& [anchor, site] : locator.sites) byVec[site.vec].push_back(site.idx);
        for (auto& [vec, idxs] : byVec) {
            std::sort(idxs.begin(), idxs.end(), std::greater<std::size_t>());
            for (const auto idx : idxs) {
                const Statement* anchor = vec->at(idx).get();
                auto it = splices.find(anchor);
                if (it == splices.end()) continue;
                // Batches in firing order; the insert point advances past each
                // batch so the next lands after it, and each batch's statements
                // are recorded with their handler while their addresses are
                // known — moves never relocate them afterwards.
                std::size_t at = idx + 1;
                for (auto& batch : it->second) {
                    const std::size_t n = batch.stmts.size();
                    vec->insert(vec->begin() + static_cast<long>(at),
                                std::make_move_iterator(batch.stmts.begin()),
                                std::make_move_iterator(batch.stmts.end()));
                    if (chunks) {
                        InjectedChunk chunk;
                        chunk.anchor = anchor;
                        chunk.event = batch.point.event;
                        chunk.handler = batch.record.handler;
                        chunk.detail = pointDetail(batch.point);
                        chunk.line = batch.point.line;
                        for (std::size_t i = 0; i < n; ++i)
                            chunk.inserted.push_back(vec->at(at + i).get());
                        chunks->push_back(std::move(chunk));
                    }
                    at += n;
                }
            }
        }
    }

    return fired;
}

} // namespace fin::events
