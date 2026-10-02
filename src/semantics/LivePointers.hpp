#pragma once

#include <string>
#include <vector>

#include "../types/Layout.hpp"
#include "../types/Type.hpp"
#include "MovedAnalysis.hpp"

namespace fin {
namespace scopes {

// Wave-4 step 18 (docs/compiler-api.md §2.5, §3.2), W9:
// `compiler.scopes.live_pointers_quote` — the projection that makes a shadow
// stack expressible without loops: at a point, the set of live pointers as a
// quote.
//
// The compiler does the walking so the handler needs no loop (Q4): the whole
// set arrives as literal syntax, an array literal of addresses. The
// interpretability line is HELD — a handler that loops over the answer is
// refused by the existing control-flow check, whatever it calls.
//
// Point identity is the payload: in Fin the operation takes no arguments
// (the current event point is ambient); here the point travels explicitly
// beside the answer, so attribution (step 19) can name the handler, the
// event and the point, never a bare quote.

// Which event point the answer was computed for. Ambient at the Fin call
// site, explicit here.
struct LivePoint {
    std::string event;
    std::string detail;
    int line = -1;
};

// One binding in scope at the point: its name, its resolved type, and its
// moved state there (events::MovedAnalysis owns the lattice and the joins;
// this file only reads the outcome).
struct LiveBinding {
    std::string name;
    TypePtr type;
    events::MovedState moved = events::MovedState::Live;
};

// Whether `type` holds a pointer the collector must trace. Answered by W4's
// LayoutEngine::pointerCount — referenced, never re-walked here, so offsets
// and pointee types have exactly one owner. A refused layout (`any`,
// interfaces, dynamic arrays, ...) is NOT pointer-containing: a refused map
// is not a root, and guessing "pointer" would be D's all-zero-bitmap defect
// in a new spelling.
bool isPointerContaining(const TypePtr& type, LayoutEngine& engine);

// The live pointer set at a point, in binding order: pointer-containing
// bindings whose moved state is not Moved.
//
// MovedMaybe is INCLUDED, conservatively, and that direction is the rule:
// excluding a maybe-moved root risks freeing a live object (unsafe), while
// including a dead one only pins memory (safe for a collector). Moved-from
// is not live and is dropped.
std::vector<LiveBinding> livePointers(const std::vector<LiveBinding>& bindings,
                                      LayoutEngine& engine);

// The quote projection of the live set: `"quote {}"` when none,
// `"quote { [&a, &b]; }"` otherwise, in binding order. Addresses, not
// values: the quote names the roots; W4's pointer maps describe the
// interiors. Building the address nodes (rather than this source form) and
// evaluating the call inside a handler wait on the comptime interpreter
// (ADR 0006) — the same gap W5's firing reports for every quote-returning
// call, never a silent skip.
struct LiveAnswer {
    LivePoint point;
    std::vector<std::string> names;
    std::string quote;
};

LiveAnswer livePointersQuote(const LivePoint& point,
                             const std::vector<LiveBinding>& bindings,
                             LayoutEngine& engine);

}  // namespace scopes
}  // namespace fin
