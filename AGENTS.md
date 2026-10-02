# AGENTS.md — Fin compiler (`finc`)

## What this is
Fin is a systems programming language. This repo holds the compiler. The language has
no prose specification: `tests/samples/*.fin` IS the specification (ADR 0008). Each sample
is normative or aspirational, and its `//@` header (`ok`, `error`, `unimplemented`) is the
testable expectation. A sample with no expectation is a harness fault, not a pass.

The active compiler is in `src/`, the shipped library is in `lib/std/`, and
`tests/test_*.cpp` contains executable regression coverage. `pyprototype/` and
`legacy/` are historical implementations, not evidence of current Fin behavior.
Do not change them for a current compiler task.

Samples under `tests/samples/` carry `//@` expectations. Read each expectation:
`ok`, `error`, and `unimplemented` have different meanings. An `ok` type-check
does not prove code generation works. Keep the expected language behavior intact
when fixing the compiler; record intentional language changes in an ADR.

Standing goal: all runnable normative samples compile and run; `lib/std/` complete.

## Backend invariant (non-negotiable)
If codegen cannot lower a construct, it reports an explicit refusal. Never silently drop
runtime code or emit guessed IR. A refusal names the construct and, where one exists, the
ruling or gap that owns it. Unsupported runtime operations must produce
diagnostics, never silently disappear.

## Held rulings (do not touch without an owner decision)
- Struct `==` is declared-only, never synthesized (ADR 0036; `deeptest4.fin` stays refused).
- `any` boxes with value semantics (ADR 0034 amendment; `useful_macros.fin` builds).

## Open questions (brief with evidence; do not settle unilaterally)
- Nullable struct-field layout (`nullifier.fin`: `b? <int>` refused in codegen).
- Enum value representation (`Ok(T)` denotes the member per ADR 0037; lowering waits).
- Wave-4 compiler API (`@implements`, `@defined`, `@Alloc`...): plan before any code.

## Choose the right documentation
- **Writing Fin applications or examples:** read [docs/agent-guide.md](docs/agent-guide.md)
  before generating code. Follow its topic links for the feature you need.
- **Changing compiler behavior:** read the relevant tests and [ADRs](docs/adr/).
  [CONTEXT.md](CONTEXT.md) defines project terminology; it also describes planned
  concepts, so it is not a feature-support reference.
- **Changing CLI behavior:** preserve [the machine contract](docs/finc-interface-contract.md).
- **Changing compile-time facilities:** [docs/compiler-api.md](docs/compiler-api.md)
  is the design; verify implemented operations in `src/semantics/CompilerApi.cpp`
  and `src/semantics/impl/Analyzer_CompilerApi.cpp`.

## Compiler map
`src/driver/Driver.cpp` runs preprocessing, parsing, macro expansion, semantic
analysis, and code generation. Imports go through `src/utils/ModuleLoader.cpp`.
Grammar lives in `src/parser/parser.y` and `src/lexer/lexer.l`; semantics live in
`src/semantics/impl/`; layout lives in `src/types/Layout.cpp`. LLVM lowering lives
in `src/codegen/CodeGen_LLVM.cpp`.

## Working rules
- Work in `/home/M1778/Fin` only. Never touch `~/finn-registry`; `~/finn` is read-only.
  Leave unrelated local files and external `finn` or registry repositories alone.
- Test-first: write the failing regression test, then implement. Never amend commits.
- Check `git status` / `git diff` show only intended changes before finishing.
- Do NOT commit, amend, push, or create PRs. Leave the diff for human review;
  only commit or open a PR when the user explicitly asks.
- `CMakeUserPresets.json` is tracked but must not be committed; never `git commit -a`.
  Machine-generated CMake/Conan files, including `CMakeUserPresets.json`, are not
  source changes.
- Fin vocabulary (do not rename): blame (not assert/throw), `blame m1778;` (not todo),
  compiler component / grant `#[use(...)]` (not plugin/permission), prototype `{K,V}`,
  `@special` function with `@call` (not macro), nullable `?` + denullify `e?`,
  `readonly` member, `pub:`/`priv:` visibility labels. See `CONTEXT.md`.

## Verify
- Normal source build: `./build.sh`; it uses the LLVM major pinned in `CMakeLists.txt`.
  A backend-disabled build cannot validate executable behavior.
- Configured build directory (and its platform): `cmake --build build -j2` then
  `ctest --test-dir build --output-on-failure`.
  Per-sample: `build/finc -c <sample>`; clean object files after sweeps
  (`rm -f *.o tests/samples/*.o tests/samples/stdlib/*.o`).
- Documentation changes: `uv run --no-project tests/tools/check_docs.py --finc build/finc`
  (use `build/finc.exe` on native Windows).
- Keep all Fin fences in the README, agent guide, and language guide classified:
  plain `fin` means a complete runnable program; `fin fragment`, `fin check`,
  `fin error`, and `fin build-error` name the exceptions. A following `output` fence
  is the exact expected stdout. Add behavior assertions with `blame` when output
  alone would not catch a wrong result. Describe limitations beside the affected API.
