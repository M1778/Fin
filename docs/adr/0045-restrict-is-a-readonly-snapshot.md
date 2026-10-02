# `restrict` is a readonly snapshot, not `&self`

`rptr.restrict` is a heap sibling built by `alias()` -- owned=false,
borrowed=false -- instead of `self.restrict = &self` in the constructor.
The snapshot is what `const.fin:83` always documented: "a copy of itself
but in readonly mode". The code now matches the comment: `test_3` reads
`owned == false` off the snapshot, takes the readonly branch honestly,
the value stays 5, and `const.fin:85` holds on both compilers.

The `&self` form depended on what the backend made of a ctor-time
self-address: a dangling slot on one compiler, a live alias (owned=true,
so `test_3` took the writable branch and `const.fin:85` failed) on the
other. The C++ rule -- `&self` in a constructor is the object address,
with SP1's fix and its pin in `tests/test_codegen.cpp` -- stands and is
now irrelevant to the library: nothing in `stdptr.fin` takes `&self` in
a constructor anymore.

## Considered Options

- `&self` (the old form): aliases the live handle, so the "readonly"
  copy reports owned=true. Wrong on the compiler that keeps it live,
  garbage on the one that does not -- behavior differs per backend for
  one line of library code.
- Snapshot via `alias()`: exactly the readonly meaning (owns nothing,
  borrows nothing, so `set` refuses it), shares the value and both
  counters, costs one reference. `alias()` already yields owned=false,
  borrowed=false, so no new construction path was needed.

## Consequences

- `refs()` reads 2 straight after `rptr(v)` (this handle plus the
  snapshot), and every downstream count in `const.fin` shifts by one;
  the sample's blames assert the honest values, verified by running.
- The snapshot sibling is heap-allocated and `~rptr()` is empty, the
  same profile as the `own()`/`borrow()`/`alias()` siblings today.
  Consistent, and no scope-exit machinery is built here (ADR 0003).
- `readonly_view()` is unchanged as the method form for a caller who
  wants a fresh readonly handle; `restrict` stays the field form the
  sample reads.
