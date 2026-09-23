# Enums are tagged unions with 32-bit discriminants

An enum lowers representationally as a tagged union with a 32-bit (`i32`) integer
discriminant tag. A fieldless enum consists solely of this 32-bit tag. An enum with
member payloads consists of the 32-bit tag followed by a payload buffer sized to the
maximum member payload and aligned to the maximum member alignment:
`{ i32 tag, [MaxPayloadSize x i8] payload }`.

## Considered Options

- **Fieldless-only enums with scalar integer representation:** The compiler currently
  lowers fieldless enums to `int` in `CodeGen_LLVM.cpp:1059` while `Layout.cpp:454`
  refuses all enums. Retaining fieldless integers alone leaves sum types, payload enums
  (`Color`, `Result<T, E>`), and error-handling pipelines uncompilable.
- **LLVM-level first-class typed union (`{ i32, %UnionPayload }`):** Requires declaring
  complex LLVM union aggregates and frequent bitcasting across conflicting member
  definitions in function ABI signatures.
- **Canonical tagged union buffer (`{ i32, [MaxPayloadSize x i8] }`):** Selected.
  Guarantees uniform pass-by-value calling conventions and deterministic contiguous
  storage matching standard C-ABI tagged unions. Member constructor calls store the
  discriminant and write into the payload buffer. Positional access (`.0`) GEPs to the
  buffer and casts to the agreed slot type.

## Consequences

- `LayoutEngine` (`src/types/Layout.cpp`) computes size and alignment for all enums:
  - Fieldless enums: size 4, alignment 4.
  - Payloaded enums: `tagAlign = 4`, `maxPayloadAlign = max(payload.align)`,
    `totalAlign = max(4, maxPayloadAlign)`, `totalSize = alignUp(4, maxPayloadAlign) + maxPayloadSize`,
    padded to `totalAlign`.
- `CodeGen_LLVM.cpp`:
  - `declareEnums` computes `maxPayloadSize` and `maxPayloadAlign`. If zero, enum maps to
    `i32`. If non-zero, enum maps to an LLVM struct with tag and payload byte array.
  - Member constructors (e.g. `Color::RGB(100, 200, 50)`) emit the tag constant and store
    payload elements into the payload buffer.
  - Positional member access (`.0`, `.1`) loads from the payload buffer at the member offset.
  - Member comparisons (`enum == Ok(T)`, ADR 0037) and reflection (`getkeyid(enum)`) read
    the 32-bit tag and compare against the member's constant identifier.
