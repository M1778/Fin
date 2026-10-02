# A provider is called once per subject, and the compiler keeps the answer

The owner accepted `provider` as the third mechanism (docs/compiler-api.md Q1):
a `@special` function the compiler calls once per subject, whose returned
value the compiler stores and emits, declared `#[provides(<slot>)]`. Exclusive
per slot, memoised per subject. Wave 4 ships one slot: `type_metadata`, the
per-type pointer map ADR 0003's collector reads.

## The contract, as built

A provider for `type_metadata` takes exactly one subject as `<$struct>` and
returns `<quote>`, and its body is exactly
`return compiler.layout.pointer_map_quote(<subject>);` -- one statement, the
projection, the subject. Anything else is refused naming the interpretability
line it breaks (ADR 0006: no loops, no arithmetic), never evaluated into
something the compiler did not promise to run.

The narrowness is the purity proof. A provider must be a pure function of its
subject (ADR 0014), and the C++-first body rule makes that true by
construction: the only expressible body has no state, no second parameter and
no other call, so there is nothing impure to check for. General `@special`
execution -- running an arbitrary straight-line body -- waits on the comptime
interpreter (ADR 0006); until then the body shape is the call, and the
compiler computes the named projection from the finalised lowering, once per
subject, and keeps it.

Exclusivity is checked where the declaration is seen: a second claimant for a
slot is a diagnostic naming the slot and both providers. Across modules the
check rides with the module-merging work the event system still owes; the
backend refuses a doubled slot as defense in depth, and that is the whole of
what it checks about providers.

## The record

`fin.typemeta.<T>` per lowered struct: size, alignment, and the map as
`(offset, stride, count, tag)` entries, stride-encoded so the record is
counted in fields, not bytes (§3.9's normative constraint -- D's 0.3 s of
zeroes is what happens otherwise). A pointer-free type is a constant-size
record with no entries. The tag is ADR 0019's slot state and is 0 (traced)
everywhere: the must-not-follow states have no producer yet, and the field
exists so they fit without an ABI break when one does.

A field the map cannot describe refuses, naming the struct and the field: an
`any` blob (heap exactly when it boxes one), an interface reference (whose
vtable word must not be followed), a prototype (no static field list), a
dynamic array (no static extent), an enum variant that may hold a pointer
(needs a discriminant rule nobody has written). A guessed map corrupts a heap
and produces no diagnostic anywhere; a refusal costs one line.

`quote` is a type before it is a value: the grammar admits it in type
position (beside `any`, for the same reason) and the analyzer registers it
opaque, so the projection signatures resolve. Quote values, splicing and
injection are steps 11-13 and are not this decision. The layout-vs-codegen
`sizeof` split (audit B2) is untouched: emission reads the backend's lowered
types, and the existing layout-agreement test is what holds the two tables
together.

## What this does not do

Layout reads have two spellings and one gate: `compiler.layout.size_of(t)`
and `t.size` both need `#[use(compiler.components.layout)]`, checked at the
call and at the member-access site through one function. Only the
argument-free scalar reads (`size`, `align`, `pointer_count`) have a member
spelling; a `$struct` value widens to a `$type` argument, and the four
meta-types stay distinct from each other.

Phases are not tracked here. The analyzer resolves the layout signatures and
never evaluates them; phase legality is the layout engine's (ADR 0015), and
the events that open each moment are the event system's to fire. No provider
exists without the declaration, so every program that names none emits
exactly what it emitted before.
