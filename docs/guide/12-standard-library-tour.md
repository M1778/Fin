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

`HashMap<T, U>` is a real hash table: open addressing with linear probing over a bucket
vector, tombstones for erasure, and a rehash at a 0.75 load factor. A key and its value share
a *slot* in two parallel `Collection`s, and the bucket vector holds slot numbers.

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

`get_index(key)` returns the slot or `-1`, `exists(key)` and `len()` answer the obvious
questions, and `__get`/`__set` do the work that `operator []` and `operator []=` forward to.
`remove(key)`, `clear()`, `capacity()` and `is_empty()` are there too, and
`slot_count()`/`is_live(s)`/`key_at(s)`/`value_at(s)` are how you iterate, since `foreach` over
a struct is not a thing the language defines. A missing key is an assertion
(`blame idx >= 0, "key not found"`) rather than a returned sentinel, and `get_or(key, fallback)`
is the one-call form for a caller who does not know whether the key is there.
`from_prototype` is empty for the same reason `Collection`'s is.

**The hash is over the key's machine value, and for a `string` key that is the pointer, not the
bytes.** That is deliberate: `==` on two `string`s in this compiler compares pointers, so a
content hash paired with a pointer equality would be the one broken combination — two keys equal
by `==` landing in different buckets. So a `string`-keyed map works for keys that are literals or
are kept alive by the caller. A caller who needs a different hash supplies one:
`HashMap::with_hasher(f)` sets a `hasher <fn(any) -> int>` field that `hash_key` consults. It is a
function field rather than a `Hashable` bound because a generic bound is not dispatched on today.

Two limits worth knowing before storing much: a growth drops the old bucket vector rather than
freeing it, and an erased entry's key and value stay in their `Collection`s forever, because
compacting them would move every slot number the bucket vector holds.

## `error`

`Error` is the base error class — a `struct` marked `#[class]`, with a `message`, an
`error_id` defaulting to `-1`, a two-parameter constructor, a `format()`, a `describe()`
and a `has_code()`:

```fin
import { Error } from error::std;

struct MyError : <Error> {}

fun main() <noret> {
    let e <Error> = Error("boom");
    let coded <Error> = Error("boom", 7);
    let msg <string> = e.format();
    let full <string> = coded.describe();
}
```

Both calls are calls: `Error(msg: string, err_code: int = -1)` has a defaulted second
parameter, and a default has been optional at the call site since `d7a91df` (chapter 5).
An earlier version of this chapter said the constructor took one argument because a
defaulted parameter was still required — that stopped being true, and `lib/std/error.fin`
now carries the draft's two.

`format()` returns the message unchanged; `describe()` is the formatter, producing
`Error 7: boom` through C's `snprintf` into a buffer the caller owns — there is no
destructor to free it on. `has_code()` asks whether a code was supplied, which is the
comparison against `-1` a caller would otherwise write out.

Subclassing it is the load-bearing use — `IOError`, `CollectionError`, `HashMapError` and
`OwnershipError` are all `struct X : <Error> {}`.

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

`rptr<T>`, a reference-counted pointer with an explicit ownership protocol, and `wptr<T>`,
the non-owning handle beside it:

```fin
import { rptr } from stdptr::std;

fun main() <noret> {
    let p <rptr<int>> = rptr(5);
    let owned <bool> = p.is_owned();
    let q <&rptr<int>> = p.alias();
    let n <int> = p.refs();
    p.set(7);
    p.release();
}
```

Fields: `owned`, `borrowed`, `readonly restrict` (a readonly copy of itself), `readonly
value`, and two private `&int` counters. Methods: `is_owned`, `is_borrowed`, `is_givenback`,
`refs`, `borrows`, `alias`, `readonly_view`, `weak`, `own`, `borrow`, `giveback`, `release`,
`set`, `get`.

The count counts. `ref_counter` and `borrow_counter` are `&int` handles *shared* between
every handle over one value, so `alias()` builds a second `rptr` over the same two cells and
an increment through one is visible through all of them. That is what makes `refs()` and
`borrows()` answers about the value rather than about the handle, and what lets `own()`
refuse a move while a borrow taken through some *other* handle is outstanding. `release()`
decrements, frees the value and both counters at zero, and raises rather than double-freeing
if called twice. Every `blame` in the module guards a specific memory error; the module's own
header lists them one by one.

`wptr<T>` holds the value and the shared count but never increments it, so it does not keep
the value alive, and `is_alive()` reads the count `release()` decrements.

There is no working destructor, because `~Self()` does not parse and a `~rptr()` body would
need scope exit the backend does not run — it is declared and empty, and `release()` is the
explicit form. What no library can enforce is a raw `&rptr<T>` copied past a `release()`;
that needs a borrow check and there is none. And none of it runs yet: an `rptr` reaches
`codegen: a variable of type 'rptr<int>' is not lowered yet`.

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
