#include "EventRegistry.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "../ast/StructuralWalk.hpp"
#include "../ast/decls/Program.hpp"
#include "../ast/decls/TypeDef.hpp"
#include "../ast/exprs/FunctionCall.hpp"
#include "../ast/exprs/Identifier.hpp"
#include "../ast/exprs/StructureExpr.hpp"
#include "../ast/stmts/VariableDecl.hpp"
#include "../ast/types/Attribute.hpp"

// Wave-4 step 15: `#[on(...)]` collection and the three-phase model
// (docs/compiler-api.md §3.4). Collect gathers handlers, Arm resolves the
// enable set; firing is the next slice and nothing here runs a handler.
namespace fin::events {

bool isKnownEvent(const std::string& name) {
    // docs/compiler-api.md §3.2, transcribed. The eleventh row,
    // `generic_instantiated`, is speculative-but-listed, so it is known here
    // and simply never fires until something wants it.
    static const std::set<std::string> known = {
        "variable_declared", "variable_scope_exit",
        "function_entry", "function_exit",
        "assignment", "allocation_site", "delete_site",
        "struct_layout_deciding", "struct_layout_finalised",
        "loop_back_edge", "generic_instantiated",
    };
    return known.count(name) != 0;
}

void EventRegistry::add(HandlerRecord record) {
    // An identical registration is one entry: firing it twice would run two
    // collectors over the same variable (ADR 0007). Same handler on the same
    // event in a *different* module is a different entry -- that is composition.
    for (const auto& r : records_)
        if (r.event == record.event && r.handler == record.handler && r.module == record.module)
            return;
    records_.push_back(std::move(record));
}

bool EventRegistry::hasHandler(const std::string& handler) const {
    for (const auto& r : records_)
        if (r.handler == handler) return true;
    return false;
}

std::vector<HandlerRecord> EventRegistry::handlers(const std::string& event) const {
    std::vector<HandlerRecord> out;
    for (const auto& r : records_)
        if (r.event == event) out.push_back(r);
    return out;
}

std::vector<HandlerRecord> EventRegistry::all() const { return records_; }

std::size_t EventRegistry::size() const { return records_.size(); }

void EventRegistry::recordImport(const std::string& importer, const std::string& imported) {
    imports_.emplace_back(importer, imported);
}

std::vector<HandlerRecord> EventRegistry::orderedHandlers(const std::string& event) const {
    // Q10 (ADR 0007 as corrected): dependencies before importers, ties by
    // module path string, then declaration order within a module.
    //
    // Kahn's algorithm, lexicographically smallest first, over
    // dependency->dependent edges. A DFS reverse post-order was tried first on
    // paper and rejected: its tie-break is local to each DFS subtree, so with
    // root importing zebra then apple (both importing shared) it can emit zebra
    // before apple depending on where the search starts. Kahn's ready set is
    // global, so the path string decides every tie the same way.
    //
    // Worked example (EventOrder.OrdersHandlersAcrossAnImportDag):
    //   edges shared->apple, shared->zebra, apple->root, zebra->root;
    //   ready {shared} -> emit shared, ready {apple, zebra} -> emit apple,
    //   then zebra, then root.
    std::set<std::string> modules;
    for (const auto& r : records_) modules.insert(r.module);
    for (const auto& [importer, imported] : imports_) {
        modules.insert(importer);
        modules.insert(imported);
    }

    std::map<std::string, std::set<std::string>> dependents;
    std::map<std::string, int> outstanding;
    for (const auto& m : modules) {
        dependents[m];
        outstanding[m] = 0;
    }
    for (const auto& [importer, imported] : imports_) {
        if (importer == imported) continue;
        if (dependents[imported].insert(importer).second) ++outstanding[importer];
    }

    std::set<std::string> ready;
    for (const auto& [m, n] : outstanding)
        if (n == 0) ready.insert(m);

    std::vector<std::string> order;
    while (!ready.empty()) {
        const std::string m = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(m);
        for (const auto& dependent : dependents[m])
            if (--outstanding[dependent] == 0) ready.insert(dependent);
    }
    // An import cycle leaves modules with outstanding dependencies; the loader
    // refuses cycles elsewhere, and the registry must not drop handlers over
    // one. Leftovers land last, lexically (`outstanding` is a map, so this
    // loop is already in module-path order).
    for (const auto& [m, n] : outstanding)
        if (n > 0) order.push_back(m);

    std::map<std::string, std::size_t> rank;
    for (std::size_t i = 0; i < order.size(); ++i) rank[order[i]] = i;

    std::vector<HandlerRecord> out;
    for (const auto& r : records_)
        if (r.event == event) out.push_back(r);
    std::stable_sort(out.begin(), out.end(), [&](const HandlerRecord& a, const HandlerRecord& b) {
        const std::size_t ra = rank.count(a.module) ? rank[a.module] : order.size();
        const std::size_t rb = rank.count(b.module) ? rank[b.module] : order.size();
        if (ra != rb) return ra < rb;
        return a.decl_index < b.decl_index;
    });
    return out;
}

void EventRegistry::arm(const std::string& handler) {
    if (!isArmed(handler)) armed_.push_back(handler);
}

bool EventRegistry::isArmed(const std::string& handler) const {
    return std::find(armed_.begin(), armed_.end(), handler) != armed_.end();
}

std::vector<std::string> EventRegistry::armed() const { return armed_; }

void EventRegistry::clear() {
    records_.clear();
    imports_.clear();
    armed_.clear();
}

bool isEnableCall(const MethodCall& node) {
    if (node.method_name != "enable") return false;
    const auto* mid = dynamic_cast<const MemberAccess*>(node.object.get());
    if (!mid || mid->member != "events") return false;
    const auto* root = dynamic_cast<const Identifier*>(mid->object.get());
    return root && root->name == "compiler";
}

namespace {

// The Collect walker: every attribute's parent is whatever node the traversal
// holds underneath it, so no declaration shape is enumerated. A `#[on]` that
// is malformed or unknown is skipped -- attribute validation (W2) owns that
// diagnostic and it must fire exactly once.
class Collector : public StructuralWalk {
public:
    Collector(const std::string& module, EventRegistry& registry, DiagReporter report)
        : module_(module), registry_(registry), report_(std::move(report)) {}

protected:
    bool enter(ASTNode& node) override {
        stack_.push_back(&node);
        if (node.kind() == NodeKind::Attribute) {
            auto& attr = static_cast<Attribute&>(node);
            if (attr.name == "on" && !attr.is_flag && !attr.value_str.empty() &&
                isKnownEvent(attr.value_str))
                collect(attr);
        }
        return true;
    }

