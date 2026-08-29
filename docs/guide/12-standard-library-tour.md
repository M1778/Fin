# 12. A tour of the standard library

The standard library lives in `lib/std/` and ships beside the compiler. Nothing configures
it: if you do not pass `--fin-libs` and do not set `$FIN_LIBS`, `finc` finds it relative to
its own binary.

Every module declares its contents inside `namespace std`, so the import spelling is always
`from <module>::std`:

```fin
import { HashMap } from hashmap::std;
```

Read this chapter as a map rather than a reference. Each module's own header comment lists
what it diverges from in its design draft and why, and that comment is the authoritative
description of the module's current state.

An important caveat throughout: a call to an imported function is not yet lowered to machine
code, so — with one exception — everything here type-checks and none of it yet reaches an
executable. Import it, check it, and read the code: that is what the library supports today.
The exception is the ambient `printf`, which builds and runs with nothing written above it,
because it is an extern rather than a Fin function and chapter 10 explains the mechanism.

## The modules

| Module | Contents |
| --- | --- |
| `stdio` | `printf`, `print`, `println`, `Printable`, `Stream`, `IStream`, `IOResult`, `IOError` |
| `collection` | `Collection<T>`, the dynamic array; `CollectionError` |
| `hashmap` | `HashMap<T, U>`, the associative array; `HashMapError` |
| `error` | `Error`, the base error class |
| `types` | numeric aliases, `Number`, `Any`, `number2str` |
| `typing` | `Result<T, U>`, `IResult` |
| `enums` | `Enum`, `EnumType`, `getkeyid`, `keyidof` |
| `operators` | one interface per overloadable operator |
| `stdptr` | `rptr<T>`, the reference-counted pointer; `OwnershipError` |
| `networking` | empty — a placeholder so the import resolves |
| `somelib` | empty — a directory-shaped module, present to exercise directory resolution |

## `stdio`

`printf` is C's, declared with `@define` under `#[llvm_name="printf"]` and marked
`#[global]` — the one name in the language that needs no import (chapter 10). It is variadic
with no format checking, and it both checks and builds with nothing written above it:

```fin
fun main() <noret> {
    printf("%d %s\n", 42, "ambient");
}
```

Above it sits a small typed layer built on the `Printable` interface:

```fin
import { print, println, Printable } from stdio::std;

struct Word : <Printable> {
    text <string>,

    pub fun format_str(self: &Self) <string> {
        return self.text;
    }
}

fun main() <noret> {
    let w <Word> = Word{text: "hi\n"};
    print::<Word>(w);
    println::<Word>(w);
}
```

`Stream` is an in-memory byte stream implementing `IStream`: `seek`, `read(nbytes)`,
`read_all()` and `expand(nbytes)` over a `[char]` buffer, with the length and read pointer
carried as fields. `read` copies forward from the pointer and leaves it past what it read;
`expand` allocates a larger buffer and copies into it.

`IOResult<T>` is the result enum, with `Err <IOError>` and `Ok <T>` members. Its
`implements` block is not present — the `keyidof` question from chapter 8 blocks all three
of its bodies.

`File` and `FileIO` are absent. Their draft depends on names nothing declares.

## `collection`

`Collection<T>` is the growable array, and it is the most-used library type in the corpus. It
wraps a `[T]` and grows by allocating a fresh buffer and copying, which is exactly why a
`[T]` itself has no capacity field (chapter 9).

```fin
import { Collection } from collection::std;

fun main() <noret> {
    let c <&Collection<int>> = new Collection::<int>{};
    c.push(1);
    c.push(2);
    let n <int> = c.len();
    let first <int> = c.get(0);
    let last <int> = c.pop_last();
    let via_index <int> = c[0];
}
```

The surface is `push`, `pop_last`, `get(index)`, `len()`, `__get`/`__set`, `operator []` and
`operator []=`, plus a static `from_prototype`. `__get` and `__set` bounds-check with `blame`.

`Collection::from_prototype({0: 10, 1: 20})` is declared because the corpus calls it, and it
returns an empty collection: nothing in the language can walk a prototype's entries yet, so
the keys are dropped. Do not use it expecting the values to arrive.

## `hashmap`

`HashMap<T, U>` is two parallel `Collection`s and a linear scan for the key. The name
describes the interface, not the algorithm — there is no hashing.

```fin
import { HashMap } from hashmap::std;

fun main() <noret> {
    let m <auto> = HashMap::<string, int>();
    m["a"] = 1;
    let v <int> = m["a"];
    let has <bool> = m.exists("a");
    let n <int> = m.len();
}
```

`get_index(key)` returns the index or `-1`, `exists(key)` and `len()` answer the obvious
questions, and `__get`/`__set` do the work that `operator []` and `operator []=` forward to.
A missing key is an assertion (`blame idx >= 0, "key not found"`) rather than a returned
sentinel. `from_prototype` is empty for the same reason `Collection`'s is.

