# Handoff — Fin compiler completion

Updated 2026-09-11 at commit `bd606c1` on branch `wave3-semantics`.

This is the **current, short handoff**. `docs/plan.md` and the older commit history contain
historical design notes; do not use their old sample counts as current measurements. Start here,
then inspect the current tree and rerun the measurements below.

## Goal

Finish the standing goal: all runnable normative samples compile and run, and `lib/std/` is
complete. The 51 files under `tests/samples/` are the language specification (ADR 0008), with
expectations in their `//@` headers. `//@ ok` describes frontend diagnostics; it does not promise
that LLVM object generation already works.

Backend invariant:

> If codegen cannot lower a construct, it must report an explicit refusal. Never silently drop
> runtime code or emit guessed IR.

## Working rules

- Work in `/home/M1778/Fin` on the existing branch; do not create a worktree.
- Commit incremental work by pathspec. Never push and never amend.
- Commit author: `M1778M <m1778.pc@gmail.com>`.
- Write regression tests first, then implement, build, and run tests.
- Keep unrelated files untouched. `CMakeUserPresets.json` is tracked but must not be committed;
  do not use `git commit -a`.
- Use the default Opus agent only if delegation is genuinely needed; keep agent count low.
- Do not work in `~/finn-registry`.

## Current verified state

Recent commits:

- `86696b3` — accept layout-neutral `#[debug]` fields and explicit operator receivers
- `76a5950` — treat user macro declarations as compile-time-only
- `e6a7a57` — update the written-`self` operator regression
- `8715f04` — lower explicit struct destructors
- `bd606c1` — give destructors an explicit empty parameter list

Verification already completed:

- Full CTest suite: **1708/1708 passed**.
- Codegen tests after the destructor work: **560/560 passed**.
- `readonly.fin` and `macros.fin` compile with `finc -c`.
- `deeptest2.fin` now passes its operator and destructor blockers and reaches multiple-inheritance
  layout.
- Working tree was clean at handoff creation.

Rebuild with:

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

The compiler is `build/finc`. Remove generated objects after audits:

```bash
rm -f *.o tests/samples/*.o tests/samples/stdlib/*.o
```

## Remaining object/codegen blockers

Run each sample with `build/finc -c <sample>` and fix the first refusal, then remeasure. Current
backend blockers are:

1. **`tests/samples/deeptest2.fin`**
   - Current refusal: `MultiInherit` inherits `Person` and `Student`; `Student` already carries
     `Person`, so the backend sees duplicate inherited fields such as `name`.
   - Do not guess a second-base ABI. `src/types/Layout.cpp` explicitly refuses multiple base
     structs because placement/upcast semantics are undecided. The attempted backend-only
     deduplication segfaulted and was reverted. Decide whether to implement the layout rule in
     the shared layout model first, or book this as a known defect.
   - Destructor lowering is explicit-only: `~T` gets a destructor symbol/body but is not implicitly
     invoked at scope exit. Revisit only if the language ruling requires implicit destruction.

2. **`tests/samples/deeptest4.fin` and `tests/samples/useful_macros.fin`**
   - Current refusals are generic `HashMap`/`Collection` instantiations and variables.
   - The imported module AST does not reach the root compilation unit's backend. This is a
     separate-compilation/design problem, not merely a missing generic-instantiation branch.
   - Inspect `src/driver/Driver.cpp`, module loading, `src/codegen/CodeGen_LLVM.cpp`'s
     `templates_`/`instantiateGeneric`, and the `@stdimport` handling before changing codegen.
   - Preserve the distinction between ambient `@define`/`#[global]` declarations and imported Fin
     definitions: emitting an extern for an imported Fin body would create a link-time lie.

3. **`tests/samples/stdlib/hashmap.fin`**
   - `HashMapError : <Error>` refuses because `Error` was not lowered in this compilation unit.
   - This is coupled to the separate-compilation decision above, and to imported interface/base-type
     classification (`parentIsInterface`). Do not solve it by blindly ignoring the parent.

