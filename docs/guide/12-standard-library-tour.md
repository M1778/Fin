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

All 37 modules in `lib/std/` check cleanly standalone with zero diagnostics (`finc lib/std/<mod>.fin`).
Through the module loader cache (ADR 0032), imported functions, structs, interfaces, enums,
prototypes, and templates lower directly to machine code in native binaries built with `-o`.
Standard library modules are actively exercised and verified across the compiler's execution test
suites (`Soundness_BundledStdlib.*`). The ambient `printf` is also available everywhere without an
explicit import.

## The modules

| Module | Category (Python Analog) | Contents |
| --- | --- | --- |
| `types` | Core Language (`typing`) | numeric aliases, `Number`, `Any`, `number2str` |
| `typing` | Core Language (`typing`) | `Result<T, U>`, `IResult` |
| `enums` | Core Language (`enum`) | `Enum`, `EnumType`, `getkeyid`, `keyidof` |
| `operators` | Core Language (`operator`) | one interface per overloadable operator |
| `error` | Core Language (`builtins`) | `Error`, the base error class |
| `stdptr` | Memory Management | `rptr<T>`, the reference-counted pointer; `OwnershipError` |
| `collection` | Collections (`list`) | `Collection<T>`, the dynamic array; `CollectionError` |
| `hashmap` | Collections (`dict`) | `HashMap<T, U>`, the associative array; `HashMapError` |
| `deque` | Collections (`collections.deque`, `queue`) | `Deque<T>` (circular buffer), `Queue<T>` (FIFO), `Stack<T>` (LIFO) |
| `algorithm` | Algorithms (`bisect`, `sorted`) | `sort_ints`, `binary_search_ints`, `lower_bound_ints`, `upper_bound_ints`, `reverse_ints`, `min_element_ints`, `max_element_ints`, `is_sorted_ints`, `swap_ints`, `fill_ints`, `copy_ints` |
| `bits` | Bit Manipulation (`<bit>`) | `bit_and`, `bit_or`, `bit_xor`, `bit_not`, `get_bit`, `set_bit`, `clear_bit`, `toggle_bit`, `popcount`, `bit_length`, `count_leading_zeros`, `count_trailing_zeros`, `is_power_of_two`, `rotate_left`, `rotate_right` |
| `strings` | Text (`str`, `string`) | `len`, `starts_with`, `ends_with`, `contains`, `index_of`, `repeat`, `slice`, `concat`, `parse_int` |
| `ascii` | Text (`string.ascii_*`, `ctype`) | `is_alpha`, `is_digit`, `is_alnum`, `is_space`, `is_upper`, `is_lower`, `to_upper`, `to_lower`, `digit_value` |
| `encoding` | Text Encodings (`base64`, `binascii`) | `hex_encode`, `hex_decode`, `base64_encode`, `base64_decode`, `free_encoded` |
| `math` | Math & Numerics (`math`) | arithmetic constants (`PI`, `E`), transcendentals (`sin`, `cos`, `sqrt`, `pow`, `log`), `abs`, `min`, `max`, `gcd` |
| `random` | Math & Numerics (`random`) | `seed`, `seed_now`, `rand`, `random_int`, `random_float`, `random_bool` |
| `hash` | Math & Numerics (`hashlib`, `zlib`) | `crc32`, `fnv1a_32`, `fnv1a_64`, `djb2`, `sdbm` |
| `stdio` | I/O & Streams (`sys.stdout`) | `printf`, `print`, `println`, `Printable`, `Stream`, `IStream`, `IOResult`, `IOError` |
| `fs` | Filesystem (`os`, `shutil`) | `file_exists`, `remove_file`, `file_size`, `read_to_string`, `write_string`, `create_dir`, `remove_dir`, `rename_path`, `is_dir`, `is_file` |
| `path` | Filesystem (`os.path`, `pathlib`) | `is_absolute`, `is_relative`, `join`, `basename`, `dirname`, `extname`, `free_path` |
| `env` | System & Process (`os`, `sys`) | `get_env`, `has_env`, `set_env`, `unset_env`, `exit`, `get_pid` |
| `time` | System & Process (`time`) | `now`, `sleep_sec`, `sleep_ms`, `diff_sec` |
| `networking` | Networking (`socket`) | `Socket` struct with `Socket::dial`, `Socket::serve`, `accept_conn`, `send_text`, `recv_bytes`, `local_port`, `shutdown` |
| `testing` | Development & QA (`unittest`) | `assert_true`, `assert_false`, `assert_eq_int`, `assert_eq_string`, `assert_null`, `assert_not_null` |
| `token` | Compiler & Language Tools (`tokenize`) | `Token`, `SourceLocation`, `Span`, token kinds, keywords, `token_name`, `lookup_keyword` |
| `diag` | Compiler & Language Tools | `Diagnostic`, `DiagnosticBag`, severity levels, `format_simple`, `format_snippet` |
| `scanner` | Compiler & Language Tools | `Scanner`, character cursor, peek/advance, number/string/identifier/operator tokenization |
| `parse` | Compiler & Language Tools | `TokenStream`, operator precedence (`PREC_*`), lookahead/backtracking, `match_token`, `expect` |
| `ast` | Compiler & Language Tools (`ast`) | `AstNode`, AST node kinds (`AST_*`), `make_ident`, `make_literal_*`, `make_binary`, `make_unary`, `make_var_decl`, `free_ast` |
| `scope` | Compiler & Language Tools | `Scope`, `Symbol`, `new_scope`, `insert_symbol`, `lookup`, `has`, `depth`, `free_scope` |
| `typesys` | Compiler & Language Tools (`types`) | `Type`, `type_equals`, `is_assignable`, `make_pointer_type`, `make_dyn_array_type`, primitive type singletons |
| `ir` | Compiler & Language Tools | `IrFunction`, `IrBlock`, `IrInstruction`, 3-address opcodes (`ir_add`, `ir_alloca`, `ir_ret`, etc.), `fresh_temp` |
| `emitter` | Compiler & Language Tools | `CEmitter`, `new_c_emitter`, C source generation, `emit_fn_header`, `emit_var_decl`, `emit_return`, `map_fin_type_to_c` |
| `printer` | Compiler & Language Tools (`ast.unparse`) | `AstPrinter`, `new_ast_printer`, `format_expr`, `format_stmt`, canonical Fin source formatter |
| `buffer` | Memory & Buffers (`io.StringIO`, `bytearray`) | `Buffer`, `new_buffer`, `new_string_builder`, `write_string`, `write_int`, `to_string`, `free_buffer` |
| `process` | System & Process (`subprocess`) | `ProcessResult`, `run_command`, `exec_command`, `is_success`, `free_process_result` |
| `argparse` | CLI & Driver (`argparse`) | `ArgParser`, `FlagDef`, `OptionDef`, `new_arg_parser`, flags, options, positionals, `parse` |
| `somelib` | Diagnostics | empty — a directory-shaped module, present to exercise directory resolution |


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

