# 4. Control flow

## `if` / `else`

The condition is parenthesised and the body is braced. `else if` chains:

```fin
fun classify(n: int) <string> {
    if (n < 0) {
        return "negative";
    } else if (n == 0) {
        return "zero";
    } else {
        return "positive";
    }
}
```

The body is braced and the condition is parenthesised. Prefer an explicit comparison —
`if (count != 0)` rather than `if (count)`; the compiler accepts a non-`bool` condition
today, but the logical operators `&&` and `||` do not, so an integer condition stops working
the moment it is combined.

Missing returns are caught. A function declared to return a value must return on every
path:

```fin
fun add() <int> {
    if (0) {
        return 1;
    }
}
```

```
error: Function 'add' is missing a return statement on some paths
```

## `for`

The three-clause form. The loop variable is declared in the first clause, and there are two
spellings — a bare `name: type = init`, and a full `let` declaration:

```fin
@define printf(fmt: string, ...) <noret>;

fun main() <noret> {
    for (i: int = 0; i < 3; i++) {
        printf("i = %d\n", i);
    }

    for (let j <int> = 0; j < 3; j++) {
        printf("j = %d\n", j);
    }
}
```

Both appear in the corpus; the standard library uses the `let` form.

## `foreach`

`foreach` walks an array. The element form takes one binding, and the indexed form takes
two:

```fin
@define printf(fmt: string, ...) <noret>;

fun main() <noret> {
    let a <[int, 3]> = [1, 2, 3];

    foreach (element <int> in a) {
        printf("%d\n", element);
    }

    foreach (idx <int>, element <int> in a) {
        printf("%d: %d\n", idx, element);
    }
}
```

The parentheses are optional in both forms — `foreach element <int> in a { … }` is the same
loop.

Note the annotation syntax on the bindings — `element <int>`, in angle brackets, the same
as a `let`. The binding is a copy of the element, and the index counts from 0 in step with it,
so `element == a[idx]` holds at every step.

`foreach` walks an array and nothing else: a struct, a prototype, a string or a number is
refused rather than walked, because there is no iteration protocol in Fin. A `prototype`'s
halves *are* arrays, so `foreach (k <int> in p.0)` walks its keys. The binding's written type
has to be the element's own type — nothing widens `int` to `long` here, and a mismatch is
refused rather than converted.

## `while` and `do`/`while`

```fin
@define printf(fmt: string, ...) <noret>;

fun main() <noret> {
    let n <int> = 0;
    while (n < 3) {
        n++;
        if (n == 2) { continue; }
    }

    while (true) {
        printf("at least once\n");
        break;
    }

    do {
        printf("also once\n");
    } while (false);
}
```

`break` leaves the innermost loop, `continue` starts its next iteration.

## Blocks and scope

A bare `{` in statement position opens a scope. This is not a special construct — it is the
same block a loop or an `if` uses, standing on its own:

```fin
fun main() <noret> {
    let a <int> = 1;
    {
        let b <int> = a + 1;   // `a` is visible
    }
    // `b` is gone
}
```

The rule that `{` at statement start is *always* a block, and never a brace-initialised
value, is what makes this unambiguous. A brace-built value always follows something that
introduces it — a type name (`Point{x: 1}`), an `=`, a macro call (`map!{...}`) — so the
two forms never occupy the same position.

The consequence: a brace-initialised value cannot be a whole expression statement. `{ x: 1
};` alone on a line is a scope, not a discarded value. That costs nothing, since a statement
that builds a value and throws it away had no purpose. It does constrain future statement
forms — pattern-matching arms and block expressions will have to be introduced by a keyword
rather than by a brace. See `docs/adr/0011-a-bare-brace-opens-a-scope.md`.

## Recursion

A function can call itself:

```fin
@define printf(fmt: string, ...) <noret>;

fun countdown(a: int) <int> {
    if (a == 0) {
        return 0;
    }
    return countdown(a - 1);
}

fun main() <noret> {
    printf("%d\n", countdown(5));
}
```

A function may also be declared *inside* another function's body:

```fin
fun main() <noret> {
    fun recursive(a: int) <int> {
        if (a == 0) {
            return 0;
        }
        return recursive(a - 1);
    }
    let b <auto> = recursive(5);
}
```

A nested function is a plain function with a name only one scope can write. It is
visible from its declaration to the end of the scope it was written in and nowhere else
— a call above it is an undefined name, a sibling function cannot reach it, and two
bodies may each declare their own `helper`. Inside that scope it shadows a module-scope
function, a global, or an enum member of the same spelling, and it can be handed around
as an `fn` value like any other function.

What it may *not* do is read the enclosing body's variables. It has no frame of its own
to keep them in, so a nested function reading an enclosing local is refused rather than
compiled — pass the value in as an argument instead. The same rule applies to a lambda,
for the same reason.

## Lifetimes across scopes

A pointer to something declared inside a block would normally die with the block. The
`#[slaveof(...)]` attribute ties one declaration's lifetime to another variable's:

```fin
fun main() <noret> {
    let z <&int>;
    {
        #[slaveof(z)]
        let m <&int> = new int(5);   // m lives as long as z does
        z = m;
    }
    blame *z == 5;
}
```

`#[slaveof($Fin)]` is the static form — the declaration lives until the program exits:

```fin
#[slaveof($Fin)]
const invincible <&int> = new int(1778);
```

Both spellings parse and type-check. The attribute has no reader in the backend yet, so it
documents intent rather than changing generated code; nothing frees the allocation early
either, because Fin's memory management is a library concern rather than a compiler one
(`docs/adr/0003-memory-management-is-a-library.md`).

Next: [functions](05-functions.md).
