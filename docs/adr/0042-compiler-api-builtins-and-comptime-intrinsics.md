# Compiler API builtins and compile-time intrinsics (@implements, @defined, @Alloc, @Free)

Compile-time special invocations (`@name(...)`) prefixed with the `@` sigil serve as
first-class builtins that bridge directly into the compiler API component surface
(`compiler.components.*` and `compiler.<component>.*`). Compile-time queries (`@implements`,
`@defined`) resolve and evaluate at analysis time to constant values, while low-level
runtime allocation intrinsics (`@Alloc`, `@Free`) map to compiler-provided memory operations.

## Context & Evidence

Four distinct built-in `@`-call forms appear across normative samples and the standard library:
1. `@implements(struct_, iface)` (`tests/samples/literal_interface.fin:6`): Queries whether a
   given struct type or value satisfies an interface at compile time. Backed by
   `compiler.types.implements(t: $type, i: $interface) <bool>` (docs/compiler-api.md §2.4).
2. `@defined("name")` (`tests/samples/literal_struct.fin:29`): Tests whether a declaration exists
   in the symbol table at compile time. Backed by `compiler.symbols.defined(name: string) <bool>`
   (docs/compiler-api.md §2.6).
3. `@Alloc(size)` (`tests/samples/stdlib/memory.fin:17`): Raw memory allocation intrinsic returning
   an unmanaged pointer (`&void` / `*void`). Backed by `compiler.memory.alloc` (docs/compiler-api.md §2.6).
4. `@Free(ptr)` (`tests/samples/stdlib/memory.fin:22`): Raw memory deallocation intrinsic. Backed by
   `compiler.memory.free` (docs/compiler-api.md §2.6).

In the grammar (`src/parser/parser.y:2386-2397`), these already parse into `fin::FunctionCall` with
`call->is_special = true`.

## Considered Options

- **Require full comptime tree-walking interpreter (Wave 4) before any builtin works:**
  Postpones resolving any `@` special calls until the complete interpreter is implemented.
  Rejected because queries like `@implements(struct, iface)` and `@defined("name")` are static
  questions about compiler symbol/type tables that the analyzer already possesses. Deferring them
  unnecessarily blocks normative samples (`literal_interface.fin`, `literal_struct.fin`, `stdlib/memory.fin`).
- **Free-standing ad-hoc hardcoded analyzer builtins:**
  Hardcoding each `@` builtin as a standalone special case in `Analyzer_Expr.cpp` disconnected
  from the component model. Rejected because ADR 0012 and docs/compiler-api.md establish that all
  compiler capabilities belong to organized component namespaces (`types`, `symbols`, `memory`).
- **Unified `@` builtin mapping to compiler API components:**
  Selected. Every `@name(...)` call routes through the corresponding component in `CompilerApi.cpp`.
  Compile-time queries evaluate their predicates against the semantic environment and fold into constant
  literals (`bool`). Allocation intrinsics map to typed function signatures in the analyzer and lower
  to runtime memory primitives in codegen.

## Consequences

- **Syntax & AST:** `FunctionCall::is_special` denotes an intrinsic call.
- **Semantic Analyzer (`src/semantics/`):**
  - `@implements(t, i)`: Checks that `t` is a `$type` / `$struct` (or struct type) and `i` is a
    `$interface` (or interface type). Type-checks and evaluates whether `t` satisfies all methods
    and fields of `i`. Returns a constant `bool` value.
  - `@defined(name)`: Checks that `name` is a string literal. Queries `currentScope->resolve(name)`
    or `resolveType(name)`. Returns a constant `bool` value.
  - `@Alloc(size)`: Validates argument is integer/uint. Types result as `&void` (or generic pointer).
  - `@Free(ptr)`: Validates argument is pointer. Types result as `void` / `noret`.
- **Comptime Branch Elimination:** Because `@implements` and `@defined` fold to boolean constants,
  conditional expressions (`if (@implements(...))`, `if (!@defined("printf"))`) evaluate their
  guards statically, enabling the protected branches to be analyzed or gated accurately.
- **LLVM CodeGen (`src/codegen/CodeGen_LLVM.cpp`):**
  - Evaluated comptime constants lower as `llvm::ConstantInt` (i1).
  - `@Alloc(size)` lowers to `malloc(size)` (or target platform memory primitive).
  - `@Free(ptr)` lowers to `free(ptr)`.
