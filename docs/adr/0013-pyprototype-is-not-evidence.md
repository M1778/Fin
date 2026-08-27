# `pyprototype/` is not evidence

`pyprototype/` may be read for ideas. It may never be cited as authority for what Fin is, and its
design is not being ported. When it disagrees with `tests/samples/`, the samples win without argument.

The project owner's assessment: *"pyprototype isnt really helpful here, the prototype is a failed not
even half complete example."*

## Why this needs writing down rather than just knowing

It is the largest body of Fin-related code in the repository and the only place a full pipeline has
ever run end to end, so it reads as the most authoritative thing in the tree. It is the opposite. A
failed implementation that emits LLVM IR is more persuasive than a corpus of samples that no compiler
accepts yet, and that persuasiveness is the hazard.

Three decisions this session leaned on it before this was settled, and each has to be re-grounded:

- The erasure representation — `Castable` as raw `i8*` with `{i8*, i64}` reserved for a marker spelled
  `Castable2` (`src/codegen/types.py:74-82`) — was argued from `pyprototype` against a corpus comment
  at `deeptest2.fin:11` that says the opposite. Neither source is authoritative now: `pyprototype`
  because of this decision, and `deeptest2.fin` because it is aspirational. The decision stands on
  design merit alone, which is that a marker changing calling convention by a trailing digit is
  undiscoverable.
- The erasure marker set `{Castable, Any, Object, VoidPointer}` (`essentials.py:65`) was to be shrunk to
  `{Castable, Any}` on the grounds that the other two appear in no sample. That reasoning survives
  intact, because it was an argument from the corpus rather than from `pyprototype`.
- `struct Any { data <&int>, type_id <long> }` at `pyprototype/stdlib/builtins.fin:62-66` was cited as
  the layout `any` should be emitted from. The idea — emit the layout from a Fin declaration rather
  than hardcode it in codegen — is good and consistent with ADR 0003. The declaration has to be
  written into `lib/std/` as real Fin to be cited.

Everything cited from `pyprototype` after this point carries the qualifier, or it does not get cited.

## Consequences

`finn` currently invokes the compiler as `Command::new("python").arg(compiler)` at
`src/commands/install.rs:33`, targeting `~/Fin/pyprototype`. That is not a fallback to keep; it is dead
code to remove once `finc` satisfies the interface contract.

The corpus is now the only specification, which raises what it has to carry. Fifty samples, of which
twelve parse, are the sole authority on a language with no prose specification — so a construct
appearing nowhere in the corpus and nowhere in the compiler has no definition at all, and `match` is
the example (ADR-less by design: it is unscheduled, not decided).

Reading `pyprototype` for lowering shape when wave 5 starts is still worthwhile and is explicitly
allowed. The rule is about authority, not about ignoring it.
