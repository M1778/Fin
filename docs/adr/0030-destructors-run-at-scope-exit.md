# Destructors run implicitly at scope exit, fields after the body

Accepted from the grill-with-docs session on `wave3-semantics`: explicit destructor symbols alone leave every scope exit silent, so codegen inserts the call. At each point control leaves a binding's scope — every `return`, `break`, `continue`, and fallthrough (`blame` aborts and unwinds nothing) — locals whose type has a destructor have it invoked, including compiler-generated parents. Field destructors run after the declared body, per ADR 0016.

There are no moves in the language to skip for: every binding copies, so every value destroys independently -- C++ value semantics without move constructors, including the double-free hazard on copied owning values, which is the programmer's exactly as there. When the `move_or_copy` protocol lands, this decision is the one that gains a moved-from case.

## Consequences

`delete &field` stays explicit storage deallocation rather than a destructor call, so it does not collide with automatic field cleanup today; if `delete` is later specified to invoke the field's destructor, this decision is the one to revisit. `variable_scope_exit` still fires once for the outer variable while nested cleanup stays the language's own work.
