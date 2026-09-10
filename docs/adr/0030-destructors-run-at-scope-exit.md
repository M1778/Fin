# Destructors run implicitly at scope exit, fields after the body

Accepted from the grill-with-docs session on `wave3-semantics`: explicit destructor symbols alone leave every scope exit silent, so codegen inserts the call. At each point control leaves a binding's scope — every `return`, `break`, `continue`, fallthrough, and `blame` unwind — locals whose type reports `has_destructor` have it invoked, including compiler-generated parents. Field destructors run after the declared body, per ADR 0016, and a moved-from variable is skipped.

## Consequences

`delete &field` stays explicit storage deallocation rather than a destructor call, so it does not collide with automatic field cleanup today; if `delete` is later specified to invoke the field's destructor, this decision is the one to revisit. `variable_scope_exit` still fires once for the outer variable while nested cleanup stays the language's own work.
