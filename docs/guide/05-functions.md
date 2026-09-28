# 5. Functions

## Declare and call

A named function uses `fun name(parameters) <ReturnType> { body }`. Parameters use
`name: Type`; local declarations use `name <Type>`. Call arguments bind by position.

```fin
fun add(left: int, right: int) <int> {
    return left + right;
}

fun main() <noret> {
    blame add(2, 3) == 5;
}
```

`noret` and `void` both mean no return value. `main` must be a top-level function
with a body when building an executable. Use `return value;` in value-returning
functions and return on every path.

A `const` parameter cannot be rebound. For `const pointer: &int`, that protects
the pointer binding while still permitting `*pointer = value`.

## Function values and lambdas

A function type is `fn(ArgumentTypes) -> ReturnType`; `=>` is also accepted as its
arrow. Lambda return annotations occupy the same position as named functions.

```fin
fun add(a: int, b: int) <int> { return a + b; }
fun apply(a: int, b: int, operation: fn(int, int) -> int) <int> {
    return operation(a, b);
}

fun main() <noret> {
    let named <fn(int, int) -> int> = add;
    let block <auto> = fun (x: int) <int> { return x * 2; };
    let arrow_block <auto> = (x: int) <int> => { return x + 5; };
    let expression <auto> = (x: int) <int> => x - 3;
    blame apply(4, 5, named) == 9;
    blame block(4) == 8;
    blame arrow_block(4) == 9;
    blame expression(4) == 1;
}
```

Lambdas are function values without captured environments. Pass state explicitly.
This attempted capture is deliberately rejected:

```fin build-error
fun main() <noret> {
    let offset <int> = 10;
    let add_offset <auto> = (x: int) <int> => x + offset;
    printf("%d\n", add_offset(2));
}
```

## Generics

Declare parameters after the function name. Supply explicit type arguments with
`::<...>` or let the compiler infer them from arguments.

```fin
fun identity<T>(value: T) <T> { return value; }
fun swap<T>(left: &T, right: &T) <noret> {
    let saved <T> = *left;
    *left = *right;
    *right = saved;
}

fun main() <noret> {
    let a <int> = identity::<int>(7);
    let b <int> = identity(9);
    swap(&a, &b);
    blame a == 9 && b == 7;
    let local_identity <auto> = fun <T>(value: T) <T> { return value; };
    blame local_identity::<int>(5) == 5;
}
```

A generic lambda is a template specialized when called, not a first-class function
pointer. It cannot be passed as an unspecialized `fn` value. A bound uses a colon,
for example `T: Printable`; see [interfaces and generics](07-interfaces-and-generics.md).
Erasure markers such as `Castable` require runtime support that is incomplete.

## Default and nullable parameters

A default expression is type-checked against its parameter. Trailing defaulted or
nullable parameters may be omitted by the frontend:

```fin build-error
fun greet(name: string, count: int = 2) <noret> {
    printf("%s %d\n", name, count);
}
fun main() <noret> {
    greet("Fin");
    greet("Fin", 3);
}
```

The backend does not yet fill in omitted arguments. Supply all values explicitly
when writing executable code. Defaults are positional: a required parameter after
a defaulted one still forces the earlier position to be supplied. A default is a
property of the declaration, not part of `fn(...) -> ...`.

`fun?` permits an absent return value, and `name?: Type` permits a nullable
parameter. General nullable runtime representation remains incomplete; see
[nullability](02-variables-and-types.md#nullable-declarations).

## Foreign declarations and ABI

`@define` declares a function provided by an external library. It has no body.
`#[llvm_name="symbol"]` gives it a different linker name.

```fin
@define strlen(text: string) <ulong>;
#[llvm_name="strcmp"]
@define compare_bytes(left: string, right: string) <int>;

fun main() <noret> {
    blame strlen("Fin") == 3;
    blame compare_bytes("same", "same") == 0;
}
```

This example uses the 64-bit Unix C ABI, where `size_t` matches `ulong`.
Match widths and signedness to the actual C declaration on your target. A function
called `sqrt` in C usually takes and returns `double`, so a Fin `float` declaration
would be wrong even if it type-checks. Foreign aggregate parameters also have
backend restrictions.

`...` marks a variadic declaration, as in the library's
`@define printf(fmt: string, ...) <noret>;`. There is no format-string checking.
Use `%d` for an `int`, `%s` for a `string`, and keep user text in a value argument:
`printf("%s", text)`. The ambient `printf` declaration intentionally ignores C's
integer return value.

A foreign declaration does not add linker flags or supply an implementation.
The driver links through `cc` (or `FIN_CC`); a library outside the default C runtime
needs a separately supported linking setup.

Next: [structs and classes](06-structs-and-classes.md).
