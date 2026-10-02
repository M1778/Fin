#pragma once

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "EventPayloads.hpp"
#include "EventRegistry.hpp"

namespace fin {

class ASTNode;
class Block;
class Program;
class SpecialDeclaration;
class Statement;
class StructType;

namespace events {

// Wave-4 step 17 (docs/compiler-api.md §3.2), W7: `variable_scope_exit` plus
// the moved analysis, including MovedMaybe.
//
// There are no moves in the language to consult (ADR 0030: every binding
// copies). The move spelling is the owner's `@move()` (docs/plan.md): a
// builtin special call marking its operand moved and lowering as the
// identity. When the `move_or_copy` protocol lands (wave 5), this analysis is
// what feeds its moved-from case; until then every value still destroys
// independently and the backend skips nothing.
//
// Ownership: this file owns the moved-state lattice, the per-function
// tracking, the `variable_scope_exit` payload row with its match check, the
// scope-exit points and their firing. W5's table must not answer for this
// row (and W6's must not either), or two match checks would own one event
// and diagnose it twice.

// --- Payload integers ------------------------------------------------------
// docs/compiler-api.md §2.4 files ExitNormal/ExitBlamed and
// MovedYes/MovedNo/MovedMaybe under compiler.events.*. The component table
// (CompilerApi.cpp) carries their names so a handler can spell them; the
// integers are pinned here because the table carries no values.
constexpr int kExitNormal = 0;
constexpr int kExitBlamed = 1;
constexpr int kMovedNo = 0;
constexpr int kMovedYes = 1;
constexpr int kMovedMaybe = 2;

// --- The moved-state lattice ------------------------------------------------
// Per-variable state across rebindings: live, moved, or moved on some paths
// only (MovedMaybe). The join is the least upper bound: agreement holds,
// disagreement becomes Maybe.
enum class MovedState { Live, Moved, Maybe };

MovedState joinMoved(MovedState a, MovedState b);
// The payload integer for a state (kMovedNo/kMovedYes/kMovedMaybe).
int movedValue(MovedState s);
// The fired-log spelling ("live"/"moved"/"maybe").
const char* movedName(MovedState s);

// --- The payload row ---------------------------------------------------------
// docs/compiler-api.md §3.2: (name: string, t: $type, exit_kind: int,
// moved: int) <quote>. At each point control leaves the binding's scope:
// every return, break, continue, fallthrough, and blame unwind. Once per
// exit path, not once per binding.
const EventPayload& w7Payload();
const EventPayload* findW7Payload(const std::string& event);
bool isW7Event(const std::string& event);

// The match check for W7's event (§3.8: caught before anything runs, naming
// expected and actual). Runs in visit(Program) right after Collect/Arm, over
// this module's handlers, this event only.
//
// A handler matches when its arity, its parameter type spellings and its
// return spelling are the payload's; the spelling rule is W6's (base name,
// no structure, `const` ignored). The interpretability line is held in the
// same pass (owner decision): a handler whose body holds control flow
// (if/while/for/foreach) is refused by name, whatever its signature says.
// That refusal is also what keeps handlers from walking the field tree
// (Q4/Q14: composition means they must NOT walk -- the language handles
// nesting, and walking needs a loop).
//
// Returns the refused handler names, so the firing pass skips them without
// diagnosing twice: one mismatch is one diagnostic, never a check-then-fire
// cascade.
std::set<std::string> checkW7HandlerPayloads(Program& program, const EventRegistry& registry,
                                             DiagReporter report);

// --- Scope-exit points --------------------------------------------------------
// One variable leaving scope on one path, as analysed. `exitKind` is
// kExitNormal/kExitBlamed; `moved` the variable's state on that path. The
// anchor says where a handler's quote lands: before `beforeStmt` for jumps
// (return/break/continue/blame), appended to `appendBlock` for fallthrough,
// or after `afterStmt` for loop header scopes (which end when the loop
// does). Exactly one of the three is set.
struct ScopeExitPoint {
    std::string varName;
    std::string varType;
    int exitKind = kExitNormal;
    MovedState moved = MovedState::Live;
    int line = -1;
    const Statement* beforeStmt = nullptr;
    const Block* appendBlock = nullptr;
    const Statement* afterStmt = nullptr;
};

// One handler run, in order: the test hook. `detail` is
// `name:type:exit=<normal|blamed>:moved=<live|moved|maybe>`, so identity,
// exit kind and moved state are assertable without re-deriving the points.
struct W7FiredHandler {
    std::string event;
    std::string handler;
    std::string detail;
};

std::string w7PointDetail(const ScopeExitPoint& point);

// Fire points through the loop: for each point, each armed handler in
// orderedHandlers sequence (Q10). Per handler: the recorded signature
// verdict is honoured (a refused handler is skipped, never silently and
// never twice), the straight-line body is invoked -- lets, bare calls and
// parameters thread with the point's values bound (ADR 0006, first step);
// the empty, quote-literal and blame paths read exactly as before -- and
// the returned quote is spliced at the exit point.
//
// Anything wider than the line is a named diagnostic reporting the
// interpreter gap precisely -- never a silent skip, never a guessed value.
// Injected code does not fire events: only the given points run, so spliced
// statements are never re-entered.
//
// Callable once for a deferred batch; each call fires exactly the points it
// is given. Fires once per outer variable: nested cleanup is the language's
// own work (Q14), so points never expand a variable into its fields.
std::vector<W7FiredHandler> fireW7Events(Program& program, const EventRegistry& registry,
                                         const std::vector<ScopeExitPoint>& points,
                                         const std::set<std::string>& refused,
                                         DiagReporter report);

// --- The analysis ---------------------------------------------------------------
// Per-function moved tracking, ridden on the main analyzer walk: the
// analyzer calls these one-liners at the statements it already visits, and
// all state lives here. Frames mirror the analyzer's scopes; markers mirror
// loop boundaries. Everything no-ops on an empty stack, so bodies the
// analyzer walks without a function frame (constructors, destructors,
// operators) still get correct block-level tracking, and globals (which
// never go out of scope) are never tracked.
//
// Diagnostics only ever name `@move` and reads of moved variables, neither
// of which any corpus program writes: recording points is unconditional,
// reporting is not, so an unarmed program compiles exactly as before.
class MovedAnalysis {
public:
    // A function boundary: pushes the function frame holding the parameters
    // (each declared Live through declare()). Returns pair with blame...
    void enterFunction();
    // At function end: records fallthrough exits for the function frame when
    // `fallsThrough` (which mirrors W6's function_exit enumeration: only a
    // body that can fall off the end has a fallthrough path), appended to
    // `body`, then pops the frame.
    void exitFunction(Block& body, bool fallsThrough, DiagReporter report);

