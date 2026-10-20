# A `blame` raises `string | Error-subtype-or-implements | any/object`

**Status:** Accepted

## Context

Issue #97 tracked two `KnownDefect_Blame` tests, the two halves of one ruling
the corpus did not settle. `blame "boom"` was rejected -- though `ErrorLike =
string | any implements <Error>` (lib/std/typing.fin) reads a string as
error-like -- because it is also the assert form's message in the wrong
position, and one spelling cannot be both. And any struct was raisable with no
relation to `Error` required, because narrowing needed the analyzer to know
what `Error` is, and `blame enum_.0` (stdlib/typing.fin:32) raises a bare `T`
no narrowing short of an enum-parameter bound would admit.

## Decision

What a `blame` raises is `string | Error-subtype-or-implements | any/object`,
by owner ruling. A `string` raises: the `ErrorLike` reading wins over the
misplaced-message reading, by ruling rather than by type. A struct, class,
enum, or interface value raises when it is `Error`, inherits it, or implements
it; any other struct is refused as not error-like rather than compared against
`bool`, which would misreport a raise as a failed assert. `any` and `object`
raise as before -- an erased payload slot cannot have come from a comparison,
whose type is `bool`, and nothing erases a `bool`.

The check is deliberately shallow. `Error` is whatever the scope resolves,
not a hardcoded declaration: a snippet's own `struct Error` and the corpus's
imported one (`import { Error } from error::std`) both answer. Subtyping and
implementing share one transitive walk because `: <Error>` lands in the same
`parents` vector either way. There is no unwrapping (`&CollectionError` stays
a pointer), no nullable or generic unwrapping, and no union-alternative
splitting -- nothing in the corpus raises through any of them. Absence of an
`Error` in scope refuses every struct raise rather than accepting it: with
nothing to relate to, nothing is related.

## Considered Options

- Keep rejecting strings (the assert-message reading wins): contradicts the
  library's own `ErrorLike`, which names `string` first.
- Keep accepting every struct: leaves a `blame` of an unrelated value silent,
  which is the defect half of the issue.
- Resolve the rule through the `ErrorLike` union alias itself: the alias
  machinery for `any implements <Error>` bounds is not enforced anywhere yet
  (KnownDefect_DynamicTypes.AnImplementsBoundOnADynamicTypeIsNotEnforced), so
  the check would be decorative. The direct walk enforces today.

## Consequences

The corpus's raise sites are all inside the rule: `CollectionError` and
`OwnershipError` inherit `Error`, and the four `blame enum_.0` sites raise an
`any`. `blame 1;` still fails the `bool` comparison with its old message, and
the `blame e, 5` message check is unchanged. A refused struct raise sets no
unwind: only the accepted raise form exits with `ExitBlamed`.

Lowering is untouched and still refused: the backend has no runtime shape for
a raised value (`Soundness_Codegen.ABlameRaisingAValueIsRefused` pins the
explicit refusal), so an accepted raise checks clean and fails the `-o` build
with a named diagnostic, per the backend invariant. Giving raises a runtime
is a backend ruling for another change, not this one.
