# `Offer<T, E>` is an erasure-union alias

`Offer<T, E>` (lib/std/types.fin) is the union `T | E` at check time and
nothing at runtime: a value of an instantiation is stored as one of the
alternatives with no tag of its own. Its only use in the corpus is a generic
bound (`E: Offer<string, Error>` in tests/samples/enums.fin:13), where a
non-struct constraint is unchecked the way every one is -- so the alias erases
rather than constrains, and no code answers for it.

A generic union alias declares its name and instantiates by substitution, the
same rule structs own (StructType::instantiate): `Offer<string, Error>` is the
UnionType `string | Error`. Member access through an instantiation reads the
slot the value holds, which is the positional payload rule enums already have,
not a rule on the alias.

## Considered Options

- **A new builtin type:** Rejected. The sample needs "one of these two", which
  the union alias already spells, and a builtin would invent machinery for a
  single bound.
- **A concrete alias (`type Offer = string | Error;`):** Rejected. The sample
  writes `Offer<string, Error>` with arguments, and a non-generic alias silently
  drops them (KnownDefect_TypeAliases.GenericArgumentsOnANonGenericAliasAre-
  Discarded) -- the shape would resolve while meaning nothing.
- **Generic aliases generally (`array<T>` included):** Deferred, not rejected.
  An erased union never materialises storage, so instantiating one needs no
  layout or lowering support; an array or struct alias would. `array<T>` stays
  refused under KnownDefect_TypeAliases.AGenericTypeAliasIsNeverDeclared.

## Consequences

- `tests/samples/enums.fin` imports `Offer` from `types::std` beside `Any`,
  like any other library name; no ambience, no builtin. It lives with the
  alias family (`Number`, `Any`) rather than beside `ErrorLike`: `ErrorLike`
  is the `Result`-specific error bound, while `Offer` is general over any two
  types -- and `types.fin` declares no template, so importing it drags no
  duplicate `Result` into a file that declares its own (which is what
  `typing::std` would do under the stage's import-merge).
- As a value type an instantiation is a union like any other: first-alternative
  assignability at check time, and the layout pass refuses it (Q12) rather than
  guessing a shape.
- The day generic bounds are enforced, `E: Offer<string, Error>` starts meaning
  "a `string` or an `Error`" without an edit.
