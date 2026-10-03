// Wave-4 step 17, W7: `variable_scope_exit` plus the moved analysis.
// See MovedAnalysis.hpp for the contract.
#include "MovedAnalysis.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "../ast/CloneVisitor.hpp"
#include "../ast/NodeKind.hpp"
#include "../ast/StructuralWalk.hpp"
#include "../ast/decls/Program.hpp"
#include "../ast/decls/TypeDef.hpp"
#include "../ast/exprs/BinaryOp.hpp"
#include "../ast/exprs/FunctionCall.hpp"
#include "../ast/exprs/Identifier.hpp"
#include "../ast/exprs/Lambda.hpp"
#include "../ast/exprs/Literal.hpp"
#include "../ast/nodes/Parameter.hpp"
#include "../ast/stmts/ControlFlow.hpp"
#include "../ast/stmts/ErrorHandling.hpp"
#include "../ast/stmts/Statement.hpp"
#include "../ast/stmts/VariableDecl.hpp"
#include "../ast/types/TypeNode.hpp"
#include "../types/StructType.hpp"
#include "ComptimeInterp.hpp"

namespace fin::events {

MovedState joinMoved(MovedState a, MovedState b) {
    if (a == b) return a;
    return MovedState::Maybe;
}

int movedValue(MovedState s) {
    switch (s) {
        case MovedState::Live: return kMovedNo;
        case MovedState::Moved: return kMovedYes;
        case MovedState::Maybe: return kMovedMaybe;
    }
    return kMovedNo;
}

const char* movedName(MovedState s) {
    switch (s) {
        case MovedState::Live: return "live";
        case MovedState::Moved: return "moved";
        case MovedState::Maybe: return "maybe";
    }
    return "live";
}

const EventPayload& w7Payload() {
    // docs/compiler-api.md §3.2, the W7 row.
    static const EventPayload table{
        "variable_scope_exit",
        {{"name", "string"}, {"t", "$type"}, {"exit_kind", "int"}, {"moved", "int"}},
        "quote",
    };
    return table;
}

const EventPayload* findW7Payload(const std::string& event) {
    const EventPayload& p = w7Payload();
    return p.event == event ? &p : nullptr;
}

bool isW7Event(const std::string& event) { return findW7Payload(event) != nullptr; }

namespace {

// A written type spelling is the payload's when it names the same base type
// with no structure of its own: no pointer, array, generic argument or
// nullability. `const` is exempt: it binds the parameter, it does not change
// what arrives. (W6's rule, restated: one rule, three owners, each owning
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

// The interpretability line as a walker: the first loop form found, by its
// source spelling. `if`/`else` evaluates (I-G3: the taken arm over a
// comptime-known bool) and is never a breach — only loops are — and a
// handler that would need to walk a field tree needs a loop, which is
// exactly what this refuses by name (Q4/Q14).
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

} // namespace

std::set<std::string> checkW7HandlerPayloads(Program& program, const EventRegistry& registry,
                                             DiagReporter report) {
    std::set<std::string> refused;
    for (const auto& record : registry.all()) {
        if (record.event != w7Payload().event) continue;
        SpecialDeclaration* decl = findSpecial(program, record.handler);
        // Another module's: its own analyzer checks it. Silence here, or one
        // handler would be diagnosed once per importing module.
        if (!decl) continue;
        if (!signatureMatches(*decl, w7Payload())) {
            report(*decl, "Handler '" + record.handler + "' for event '" +
                              record.event + "' has the wrong payload: expected " +
                              expectedSignature(w7Payload()) + ", wrote " +
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
                                   "'): handlers hold lets, calls, rebinds and known-bool "
                                   "branches, and no other control flow "
                                   "(the interpretability line)");
                refused.insert(record.handler);
            }
        }
    }
    return refused;
}

std::string w7PointDetail(const ScopeExitPoint& point) {
    return point.varName + ":" + point.varType +
           ":exit=" + (point.exitKind == kExitBlamed ? "blamed" : "normal") +
           ":moved=" + movedName(point.moved);
}

