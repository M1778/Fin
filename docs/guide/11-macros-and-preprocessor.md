# 11. Macros, attributes and the preprocessor

Fin has three separate mechanisms that all look like "code that runs at compile time", and
they are worth keeping apart: `@macro` operates on the AST, `#cdef` operates on text, and
attributes annotate declarations.

## `@macro`

A macro declares an ordinary parameter list and a body that returns exactly one `quote`d
expression. Parameters are referenced with `$name`:

```fin
@define printf(fmt: string, ...) <noret>;

@macro magic_add(a, b) {
    return quote { $a + $b; };
}

fun main() <noret> {
    let v <int> = magic_add!(10, 20);
    printf("%d\n", v);
}
```

A macro is invoked with `!` after its name. Expansion is real substitution into a typed AST,
not text pasting — `@macro twice(a) { return quote { $a + $a; }; }` used as
`let v <string> = twice!(3);` reports `Type mismatch: expected 'string', got 'int'`, with the
error pointing into the macro body.

There are no rule arms, no fragment specifiers (`$x:expr`) and no repetition (`$(...)*`).
`tests/samples/macro_definitions.fin` sketches a Rust-like form with all three, and all of it
sits inside a `/* [WIP] */` block; `docs/adr/0023-a-macro-returns-one-quoted-expression-and-the-bracket-shapes-the-argument.md`
records why that sketch was declined. Use the parameter form above. The `@` is required —
a bare `macro name { ... }` is not the current syntax.

A vararg parameter accepts zero or more arguments:

```fin
@macro many(a...) {
    return quote { $a; };
}

fun main() <noret> {
    let x <[int]> = many!(1, 2, 3);
    let y <[int]> = many!();
}
```

### The bracket shapes the argument

Three brackets can delimit a macro call, and the choice is meaningful rather than cosmetic:

- `name!(a, b)` — positional arguments, one per expression.
- `name![a, b]` — one prototype argument, keyed by position: `0`, `1`, ...
- `name!{k => v}` — one prototype argument, keyed by the written keys.

The corpus names the three uses in its own comments: "funccall like macro", "List like
macro", "dict like macro".

All three are implemented. A one-parameter macro takes `m![1, 2]` and `m!{"a" => 1}` because
each is *one* argument — a prototype literal — so the arity check sees one either way:

```fin
@macro m(p) {
    return quote { $p; };
}

fun main() <noret> {
    let list <{int, string}> = m!["a", "b"];    // keys 0 and 1
    let dict <{string, int}> = m!{"a" => 1};    // key "a"
}
```

Both `=>` and `:` separate a pair, and a trailing comma is accepted in either bracketed form
— which a prototype literal written directly does not accept, so `m![1, 2,]` compiles where
`{0: 1, 1: 2,}` is a syntax error.

An empty shaped call, `m![]` or `m!{}`, is the only way to write an empty prototype literal:
it takes its key and value types from the annotation, and reports
`Empty prototype literal cannot infer its key and value types.` when there is nothing to take
them from.

One current limit: a macro declaration is not lowered to machine code, so a file declaring one
type-checks and does not build with `-o`.

### Macros across a module boundary

A module exports the macros it declares, and both spellings reach them:

```fin
import { shout } from "mm.fin";     // the macro by name
import "mm.fin";                    // the module, as `mm`
```

so `shout!(1)` works after the first and `mm.shout!(1)` after the second. `import * from mm;`
does **not** carry macros — a call then reports `Undefined macro` — and a bare `shout!(1)`
after a whole-module import reports the same, because that form binds the module's name rather
than its contents.

A macro carries no visibility marker: `pub @macro` is a syntax error. Declaring a macro in a
module is what exports it, and there is no narrower rule to write.

### Hygiene: a body may only spell qualified names

A body may spell `$param` unquotes, literals, operators and module-qualified paths. A bare
unqualified identifier is refused, and refused at the declaration rather than at a call:

```fin
@macro f(a) { return quote { tmp + $a; }; }
```

```
error: macro 'f' names 'tmp' with no qualifier
```

The reason is capture. A bare `tmp` in a body is a name looked up wherever the macro lands, so
it collides with a caller's `tmp` and binds whatever that caller happened to have — the C
preprocessor's failure, which `@macro` exists to not repeat. The rule costs very little
because a body is one expression and so cannot declare a binding of its own; there is no
`let tmp` for the ban to get in the way of.

Refused at the declaration means the author of a library learns about it from their own build,
which is the only place the body can be fixed. A macro nobody calls is still refused.

Qualified paths are what a body reaches other code through:

```fin
@macro mk(n) { return quote { Held::make($n); }; }
@macro lim()  { return quote { Held::LIMIT; }; }
```

A free call is refused too — `from_prototype($items)` names something in no namespace — so the
two collection macros ADR 0023 sketches are spelled `Collection::from_prototype($items)` and
`HashMap::from_prototype($pairs)`.

Those names resolve **in the module that declared the macro**, not at the call site. So the
`Held` above is the `Held` that *`mac.fin`* imported, and a caller writing `mk!(3)` need not
import it, or know that the macro names it at all:

```fin
// libs/mac.fin
import { Held } from holder;
@macro mk(n) { return quote { Held::make($n); }; }

// app.fin -- imports the macro and nothing else
import { mk } from mac;
fun main() <noret> { let x <int> = mk!(3); }
```

The call site is consulted first, so a caller that has its own `Held` keeps it: the macro asked
for a type by that name, and shadowing a name stays the caller's prerogative.

