# 2. Variables and types

## Declarations

Use `let name <Type> = value;`. `auto` asks the compiler to infer the type from
that value. Initialize a local before reading it.

```fin
fun main() <noret> {
    let count <int> = 3;
    let inferred <auto> = 4;
    let enabled <bool> = true;
    let text <string> = "Fin";
    let letter <char> = 'F';
    let ratio <float> = 1.5;
    count += inferred;
    blame count == 7 && enabled;
    printf("%s %d\n", text, count);
}
```

```output
Fin 7
```

`let value <int>;` permits later assignment. `let value <auto>;` has no initializer
from which to infer a type. For an empty array, write the element type:
`let items <[int]> = [];`.

## Builtin types

These need no import. Aliases such as `i32` belong to `types::std` instead.

| Type | Representation / use |
| --- | --- |
| `char` | 8-bit character/integer representation |
| `short`, `ushort` | Signed/unsigned 16-bit integers |
| `int`, `uint` | Signed/unsigned 32-bit integers |
| `long`, `ulong` | Signed/unsigned 64-bit integers |
| `float`, `double` | 32-bit / 64-bit floating point |
| `bool` | `true` or `false` |
| `string` | Pointer to NUL-terminated bytes |
| `noret`, `void` | No return value; equivalent spellings |
| `auto` | Inferred type, not a runtime box |
| `any`, `object` | Dynamic/erased type machinery with runtime limits; prefer concrete types |

A sized integer uses braces: `int{8}`, `uint{64}`, or `int{8 * 8}`. The expression
must produce a supported constant width. Pointers preserve their pointee type;
`&int{64}` and `&int{32}` are not interchangeable.

## Numeric conversion

Integers widen implicitly by width, including `int` to `long`. A narrower target
or an equal-width sign change needs `cast<T>`. Integer constants that fit a target
can initialize it directly. Negative constants cannot initialize unsigned types.
The integer widening rule does not imply that `float` converts to `double`.

```fin
fun main() <noret> {
    let small <int{8}> = 7;
    let wider <long> = small;
    let precise <double> = cast<double>(1.5);
    let ordinary <int> = cast<int>(wider);
    blame ordinary == 7;
    blame precise > cast<double>(1.0);
}
```

```fin error
fun main() <noret> {
    let negative <ulong> = -1;
}
```

Use explicit casts for mixed integer/floating-point calculations so the intended
precision is visible. [Expressions](03-operators-and-expressions.md) explains casts.

## `const` and `readonly`

`const` prevents rebinding. At module scope, use constant initializers; arbitrary
runtime initialization is not supported. A `const` pointer binding does not make
its pointee immutable.

```fin
const LIMIT <int> = 8;

fun change(const target: &int) <noret> {
    *target = LIMIT;
}

fun main() <noret> {
    let count <int> = 0;
    change(&count);
    blame count == 8;
}
```

A `readonly` field permits initialization and writes inside its declaring type,
while rejecting later writes from outside. This check is implemented.

```fin
struct Counter {
    pub readonly value <int>,
    pub fun bump(self: &Self) <noret> { self.value++; }
}

fun main() <noret> {
    let counter <Counter> = Counter{value: 4};
    counter.bump();
    blame counter.value == 5;
}
```

```fin error
struct Counter { pub readonly value <int> }
fun main() <noret> {
    let counter <Counter> = Counter{value: 4};
    counter.value = 5;
}
```

## Aliases and constraint sets

A type alias names one type. A constraint set uses `|` and describes allowed
choices for a generic bound; it is not a tagged runtime union.

```fin check
type IntArray = [int];
type ArrayType<T> = [T];
type Number = int | uint | float;
fun identity<T: Number>(value: T) <T> { return value; }
```

Use concrete types for storage. Generic bound enforcement still has gaps, so
passing a check is not proof that every declared bound was enforced.

## Nullable declarations

`?` on a declaration permits absence; postfix `?` on a value denullifies it.
A nullable function declaration starts with `fun?`. This is syntax/type-checking
support, not a promise that nullable structs or scalars build.

```fin check
struct Record { pub number? <int> }
fun? make_record(number?: int) <Record> {
    if (number == null) { return null; }
    return Record{number: number};
}
```

The backend has limited nullable support, including nullable function values.
For general application data, a concrete value plus an explicit presence flag
avoids relying on an unimplemented representation. See
[functions](05-functions.md) for omitted-argument limits.

Next: [operators and expressions](03-operators-and-expressions.md).