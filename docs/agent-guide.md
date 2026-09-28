# Writing working Fin programs

Give this file to an AI agent working on a Fin application. It describes the
compiler in this repository, version 0.4.0. Use `finc --version` to identify your
binary; a version number alone does not identify the source revision of a local
build. Fin syntax resembles several languages but does not follow all their rules.

## Start with an executable

```fin
fun main() <noret> {
    let values <[int]> = [2, 4, 6];
    let total <int> = 0;
    foreach (value <int> in values) {
        total += value;
    }
    blame total == 12;
    printf("total = %d\n", total);
    delete values;
}
```

```output
total = 12
```

`printf` is available through the bundled standard library without an import.
`noret` means no return value. A `[T]` is an array handle with a pointer and length;
the example frees its buffer explicitly.

Use this workflow for each feature you add:

1. Check `finc --version` and build a small `main` with `finc main.fin -o app`.
   A no-backend build cannot generate executables. Linking needs `cc` on `PATH`,
   or a C compiler driver selected through `FIN_CC`.
2. Find the feature in the topic table below. For a library API, read its actual
   declaration in `lib/std/<module>.fin` before calling it.
3. Run `finc main.fin` for syntax and types, then `finc main.fin -o app` for code
   generation and linking. Run the resulting executable and check its result.
4. Report the commands and what ran. If a feature only type-checks, describe that
   limit; use a supported pattern or report the missing behavior explicitly.

For native Windows executables, use `app.exe` and `.\app.exe` from a Visual Studio
Developer shell with `clang` on `PATH`. See [setup](guide/01-quick-start.md) for the
native PowerShell/uv build commands.

## Syntax to keep in context

The following entries are **fragments**, not complete files.

| Intent | Fin spelling |
| --- | --- |
| Variable / inference | `let count <int> = 0;` / `let count <auto> = 0;` |
| Constant binding | `const LIMIT <int> = 10;` |
| Function | `fun add(a: int, b: int) <int> { return a + b; }` |
| No result | `fun log(message: string) <noret> { printf("%s\n", message); }` |
| Branch | `if (count > 0) { count--; } else { count = 1; }` |
| Conditional value | `let limit <int> = enabled : 10 ? 0;` |
| Counted loop | `for (let i <int> = 0; i < 3; i++) { ... }` |
| Array loop | `foreach (i <int>, value <int> in values) { ... }` |
| Fixed / dynamic array | `let a <[int, 2]> = [1, 2];` / `let a <[int]> = [1, 2];` |
| Pointer / dereference | `let p <&int> = &count;` / `*p = 4;` |
| Heap allocation | `let p <&int> = new int(4);` / `delete p;` |
| Struct and value | `struct Point { x <int>, y <int> }` / `Point{x: 1, y: 2}` |
| Instance method | `fun sum(self: &Self) <int> { return self.x + self.y; }` |
| Generic declaration | `fun identity<T>(value: T) <T> { return value; }` |
| Generic use | `identity::<int>(7)` / `Box::<int>{value: 7}` |
| Function value | `let f <fn(int) -> int> = (x: int) <int> => x * 2;` |
| Import | `import { Collection } from collection::std;` |
| Module alias / call | `import stdio::std as io;` / `io.printf("hi\n");` |
| Prototype (structural map) | `let p <{string, int}> = {"one": 1};` |
| Enum member | `enum State { Ready, Busy }` / `State::Ready` |
| Cast | `cast<double>(1.5)` |
| Assertion | `blame count >= 0, "count must be nonnegative";` |
| Foreign function | `@define strlen(text: string) <ulong>;` |
| Macro declaration / call | `@macro twice(x) { return quote { $x + $x; }; }` / `twice!(3)` |

Declarations use angle brackets, parameters use colons, and a function's return
type comes **after** its parameters. Generic types use `Box<int>` in annotations
and `Box::<int>` in expressions. Statements end with `;`; struct fields use commas.

Fin's conditional order is `condition : value_if_true ? value_if_false`.
Keep that order when translating code from another language.

## Behavior that changes application design

- **Strings:** `string` holds a pointer to NUL-terminated bytes. General `==`
  compares string addresses. Use C's `strcmp` for byte equality or check whether
  the `strings::std` API you need builds in your compiler. `string` indexing and
  `.length` are not the same operations as array indexing and length.
- **Memory:** `[T]`, pointers, and interface references can share storage. Copying
  an array handle does not copy its elements. Keep the owner alive and free an
  allocation once. Value structs with destructors clean up at scope exit; copying
  an owning struct can therefore double-free its resources. There is no borrow checker.