4. **`tests/samples/stdlib/error.fin`**
   - Current first refusal: struct attributes such as `#[uncastable]` are refused by
     `canLowerStruct`.
   - `#[uncastable]`, `#[stderror]`, and `#[class]` have semantic meaning in the frontend; none has
     a complete backend implementation yet. Existing soundness tests intentionally require unread
     attributes to be refused (`tests/test_codegen.cpp`, `AnAttributeThisFileDoesNotReadIsStillRefused`
     and `AnLlvmNameBesideAnUnreadAttributeIsStillRefused`). Add semantics and invert tests only
     when the implementation is real. `error.fin` also exposes `@special` and pointer comparison
     after the attribute refusal is removed.

5. **`tests/samples/stdlib/prototypes.fin`**
   - Current refusal: return type `$type` is not lowered.
   - Prototype values already lower as a pair of dynamic arrays (`prototype<K,V>`); `$type` is a
     compile-time/type-reflection result and needs an explicit representation/ruling. Do not map it
     to an ordinary runtime pointer without checking `docs/adr/0028` and the analyzer's `$type` use.

## Frontend blockers still visible in the corpus

These are not codegen failures and should be handled after measuring each one against its sample
expectation. Do not turn a documented sample typo into a compiler feature:

- `const.fin` — `rptr<int>` versus `&rptr<int>` mismatch
- `enums.fin`, `stdlib/operators.fin`, `stdlib/typing.fin` — `Any`
- `importing.fin` — intentionally missing `somelib` module
- `literal_interface.fin` — `implements`
- `literal_struct.fin` — undefined `st`
- `nullifier.fin` — nullable `A?` versus `int?`
- `preprocessor.fin` — parser error at `RPAREN`
- `prototype_test.fin` — `int` versus `object`
- `stdlib/collection.fin` — function variance/signature mismatch
- `stdlib/enums.fin` — `Enum`
- `stdlib/memory.fin` — `Alloc`
- `stdlib/stdio.fin` — generic `X` method lookup
- `stdlib/stdptr.fin` — `pointer_type`
- `stdlib/types.fin` — `_static_string`
- `undefined_behavior.fin` — expected missing-return diagnostic; this is a negative sample and
  should continue failing frontend analysis.

The exact current first diagnostics should always be regenerated rather than copied from this list.

## Important implementation locations

- `src/codegen/CodeGen_LLVM.cpp` — LLVM emitter, lowerability checks, struct layout, templates,
  imported declarations, and explicit refusal policy.
- `src/types/Layout.cpp` / `src/types/StructType.hpp` — shared semantic layout; multiple base
  structs are currently refused here.
- `src/driver/Driver.cpp` and module-loader sources — root/module compilation boundary.
- `src/semantics/impl/Analyzer_Decl.cpp` — struct parents, attributes, `@special`, macros, and
  operator signatures.
- `tests/test_codegen.cpp` — backend soundness and known-defect tests.
- `tests/test_expectations.cpp` — corpus discovery/expectation harness.
- `lib/std/` — standard-library declarations and implementations.

## Measurement script

```bash
for f in tests/samples/*.fin tests/samples/stdlib/*.fin; do
  out=$(build/finc -c "$f" 2>&1); rc=$?
  if [ "$rc" -eq 0 ]; then
    printf 'OK   %s\n' "$f"
  else
    printf 'ERR  %s | %s\n' "$f" "$(printf '%s' "$out" | grep '^error:' | head -1)"
  fi
done
rm -f *.o tests/samples/*.o tests/samples/stdlib/*.o
```

After each backend change, add or update a focused `Soundness_Codegen` test, run that test, then
run the full suite. When a refusal becomes supported, rename/invert the old regression test rather
than weakening it. Commit only the intentional source/test/docs paths.