    // A bare block (ADR 0011: a bare brace opens a scope). The body of a
    // function goes through here too, which is what records body locals at
    // function fallthrough; the function frame itself holds only parameters.
    void enterBlock();
    // Records fallthrough exits (ExitNormal) for the block frame's
    // variables, appended to `block`, then pops the frame. Every syntactic
    // block end records, regardless of reachability: reachability is a
    // separate analysis, and injected code on an unreachable path never
    // executes.
    void exitBlock(Block& block, DiagReporter report);
    // The same, anchored after `anchor` instead of at a block end: for loop
    // header scopes, which end when the loop does. The quote lands after the
    // loop statement, where the scope ends.
    void exitBlockAfter(const Statement& anchor, DiagReporter report);

    // A loop boundary for break/continue: what those jumps exit is every
    // frame above the innermost loop marker.
    void enterLoop();
    void exitLoop();

    // A lambda boundary: a return inside stops here rather than unwinding
    // the enclosing function, and a break/continue inside binds to no outer
    // loop. The body block still pushes its own frame, so lambda locals get
    // their fallthrough exits; effects on outer variables join as maybe-run.
    void enterLambda();
    void exitLambda();

    // A branch snapshot: an opaque copy of the whole stack (frames and
    // markers). Protocol for `if (c) T else E`: snap = snapshot(); walk T;
    // thenEnd = snapshot(); restore(snap); walk E; installJoin(thenEnd,
    // snapshot()). Without else: installJoin(thenEnd, snap). Loops and
    // conditional expressions follow the same shape.
    struct Snapshot {
        struct Frame {
            // Variable -> state, in declaration order.
            std::vector<std::pair<std::string, MovedState>> vars;
            // Variable -> type spelling, for the payload.
            std::map<std::string, std::string> types;
            enum class Kind { Normal, FuncRoot, FuncMark, LoopMark };
            Kind kind = Kind::Normal;
        };
        std::vector<Frame> frames;
    };
    Snapshot snapshot() const;
    void restore(const Snapshot& snap);
    // Installs the frame-wise join of `a` and `b` (same shape: both taken
    // at the same nesting). Variables declared on only one side keep their
    // side's state on the assumption the other side never declared them.
    void installJoin(const Snapshot& a, const Snapshot& b);