namespace {

// One handler run's outcome: nothing to inject, a quote to splice, or a
// diagnostic already reported (abort and gap alike inject nothing).
struct EvalOutcome {
    std::vector<std::unique_ptr<Statement>> quote;
    bool diagnosed = false;
};

EvalOutcome evaluateHandler(SpecialDeclaration& decl, Program& program, const ScopeExitPoint& point,
                            const HandlerRecord& record, DiagReporter& report) {
    EvalOutcome out;
    if (!decl.body) return out;
    // Straight-line threading (ADR 0006, first step): the handler's
    // parameters bind to the exit point's values -- the name by value, the
    // type as an opaque identity, the two integers by value -- so lets may
    // name them and calls may thread them. The empty, quote-literal and
    // blame paths below read exactly as before; only the two
    // previously-refused shapes (a let or bare call before the return, a
    // threaded quote return) evaluate.
    comptime::Interpreter interp(program);
    comptime::Env env;
    if (!decl.params.empty())
        env.bind(decl.params[0]->name, comptime::Value::makeString(point.varName));
    if (decl.params.size() > 1)
        env.bind(decl.params[1]->name,
                 comptime::Value::makeOpaque("$type:" + point.varType));
    if (decl.params.size() > 2)
        env.bind(decl.params[2]->name,
                 comptime::Value::makeInt(std::to_string(point.exitKind)));
    if (decl.params.size() > 3)
        env.bind(decl.params[3]->name,
                 comptime::Value::makeInt(std::to_string(movedValue(point.moved))));
    for (auto& stmt : decl.body->statements) {
        if (const auto* blame = dynamic_cast<const BlameStatement*>(stmt.get())) {
            std::string msg = "Handler '" + record.handler + "' for event '" +
                              w7Payload().event + "' at '" + w7PointDetail(point) + "' blamed";
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
                               w7Payload().event + "' returns '" +
                               std::string(nodeKindName(ret->value->kind())) +
                               "': only a quote literal evaluates today "
                               "(the comptime interpreter gap: no @special execution yet, "
                               "so handler parameters do not bind and calls do not run)");
            out.diagnosed = true;
            return out;
        }
        // I-G3: an `if` over a comptime-known bool takes its arm here — the
        // same arm the interpreter takes (evaluateIf carries its depth and
        // acyclicity guards). A quote answered from the arm splices like a
        // top-level one; falling off the arm continues. Anything else keeps
        // the named gap below.
        if (const auto* ifStmt = dynamic_cast<const IfStatement*>(stmt.get())) {
            comptime::BodyResult branch = interp.evaluateIf(*ifStmt, env);
            if (branch.status == comptime::BodyStatus::Empty) continue;
            if (branch.status == comptime::BodyStatus::Returned &&
                branch.value.kind == comptime::ValueKind::Quote &&
                branch.value.quote) {
                if (branch.value.quote->block) {
                    CloneVisitor cloner;
                    for (auto& qstmt : branch.value.quote->block->statements)
                        out.quote.push_back(cloner.clone<Statement>(qstmt.get()));
                }
                return out;
            }
            if (branch.status == comptime::BodyStatus::Blame) {
                std::string msg = "Handler '" + record.handler + "' for event '" +
                                  w7Payload().event + "' at '" + w7PointDetail(point) +
                                  "' blamed";
                const std::string prefix = "blame:";
                if (branch.detail.compare(0, prefix.size(), prefix) == 0)
                    msg += branch.detail.substr(prefix.size());
                report(*stmt, msg);
                out.diagnosed = true;
                return out;
            }
            if (branch.status == comptime::BodyStatus::Returned) {
                report(*stmt, "Handler '" + record.handler + "' for event '" +
                                   w7Payload().event +
                                   "' returns a non-quote answer from its 'if' arm: "
                                   "only a quote literal evaluates today "
                                   "(the comptime interpreter gap: no @special execution yet, "
                                   "so handler parameters do not bind and calls do not run)");
            } else {
                report(*stmt, "Handler '" + record.handler + "' for event '" +
                                   w7Payload().event + "' cannot take its 'if' arm: '" +
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
                          w7Payload().event + "' runs " + form +
                          " before its return: only an empty body or a single "
                          "'return quote { ... };' evaluates today "
                          "(the comptime interpreter gap: no @special execution yet)");
        out.diagnosed = true;
        return out;
    }
    return out;
}

// Where a before-anchor sits: anchors are statements, so their direct
// container is always a statement vector on a Program or a Block. Anything
// else is the compiler's own structural surprise, diagnosed rather than
// guessed around.
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

std::vector<W7FiredHandler> fireW7Events(Program& program, const EventRegistry& registry,
                                         const std::vector<ScopeExitPoint>& points,
                                         const std::set<std::string>& refused,
                                         DiagReporter report) {
    std::vector<W7FiredHandler> fired;
    std::map<const Statement*, std::vector<std::unique_ptr<Statement>>> befores;
    std::map<const Statement*, std::vector<std::unique_ptr<Statement>>> afters;
    std::map<const Block*, std::vector<std::unique_ptr<Statement>>> appends;
    std::vector<const Statement*> anchors;

    for (const auto& point : points) {
        const std::string detail = w7PointDetail(point);
        for (const auto& record : registry.orderedHandlers(w7Payload().event)) {
            if (!registry.isArmed(record.handler)) continue;
            if (refused.count(record.handler)) continue;
            SpecialDeclaration* decl = findSpecial(program, record.handler);
            // Another module's: its own analyzer fires it. Silence, as in the
            // pre-pass check, or one handler would fire once per importer.
            if (!decl) continue;
            EvalOutcome outcome = evaluateHandler(*decl, program, point, record, report);
            fired.push_back(W7FiredHandler{w7Payload().event, record.handler, detail});
            if (outcome.quote.empty()) continue;
            if (point.beforeStmt) {
                auto& slot = befores[point.beforeStmt];
                for (auto& stmt : outcome.quote) slot.push_back(std::move(stmt));
                anchors.push_back(point.beforeStmt);
            } else if (point.afterStmt) {
                auto& slot = afters[point.afterStmt];
                for (auto& stmt : outcome.quote) slot.push_back(std::move(stmt));
                anchors.push_back(point.afterStmt);
            } else if (point.appendBlock) {
                auto& slot = appends[point.appendBlock];
                for (auto& stmt : outcome.quote) slot.push_back(std::move(stmt));
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
            for (const auto& point : points) {
                if (point.beforeStmt == anchor)
                    report(program, "Event 'variable_scope_exit' at '" + w7PointDetail(point) +
                                        "' has nowhere to inject: the statement moved "
                                        "during analysis");
            }
        }
        // Grouped by vector, descending index, so each insert holds the
        // indices of the ones below it still. Afters go in first so that, at
        // a shared index, the before batch still lands before the statement
        // and the after batch after it.
        std::map<std::vector<std::unique_ptr<Statement>>*, std::vector<std::size_t>> byVec;
        for (const auto& [anchor, site] : locator.sites) byVec[site.vec].push_back(site.idx);
        auto insertAt = [&](const Statement* anchor, std::size_t idx,
                            std::vector<std::unique_ptr<Statement>>* vec) {
            if (auto it = afters.find(anchor); it != afters.end()) {
                auto& stmts = it->second;
                vec->insert(vec->begin() + static_cast<long>(idx + 1),
                            std::make_move_iterator(stmts.begin()),
                            std::make_move_iterator(stmts.end()));
                afters.erase(it);
            }
            if (auto it = befores.find(anchor); it != befores.end()) {
                auto& stmts = it->second;
                vec->insert(vec->begin() + static_cast<long>(idx),
                            std::make_move_iterator(stmts.begin()),
                            std::make_move_iterator(stmts.end()));
                befores.erase(it);
            }
        };
        for (auto& [vec, idxs] : byVec) {
            std::sort(idxs.begin(), idxs.end(), std::greater<std::size_t>());
            for (const auto idx : idxs) insertAt(vec->at(idx).get(), idx, vec);
        }
    }

    // Appends land at the block end whatever the before-inserts did: an
    // insert shifts indices, never the end. Pointers are heap-stable, so
    // holding the Block across the inserts above is sound.
    for (auto& [block, stmts] : appends) {
        auto* mutableBlock = const_cast<Block*>(block);
        for (auto& stmt : stmts) mutableBlock->statements.push_back(std::move(stmt));
    }

    return fired;
}

// --- MovedAnalysis ------------------------------------------------------------

namespace {

bool isBoundaryKind(MovedAnalysis::Snapshot::Frame::Kind kind) {
    return kind != MovedAnalysis::Snapshot::Frame::Kind::Normal;
}

bool isFuncBoundary(MovedAnalysis::Snapshot::Frame::Kind kind) {
    return kind == MovedAnalysis::Snapshot::Frame::Kind::FuncRoot ||
           kind == MovedAnalysis::Snapshot::Frame::Kind::FuncMark;
}

} // namespace

MovedAnalysis::Snapshot::Frame* MovedAnalysis::find(const std::string& name, std::size_t top) {
    const std::size_t n = frames_.size();
    if (n == 0) return nullptr;
    if (top >= n) top = n - 1;
    for (std::size_t i = top + 1; i-- > 0;) {
        auto& frame = frames_[i];
        if (frame.kind == Kind::FuncMark || frame.kind == Kind::LoopMark) continue;
        for (const auto& var : frame.vars) {
            if (var.first == name) return &frame;
        }
        if (frame.kind == Kind::FuncRoot) return nullptr;
    }
    return nullptr;
}

void MovedAnalysis::recordFrame(std::size_t index, int exitKind, const Statement* before,
                                const Block* append, int line, DiagReporter report) {
    (void)report;
    if (index >= frames_.size()) return;
    const auto& frame = frames_[index];
    for (const auto& [var, state] : frame.vars) {
        auto typed = frame.types.find(var);
        ScopeExitPoint point;
        point.varName = var;
        point.varType = typed != frame.types.end() ? typed->second : "<unresolved>";
        point.exitKind = exitKind;
        point.moved = state;
        point.line = line;
        point.beforeStmt = before;
        point.appendBlock = append;
        points_.push_back(std::move(point));
    }
}

void MovedAnalysis::enterFunction() {
    Snapshot::Frame frame;
    frame.kind = Kind::FuncRoot;
    frames_.push_back(std::move(frame));
}

void MovedAnalysis::exitFunction(Block& body, bool fallsThrough, DiagReporter report) {
    if (!frames_.empty() && frames_.back().kind == Kind::FuncRoot) {
        if (fallsThrough)
            recordFrame(frames_.size() - 1, kExitNormal, nullptr, &body,
                        body.loc.begin.line, report);
        frames_.pop_back();
    }
}

void MovedAnalysis::enterBlock() {
    Snapshot::Frame frame;
    frame.kind = Kind::Normal;
    frames_.push_back(std::move(frame));
}

void MovedAnalysis::exitBlock(Block& block, DiagReporter report) {
    if (!frames_.empty() && frames_.back().kind == Kind::Normal) {
        recordFrame(frames_.size() - 1, kExitNormal, nullptr, &block,
                    block.loc.begin.line, report);
        frames_.pop_back();
    }
}

void MovedAnalysis::exitBlockAfter(const Statement& anchor, DiagReporter report) {
    (void)report;
    if (frames_.empty() || frames_.back().kind != Kind::Normal) return;
    // The quote lands after the loop statement. Points carry the after
    // anchor; firing inserts at anchor index + 1 (see fireW7Events).
    const auto& frame = frames_.back();
    for (const auto& [var, state] : frame.vars) {
        auto typed = frame.types.find(var);
        ScopeExitPoint point;
        point.varName = var;
        point.varType = typed != frame.types.end() ? typed->second : "<unresolved>";
        point.exitKind = kExitNormal;
        point.moved = state;
        point.line = anchor.loc.begin.line;
        point.afterStmt = &anchor;
        points_.push_back(std::move(point));
    }
    frames_.pop_back();
}

void MovedAnalysis::enterLoop() {
    Snapshot::Frame frame;
    frame.kind = Kind::LoopMark;
    frames_.push_back(std::move(frame));
}

void MovedAnalysis::exitLoop() {
    if (!frames_.empty() && frames_.back().kind == Kind::LoopMark) frames_.pop_back();
}

void MovedAnalysis::enterLambda() {
    Snapshot::Frame frame;
    frame.kind = Kind::FuncMark;
    frames_.push_back(std::move(frame));
}

void MovedAnalysis::exitLambda() {
    if (!frames_.empty() && frames_.back().kind == Kind::FuncMark) frames_.pop_back();
}

MovedAnalysis::Snapshot MovedAnalysis::snapshot() const {
    Snapshot snap;
    snap.frames = frames_;
    return snap;
}

void MovedAnalysis::restore(const Snapshot& snap) { frames_ = snap.frames; }

void MovedAnalysis::installJoin(const Snapshot& a, const Snapshot& b) {
    const std::size_t n = std::min({frames_.size(), a.frames.size(), b.frames.size()});
    for (std::size_t i = 0; i < n; ++i) {
        auto& live = frames_[i];
        const auto& fa = a.frames[i];
        const auto& fb = b.frames[i];
        if (live.kind != fa.kind || live.kind != fb.kind) continue;
        if (isBoundaryKind(live.kind)) continue;
        // Every variable on either side ends joined. A variable declared on
        // only one side keeps that side's state: on the other path the
        // binding never existed, so there is nothing to disagree with.
        std::map<std::string, MovedState> joined;
        for (const auto& [var, state] : fa.vars) joined[var] = state;
        for (const auto& [var, state] : fb.vars) {
            auto it = joined.find(var);
            joined[var] = it == joined.end() ? state : joinMoved(it->second, state);
        }
        live.vars.clear();
        for (const auto& [var, state] : joined) live.vars.emplace_back(var, state);
        for (const auto& [var, type] : fa.types)
            if (!live.types.count(var)) live.types[var] = type;
        for (const auto& [var, type] : fb.types)
            if (!live.types.count(var)) live.types[var] = type;
    }
}

void MovedAnalysis::declare(const std::string& name, const std::string& type) {
    if (frames_.empty()) return;
    auto& frame = frames_.back();
    if (isBoundaryKind(frame.kind)) return;
    for (auto& [var, state] : frame.vars) {
        if (var == name) {
            state = MovedState::Live;
            frame.types[name] = type;
            return;
        }
    }
    frame.vars.emplace_back(name, MovedState::Live);
    frame.types[name] = type;
}

void MovedAnalysis::use(const std::string& name, ASTNode& at, DiagReporter report) {
    if (frames_.empty() || useSuspended_ > 0) return;
    Snapshot::Frame* frame = find(name, frames_.size() - 1);
    if (!frame) return;
    for (const auto& [var, state] : frame->vars) {
        if (var == name && state == MovedState::Moved) {
            report(at, "Use of moved-from variable '" + name + "'");
            return;
        }
    }
}

void MovedAnalysis::assign(const std::string& name) {
    if (frames_.empty()) return;
    Snapshot::Frame* frame = find(name, frames_.size() - 1);
    if (!frame) return;
    for (auto& [var, state] : frame->vars) {
        if (var == name) {
            state = MovedState::Live;
            return;
        }
    }
}

void MovedAnalysis::move(const std::string& name) {
    if (frames_.empty()) return;
    Snapshot::Frame* frame = find(name, frames_.size() - 1);
    if (!frame) return;
    for (auto& [var, state] : frame->vars) {
        if (var == name) {
            // Already-Moved stays: the operand's own read reported it.
            if (state != MovedState::Moved) state = MovedState::Moved;
            return;
        }
    }
}

void MovedAnalysis::suspendUses() { ++useSuspended_; }

void MovedAnalysis::resumeUses() {
    if (useSuspended_ > 0) --useSuspended_;
}

void MovedAnalysis::onReturn(const Statement& anchor, DiagReporter report) {
    if (frames_.empty()) return;
    for (std::size_t i = frames_.size(); i-- > 0;) {
        const auto kind = frames_[i].kind;
        if (isFuncBoundary(kind)) {
            if (kind == Kind::FuncRoot)
                recordFrame(i, kExitNormal, &anchor, nullptr, anchor.loc.begin.line, report);
            return;
        }
        if (kind == Kind::Normal)
            recordFrame(i, kExitNormal, &anchor, nullptr, anchor.loc.begin.line, report);
    }
}

void MovedAnalysis::onBreak(const Statement& anchor, DiagReporter report) {
    onJumpToLoop(anchor, report);
}

void MovedAnalysis::onContinue(const Statement& anchor, DiagReporter report) {
    onJumpToLoop(anchor, report);
}

void MovedAnalysis::onJumpToLoop(const Statement& anchor, DiagReporter report) {
    if (frames_.empty()) return;
    // Collect the Normal frames above the top, stopping at any boundary.
    // Only a loop marker below them makes the jump bind to a loop here.
    std::vector<std::size_t> above;
    bool bindsLoop = false;
    for (std::size_t i = frames_.size(); i-- > 0;) {
        const auto kind = frames_[i].kind;
        if (kind == Kind::Normal) {
            above.push_back(i);
            continue;
        }
        bindsLoop = (kind == Kind::LoopMark);
        break;
    }
    if (!bindsLoop) return;
    for (const auto idx : above)
        recordFrame(idx, kExitNormal, &anchor, nullptr, anchor.loc.begin.line, report);
}

void MovedAnalysis::onBlame(const Statement& anchor, DiagReporter report) {
    if (frames_.empty()) return;
    for (std::size_t i = frames_.size(); i-- > 0;) {
        const auto kind = frames_[i].kind;
        if (isFuncBoundary(kind)) {
            if (kind == Kind::FuncRoot)
                recordFrame(i, kExitBlamed, &anchor, nullptr, anchor.loc.begin.line, report);
            return;
        }
        if (kind == Kind::Normal)
            recordFrame(i, kExitBlamed, &anchor, nullptr, anchor.loc.begin.line, report);
    }
}

// --- Slice 3: composition ------------------------------------------------------
// ADR 0016: a struct field whose type has a destructor gets it called from
// the parent's destructor, and the compiler generates the parent when there
// is none. The generated destructor is observable: has_destructor answers
// true for a type that declared none but acquired one by composition, which
// is what compiler.structs.has_destructor lets a handler ask.
void propagateComposedDestructor(StructType& type) {
    if (type.has_destructor) return;
    for (const auto& field : type.fields) {
        // Only a directly-held struct value composes: pointer and array
        // fields hold no owned value (the backend's emitDestructorCall
        // recurses on the same rule), and a nullable field is the open
        // nullifier question -- refusing the answer rather than guessing it.
        const auto* fieldStruct = field.type ? field.type->as<StructType>() : nullptr;
        if (fieldStruct && fieldStruct->has_destructor) {
            type.has_destructor = true;
            return;
        }
    }
    for (const auto& parent : type.parents) {
        // Bases compose like fields (the backend recurses into effective
        // bases); interfaces do not -- a destructor there is a requirement,
        // not an implementation, and nothing lowers it.
        const auto* base = parent ? parent->as<StructType>() : nullptr;
        if (base && !base->is_interface && base->has_destructor) {
            type.has_destructor = true;
            return;
        }
    }
}

} // namespace fin::events
