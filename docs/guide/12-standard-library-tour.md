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
| [outcome](../../lib/std/outcome.fin) | `Outcome<T>`, free-function constructors `Ok<T>`/`Err<T>`, `is_ok`/`is_err`/`unwrap_or`/`expect` | The struct form builds and runs today (`Ok::<int>(80)`, `Err::<int>(msg)`, field reads and methods probed live); bare struct literals only, `new Outcome{...}` refuses; `expect` aborts on an Err and never returns; `message` is caller-owned |
| [enums](../../lib/std/enums.fin) | `Enum`, `EnumType`, `getkeyid`, `keyidof` | Reflection declarations are not general runtime payload dispatch |
| [operators](../../lib/std/operators.fin) | `Add`, `Equal`, `Index`, `IndexAssign`, other operator interfaces | Some declared operators have no expression grammar or use erased results |
| [error](../../lib/std/error.fin) | `Error`, message/code accessors | Error values do not enable catchable exceptions |
| [stdptr](../../lib/std/stdptr.fin) | `rptr<T>`, `wptr<T>`, `OwnershipError` | Incomplete execution/safety paths; no borrow checker |
| [networking](../../lib/std/networking.fin) | `Socket` (dial/serve/accept_conn/send_text/recv_bytes/local_port/shutdown), `TcpStream` (connect/write/read/close), `TcpListener` (bind/accept/local_port/close), `http_get` | connect/listen/accept/read/write/close run on loopback (the bundled sample ends to end plus pins in `Soundness_SyncNet`, echo and refusal and an against-test-peer `http_get`); explicit byte counts on `write`/`read`, failures `blame` and name the call; dotted-quad addresses only -- a hostname refuses as "cannot parse the address as a dotted quad", no TLS, `http_get` caps at 4096 bytes |
| [sync](../../lib/std/sync.fin) | `Thread::spawn`/`join` over pthreads, `Mutex<T>` (create/lock/unlock), `Channel<T>` (create/shared/send/recv) | The bundled sample runs a worker thread and a blocking channel end to end on all three compilers (pins in `Soundness_SyncNet` keep a mutex counter at a deterministic `2000` and channel FIFO/refusal); `recv` answers `Outcome<T>` whose `Err` arm is closed in v0; captures work on the reference compiler only, Windows links refuse with the named `sync_*` symbols, self-deadlock is undetected in v0 |
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
| [log](../../lib/std/log.fin) | `Level` (`Debug`/`Info`/`Warn`/`Error` statics with `.rank`), `Logger`, `init`/`init_json`, `timestamps`, `debug`/`info`/`warn`/`error` | All four levels filter and both modes build and run (`LOG_LEVEL=debug` no-init emit and `init(Level::Error())` silence probed live); `LOG_LEVEL` outside `debug|info|warn|error` blames naming the value; everything goes to stderr and never stdout (pins in `Soundness_Log`); a plain `enum Level` refuses in codegen, so the members are statics (recorded in the module header) |
| [process](../../lib/std/process.fin) | `run_command`, `exec_command`, `ProcessResult`, `is_success`, `free_process_result` | `run_command("true") == 0` and `exec_command("echo hi")` build and run; release results with `free_process_result`; consumed by `finc/driver` |
| [mem](../../lib/std/mem.fin) | `Allocator`, `Arena`, `Pool<T>`, `LibcAlloc`, `NullAlloc`, typed routes `arena_block`/`mem_release`, raw routes `mem_malloc_bytes`/`mem_grow_bytes`/`mem_free_bytes`, `null_alloc` | Byte arena, slot pool, libc wrapper, and typed routes build and run (`arena_block::<int>(&a, 1024)` + `mem_release::<int>`, `arena_bytes` bump, pool recycle probed live); the issue's `arena.alloc::<int>` member spelling is refused (`Method 'alloc' not found in type 'Arena'`) and typed routes plus interface-value dispatch refuse at stage codegen, so all-compiler programs keep to the concrete structs; every handout names its owner (arena slab dies with `destroy()`, fresh blocks are caller-owned, release with `mem_release`); no alignment, no byte-to-typed recast |
| [testing](../../lib/std/testing.fin) | `assert_true`/`assert_false`, `assert_eq/ne_*`, `assert_null`/`assert_not_null`, `check`, `for_all`, `shrink`, `bench` | Assertions build and run (`assert_eq_int(1 + 1, 2, ...)` probed live); failures blame, they do not raise catchable errors; `for_all(seed, n, gen, prop)` runs `gen(seed + i)` for i in 0..n-1, silent while it holds, and blames naming the first failing input plus the minimally failing one, which `shrink` also answers on its own (the smallest failing input in the halving chain -- 100 against a property failing from 8 up names 12, pinned in `Soundness_Testing` and the bundled sample); `bench` refuses honestly ("ns/op needs sub-second timing (issue #59); sec resolution lies about the magnitude") until #59 lands, because a whole-second timing would print a magnitude it does not know; `*_test.fin` programs run under `tools/fintest` (`fintest --file <path>` ctest lanes, seed dir `fintest/`), and the libFuzzer parser harness recipe lives in `fuzz/parse_fuzz.cpp`'s header, Clang-only, #84's |
| [token](../../lib/std/token.fin) | `tok_*()`, `kw_*()`, `is_keyword`, `token_name`, `Token` | Kind helpers and `Token` values build and run (`tok_ident() == 2`, `token_name` probed live); use the `tok_*()`/`kw_*()` functions because the `TOKEN_*`/`KW_*` consts resolve but are refused in codegen (`the name 'TOKEN_EOF' is not lowered yet`); consumed by `finc/driver` and `finc/parser` |
| [scanner](../../lib/std/scanner.fin) | `Scanner`, `new_scanner`, `free_scanner`, `peek`/`peek_next`/`advance` | `new_scanner` with `peek` builds and runs (`peek("let x = 1;") == 'l'` probed live); release with `free_scanner`; consumed by `finc/driver` |
| [parse](../../lib/std/parse.fin) | `prec_*()`, `binary_precedence`, `unary_precedence`, `is_right_associative`, `TokenStream` (`check`/`match_token`/`expect`/`position`/`seek`), the `lit`/`alt`/`many`/`seq` combinators | Precedence helpers build and run; the combinators are consumable spellings over the same stream machinery (`lit` = match, `many` = repeated `alt`, `seq` = all-or-rewind), consumed by `cdk.fin`'s grammar and `finc/parser` |
| [cdk](../../lib/std/cdk.fin) | `cdk_scan`/`cdk_parse`/`cdk_lower`/`cdk_emit_text`/`cdk_eval_ir`, `calc_compile` (`Outcome<string>`), `calc_eval`, `CdkTokens`/`CdkParse`/`CdkGrammar`, `free_cdk_tokens` | The write-a-language-in-200-lines demo: scanner::std's `scan_token` (never re-lexed) -> parse::std's TokenStream + combinators -> ast::std makers -> ir::std instructions (`fresh_temp` dests, `IrBlock::emit`) -> `calc_eval("2 + 3 * 4") == 14` through the interpreter; `calc_compile` answers the emitted text (pin byte-exact in the sample: `%t0 = const_int 2` ... `ret %t4`); refuse arms are `Outcome::Err` naming the parse stage -- never a blame; v0 boundaries recorded in the module header: emit is cdk's own transcription (emitter::std's CEmitter stops at prologue/fn_header), errors are cdk wordings not yet diag::std's jsonl bag, temp-name strings are process-end leaks at demo scale; readers take the IrFunction by value because the stage crash-reads an array field through a struct pointer parameter (scalar reads and stores through the pointer lower fine) |
| [reflect](../../lib/std/reflect.fin) | `@type_name`, `@type_size`, `@field_count`, `@field_offset` -- the reflected intrinsics the module is the home of | Answers are CONSTANTS folded from the facts the compiler lowers with: `@type_size(Point{int, long})` is 16 with `y` at 8 and field count 2, pinned in the sample so a layout-model change breaks it; the argument is a type's name written where a type belongs (the `@implements` contextual reading), a payloadless enum answers as the int its tag is; `@field_offset` takes the field LITERAL (a runtime string cannot name a field at compile time), which is also why `fields_of` waits on a runtime string owner rather than shipping half-built; payloaded enums refuse all four (members, not fields -- the member ops come with `enum_from!`), interfaces refuse (a fat value with no owning layout), a type parameter folds at the call's turbofish on the C++ side and refuses on the stage side (one erased body); ADR 0057 |
| [derive](../../lib/std/derive.fin) | `derive_hash(x)` -- the explicit derive the module is the home of | The call mixes a struct value's fields in declaration order with FNV-1a 64 (`h = 14695981039346656037`; per byte of each field's VALUE, least-significant first, `h = (h ^ byte) * 1099511628211` mod 2^64), so padding never enters and equal field values hash equal under any layout that keeps their order -- which is why the two backends answer the same number even though they lay objects out differently (C++ DataLayout-packed boxes, stage pointer-width field slots); integers ride their two's-complement bytes zero-extended (`int` 4, `long` 8, `bool`/`char` 1), float and double their IEEE bit patterns, a nested struct recurses, an empty struct stays the basis alone, a payloadless enum's tag mixes as the int it is; `Type` binds from the value argument (`derive_hash(p)` with `p` a Point needs no turbofish, the Phase B wrapper route); every kind the walk does not lower refuses naming the field and the kind -- string, array, pointer, nullable, interface, any, union, fn, payloaded enum, deeper than sixteen -- never silently skipping; ADR 0058 |
| [ast](../../lib/std/ast.fin) | `ast_*()`, `node_kind_name`, `AstNode`, `make_*` makers, `free_ast` | Kind helpers, `make_literal_int`, and `free_ast` build and run; consumed by `finc/driver`, `finc/parser`, `finc/checker`, and `finc/codegen` |
| [printer](../../lib/std/printer.fin) | `op_symbol`, `AstPrinter`, `new_ast_printer`, `free_ast_printer`, `format_expr`, `format_stmt` | `format_expr` over a made literal builds and runs (`41` round-trips probed live); `format_stmt` is consumed by `finc/driver` |
| [scope](../../lib/std/scope.fin) | `sym_*()`, `symbol_kind_name`, `Symbol`, `Scope`, `new_scope`, `free_scope`, `insert_symbol`, `lookup`, `has`, `depth` | `new_scope` with `insert_symbol`, `has`, and `depth` builds and runs; release with `free_scope`; consumed by `finc/checker` |
| [typesys](../../lib/std/typesys.fin) | `type_kind_*()`, `Type`, `make_primitive_type`, pointer/array/struct makers, `free_type` | `make_primitive_type(type_kind_int(), ...)` with `is_numeric` builds and runs; release with `free_type`; no `finc/` consumer yet |
| [diag](../../lib/std/diag.fin) | `diag_*()`, `level_name`, `Diagnostic`, `DiagnosticBag`, `format_*`, `format_snippet`, `free_diag_str` | `level_name(diag_error()) == "error"` and `format_error` build and run; release formatted strings with `free_diag_str`; consumed by `finc/parser` and `finc/checker` |
| [emitter](../../lib/std/emitter.fin) | `CEmitter`, `new_c_emitter`, `free_c_emitter`, `emit_*` | `new_c_emitter` with `emit_prologue` and `emit_fn_header` builds and runs; always build via `new_c_emitter` and release with `free_c_emitter`; no `finc/` consumer yet |
| [ir](../../lib/std/ir.fin) | `ir_*()` opcodes, `ir_opcode_name`, `IrInstruction`, `IrBlock`, `IrFunction`, `make_ir_*`, `new_ir_function`, `free_ir_function` | Opcode helpers with `make_ir_const_int`/`make_ir_binop` build and run; no `finc/` consumer yet |
| [argparse](../../lib/std/argparse.fin) | `FlagDef`, `OptionDef`, `ArgParser`, `new/free_arg_parser`, `add_flag`/`add_option`, `parse`, `get_flag`/`get_option`, positionals | `new_arg_parser` with `add_flag`, `parse`, and `get_flag` builds and runs; flag spelling is literal (`verbose`, not `--verbose`) and unmatched dash-args fail `parse`; release with `free_arg_parser` |
| [fs](../../lib/std/fs.fin) | `file_exists`, `remove_file`, `file_size`, `read_to_string`, `write_string`, `append_string`, `free_file_content`, `copy_file`, `create_dir`, `remove_dir`, `rename_path`, `is_dir`, `is_file`, `mk_tmp_dir`, `mkdir_p`, `walk_dir` | Every declared call builds and runs (`Soundness_Fs` pins the tmpdir round trip and the missing-path shapes); `read_to_string` is nullable, denullify with `?`; missing files are routine (null / `-1` / empty walk), the deliberate contrast with networking's diagnostic ruling; release with `free_file_content` (reads), `free_str` (paths, `mk_tmp_dir`, walk entries -- `delete` the array); `mkdir_p` walks `substr` prefixes, and `walk_dir` copies each dirent through `memcpy` into a `[char]` buffer and reads the name at the Linux x86-64 glibc offsets (name at byte 19, `d_reclen` at 16..17) because the library has no pointer arithmetic; consumed by `finc/driver` and `finc/checker` |
| [cli](../../lib/std/cli.fin) | `Arg`, `Command` (`create`/`arg`/`sub`), `Match` (`get`/`is_set`/`positional`), `free_match`, `parse_args()`, `get_env_or`, `exit_with` | The bundled sample parses, defaults, and dispatches a subcommand end to end; help and version print to stdout and `exit(0)` inside `parse_args`; unknown options and missing values answer `Err` naming the token; `parse_args()` takes no argv (it reads `/proc/self/cmdline`), Linux-only, 4 KiB cap; subcommand option scoping is v0-off (the root's option table parses all args); the issue's flat `[Command]` field refuses in codegen, so subs live in `[&Command]` (recorded in the module header) |
| [serde](../../lib/std/serde.fin) | `Encode`, `Decode` interfaces | The contract resolves through the import a tool writes and `Outcome<Self>` answers inside the interface body; runtime dispatch flows through json::std's free `decode_T` spelling because interface-method dispatch does not lower yet (`Derive` is #53's); the bundled sample pins the issue's own round trip and the refusal-is-`Err` rule |
| [json](../../lib/std/json.fin) | `json_encode/decode_int/str/bool/float`, `json_encode/decode_int_array`, `json_encode/decode_intmap/strmap`, `json_escape`, `json_join2/3/4` | All scalar round-trips, the `[int]` pair, and the two prototype maps build and run (sorted key order pinned byte-exact in `Soundness_Json` and the bundled sample); refusals answer `Err` naming the kind; every returned string is fresh, release with `free_str`; v0 is int32 ints, ASCII-only `\u` escapes, no UTF-8 encoding, and the int-array decode caps at 64 elements (all recorded in the module header); the map decode paths answer `Outcome<HashMap<string, T>>` and build and run on the stage compilers too (the earlier stage-allowlist is gone, not pending) |
| [btreemap](../../lib/std/btreemap.fin) | `BTreeMap` (`new_btree_map`, `len`, `insert`, `remove`, `find`, `key_at`, `value_at`, `free_btree_map`) | The ordered map: two parallel arrays kept sorted under the caller's `less <fn(K, K) -> bool>` comparator, so index order IS sorted order in every compiler at every opt level -- the determinism rule codegen iteration needs, and the reason nothing here is a hash; ordering is the caller's (the same function-field-not-bind reasoning HashMap's header records); `insert` answers true for a new key, false when it replaced; the stored words are the caller's and `free_btree_map` frees only the arrays; O(n) insert is honest v0 (this is spine search, not balancing); spell the instantiation on the receiver (`<BTreeMap<string, int>>`), an `<auto>` receiver is refused by the stage checkers (recorded in the module header) |
| [intern](../../lib/std/intern.fin) | `Interner` (`new_interner`, `intern`, `resolve`, `size`, `free_interner`) | Same text, same id: a `[string]` sweep answered by strings::std `compare`, so equality is content, not a pointer word or a hash -- a `concat`-computed copy of a stored text lands on the stored id (the bundled sample pins it); ids are the array index, stable for the interner's lifetime (append-only); O(n) `intern` is honest v0, buckets are a stretch; `intern` stores its own `clone` (free the caller's string), `resolve` answers the interner's string, release everything with `free_interner` and never `free_str` a resolve result |

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

