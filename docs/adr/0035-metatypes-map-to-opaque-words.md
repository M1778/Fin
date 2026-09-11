# Meta-types map to opaque words; `resolve_type*` evaluate at compile time

Each of `$type`, `$struct`, `$interface` and `$enum_member` maps to its own
named single-word type (`fin.type` and kin), so the four stay distinct without
claiming anything about what a word means. Nothing compares, prints, branches
on or sizes one: those all refuse where values are handled, and `sizeof` names
the type rather than printing the word's width (which would disagree with the
shared layout model, where meta-types still have no layout).

A `$type` value only ever comes from the `resolve_type`/`resolve_arr_type`
intrinsics (bodiless lib/std declarations): nothing else in the language
produces one. A call with no visible definition to either name evaluates at
compile time to the argument's static type -- `resolve_arr_type` to its
element type -- numbered per compilation starting at 1. The argument itself
is still emitted, so a call never drops runtime effects to answer statically.
A same-file definition wins by lookup order, and a WITH-body definition in a
module refuses: its body is never emitted, so calling it would link against a
symbol this object never defines.

## Considered Options

- Full meta-type values (comparable, printable, round-trippable through
  declarations): the real workstream, still open -- this is its narrow
  prerequisite for declarations and calls, not a substitute.
- Keep refusing the types: leaves every `$type`-mentioning declaration
  (notably `prototypes.fin`) refused at the signature for no additional
  honesty, since nothing observes the word.

## Consequences

Numbering is per compilation and deterministic in walk order; there is no
cross-object identity to keep, because no consumer of one exists yet. If a
consumer lands (equality, formatting, a runtime `cmp_types`), the numbering,
the word, or both are the decisions to revisit -- not the call sites, which
already hand over fully-typed arguments.
