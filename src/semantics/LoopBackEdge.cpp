// Wave-4 step 20, W10: `loop_back_edge` plus its firing.
// See LoopBackEdge.hpp for the contract.
#include "LoopBackEdge.hpp"

#include <map>
#include <set>
#include <string>

#include "../ast/CloneVisitor.hpp"
#include "../ast/NodeKind.hpp"
#include "../ast/StructuralWalk.hpp"
#include "../ast/decls/Program.hpp"
#include "../ast/decls/TypeDef.hpp"
#include "../ast/exprs/Identifier.hpp"
#include "../ast/exprs/Lambda.hpp"
#include "../ast/exprs/Literal.hpp"
#include "../ast/nodes/Parameter.hpp"
#include "../ast/stmts/ControlFlow.hpp"
#include "../ast/stmts/ErrorHandling.hpp"
#include "../ast/stmts/Statement.hpp"
#include "../ast/stmts/VariableDecl.hpp"
#include "../ast/types/TypeNode.hpp"
#include "ComptimeInterp.hpp"

namespace fin::events {

const EventPayload& w10Payload() {
    // docs/compiler-api.md §3.2, the W10 row.
    static const EventPayload table{
        "loop_back_edge",
        {{"depth", "int"}},
        "quote",
    };
    return table;
}

const EventPayload* findW10Payload(const std::string& event) {
    const EventPayload& p = w10Payload();
    return p.event == event ? &p : nullptr;
}

bool isW10Event(const std::string& event) { return findW10Payload(event) != nullptr; }

namespace {

// A written type spelling is the payload's when it names the same base type
// with no structure of its own: no pointer, array, generic argument or
// nullability. `const` is exempt: it binds the parameter, it does not change
// what arrives. (W6's rule, restated: one rule, four owners, each owning
// disjoint events.)
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

// The interpretability line as a walker: the first control-flow form found,
// by its source spelling. Same four forms W6 holds, so the diagnostics read
// alike — and a handler that would need to walk anything needs a loop,
// which is exactly what this refuses by name.
class FlowWalker : public StructuralWalk {
public:
    std::string found;
    bool enter(ASTNode& node) override {
        switch (node.kind()) {
            case NodeKind::IfStatement: found = "if"; return false;
            case NodeKind::WhileLoop: found = "while"; return false;
            case NodeKind::ForLoop: found = "for"; return false;
            case NodeKind::ForeachLoop: found = "foreach"; return false;
            default: return true;
        }
    }
};

} // namespace

std::set<std::string> checkW10HandlerPayloads(Program& program, const EventRegistry& registry,
                                              DiagReporter report) {
    std::set<std::string> refused;
    for (const auto& record : registry.all()) {
        if (record.event != w10Payload().event) continue;
        SpecialDeclaration* decl = findSpecial(program, record.handler);
        // Another module's: its own analyzer checks it. Silence here, or one
        // handler would be diagnosed once per importing module.
        if (!decl) continue;
        if (!signatureMatches(*decl, w10Payload())) {
            report(*decl, "Handler '" + record.handler + "' for event '" +
                              record.event + "' has the wrong payload: expected " +
                              expectedSignature(w10Payload()) + ", wrote " +
                              writtenSignature(*decl));
            refused.insert(record.handler);
            continue;
        }
        if (decl->body) {
            FlowWalker flow;
            flow.walk(decl->body.get());
            if (!flow.found.empty()) {
                report(*decl, "Handler '" + record.handler + "' for event '" +
                                  record.event + "' uses control flow ('" +
                                  flow.found +
                                  "'): handlers hold no control flow "
                                  "(the interpretability line)");
                refused.insert(record.handler);
            }
        }
    }
    return refused;
}

std::string w10PointDetail(const LoopBackEdgePoint& point) {
    return point.kind + "@" + std::to_string(point.line) + ":depth=" +
           std::to_string(point.depth);
}

namespace {

// One handler run's outcome: nothing to inject, a quote to splice, or a
// diagnostic already reported (abort and gap alike inject nothing).
struct EvalOutcome {
    std::vector<std::unique_ptr<Statement>> quote;
    bool diagnosed = false;
};

EvalOutcome evaluateHandler(SpecialDeclaration& decl, Program& program, const LoopBackEdgePoint& point,
                            const HandlerRecord& record, DiagReporter& report) {
    EvalOutcome out;
    if (!decl.body) return out;
    // Straight-line threading (ADR 0006, first step): the handler's depth
    // parameter binds to the latch point's depth, so lets may name it and
    // calls may thread it. The empty, quote-literal and blame paths below
    // read exactly as before; only the two previously-refused shapes (a let
    // or bare call before the return, a threaded quote return) evaluate.
    comptime::Interpreter interp(program);
    comptime::Env env;
    if (!decl.params.empty())
        env.bind(decl.params[0]->name,
                 comptime::Value::makeInt(std::to_string(point.depth)));
    for (auto& stmt : decl.body->statements) {
        if (const auto* blame = dynamic_cast<const BlameStatement*>(stmt.get())) {
            std::string msg = "Handler '" + record.handler + "' for event '" +
                              w10Payload().event + "' at '" + w10PointDetail(point) + "' blamed";
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
                    if (threaded.value.quote->block) {
                        CloneVisitor cloner;
                        for (auto& qstmt : threaded.value.quote->block->statements)
                            out.quote.push_back(cloner.clone<Statement>(qstmt.get()));
                    }
                    return out;
                }
            }
            report(*stmt, "Handler '" + record.handler + "' for event '" +
                              w10Payload().event + "' returns '" +
                              std::string(nodeKindName(ret->value->kind())) +
                              "': only a quote literal evaluates today "
                              "(the comptime interpreter gap: no @special execution yet, "
                              "so handler parameters do not bind and calls do not run)");
            out.diagnosed = true;
            return out;
        }
        // A `let` whose initialiser evaluates binds and continues; a bare
        // call that evaluates runs for its threading and continues. Either
        // is straight-line threading (literals + lets + calls). Anything
        // else falls to the existing gap below, word for word.
        if (const auto* let = dynamic_cast<const VariableDeclaration*>(stmt.get())) {
            std::string detail;
            if (interp.evaluateDeclaration(*let, env, &detail) == comptime::ExprStatus::Ok)
                continue;
        } else if (const auto* bare = dynamic_cast<const ExpressionStatement*>(stmt.get())) {
            if (bare->expr &&
                interp.evaluateExpression(*bare->expr, env).status == comptime::ExprStatus::Ok)
                continue;
        }
        // A `let`/`const`, an index store, or a bare call before the return:
        // within the line, but needs an environment the gap does not have.
        const char* form = dynamic_cast<const VariableDeclaration*>(stmt.get()) ? "a declaration"
                           : "a statement";
        report(*stmt, "Handler '" + record.handler + "' for event '" +
                          w10Payload().event + "' runs " + form +
                          " before its return: only an empty body or a single "
                          "'return quote { ... };' evaluates today "
                          "(the comptime interpreter gap: no @special execution yet)");
        out.diagnosed = true;
        return out;
    }
    return out;
}

} // namespace

