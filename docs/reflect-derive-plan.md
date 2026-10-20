# Plan: `reflect.fin` + `derive.fin` (issue #53, Scopes A–B)

Status: **proposed, awaiting owner sign-off** — not a ruling. Each numbered Ruling below
becomes its own ADR when ratified; this file is the plan those split out of. Scope C
(the `M1778/fin-sdk` repo) is deliberately out of this plan and stays a separate owner
decision.

Design context: `docs/compiler-api.md` §2.2 — "the interesting work is at run time, not
compile time. Every list-valued operation ships as a **projection**: the compiler performs
the fold and returns a scalar, a `{K,V}` prototype, or a quote that already contains the
whole list as literal syntax." The plan follows that spine.

## 1. Measured substrate (what exists today, with evidence)

| Fact | Evidence |
| --- | --- |
| Component ops exist and **resolve** (signature checks, grants, `<R>` turbofish) but **never evaluate** | comment in `src/semantics/CompilerApi.cpp` ("analyzer resolves these signatures ... and never evaluates them"); `tryLayoutMember` in `Analyzer_CompilerApi.cpp` only sets `lastExprType` |
| Op inventory needed by Scope A is already in the table | `CompilerApi.cpp`: `name_of`/`typeid_of`/`kind_of` (types), `has_field`/`field_count`/`field_type`/`select_field` (structs), `size_of`/`offset_of` (layout) |
| Real sizes/offsets already computed in the analyzer | `src/types/Layout.cpp`; nothing has to be re-derived to evaluate |
| `@`-calls CAN lower to runtime code | `@Alloc` route: analyzer checks at `Analyzer_Expr.cpp:1845`, codegen lowers to `malloc` at `CodeGen_LLVM.cpp:11315` (ADR 0042) |
| Comptime-evaluated type facts exist as a precedent | `resolve_type`/`resolve_arr_type` fold at compile time per ADR 0035 |
| Quotes are data inside handlers (so a helper can hand back a quote body) | `visit(QuoteExpression)`, `Analyzer_Expr.cpp:3834` — "Wave-4 step 19: inside a handler ... a quote is data, not code" |
| `map!`/`coll!` already productized as `@macro` renames | ADR 0023 steps 8–9; `tests/samples/useful_macros.fin` builds green |
| `typeof` is a lexer keyword | `lexer.l:183` — no function may be named `typeof` |

## 2. Scope A — `lib/std/reflect.fin` that RUNS

### Design choice (R2 below is the owner call)

**Route A — builtin evaluation + static materialization (recommended).**
New analyzer-evaluated builtins spelled `@type_of::<Point>()`, `@fields_of::<Point>()`
(runtime-shaped results). Flow:

1. Analyzer: the call resolves `T` through the existing `typeHint`/`typeHintFor`
   mechanism (the same one `@implements` uses for its struct argument — `T` is a type
   *name* here, so Soundness_TypeAsValue already covers the spelling; no meta-type ruling
   is load-bearing).
2. Analyzer evaluates the facts it already owns: name, `size_of`/alignment from the real
   `LayoutEngine`, `field_count`, and per-field name/offset/type. This is the narrow
   evaluation set — a const-fold layer over facts the analyzer holds, exactly the
   `resolve_type` precedent (ADR 0035), not a general comptime interpreter.
3. Codegen lowers each result through the nearest existing path: the name text as an
   ordinary **string literal**, sizes/offsets as **integer constants**, and
   `@fields_of::<T>()` as **unrolled construction** of the array at the call site
   (one `FieldInfo` per field, name literal + offset constant) — comptime-known
   elements of a runtime loop, §2.2's projection form. No new runtime shape is born:
   the compiler never introduces a table, and a runtime loop still works because it
   indexes a real array.
4. `reflect.fin` declares the runtime structs `TypeInfo{name: string, size: long,
   field_count: int}` and `FieldInfo{name: string, offset: long}` plus thin functions
   `type_of<T>()`, `fields_of<T>()` that wrap the builtins, so user code never writes `@`.
5. Sample `tests/samples/stdlib/reflect.fin` proves both on a struct and an enum; every
   op outside the evaluation set keeps the resolve-only behavior and refuses by name.

The new backend capability is exactly one shape: **unrolled comptime-constructed
runtime values** (per-field `FieldInfo` construction at the call site, plus folded
scalars). Everything else is evaluation of facts the analyzer already holds.

