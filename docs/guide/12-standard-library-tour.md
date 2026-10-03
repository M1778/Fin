# 12. Standard library reference

The shipped library is [lib/std](../../lib/std/), not the design drafts under
`tests/samples/stdlib`. Import spelling is `from <module>::std`. All ordinary
library names need an import; `printf` is ambient and `format!` is a compiler builtin.

**Importable does not mean executable.** The loader publishes signatures and
retains generic templates, but ordinary imported Fin function bodies still do not
reach the backend. Template methods can encounter further limits such as `any`
conversions or unsupported calls. The examples below mark those boundaries.

## Choose a module

| Module | Main declarations | Application boundary |
| --- | --- | --- |
| [stdio](../../lib/std/stdio.fin) | `printf`, `Printable`, printing helpers, `Stream`, `File`, `IOResult<T>` | Ambient `printf` runs; ordinary helper calls and payload results have backend limits |
| [strings](../../lib/std/strings.fin) | Length, byte comparison, search, transforms, splitting/joining | Ordinary imported functions are frontend-only; allocating calls require caller cleanup |
| [math](../../lib/std/math.fin) | Numeric helpers, integer algorithms | Check each imported call before using it in an executable |
| [collection](../../lib/std/collection.fin) | `Collection<T>`, `CollectionError`, `coll!` | The concrete integer example below runs; buffer cleanup is incomplete; `from_prototype` stores entries via `push`, so `coll![...]` builds a populated collection |
| [hashmap](../../lib/std/hashmap.fin) | `HashMap<K,V>`, `HashMapError`, `map!` | Erased/custom-hasher paths and method emission have limits; `from_prototype` stores entries via `__set`, so `map!{...}` builds a populated map |
| [types](../../lib/std/types.fin) | Numeric aliases, `Number`, `Any`, `number2str` | Alias/bound declarations do not imply working dynamic values or conversion helpers |
| [typing](../../lib/std/typing.fin) | `Result<T,E>`, `IResult` | Generic payload enums do not build |
| [enums](../../lib/std/enums.fin) | `Enum`, `EnumType`, `getkeyid`, `keyidof` | Reflection declarations are not general runtime payload dispatch |
| [operators](../../lib/std/operators.fin) | `Add`, `Equal`, `Index`, `IndexAssign`, other operator interfaces | Some declared operators have no expression grammar or use erased results |
| [error](../../lib/std/error.fin) | `Error`, message/code accessors | Error values do not enable catchable exceptions |
| [stdptr](../../lib/std/stdptr.fin) | `rptr<T>`, `wptr<T>`, `OwnershipError` | Incomplete execution/safety paths; no borrow checker |
| [networking](../../lib/std/networking.fin) | Placeholder module | No networking implementation |

`strings` and `math` exist in the shipped library. `somelib` never did; it appeared
only in the sample drafts, whose last lines importing it now import the shipped
`networking` module instead. A module cannot be named `string` through a pathless
import because `string` is a keyword.

## Printing and formatting

