# Provenance for escaping references: literals, stores, and forwarding calls

## Context

ADR 0055's rule reads only `return`'s immediate operand: an identifier, a member
path, or an array element rooted at one names storage; a call, `new`, or a
literal names none and is not asked about. Issue #109 measured four ways a
frame-local address leaves the frame without ever presenting an operand of that
shape, and all four print whatever the reused frame slot was given next
(`4243` after a clobbering call in the issue's reproduction):

| shape | spelling | why the 0055 check misses it |
|---|---|---|
| struct-literal field | `return W { p: &d };` | `returnedStorage` answers `{}` for a literal — no root to ask |
| collection literal | `return [&d];` | same, for an array literal |
| out-parameter store | `*out = &d;` | not a return, so no `ReturnStatement` is visited |
| global store | `g = &d;` | same, and 0055 "does not track lifetimes across assignments" |
| call wrapper | `fun id(p: &int) <&int> { return p; }` then `return id(&a);` | a call "names none, and is not asked about", and `id` alone is a legal forward |

Three same-frame spellings pin what a construction-time refusal would have
broken: `Holder{p: &x}` built into a local, `[&x, &y]` built into a local, and a
linked `next: &tail`. The defect had to move out of the expression pass — where
a literal cannot know whether it flows out — and into the places that can see
flow: the returned literal's own site, the store statement, and the call the
frame leaves through.

## Decision

The unit of ownership stays the same and the corners of it are closed by three
local checks and one interprocedural summary, all running where flow decides:

**1. A returned literal of reference fields is walked.** A `return` whose value
is a struct literal or an array literal has its initialiser tree walked — the
same walk `ptrProvReferent` already does for one `&` operand — and each field or
element is held to the ADR 0055 test: exempt while it is a global's,
`#[slaveof($Fin)]`-pinned's, or the receiver's storage, or an address reached
through a pointer; refused with the existing wording, blamed on the field or
element expression, when it names this frame's storage. A pointer field
initialised from a tracked pointer local faults the same way `#105` faults
`return p` — provenance, not declared type. Same-frame constructions
(`Holder{p: &x}` into a local) never reach this walk, which is exactly what the
three pins require.

**2. A store that installs a frame-local address in storage that outlives the
frame is refused.** Two left-hand shapes are owned by this rule: a store through
a dereference (`*out = &d`, the out-parameter route) and a store into a global
(`g = &d`). The right-hand side must be handing out this frame's storage — an
`&local`, or a pointer local whose recorded provenance derives from one — and
the receipt must outlive the frame (a derefenced parameter's storage does; a
pointer local whose provenance derives from a local — both slots die together —
does not, and stays legal). Wording stays in the family: `cannot store the
address of local variable '<name>'...`, blamed on the right-hand side. Field
stores (`h.p = &d`), array-element stores, and nested-deref targets (`**slot =
&d`) stay open and keep the wording honest about what is covered.

**3. A forwarding call is asked to answer for its arguments.** `return
id(&a)` names no root, so the site asks a per-function provenance summary. Each
reference-returning function computes which of its expressions answer "the
returned reference carries the storage named by parameter *i*": a bare
`return p;` where `p` is a pointer or interface parameter, and by composition a
forwarded call whose own summary names a parameter fed from this one's
parameter. The summary fixes the frame-level truth first: it faults a caller
whose argument *i* carries this frame's address into a callee that forwards it,
for blame (`cannot return the address of local variable 'a'`) on the argument
expression at the call site, and keeps an argument that is itself a pointer
parameter, a global, or `new`-owned storage legal. Summaries are computed once
before any body is walked, from top-level declarations and the hoisted
signatures already resolved there, and are re-derived to a fixed point over the
call graph, so `id`, `wrap(q) { return id(q) }` and their mutual-recursive
forwards all converge regardless of file order.

Convergence is by bounds, not by faith: iterate over re-derivations of the
facts until no summary changes or the function count is exhausted, and skip any
body whose parameter list is shadowed (permissive grammar) or whose signature
did not hoist — the skip is an under-refusal, never an over-refusal.

**4. The refusal wording keeps naming the route.** The store wording is new
because the route is new; the literal and call routes reuse ADR 0055's, since
the storage they hand out is the same storage and the remedy does not depend on
how it reached the return.

## What stays known-dangling

- **A struct local returned whole.** `let w <W> = W{p: &d}; return w;` reaches
  `returnedStorage` as a root of a value struct, is refused by nothing, and
  still dangles. Closing it is field-level provenance for struct locals, a
  wider alias-analysis commitment than this ADR takes on.
- **Laundering through untracked locals** — a pointer local written inside one
  branch, a `&frame` copied by a construct the provenance pass does not record
  (array literals `[&d]` assigned into a local's tracked value, lambda captures
  beyond the direct-identifier case `#105` covers) — stays as measured by
  `ptrProvReferent`'s ceiling. The branch-renaming counterflows in the same
  direction: whatever the walk cannot pin stays accepted.
- **Method-resolved forwarding** (`obj.forward(arg)`, `S::fwd(arg)`): the
  summary pass gathers free top-level declarations; a bare call's callee is the
  free function of that name if the summary map has it, and a method forward
  stays accepted and documented.
- **Cross-module calls** stay accepted: a summary is this module's, and a
  callee's provenance is a translation-unit right, not a loader-owned one.
- **Function-argument positions beyond the summary's map**: `f(x, &a)` faults
  exactly the positions its callee answered about; defaults-filled or trailing
  positions the argument list does not carry are skipped, safe in both
  directions.

## Consequences

The 3-arg `error` contract holds everywhere: each refusal names the local whose
slot dangles, blames the expression that carries the address out, and keeps the
`= help:` row — return caller-owned storage or an owned heap allocation
(`new`) instead; this slot ends with the call (stores require storing caller-
owned storage instead).

Existing pins from ADR 0055 must keep their behavior: a global, `#[slaveof($Fin)]` storage, the receiver, a pointer parameter's referent, and a
forwarded interface pair all stay legal. `tests/samples/pointer_escape.fin`
stays the specification.

The known-defect posture inverts:
`KnownDefect_Codegen.ALiteralCarryingALocalAddressEscapes` becomes a soundness
test alongside the route pins in `tests/test_semantics.cpp`.

Revisit with an ownership model, the point at which every check here is the
special case of a general rule.

## References

- Issue #109 (this closes it), #105 (the aliasing ride-along), #99 (ADR 0055's
  original defect).
- ADR 0055 (the return-operand rule and its legal-form pins), ADR 0003
  (memory management in the library), ADR 0008 (samples are the spec).
- `refuseEscapingReturn`, `notePtrProvenance`, `ptrProvReferent`,
  `refuseEscapingStore`, and `buildReferenceProvenance` in
  `src/semantics/impl/Analyzer_Stmt.cpp`; the prepass call at
  `SemanticAnalyzer::visit(Program&)` in `src/semantics/impl/Analyzer_Core.cpp`.
- `tests/test_semantics.cpp`: `Refuses*AtAStructLiteralField`,
  `...AtAnArrayLiteralElement`, `...ThroughAnOutParameterStore`,
  `...IntoAGlobalStore`, `...ThroughAForwardingCall`, and inverted controls
  beside the ADR 0055 pins.
- `tests/test_codegen.cpp`: `Soundness_Codegen.ALiteralCarryingALocalAddressIsRefusedAtItsReturnSite`,
  inverted from `KnownDefect_Codegen.ALiteralCarryingALocalAddressEscapes`.
- `docs/guide/09-arrays-and-pointers.md`: the issue's known-dangling paragraph
  narrows to what stays open as statuses above.
