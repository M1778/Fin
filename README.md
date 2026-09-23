# Fin Programming Language (`finc`)

Fin is a modern, statically typed systems programming language designed for clarity, safety, and uncompromising performance. The compiler (`finc`) lowers directly to LLVM Intermediate Representation (IR), producing optimized native machine code with zero foreign runtime dependencies.

The language specification is normative: `tests/samples/*.fin` defines the language behavior and testable expectations (ADR 0008).

---

## Key Language Features & Idioms

- **Explicit Verification with `blame`**: Fin uses `blame <condition>;` to verify invariants at runtime or compile time (never `assert` or `throw`). Unimplemented markers are written as `blame m1778;` (never `todo`).
- **Null Safety & Denullification**: Value types and pointers express nullability via `?` (e.g. `int?`, `Node*?`). Safe unwrapping is performed with postfix denullify `e?`, which evaluates the value or triggers runtime blame on null absence (ADR 0040).
- **Tagged Union Enums**: Enums support plain discriminants, member payloads (e.g., `RGB(uint{8}, uint{8}, uint{8})`), and generic variants (e.g., `Result<T, U>`). Enums lower to efficient tagged unions `{ i32, [MaxPayload x i8] }` (ADR 0041). Member comparisons (`e == Ok(T)`) and reflection (`getkeyid`) are fully supported.
- **Prototypes `{K,V}`**: Type parameters, generic templates, and prototypes enable expressive generic data structures and algorithms.
- **`readonly` Members**: Immutable fields are declared with `readonly` (e.g., `pub readonly count <int>`). Mutations from outside the declaring struct's scope are strictly rejected at compile time.
- **Visibility Labels**: Fine-grained visibility within structs, classes, and namespaces is defined using `pub:` and `priv:` labels.
- **Operator Overloading**: Overload arithmetic (`operator +`), comparisons, and indexing (`operator []`, `operator []=`), which lower directly to machine code.
- **Compiler Operations & API Builtins**: `@special` functions, compile-time predicates (`@implements(struct, iface)`, `@defined("name")`), and low-level memory intrinsics (`@Alloc(size)`, `@Free(ptr)`) provide direct access to compiler introspection and runtime primitives (ADR 0042).
- **Self-Hosting Compiler Architecture**: The self-hosting compiler lowers Fin ASTs directly to in-memory LLVM IR via LLVM-C (`finc/llvm.fin`), strictly refusing intermediate C source code generation (ADR 0043).

---

## Standard Library (`lib/std/`)

Fin ships with a comprehensive standard library of 37 standalone modules located in `lib/std/`. All 37 modules check cleanly standalone (`finc lib/std/<mod>.fin`) and lower to native executables:

| Domain | Modules |
| --- | --- |
| **Core & Types** | `types`, `typing`, `enums`, `operators`, `error` |
| **Memory & Buffers** | `stdptr` (`rptr<T>`), `buffer` (`Buffer`, `StringBuilder`), `memory` |
| **Collections** | `collection` (`Collection<T>`), `hashmap` (`HashMap<K, V>`), `deque` (`Deque<T>`, `Queue<T>`, `Stack<T>`) |
| **Algorithms & Math** | `algorithm` (sorting, binary search, partition), `math`, `random`, `hash` (CRC32, FNV-1a), `bits` |
| **Strings & Text** | `strings`, `ascii`, `encoding` (hex, base64) |
| **I/O, OS & System** | `stdio` (`printf`, `print`, `Stream`), `fs` (files, streams), `path`, `env`, `time`, `process`, `networking` (TCP sockets) |
| **CLI & Driver** | `argparse` (`ArgParser`), `testing` (unit test assertions) |
| **Compiler & Tooling** | `token`, `scanner`, `parse`, `ast`, `scope`, `typesys`, `ir`, `printer`, `diag`, `emitter` |

---

## Building the Compiler

### Prerequisites
- CMake 3.20+
- Clang / LLVM (version pinned in `CMakeLists.txt`)
- Flex and Bison
- Conan package manager

### Quick Start
Build the compiler using the root build script:

```bash
./build.sh
```

Flags:
- `./build.sh --release` — Build in Release mode.
- `./build.sh --clean`   — Clean the build directory before building.
- `./build.sh --no-test` — Skip running the test suite during build.

Alternatively, build and test using CMake directly:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

The resulting compiler binary is located at `build/finc`.

---

## Usage

### Check Source Syntax & Types
```bash
build/finc hello.fin
```

### Compile to Native Executable (Contract 2)
```bash
build/finc hello.fin -o hello
./hello
```

### Compiler Interface Contract
Tooling and package managers interact with `finc` according to **Contract 2** (`finc --version` outputs `finc 0.4.0 (contract 2)`). Full command-line options, JSONL diagnostics (`--diagnostics=json`), exit code specifications, and search path resolution orders are documented in [`docs/finc-interface-contract.md`](docs/finc-interface-contract.md).

---

## Documentation

Detailed language and architectural guides are available in `docs/`:

- [**The Fin Guide**](docs/guide/README.md) — 12-chapter comprehensive walkthrough:
  1. [Quick Start](docs/guide/01-quick-start.md)
  2. [Variables and Types](docs/guide/02-variables-and-types.md)
  3. [Operators and Expressions](docs/guide/03-operators-and-expressions.md)
  4. [Control Flow](docs/guide/04-control-flow.md)
  5. [Functions](docs/guide/05-functions.md)
  6. [Structs and Classes](docs/guide/06-structs-and-classes.md)
  7. [Interfaces and Generics](docs/guide/07-interfaces-and-generics.md)
  8. [Enums](docs/guide/08-enums-and-pattern-data.md)
  9. [Arrays and Pointers](docs/guide/09-arrays-and-pointers.md)
  10. [Modules and Imports](docs/guide/10-modules-and-imports.md)
  11. [Macros, Attributes and the Preprocessor](docs/guide/11-macros-and-preprocessor.md)
  12. [A Tour of the Standard Library](docs/guide/12-standard-library-tour.md)
- [**Compiler Interface Contract**](docs/finc-interface-contract.md) — Tooling ABI, JSON diagnostics, and CLI contract.
- [**Architecture Decision Records (ADRs)**](docs/adr/) — Key language and compiler design decisions (ADRs 0001–0043).
- [**Handoff Notes**](docs/HANDOFF.md) — Current development status, corpus measurements, and implementation pointers.
