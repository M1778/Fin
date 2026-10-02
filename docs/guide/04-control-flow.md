# 4. Control flow

## Branches

Use parentheses around an `if` condition and braces around each body. A function
returning a value must return on every path.

```fin
fun sign(number: int) <int> {
    if (number < 0) {
        return -1;
    } else if (number == 0) {
        return 0;
    } else {
        return 1;
    }
}

fun main() <noret> {
    blame sign(-3) == -1;
    blame sign(0) == 0;
    blame sign(3) == 1;
}
```

## Counted and conditional loops

A `for` loop has an initializer, a condition, and an update. `while` tests before
its body; `do`/`while` tests afterward. `break` exits the innermost loop and
`continue` proceeds to its next iteration.

```fin
fun main() <noret> {
    let total <int> = 0;
    for (let i <int> = 0; i < 5; i++) {
        if (i == 2) { continue; }
        total += i;
    }
    blame total == 8;

    let count <int> = 0;
    while (true) {
        count++;
        if (count == 3) { break; }
    }
    do { count++; } while (false);
    blame count == 4;
}
```

`for (i: int = 0; i < 5; i++)` is also accepted; the `let` form uses the same
annotation syntax as a normal local declaration.

## `foreach`

The element binding uses angle brackets. Add an index binding before it when
needed. The element is a copy; assigning to it does not update the array.

```fin
fun main() <noret> {
    let numbers <[int, 3]> = [10, 20, 30];
    let sum <int> = 0;
    foreach (index <int>, value <int> in numbers) {
        blame value == numbers[index];
        sum += value;
    }
    blame sum == 60;
    foreach (value <int> in numbers) {
        printf("%d\n", value);
    }
}
```

```output
10
20
30
```

`foreach` supports arrays, including a prototype's `.0` keys or `.1` values.
It does not implement arbitrary iteration over a `Collection`, `HashMap`, string,
or user-defined struct. Use a container's explicit access methods and a `for` loop.
The element annotation must match the element type; it is not an implicit widening
assignment. Parentheses around the bindings are optional.

## Blocks and lifetime

A bare `{ ... }` in statement position opens a lexical scope. Its locals stop
being visible when the block ends. A brace-built value needs an introducing
expression, such as `Point{x: 1}` or `let p <Point> = ...`.

```fin
fun main() <noret> {
    let outer <int> = 4;
    {
        let inner <int> = outer + 1;
        blame inner == 5;
    }
    blame outer == 4;
}
```

Struct values with destructors clean up on normal scope exit and on `return`,
`break`, or `continue`. A `blame` abort does not unwind. Raw pointers and dynamic
array handles do not gain automatic ownership simply by leaving scope.

A `#[slaveof(...)]` tie moves a local's cleanup. A tie on destructor-less storage
changes nothing — there is no cleanup to move — while a struct-typed tie delays
destruction to its referent's scope exit:

```fin
fun main() <noret> {
    let z <&int>;
    {
        #[slaveof(z)]
        let m <&int> = new int(5);
        z = m;
    }
    blame *z == 5;
}
```

```fin
struct Track {
    v <int>,
    ~Track() { printf("dtor %d\n", self.v); }
}

fun main() <noret> {
    let z <int> = 0;
    {
        #[slaveof(z)]
        let m <Track> = Track{ v: 7 };
        printf("inner\n");
    }
    printf("outer\n");
}
```

```output
inner
outer
dtor 7
```

`#[slaveof($Fin)]` — or a tie to a global, which already outlives every frame —
pins a local past every scope exit; its destructor does not run:

```fin
struct Track {
    v <int>,
    ~Track() { printf("dtor %d\n", self.v); }
}

fun main() <noret> {
    #[slaveof($Fin)]
    let m <Track> = Track{ v: 3 };
    printf("body\n");
}
```

```output
body
```

A tie to a name that is not in scope is refused, as is a tie declared inside a
loop body or branch arm, which the tie cannot cross. A tie to a moved-from
binding is never resurrected. None of this is a borrow checker or a collector;
see [arrays and pointers](09-arrays-and-pointers.md) for explicit ownership.

```fin error
fun main() <noret> {
    #[slaveof(nope)]
    let m <int> = 1;
}
```

```fin build-error
struct Track {
    v <int>,
    ~Track() { printf("dtor %d\n", self.v); }
}

fun main() <noret> {
    let z <int> = 0;
    let i <int> = 0;
    while (i < 2) {
        #[slaveof(z)]
        let m <Track> = Track{ v: 9 };
        printf("iter\n");
        i = i + 1;
    }
    printf("done\n");
}
```

## Recursion and nested functions

```fin
fun factorial(number: int) <int> {
    if (number <= 1) { return 1; }
    return number * factorial(number - 1);
}

fun main() <noret> {
    fun twice(value: int) <int> { return value * 2; }
    blame twice(factorial(4)) == 48;
}
```

A nested function is visible from its declaration to the end of its containing
scope. It can recurse and can be used as a function value. It cannot capture the
enclosing function's locals; pass those values as arguments. The same restriction
applies to lambdas.

Next: [functions](05-functions.md).
