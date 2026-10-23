# The derive_hash walk mixes field values, not object bytes

**Status:** Accepted

> 2026-10-20 status: shipped (commit 300458f — FNV-1a 64 over field values in declaration order, one walk on both compiler lanes); `derive_eq`/`derive_encode` stay blocked pending the R3 owner decision (docs/reflect-derive-plan.md §4); issue #53 CLOSED (ledger comment 6280766517: scopes A+B shipped; full suite 2931/2931).

## Context

Issue #53's Scope B asks for explicit derives, hash first. A hash function is an
ordinary function, so ADR 0036's `==`-declared-only ruling does not reach it: derive
is opt-in and a call, never a synthesized operator. The plan (docs/
reflect-derive-plan.md) measured the substrate ADR 0057 shipped: a bodiless generic
declaration lowerable per concrete call (`hash_word`'s footing, lib/std/hashmap.fin:
107), field lists backend-side in `StructInfo`, and a two-backend harness that only
answers byte-identical output when both compilers agree on *what they mix*, not on
what the object layout happens to look like.

The representation question the two backends disagree on by design: the C++ lays a
struct out with the DataLayout (two `int` fields pack at 0 and 4), while the self-host
stage stores its boxes with one pointer-width word per field slot (field *i* at byte
*i × pointer size*; `finc/codegen.fin`'s member-store lane, `cg_prototype_erase`'s
keys and lengths). Both are deliberate local facts. A derive that hashed object bytes
would make the same value hash differently per backend and never agree.

## Ruling

`derive_hash(x)` walks a struct value's fields in declaration order and mixes each
field's VALUE with FNV-1a 64:

```
h = 14695981039346656037;
for each byte of each field's value, least-significant byte first:
  h = (h ^ byte) * 1099511628211  (mod 2^64)
```

Field values, never object bytes: padding never enters the mix on either backend,
so equal field values hash equal under any layout that keeps their order, and a
layout-model change -- a field moved, a width widened -- shows up in the number.
What one field contributes:

- An integer scalar rides its two's-complement bytes zero-extended to 64 bits, and
  the mixer reads only the field's own width (`int` 4 bytes, `long` 8, `bool` and
  `char` 1), so the extension never pads the stream. A negative `int` value's low
  four bytes are the same either way; stored into a `long`, its full eight bytes
  ride.
- float and double ride their IEEE bit patterns (4 and 8 bytes), the same words a
  bit inspect sees.
- A nested struct recurses through its own fields; an empty struct contributes
  nothing, so `{}` hashes to the basis alone.
