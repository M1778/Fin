# Layout is two moments, and a query asked in the wrong one refuses instead of answering

A type's layout is worked out in two named moments. In **decide** the type is incomplete: a library may
contribute header words and alignment, and every query whose answer depends on the final layout is
illegal. In **observe** the type is complete: layout may be measured, and contributions are illegal.

A design with only the observe moment cannot let a library contribute to a layout; one with only the
decide moment cannot let it measure the result. Terra splits the same concern the same way —
`__getentries` while the type is incomplete, `__staticinitialize` "after the type is complete but before
the compiler returns to user-defined code" — and documents erroring on completeness questions in the
first, which is the part worth copying rather than re-deriving.

## An illegal query refuses; it does not return zero

D's `getTypePointerBitmap` sizes an interface with `t.size()` and yields a pointer-sized span with an
all-zero bitmap, although an interface reference is a GC pointer. That is an answer that is confidently
wrong, and its consumer is a collector deciding what to trace. A refusal costs one diagnostic. A wrong
answer costs a corrupted heap and produces no diagnostic anywhere, at any point, ever.

So a layout query in the decide moment names the moment and refuses. This is the same rule as the grant
layer's: the compiler would rather say it cannot answer than answer badly.

## `request_header_words` exists, is additive, and returns an offset

ADR 0003's collector has no other way to obtain a per-object mark bit. A side table keyed by address
allocates during collection, which is the bootstrapping problem every collector meets, and in Fin it
would have to be written against an allocator that is itself the subject of collection.

The request is additive across claimants, so two collectors get two headers instead of a conflict — which
matches ADR 0014's rule that events and their kin are plural where protocols are singular. Additive
requires that the call **return the offset at which the caller's words were placed**: without it, each
claimant knows it has words and not which ones are its own, which makes the second claimant unusable.
That is one integer now, and an ABI break after libraries ship.

Requesting header words changes the type's size and its field offsets, which is exactly why the decide
moment must close before anything observes. A type with a foreign ABI cannot grow a header, so a request
against one is a diagnostic rather than a silent no-op.

## Consequences

The decide moment has exactly one operation in it today. Refusing `request_header_words` would have left
it an empty phase, and the two-moment split a distinction with nothing on one side of it — so the two
halves of this decision hold each other up and should be reversed together or not at all.

Every layout answer must be counted in fields rather than bytes, per ADR 0007's consequence. A per-object
header is a fixed cost; a per-word pointer bitmap is not, and D pays about 0.3 s of semantic analysis to
produce a bitmap of zeroes for `struct S { int[200000000] x; }`.

`pointer_offsets` is askable only in the observe moment. ADR 0007 named `struct_layout_finalised` as "the
only sound moment" to ask for it, which was right and is now the second of two named moments rather than
the only moment there is.