For a runnable program, use `printf` with the correct C conversion for each value.
`format!` accepts only literal format strings and `{}` placeholders; see
[macros](11-macros-and-preprocessor.md#format).

```fin
fun main() <noret> {
    printf("%s: %d\n", "count", 3);
}
```

```output
count: 3
```

The typed helpers are declared as `print<X: Printable>(object: X)` and
`println<X: Printable>(object: X)`; implement `format_str() <string>` to satisfy
`Printable`. A plain string is not automatically an implementation of that interface.
String-specific helpers include `print_str`, `println_str`, `eprint_str`, and
`eprintln_str`. These declarations type-check, but ordinary imported helper calls
are not emitted. `println("hello")` is not a universal print builtin.

## `Collection<T>`

Common signatures (receiver omitted from this table):

| Method | Result / effect |
| --- | --- |
| `push(item: T)` / `pop_last()` | Append / remove and return the last element |
| `get(index: int)` / `set(index: int, value: T)` | Checked access / replacement |
| `len()` / `capacity()` | Filled cells / allocated cells |
| `reserve(amount: int)` | Ensure buffer capacity |
| `insert(index: int, item: T)` / `remove(index: int)` | Insert / remove and return an element |
| `first()` / `last()` | Read an endpoint; assert when empty |
| `contains(item: T)` / `index_of(item: T)` | Equality-based search; index or `-1` |
| `clear()` / `reverse()` / `extend(other: &Collection<T>)` | Mutate contents |

This concrete integer example builds and runs:

```fin
import { Collection } from collection::std;
fun main() <noret> {
    let values <Collection<int>> = Collection::<int>{};
    values.push(7);
    values.set(0, 8);
    blame values.get(0) == 8;
    blame values.len() == 1;
    printf("%d %d\n", values.get(0), values.len());
    // ponytail: Collection has no buffer destructor; process exit reclaims this demo's buffer.
}
```

```output
8 1
```

The current `Collection` declares no buffer destructor. For repeated allocation
where reclamation matters, use a raw `[T]` with explicit `delete` until the library
provides cleanup; deleting a heap-allocated wrapper alone does not free its buffer.

The buffer grows geometrically. Index-taking accessors assert bounds with `blame`.
`foreach` does not walk a `Collection`; use `for` with `len()` and `get()`.
`contains` uses `==`, so string elements compare pointers and struct elements need
an equality operator. Removing a pointer element does not free its pointee.

`Collection::from_prototype` stores the supplied values in order through `push`,
so `coll![1, 2, 3]` builds a populated collection holding the written elements.

## `HashMap<K,V>`

`get_index(key)` returns a slot or `-1`; `exists(key)` checks membership.
`__get(key)` and `__set(key, value)` implement access, with `get_or(key, fallback)`
for expected absence. `remove`, `clear`, `len`, `capacity`, and `is_empty` manage
the table. Iterate via `slot_count`, `is_live`, `key_at`, and `value_at`.

The following example builds and runs:

```fin
import { HashMap } from hashmap::std;
fun main() <noret> {
    let counts <auto> = HashMap::<string, int>();
    counts.__set("Fin", 1);
    let present <bool> = counts.exists("Fin");
    let value <int> = counts.get_or("Fin", 0);
}
```

The implementation uses open addressing. Its default string hash and equality
use pointer identity, unlike builtin prototype string keys. Reconstructed strings
with equal bytes are not interchangeable keys. Supplying only a content hash does
not repair pointer equality. `with_hasher(fn(any) -> int)` also depends on incomplete
erased-call support.

Growth can retain old storage; removed slots retain key/value storage.
`from_prototype` stores each pair through `__set`, so `map!{...}` builds a
populated map holding the written entries.

## Strings and math

`strings::std` declares `len`, `equals`, `compare`, `find`, `contains`,
`index_of_char`, `starts_with`, `ends_with`, `substr`, `concat`, `trim`,
`to_upper`, `to_lower`, `split`, `join`, `to_chars`, `from_chars`, and `free_str`.
Search functions return indices or `-1`; `equals` compares bytes.

These ordinary string and integer-algorithm imports build and run:

```fin
import { len, equals } from strings::std;
import { gcd, clamp } from math::std;
fun main() <noret> {
    let length <int> = len("Fin");
    let same <bool> = equals("Fin", "Fin");
    let divisor <int> = gcd(12, 18);
    let bounded <int> = clamp::<int>(12, 0, 10);
}
```

Allocating string helpers return owned storage. `to_chars` returns a fresh buffer;
`split` returns a collection containing separately allocated strings. Freeing the
outer container alone does not free each string. Case conversion is ASCII-based.
Use direct C declarations for a small executable path such as `strcmp` or `strlen`
when the imported helper cannot lower; see [foreign calls](05-functions.md#foreign-declarations-and-abi).

`math::std` declares generic `min`, `max`, `clamp`, `abs`, `signum`, and `in_range`;
integer algorithms include `gcd`, `lcm`, `ipow`, `isqrt`, `floor_div`, `floor_mod`,
`is_even`, and `is_odd`. Read the function body for domain checks and overflow
limits; the existence of a function does not add checked arithmetic to the language.

## Files, streams, and results

`Stream` is an in-memory `[char]` buffer with `seek`, `tell`, `remaining`, `rewind`,
`read`, `read_all`, `write`, `expand`, and `capacity` methods. `File` **is declared**
and exposes static `exists`, `size`, `read_all`, `write_text`, `append_text`,
`remove`, and `open`. For example, `write_text(path: string, text: string, count: int)`
requires the byte count; it does not infer it from the string.

These APIs use C wrappers and imported method bodies. They are not a verified
end-to-end file API merely because their declarations are available. A direct C
FFI operation is often the smallest executable path until those imports lower.

`typing::std` declares `Result<T,E>` with `Ok`/`Err`; `stdio::std` declares
`IOResult<T>`. Their implementation blocks contain methods including `unwrap`,
`unwrap_or`, `expect`, `select`, `is_ok`, and `is_err`. The methods exist, but
payload enum code generation is still missing. Use an explicit status and concrete
data when recoverable errors must run today.

## Smart pointers

`rptr<T>` declares `get`, `set`, `alias`, `readonly_view`, `weak`, `own`, `borrow`,
`giveback`, `release`, `refs`, and `borrows`. `get()` returns `&T`, not `T`.
`wptr<T>` is non-owning. The library's checks cannot prevent dangling raw pointers,
and a released handle can still contain pointers to freed counters. Do not read,
release again, or inspect a weak handle after its counter storage has died.

Treat these APIs as incomplete library work, not automatic memory safety.
The language's ordinary destructor support does not imply that this library's
ownership protocol is complete. For a small application, use explicit allocation,
a clear owner, and one cleanup path.
