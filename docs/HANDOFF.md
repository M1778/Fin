# Handoff — Fin compiler completion

Updated 2026-09-12 at commit `2e4065f` on branch `wave3-semantics`.

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
- Commit incremental work by pathspec. Pushing `wave3-semantics` to origin is allowed
  (user-lifted 2026-09-11); never amend. Monitor CI with `gh` after pushes.
- Commit author: `M1778M <m1778.pc@gmail.com>`.
- Write regression tests first, then implement, build, and run tests.
- After every edit, verify `git diff` shows only the intended change before building.
- Keep unrelated files untouched. `CMakeUserPresets.json` is tracked but must not be committed;
  do not use `git commit -a`.
- Use the default Opus agent only if delegation is genuinely needed; keep agent count low.
- Do not work in `~/finn-registry`. Read-only research in `~/finn` (package manager) is allowed
  and settled the import model (see ADR 0032): whole-program source visibility, no objects.

## Current verified state

Recent work (21 commits since `bd606c1`, all pushed):

- Diamond multiple inheritance shares one ancestor (ADR 0029, layout + codegen).
- `deeptest2.fin` tail: implicit-`self` fields, `super::` access, `Self()` calls,
  bodiless interface ctor/dtor requirements — the sample compiles with `finc -c`.
- Import registry (ADR 0032): templates/interfaces/blocks register lazily from the
  loader; export is visibility-only (ADR 0033); concrete bases lay out on need.
- `any` maps as an opaque blob, values still refuse (ADR 0034); nullable `fn`
  lowers; null compares, null scalar defaults, pointer→int casts, denullify.
- Meta-types map distinctly; `resolve_type*` evaluate to per-compile typeids (ADR 0035).
- `stdlib/hashmap.fin` and `stdlib/prototypes.fin` compile with `finc -c`.
- CI runs on branch pushes and is green on Linux/macOS/build-script; Windows rows
  are experimental (`continue-on-error`) pending two known issues below.

Verification already completed:

- Full CTest suite: **1866/1866 passed** (local Debug build).
- All 37 standard library modules (`lib/std/*.fin`) check clean standalone with zero diagnostics.
- Linux x86_64/arm64, macOS arm64/x86_64, build-script jobs green on CI Release builds.
- Fixed along the way: generic interface value lowering (`Box<int>`), type alias lowering (`type size_t`),
  temporary rvalue struct receiver addressability, struct memberwise equality in codegen.
- Complete documentation web application in `docs/site/` and GitHub Pages workflow `.github/workflows/deploy-pages.yml`.
- `finc -c` corpus remeasure: **41 of 51 samples compile to valid object code**.
- Architecture Decisions settled: ADR 0040 (nullable tagged aggregates), ADR 0041 (enums as tagged unions),
  ADR 0042 (compiler API builtins and comptime intrinsics), ADR 0043 (self-hosting LLVM IR lowering).
- Scope-exit destruction verified: destructors run via `delete` and automatically at block/function scope
  exit in reverse order (ADR 0016, ADR 0030; verified by `Soundness_Codegen.ALocalsCleanUpAtReturnInReverseOrder`
  and `ABlockLocalCleansUpAtBlockEnd`).

Rebuild with:

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

The compiler is `build/finc`. Remove generated objects after audits:

```bash
rm -f *.o tests/samples/*.o tests/samples/stdlib/*.o
```

## Held rulings (do not touch without an owner decision)

- Struct `==` is declared-only, never synthesized (ADR 0036; `deeptest4.fin` stays refused).
- `any` values stay refused; no boxing/unboxing (ADR 0034; `useful_macros.fin` stays refused).

## Remaining object/codegen blockers

Run each sample with `build/finc -c <sample>` and fix the first refusal, then remeasure.
41 of 51 samples compile.

1. **Resolved since previous handoff**:
   - `useful_macros.fin` compiles (zero diagnostics, exit code 0).
   - `stdlib/networking.fin` compiles.
   - `stdlib/typing.fin` compiles.
   - `deeptest4.fin` compiles.
   - `nullifier.fin` compiles (nullable tagged discriminant layout, ADR 0040).
   - `prototype_test.fin` compiles (struct memberwise equality).
   - `literal_interface.fin` compiles (compile-time `@implements` builtin, ADR 0042).
   - `stdlib/memory.fin` compiles (`@Alloc` and `@Free` memory intrinsics, ADR 0042).
   - `stdlib/error.fin` compiles.

2. **Frontend blockers still visible in the corpus** (not codegen failures; do not
   turn a documented sample typo into a compiler feature):
   - `enums.fin` — `Offer` (booked: declared nowhere, must not be invented) and `Ok(T)`
     designator (ADR 0037).
   - `stdlib/operators.fin` — owes `Any` import.
   - `importing.fin` — intentionally missing `somelib` module.
   - `literal_struct.fin` — undefined `st`.
   - `stdlib/collection.fin` — function variance/signature mismatch.
   - `stdlib/enums.fin` — `Enum`.
   - `stdlib/stdio.fin` — generic `X` method lookup.
   - `stdlib/stdptr.fin` — `pointer_type`.
   - `stdlib/types.fin` — `_static_string`.
   - `undefined_behavior.fin` — expected missing-return diagnostic; negative sample.

   Regenerate first diagnostics rather than copying this list.

## Accepted but not yet implemented

- **`delete &field` vs automatic field cleanup**: an explicit deallocation of a
  field with a destructor will run twice once scope exits clean it too (ADR 0016
  names this decision as the one to revisit). No corpus program hits it yet.
- **Windows full green**: MSVC component-link ruling implemented (ADR 0031) and
  `__acrt_iob_func` stderr handled, but Windows jobs now fail compiling the bison
  parser (`parser.hpp` copies move-only AST nodes; MSVC C2280, GCC/Clang move).
  A portability workstream of its own — do not fix inside language work.
- **Stale pointers**: `CMakeLists.txt` and ADRs 0025/0026 cite `docs/HANDOFF.md §8`,
  which the rewrite deleted. Point them somewhere real when touching those lines.

## Important implementation locations

- `src/codegen/CodeGen_LLVM.cpp` — LLVM emitter, lowerability checks, struct layout,
  templates, imported declarations, and explicit refusal policy.
- `src/types/Layout.cpp` / `src/types/StructType.hpp` — shared semantic layout; only
  transitive (chain-diamond) sharing is decided, fork diamonds still refuse.
- `src/driver/Driver.cpp` and `src/utils/ModuleLoader.*` — root/module boundary;
  `astStorage` owns module Programs, backend borrows them (ADR 0032).
- `src/semantics/impl/Analyzer_Decl.cpp` — struct parents, attributes, `@special`, macros.
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