`format!` is *not* a `@macro`. It is compiler-implemented, because one call site in the
standard library passes a runtime `string` as the format — which a macro taking a literal
cannot serve. It is not registered yet, so `format!(...)` reports `Undefined macro`.

## The C-style preprocessor

`#cdef` and friends operate on source text before parsing, exactly like C's preprocessor.

Object-like definitions substitute text:

```fin
@define printf(fmt: string, ...) <noret>;

#cdef GREETING "hi"
#cdef COUNT 3

fun main() <noret> {
    printf("%s %d\n", GREETING, COUNT);
}
```

Function-like definitions take parameters:

```fin
@define printf(fmt: string, ...) <noret>;

#cdef MAX(a, b) a > b : a ? b

fun main() <noret> {
    let x <int> = 5;
    let y <int> = 9;
    let m <int> = MAX(x, y);
    printf("%d\n", m);       // 9
}
```

Note what is *not* in that body: the C habit of parenthesising every operand. A `#cdef` body
is spliced in and parsed as Fin, and Fin's conditional does not accept a parenthesised
`otherwise` arm — `((a) > (b) : (a) ? (b))` is a syntax error. `tests/samples/preprocessor.fin`
is annotated `unimplemented` for a related reason: it writes the conditional in C's operand
order inside a `#cdef`, and C order does not exist in Fin (chapter 3). Write `#cdef` bodies as
Fin, not as C.

Conditional compilation:

```fin
@define printf(fmt: string, ...) <noret>;

#cdef DEBUG
#c_ifdef DEBUG
    #cdef LOG_MSG "Debug Mode"
#c_else
    #cdef LOG_MSG "Release Mode"
#c_endif

fun main() <noret> {
    printf("%s\n", LOG_MSG);   // Debug Mode
}
```

`#c_ifdef`, `#c_else` and `#c_endif` work. `#c_ifndef` is **not** implemented — it is
stripped from the source and pushes nothing, so its body is always compiled, whether or not
the name is defined. Write `#c_ifdef` / `#c_else` and put the body in the `#c_else` branch
instead.

A line ending in `\` continues onto the next.

## Attributes

An attribute is `#[name]` or `#[name(args)]` written before a declaration:

```fin
#[llvm_name="general_point"]
struct Point<T> {
   x <T>,
   y <T> = 0
}
```

Attributes appearing in the corpus and the standard library:

| Attribute | Meaning |
| --- | --- |
| `#[llvm_name="s"]` | the symbol name the backend emits |
| `#[global]` | ambient, no import needed — `namespace std` only (chapter 10) |
| `#[export]` | part of the module's public surface |
| `#[class]` | make a `struct` a class |
| `#[slaveof(x)]` | tie this declaration's lifetime to `x`, or to `$Fin` for program lifetime |
| `#[debug]` | mark for compile-time tracking |
| `#[future]` | a forward declaration |
| `#[stdimport]` | marks the standard library's own import block |
| `#[type(enum)]` | classify a declaration for the type system |

Only some of these have a reader. `#[global]` is enforced — using it outside `namespace std`
is an error. `#[llvm_name]` reaches the backend. The rest parse and are recorded and do not
yet change anything, and an *unknown* attribute is accepted silently rather than rejected, so
a typo in an attribute name is not caught for you.

Two attributes are refused by the backend rather than ignored: `#[export]` on a `@define`
reports `codegen: the attribute 'export' on a '@define' is not lowered yet`, and `#[debug]` on
a function reports `codegen: the attribute 'debug' on a function is not lowered yet`. Keep
them out of anything you build with `-o`. `#[global]` is read on a `@define` — that is how the
ambient `printf` links — and still refused on a `fun`.

## The `%{ ... }%` block

An attribute can apply to a group of declarations rather than one, by wrapping them in
`%{ ... }%`:

```fin
#[export] %{
pub type i32 = int;
pub type i64 = int{64};
pub type u32 = uint;
}%
```

`lib/std/types.fin` uses it for its alias set and `#[stdimport] %{ ... }%` for its imports.
The block is the attribute's scope: every declaration inside carries it.

## Special functions and compiler operations

`@name(args)` calls a *special function* — an operation the compiler provides rather than a
function Fin declares:

```fin
@getenumkeyid(value)
@implements(struct_, iface)
@Alloc(size)
```

`@special` declares one. These belong to a compile-time API layer that is specified in
`docs/compiler-api.md` and not yet built: none of the operations above resolves today, so a
call to one is a diagnostic. `@defined("name")`, which would let a file ask whether a symbol
exists before declaring it, is in the same state.

`@implements` has two unrelated uses, which is worth flagging: `@implements Type::name =
<lambda>;` is the member-attachment declaration from chapters 7 and 8 and it works, while
`@implements(t, i)` as an expression is the unbuilt compile-time predicate.

## Injected code, when it arrives

One rule is already settled for the compile-time layer, and it is worth knowing because it
shapes what such a library can do. Code injected by a compile-time handler mints its
identifiers with `compiler.code.fresh()`, and it can name existing declarations only through
module-qualified paths — never a local. That rules out both accidental collision with a
user's `tmp` and accidental capture of a user's `count`, which is the failure mode C's
preprocessor is remembered for. See
`docs/adr/0020-injected-identifiers-are-fresh-and-only-qualified-paths-are-spelled.md`.

Next: [a tour of the standard library](12-standard-library-tour.md).
