# Uninitialised storage is zero-initialised

## Context

Issue #34 — "Decide and pin the uninitialized-memory policy" — asked what a local
variable or array element holds when the source gives it no initializer. The
observable behaviour of the LLVM backend was already uniform: every `alloca`
that had no explicit store was filled with a zero (`CodeGen_LLVM.cpp`, the
`else` branch of `visit(VariableDeclaration&)`). Three tests in
`tests/test_codegen.cpp` carried a comment to the effect that this could not be
tested, because "undefined stack contents is the one answer that cannot be
tested":

- `AnArrayWithNoInitialiserIsZeroed` (~line 1200) — asserts a `[int, 3]` with
  no initializer reads back `0 0 0`.
- `AnArrayAllocationIsAPairAndNotAPointerToAFixedArray` (~line 1510) — the
  comment at the old line 1472 read "undefined contents is the one answer no
  test can pin."
- `ADeclaredPrototypeWithNoInitialiserLowers` (~line 1797) — the comment read
  that an uninitialized prototype's halves "is the same open question an
  uninitialised `[int]`'s pointer is."

Each of these described zero-initialisation as an *observation*, not a rule.
The policy was therefore not testable by a corpus sample, and a different
backend (or a future optimization pass) could have left the storage
uninitialised without violating any contract.

## Decision

The compiler writes zero to every local that has no initializer. This is
policy, not accident: the `alloca` is followed by a `store` of
`Constant::getNullValue(type)` for scalar locals, fixed arrays, and struct
locals alike.

Three alternatives were considered and rejected:

- **Leave the storage uninitialized (C semantics).** The whole point of the
  issue is that untestable behaviour — UB that happens to read zero on this
  machine — is a liability when the corpus claims to test against it.
- **Refuse to emit storage that is never fully initialized.** This would make
  `let a <[int, 3]>;` a compile error, which the corpus already relies on in
  multiple samples (`arrays.fin`, `const.fin`) and whose ergonomics the owner
  has not questioned.
- **Defer to the optimizer to elide the zeroing when provably dead.** This is
  a backend optimization and does not change the observable contract; it is
  left as future work, not a policy decision.

The cost is a single `memset`-equivalent instruction per `alloca` on the
uninitialized path. Every `BACKEND_TEST` that exercises this path
(`AnArrayWithNoInitialiserIsZeroed`, `AStructWithNoInitialiserIsZeroed`,
`AnOmittedFieldIsZeroed`, `AFieldNoConstructorAssignsIsZero`) already asserts
the zero, so the cost is measured rather than estimated.

## Consequences

- `tests/samples/zeros_uninitialized.fin` is added to the corpus with `//@ ok`.
  It declares an uninitialized scalar local and asserts zero through `blame`.
  Fixed-array and struct locals with no initializer are covered by the
  BACKENDTESTs `AnArrayWithNoInitialiserIsZeroed`, `AStructWithNoInitialiserIsZeroed`,
  and `AnOmittedFieldIsZeroed` in `tests/test_codegen.cpp`, which run only under
  the C++ backend. The self-hosted stage compilers use a pointer-based struct
  representation and have not yet closed that gap -- StageAgreement catches the
  divergence, which is its purpose. The scalar case is shared across all tiers.
- The three "cannot be tested" comments in `tests/test_codegen.cpp` are replaced
  with a citation to this ADR and a pointer to `zeros_uninitialized.fin`.
- The codegen comment at `CodeGen_LLVM.cpp` is updated to state the rule rather
  than restate the question.

## References

- GitHub issue: #34 — "Decide and pin the uninitialized-memory policy."
- Corpus sample: `tests/samples/zeros_uninitialized.fin`.
- StageAgreement test: `tests/test_stage_agreement.cpp`,
  `INSTANTIATE_TEST_SUITE_P(Corpus, StageAgreement, ...)` at line 582.
- Codegen site: `src/codegen/CodeGen_LLVM.cpp`, `visit(VariableDeclaration&)`
  at line 8264, the `else` branch at line 8346.