`IResult` is fully implemented for `Result<T, U>` in an `implements` block, supplying
`unwrap`, `expect`, and `select` (which guard on `keyidof(Ok)` and `getkeyid`, settled
in chapter 8 and ADR 0037), along with helper methods `unwrap_or`, `is_ok`, and `is_err`.

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

While `~Self()` parses as an interface destructor requirement and the backend supports
automatic scope-exit destruction (ADR 0016, ADR 0030), `rptr` provides an explicit `release()`
protocol to decrement references and reclaim memory deterministically. What no library can
enforce is a raw `&rptr<T>` copied past a `release()`; that needs a compile-time borrow
checker and there is none. `rptr<T>` compiles cleanly to machine code and executes in native
binaries produced with `-o`.

## `math`

Arithmetic constants and common mathematical operations:

```fin
import { PI, E, abs, min, max, clamp, gcd, lcm, ipow, isqrt } from math::std;

fun main() <noret> {
    let pi <double> = PI;
    let m <int> = min::<int>(10, 20);
    let c <int> = clamp::<int>(15, 0, 10);
    let g <int> = gcd(48, 18);
    let p <int> = ipow(2, 10);
}
```

Constants include `PI`, `E`, `PI_F`, and `E_F`. Generic utilities (`abs::<T>`, `min::<T>`,
`max::<T>`, `clamp::<T>`, `signum::<T>`) work over types satisfying the numeric operators.
Integer algorithms include `gcd`, `lcm`, `ipow`, `isqrt`, `floor_div`, and `floor_mod`.
Float and transcendental functions matching Python's `math` module are directly callable:
`sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sqrt`, `cbrt`, `pow`, `exp`,
`log`, `log2`, `log10`, `floor`, `ceil`, `round`, `trunc`, and `hypot`.

