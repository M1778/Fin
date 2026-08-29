# 2. Variables and types

## Declarations

A local variable is introduced with `let`, and its type goes in angle brackets after the
name:

```fin
fun main() <noret> {
    let a <int> = 100;
    let b <float> = 2.5;
    let c <bool> = true;
    let s <string> = "hello";
    let ch <char> = 'x';
}
```

The annotation is not optional in general, but it can be deferred. A declaration without an
initialiser is legal, and the variable is assigned later:

```fin
let uninit <int>;
uninit = 5;
```

## Inference with `auto`

`auto` asks the compiler to determine the type from the initialiser, at compile time:

```fin
let kilo <auto> = 19;              // int
let squarer <auto> = (x: int) <int> => x * x;   // fn(int) -> int
```

If the compiler cannot determine a type, that is a diagnostic rather than a fallback. An
empty array literal is the common case: `let a <auto> = [];` reports that it cannot infer
the element type, while `let a <[int]> = [];` is fine because the annotation says what the
literal could not.

## Builtin types

The compiler registers these names in the global scope, so they need no import:

| Name | Meaning |
| --- | --- |
| `int` | signed integer |
| `uint` | unsigned integer |
| `short`, `ushort` | narrower signed/unsigned integer |
| `long`, `ulong` | wider signed/unsigned integer |
| `float`, `double` | floating point |
| `bool` | `true` / `false` |
| `char` | a single character |
| `string` | text |
| `void`, `noret` | no value; both spell the same return type |
| `auto` | infer from the initialiser |
| `any` | compile-time erasure — checked at runtime, not here |
| `object` | a runtime box that can hold any value |

`any` and `object` are distinguished deliberately: `any` is erasure, `object` is a box that
pays memory and speed to hold anything.

There is one sharp edge worth knowing early. Integer and float literals get a default type,
and there is no implicit narrowing or widening between the numeric types, so a `double`
needs its literal converted:

```fin
let d <double> = cast<double>(1.5);   // `= 1.5` reports: expected 'double', got 'float'
```

## Sized types

A builtin numeric type can carry a width in braces. The width is an expression evaluated at
compile time, not just a literal:

```fin
let x <int{64}> = 10;
let z <int{8 * 8}> = 42;       // the annotation's expression is evaluated
let p <*int{32}> = &x;         // pointer to a 32-bit int
```

## Constants and module-scope variables

`const` declares a binding that cannot be reassigned. `let` at module scope declares a
mutable global. Both require a type the compiler can determine at compile time:

```fin
const PI <float> = 3.14;
const MAX_FILE_SIZE <auto> = 1000;

let Counter <int> = 0;
```

A `const` parameter is the same idea applied to an argument — the callee may read and copy
it but not reassign it:

```fin
fun test(const a: int) <noret> {
    let scope_a <int> = a;   // copying is fine
    scope_a = 5;             // and the copy is writable
    // a = 10;               // rejected: `a` is const
}
```

`const` restricts the *binding*, not what it points through. A `const a: &int` cannot be
re-pointed, but `*a = 20;` writes the pointee.

## `readonly`

`readonly` is the struct-member counterpart: the field is visible outside its declaring
type but writable only from inside it.

```fin
struct Counter {
    pub readonly count <int>,

    pub fun bump(new_val: int) <noret> {
        self.count = new_val;    // fine: inside the struct
    }
}

fun main() <noret> {
    let c <Counter> = Counter { count: 1 };   // first initialisation is allowed
    let copy <int> = c.count;                 // reading and copying is allowed
    c.bump(10);                               // the intended way to change it
}
```

A `readonly` field can be copied out and the copy modified freely; what is protected is the
field itself.

One limitation: `readonly` parses and is recorded, but the compiler does not yet reject a
write to a `readonly` field from outside its declaring type — `c.count = 5;` compiles today.
Treat it as documentation of intent until that check lands.

## Type aliases

`type` gives a name to an existing type:

```fin
type IntArray = [int];
type PtrIntArray = &[int];
type ArrayType<T> = [T];      // aliases take generic parameters
```

`type` also declares a *constraint set*, using `|`:

```fin
type Number = int | uint | float | short | long | ushort | ulong | double;
```

A constraint set is a bound and never a storage type. `fun sort<T: Number>(...)` is what it
is for. `let x <Number>;` is specified to be a diagnostic at the point of use, and that
check is not yet implemented — such a declaration compiles today — so treat the bound
position as the only supported one. The reasoning is in
`docs/adr/0018-a-constraint-set-is-a-bound-never-a-storage-type.md`: storage would need a
tag, and Fin already has a tagged sum in `enum`.

## Nullability

A `?` after the name makes a declaration nullable. Its default value is null, and `null` is
a permitted initialiser:

```fin
struct A {
    pub b? <int>,           // same as `b <int> = null,`
}
```

A postfix `?` on an expression *denullifies* it — it yields the value or panics:

```fin
let sure <A> = make_a(1)?;
```

A function whose return type may be null is declared `fun?`, and a nullable parameter is
optional at the call site:

```fin
fun? make_a(n?: int) <A> {
    if (n == null) {
        return null;
    }
    return A{};
}
```

Note that `?` means two unrelated things in Fin: this denullify, and the `otherwise` arm of
the conditional expression covered in the next chapter.

## Scopes

A variable lives until the end of the block that declares it, and a bare `{` in statement
position opens a block:

```fin
fun main() <noret> {
    let a <int> = 1;
    {
        let b <int> = a + 1;   // `a` is visible here
    }
    // `b` is not visible here
}
```

That rule is positional and it matters: `{` at the start of a statement is *always* a
block, never a brace-initialised value. A value written with braces only ever appears after
something that introduces it — `Point{x: 1}`, `= {...}` — so the two never compete. See
`docs/adr/0011-a-bare-brace-opens-a-scope.md`.

Next: [operators and expressions](03-operators-and-expressions.md).
