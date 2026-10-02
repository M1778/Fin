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

## Amendment: `any` values lower (owner decision overturns the refusals)

Boxing, typeids, and conversions are real semantics now, not refusals. The
layout above is unchanged -- still `{i8*, i64}`, still the one named type
`fin.any`, so the no-collision consequence stands -- and what changes is that
a value in the blob means something: the payload word plus the typeid word,
checked on the way out.

Boxing records the static type's id beside the value: pointers and function
values go in as the pointer; integers and floats go in as their bit pattern
(extended to 64 bits); anything with an address boxes the address, and a
struct rvalue is spilled to a slot first. Unboxing checks the stored id
against the target's and Fin-blames on mismatch
(`an 'any' unbox to '<want>' holding another type`, then abort) instead of
reinterpreting the bits. Before the check, `cast<float>` of a
double-holding blob truncated its bits and answered wrong, and
`cast<string>` of an int-holding blob handed back address `0x29` as a
string. Both now blame. `==`/`!=` compare typeid first, then payload
(strings by bytes) -- the rule map keys already used, so `==` agrees with
lookup. A non-`any` side boxes first, so `y == 41` reads as two blobs.
The `strcmp` in that rule used to run unconditionally and segfaulted on
int-holding payloads; it now runs only when both sides hold strings.

The ids are per-compilation (numbering from 1, 0 stays "no type"), keyed by
spelling:

| Value | Typeid key |
|---|---|
| `int`, `uint`, `long`, ... (width and signedness count) | that spelling |
| `bool` | `bool` |
| `float` (f32), `double` (f64) | `float`, `double` |
| `string` | `string`; `&T` is `&` plus the pointee's key |
| `fn(A) -> R` | the full signature, so arities never share one |
| struct / enum | the (mangled) type name |
| interface | `interface<Name>` |
| array / prototype | the existing element spellings |

Two deliberate narrownesses. Nullability is not in the key: `A?` boxes as
`A`, which is what `nullifier.fin`'s `cast<T>` needs. And the unbox names
the held type exactly -- `cast<long>` of an int-holding `any` blames, and
so does `cast<float>` of a `1.5`-holding one, because a float literal
emits a double (the literal visitor builds an f64). Widen before boxing;
the blame says which side disagreed.

What still refuses, each named where it refuses: `sizeof(any)`
(`the size of 'any'` -- the shared layout model still answers "no layout"
for a dynamic type, and that pass must not learn a shape from this one);
calling through an `any` variable (the analyzer refuses first, and the
variable-call path refuses non-function types -- unbox-then-indirect-call
is the rule nobody has written); operators past `==`/`!=`
(`an operator on 'any'`); an unbox outside a function
(`an 'any' unbox outside a function (ADR 0034)` -- the check needs a block
to live in); boxing a type with no value rule and unboxing to one with no
struct to load (both `a conversion from '<a>' to '<b>'`).