    void leave(ASTNode&) override { stack_.pop_back(); }

private:
    void collect(Attribute& attr) {
        ASTNode* parent = stack_.size() >= 2 ? stack_[stack_.size() - 2] : nullptr;
        auto* special = parent ? dynamic_cast<SpecialDeclaration*>(parent) : nullptr;
        if (!special) {
            const char* where = parent ? nodeKindName(parent->kind()) : "Unknown";
            report_(attr, "#[on('" + attr.value_str +
                               "')] declares an event handler, which must be a @special "
                               "function (found on " +
                               where + ")");
            return;
        }
        HandlerRecord record;
        record.event = attr.value_str;
        record.handler = special->name;
        record.module = module_;
        record.decl_index = next_index_++;
        record.param_count = static_cast<int>(special->params.size());
        record.returns_quote = special->return_type && special->return_type->name == "quote";
        registry_.add(std::move(record));
    }

    const std::string module_;
    EventRegistry& registry_;
    DiagReporter report_;
    std::vector<ASTNode*> stack_;
    std::size_t next_index_ = 0;
};

// The Arm walker: every `compiler.events.enable` spelling not at top level is
// refused. Top-level ones never reach it -- the caller matches them by pointer.
class ArmFinder : public StructuralWalk {
public:
    ArmFinder(const std::set<const MethodCall*>& topLevel, DiagReporter report)
        : topLevel_(topLevel), report_(std::move(report)) {}

protected:
    bool enter(ASTNode& node) override {
        if (node.kind() == NodeKind::MethodCall) {
            auto& call = static_cast<MethodCall&>(node);
            if (isEnableCall(call) && !topLevel_.count(&call))
                report_(call, "'compiler.events.enable' is legal only as a top-level "
                              "statement (the arm phase): it cannot be called from "
                              "inside a function or handler body");
        }
        return true;
    }

private:
    const std::set<const MethodCall*>& topLevel_;
    DiagReporter report_;
};

} // namespace

void collectProgramHandlers(Program& program, const std::string& modulePath,
                            EventRegistry& registry, DiagReporter report) {
    Collector collector(modulePath, registry, std::move(report));
    collector.walkAll(program.statements);
}

void armProgramHandlers(Program& program, EventRegistry& registry, DiagReporter report) {
    // Top level first: direct ExpressionStatement children of the program. Each
    // one is consumed whatever it says -- a valid enable arms, an invalid one
    // was just diagnosed -- so the main walk never sees an enable and the
    // backend never lowers one.
    std::set<const MethodCall*> topLevel;
    std::vector<std::size_t> consumed;
    for (std::size_t i = 0; i < program.statements.size(); ++i) {
        const auto* stmt = dynamic_cast<const ExpressionStatement*>(program.statements[i].get());
        const auto* call = stmt ? dynamic_cast<const MethodCall*>(stmt->expr.get()) : nullptr;
        if (!call || !isEnableCall(*call)) continue;
        topLevel.insert(call);

        if (call->args.size() != 1) {
            report(*program.statements[i],
                   "'compiler.events.enable' takes exactly one argument: the handler to arm");
            consumed.push_back(i);
            continue;
        }
        const auto* target = dynamic_cast<const Identifier*>(call->args[0].get());
        if (!target) {
            report(*call->args[0], "'compiler.events.enable' takes the handler itself: "
                                   "write enable(h), not enable(\"h\")");
            consumed.push_back(i);
            continue;
        }
        if (!registry.hasHandler(target->name)) {
            report(*call->args[0],
                   "'compiler.events.enable' names no handler '" + target->name +
                       "': declare one with #[on(<event>)] on a @special function first");
            consumed.push_back(i);
            continue;
        }
        registry.arm(target->name);
        consumed.push_back(i);
    }

    ArmFinder finder(topLevel, report);
    finder.walkAll(program.statements);

    std::sort(consumed.begin(), consumed.end(), std::greater<std::size_t>());
    for (const auto i : consumed)
        program.statements.erase(program.statements.begin() + static_cast<long>(i));
}

} // namespace fin::events
