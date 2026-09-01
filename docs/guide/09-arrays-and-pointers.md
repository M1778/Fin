# 9. Arrays and pointers

## Two kinds of array

`[T, N]` has its extent in its type. `[T]` does not:

```fin
let fixed <[int, 3]> = [1, 2, 3];
let dyn <[int]> = [4, 5, 6];
```

They are different types, not one type with a number missing. Indexing and assignment work
the same on both:

```fin
@define printf(fmt: string, ...) <noret>;

fun main() <noret> {
    let fixed <[int, 3]> = [1, 2, 3];
    printf("%d\n", fixed[0]);

    let dyn <[int]> = [4, 5, 6];
    printf("%d %d\n", dyn[1], dyn.length);
    dyn[0] = 9;
    printf("%d\n", dyn[0]);
}
```

An array literal is checked element by element against its annotation's element type, so
`let a <[uint]> = [7, 3, 4];` is fine — the literal is offered `uint` rather than defaulting
to `int`.

## `.length`

A `[T]` carries its length as an `int`:

```fin
let n <int> = dyn.length;
```

This works through a pointer as well — a callee handed a `&[T]` can still ask. That is the
fact that fixes the representation: a `[T]` is exactly two words, a pointer to
heap-allocated elements and a length, and a `&[T]` is a pointer to that pair rather than to
the elements. There is no capacity field; growth is something a library builds *on* `[T]`,
never *into* it. See `docs/adr/0025-a-dynamic-array-is-a-pointer-and-a-length.md`.

`string` also has a `.length`, which type-checks but is not yet lowered.

## Allocating an array

`new [T, n]{}` allocates on the heap, and `n` may be computed at runtime:

```fin
fun main() <noret> {
    let n <int> = 4;
    let heap <[int]> = new [int, n]{};
    heap[0] = 1;
    delete heap;
}
```

The result is a `[T]`, not a pointer to a fixed-size array — this is the one `new` that
does not produce a pointer. `delete` frees it, and it frees the pair's data word.

This builds and runs. The elements are zeroed, which is what the empty `{}` means; `new [int]`
with no extent at all is refused, because there is no count to allocate and zero would be a
guess. Array literals, indexing, assignment and `.length` build and run too, including
`.length` on an array that never had an address (`give().length`).

## Passing arrays

A `[T]` passed by value is copied; passed as `&[T]` it is not:

```fin
@define printf(fmt: string, ...) <noret>;

type Number = int | uint | float;

fun total<T: Number>(array: &[T]) <int> {
    let acc <int> = 0;
    for (let i <int> = 0; i < array.length; i++) {
        acc += cast<int>(array[i]);
    }
    return acc;
}

fun main() <noret> {
    let a <[int]> = [1, 2, 3];
    printf("%d\n", total(&a));
}
```

Array types can be named:

```fin
type ArrayType<T> = [T];      // generic array alias
type IntArray = [int];        // array of ints
type PtrIntArray = &[int];    // pointer to an array of ints
type IntPtrArray = [&int];    // array of pointers to int
```

The last two are worth reading twice. `&[int]` is a pointer to an array; `[&int]` is an
array of pointers.

## References and pointers

`&T` is a pointer to `T`. `&expr` takes an address, `*expr` dereferences:

```fin
@define printf(fmt: string, ...) <noret>;

fun bump(p: &int) <noret> {
    *p = *p + 1;
}

fun main() <noret> {
    let a <int> = 10;
    let p <&int> = &a;
    printf("%d\n", *p);
    bump(&a);
    printf("%d\n", a);     // 11
}
```

The address must be taken explicitly. `bump(a)` where `&int` is expected is a type error
(`expected '&int', got 'int'`); the corpus describes a future mode where the compiler passes
by reference automatically with a warning, but that is not what happens today.

A generic function can take pointers, which is how a swap is written:

```fin
fun swap<T>(a: &T, b: &T) <noret> {
    let temp <T> = *b;
    *b = *a;
    *a = temp;
}
```

## Heap allocation with `new`

`new` allocates. The parenthesised form initialises a primitive; the braced form initialises
a struct or an array:

```fin
let h <&int> = new int(5);              // primitive, with a value
let p <&Point> = new Point{x: 1, y: 2}; // struct
let a <[int]> = new [int, 4]{};         // array
```

`delete` frees:

```fin
delete h;
delete a;
```

Pointers nest arbitrarily, and each level is allocated with a `*` per remaining level:

```fin
fun main() <noret> {
    let x <&&int>;
    *x = new int*;
    **x = 10;
}
```

`&&int` is a pointer to a pointer to an int; `new int*` allocates a cell that holds an
`int*`. This generalises — `&&&&int` with `new int***`, `new int**`, `new int*` down the
chain — but past two levels it is rarely what you want.

There is one restriction on `new` with a generic type. `new T(v)` is a syntax error, because
`new` accepts `(...)` after a primitive type keyword and `{...}` after an identifier, and a
type parameter lexes as an identifier whatever it is bound to. The standard library's
smart pointer works around it by allocating zeroed and writing through:

```fin
self.value = new T{};
*self.value = initial_value;
```

## Where memory management lives

Fin's answer to lifetimes is deliberately not in the compiler:
`docs/adr/0003-memory-management-is-a-library.md` makes allocation and reclamation a library
concern. Two pieces of that are visible from here.

`#[slaveof(x)]` ties a declaration's lifetime to another variable's, and `#[slaveof($Fin)]`
extends it to program exit (chapter 4). Both parse; neither has a backend reader yet.

`stdptr::std` provides `rptr<T>`, a reference-counted pointer with an ownership protocol —
`owned`, `borrowed`, `restrict`, and the methods `alias`, `refs`, `borrows`, `set`, `own`,
`borrow`, `giveback`, `release`:

```fin
import { rptr } from stdptr::std;

fun main() <noret> {
    let p <rptr<int>> = rptr(5);
    let q <&rptr<int>> = p.alias();
    let n <int> = p.refs();
}
```

The reference and borrow counts are `&int` handles shared between every handle over one
value, so an increment through one is visible through all of them and `refs()` answers a
question about the value rather than about the handle — chapter 12 has the protocol. What
the library still cannot see is a raw `&rptr<T>` copied past a `release()`: that needs a
borrow check and there is none. Nor does an `rptr` reach an executable — the example above
type-checks and then reports `codegen: a variable of type 'rptr<int>' is not lowered yet`.

Bounds checking is also undecided. Nothing in the language says an index is checked against
the length; `Collection`'s `__get` asserts with `blame` because the library chose to, not
because indexing does.

Next: [modules and imports](10-modules-and-imports.md).
