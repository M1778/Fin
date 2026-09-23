# AGENTS.md — Fin compiler (`finc`)

## What this is
Fin is a systems programming language. This repo holds the compiler. The language has
no prose specification: `tests/samples/*.fin` IS the specification (ADR 0008). Each sample
is normative or aspirational, and its `//@` header (`ok`, `error`, `unimplemented`) is the
testable expectation. A sample with no expectation is a harness fault, not a pass.

Standing goal: all runnable normative samples compile and run; `lib/std/` complete.

## Backend invariant (non-negotiable)
If codegen cannot lower a construct, it reports an explicit refusal. Never silently drop
runtime code or emit guessed IR. A refusal names the construct and, where one exists, the
ruling or gap that owns it.

## Held rulings (do not touch without an owner decision)
- Struct `==` is declared-only, never synthesized (ADR 0036; `deeptest4.fin` stays refused).
- `any` values stay refused; no boxing/unboxing (ADR 0034; `useful_macros.fin` stays refused).

## Open questions (brief with evidence; do not settle unilaterally)
- Nullable struct-field layout (`nullifier.fin`: `b? <int>` refused in codegen).
- Enum value representation (`Ok(T)` denotes the member per ADR 0037; lowering waits).
- Wave-4 compiler API (`@implements`, `@defined`, `@Alloc`...): plan before any code.

## Working rules
- Work in `/home/M1778/Fin` only. Never touch `~/finn-registry`; `~/finn` is read-only.
- Test-first: write the failing regression test, then implement. Never amend commits.
- Verify with: `cmake --build build -j2` then `ctest --test-dir build --output-on-failure`.
  Per-sample: `build/finc -c <sample>`; clean object files after sweeps
  (`rm -f *.o tests/samples/*.o tests/samples/stdlib/*.o`).
- Check `git status` / `git diff` show only intended changes before finishing.
- Do NOT commit, amend, push, or create PRs. Leave the diff for human review.
- `CMakeUserPresets.json` is tracked but must not be committed; never `git commit -a`.
- Fin vocabulary (do not rename): blame (not assert/throw), `blame m1778;` (not todo),
  compiler component / grant `#[use(...)]` (not plugin/permission), prototype `{K,V}`,
  `@special` function with `@call` (not macro), nullable `?` + denullify `e?`,
  `readonly` member, `pub:`/`priv:` visibility labels. See `CONTEXT.md`.
