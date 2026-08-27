# A constraint set is a bound, never a storage type

`type Number = int | uint | float | ...` declares a **constraint set**: a set of types admissible where a
bound is expected. It is not a type a value can have. `let x <Number>;` and `fun f(x: Number)` are
diagnostics at the point of use, naming the declaration.

The corpus already agrees, so this rule convicts nothing. Three constraint sets exist — `arrays.fin:7`,
`types.fin:51`, `typing.fin:8` — and every use of one is a bound: `sort<T: Number>` (`arrays.fin:9`),
`number2str<T: Number>` (`types.fin:106`), `Result<T, U: ErrorLike>` (`typing.fin:12`). Across fifty samples,
zero uses as a storage type.

## Why not let it be an untagged sum

Storage requires a tag, and a tag requires the language to say where it lives, how wide it is, what happens
when it is invalid, and how it survives the foreign ABI. Every one of those is a decision, and none of them
is implied by the `|` syntax.

Fin already has a tagged sum, and the standard library is written in it — `enum Result<T: Any<...>, E:
Offer<string, Error>>` (`enums.fin:11`), `enum IOResult<T: Strict<Stream>>` (`stdio.fin:47`), `enum Result<T,
U: ErrorLike>` (`typing.fin:12`). Making constraint sets storage types would add a second, weaker sum type
competing with the one already in use, and the language would have two answers to one question.

## Why a diagnostic rather than quietly inferring something

The syntax invites the mistake. In this corpus `type` declares both aliases and constraint sets, and the two
are told apart only by whether a `|` appears: `type IntArray = [int]` (`arrays.fin:3`) is storage,
`type Number = int | uint` is a bound. A reader who has used `IntArray` as a storage type will reasonably
write `let x <Number>;`. Inferring a generic there produces a confusing error about `T` somewhere downstream;
a diagnostic at the use site says the thing that is actually wrong.

## The rule is written against alternation, not against "constraint sets"

Stated as "a constraint set may not be a storage type", this rule convicts `enums.fin:10`, which the corpus
plainly does not intend:

```fin
pub type EnumType = any implements Enum;          // enums.fin:4
@special(priv) getenumkeyid(value: EnumType) <int> // enums.fin:10 — a parameter
```

`type nullptr = any implements <&void>;` (`types.fin:76`) and `type Any<...> = any implements <...>;`
(`types.fin:72`) have the same shape. So the distinction is **not** how many members the set has. `any
implements <X>` is an erased type carrying a bound, and an erased type has a representation (ADR 0019);
`A | B` has none. The diagnostic therefore fires on the **alternation form** only, and `EnumType` remains a
perfectly good parameter type.

Recording this narrowing explicitly because the decision was taken in the broader wording, and the corpus
forces the narrower one. Alternation is what lacks a representation; that, and not membership in a category
called "constraint set", is the reason for the rule.

## Consequences

The analyzer must distinguish alternation from erasure-with-a-bound when it resolves a `type` declaration,
and the two compose: `ErrorLike` is `string | any implements <Error>` (`typing.fin:8`), so a member of an
alternation may itself be an erased type with a bound.

A constraint set is a compile-time-only entity, so it never reaches the backend, never appears in a mangled
name, and cannot cross the foreign ABI. That is a simplification the alternative would have cost.

Someone will eventually want the untagged sum. The answer is `enum`, and this ADR is where to point them.
