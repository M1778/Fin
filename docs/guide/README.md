# The Fin guide

A tutorial introduction to the Fin programming language, in reading order. Each chapter
assumes the ones before it and introduces every construct before it uses one.

1. [Quick start](01-quick-start.md) — building `finc`, compiling and running a hello-world,
   the flags a beginner needs, and how to read a diagnostic.
2. [Variables and types](02-variables-and-types.md) — `let`, the angle-bracket annotation,
   the builtin types, `auto`, `const`, `readonly`, type aliases, nullability, scopes.
3. [Operators and expressions](03-operators-and-expressions.md) — arithmetic, comparison,
   shifts, the conditional expression (`cond : then ? otherwise`), `cast`, `sizeof`,
   `blame`.
4. [Control flow](04-control-flow.md) — `if`/`else`, `for`, `foreach`, `while`, `do`/`while`,
   the bare brace as a scope opener, lifetimes across scopes.
5. [Functions](05-functions.md) — declaration syntax, the return type's position, parameters,
   `noret`, function types, the three lambda forms, generic functions, `@define`.
6. [Structs and classes](06-structs-and-classes.md) — fields, methods and `self`, static
   methods, visibility, generics, operator overloading, constructors, `class` and
   inheritance.
7. [Interfaces and generics](07-interfaces-and-generics.md) — interfaces, `implements` at the
   declaration and in a block, bounds, monomorphisation versus erasure, meta-types.
8. [Enums](08-enums-and-pattern-data.md) — members and discriminants, payloads, positional
   access, generic enums, attaching methods, enum reflection.
9. [Arrays and pointers](09-arrays-and-pointers.md) — `[T, N]` and `[T]`, `.length`,
   `new [T, n]`, references and pointers, `new`/`delete`, where memory management lives.
10. [Modules and imports](10-modules-and-imports.md) — the four `import` forms, the `::`
    selector, search paths, `namespace`, `pub`/`#[export]`, `extern ... as`, `#[global]`.
11. [Macros, attributes and the preprocessor](11-macros-and-preprocessor.md) — `@macro` and
    `quote`, the `#cdef` family, attributes, the `%{ ... }%` block, compiler operations.
12. [A tour of the standard library](12-standard-library-tour.md) — what is in `lib/std` and
    how to use each module.

## How to read this guide

Every code example was compiled with `finc` before it was written down. Fin is a language
under construction, and the two states a construct can be in are distinguished throughout:

- **type-checks and builds** — `finc file.fin -o out` produces a working executable. Most of
  the language is here.
- **type-checks only** — `finc file.fin` exits 0, and `-o` reports `codegen: ... is not
  lowered yet`. Where a chapter documents such a construct it says so in a sentence rather
  than leaving you to discover it.

Nothing in this guide is documented from a design draft alone. Where a draft describes
something the compiler does not do, the guide states the limitation instead of the feature.

## Where the language is defined

The sample corpus is the specification. `tests/samples/*.fin` and `tests/samples/stdlib/*.fin`
carry `//@` expectations that decide whether the compiler is at fault for a given construct,
and `docs/adr/0008-sample-authority-is-per-expectation.md` explains why authority lives in
those per-construct expectations rather than in per-file labels.

`docs/adr/` holds the architecture decisions. They are short, and each one records why a
choice was made rather than merely what it was. The ones this guide leans on most:

- `0005` — the conditional expression's operand order
- `0011` — a bare brace opens a scope
- `0018` — a constraint set is a bound, never a storage type
- `0019`, `0027` — the representation of an interface reference
- `0021` — `#[global]` and what it does and does not answer
- `0023` — the shape of a `@macro`
- `0025` — a dynamic array is a pointer and a length
- `0026` — a class is a struct with a base, and `try` is a scope

`docs/finc-interface-contract.md` specifies the compiler's command-line and diagnostic
surface for tooling. `docs/compiler-api.md` specifies the compile-time API that chapter 11's
special functions belong to, which is designed and not yet built.
