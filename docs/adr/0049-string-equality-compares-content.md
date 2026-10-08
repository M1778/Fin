# String equality compares content

## Context

`==` on two strings was a pointer `ICmpEQ` at runtime
(`src/codegen/CodeGen_LLVM.cpp`, the pointer path of `emitArithmetic`),
while the comptime folder compared decoded bytes
(`src/semantics/ComptimeInterp.cpp`: `left.value.text == right.value.text`).
One spelling, two operations: two spellings of `"hello"` were equal as
literals (pooled to one pointer) and `clone("lit") == "lit"` was false
(distinct pointers). The map-key rule and the `any` rule used `strcmp`
(NUL-terminated), a third spelling of almost the same question. And
`hash_of<T>` (`lib/std/hashmap.fin`) folded the key's address, deliberately:
its header documented pointer semantics as the consistent choice beside a
pointer equality. A map with computed keys missed (`missing` for a stored
`"k"` looked up through `clone("k")`).

## Decision

Owner-first, both compilers; `==` is memcmp semantics over the full bytes.

- **Runtime `==`/`!=`:** true iff same length AND byte-equal. Lowered in one
  helper (`emitStringContentEquality`, CodeGen_LLVM.cpp) used at all three
  string sites: the `emitArithmetic` pointer path (both sides pointers with
  nothing recorded past them), the `emitKeyEquality` string-key case, and
  the string sub-case of `any` equality. Lengths come from `strlen` (a
  `string` is NUL-terminated bytes, so that IS the length) and `memcmp`
  runs over the shorter length with the lengths ANDed in, so unequal
  lengths decide false without reading past either terminator. `!=` is its
  negation. A null guard answers null-valued variables before any `strlen`
  runs (`null` against `null` is true, against bytes false) -- a call
  executes even where its result would be discarded, so only a branch keeps
  `s == t` from becoming a `strlen` of null. `== null` itself still
  compares the pointer, and ordering on any pointer (strings included)
  still refuses as before.
- **Hash:** `hash_of<T>` folds `hash_word::<T>(key)` through the same two
  primes. `hash_word` is a bodiless generic declaration lowered per concrete
  `T` by the backend, on the footing of `keyidof`/`getkeyid`/`resolve_type`
  (a definition wins; otherwise no symbol is emitted and none is needed).
  For a `string` the word is FNV-1a over the `strlen` bytes (offset basis
  2166136261, prime 16777619, 32-bit wrap); for anything else it is exactly
  what `cast<int>(key)` gave before, through the same conversion, so `int`,
  `char`, `bool`, `uint` and struct keys hash exactly as they did. Equal
  strings share buckets; unequal strings sharing one only costs a probe.
- **Why the leaf:** Fin has no overloading, no specialization and no
  static-if, so a string-specific operation written into `hash_of`'s body
  refuses every non-string instantiation at codegen, and `HashMap<int, int>`
  is a working program. The dispatch lives in the backend's per-type
  lowering instead. Both compilers implement the same word rule with the
  same constants, so both partition string keys identically.
- **Stage shape this forces:** with `cast<int>(key)` gone from `hash_of`'s
  body, the body no longer matches the erasure-idiom detector
  (`cg_body_has_erasure_cast`, finc/codegen.fin:5664), so the stage
  monomorphizes per concrete call -- the same per-type point the C++
  backend hooks (`emitTemplateCall`, `emitNamedCall`). Until the stage
  lowers the leaf, its programs link against a symbol nothing defines (the
  documented `keyidof` failure mode); that gap belongs to the stage half.

## Considered Options

- **Status quo (pointer `==`, pointer hash):** consistent, and cheap. Wrong:
  two values that print the same compare unequal, and every computed key
  misses. Keeps the comptime/runtime split, which is the thing being
  removed.
- **Explicit API only (`strings.equals`, content hash opt-in):** no language
  change and no lowering risk. Leaves `==` meaning identity beside a
  comptime `==` meaning bytes, and leaves every `==`-based lookup (map
  keys, `any`) on pointer semantics -- the split survives everywhere the
  spelling is used rather than where it is imported.
- **Change what `cast<int>` gives for strings (content hash as address):**
  the smallest diff -- `hash_of`'s body would not even change. Rejected:
  a cast must reveal representation, not compute a hash, and the C++
  backend would silently disagree with the stage (whose erased
  `cast<int>(key)` stays a word op) on a program no corpus sample covers
  -- exactly the undetectable divergence `test_stage_agreement.cpp` exists
  to prevent.
- **Pure-Fin content hash in `hash_of`'s body (`cast<[char]>` + loop):**
  lowers and runs for `T = string`, and refuses `T = int` at codegen (`this
  conversion (from 'an integer' to 'an array')`), proven by building both
  instantiations. No branch placement avoids it: codegen lowers both arms
  unconditionally. It would work only while no program keys a map by `int`
  -- a landmine in the standard library, forbidden by the `IntMap` lock
  below.
- **Intern all strings to canonical pointers:** makes pointer equality and
  pointer hashing content-correct with no compiler change at all. Rejected:
  it touches every string constructor in `strings.fin`, needs unbounded
  global pool state, and contradicts the ordered approach (this ADR), which
  both halves implement.

## Consequences

- Pins: `Soundness_Codegen.StringEqualityComparesContentNotPointers`,
  `StringMapLookupHitsComputedKeys`, `IntMapLookupStillWorks`
  (tests/test_codegen.cpp; the int map is the lock against the pure-Fin
  landmine), `StringOrderingIsStillRefused` and
  `StringNullComparisonsStillCompareThePointer` (characterizations: ordering
  refuses, null compares by pointer).
- Leftovers, each deliberate: ordering on strings stays refused (a content
  ordering is a second ruling, not this one); `lib/std/strings.fin`'s
  header still documents pointer `==` (outside this change's ownership --
  it is stale, not wrong at rest); embedded NULs compare by `strlen`
  prefix (the IR has no length-tracked string; lengthening the
  representation is ADR 0003's question, not this one's); `any`-boxed null
  strings now compare true instead of crashing in `strcmp` (a crash turned
  into an answer on a path no program can reach without boxing null).
- The stage half mirrors the word rule and the leaf; until it does, stage
  builds of string-map programs refuse (`free generic function 'hash_of' is
  not lowered yet`, the erasure idiom being gone) -- and past that refusal
  would link against a symbol nothing defines, the documented `keyidof`
  failure mode. Both shapes are stage-build refusals for the allowlist with
  the stage owner (test_stage_agreement.cpp, test_opt_agreement.cpp), not a
  C++ regression; both are stale-detected, so the mirror deletes them.
