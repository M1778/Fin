# Equality is declared, not synthesized

`==` on a struct without a declared `operator ==` (or the `Equal` interface)
stays refused. The backend will not synthesize field-wise equality, and
uncalled method bodies keep lowering eagerly -- so `Collection<Data>` fails
on the `==` in `index_of` even where no caller reaches it.

## Considered Options

- Synthesize field-wise `==` per struct: implementable, but every field kind
  needs a rule that does not exist (float `-0.0`/`NaN`, padding bytes, `any`
  fields, cyclic structs), and it would bypass the `Equal` opt-in the library
  already declares -- making the interface pointless.
- Lower only called method bodies: overturns eager lowering (the "unreachable
  today is a miscompile tomorrow" rule behind `declareStructs`), the shared
  `linkonce_odr` design, and any future dynamic dispatch through interfaces,
  all to dodge one operator.
- Keep refusing: the analyzer already routes struct `==` through declared
  operators, the backend's refusal names the struct and the missing operator,
  and the standard library documents the refusal as its contract
  (lib/std/hashmap.fin:66-68). A program that wants `==` declares it.

## Consequences

Generic code over `==` (like `index_of`) fails at instantiation for element
types without the operator -- use-site checking, the same bargain concepts
have elsewhere. `deeptest4.fin` waits on either `Data` declaring `==` (a
sample change, by language decision only) or a future ruling that revisits
this one. Padding bytes are the sharpest reason synthesis stays out: a
`memcmp` would compare indeterminate bytes, and anything finer is a language
definition, not a lowering.
