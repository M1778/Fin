# 3. Operators and expressions

## Arithmetic

`+ - * / %` work as expected on the numeric types:

```fin
@define printf(fmt: string, ...) <noret>;

fun main() <noret> {
    let a <int> = 7;
    let b <int> = 3;
    printf("%d %d %d\n", a + b, a / b, a % b);
}
```

There is no implicit conversion between numeric types, so mixed arithmetic needs an
explicit `cast`:

```fin
let f <float> = cast<float>(a) / 2.0;
```

## Comparison and logic

`== != < > <= >=` produce a `bool`. `&&` and `||` take `bool` operands on both sides —
an `int` in a logical operator is a type error, not an implicit truth test:

```fin
let cmp <bool> = a > b && b != 0;
let either <bool> = cmp || false;
let flipped <bool> = !cmp;
```

## Shifts

`<<` and `>>` shift an integer:

```fin
let shifted <int> = a << 2;
let back <int> = a >> 1;
```

`>>` reaches the parser as two `>` tokens and is reassembled, so it keeps shift
precedence rather than comparison precedence.

The bitwise operators `& | ^` are **not available** as binary operators today: `a & b`
is a syntax error (`&` is the address-of operator and `|` belongs to the constraint-set
syntax from chapter 2). The `operators::std` module declares `BitAnd`, `BitOr` and their
assigning forms as interfaces, so the design intends them; the expression grammar has not
grown them yet.

## Unary operators

```fin
let neg <int> = -a;          // negation
let notted <bool> = !cmp;    // logical not
let p <&int> = &a;           // address of
let v <int> = *p;            // dereference
++a;                         // prefix increment
--a;                         // prefix decrement
a++;                         // postfix increment
a--;                         // postfix decrement
```

## Assignment

Plain assignment and the compound forms:

```fin
a = 5;
a += 1;
a -= 1;
a *= 2;
a /= 2;
```

## The conditional expression

Fin's conditional is written **`cond : then ? otherwise`**. The condition comes first,
then `:`, then the value when the condition holds, then `?`, then the value when it does
not:

```fin
@define printf(fmt: string, ...) <noret>;

fun main() <noret> {
    let big <int> = 5 > 3 : 100 ? 200;
    printf("%d\n", big);        // prints 100
}
```

This is not C's `cond ? then : otherwise`, and C's order is a syntax error rather than an
alternative spelling — `a > 2 ? 1 : 2` reports `syntax error, unexpected INTEGER`.

The reason is parseability, and it is worth understanding rather than memorising. `?` is
already Fin's postfix denullifier. In C order, the parser cannot distinguish `x ? -1 : 2`
from `x?` followed by a subtraction without unbounded lookahead, and the same collision
appears with a leading `*`, `&` or `(`. Leading with `:` has no such ambiguity. The full
argument is in `docs/adr/0005-ternary-is-written-condition-then-otherwise.md`.

That ADR also names the hazard plainly: a reader arriving from C will transpose the arms,
transposed arms compile silently, and the expression returns the wrong value. No diagnostic
can catch it. Two lines in the corpus use this form — `tests/samples/stdlib/error.fin:28`
writes `return result == -1 : false ? true;` — and reading them as "condition, then
result-if-true, then result-if-false" is the habit to build.

## Casting

`cast<T>(expr)` converts between types. It is the only conversion; nothing is implicit:

```fin
let f <float> = cast<float>(x);
let n <int> = cast<int>(some_enum_value);
```

A cast that cannot succeed at runtime panics rather than returning a sentinel.

## `sizeof`

`sizeof(T)` yields the size of a type as an `int`:

```fin
let size <int> = sizeof(Vector2);
```

## Denullify

Postfix `?` on an expression unwraps a nullable value, panicking if it is null:

```fin
let sure <A> = make_a(-1)?;
blame make_a(10)?.get_b()? == 10;
```

It chains: the example above denullifies the call, calls a method on the result, and
denullifies that too.

## `blame`

`blame` is Fin's assertion statement. With a condition it asserts, optionally with a
message:

```fin
@define printf(fmt: string, ...) <noret>;

fun main() <noret> {
    let x <int> = 10;
    blame x > 0, "x must be positive";
    blame x == 10;
}
```

A failing assertion prints a location and message to stderr and aborts. `blame` also has a
*raise* form — `blame some_value;` — which is what an error path in the standard library
drafts is written with. It requires a value whose type is erased (`any`); a plain `int` there
reports `expected 'bool', got 'int'`, because the assert form is what the compiler assumes.
The raise form does not build, and the refusal lands on its operand rather than on `blame`:
a value of erased type has no backend representation, so `let e <Err> = ...` reports
`codegen: a variable of type 'Err' is not lowered yet` before the `blame` is reached.

## `try` / `catch`

A `try` block with a `catch` handler parses and both blocks are type-checked:

```fin
import { Error } from error::std;
@define printf(fmt: string, ...) <noret>;

fun main() <noret> {
    try {
        printf("in try\n");
    } catch (Error as err) {
        printf("never runs\n");
    }
}
```

The limitation is important: nothing in Fin currently raises anything a `catch` can
receive, so a `try` lowers as its block and the `catch` body emits no code at all. The
catch body is still *analysed* — an undefined name inside it is still a diagnostic — but it
never runs. See `docs/adr/0026-a-class-is-a-struct-with-a-base-and-try-is-a-scope.md` §2.

Next: [control flow](04-control-flow.md).
