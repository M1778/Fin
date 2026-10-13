# Signed-integer overflow wraps

## Context

Issue #45 — "Decide and pin the signed-integer overflow policy (wrap vs trap
vs blame)" — asked what a Fin `int` holds when arithmetic overflows. The
observable behaviour of the LLVM backend was already uniform: signed `+`, `-`,
`*`, and unary negation lower to plain two's-complement LLVM instructions
(`visit(BinaryOp&)`, `src/codegen/CodeGen_LLVM.cpp:9116`), with no overflow
flags queried and no check on constant results. Measured 2026-10-09 and
re-confirmed for this ADR: `let x <int> = 2147483647 + 1;` builds clean, prints
`-2147483648`, exits 0 — the compiler observes the wrap at compile time and
says nothing. The self-hosted stage compiler agrees byte-for-byte on every
probe below.

The policy was therefore decided by LLVM's default, not by the language — the
same undecided-policy shape issue #34 closed for uninitialized memory (ADR
0052), and the same future StageAgreement divergence risk: the C++ backend and
the stage compiler must agree on overflow.

## Decision

Signed `int` overflow wraps modulo 2^32 (two's-complement). This codifies
reality: no codegen change. It covers constants, runtime values, and
negation-of-`INT_MIN` identically, because all three are the same instruction:

- `2147483647 + 1` (constant or runtime) is `-2147483648`.
- `-(-2147483648)` (negation of `INT_MIN`) is `-2147483648`.
- `INT_MIN - 1` is `2147483647`.
- Unsigned `uint` already wraps (`cast<uint>(4294967295) + cast<uint>(1)` is
  `0`); this ADR changes nothing there, it only records that the two widths
  agree on wrap semantics.

Two alternatives were considered and rejected:

- **Trap (poison / `nsw` flags plus a trap on overflow).** Would turn silent
  wraps into crashes, but every existing corpus program that relies on plain
  arithmetic keeps working only until it overflows — and no corpus sample
  overflows today, so the trap would be dead code with a real cost: an extra
  branch or intrinsic on every arithmetic op, plus a new runtime failure mode
  the stage compiler would have to reproduce exactly.
- **Blame-on-overflow (a blame site at each overflowing op).** Consistent with
  Fin's blame vocabulary, but blame sites need source positions for values that
  may be folded far from any operator, and constant-folded overflow would blame
  at compile time while runtime overflow blames at run time — two mechanisms
  for one rule. Revisit if Fin ever wants checked arithmetic as an opt-in; it
  is not the default.

The cost of the chosen rule is zero instructions: it is what the backend
already emits. The cost it accepts is the familiar one — silent wrap is the
programmer's responsibility, as in C unsigned arithmetic.

## Consequences

- `tests/test_codegen.cpp` gains `BACKEND_TEST(Soundness_Codegen, ...)` pins:
  `SignedOverflowWrapsAtConstant...` (the issue's `2147483647 + 1` probe),
  runtime `INT_MAX + 1` through a function argument (un-foldable), negation of
  `INT_MIN`, `INT_MIN - 1`, and unsigned wrap — each asserting the wrapped
  value on stdout under the C++ backend.
- `tests/test_stage_agreement.cpp` gains `TEST(StageAgreementOverflow, ...)`
  reusing the generic build-both-run-both-compare helper: the same wrapped
  outputs must be byte-identical under the stage compiler. Wrap is a cross-
  compiler contract, which is the point of the issue.
- No corpus sample: `tests/samples/*` is specification, and a sample asserting
  `-2147483648` via `blame` would enshrine one wrapped literal, not the rule;
  the inline pins above carry the exact expectations instead.

## References

- GitHub issue: #45 — "Decide and pin the signed-integer overflow policy
  (wrap vs trap vs blame)."
- Sibling precedent: ADR 0052 (issue #34, uninitialized memory).
- Codegen site: `src/codegen/CodeGen_LLVM.cpp`, `visit(BinaryOp&)` at line
  9116 — plain arithmetic instructions, no overflow flags.
