#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace fin {

class ASTNode;
class MethodCall;
class Program;

namespace events {

// The event set of docs/compiler-api.md §3.2. The single source of truth for
// which event names exist: the analyzer's attribute validation answers from
// here, so collection can reuse the answer instead of restating the list.
bool isKnownEvent(const std::string& name);

// One `#[on(event)]` declaration: the event it subscribes and the `@special`
// function that handles it, with where it was declared. `param_count` and
// `returns_quote` are recorded now and checked later: whether the handler's
// signature matches its event's payload is the firing slice's check, when
// payloads exist to check against. Until then a mismatch is carried, not
// refused.
struct HandlerRecord {
    std::string event;
    std::string handler;
    std::string module;
    std::size_t decl_index = 0;
    int param_count = 0;
    bool returns_quote = false;
};

// The subscription table of docs/compiler-api.md §3.4 (Collect) plus the armed
// set (Arm). Keyed by event name; several handlers may share one event, and an
// identical registration (same event, handler and module) is one entry, so a
// later firing pass can never double-fire it.
//
// Ordering (Q10, ADR 0007 as corrected): dependencies before importers,
// ties broken by module path string, then declaration order within a module.
// `recordImport` feeds the DAG; `orderedHandlers` answers in that order.
// Cross-module wiring -- filling one registry from many modules with loader-
// canonical paths -- is the firing slice's seam; until then each analyzer
// collects its own module and the ordering is proven at this level.
class EventRegistry {
public:
    void add(HandlerRecord record);
    bool hasHandler(const std::string& handler) const;
    std::vector<HandlerRecord> handlers(const std::string& event) const;
    std::vector<HandlerRecord> all() const;
    std::size_t size() const;

    void recordImport(const std::string& importer, const std::string& imported);
    std::vector<HandlerRecord> orderedHandlers(const std::string& event) const;

    void arm(const std::string& handler);
    bool isArmed(const std::string& handler) const;
    std::vector<std::string> armed() const;

    void clear();

private:
    std::vector<HandlerRecord> records_;
    std::vector<std::pair<std::string, std::string>> imports_;
    std::vector<std::string> armed_;
};

// Whether `node` is spelled `compiler.events.enable(...)`. Syntactic: the Arm
// phase reads it off the tree before analysis resolves anything, which is what
// lets arming stay legal with no grant attached (a top-level statement has no
// declaration to carry `#[use(...)]`).
bool isEnableCall(const MethodCall& node);

using DiagReporter = std::function<void(ASTNode&, const std::string&)>;

// Collect: gather this program's `#[on(...)]` handlers into the registry.
// Runs before any body is analysed, so the table exists before anything that
// could fire it (the Nim guard of §3.4). A well-formed known event on anything
// but a @special is refused here; a malformed or unknown one is left alone --
// attribute validation (W2) owns that diagnostic and it must fire exactly once.
void collectProgramHandlers(Program& program, const std::string& modulePath,
                            EventRegistry& registry, DiagReporter report);

// Arm: resolve the enable set from top-level `compiler.events.enable(h)`
// statements and consume those statements from the tree (arming is a
// compile-time effect; the backend has no lowering for it). An enable anywhere
// else -- inside a function, a handler, a block -- is refused: `enable` is
// legal only at top level. Invalid top-level enables are diagnosed and
// consumed alike, so one bad line is one diagnostic, never a cascade.
void armProgramHandlers(Program& program, EventRegistry& registry, DiagReporter report);

} // namespace events
} // namespace fin
