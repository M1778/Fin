# The LLVM backend is written fresh, not ported from `pyprototype`

`pyprototype/src/codegen/` is a working LLVM IR emitter covering monomorphisation, type
erasure, `typeof`, and `as_ptr`, and porting it to C++ would have given us a differential
oracle against the new front end almost for free. We are writing a new backend instead,
because the prototype was built to prove the language could be lowered at all: it has no
optimisation story and its error handling is thin, and both are requirements rather than
extras for the shipping compiler. Inheriting its structure would mean inheriting the
decisions it made under that lower bar.

## Consequences

`pyprototype` remains authoritative as a *design* record even though none of its code ships.
Two of its lowering decisions are carried forward deliberately: erasure is selected by the
presence of an erasure-marker constraint on any one parameter, and an erased generic is
represented as a raw pointer.

We give up the differential oracle. Until the new backend can run a sample, the front end has
no independent check that it agrees with the reference implementation, so the sample corpus
has to carry that weight alone.