- **Generics:** concrete unbounded generics produce specialized code. Erasure
  through `any`, `object`, or `Castable` is not a general runtime container mechanism.
  Import the exact type or interface name an API uses; `Any` and `any` differ.
- **Errors:** `blame bool, "message"` aborts on failure. Runtime exception handling
  is incomplete: a `catch` does not recover from `blame`. Use explicit status values
  for recoverable application errors.
- **Modules:** imports resolve more declarations than the backend can emit.
  Imported generic structs can instantiate; ordinary imported Fin function bodies
  are not emitted into the caller's executable. Prove a multi-file path with a
  small build before splitting application logic across it.
- **Foreign calls:** `@define` declares a linker symbol, not an implementation.
  Match the real C ABI. Variadic calls such as `printf` have no format checking;
  use `printf("%s", text)` for variable text.

## Current limits and usable alternatives

| Feature | Current boundary / application pattern |
| --- | --- |
| `match`, async/await | No language implementation. Use explicit branches and synchronous calls. |
| Capturing lambdas | Pass state as an explicit parameter; lambdas and nested functions do not capture locals. |
| Default arguments | Frontend accepts omitted trailing defaults; supply arguments explicitly for executable calls. |
| Payload or generic enums | Type-checking exists; backend refuses their runtime representation. Use a plain enum plus a concrete struct if needed. |
| `any` / `object` values | Opaque storage is not working boxing or dynamic dispatch. Prefer concrete types. |
| General nullable values | Parse/type-check support is broader than runtime support. Check the exact form before using it. |
| Dynamic array indexing | Runtime bounds checks are not automatic. Check the index or use a library method that asserts bounds. |
| Bitwise `&`, `\|`, `^` | Binary expression forms are absent; `&` still takes an address and `\|` forms a constraint set. |
| `format!` | Works for literal format strings with `{}` and scalar arguments; no aggregate formatting or format specifiers. Returned storage needs ownership. |
| `File`, `Result`, smart pointers | Declarations exist in the library. That does not prove all methods can build through an import. Consult the library chapter. |
| Compiler components / events / GC | The API design includes unimplemented execution behavior. Do not assume a collector or compile-time event handler runs. |

## Find the right detail

| Need | Read |
| --- | --- |
| Build, run, linker errors, JSON diagnostics | [Quick start](guide/01-quick-start.md) |
| Scalar types, widening, `readonly`, nullability | [Variables and types](guide/02-variables-and-types.md) |
| Casts, comparisons, `blame`, conditional expressions | [Expressions](guide/03-operators-and-expressions.md) |
| Loops, scopes, early exits | [Control flow](guide/04-control-flow.md) |
| Callbacks, generic functions, defaults, C ABI | [Functions](guide/05-functions.md) |
| Data models, methods, constructors, cleanup | [Structs and classes](guide/06-structs-and-classes.md) |
| Bounds, interfaces, erasure | [Interfaces and generics](guide/07-interfaces-and-generics.md) |
| Enum tags and payload limitations | [Enums](guide/08-enums-and-pattern-data.md) |
| Buffers, prototypes, pointers, allocation | [Arrays and pointers](guide/09-arrays-and-pointers.md) |
| Project structure and import resolution | [Modules](guide/10-modules-and-imports.md) |
| `format!`, quoted macros, preprocessing, attributes | [Macros](guide/11-macros-and-preprocessor.md) |
| Available standard-library declarations | [Library reference](guide/12-standard-library-tour.md) |

## When a build fails

`finc file.fin` checks the frontend only. An error beginning `codegen:` under
`-o` names a backend limit. Reduce the construct to a small file before rewriting
unrelated code. A `link failed` message means object generation succeeded; check
`FIN_CC` and external symbols. A `module not found` message lists searched paths.

`-I path` adds a search directory. `--fin-libs` replaces `FIN_LIBS` and suppresses
the bundled library, even when explicitly empty. If you pin library paths, include
the real `lib/std` path too. Quoted imports resolve relative to the importing file;
pathless imports use the configured search paths.

For contradictory references, check the active `src/` implementation and executable
tests. `tests/samples/` contains both accepted and deliberately unimplemented cases;
read the `//@` header. `pyprototype/`, `legacy/`, roadmap files, and historical source
comments do not establish current support. The language's intended rules live in
the ADRs; implementation bugs do not change those rules.

Application completion means the intended entry point builds and runs, its behavior
has been checked, and any remaining compiler limitation is named explicitly.