## `error`

`Error` is the base error class — a `struct` marked `#[class]`, with a `message`, an
`error_id` defaulting to `-1`, a one-argument constructor and a `format()`:

```fin
import { Error } from error::std;

struct MyError : <Error> {}

fun main() <noret> {
    let e <Error> = Error("boom");
    let msg <string> = e.format();
}
```

Subclassing it is the load-bearing use — `IOError`, `CollectionError`, `HashMapError` and
`OwnershipError` are all `struct X : <Error> {}`. The constructor takes one argument, not the
draft's two, because a defaulted parameter is still required at the call site (chapter 5).

## `types`

Numeric aliases and the constraint set every numeric generic uses:

```fin
import { i32, i64, u32, u64, f32, f64, Number, number2str, Any } from types::std;

fun main() <noret> {
    let a <i32> = 1;
    let s <string> = number2str::<int>(42);
}
```

`i32 = int`, `i64 = int{64}`, `u32 = uint`, `u64 = uint{64}`, `f32 = float`, `f64 = double`.
`Number` is the constraint set from chapter 2. `Any = any` and `nullptr = any`.

`array<T> = [T]` is declared and marked `#[export]`, but importing it reports `Module 'types'
does not export 'array'` — a generic alias does not reach the export table yet. Write `[T]`
directly.

`resolve_type` and `resolve_arr_type` are declared with no body: nothing in the language
produces a `$type` value, so they are compiler intrinsics rather than stubs.

## `typing`

`Result<T, U>` with `Ok(T)` and `Err(U)`, and the `IResult` interface describing `unwrap`,
`expect` and `select`:

```fin
import { Result } from typing::std;

fun main() <noret> {
    let r <Result<int, string>> = Result::Ok(1);
}
```

`IResult` is declared and not implemented — its three bodies all guard on `keyidof(Ok)`,
which is the undecided question from chapter 8.

## `enums`

Enum reflection: the `Enum` marker interface, `EnumType = any implements <Enum>`, and the two
intrinsics `getkeyid(value)` and `keyidof(member)`. Chapter 8 covers how they pair up.

## `operators`

One interface per overloadable operator, inside `namespace std { namespace ops { ... } }`:
`Equal`, `NotEqual`, `GreaterThan`, `LessThan`, `Add`, `AddAssign`, `Sub`, `Mul`, `Div`,
`Mod`, `BitAnd`, `BitOr`, `ShiftLeft`, `ShiftRight`, their assigning forms, `Unary`, `Not`,
`Index`, `IndexAssign`, `Deref`, `FnCall`, and `Addable` (which is `Add` under the name the
corpus uses).

Each requires its operator and returns `Output`, which is `any`:

```fin
import { Add } from operators::std;

struct N : <Add> {
    v <int>,
    pub operator +(rhs: any) <any> { return self.v; }
}
```

`Collection` and `HashMap` both declare `: <Index, IndexAssign>`, which is where these
interfaces earn their place.

Two of them describe operators the expression grammar does not have yet — `BitAnd` and
`BitOr` (chapter 3).

## `stdptr`

`rptr<T>`, a reference-counted pointer with an explicit ownership protocol:

```fin
import { rptr } from stdptr::std;

fun main() <noret> {
    let p <rptr<int>> = rptr(5);
    let owned <bool> = p.is_owned();
    p.set(7);
    p.release();
}
```

Fields: `owned`, `borrowed`, `readonly restrict` (a readonly copy of itself), `readonly
value`. Methods: `is_owned`, `is_borrowed`, `is_givenback`, `own`, `borrow`, `giveback`,
`release`, `set`, `get`. The two `blame`s in `own()` are the whole discipline the module can
express — you cannot give what you do not have, and you cannot give it away while somebody
holds it.

Its own header is candid about the rest: the counter is never incremented and ownership is
not enforced against aliases, because both need codegen and a borrow check that do not exist.
There is no destructor, because `~Self()` does not parse; `release()` is the explicit form.

## Reading the library as documentation

Two habits are worth adopting.

First, `lib/std/<module>.fin` opens with a comment naming the sample that specifies it and
listing every divergence with its reason. That comment is where "why does the library spell
it this way" is answered.

Second, `tests/samples/stdlib/*.fin` holds the *drafts* — the fuller designs the shipped
modules are cut down from. They are the design intent, not the current behaviour. When the two
disagree, `lib/std` is what the compiler will accept.

Two modules do not type-check standalone (`finc lib/std/stdio.fin` and `finc
lib/std/error.fin` report a circular dependency), which is a loader limitation rather than a
problem with either file — both work fine when reached through an `import`.
