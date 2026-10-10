# Pattern matching (`match`) dispatches over tags the language already has

## Context

Fin has no `match` keyword and no pattern-matching statement or expression:
`match` lexes as an ordinary identifier, and `match 1 { }` fails to parse
(`error: expected ';'` at the scrutinee, verified against `build/finc`).
Yet three decided features are already match-shaped:

- Nullable denullify (`e?`) branches on the presence tag and raises
  (`emitRuntimeBlame`, "denullify of an absent value") on absence
  (ADR 0040; `src/codegen/CodeGen_LLVM.cpp`).
- Enum values carry a 32-bit discriminant tag, fieldless or `{ i32 tag,
  payload }` (ADR 0041; `CodeGen_LLVM.cpp:469-470`, `Layout.cpp`).
- `enum_ == Ok(T)` denotes the member by discriminant comparison, with the
  `T` as disambiguation, never a value (ADR 0037; `Analyzer_Expr.cpp:479`).

Each new ad-hoc conditional over a tag (`== null`, `== Ok(T)`, `.0` slot
reads) is a fragment of a match the language will eventually want whole.
Issue #64 records the adjacent pressure from the other side: the self-host
compiler fans out over runtime integers in 60+-arm `else if` chains because
there is no value-dispatch syntax either, and recommends a library fix for
that disease — explicitly NOT this issue. This ADR scopes the user-facing
ergonomics proposal: exhaustive dispatch over Fin's discriminated shapes.

## Decision

`match` is an expression over a parenthesised scrutinee, with arms separated
by the existing `=>` token (already `ARROW` in the lexer, used by prototype
literals `map!{"k" => v}` and macro arms). Every pattern reuses a spelling
Fin already has; no new sigil:

```fin
let r <Result<int, string>> = Ok(10);
let n <int> = match (r) {
    Ok(v) => v,
    Err(e) => 0
};
```

- **Scrutinee:** `match (expr)`, parens mandatory, mirroring `if (expr)`
  (`src/parser/parser.y:2223`). Statement position (`match (x) { ... };`
  discarding the value) falls out the same way `if` without `else` does.
- **Enum patterns:** member paths as already written at construction and
  comparison sites: `Color::RGB(r, g, b)`, or bare `Ok(v)` / `Err(e)` where
  the `extern Result::Ok as Ok` aliasing idiom (`enums.fin`) is in scope.
  Bare `Ok` (no parens, ADR 0037 spelling) matches the member and binds
  nothing. Payload bindings (`v`, `r, g, b`) are fresh `let`-style immutable
  bindings scoped to the arm body — the only binding mode (see non-goals:
  no `&`/ref bindings; Fin has no borrow checker to serve).
- **Nullable patterns:** `null` matches absence (the `== null` spelling,
  tag-only per ADR 0040 amendment); a bare identifier matches presence and
  binds the unwrapped payload (the `e?` denullify shape, without the
  blame-on-absent: absence is a different arm, not a panic).
- **Constant patterns:** integer, string, and boolean literals, compared
  with the existing `==` of that type (string arms get ADR 0049 content
  equality for free).
- **Wildcard:** `_` matches anything and binds nothing. `_` is already a
  legal identifier (`let _ <int> = 1;` builds), so the arm needs no new
  token — it is an identifier pattern that the checker exempts from the
  unused-binding rule and from exhaustiveness contribution beyond closure.
- **Exhaustiveness:** the checker must prove every tag reachable. A missing
  case is a semantic refusal naming the uncovered member(s), not a runtime
  fallthrough. `_` closes any match. Non-exhaustive match is NOT a `blame`
  site: `blame` is a runtime raise (`BlameStatement`, `parser.y:2374`), and
  a missing arm is knowable at check time — refusing statically is strictly
  stronger and matches the backend invariant (refuse, never silently drop).
- **Guards:** v1 has none. An arm is a pattern plus body, full stop. Guard
  (`if` on bound values) is the documented first extension, deferred until
  the unguarded form ships (it needs no grammar change later: `Pattern
  => body` gains an optional trailing condition in arm position only).
- **Result type:** all arm bodies must agree on one type under the existing
  unification at the match node; divergent arms refuse exactly as divergent
  `if`/`else` bodies do.

## Lowering sketch

Desugar is NOT to nested `if`s in the AST (that shape would re-run tag
loads and duplicate the scrutinee on every arm). The match node lowers
directly, one tag read per level:

