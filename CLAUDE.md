# CLAUDE.md — Fin compiler (`finc`)

Agent instructions of record live in **`AGENTS.md`** (working rules) and
**`CONTEXT.md`** (the language's controlled vocabulary). This file adds what
those two do not cover: an architecture map and build/test quick reference.
On any conflict, `AGENTS.md` wins. When writing a program **in Fin**, start
with [docs/agent-guide.md](docs/agent-guide.md).

## Tech stack
- C++20 (hard floor; configure fails below), CMake ≥ 3.20, Conan toolchain
  (`conan/profiles/fin` pins gnu20).
- Backend: LLVM 22, single-major pin (ADR 0010). `FIN_WITH_LLVM=OFF` swaps in
  `src/codegen/CodeGen_Stub.cpp`, which refuses `-o` with a diagnostic —
  refusals, never silently empty artifacts.
- Parser: Bison 3.2 (`src/parser/parser.y`) + Flex (`src/lexer/lexer.l`).
- Tests: GTest (`fin_tests`) + the `tests/samples/` expectation corpus.

## Pipeline
```
preprocessor → lexer → parser → AST → macro expander (@special)
  → SemanticAnalyzer (src/semantics/impl/) → types/Layout
  → CodeGen_LLVM (or CodeGen_Stub) → Driver (options, ModuleLoader)
```

## Key entry points
- `src/main.cpp` / `src/driver/Driver.cpp` — CLI
- `src/semantics/impl/Analyzer_{Core,Decl,Expr}.cpp` — semantic phases
- `src/types/Layout.cpp` — struct layout (ADR 0015: layout is two moments)
- `src/codegen/CodeGen_LLVM.cpp` — lowering; **explicit refusals only**,
  never silently dropped runtime code (backend invariant, AGENTS.md)
- `lib/std/` — the Fin standard library, written in Fin
- `tests/samples/*.fin` — the language specification (ADR 0008); each sample
  needs a `//@` header (`ok` / `error <line>:<col> "<msg>"` /
  `unimplemented "<reason>"`); missing expectation = harness fault
- `docs/adr/` — 42 binding numbered rulings; behavior changes trace to an ADR

## Verify commands
```sh
cmake --build build -j2
ctest --test-dir build --output-on-failure
build/finc -c tests/samples/<name>.fin          # one sample
rm -f *.o tests/samples/*.o tests/samples/stdlib/*.o   # after object sweeps
tests/tools/corpus_snapshot.sh                  # corpus state
```

## Hard rules (restated; see AGENTS.md for the full list)
- Work in `/home/M1778/Fin` only. Never touch `~/finn-registry`; `~/finn`
  is read-only.
- Do NOT commit, amend, push, or create PRs. Leave the diff for review.
  Never `git commit -a` — `CMakeUserPresets.json` is tracked but must never
  be committed.
- Test-first: failing regression test before implementation. Never amend.
- Held rulings (owner decision required): struct `==` declared-only
  (ADR 0036); `any` values refused (ADR 0034). Open questions (do not settle
  unilaterally): nullable struct-field layout (`nullifier.fin`), enum value
  representation (ADR 0037), wave-4 compiler API.