- A payloadless enum field rides its tag word, which is the `int` it lowers as
  (reflect.fin's model).
- bool widens unsigned whatever the target, like the C++ coerce's isBool gate.

The walk's width comes from the field's own written type and both compilers read
the same chain from the same constants. The stage keeps the classification inline
with the walk in one self-recursive function, because the self-host checker does
not hoist an imported-typed signature for a forward call; the C++ splits the same
decision into `deriveHashWalkField`. The rules, not the emitted IR, are what the
two agree on.

FNV-1a 64 is itself the choice for the same reason the 32-bit variant already
serves `hash.map` (ADR 0049): its constants are public, one multiply-and-xor per
byte, and no state beyond the `i64` accumulates. The pins compute the same number
in a spreadsheet, which is what makes a drift visible at review time.

## Refusals

Every kind the walk does not lower refuses naming the field and the kind, never
silently skipping -- a skipped field would still answer a number, which is the
quiet bug kind of answer a derive must not give:

- `'derive_hash' expects a struct value, got '<cgDisplay>'` -- the argument is not
  a struct value.
- `derive-hash of field 'f' of type 'string' is not lowered yet` -- a field the
  hash does not ride (a string points to bytes not owned by the value).
- `derive-hash of field 'f' of a pointer type is not lowered yet` -- no pointer
  rides a struct's hash through an address.
- the same shape for an array, a nullable, an interface reference, an `'any'` box,
  a union value, a function value, and a struct still incomplete at lowering.
- a payloaded enum field refuses (`carries members, not field words`) -- a tag a
  runtime type system owns, not a layout question a walk can answer program-side;
  bare and instantiated spellings both answer through the fallback text
  (`of type 'X'`), which is what `Soundness_Derive` pins.
- `derive-hash of a struct nested more than sixteen fields deep is not lowered
  yet` -- richer recursion needs a value model of its own, not a capped walk.

An arity mismatch is an ordinary call-count diagnostic: `a call to 'derive_hash'
with N argument(s) where it declares 1`. The stage echoes the C++ texts under its
own `finc codegen refusal:` prefix, the phase-A house rule.

## Considered options

- **Hashing object bytes (memhash of the struct's memory).** Layout-bound: the
  same value hashes differently on the two representations the compiler ships,
  and padding the value stream cannot see leaks in. Rejected.
- **Hashing through the checker's value semantics.** Needs the checker to lower
  values, which its contract does not include; the backend already owns every
  value the caller reads. Rejected.
- **A derive_if?/derive family over operators (==, >=).** Out of scope and
  reserved: ADR 0036 holds struct `==` declared-only and `deeptest4.fin` stays
  refused; derive_eq waits on that ruling, not on this plan.
- **Wrapping the walk behind lib/std as an ordinary library function with an
  owning operator per field.** Would give every kind a program-side answer,
  which is exactly the guessed answer the invariant forbids when the field's
  representation has no owning read at run time (strings, interfaces, any).
  The 16-deep cap stays until that model exists.

## Consequences

Sample: `tests/samples/stdlib/derive.fin` (`//@ ok`, pins the exact number
7192506404828969274 for `Point{x: 3, y: -5}`, the equal-values comparison `1`,
byte-identical across finc, stage2 and stage3). Tests: `Soundness_Derive` in
tests/test_soundness.cpp (9 cases: the number, equal equals, nested walk, empty
struct, per-width byte sweep, string/pointer/argument refusals, the wrapper bind,
arity).

Surface: lib/std/derive.fin is the module home; the declaration is bodiless and
generic exactly like hash_word, on the same footing, with `Type` bound from the
VALUE argument's spelling. The C++ dispatch sits in the call lane beside
`tryHashWordIntrinsic` and the stage sits in `lower_expr` beside the `hash_word`
lane; both refuse before any generic clone registers a body for the name, and a
definition (hash_word's own rule) wins the name and falls through.

The stage's signedness rides a noted boundary rather than a silent one:
`cg_coerce_field_int` signs field-store widening from the written annotation
(scalarByName's table, src/types/Layout.cpp) so that a negative `int` landing in a
`long` field keeps its bits on both backends -- the sample's `y: -5` pin requires
it. The same zero-extension still answers other coerced lanes (decl initializers
and arithmetic folds widen narrow sources through coerce_val, which carries no
sign knowledge); lanes that need the same answer reach for the helper's own
signedness-keyed gate and report a miss rather than guessing a wider rewrite.

Relationships: ADR 0053's blame/raise holds the refusal family; `blame` for these
refusals is reached through PanicsPath. ADR 0054's wrap wording doesn't apply
(derived values hash the representation, not arithmetic); padding agrees with
ADR 0052 zero-initialization (slot padding is not a decision a value can observe
here). ADR 0057's reflect facts remain the metadata surface; this derive is its
first *value* read and stays one read: field values to bits, the walk never
distinguishes a type from the memory it lowers as.

The wrapper-primary shape means the wrapper is the thing tested: a caller writing
`derive_hash(p)` always gets the built-in walk on both backends, including inside
a lib/std wrapper module, which is the shape Phase B's plan promised. Fields that
need a different owning model (`fields_of`, `enum_from!`, derive_eq) stay owned by
issue #53's remaining scope and separate rulings; this ADR does not touch them.

## Addendum — platform-stable width (2026-10-20)

For the same declared field types, declaration order and scalar bit patterns,
`derive_hash` must return the same unsigned 64-bit value on every supported
platform. Its arithmetic wraps modulo 2^64, independently of the target C ABI
and struct padding. Fin `long` and `ulong` remain 64-bit on both LP64 and LLP64;
the existing LLVM walk already uses explicit `i64` operations throughout.

The Windows truncation reported by the original runtime pins was in C `printf`:
`%lu` reads a 32-bit C `unsigned long` on LLP64. Printing a Fin `ulong` uses
`%llu` to preserve all 64 bits. The four `Soundness_Derive` output pins and the
normative sample use that conversion without changing their expected hashes or
the `derive_hash` declaration. Native Windows CI owns the LLP64 runtime proof;
Linux output alone cannot detect that formatting defect.
