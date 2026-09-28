# The Fin language guide

For an AI agent writing an application, start with the shorter
[agent guide](../agent-guide.md). These chapters explain individual features and
include complete programs you can copy into a `.fin` file.

| Chapter | Contents |
| --- | --- |
| [1. Quick start](01-quick-start.md) | Build the compiler, compile a program, diagnose setup failures |
| [2. Variables and types](02-variables-and-types.md) | Annotations, scalar widths, conversion, constants, nullability |
| [3. Expressions](03-operators-and-expressions.md) | Operators, conditional order, casts, assertions, error handling |
| [4. Control flow](04-control-flow.md) | Branches, loops, scopes, recursion |
| [5. Functions](05-functions.md) | Parameters, callbacks, lambdas, defaults, foreign functions |
| [6. Structs and classes](06-structs-and-classes.md) | Fields, methods, construction, cleanup, inheritance |
| [7. Interfaces and generics](07-interfaces-and-generics.md) | Bounds, implementations, runtime interface references |
| [8. Enums](08-enums-and-pattern-data.md) | Tags, payload syntax, runtime limits |
| [9. Arrays and pointers](09-arrays-and-pointers.md) | Buffers, prototypes, allocation, ownership |
| [10. Modules](10-modules-and-imports.md) | Imports, names, paths, multi-file limits |
| [11. Macros and preprocessing](11-macros-and-preprocessor.md) | Quotes, `format!`, attributes, compiler components |
| [12. Standard library](12-standard-library-tour.md) | Module names, signatures, usable paths and limits |

## What an example promises

A plain `fin` code fence is a **complete runnable program**. Save it as `example.fin`,
run `finc example.fin -o example`, then run the executable. A following `output`
block gives exact stdout. Some programs instead check their result using `blame`.

Other fences name their scope explicitly:

| Fence | Meaning |
| --- | --- |
| `fin fragment` | Syntax excerpt; surrounding declarations or files are required |
| `fin check` | Complete input that must pass frontend checking; no claim about execution |
| `fin error` | Deliberately rejected by the frontend |
| `fin build-error` | Passes frontend checking; code generation must reject it |

Run all classified examples from the repository root:

```sh
python3 tests/tools/check_docs.py --finc build/finc
```

The checker builds and runs every runnable fence, compares displayed stdout, and
checks the expected exit code of negative examples. Fragments are explicitly
excluded. It uses this checkout's `lib/std` and temporary output files.

## Implementation versus design

The [ADRs](../adr/) record intended language rules. The `//@` expectations in
[the sample corpus](../../tests/samples/) specify individual accepted or rejected
constructs; an `unimplemented` sample is not a working application template.
[Backend tests](../../tests/test_codegen.cpp) check generated programs.

[The compiler API document](../compiler-api.md) contains design beyond the executed
compiler. Library headers and old handoff notes can also describe earlier states;
use current code and runnable checks to settle implementation questions. Report
contradictions instead of silently treating a bug as a new language rule.