## Threads and channels

`sync::std` is the one place the library puts concurrency: `Thread::spawn(body)`
starts a real OS thread over `pthread_create` (the body is a `fn() => noret`
passed as the start routine; state crosses by capture on the reference
compiler, or through shared storage a caller arranges), and `join` closes the
timing hole. A missed spawn blames rather than returning a broken handle.

The bundled sample `tests/samples/stdlib/sync.fin` runs a worker thread and a
bounded channel end to end: the worker sends through a module-level channel
slot, the main thread blocks on `recv` until the value arrives, `join` closes
the hole, and stdout is exactly `7` on every order of events -- and on all
three compilers, which the corpus harnesses re-check on every sample change.

Cross-thread state needs care the language does not take for you: a stack
pointer stored into a global refuses with the escape rule, so shared structs
are built on the heap (`Channel::shared` initializes the mutex, then stores
the struct); a `Mutex<T>` guards its value behind an explicit `lock`/`unlock`
pair; and `Channel<T>` is bounded and blocking (`send` answers `false` when
full, `recv` waits). Boundaries worth knowing before designing around it:
lambdas that capture are reference-compiler-only until the self-host stage
gains upvalues; a thread that locks its own mutex waits forever -- no
try-lock, no detection in v0; lock metadata is never destroyed; and there is
no thread-safe `delete`.

