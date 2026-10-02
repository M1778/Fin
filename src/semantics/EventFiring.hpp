#pragma once

#include <set>
#include <string>
#include <vector>

#include "EventPayloads.hpp"
#include "EventRegistry.hpp"

namespace fin {
class Program;
class SpecialDeclaration;
class Statement;
class StructDeclaration;
class VariableDeclaration;

namespace events {

// Wave-4 step 17 (docs/compiler-api.md §3.1-§3.2), W5 floor: the firing loop
// plus `struct_layout_finalised` and `variable_declared`.
//
// W6 (parallel) owns function_entry/exit, assignment, allocation_site/
// delete_site: this table must not answer for those, or two match checks
// would own one event and diagnose it twice (the same rule W6's table
// follows towards these two rows).

// The W5 payload rows (§3.2 transcribed): `struct_layout_finalised` carries
// the struct identity, `variable_declared` the name, the type and the
// mutability. A payload change lands in this one table; the match check
// answers from these rows so the diagnostic follows without a second
// transcription to drift from it.
const std::vector<EventPayload>& w5Payloads();
const EventPayload* findW5Payload(const std::string& event);
bool isW5Event(const std::string& event);

// The match check for W5's events (§3.8: caught before anything runs, naming
// expected and actual). Runs in visit(Program) right after Collect/Arm, over
// this module's handlers, W5 events only. A handler matches when its arity,
// its parameter type spellings and its return spelling are the payload's;
// the spelling rule is W6's (base name, no structure, `const` ignored).
//
// The interpretability line is held in the same pass (owner decision): five
// statement forms, no control flow, quote-returning calls + splice only. A
// handler that exceeds it is refused by name, whatever its signature says.
// `blame` and `return` stay legal: §3.8 needs the first and every handler
// needs the second.
//
// Returns the refused handler names, so the firing pass skips them without
// diagnosing twice: one mismatch is one diagnostic, never a check-then-fire
// cascade.
std::set<std::string> checkW5HandlerPayloads(Program& program, const EventRegistry& registry,
                                             DiagReporter report);

// One W5 event point, as recorded. `line` is the source line; the human half
// of the payload travels beside the anchor because the diagnostic at fire
// time names the point: the struct name, or `name:type` for a declaration.
struct W5FirePoint {
    std::string event;
    const VariableDeclaration* varDecl = nullptr;
    const StructDeclaration* structDecl = nullptr;
    std::string structName;
    std::string varName;
    std::string varType;
    bool varMutable = false;
    int line = -1;
};

// One handler run, in order: the test hook and what W6's five events consume
// this loop through. `detail` is the point's human half (struct name,
// `name:type`), so ordering and identity are assertable without re-deriving
// where the points are.
struct FiredHandler {
    std::string event;
    std::string handler;
    std::string detail;
};

// Wave-4 step 19: one spliced batch, as inserted. The analyzer's post-splice
// check walks `inserted` with this identity active, so a diagnostic in
// generated code names the handler that wrote it and the event point that
// fired it. `anchor` is the declaration the batch follows; `line` the event
// point's source line. Raw pointers: statements are heap-owned by the tree,
// so splice moves never relocate them.
struct InjectedChunk {
    const Statement* anchor = nullptr;
    std::string event;
    std::string handler;
    std::string detail;
    int line = -1;
    std::vector<Statement*> inserted;
};

// Fire points through the loop: for each point, each armed handler in
// orderedHandlers sequence (Q10). Per handler: the recorded signature verdict
// is honoured (a refused handler is skipped, never silently and never twice),
// the straight-line body is invoked, and the returned quote is spliced at
// the event point (variable_declared) or refused when the event takes no
// injection (struct_layout_finalised: the only legal answer is empty).
//
// There is no comptime interpreter yet (ADR 0006): evaluation covers an empty
// body (empty quote), a single `return quote { ... }` (that quote), and
// `blame` (§3.8 abort). Anything else within the line is a named diagnostic
// reporting the interpreter gap precisely -- never a silent skip, never a
// guessed value. Injected code does not fire events: only the given points
// run, so spliced statements are never re-entered.
//
// Callable inline at an event point or once for a deferred batch; each call
// fires exactly the points it is given.
//
// `warnReport` and `noteReport` carry `compiler.diag.warning` and
// `compiler.diag.note` calls a handler executes; both default to empty, in
// which case a warning reports through `report` as an error and a note
// through the warning — a severity the caller did not ask for is still
// better than a silent one, and both analyzer call sites pass all three.
// `chunks` receives one entry per spliced batch, in insertion order, for the
// post-splice check; null skips the recording and changes nothing else.
std::vector<FiredHandler> fireW5Events(Program& program, const EventRegistry& registry,
                                       const std::vector<W5FirePoint>& points,
                                       const std::set<std::string>& refused,
                                       DiagReporter report,
                                       DiagReporter warnReport = DiagReporter(),
                                       DiagReporter noteReport = DiagReporter(),
                                       std::vector<InjectedChunk>* chunks = nullptr);

} // namespace events
} // namespace fin