    // Declares a binding Live in the current frame: variable declarations,
    // parameters (including `self`), foreach bindings, catch variables. A
    // redeclaration in the same frame starts Live again. No-ops on an empty
    // stack (globals) or above a bare marker.
    void declare(const std::string& name, const std::string& type);
    // Reads a binding: a definitely-moved variable is a use-after-move
    // diagnostic naming it. Maybe-moved stays readable: the payload carries
    // the doubt. Untracked names are not variables and never report.
    void use(const std::string& name, ASTNode& at, DiagReporter report);
    // Rebinds a binding to Live: W6's assignment points are the rebinding
    // sites (`x = e` only -- a member or index write does not rebind the
    // base). Reading the old value is the caller's check, not this one's.
    void assign(const std::string& name);
    // Moves a binding: Live becomes Moved, Maybe stays Moved (the move runs
    // on the live path and is a no-op on the moved one). Already-Moved is
    // left alone: the operand's own read already reported it.
    void move(const std::string& name);
    // Suspends the use check for exactly one expression walk: a plain
    // assignment's target rebinds rather than reads. Always paired; nesting
    // counts so an inner walk cannot re-enable an outer suspension.
    void suspendUses();
    void resumeUses();

    // A `return`: every frame up to and including the innermost function
    // boundary unwinds with ExitNormal. Outside any function, records
    // nothing.
    void onReturn(const Statement& anchor, DiagReporter report);
    // A `break`/`continue`: every frame above the innermost loop marker
    // unwinds with ExitNormal. Inside a nested function (or with no loop),
    // records nothing: the jump binds to no loop here.
    void onBreak(const Statement& anchor, DiagReporter report);
    void onContinue(const Statement& anchor, DiagReporter report);
    // A raising `blame`: every frame up to and including the innermost
    // function boundary unwinds with ExitBlamed. The asserting form is not
    // an exit and never reaches here.
    void onBlame(const Statement& anchor, DiagReporter report);

    const std::vector<ScopeExitPoint>& points() const { return points_; }

private:
    std::vector<Snapshot::Frame> frames_;
    std::vector<ScopeExitPoint> points_;
    int useSuspended_ = 0;

    using Kind = Snapshot::Frame::Kind;
    // Nearest frame at or below `top` holding `name`, or nullptr.
    // `top` defaults to the stack top; branch snapshots pass their own.
    Snapshot::Frame* find(const std::string& name, std::size_t top);
    void recordFrame(std::size_t index, int exitKind, const Statement* before,
                     const Block* append, int line, DiagReporter report);
    // Shared by onBreak/onContinue: the jump binds to the innermost loop.
    void onJumpToLoop(const Statement& anchor, DiagReporter report);
};

// Slice 3 (ADR 0016): marks a generated parent destructor observable. When a
// struct with no declared destructor holds a directly-held struct field (or
// a non-interface base) whose type has one, the parent acquires cleanup and
// has_destructor becomes true. Idempotent: a declared destructor already
// sets the flag, and single-level reads compose transitively through visit
// order (every field type is fully visited before its holder).
void propagateComposedDestructor(StructType& type);

} // namespace events
} // namespace fin
