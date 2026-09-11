# `any` maps as an opaque blob; its values do not lower

`any` is `{i8*, i64}` (payload, typeid) for layout and signatures only: big
enough to hold, with no claim about what a value in it means. That unblocks
types that mention `any` without giving it semantics -- a `fn(any) -> int`
field lays out, and a generic instantiated at one proceeds past its shape.
Boxing a value into one, converting either way, comparing, sizing, and
calling through one all still refuse where values are handled, because none
of those has a rule yet. A blob holding nothing is storage, and storage that
answers reads would be a wrong answer rather than a missing feature.

## Considered Options

- Full `any` now (lib/std declaration, boxing, typeids, conversions): the
  real workstream, still open -- this decision is its narrow prerequisite,
  not a substitute.
- Keep refusing the type: leaves every generic over `any`-mentioning fields
  (notably `HashMap`) refused at the field rather than at the value, one
  level further from running for no additional honesty.

## Consequences

The blob is one named type (`fin.any`), so all `any`s compare identical and
no user struct of the same shape can collide with it. `object` stays
unmapped: a distinct dynamic type with even less settled meaning.
`LayoutEngine` still refuses `DynamicType` -- the collector's pointer map
for a payload word is unruled, and that pass must not learn a shape from
this one. Prototype halves and `sizeof` refuse `any` explicitly, so the new
mapping cannot leak into a value through those doors.
