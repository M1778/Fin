# 9. Arrays and pointers

## Fixed and dynamic arrays

`[T, N]` includes its extent in its type. `[T]` carries a data pointer and a runtime
length, without a capacity field. Indexing begins at zero.

```fin
fun main() <noret> {
    let fixed <[int, 3]> = [1, 2, 3];
    let dynamic <[int]> = [4, 5, 6];
    blame fixed[2] == 3;
    blame dynamic.length == 3;
    dynamic[0] = 9;
    blame dynamic[0] == 9;
    delete dynamic;
}
```

A constant out-of-range index into a fixed array is rejected. Runtime indices and
dynamic extents do not get automatic bounds checks. Validate them explicitly.
`foreach` over an array avoids manual indexing for a simple traversal.

A `[T]` copy copies the handle, so its elements remain shared. An `&[T]` points to
the handle itself, allowing a callee to replace its pointer or length. A `[&T]`
is instead an array of pointers.

```fin
fun main() <noret> {
    let original <[int]> = [1, 2];
    let alias <[int]> = original;
    alias[0] = 8;
    blame original[0] == 8;
    delete original;
    // alias now refers to freed storage; do not use or delete it.
}
```

## Allocate a buffer

`new [T, count]{}` allocates zeroed elements and returns `[T]`. The count may be
computed at runtime. A bare `new [T]` has no allocation size and is refused.

```fin
fun main() <noret> {
    let count <int> = 4;
    let buffer <[int]> = new [int, count]{};
    blame buffer.length == 4;
    blame buffer[0] == 0;
    buffer[3] = 12;
    blame buffer[3] == 12;
    delete buffer;
}
```

`delete` releases the buffer; it does not recursively invent ownership for
pointer elements. Keep one owner and free each allocation once.

## Raw pointers

`&T` means pointer to `T`, `&value` takes an address, and `*pointer` dereferences.
Pass an address explicitly when a function expects a pointer.

```fin
fun increment(pointer: &int) <noret> { *pointer = *pointer + 1; }

fun main() <noret> {
    let local <int> = 4;
    increment(&local);
    blame local == 5;
    let heap <&int> = new int(7);
    increment(heap);
    blame *heap == 8;
    delete heap;
}
```

`new Point{x: 1}` returns `&Point`; `Point{x: 1}` returns a value. For a generic
parameter, allocate with `new T{}` and assign through the result; `new T(value)`
is not the primitive-allocation grammar.

Pointers can nest, but every level needs valid storage. Initialize the cells
before dereferencing them:

```fin
fun main() <noret> {
    let value <int> = 7;
    let pointer <&int> = &value;
    let outer <&&int> = &pointer;
    blame **outer == 7;
}
```

A returned reference must name storage that outlives the call, so handing back
the address of a local is refused (ADR 0055):

```fin error
struct Point { x <int> }

fun escapes() <&Point> {
    let local <Point> = Point { x: 1 };
    return &local;
}

fun main() <noret> { }
```

Reaching through a pointer is fine, because the storage belongs to whoever
passed it in, and so are a global and the receiver of a method — `self` is by
reference, so `&self.x` names the caller's object:

```fin
struct Point { x <int>
    fun ref() <&int> { return &self.x; }
}

fun fromParameter(p: &Point) <&int> { return &p.x; }

fun main() <noret> {
    let point <Point> = Point { x: 1 };
    blame *point.ref() == 1;
    blame *fromParameter(&point) == 1;
}
```

The check reads the declared type of the variable named, not what it points at,
so an address laundered through a pointer local is still accepted today
(issue #105). The other bypasses issue #109 tracked are closed now (ADR 0056):
a returned literal's fields and elements (`return W { p: &d }`, `return [&d]`),
a store into storage that outlives the frame (`*out = &d` into an out-
parameter, `g = &d` into a global), and a call wrapper that forwards its
parameter (`return id(&a)`) are all refused at the site, each blamed on the
field, element, right-hand side, or argument that carries the address, through
the same wording as the direct rule. Same-frame spellings stay legal: the
refusal reads a returned literal's fields, not the literal as built
(`Holder{p: &x}` in the same body, `[&x, &y]`, a linked `next: &tail`), a
store into a pointer local proven to point at this frame dies with it, and
forwarding a parameter through a call
(`fun id(p: &int) <&int> { return p; }`) stays legal at every depth, with the
facts converged across definition order and call cycles before any body is
walked. What still needs eyes: a whole struct local returned by value whose
pointer field a store filled, forwarding resolved through a method rather than
a top-level `fun`, calls into another module, a parameter reassigned under a
branch and returned after it, storage reached through a dereference of
something other than a bare name (`**slot`, `*self.q`), stores into a field or
element (`o.p = &d`), and the laundered reads of issue #105. Use caller-owned
storage or an explicitly owned heap allocation
for escaping data rather than relying on the refusal to catch every shape.

## Prototypes: builtin structural maps

A prototype type is `{KeyType, ValueType}` and a literal is `{key: value}`.
Use an explicit type when empty or when inference would lose needed information.
The runtime stores parallel key and value arrays, available as `.0` and `.1`.

```fin
fun main() <noret> {
    let scores <{string, int}> = {"Ada": 7, "Lin": 9};
    blame scores.contains("Ada");
    blame scores["Lin"] == 9;
    scores["Ada"] = 10;
    blame scores.get("Ada") == 10;
    blame scores.0.length == 2;
    blame scores.remove("Lin");
    blame scores.0.length == 1;
    delete scores.0;
    delete scores.1;
}
```

Lookup uses keys; `.0`/`.1` use positions to select the two arrays. String keys
compare bytes, unlike general string `==`. `get`/indexing a missing key aborts;
use `contains` first when absence is expected. `try_get` needs nullable runtime
support and is currently refused. `delete prototype[key]` removes an entry.

Current lookup scans entries. It is not a promise of hash-table performance or a
stable ordering contract. A nominal `HashMap` is a separate library type; conversion
through its `from_prototype` stores each entry via `__set`.

## Strings and ownership

A `string` is a NUL-terminated byte pointer, not a `[char]`. It does not support
the same direct indexing or `.length` lowering. Use a C declaration such as
`strlen`, a supported cast to `[char]`, or a verified library operation. A borrowed
view does not extend the original string's lifetime.

Struct destructors run at scope exit, but raw pointer and array ownership remains
explicit. A `#[slaveof(...)]` tie adjusts when a local is destroyed — pinning it
to `$Fin` or a global, or delaying a struct's destruction to its referent's scope
exit — and refuses ties that would dangle or cross a loop body or branch arm; it
does not borrow-check or collect. See [control flow](04-control-flow.md). The
`stdptr` library exposes reference-counting methods with incomplete safety and
execution paths; it is not a compiler-enforced ownership system. See
[the library reference](12-standard-library-tour.md).

Next: [modules and imports](10-modules-and-imports.md).
