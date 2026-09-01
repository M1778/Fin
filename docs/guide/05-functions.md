# 5. Functions

## Declaration

`fun`, a name, a parenthesised parameter list, then the return type in angle brackets:

```fin
fun add(x: int, y: int) <int> {
    return x + y;
}
```

The return type's position is the thing to notice: it comes *after* the parameters, in
angle brackets, the same bracket that annotates a variable. `<noret>` (equivalently
`<void>`) means the function returns nothing:

```fin
@define printf(fmt: string, ...) <noret>;

fun greet(name: string) <noret> {
    printf("hello %s\n", name);
}
```

Parameters use `name: type` — a colon, with no angle brackets. This is the one place in Fin
where a type is written without them, and it is worth memorising early because it differs
from every `let` you write:

```fin
let a <int> = 1;            // declaration: angle brackets
fun f(a: int) <int> { ... } // parameter: colon
```

A parameter type *may* be written bracketed (`fun f(a: <int>)`) and parses to the same
type, but the corpus writes nineteen parameters unbracketed and none bracketed, so the
colon form is the idiom.

`main` is the entry point, and building an executable with `-o` requires a top-level
`fun main` with a body.

## `const` parameters

A `const` parameter cannot be reassigned inside the body:

```fin
fun test(const a: int) <noret> {
    let copy <int> = a;    // read and copy freely
    copy = 5;              // the copy is writable
    // a = 10;             // rejected: Cannot assign to immutable variable 'a'
}
```

For a pointer parameter, `const` protects the pointer and not the pointee:

```fin
fun test_2(const a: &int) <noret> {
    // a = new int(1);     // rejected
    *a = 20;               // allowed: writes through
}
```

## Function types

A function's type is written `fn(params) -> ret`. Both `->` and `=>` are accepted as the
arrow:

```fin
fun compute(a: int, b: int, operation: fn(int, int) => int) <int> {
    return operation(a, b);
}

fun main() <noret> {
    let my_op <fn(int, int) => int> = add;
    let res <int> = my_op(5, 5);
}
```

A named function can be passed wherever a matching function type is expected:

```fin
let res1 <int> = compute(10, 20, add);
```

## Lambdas

There are three spellings, and they differ only in punctuation.

**`fun` with a block body** — an anonymous function:

```fin
let f1 <fn(int) -> int> = fun (x: int) <int> {
    return x * 2;
};
```

**Arrow with a block body**:

```fin
let f2 <fn(int) -> int> = (x: int) <int> => { return x + 5; };
```

**Arrow with an expression body** — no `return`, no braces:

```fin
let f3 <fn(int) -> int> = (x: int) <int> => x - 3;
```

In every form the return type is annotated in angle brackets, in the same position as a
named function's. `auto` infers the whole function type:

```fin
let inferred <auto> = (x: int) <int> => x * x;
let logger <auto> = (msg: string) <void> => printf("Log: %s\n", msg);
```

A lambda can be written inline at a call site:

```fin
let res <int> = compute(100, 50, fun (a: int, b: int) <int> {
    return a - b;
});

compute(20, 10, (a: int, b: int) <int> => a + b);
```

Here is the whole set, verified together:

```fin
@define printf(fmt: string, ...) <noret>;

fun add(x: int, y: int) <int> {
    return x + y;
}

fun compute(a: int, b: int, operation: fn(int, int) => int) <int> {
    return operation(a, b);
}

fun main() <noret> {
    let f1 <fn(int) -> int> = fun (x: int) <int> { return x * 2; };
    let f2 <fn(int) -> int> = (x: int) <int> => { return x + 5; };
    let f3 <fn(int) -> int> = (x: int) <int> => x - 3;
    let f4 <auto> = (x: int) <int> => x * x;

    printf("%d %d %d %d\n", f1(10), f2(10), f3(10), f4(5));
    printf("%d\n", compute(10, 20, add));
    printf("%d\n", compute(100, 50, fun (a: int, b: int) <int> { return a - b; }));
}
```

A function can also return a function type:

```fin
fun get_adder() <fn(int, int) -> int> {
    return (a: int, b: int) <int> => a + b;
}
```

This type-checks. Capturing the enclosing environment is not implemented, so treat a
returned lambda as a function pointer rather than a closure.

## Generic functions

Type parameters go in angle brackets after the name, and a bound after a colon:

```fin
fun identity<T>(a: T) <T> {
    return a;
}

fun using_erasure<T: Castable, U: Castable>(a: T, b: U) <int> {
    return cast<int>(a) + cast<int>(b);
}
```

An unbounded parameter is monomorphised — one instantiation per concrete type. A parameter
bound by an erasure marker such as `Castable` is erased instead; erasure type-checks but is
not yet lowered, so **calling** one will not build with `-o`. Declaring it will: a template
is not code until a use says what its parameters are, so `using_erasure` above is emitted as
nothing until something calls it. Generic arguments can be supplied explicitly with `::<...>`
at the call site, or inferred from the arguments:

```fin
printf("%d\n", identity::<int>(7));
```

A lambda can be generic too:

```fin
import { Addable } from operators::std;

let g <auto> = fun <G: Addable>(a: G, b: G) <G> { return a + b; };
```

## Variadic and foreign declarations

`@define` declares a function implemented outside Fin. It has a signature and no body, and
`...` makes it variadic:

```fin
@define printf(fmt: string, ...) <noret>;
@define sqrt(f: float) <float>;
```

A `@define` accepts anything through `...` — there is no format checking.

## Missing returns

The compiler requires a return on every path of a value-returning function:

```
error: Function 'add' is missing a return statement on some paths
```

`fun?` relaxes this to "returns the type or null", and falling off the end returns null:

```fin
fun? make_a(n?: int) <A> {
    if (n == null) {
        return null;
    }
    if (n > 0) {
        return A{};
    }
    // returns null implicitly
}
```

## Default parameter values

A parameter may carry a default value, and it is checked against the parameter's declared
type exactly as an initialiser is:

```fin
fun greet(name: string, times: int = 2) <noret> {
    printf("%s %d\n", name, times);
}
```

Writing a default of the wrong type is a diagnostic on the default itself:

```
error: Type mismatch: expected 'string', got 'int'
   --> f.fin:1:19
   |
 1 | fun f(n: string = 3) <noret> { }
   |                   ^ here
```

It is an *initialiser* check, which is what settles the three edge cases. `= null` is
accepted whatever the declared type is (chapter 2's rule for a declaration, and
`lib/std/error.fin`'s draft writes `err_code: int = null`). A narrower constant widens, so
`n: ulong = 5` is fine. And a negative constant is still not an unsigned value, so
`n: ulong = -1` is refused for the same reason `let x <ulong> = -1;` is.

A default may also name something already in scope, including an earlier parameter in the
same list:

```fin
fun span(lo: int, hi: int = lo) <int> { return hi - lo; }
```

(`from` is a keyword — it is `import`'s — so a parameter cannot be called that.)

A default makes the parameter optional at the call site, so `span(1)` is a call and
`hi` is 2:

```fin
fun greet(name: string, times: int = 2) <noret> { }

fun main() <noret> {
    greet("hi");
    greet("hi", 3);
}
```

Two limits on that, both about where the optionality comes from rather than about
defaults. It is *positional*: arguments bind by position, so only a trailing run of
optional parameters can be omitted. `fun f(a: int = 1, b: int)` still requires both, and
`f(2)` reports `Function 'f' expects 2 arguments, got 1` — there is no way to write the
second without the first. And a default does not yet supply the *value*: the argument
stops being required, and a call to an ordinary imported function is not lowered at all
yet (chapter 10), so nothing in the language observes what the omitted argument would have
been. Within a single file the same holds — the arity check is what a default reaches, and
codegen for the missing argument is a separate unit.

Defaults and nullable parameters (chapter 2) are the same mechanism from the arity check's
point of view, and a signature may mix them:

```fin
fun g(a: int, b?: int, c: int = 3) <int> { return a; }
```

`g(1)`, `g(1, null)` and `g(1, null, 4)` are all calls. The minimum is the last parameter
that is neither nullable nor defaulted, and a parameter that is both counts once — so this
signature reports `expects between 1 and 3 arguments` when given none.

A default is not part of the function's type. `fn(int) -> int` describes both of these,
and either may be assigned to a variable of that type:

```fin
fun a(x: int) <int> { return x; }
fun b(x: int = 1) <int> { return x; }
```

That follows from what a default is: a fact about the declaration, observable only by
omitting an argument. A `fn` annotation has nowhere to write one, so making the two types
disagree would split them over a difference no call site can see.

Next: [structs and classes](06-structs-and-classes.md).
