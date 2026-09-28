# 3. Operators and expressions

## Arithmetic, comparisons, and logic

`+ - * / %` provide numeric arithmetic. Integer division produces an integer.
`== != < > <= >=` compare values; `&&` and `||` require boolean operands.
Use explicit comparisons such as `count != 0` when combining conditions.

```fin
fun main() <noret> {
    let a <int> = 7;
    let b <int> = 3;
    blame a / b == 2;
    blame a % b == 1;
    blame a > b && b != 0;
    let scaled <float> = cast<float>(a) / 2.0;
    blame scaled == 3.5;
    a += 1;
    a--;
    blame a == 7;
}
```

Assignment forms include `=`, `+=`, `-=`, `*=`, and `/=`. Prefix and postfix
`++`/`--` work on integers. `<<` and `>>` shift integers. Binary bitwise
`&`, `|`, and `^` are absent from the expression grammar; unary `&` still takes
an address, and `|` separates types in a constraint set.

## Conditional expressions

The order is **`condition : value_if_true ? value_if_false`**:

```fin
fun main() <noret> {
    let high <int> = 5 > 3 : 100 ? 200;
    let low <int> = 5 < 3 : 100 ? 200;
    blame high == 100;
    blame low == 200;
    printf("%d %d\n", high, low);
}
```

```output
100 200
```

`?` already serves as a postfix denullifier, which is why the conditional starts
its true arm with `:`. [ADR 0005](../adr/0005-ternary-is-written-condition-then-otherwise.md)
records the grammar decision. Prefer `if` for complicated branches.

## Casts and size

`cast<T>(value)` requests conversion. It is not a promise that every pair of types
can convert, nor a general checked runtime cast. Numeric casts may narrow values.
Conversions involving erased values or unsupported aggregates can fail in codegen.

```fin
fun main() <noret> {
    let wide <long> = 42;
    let small <int> = cast<int>(wide);
    blame small == 42;
    blame sizeof(int) == 4;
}
```

`sizeof(T)` measures the target layout. Avoid hardcoding pointer sizes in portable
code. Use integer widening where it applies; an explicit cast is not necessary
for every assignment between integer widths.

## String equality

General `string` equality compares pointers. Equal bytes in separately allocated
strings need not compare equal. A C declaration gives a working byte comparison:

```fin
@define strcmp(a: string, b: string) <int>;

fun main() <noret> {
    blame strcmp("Fin", "Fin") == 0;
    blame strcmp("Fin", "Other") != 0;
}
```

Prototype string keys use their own byte-comparison lookup. That does not change
what `a == b` means for two ordinary strings.

## `blame` and recoverable errors

`blame condition;` asserts a boolean. A message is optional:

```fin
fun divide(numerator: int, denominator: int) <int> {
    blame denominator != 0, "denominator must be nonzero";
    return numerator / denominator;
}

fun main() <noret> {
    blame divide(12, 3) == 4;
}
```

Failure prints a location/message to stderr and aborts. It does not unwind scopes
or run cleanup. The error-value form of `blame` is incomplete.

`try { ... } catch (Error as err) { ... }` parses, but current code generation runs
only the `try` body. A `catch` cannot recover from a failed assertion. Represent
recoverable outcomes explicitly, for example with a `bool` result and an output
pointer, or a plain enum and a struct. Payload-based `Result` enums do not yet
provide a working exception mechanism.

Postfix `value?` means denullify; it is unrelated to recoverable error propagation.
See [nullable declarations](02-variables-and-types.md#nullable-declarations).

Next: [control flow](04-control-flow.md).