# Memory management is written in Fin, not built into the compiler

Fin has neither a garbage collector nor a borrow checker. The compiler instead exposes its own
machinery as compiler components, and memory-management strategies are written in Fin against
them: an ownership model, a reference-counted pointer, or a full garbage collector is a library
that a user imports and initialises in one line. This is the central architectural bet of the
language — the alternative was to pick one memory model and build it in, which would have made
the component API optional rather than load-bearing and would have foreclosed exactly the
extensibility the language exists to offer.

## Consequences

The component API is not a convenience layer; it is the mechanism by which the language keeps
its promises, so its surface is a language-design commitment and not an implementation detail.
It must be powerful enough that a garbage collector is expressible in it.

Components must include compile-time *events* — a strategy that frees a variable when it leaves
scope needs the compiler to tell it that a variable left scope. No sample specifies this yet.

`rptr` in the standard library is a library, not a compiler feature. Its rules are enforced at
run time by raising error values, so misuse is caught by `try`/`catch` and not by the type
checker. The compiler's only obligation to it is deterministic, ordered scope-exit.