## `strings`

String query and manipulation utilities:

```fin
import { len, starts_with, ends_with, contains, index_of, repeat, concat, parse_int, free_string } from strings::std;

fun main() <noret> {
    let s <string> = "hello world";
    let n <int> = len(s);
    let sw <bool> = starts_with(s, "hello");
    let has <bool> = contains(s, "world");
    let rep <string> = repeat("ab", 3);
    let val <int> = parse_int("-12345");
    free_string(rep);
}
```

Functions include length (`len`), prefix/suffix checks (`starts_with`, `ends_with`), substring search
(`contains`, `index_of`), slicing (`slice`), repetition (`repeat`), concatenation (`concat`), and
pure integer parsing with bounds validation (`parse_int`). Allocated strings produced by `repeat`,
`slice`, and `concat` are owned by the caller and freed with `free_string`.

## `ascii`

ASCII character classification and transformation, matching Python's `string.ascii_*` and `<ctype.h>`:

```fin
import { is_alpha, is_digit, is_alnum, is_space, is_upper, is_lower, to_upper, to_lower, digit_value } from ascii::std;

fun main() <noret> {
    let a <bool> = is_alpha('x');
    let d <bool> = is_digit('5');
    let sp <bool> = is_space(' ');
    let up <char> = to_upper('a');
    let lo <char> = to_lower('Z');
    let num <int> = digit_value('9'); // returns 9
}
```

Provides fast character classification (`is_alpha`, `is_digit`, `is_alnum`, `is_space`, `is_upper`,
`is_lower`, `is_ascii`, `is_print`, `is_cntrl`, `is_punct`, `is_xdigit`), case mapping (`to_upper`,
`to_lower`), and radix conversion helpers (`digit_value`, `hex_value`).

## `encoding`

Hexadecimal and RFC 4648 Base64 data encoding and decoding, matching Python's `base64` and `binascii`:

```fin
import { hex_encode, hex_decode, base64_encode, base64_decode, free_encoded } from encoding::std;

fun main() <noret> {
    let h <string> = hex_encode("hello"); // "68656c6c6f"
    let orig <string> = hex_decode(h);     // "hello"
    let b64 <string> = base64_encode("hello"); // "aGVsbG8="
    let dec <string> = base64_decode(b64);     // "hello"

    free_encoded(h);
    free_encoded(orig);
    free_encoded(b64);
    free_encoded(dec);
}
```

Provides standard `hex_encode` and `hex_decode` for lowercase hexadecimal string conversions,
and `base64_encode` and `base64_decode` for 6-bit binary-to-text transfers. Strings returned by
the encoding functions are caller-owned and released via `free_encoded`.

## `path`

Filesystem path queries and transformations:

```fin
import { join, basename, dirname, extname, is_absolute, is_relative, free_path } from path::std;

fun main() <noret> {
    let p <string> = join("/usr/local", "bin");
    let base <string> = basename(p);
    let dir <string> = dirname(p);
    let ext <string> = extname("/path/to/file.txt");
    let abs <bool> = is_absolute(p);
    free_path(p);
    free_path(base);
    free_path(dir);
    free_path(ext);
}
```

Path functions normalize path components, handle root boundaries and trailing slashes, and
differentiate absolute and relative paths. Paths returned by allocating functions are owned
by the caller and released with `free_path`.

## `env`

Environment variable inspection, process identification, and process exit:

```fin
import { get_env, has_env, set_env, unset_env, get_pid, exit } from env::std;

fun main() <noret> {
    let home <string> = get_env("HOME", "/");
    let exists <bool> = has_env("PATH");
    let pid <int> = get_pid();
}
```

Provides safe wrappers around standard C environment facilities (`getenv`, `setenv`, `unsetenv`),
allowing default fallbacks with `get_env(name, default_val)`. `exit(code)` delegates to C's `exit`.

## `time`

Epoch clock queries and synchronous thread delays:

```fin
import { now, sleep_sec, sleep_ms, diff_sec } from time::std;

fun main() <noret> {
    let start <long> = now();
    sleep_ms(100);
    let finish <long> = now();
    let elapsed <long> = diff_sec(start, finish);
}
```

`now()` returns the current Unix epoch time in seconds (`time(NULL)`). `sleep_sec` and `sleep_ms`
pause execution via C `sleep` and `usleep`. `diff_sec` computes the signed elapsed seconds between
two timestamps.

