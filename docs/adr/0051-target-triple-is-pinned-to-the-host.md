# Target triple is pinned to the host

## Context

Issue #39 asked whether `--target <triple>` should be supported for non-host targets (e.g.
`wasm32-unknown-unknown`) or refused. The compiler had no `--target` flag at all, and
`generateObject` hardcoded the host triple via `llvm::sys::getDefaultTargetTriple()` (line
15747 of `src/codegen/CodeGen_LLVM.cpp`). The Driver called `generateObject` with no target
argument (lines 375 and 387 of `src/driver/Driver.cpp`).

A silent host build for a requested non-host target is the failure mode this ADR closes:
a caller asking for wasm gets an x86-64 object and has no way to know it did, which is
strictly worse than a refusal.

## Decision

**Refuse by name.** Any `--target` whose triple differs from the host triple is refused at
the codegen stage with a diagnostic naming the unsupported target. The empty value (no flag)
proceeds normally, using the host triple as before.

### How the comparison is made

The host triple is `llvm::sys::getDefaultTargetTriple()`, wrapped in `llvm::Triple`. The
requested triple is parsed the same way and compared as `llvm::Triple` values — so
`--target x86_64-pc-linux-gnu` (exactly the host) is accepted, while `--target wasm32` is
not. `llvm::Triple`'s `operator!=` handles normalization (e.g. `wasm32` vs `wasm32-unknown-unknown`),
so the comparison is robust to the different spellings a caller might try.

### Where the refusal lives

The check is inside `generateObject` in both backends:

- **LLVM backend (`CodeGen_LLVM.cpp`):** the refusal fires after the host triple is parsed
  but before `lookupTarget` and before any instruction is emitted. This means a refused
  target fails before any object file is written — the object cleanup in `Driver::runCodeGen`
  removes nothing because nothing was written.

- **Stub backend (`CodeGen_Stub.cpp`):** the stub already refuses any `-o`/`-c` request
  (it emits "this finc was built without a backend"). The `--target` parameter is accepted
  in the signature but the stub's existing refusal message covers it — a build with
  `FIN_WITH_LLVM=OFF` cannot produce any object, host or otherwise.

In both cases, the refusal uses `diag.reportError(...)`, which integrates with the
`DiagnosticEngine` and the JSON diagnostic schema — the caller sees a structured error,
not a raw print.

### Why the codegen stage, not the CLI parse

`--target` is a CLI-only flag, not a per-file language construct, so it cannot be pinned
by a `//@` expectation in `tests/samples/`. The `targetTriple` field in `CompilerOptions`
is threaded through `Driver::runCodeGen` into `generateObject` as a defaulted parameter, so
the refusal is a codegen-stage refusal. This follows the existing pattern for `-o`: the
decision to emit happens in `generateObject`, not in argument parsing.

## Consequences

- A caller that passes `--target wasm32-unknown-unknown` gets exit code 1 and a diagnostic
  that names both the unsupported triple and the host triple, with guidance to remove the
  flag. No object file is written.
- A caller that passes `--target <exact-host-triple>` gets a successful build, same as
  before. The host triple is obtained at runtime from LLVM, so the accepted value follows
  whichever host the binary runs on.
- Adding a non-host target in the future means removing the `!=` comparison and wiring the
  triple into `createTargetMachine` and `module.setTargetTriple` — the plumbing is already
  in place.
- The `tests/samples/target_refusal.fin` sample pins the program text (a valid Fin program)
  that serves as the input to the CLI test. The `//@ ok` expectation confirms the program
  itself is well-formed; the refusal is asserted in `tests/test_cli.cpp`, which invokes
  `finc --target wasm32-unknown-unknown -c tests/samples/target_refusal.fin` and checks the
  exit code and diagnostic text.

## Held rulings (open for the owner)

- Whether `--target` with the exact host triple should warn (it currently does not — it is
  accepted silently, the same as omitting the flag). This was the smallest change that
  satisfies the "no silent host binary" guarantee.
