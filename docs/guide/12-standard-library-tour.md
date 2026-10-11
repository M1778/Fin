# 12. Standard library reference

The shipped library is [lib/std](../../lib/std/), not the design drafts under
`tests/samples/stdlib`. Import spelling is `from <module>::std`. All ordinary
library names need an import; `printf` is ambient and `format!` is a compiler builtin.

**Importable does not mean executable.** The loader publishes signatures and
retains generic templates, and many ordinary imported Fin function bodies now
reach the backend: `len`, `split_str`, `replace`, `Deque` get/set, `stem`,
`normalize`, `print_bool`, and `copy_file` all build and run. Other paths are
still refused, so resolving a signature does not by itself make a call
executable. The examples below mark those boundaries.

## Choose a module

| Module | Main declarations | Application boundary |
| --- | --- | --- |
| [stdio](../../lib/std/stdio.fin) | `printf`, `Printable`, printing helpers, `Stream`, `File`, `IOResult<T>` | Ambient `printf` runs; concrete-typed helpers such as `println_str` and `print_bool` build and run; other helper calls and payload results have backend limits |
| [strings](../../lib/std/strings.fin) | Length, byte comparison, search, transforms, splitting/joining | `len`, `split_str`, `replace`, and other imports build and run; allocating calls return owned storage requiring caller cleanup |
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
| [time](../../lib/std/time.fin) | `now`, `sleep_sec`/`sleep_ms`, `diff_sec`, `Instant`, `Duration`, `format_rfc3339`/`parse_rfc3339` | `Instant` is a monotonic ns clock (Linux id 1, macOS id 6); RFC3339 is UTC `YYYY-MM-DDTHH:MM:SSZ` with caller-owned storage freed by `free_rfc3339`; Windows has no `clock_gettime` |
| [ascii](../../lib/std/ascii.fin) | `is_alpha`, `is_digit`, classification predicates, `to_upper`/`to_lower`, `digit_value`/`hex_value` | Pure predicates build and run (`is_alpha('a')`, `is_digit('3')` probed live); no allocation, no cleanup |
| [bits](../../lib/std/bits.fin) | `bit_and`/`bit_or`/`bit_xor`/`bit_not`, get/set/clear/toggle, `popcount`, `bit_length`, leading/trailing zeros, `is_power_of_two`, rotations | Arithmetic lowering builds and runs (`bit_and(6, 3) == 2`, `popcount(7) == 3` probed live); the grammar has no `&`/`|`/`^` operators, so use these helpers |
| [encoding](../../lib/std/encoding.fin) | `hex_encode`/`hex_decode`, `base64_encode`/`base64_decode`, `free_encoded` | Hex and Base64 round-trips build and run (`hex_encode("Hi") == "4869"`, `base64_encode("Hi") == "SGk="` probed live); results are caller-owned, release with `free_encoded` |
| [hash](../../lib/std/hash.fin) | `crc32`, `fnv1a_32`/`fnv1a_64`, `djb2`, `sdbm` | String hashes build and run (`fnv1a_32("Fin")`, `djb2("Fin")` probed live); pure values, no cleanup |
| [random](../../lib/std/random.fin) | `seed`, `seed_now`, `rand`, `random_int`, `random_float`, `random_bool` | `seed` with `random_int`/`random_bool` builds and runs; bare `rand()` is refused in codegen (`signature mismatch or duplicate definition for symbol 'rand' is not lowered yet`) because the name collides with C `rand` |
| [algorithm](../../lib/std/algorithm.fin) | `sort_ints`, `binary_search_ints`, lower/upper bounds, min/max, `is_sorted_ints`, fill/copy/reverse | Sorting and search over `[int]` build and run (sorted `[3, 1, 4, 2]`, `binary_search` hit probed live); the caller owns the array, `delete` it |
| [buffer](../../lib/std/buffer.fin) | `Buffer`, `new_buffer`/`new_string_builder`, `free_buffer`/`free_string`, `write_*`, `length`, `to_string` | `new_buffer` with `write_string` and `length` builds and runs; the buffer owns heap storage, release with `free_buffer` |
| [deque](../../lib/std/deque.fin) | `Deque<T>`, `Queue<T>`, `Stack<T>`, push/pop at both ends, `get`, `len`, `is_empty` | `push_back`/`push_front`/`pop_back`/`pop_front`/`get` build and run (int deque probed live); no buffer destructor, process exit reclaims demo storage |
| [path](../../lib/std/path.fin) | `join`, `basename`, `dirname`, `extname`, `stem`, `normalize`, `is_absolute`/`is_relative`, `free_path` | `join`/`basename`/`stem`/`normalize` build and run (`normalize("/a/./b/../c") == "/a/c"` probed live); results are caller-owned, release with `free_path` |
| [env](../../lib/std/env.fin) | `getenv`/`setenv` externals, `get_env`, `has_env`, `set_env`, `unset_env` | `set_env(name, value)`, `get_env`, and `has_env` build and run (two-argument `set_env` probed live); consumed by `finc/driver` |
| [process](../../lib/std/process.fin) | `run_command`, `exec_command`, `ProcessResult`, `is_success`, `free_process_result` | `run_command("true") == 0` and `exec_command("echo hi")` build and run; release results with `free_process_result`; consumed by `finc/driver` |
| [testing](../../lib/std/testing.fin) | `assert_true`/`assert_false`, `assert_eq/ne_*`, `assert_null`/`assert_not_null` | Assertions build and run (`assert_eq_int(1 + 1, 2, ...)` probed live); failures blame, they do not raise catchable errors |
| [token](../../lib/std/token.fin) | `tok_*()`, `kw_*()`, `is_keyword`, `token_name`, `Token` | Kind helpers and `Token` values build and run (`tok_ident() == 2`, `token_name` probed live); use the `tok_*()`/`kw_*()` functions because the `TOKEN_*`/`KW_*` consts resolve but are refused in codegen (`the name 'TOKEN_EOF' is not lowered yet`); consumed by `finc/driver` and `finc/parser` |
| [scanner](../../lib/std/scanner.fin) | `Scanner`, `new_scanner`, `free_scanner`, `peek`/`peek_next`/`advance` | `new_scanner` with `peek` builds and runs (`peek("let x = 1;") == 'l'` probed live); release with `free_scanner`; consumed by `finc/driver` |
| [parse](../../lib/std/parse.fin) | `prec_*()`, `binary_precedence`, `unary_precedence`, `is_right_associative` | Precedence helpers build and run; consumed by `finc/parser` |
| [ast](../../lib/std/ast.fin) | `ast_*()`, `node_kind_name`, `AstNode`, `make_*` makers, `free_ast` | Kind helpers, `make_literal_int`, and `free_ast` build and run; consumed by `finc/driver`, `finc/parser`, `finc/checker`, and `finc/codegen` |
| [printer](../../lib/std/printer.fin) | `op_symbol`, `AstPrinter`, `new_ast_printer`, `free_ast_printer`, `format_expr`, `format_stmt` | `format_expr` over a made literal builds and runs (`41` round-trips probed live); `format_stmt` is consumed by `finc/driver` |
| [scope](../../lib/std/scope.fin) | `sym_*()`, `symbol_kind_name`, `Symbol`, `Scope`, `new_scope`, `free_scope`, `insert_symbol`, `lookup`, `has`, `depth` | `new_scope` with `insert_symbol`, `has`, and `depth` builds and runs; release with `free_scope`; consumed by `finc/checker` |
| [typesys](../../lib/std/typesys.fin) | `type_kind_*()`, `Type`, `make_primitive_type`, pointer/array/struct makers, `free_type` | `make_primitive_type(type_kind_int(), ...)` with `is_numeric` builds and runs; release with `free_type`; no `finc/` consumer yet |
| [diag](../../lib/std/diag.fin) | `diag_*()`, `level_name`, `Diagnostic`, `DiagnosticBag`, `format_*`, `format_snippet`, `free_diag_str` | `level_name(diag_error()) == "error"` and `format_error` build and run; release formatted strings with `free_diag_str`; consumed by `finc/parser` and `finc/checker` |
| [emitter](../../lib/std/emitter.fin) | `CEmitter`, `new_c_emitter`, `free_c_emitter`, `emit_*` | `new_c_emitter` with `emit_prologue` and `emit_fn_header` builds and runs; always build via `new_c_emitter` and release with `free_c_emitter`; no `finc/` consumer yet |
| [ir](../../lib/std/ir.fin) | `ir_*()` opcodes, `ir_opcode_name`, `IrInstruction`, `IrBlock`, `IrFunction`, `make_ir_*`, `new_ir_function`, `free_ir_function` | Opcode helpers with `make_ir_const_int`/`make_ir_binop` build and run; no `finc/` consumer yet |
| [argparse](../../lib/std/argparse.fin) | `FlagDef`, `OptionDef`, `ArgParser`, `new/free_arg_parser`, `add_flag`/`add_option`, `parse`, `get_flag`/`get_option`, positionals | `new_arg_parser` with `add_flag`, `parse`, and `get_flag` builds and runs; flag spelling is literal (`verbose`, not `--verbose`) and unmatched dash-args fail `parse`; release with `free_arg_parser` |
| [fs](../../lib/std/fs.fin) | `file_exists`, `remove_file`, `file_size`, `read_to_string`, `write_string`, `append_string`, `free_file_content` | `write_string` with `read_to_string` and `remove_file` builds and runs (round-trip probed live); `read_to_string` is nullable, denullify with `?`; release with `free_file_content`; consumed by `finc/driver` and `finc/checker`; deep proof owned by #61 |