## `fs`

File and directory inspection, manipulation, reading, and writing (matching Python's `os` and `shutil`):

```fin
import { file_exists, remove_file, file_size, read_to_string, write_string, append_string, free_file_content, create_dir, remove_dir, rename_path, is_dir, is_file } from fs::std;

fun main() <noret> {
    let exists <bool> = file_exists("/tmp/hello.txt");
    write_string("/tmp/hello.txt", "Fin systems language");
    append_string("/tmp/hello.txt", "\n");
    let size <long> = file_size("/tmp/hello.txt");
    let content? <string> = read_to_string("/tmp/hello.txt");
    free_file_content(content);
    remove_file("/tmp/hello.txt");

    let ok <bool> = create_dir("/tmp/fin_demo_dir");
    let dir_check <bool> = is_dir("/tmp/fin_demo_dir");
    let file_check <bool> = is_file("/tmp/fin_demo_dir");
    rename_path("/tmp/fin_demo_dir", "/tmp/fin_demo_dir2");
    remove_dir("/tmp/fin_demo_dir2");
}
```

Provides file and directory manipulation built directly over standard POSIX / C APIs (`fopen`, `fread`,
`fwrite`, `mkdir`, `rmdir`, `rename`, `stat`, `unlink`). `read_to_string` returns a nullable `string?`
(`null` if the file could not be read), which must be released with `free_file_content` when no longer needed.

## `random`

Pseudo-random number generation and entropy seeding:

```fin
import { seed, seed_now, rand, random_int, random_float, random_bool } from random::std;

fun main() <noret> {
    seed_now();
    let n <int> = random_int(1, 100);
    let f <float> = random_float();
    let b <bool> = random_bool();
}
```

Wraps C's `rand` and `srand`. `seed_now()` seeds the generator using the current epoch seconds.
`random_int(min, max)` produces inclusive uniform pseudo-random integers. `random_float()`
returns a float in `[0.0, 1.0)`.

## `hash`

Non-cryptographic hashing and checksum algorithms matching Python's `hashlib` and `zlib.crc32`:

```fin
import { crc32, fnv1a_32, fnv1a_64, djb2, sdbm } from hash::std;

fun main() <noret> {
    let c <uint> = crc32("hello");
    let f32 <uint> = fnv1a_32("hello");
    let f64 <ulong> = fnv1a_64("hello");
    let dj <ulong> = djb2("hello");
    let sd <ulong> = sdbm("hello");
}
```

Provides standard IEEE 802.3 32-bit CRC (`crc32`), Fowler-Noll-Vo 32-bit and 64-bit algorithms
(`fnv1a_32`, `fnv1a_64`), Dan Bernstein's `djb2`, and the `sdbm` hash. Built in pure Fin using
arithmetic bitwise techniques.

## `networking`

Low-level TCP network socket communication matching Python's `socket`:

```fin
import { Socket } from networking::std;

fun main() <noret> {
    // Start listening on loopback (port 0 chooses an ephemeral port)
    let srv <Socket> = Socket::serve(0, 4);
    let port <int> = srv.local_port();

    // Dial client connection
    let cli <Socket> = Socket::dial("127.0.0.1", port);
    let conn <Socket> = srv.accept_conn();

    let ok <bool> = cli.send_text("ping", 4);
    let back <[char]> = conn.recv_bytes(4);

    cli.shutdown();
    conn.shutdown();
    srv.shutdown();
}
```

The `Socket` struct wraps POSIX socket handles with safe methods for streaming text and byte buffers,
accepting inbound connections, and querying allocated local ports.

## `testing`

Assertion library and test runners matching Python's `unittest` / `pytest`:

```fin
import { assert_true, assert_false, assert_eq_int, assert_ne_int, assert_eq_string, assert_null, assert_not_null } from testing::std;

fun main() <noret> {
    assert_true(1 == 1, "equality holds");
    assert_false(1 == 2, "inequality holds");
    assert_eq_int(42, 42, "values match");
    assert_ne_int(1, 2, "values differ");
    assert_eq_string("fin", "fin", "strings equal");
    assert_null(null, "expected null pointer");
}
```

Uses Fin's native `blame` construct for explicit assertion failure diagnostics and failure attribution.

## `deque`

Double-ended queues, FIFO queues, and LIFO stacks matching Python's `collections.deque` and C++'s `<deque>`, `<queue>`, and `<stack>`:

```fin
import { Deque, Queue, Stack } from deque::std;

fun main() <noret> {
    // Double-ended queue with amortized O(1) push/pop at both ends
    let d <Deque<int>> = Deque::<int>{};
    d.push_back(10);
    d.push_front(20);
    let front <int> = d.pop_front(); // 20
    let back <int> = d.pop_back();   // 10

    // FIFO Queue
    let q <Queue<string>> = Queue::<string>{};
    q.push("first");
    q.push("second");
    let next <string> = q.pop(); // "first"

    // LIFO Stack
    let s <Stack<int>> = Stack::<int>{};
    s.push(1);
    s.push(2);
    let top <int> = s.pop(); // 2
}
```

Built on an internal dynamic circular buffer that grows automatically when capacity is exhausted.

## `algorithm`

Array manipulation and search algorithms matching C++'s `<algorithm>` and Python's `bisect` / `sorted`:

```fin
import {
    sort_ints,
    binary_search_ints,
    lower_bound_ints,
    upper_bound_ints,
    reverse_ints,
    min_element_ints,
    max_element_ints,
    is_sorted_ints
} from algorithm::std;

fun main() <noret> {
    let arr <[int]> = [5, 2, 9, 1, 7, 3];

    let min_i <int> = min_element_ints(arr, 6); // 3 (element 1)
    let max_i <int> = max_element_ints(arr, 6); // 2 (element 9)

    // In-place sort (hybrid 3-way quicksort + insertion sort)
    sort_ints(arr, 6); // [1, 2, 3, 5, 7, 9]

    let sorted <bool> = is_sorted_ints(arr, 6); // true

    // Logarithmic binary search
    let idx <int> = binary_search_ints(arr, 6, 7); // 4 (or -1 if absent)
    let lb <int> = lower_bound_ints(arr, 6, 6);    // index where arr[i] >= 6
}
```

## `bits`

Low-level bit manipulation matching C++20 `<bit>` and Python bitwise utilities:

```fin
import {
    bit_and,
    bit_or,
    bit_xor,
    bit_not,
    get_bit,
    set_bit,
    clear_bit,
    toggle_bit,
    popcount,
    bit_length,
    count_leading_zeros,
    count_trailing_zeros,
    is_power_of_two,
    rotate_left,
    rotate_right
} from bits::std;

fun main() <noret> {
    let a <uint> = cast<uint>(12); // 1100 in binary
    let b <uint> = cast<uint>(10); // 1010 in binary

    let wand <uint> = bit_and(a, b); // 8  (1000)
    let wor <uint> = bit_or(a, b);   // 14 (1110)
    let wxor <uint> = bit_xor(a, b); // 6  (0110)

    let count <int> = popcount(a);             // 2 set bits
    let blen <int> = bit_length(a);           // 4 bits
    let clz <int> = count_leading_zeros(a);   // 28
    let ctz <int> = count_trailing_zeros(a);  // 2
    let p2 <bool> = is_power_of_two(cast<uint>(16)); // true

    let rot <uint> = rotate_left(cast<uint>(1), cast<uint>(3)); // 8
}
```

## `token`

Lexical token definitions and source spans for Fin compiler self-hosting and tooling:

```fin
import {
    SourceLocation,
    Span,
    Token,
    token_name,
    lookup_keyword,
    is_keyword,
    is_literal,
    is_operator,
    tok_identifier,
    tok_lit_int,
    kw_let,
    kw_fun
} from token::std;

fun main() <noret> {
    let loc <SourceLocation> = SourceLocation{ line: 1, column: 5, offset: 4 };
    let span <Span> = Span{ start: loc, end: loc };
    let tok <Token> = Token{
        kind: tok_identifier(),
        lexeme: "count",
        span: span,
        int_val: 0
    };

    let kw <int> = lookup_keyword("let"); // returns kw_let()
    let is_kw <bool> = is_keyword(kw);    // true
    let name <string> = token_name(tok.kind); // "IDENTIFIER"
}
```

## `diag`

Compiler diagnostics, severity levels, and terminal diagnostics reporting with snippet formatting:

```fin
import {
    Diagnostic,
    DiagnosticBag,
    new_diagnostic_bag,
    format_simple,
    format_snippet,
    diag_error,
    diag_warning
} from diag::std;

fun main() <noret> {
    let bag <DiagnosticBag> = new_diagnostic_bag();
    bag.report(diag_error(), "expected ';' after declaration", "sample.fin", 1, 15);

    let has_errs <bool> = bag.has_errors(); // true
    let count <int> = bag.error_count;      // 1

    // Terminal snippet with line number and caret underline
    let src <string> = "let x <int> = 42\n";
    let formatted <string> = format_snippet(bag.items[0], src);
}
```

## `scanner`

Character cursor and lexical scanner tokenizing Fin source code:

```fin
import {
    Scanner,
    new_scanner,
    free_scanner
} from scanner::std;
import { Token, tok_eof } from token::std;

fun main() <noret> {
    let s <Scanner> = new_scanner("main.fin", "let answer <int> = 42;");
    let tok <Token> = s.next_token();
    while (tok.kind != tok_eof()) {
        tok = s.next_token();
    }
    free_scanner(s);
}
```

## `parse`

Parser stream and operator precedence tables for Pratt parsing:

```fin
import {
    TokenStream,
    new_token_stream,
    free_token_stream,
    binary_precedence,
    unary_precedence,
    is_right_associative,
    prec_assignment,
    prec_equality
} from parse::std;
import { tok_plus, tok_equal } from token::std;

fun main() <noret> {
    let p_add <int> = binary_precedence(tok_plus());
    let p_assign <int> = binary_precedence(tok_equal());
    let r_assoc <bool> = is_right_associative(tok_equal()); // true
}
```

## `ast`

Abstract syntax tree nodes and construction helpers for compiler frontends:

```fin
import {
    AstNode,
    make_ident,
    make_literal_int,
    make_binary,
    make_var_decl,
    free_ast,
    ast_binary_expr
} from ast::std;
import { tok_plus } from token::std;

fun main() <noret> {
    let left <AstNode*> = make_literal_int(40);
    let right <AstNode*> = make_literal_int(2);
    let add_expr <AstNode*> = make_binary(tok_plus(), left, right);
    let var_decl <AstNode*> = make_var_decl("total", "int", add_expr);

    free_ast(var_decl);
}
```

## `scope`

Lexical scopes, symbol tables, and variable resolution:

```fin
import {
    Scope,
    Symbol,
    new_scope,
    free_scope,
    make_var_sym,
    make_fn_sym
} from scope::std;

fun main() <noret> {
    let global <&Scope> = new_scope(null);
    let v_glob <Symbol> = make_var_sym("answer", "int", true);
    global.insert_symbol(v_glob);

    let local_sc <&Scope> = new_scope(global);
    let v_loc <Symbol> = make_var_sym("i", "int", false);
    local_sc.insert_symbol(v_loc);

    let has_ans <bool> = local_sc.has("answer"); // true (resolved from parent)
    let has_i <bool> = local_sc.has_local("i");    // true
    let depth <int> = local_sc.depth();          // 1

    free_scope(local_sc);
    free_scope(global);
}
```

## `typesys`

Semantic type representations, type equivalence, and assignability checking:

```fin
import {
    Type,
    type_int,
    type_int64,
    type_string,
    make_pointer_type,
    make_dyn_array_type,
    type_equals,
    is_assignable,
    free_type
} from typesys::std;

fun main() <noret> {
    let t_int <&Type> = type_int();
    let t_ptr <&Type> = make_pointer_type(t_int);
    let t_arr <&Type> = make_dyn_array_type(t_int);

    let is_num <bool> = t_int.is_numeric();     // true
    let eq <bool> = type_equals(t_int, t_int);  // true
    let widen <bool> = is_assignable(type_int64(), t_int); // true

    free_type(t_arr);
    free_type(t_ptr);
    free_type(t_int);
}
```

## `ir`

Three-address code intermediate representation for compiler backends:

```fin
import {
    IrFunction,
    IrInstruction,
    new_ir_function,
    free_ir_function,
    make_ir_const_int,
    make_ir_binop,
    make_ir_ret,
    ir_add
} from ir::std;

fun main() <noret> {
    let func <IrFunction> = new_ir_function("add_numbers", "int");
    let b <int> = func.add_block("entry");

    let t0 <string> = func.fresh_temp(); // "%t0"
    let t1 <string> = func.fresh_temp(); // "%t1"
    let t2 <string> = func.fresh_temp(); // "%t2"

    func.blocks[b].emit(make_ir_const_int(t0, 40));
    func.blocks[b].emit(make_ir_const_int(t1, 2));
    func.blocks[b].emit(make_ir_binop(ir_add(), t2, t0, t1));
    func.blocks[b].emit(make_ir_ret(t2));

    free_ir_function(func);
}
```

## `emitter`

Stand-alone ANSI C/C99 source code generator module. Note that per ADR 0043, Fin's self-hosting
compiler lowers ASTs directly to in-memory LLVM IR via LLVM-C (`finc/llvm.fin`), refusing intermediate
C transpilation in order to maintain backend integrity and avoid external C compiler toolchain dependencies:

```fin
import {
    CEmitter,
    new_c_emitter,
    free_c_emitter,
    map_fin_type_to_c
} from emitter::std;

fun main() <noret> {
    let em <CEmitter> = new_c_emitter();
    em.emit_prologue();

    let c_ty <string> = map_fin_type_to_c("int");
    em.emit_fn_header(c_ty, "main", "");
    em.emit_var_decl("int32_t", "total", "40 + 2");
    em.emit_return("total");
    em.emit_fn_footer();

    let code <string> = em.get_code();
    free_c_emitter(em);
}
```

## `printer`

AST pretty-printer and canonical Fin source code formatter:

```fin
import {
    format_expr,
    format_stmt
} from printer::std;
import {
    AstNode,
    make_ident,
    make_literal_int,
    make_binary,
    make_var_decl,
    free_ast
} from ast::std;

fun main() <noret> {
    let id <&AstNode> = make_ident("x", 1, 1);
    let lit <&AstNode> = make_literal_int(10, 1, 5);
    let bin <&AstNode> = make_binary("+", id, lit, 1, 3);
    let var <&AstNode> = make_var_decl("total", "int", bin, 1, 1);

    let fin_code <string> = format_stmt(var); // "let total <int> = (x + 10);\n"
    free_ast(var);
}
```

## `buffer`

Dynamic byte buffer and high-performance string builder:

```fin
import {
    Buffer,
    new_buffer,
    new_string_builder,
    free_buffer,
    free_string
} from buffer::std;

fun main() <noret> {
    let b <Buffer> = new_string_builder(64);
    b.write_string("answer = ");
    b.write_int(42);
    b.write_line(";");

    let s <string> = b.to_string(); // "answer = 42;\n"
    free_string(s);
    free_buffer(b);
}
```

## `process`

Process execution, pipeline commands, and subprocess output capture:

```fin
import {
    ProcessResult,
    run_command,
    exec_command,
    is_success,
    free_process_result
} from process::std;

fun main() <noret> {
    // Synchronous exit code
    let code <int> = run_command("clang -v");

    // Output capture
    let res <ProcessResult> = exec_command("uname -s");
    if (is_success(res)) {
        // res.output contains "Linux\n"
    }
    free_process_result(res);
}
```

## `argparse`

Command-line argument and flag parser for compiler drivers and CLIs:

```fin
import {
    ArgParser,
    new_arg_parser,
    free_arg_parser
} from argparse::std;

fun main() <noret> {
    let p <ArgParser> = new_arg_parser("finc", "Fin self-hosting compiler");
    p.add_flag("-c", "-c", "Compile and assemble, but do not link");
    p.add_flag("-v", "-v", "Verbose output");
    p.add_option("-o", "-o", "a.out", "Output file");

    let args <[string]> = ["-c", "-o", "out.o", "main.fin"];
    let ok <bool> = p.parse(args, 4);

    let is_compile_only <bool> = p.get_flag("-c");     // true
    let out_file <string> = p.get_option("-o");        // "out.o"
    let source_file <string> = p.get_positional(0);    // "main.fin"

    free_arg_parser(p);
}
```

## Reading the library as documentation

Two habits are worth adopting.

First, `lib/std/<module>.fin` opens with a comment naming the sample that specifies it and
listing every divergence with its reason. That comment is where "why does the library spell
it this way" is answered.

Second, `tests/samples/stdlib/*.fin` holds the *drafts* — the fuller designs the shipped
modules are cut down from. They are the design intent, not the current behaviour. When the two
disagree, `lib/std` is what the compiler will accept.

Every module in `lib/std` type-checks clean standalone (`finc lib/std/<module>.fin`).