**Route B — evaluate the component calls themselves** (`compiler.layout.size_of(t)`
returns a comptime constant at the call site). Costs more: comptime struct values with
runtime strings, a `$type`-word identity decision, and the comptime interpretability line
(no loops — a field walk has to be projection-shaped anyway, so Route A's fold is
required regardless). Route B remains the long-term home of the component API; Route A
does not block it.

### Naming (needs owner nod, cheap)

`typeof` is a keyword (`lexer.l:183`), so the issue's `typeof<T>` spelling cannot be a
function. Proposal: module surface `type_of`/`fields_of`; leave the `typeof` keyword as
is (its own topic, not #53's).

## 3. Scope B — `lib/std/derive.fin`

Split by what is already landed:

- **Already landed**: `map!`/`coll!` — promoted as `@macro` renames (ADR 0023 steps
  8–9, `useful_macros.fin` green). Nothing to do; the issue's "bottling the any-box
  path" is measured as shipped.
- **To build**: `derive_hash<T>()` first (a hash function is an ordinary function, so
  ADR 0036's `==` conflict does not apply), then `enum_from!` (enum value ⇄ name needs
  the same metadata as Scope A composes on), then `derive_eq<T>()`/`derive_encode<T>()`.
- Shape for `derive_eq` under ADR 0036 (R3 ruler): the user *declares*
  `fun op ==(a: &T, b: &T) <bool>` and its body is `"" @derive_eq::<T>(a, b)?""` — hmm,
  concretely: the declaration stays the user's; the explicit call fills the body with a
  field-walked quote. Since quotes are data (step 19), an explicit-call body-fill is a
  candidate to be ruled *not* synthesis; that is R3, not pre-decided here.
- `derive_hash<T>()` needs no ruling: ordinary function synthesis of a field-walked
  mixer over the same metadata as Scope A.

## 4. Owner decisions requested

| # | Question | Recommendation | Blocks |
| --- | --- | --- | --- |
| R1 | `$type` **value** in a turbofish (`types.fin:26`, `//@ unimplemented` sample) | Leave open and untouched — Route A needs only the generic *name* spelling. `types.fin`'s sample stays aspirational. | Nothing in this plan |
| R2 | Route A vs Route B for Scope A; and the new codegen shape's name/shape (unrolled comptime-forwarded construction + folded scalars) | Route A; each fact lowers via the nearest existing literal path | Scope A entirely |
| R3 | Does an explicit-call body-fill of a user-written `op ==` declaration respect ADR 0036 ("declared-only")? | Yes: the declaration is still written by the user; the compiler never manufactures it | `derive_eq`/`derive_encode` |
| R4 | `type_of`/`fields_of` as the public spellings (keyword collision) | Approve rename of the issue's `typeof` | Docs + sample only |

## 5. Sequencing

1. **Phase A1** — analyzer evaluation set + codegen blob emission (`@type_of`,
   `@fields_of`), TDD red→green with failing regression tests first; refusal for every
   op outside the set.
2. **Phase A2** — `reflect.fin` runtime structs + wrappers + sample + tour row + site
   card; corpus lanes byte-exact across finc/stage2/stage3.
3. **Phase B** — `derive_hash` (sample, tour row), then `derive_eq` behind R3, then
   `enum_from!`.
4. Out of scope until a separate owner decision: `M1778/fin-sdk`, any repo-topic edit
   (`#50` idea 0).

Cost estimate: Phase A1 is the real work (analyzer const-fold layer ≈ one
`Analyzer_Expr`-side builtin family + one codegen blob emitter, plus regression tests);
A2 is a productization day like #63/#54/#55; Phase B's `derive_hash` reuses A1's
metadata.

## 6. Status (recorded as of #53's close)

- **Shipped:** A1/A2 as `@type_name`/`@type_size`/`@field_count`/`@field_offset`
  (ADR 0057 — the zero-arg-generic wrapper spellings `type_of::<T>()` cannot
  monomorphize on the stage, so the builtins are the v0 surface; R4's rename is
  therefore moot for now); `derive_hash` (ADR 0058, commit `300458f`);
  `enum_from!` (ADR 0059, commit `cdc9331`).
- **Blocked:** `fields_of` + `FieldInfo` — needs a runtime owner for the field
  name string (ADR 0057 note; `lib/std/reflect.fin` header records it).
- **Un-owned decisions still open:** R1 untouched; R3 remains the gate for
  `derive_eq`/`derive_encode` (explicit derives beyond `derive_hash`); Route B
  never started.
- **Scope C:** `M1778/fin-sdk` scaffolded (contract-3 snapshot + template +
  Linf first-rules proof); full linter and `fin fmt`'s Fin-native first pass
  stay owned by `#50`.