Self-host relevance: `finc/driver` consumes `token`, `scanner`, `ast`, `printer`,
`buffer`, `fs`, `process`, and `env`; `finc/parser` consumes `token`, `parse`,
`ast`, and `diag`; `finc/checker` consumes `ast`, `scope`, `diag`, `buffer`, and
`fs`; `finc/codegen` consumes `ast`. `emitter`, `ir`, and `typesys` have no
`finc/` consumer yet and are future self-host material (ties to #54).

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
`eprintln_str`. `println_str` builds and runs, as do the concrete-typed helpers
`print_bool`/`println_bool`,
`eprint_int`/`eprintln_int`, and `print_double`/`println_double`, each with an
`eprint_*`/`eprintln_*` stderr half, build and run. `println("hello")` is not a
universal print builtin.

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
`to_upper`, `to_lower`, `split`, `split_str`, `replace`, `join`, `to_chars`,
`from_chars`, and `free_str`.
Search functions return indices or `-1`; `equals` compares bytes. `split_str`
splits on a whole separator string, without overlapping, and `replace` rewrites
every non-overlapping occurrence; `join(split_str(x, s), s)` gives `x` back, the
same round-trip `split` documents for its character form. Both build and run.
Both `strings::std` and `path::std` declare `join` with different signatures;
a call inside one module always resolves to that module's own definition
regardless of import order, so the `join` inside `replace` is the string one
even when `path::std` is imported first.

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

These file APIs are not a verified end-to-end path merely because their
declarations are available: the `copy_file` import builds and runs, but check
any other `File` call by building before relying on it. A direct C
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
