#pragma once

#include <set>
#include <string>
#include <vector>

#include "EventPayloads.hpp"
#include "EventRegistry.hpp"

namespace fin {

class Block;
class Program;
class SpecialDeclaration;
class Statement;

namespace events {

// Wave-4 step 20 (docs/compiler-api.md §3.2), W10: `loop_back_edge` — the LAST
// floor event (owner Q9: floor-last). A tracing collector needs safepoints: a
// call-free hot loop otherwise runs unboundedly without reaching one.
//
// Ownership: this file owns the back-edge payload row with its match check,
// the latch points and their firing. No older table answers for this row, or
// two match checks would own one event and diagnose it twice. W9 (parallel)
// owns live_pointers_quote — nothing here touches scopes.
//
// The language has no `loop` statement (checked against NodeKind and the
// grammar): `for`, `while` (including `do-while`), and `foreach` are the
// whole set. One static latch point is recorded per loop statement, with the
// nesting depth (1 = outermost, function-local: entering a function or lambda
// resets the count, so a loop inside a nested body starts at 1 again).
//
// The event fires per static edge, not per iteration: an infinite-loop
// program still terminates analysis, because recording one point per loop
// statement is a syntactic walk, never an execution of the loop. The injected
// quote lands at the loop header (prepended to the body block), so it runs
// each iteration at run time — including past a `continue`, which is what
// makes it a safepoint rather than a tail marker. Injected code does not fire
// events (§3.3): only the recorded points run, so spliced statements are
// never re-entered.

// --- The payload row ---------------------------------------------------------
// docs/compiler-api.md §3.2: (depth: int) <quote>. At the jump back to a loop
// header: the latch point of each `for`/`while`/`foreach`.
const EventPayload& w10Payload();
const EventPayload* findW10Payload(const std::string& event);
bool isW10Event(const std::string& event);

// The match check for W10's event (§3.8: caught before anything runs, naming
// expected and actual). Runs in visit(Program) right after Collect/Arm, over
// this module's handlers, this event only.
//
// A handler matches when its arity, its parameter type spellings and its
// return spelling are the payload's; the spelling rule is W6's (base name,
// no structure, `const` ignored). The interpretability line is held in the
// same pass (owner decision): a handler whose body holds control flow
// (if/while/for/foreach) is refused by name, whatever its signature says.
//
// Returns the refused handler names, so the firing pass skips them without
// diagnosing twice: one mismatch is one diagnostic, never a check-then-fire
// cascade.
std::set<std::string> checkW10HandlerPayloads(Program& program, const EventRegistry& registry,
                                              DiagReporter report);

// --- Back-edge points --------------------------------------------------------
// One loop's latch, as analysed. `kind` is the loop form ("while",
// "do-while", "for", "foreach"); `line` its source line; `depth` the
// function-local nesting depth. `body` is the loop's body block: the anchor
// a handler's quote is prepended to (the loop header). Null when the loop
// has no body to anchor to — the handler still runs and is logged, but
// nothing is injected.
struct LoopBackEdgePoint {
    std::string event = "loop_back_edge";
    std::string kind;
    int line = -1;
    int depth = 0;
    Block* body = nullptr;
};

// One handler run, in order: the test hook. `detail` is
// `<kind>@<line>:depth=<depth>` (e.g. `while@12:depth=1`), so loop identity
// and the payload are assertable without re-deriving where the points are.
struct W10FiredHandler {
    std::string event;
    std::string handler;
    std::string detail;
};

std::string w10PointDetail(const LoopBackEdgePoint& point);

// Fire points through the loop: for each point, each armed handler in
// orderedHandlers sequence (Q10). Per handler: the recorded signature
// verdict is honoured (a refused handler is skipped, never silently and
// never twice), the straight-line body is invoked -- lets, bare calls and
// the depth parameter thread with the latch point's depth bound (ADR 0006,
// first step); the empty, quote-literal and blame paths read exactly as
// before -- and the returned quote is prepended to the loop body (the
// header safepoint).
//
// Anything wider than the line is a named diagnostic reporting the
// interpreter gap precisely — never a silent skip, never a guessed value.
// A `compiler.diag.error` call in the handler reports (naming handler,
// event and point) and suppresses that point's injection; `warning`/`note`
// report through their own reporters and the quote still splices — the W7
// parity (test_events_w7.cpp, W7Diag).
//
// Callable once for a deferred batch; each call fires exactly the points it
// is given. Points from nested loops share no anchor (each body is its own
// block), so firing order never shifts another point's insert site.
//
// `warnReport` and `noteReport` carry `compiler.diag.warning` and
// `compiler.diag.note` calls a handler executes; both default to empty, in
// which case a warning reports through `report` as an error and a note
// through the warning — a severity the caller did not ask for is still
// better than a silent one, and the analyzer call site passes all three.
std::vector<W10FiredHandler> fireW10Events(Program& program, const EventRegistry& registry,
                                           const std::vector<LoopBackEdgePoint>& points,
                                           const std::set<std::string>& refused,
                                           DiagReporter report,
                                           DiagReporter warnReport = DiagReporter(),
                                           DiagReporter noteReport = DiagReporter());

} // namespace events
} // namespace fin
