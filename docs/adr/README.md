# ADR index

ADRs are historical rulings: the decision prose below each file's H1 is never rewritten, and each file opens with a `**Status:**` line (Accepted / Superseded by NNNN / Proposed, plus Date where the file already kept one).
Facts that go stale under a standing ruling are refreshed only by a dated addendum — a `> YYYY-MM-DD status: …` blockquote right under the Status line — never by editing the ruling text.
The language spec is `tests/samples/*.fin` read per its `//@` expectation (ADR 0008); the application how-to is `docs/agent-guide.md`.
Live pointers: `docs/reflect-derive-plan.md` (§6 status) owns the reflect/derive chain state, and `docs/compiler-api.md` is the wave-4 design behind Proposed ADR 0042 (implemented ops: `src/semantics/CompilerApi.cpp`, `src/semantics/impl/Analyzer_CompilerApi.cpp`).
Numbering: ADR 0024 is deliberately skipped and reserved (`docs/HANDOFF.md` §8, recorded inside ADR 0025).

## 2026-08 — corpus and first compiler rulings (0001–0027)

| # | Ruling | Status | What it rules |
|---|--------|--------|---------------|
| 0001 | `m1778` means "not implemented" | Accepted; addendum: shipped (0026fe5) | `blame m1778;` is solely the blame expression; the token's old loop form is gone |
| 0002 | Fresh LLVM backend | Accepted | The backend is written fresh, not ported from `pyprototype` |
| 0003 | Memory management is a library | Accepted | Ownership/GC strategies are Fin libraries over compiler components, not built in |
| 0004 | NodeKind + structural walk | Accepted | AST carries a kind discriminator; `StructuralWalk` gives default child traversal beside the `Visitor` |
| 0005 | `cond : then ? otherwise` | Accepted | The conditional arm order — `?` is the denullifier, so C order would be ambiguous |
| 0006 | Comptime is a tree-walking interpreter | Accepted | `@special` executes in the analyzer; folding or a JIT would break ADR 0003 commitments |
| 0007 | Handlers read broadly, write narrowly | Accepted | Event handlers read anything, write only injected code at the event point, in a determined order |
| 0008 | Sample authority per expectation | Accepted | `//@ ok`/`error`/`unimplemented` decide fault, not file labels; no expectation = harness fault |
| 0009 | `finc` has a machine contract | Accepted; addendum: pinned (`kFincContractVersion = 3`) | Exit codes, stream discipline and JSON diagnostics are a stable interface for `finn` |
| 0010 | One LLVM major, proven by CI | Accepted; addendum: major now 22 (19b465c) | A single LLVM major per platform, built and tested by one CI matrix |
| 0011 | Bare brace opens a scope | Accepted | `{` in statement position is always a block; no lookahead disambiguation |
| 0012 | Grant vs operations namespaces | Accepted | `compiler.components.X` is about a component; `compiler.X` is through one |
| 0013 | `pyprototype` is not evidence | Accepted | The prototype may be read for ideas, never cited as authority; samples win |
| 0014 | Events, protocols, providers differ | Accepted | Three extension mechanisms, told apart by what the library may produce |
| 0015 | Layout is two moments | Accepted | decide vs observe moments; a wrong-moment query refuses, never returns zero |
| 0016 | Destructors compose | Accepted | A field's destructor runs from its parent's; the parent is compiler-generated when absent |
| 0017 | No branching on the host | Accepted | Tainted compile-time host values may be read, never branched on |
| 0018 | Constraint set is a bound | Accepted | `int \| uint \| …` declares admissible types, never a storage type |
| 0019 | Interface reference = two words | Accepted | `{data, vtable}` runtime object; collector pointer map has three states per word |
| 0020 | Injected identifiers are fresh | Accepted | Handlers mint fresh ids and spell only module-qualified paths |
| 0021 | `#[global]` needs no import | Accepted | Only `std` may mark `#[global]` (`printf`, `format!`); elsewhere it diagnoses |
| 0022 | Wider integers convert implicitly | Accepted | Implicit widening, never narrowing; subscripts, comparisons and arithmetic widen |
| 0023 | Macro returns one quoted expression | Accepted | Named params, `$name`, one `quote`; the call bracket shapes the argument |
| 0025 | Dynamic array = ptr + len | Accepted | `[T]` is `{ptr, len}`, owns its heap memory, has no capacity field |
| 0026 | Class = struct with base; `try` is a scope | Accepted | Classes are value types; only `class` may name a base |
| 0027 | Interface vtable carries field offsets | Accepted | Per-`(struct, interface)` vtable: field offsets first, then method pointers |

## 2026-09 — wave-3 semantics and stdlib (0028–0043)