std::vector<W10FiredHandler> fireW10Events(Program& program, const EventRegistry& registry,
                                           const std::vector<LoopBackEdgePoint>& points,
                                           const std::set<std::string>& refused,
                                           DiagReporter report) {
    std::vector<W10FiredHandler> fired;
    for (const auto& point : points) {
        if (point.event != w10Payload().event) continue;
        const std::string detail = w10PointDetail(point);
        // One batch per point: every armed handler's quote in firing order,
        // prepended as one header so the first handler's poll runs first.
        std::vector<std::unique_ptr<Statement>> batch;
        for (const auto& record : registry.orderedHandlers(w10Payload().event)) {
            if (!registry.isArmed(record.handler)) continue;
            if (refused.count(record.handler)) continue;
            SpecialDeclaration* decl = findSpecial(program, record.handler);
            // Another module's: its own analyzer fires it. Silence, as in the
            // pre-pass check, or one handler would fire once per importer.
            if (!decl) continue;
            EvalOutcome outcome = evaluateHandler(*decl, program, point, record, report);
            fired.push_back(W10FiredHandler{w10Payload().event, record.handler, detail});
            for (auto& stmt : outcome.quote) batch.push_back(std::move(stmt));
        }
        // The header: the poll runs each iteration, including past a
        // `continue` (which skips the tail but re-enters here). A null body
        // still logs the run above; there is simply nowhere to inject.
        if (!batch.empty() && point.body != nullptr) {
            auto* mutableBody = const_cast<Block*>(point.body);
            mutableBody->statements.insert(
                mutableBody->statements.begin(),
                std::make_move_iterator(batch.begin()),
                std::make_move_iterator(batch.end()));
        }
    }
    return fired;
}

} // namespace fin::events
