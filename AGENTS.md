# Working in the Fin repository

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

## Evidence and boundaries

The active compiler is in `src/`, the shipped library is in `lib/std/`, and
`tests/test_*.cpp` contains executable regression coverage. `pyprototype/` and
`legacy/` are historical implementations, not evidence of current Fin behavior.
Do not change them for a current compiler task.

Samples under `tests/samples/` carry `//@` expectations. Read each expectation:
`ok`, `error`, and `unimplemented` have different meanings. An `ok` type-check
does not prove code generation works. Keep the expected language behavior intact
when fixing the compiler; record intentional language changes in an ADR.

Work in the current checkout. Leave unrelated local files and external `finn`
or registry repositories alone. Leave changes uncommitted for review unless the
user asks for a commit or PR. Machine-generated CMake/Conan files, including
`CMakeUserPresets.json`, are not source changes.

## Compiler map and checks

`src/driver/Driver.cpp` runs preprocessing, parsing, macro expansion, semantic
analysis, and code generation. Imports go through `src/utils/ModuleLoader.cpp`.
Grammar lives in `src/parser/parser.y` and `src/lexer/lexer.l`; semantics live in
`src/semantics/impl/`; layout lives in `src/types/Layout.cpp`. LLVM lowering lives
in `src/codegen/CodeGen_LLVM.cpp`. Unsupported runtime operations must produce
diagnostics, never silently disappear.

Use the configured build directory and its platform. The normal source build is
`./build.sh`; it uses the LLVM major pinned in `CMakeLists.txt`. A backend-disabled
build cannot validate executable behavior. For compiler changes, add a failing
regression before the implementation, then run the affected tests and the suite:

```sh
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure --no-tests=error
```

For documentation changes, run `uv run --no-project tests/tools/check_docs.py --finc build/finc`
(use `build/finc.exe` on native Windows).
Keep all Fin fences in the README, agent guide, and language guide classified:
plain `fin` means a complete runnable program; `fin fragment`, `fin check`,
`fin error`, and `fin build-error` name the exceptions. A following `output` fence
is the exact expected stdout. Add behavior assertions with `blame` when output
alone would not catch a wrong result. Describe limitations beside the affected API.
