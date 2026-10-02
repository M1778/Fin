# 11. Macros, attributes, and preprocessing

## Expression macros

A `@macro` rewrites syntax. Its body returns one quoted expression; `$parameter`
inserts an argument. Invoke it with `name!(...)`.

```fin
@macro twice(value) {
    return quote { $value + $value; };
}

fun main() <noret> {
    blame twice!(6) == 12;
}
```

The declaration has no runtime symbol and does not prevent executable generation.
`twice!(side_effect())` would evaluate the argument twice: substitution does not
create a temporary value. Use an ordinary function when evaluation count matters.

The quoted body may contain parameters, literals, operators, and qualified names.
Bare unqualified names in the macro body are rejected to prevent accidental capture.

```fin error
@macro add_hidden(value) {
    return quote { hidden + $value; };
}
```

Quotes currently contain one expression. Arbitrary statement-generating macros
and the larger compiler-event design are separate capabilities.

## Bracket-shaped arguments and imports

`macro![a, b]` passes one prototype with integer keys; `macro!{"key" => value}`
passes one prototype with written keys. These are complete calls, not variadic
argument lists. Shaped calls permit trailing commas and empty prototypes with a
suitable annotation.

```fin
@macro entries(items) { return quote { $items; }; }

fun main() <noret> {
    let list <{int, int}> = entries![10, 20];
    blame list[0] == 10 && list[1] == 20;
    delete list.0;
    delete list.1;
}
```

For a macro in another file, use a selected import or a qualified module call:

```fin fragment
import { shout } from "macros.fin";
import "macros.fin" as macros;
// shout!(value) or macros.shout!(value)
```

Wildcard imports do not carry user macros. `pub @macro` is not valid syntax;
a module exports its macro declarations through their supported import forms.
Qualified names in a macro can depend on imports of the defining module, but
call-site names can still shadow them; do not assume stronger hygiene than that.

## `format!`

`format!` is a compiler builtin. It needs no declaration or import. A literal
format string contains `{}` placeholders, one per argument.

```fin
#[llvm_name="free"]
@define free_text(text: string) <noret>;

fun main() <noret> {
    let message <string> = format!("{} squared is {}", 7, 49);
    printf("%s\n", message);
    free_text(message);
}
```

```output
7 squared is 49
```

Integers, floats, strings, and pointers have formatting paths. A `bool` prints
numerically, and a `char` prints its numeric value. Aggregates do not format.
`%` is ordinary text here. `{0}`, `{:x}`, and a mismatched placeholder count are
refused during code generation.

```fin build-error
fun main() <noret> {
    printf("%s", format!("{:x}", 15));
}
```

A runtime format string also cannot build. The optional declaration
`@define format!(fmt: string, ...) <string>;` records the builtin signature;
it does not implement a new macro. Unknown bodyless macro declarations are errors.

The returned string is heap-allocated. The example frees its own result through
C's `free`. Never free a string literal or borrowed string this way.

## Text preprocessing

Preprocessing happens before parsing. `#cdef` substitutes source text;
`#c_ifdef`, `#c_else`, and `#c_endif` select text based on a definition.

```fin
#cdef COUNT 3
#cdef VERBOSE
#c_ifdef VERBOSE
    #cdef LABEL "verbose"
#c_else
    #cdef LABEL "quiet"
#c_endif

fun main() <noret> {
    blame COUNT == 3;
    printf("%s\n", LABEL);
}
```

```output
verbose
```

Function-like definitions such as `#cdef DOUBLE(x) x + x` also expand text.
Their bodies must be valid Fin, including Fin's conditional operand order. Prefer
an expression macro or function when precedence and repeated evaluation matter.
A trailing backslash continues a preprocessor line.

`#c_ifndef` is not implemented as an inverse test; use `#c_ifdef` with `#c_else`.

## Attributes and grouped declarations

An attribute precedes its declaration: `#[name]`, `#[name(args)]`, or a supported
value form such as `#[llvm_name="symbol"]`. An attribute on `%{ ... }%` applies to
the declarations in that block.

```fin check
#[export] %{
    pub type i32 = int;
    pub type i64 = int{64};
}%
```

| Attribute | Current meaning / boundary |
| --- | --- |
| `#[llvm_name="symbol"]` | Controls supported function/struct/foreign symbol names |
| `#[export]` | Visibility metadata; accepted by the backend on supported declarations |
| `#[global]` | Ambient foreign declarations inside `namespace std` |
| `#[class]` | Class marker on a struct |
| `#[uncastable]` | Backend rejects explicit casts to/from the marked type |
| `#[stderror]` | Standard-error marker |
| `#[slaveof(...)]` | Read by the backend: pins a local to `$Fin` or a global, delays a struct's destruction to its referent's exit, refuses dangling or region-crossing ties |
| `#[debug]`, `#[future]` | Parse support does not guarantee an executing implementation |
| `#[stdimport]` | Used for the library's import blocks |

Unknown attributes can parse and still fail later or have no consumer. Check the
specific declaration's backend behavior before relying on an attribute's effect.

## Special functions and compiler components

`@special name(...) <Type> { ... }` declares a compile-time function; `@name(...)`
is its call syntax. It is distinct from `name!(...)` macro expansion.

The current semantic layer registers component operations for `types`, `structs`,
`enums`, and `system`, and checks grants such as
`#[use(compiler.components.types)]`. That registration is not an interpreter for
all component operations. The larger events, protocols, providers, layout queries,
and injected-code design remains only partly implemented.

For compile-time work, read [compiler-api.md](../compiler-api.md), then verify the
operation in [CompilerApi.cpp](../../src/semantics/CompilerApi.cpp) and
[Analyzer_CompilerApi.cpp](../../src/semantics/impl/Analyzer_CompilerApi.cpp).
A declaration passing the frontend is insufficient evidence that its body executes.
Application code should not assume automatic GC, borrow checking, or event handlers.

Next: [standard library](12-standard-library-tour.md).