## Networking

`networking::std` is TCP over IPv4, blocking, through the C library's socket
calls. Dial with `TcpStream::connect(host, port)` -- `host` is a dotted-quad
string, `port` a positive int -- listen with `TcpListener::bind(port,
backlog)`, accept with `accept`, and move bytes with `write`/`read`, both
explicit byte counts like `File`'s. Failures `blame` and name the call:
connection refused, cannot bind, cannot parse the address. `http_get` is the
newcomer's demo of the whole surface: blocking HTTP/1.0 over raw sockets, no
TLS, capped at 4096 bytes.

The bundled sample `tests/samples/stdlib/networking.fin` builds an ephemeral
listener on loopback, dials it, sends four bytes, and echoes them back along
with the kernel-chosen port; `Soundness_SyncNet` runs the same shape plus a
connect-refusal and a full `http_get` against an in-test peer.

```fin fragment
import { TcpStream } from networking::std;
```

## Serialization and JSON

`serde::std` holds the contract (issue #57): `Encode`/`Decode` are interfaces,
the decode side answers `Outcome` and refuses bad documents with an `Err`
naming the kind -- never a `blame`, because the caller's data is user data.
`json::std` is the engine answering the contract today, through the
free-function spelling the issue itself prescribes (`decode_T` per concrete
type), because an `Outcome<Self>`-returning interface method does not dispatch
yet; `Derive` is issue #53's work and serde.fin's header records the gap.

The scalars, the `[int]` array pair, and the two prototype maps
(`{string, int}`, `{string, string}`) round-trip on all three compilers. Map
keys encode in sorted order so the same map answers the same bytes every run,
and every returned string is fresh memory the caller frees with `free_str`:

```fin
import { json_encode_int, json_decode_int } from json::std;
import { free_str } from strings::std;

fun main() <noret> {
    let text <string> = json_encode_int(42);
    printf("%s\n", text);
    let back <auto> = json_decode_int(text);
    printf("%d\n", back.unwrap_or(-1));
    free_str(text);
}
```

```output
42
42
```

Decode limits worth knowing before designing around them: integers are int32
with JSON grammar (no leading zeros, no `+`); `\u` escapes read ASCII only
(`\u00e9` refuses, no UTF-8 encoding in v0); and the `[int]` decode caps at 64
elements while the encoder takes any count. The bundled sample
`tests/samples/stdlib/json.fin` pins every printed byte shape under the
run-agreement harness; `tests/samples/stdlib/serde.fin` pins the contract.

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

## Write a language in a few hundred lines

`cdk::std` (issue #54) is the pitch made real: one import, source string
to object text or one evaluated value, with every noun reused from the
modules this library already ships -- scanner::std's `scan_token` for the
front end, token::std's kind constants, parse::std's `TokenStream` plus
the `lit`/`alt`/`many`/`seq` combinators, ast::std's node makers, and
ir::std's instruction makers. The demo language is `calc`: integers,
`+ - * /`, parentheses. `calc_eval` answers through an IR interpreter, so
precedence comes from the parse and the value from the instructions:

```fin
import { calc_eval, calc_compile } from cdk::std;
import { Outcome } from outcome::std;
import { free_str } from strings::std;

fun main() <noret> {
    printf("%d\n", calc_eval("(2 + 3) * 4"));
    let ok <Outcome<string>> = calc_compile("2 + 3 * 4");
    printf("%s", ok.value);
    free_str(ok.value);
}
```

```output
20
%t0 = const_int 2
%t1 = const_int 3
%t2 = const_int 4
%t3 = mul %t1 %t2
%t4 = add %t0 %t3
ret %t4
```

Refuse shapes are `Outcome::Err` naming the parse stage -- `calc:
expected an expression` for a dangling operator, `calc: expected ')'`
for an unclosed paren, `calc: unexpected input after the expression`
for junk on the tail -- because malformed source is a kit's routine
input, never a blame. The v0 boundaries are in the module header: emit
is cdk's own transcription (emitter::std's CEmitter stops at
prologue/fn_header, the stretch the issue marks), errors are cdk
wordings rather than diag::std's jsonl bag, and `fresh_temp`'s name
strings are demo-scale process-end leaks. The bundled sample
`tests/samples/stdlib/cdk.fin` pins the eval facts, the exact emitted
text, and the Err wordings on all three compilers.
