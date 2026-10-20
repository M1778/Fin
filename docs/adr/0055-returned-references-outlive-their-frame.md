# A returned reference must name storage that outlives the call

**Status:** Accepted

## Context

Issue #99 tracked a known defect: the backend accepted an escaping interface
reference the way it accepted any escaping address, and the issue proposed that
"the interface case needs its own rule". Measured at the time, `fun make()
<Speaker> { let d <D> = D { n: 7 }; return d; }` compiled, ran, and printed
garbage; the disassembly was `lea -0x8(%rsp)`, so the pair's `data` word
pointed into the frame `make` had just left.

The test's own header had already recorded why a rule for the interface alone
would be the wrong shape: the second half of the same test was that program
with a plain `&D` in place of the interface, accepted just as happily, and the
header set the condition explicitly — "if the interface half ever refuses while
the pointer half still compiles, the refusal was written in the wrong place."
The defect was never interface-specific. An interface reference is `{data,
vtable}` (ADR 0019, ADR 0027), so converting a local to one takes that local's
address; the two spellings differ in what surrounds the address, not in whose
frame it points at.

Fin has no general escape analysis, and ADR 0003 puts memory management in the
library rather than the compiler, so there is no ownership model to consult.
Wave-5 slice 4 gave `#[slaveof]` a real rule, but only for declarations that
ask for it: an untied reference inherits the general property and does not add
one.

## Decision

A `return` refuses when the returned expression names storage that the
returning frame owns. The question is whose frame owns it, not which type was
written, so one check at the return statement covers both spellings rather
than each type checking its own.

The returned expression names storage when it is an identifier, a member path,
or an array element rooted at one (`d`, `d.n`, `a[0]`). A call, `new`, or a
literal names none, and is not asked about. Storage is the frame's unless one
of these holds:

- it is a global — the storage is the program's, so no call ends it;
- it is pinned with `#[slaveof($Fin)]`, which ties storage to program exit;
- it is the method receiver. `self` is by reference: a method assigning
  `self.n` is visible in the caller, so `&self.n` and `self` returned as an
  interface both name the caller's object. The implicit injection types `self`
  as `Self` or as the struct itself, neither a pointer, so the receiver needs
  saying separately from the clause below;
- it is reached *through* a pointer, as in `&p.n` where `p` is a pointer
  parameter. That dereferences into storage the caller already owns. The
  pointer's own slot is still the frame's, so `&p` is refused.

Two messages, by the spelling that asked: `cannot return the address of local
variable '<name>'` for a returned address, and `cannot return local variable
'<name>' as an interface` for a conversion. Blame falls on the returned
expression. Both carry the same `= help:` row — return caller-owned storage or
an owned heap allocation — because the remedy does not depend on the spelling
and the compiler is sure of it, which is what that row is for.

The constructor convention — a returned pointer accepted where the declared
result is the value type, because `new S{}` is the constructor's storage
operation — waives the *type* check only. `&local` handed back through that
waiver is the same dangle the `<&D>` spelling refuses, and is refused too.

## Consequences

The rule is deliberately shallow: it reads the declared type of the root
symbol, not what that symbol points at. An address laundered through a pointer
local therefore still escapes — `let p <&D> = &d; return p;` and `return &p.n`
both compile, and measured, both dangle, printing `8 8` where the first number
should be `7` because the two calls reuse one frame slot. That is a non-goal
here, tracked as #105 and pinned by
`KnownDefect_Semantics.AnEscapingAddressLaunderedThroughAPointerLocalIsAccepted`.
Closing it needs the referent rather than the declared type: initialiser
provenance for pointer locals, which a reassignment through a branch defeats,
or real alias analysis. Either is an ownership design call, not a slice, and
whatever lands must keep the pointer-parameter case accepted — a rule that
refuses `fun get(p: &D) <&int> { return &p.n; }` has moved the defect, not
closed it.

This is not a borrow checker and does not become one. It refuses the shapes
that name a frame's own storage outright; it does not track lifetimes across
assignments, through containers, or into closures. `self` is exempt by name,
which a local that shadows `self` inside a method body would inherit — an
under-refusal, and the safer direction of the two.

Revisit if Fin takes on an ownership model, at which point this check becomes
the special case of a general rule rather than the whole of it.

## References

- Issue #99 (the defect this closes), issue #105 (the aliasing residue).
- ADR 0003 (memory management is a library), ADR 0019 and ADR 0027 (an
  interface reference is `{data, vtable}`), ADR 0008 (samples are the
  specification).
- `visit(ReturnStatement&)` and `refuseEscapingReturn`,
  `src/semantics/impl/Analyzer_Stmt.cpp`.
- `tests/samples/pointer_escape.fin` — both refused spellings, the storage
  that stays legal, and the struct-result waiver.
- `tests/test_semantics.cpp`:
  `SemanticAnalyzer.AcceptsReturningTheAddressOfASelfField`,
  `AcceptsReturningSelfAsAnInterface`,
  `AcceptsReturningTheAddressOfAParameterField`,
  `AcceptsReturningTheAddressOfAGlobal`,
  `RefusesReturningTheAddressOfALocal`,
  `RefusesReturningALocalAsAnInterface`,
  `RefusesReturningTheAddressOfALocalAsAStructResult`,
  `RefusesReturningTheAddressOfALocalArrayElement`.
- `tests/test_codegen.cpp`:
  `Soundness_Codegen.AnEscapingReferenceIsRefusedWhetherInterfaceOrAddress`.
