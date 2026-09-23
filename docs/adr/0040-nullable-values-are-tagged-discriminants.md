# Nullable values are tagged discriminants; nullable pointers use null sentinel

A nullable pointer (`(&T)?`) or nullable function (`fn?`) uses the standard 8-byte
pointer representation where `null` is `0x0`. A nullable value type (`int?`, `float?`,
`struct?`) lowers as a tagged aggregate `{ T, bool }`: a payload of type `T` paired with
a boolean presence flag (`has_value`).

## Considered Options

- **In-band scalar sentinel (`0` is null):** Codified in the draft note on
  `tests/samples/nullifier.fin:4` (`b? <int>`). Rejected because `0` is a valid,
  ubiquitous data value in systems programming (array indices, exit codes, file
  descriptors, counters). Conflating zero with absence prevents distinguishing
  between a present zero and an uninitialized/absent field, leading to silent bugs.
  Furthermore, arbitrary structs have no natural sentinel pattern.
- **Heap pointer indirection (`ptr` to heap `T`):** Allocating every nullable scalar or
  struct on the heap introduces significant allocator overhead, pointer chasing, and
  cache misses for basic value types.
- **Tagged aggregate discriminant `{ T, bool }`:** Selected. Provides a true optional/sum-type
  representation without allocator overhead. Memory layout is contiguous, deterministic,
  and mirrors modern systems languages (Rust `Option<T>`, Swift `Optional<T>`).

## Consequences

- `LayoutEngine` (`src/types/Layout.cpp`) lays out nullable value types as a struct
  containing `inner` followed by `bool`, properly padded to alignment. Nullable pointers
  remain 8 bytes with alignment 8.
- LLVM CodeGen (`src/codegen/CodeGen_LLVM.cpp`) maps `(&T)?` through `TypeMapper::map` as
  an 8-byte pointer with `isNullable = true`.
- For nullable value types, struct instantiation initializes absent fields to `{ 0, false }`.
- Postfix denullify `expr?` branches on `has_value` (or `ptr != null`), invoking
  `emitRuntimeBlame` ("denullify of an absent value") on absence.
- Comparison `val == null` tests the discriminant flag for value types and `ptr == null`
  for pointers.