- **Enum/nullable scrutinee:** one load of the discriminant (i32 tag, or
  `has_value`/null for nullable), then a compare chain over the constant
  member tags with payload GEP binds per taken arm. v1 emits the linear
  chain — arm counts in user code are small (2-4: `Ok`/`Err`,
  present/absent), where a jump table buys nothing and complicates payload
  binding. The `LLVMBuildSwitch` path (`llvm_build_switch` is already
  bound in `finc/llvm.fin:512`, imported-but-unused per issue #64) is the
  documented upgrade when constant-pattern matches over dense integers
  arrive; LLVM itself lowers `SwitchInst` to jump table or compare tree
  per site, so the choice is never user-visible. Cost note: for v1's
  shapes the chain and the switch generate identical code at these arm
  counts — the switch is strictly a later optimization, not a semantic
  fork.
- **Constant patterns on integers:** same chain; duplicate literal arms
  refuse at check time (one arm is dead, and dead arms are silent drops).
- **Decision tree vs nested ifs:** v1 is nested-ifs-at-IR-level (one
  discriminant, ordered tests). A compiled decision tree (shared prefix
  tests for nested patterns) waits on nested patterns existing at all —
  v1 patterns are exactly one level deep, so there is no prefix to share.

## Interaction notes

- **Nullable:** `match (x?)`... no — the scrutinee is the nullable itself,
  arms are `null` and a binder. Denullify `e?` stays the one-armed sugar:
  `e?` ≡ `match (e) { v => v, null => blame ... }` in spirit, keeping its
  exact current blame message. No behavior change to `?`.
- **Enums/Result:** `match` is the first syntax that READS the tag ADR 0041
  writes; `== Ok(T)` (ADR 0037) stays the one-off comparison. Payload slot
  reads (`.0`) inside an arm body are checked against the matched member's
  slot type instead of today's erased handling.
- **Struct `==`:** untouched (ADR 0036: declared-only, never synthesized).
  Struct patterns are a non-goal for v1 (see below), so no interaction.
- **`any` boxes:** value semantics (ADR 0034 amendment). Matching INTO an
  `any` scrutinee (type-test arms) is a non-goal; `any` arms would need
  runtime type dispatch the box does not carry.

## Considered options (rejected alternatives)

- **C-style `switch` statement:** refused. `switch` dispatches on values
  with fallthrough and `break`; Fin's discriminated shapes need binding
  (`Ok(v)` binds `v`) and exhaustiveness, neither of which `switch` gives.
  It would also be a second conditional syntax beside `if`/`:`-ternary for
  no added power. Issue #64's integer chains want EITHER a library fix
  (recommended there) or constant-pattern `match` arms (this ADR's
  extension path) — not a separate statement.
- **`if let` chains:** refused as the primary form. `if (r == Ok(T))` plus
  manual `.0` reads is today's fragment; blessing it adds nothing —
  no exhaustiveness, no bindings, and the tag is re-tested per chain link.
  `match` subsumes it; `if let` as extra sugar waits until `match` proves
  insufficient somewhere concrete.
- **External pattern library (macros/comptime):** refused. Macros return
  one quoted expression (ADR 0023) and cannot add exhaustiveness checking,
  which needs the checker's tag knowledge at the match site. A library can
  only wrap the `if`-fragment, keeping every weakness. The tag read itself
  must be a compiler node.

## Non-goals (v1 edges)

Guards, nested patterns (`Ok(Some(x))`), struct patterns, range patterns,
or-patterns (`Ok(v) | Err(v)`), ref/mut bindings, matching on `any`
type-tests, `switch`-style fallthrough. Each is a named later ADR-sized
step, not a silent v1 stretch.

## Implementation plan (sequenced, test-first)

1. **Grammar + AST node (~S).** One `match (expr) { arms }` production,
   `MatchExpr` node, `accept`/clone branches per ADR 0004. Red shape:
   aspirational `tests/samples/match.fin` (`//@ unimplemented`) with the
   `Ok`/`Err` canonical example — today fails at `expected ';'`
   (verified), parses after.
2. **Semantics: enum + wildcard only (~M).** Tag obviously-covered set per
   enum decl, `_` closure, bare-member and binding patterns, one-type arm
   agreement, duplicate-arm refusal. Red shapes: non-exhaustive `match`
   without `_` refuses naming the member; duplicate literal/member arms
   refuse.
3. **Codegen: enum/Result lowering (~M).** Discriminant load + compare
   chain + payload GEP binds, both compilers (owner-first per ADR 0048
   precedent). Red shape: `match.fin` flipped to `//@ ok` running the
   canonical example.
4. **Nullable arms (~S).** `null` + binder over `{T,i1}` / null-sentinel
   representations (ADR 0040 amendment). Red shape: `match (make_A(1))`
   over present/absent.
5. **Constant patterns (~M).** Integer/string/bool literal arms with
   duplicate refusal; documents the `llvm_build_switch` upgrade as
   optional follow-up, never a prerequisite.

## Consequences

- Closes issue #40's design requirement on merge (ADR review, not code —
  per the issue, NO implementation ships here); the aspirational
  `match.fin` sample is owned by the implementing issue, not this file.
- Issue #64 stays independent: its (b)-library recommendation is
  unaffected, and its (a) constant-dispatch ergonomics gains this ADR as
  its design home when filed separately.
- Held rulings untouched: ADR 0036 (`==` declared-only), ADR 0034 (`any`
  value semantics), ADR 0037 (`Ok(T)` denotes the member — this ADR's
  patterns reuse that denotation).
