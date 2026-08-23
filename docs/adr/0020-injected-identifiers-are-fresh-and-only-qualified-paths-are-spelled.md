# Injected code mints fresh identifiers and can spell only module-qualified paths

Code that a handler injects gets its identifiers two ways and no third. `compiler.code.fresh()` mints an
identifier that cannot collide with anything, and every binding the injected code introduces uses one.
`compiler.code.ident(name)` builds a reference to an existing declaration and accepts **module-qualified
paths only** — it cannot name a local.

There is no quote and no splice anywhere in the fifty samples, so nothing in the corpus constrains this. That
absence is the reason to write it down: there will be no sample to read the rule off later.

## Why

A handler injects code at a point it did not choose (ADR 0007), into a scope written by someone who has never
heard of the library. Let injected code spell a bare identifier and two failures follow immediately. A
handler that introduces `let tmp` breaks every function that already had a `tmp` — a collision the user
cannot avoid, because the name belongs to a library they may not know is armed. And a handler that *reads*
`count` binds to whatever local happens to be called `count` at the injection point — a capture the user
cannot see, because nothing at the injection point mentions the handler.

C's preprocessor is the counterexample that every macro system since has been designed against, and a
compile-time library injecting deallocation calls into arbitrary scopes is precisely the shape that
reproduces it.

## Why not full hygiene

Full hygiene — Scheme's `syntax-rules`, Rust's mixed-site hygiene in `macro_rules!` — requires the compiler to
carry a renaming environment through every injected node and resolve names against the environment they were
written in rather than the one they land in. ADR 0006's interpreter is a tree-walker over the host AST and
has no such machinery, and adding it is a large piece of work aimed at a problem the restriction solves
outright.

Restricting `ident` to qualified paths buys the property that actually matters — no accidental capture — for
the price of one check on one argument, because a qualified path resolves in a module namespace where locals
do not exist. Full hygiene would additionally let injected code *deliberately* refer to a caller's local,
which is a capability, not a safety property, and not one anything has asked for.

## Consequences

A handler cannot refer to a local by name, so anything it needs from the scope must be handed to it by the
event as a value. That constrains the payload: a scope-exit event has to supply the live variables
themselves, not merely the scope. ADR 0003 arrived at the same requirement from the other direction — a
memory-management library has to walk a scope's live variables — and two independent requirements landing on
one payload is evidence the payload is shaped right.

`fresh()` is called during injection and its results are not stable between compilations, so nothing may
depend on how a fresh identifier is spelled. Diagnostics in particular must show the user's names; a
diagnostic naming a minted identifier is unactionable. That is the same surface as the handler-attribution
field reserved in the machine contract (ADR 0009), and the two need to agree.

`ident` rejecting an unqualified name is a diagnostic the library author sees, not the user of the library —
which is the right place for it, and an argument for making the restriction a hard error rather than a
convention.