| # | Ruling | Status | What it rules |
|---|--------|--------|---------------|
| 0028 | Prototypes are inference-first maps | Accepted | `{K, V}` structural dictionaries: literal infers types, insertion-ordered, stdlib-backed |
| 0029 | Diamond base shared once | Accepted | Shared ancestor bytes at offset 0; unrelated second base still refuses |
| 0030 | Destructors run at scope exit | Accepted | Implicit calls at every scope exit; fields after the body (per ADR 0016) |
| 0031 | Link LLVM components on MSVC | Accepted | Windows links component static libs; the component list is closed on purpose |
| 0032 | Imported templates reach the backend | Accepted | Whole-program source visibility via the module loader's registry; emission stays root-only |
| 0033 | `#[export]` is visibility only | Accepted | Import-visibility flag; emission keeps `linkonce_odr` so two-object links merge |
| 0034 | `any` maps as an opaque blob | Accepted | `{i8*, i64}` for layout/signatures only; all value operations still refuse (held: value semantics per amendment) |
| 0035 | Meta-types map to opaque words | Accepted | `$type`-family are named words; `resolve_type*` fold at compile time to the static type |
| 0036 | Equality is declared, not synthesized | Accepted | Struct `==` is declared-only (`operator ==` / `Equal`); no field-wise synthesis — held ruling |
| 0037 | `Ok(T)` is the member | Accepted; representation settled by 0041 | Enum comparison `Ok(T)` denotes the member; tagged-union lowering reads its discriminant |
| 0038 | Bounded any narrows | Accepted | `Any<Printable>` resolves calls against the bound (see ADR 0034) |
| 0039 | `release()` drops one reference | Accepted | Decrement and free at zero; outstanding handles keep working; Dispose refused |
| 0040 | Nullable values are tagged discriminants | Accepted | Pointers use the null sentinel; value types lower as `{T, bool}` presence tag |
| 0041 | Enums are tagged unions | Accepted | 32-bit tag, payload buffer max-sized max-aligned, positional member access |
| 0042 | `@`-call builtins route through components | **Proposed — open** | `@implements`/`@defined` fold, `@Alloc`/`@Free` lower via `compiler.components.*` |
| 0043 | Self-hosting lowers to LLVM IR | Accepted, refusing C generation | Bootstrap emits IR, never C transpilation |

## 2026-10 — runtime semantics, stdlib hardening, reflect/derive (0044–0059)

| # | Ruling | Status | What it rules |
|---|--------|--------|---------------|
| 0044 | Provider called once per subject | Accepted | Exclusive, memoised `#[provides(...)]`; wave-4 slot `type_metadata` via `pointer_map_quote` |
| 0045 | `restrict` is a readonly snapshot | Accepted | A heap sibling built by `alias()` (`owned=false`), not `&self` |
| 0046 | `Offer<T, E>` is an erasure-union alias | Accepted | Check-time union alias; no runtime tag; member access is the positional payload rule |
| 0047 | fin-guard rewrites bare `let`/`const` | Accepted | On by default: rewritten to `<auto>` with one warning per site (exit 0) |
| 0048 | Module body calls its own function | Accepted | Same-name free functions resolve to the calling module, whatever the import order |
| 0049 | String equality compares content | Accepted | `==`, map keys and `.find` all compare content; `hash_of` keeps its documented pointer contract |
| 0050 | Pattern matching over existing tags | Accepted; addendum: shipped stage 1 (19ffc10) | `match (scrutinee)` with `=>` arms reusing existing spellings; no new sigil |
| 0051 | Target triple pinned to the host | Accepted; addendum: native ABI selectors | Foreign `--target` refused; predefined `#c_ifdef` names select the native OS and architecture in both preprocessors |
| 0052 | Uninitialized storage is zero-filled | Accepted | Every uninitialised alloca reads zeros; the policy is pinned by tests |
| 0053 | A `blame` raises error-like values | Accepted | Raises `string`, an `Error`-implementing value, or `any`/object |
| 0054 | Signed overflow wraps | Accepted | Plain two's-complement `+ - *` and negation; no traps, no flags |
| 0055 | Returned references outlive the frame | Accepted | A returned reference must name storage that survives the call |
| 0056 | Escaping references tracked interprocedurally | Accepted | Literals, stores and forwarding calls get provenance checks beside ADR 0055's operand rule |
| 0057 | Reflected intrinsics fold from own facts | Accepted; addendum: shipped (47ceb96) | `name_of`/`size_of`/`field_count`/`offset_of` runtime-compute from the compiler's own facts; `fields_of`/FieldInfo blocked |
| 0058 | derive_hash walks field values | Accepted; addenda: shipped (300458f), platform-stable width | FNV-1a 64 over field values in declaration order, identical across target ABIs; `derive_eq`/`derive_encode` blocked |
| 0059 | `enum_from!` answers the member name | Accepted; addendum: shipped (cdc9331) | Runtime value→member-name dispatch for enums; the member ops' missing leg |

---

Issue ledger worth remembering: issue #53 (reflection + derives) is CLOSED — ledger comment 6280766517 confirms scopes A+B shipped (ADRs 0057–0059); issue #114 (json/serde) CLOSED, landed at 4a8beda. Standard-library packaging refs today: `M1778/fin-sdk` 3.2.0, `M1778/Linf` v0.
