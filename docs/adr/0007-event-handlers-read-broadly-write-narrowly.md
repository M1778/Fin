# Event handlers read anything and write only at the event point

A library subscribes to a compile-time event in two steps: `#[on(variable_scope_exit)]` on a
`@special` function declares the handler, and a `compiler.events.enable(handler)` call inside an init
special function arms it. A handler may read any part of the program — every declaration, every
type's layout, which types contain pointers, the other variables in scope. It may write nothing
except code injected at the event point, which it returns as a quote. When several handlers are
registered for one event they all run, in a fully determined order: reverse post-order of the import
DAG, ties broken by module path string, then declaration order within a module.

That ordering rule is a correction to this decision rather than a change of it. It first said "import
order across modules", which is not a total order — two modules each importing a third leaves the third
unplaced — while also making handler order observable. An observable property must be defined.

The two-step registration exists so that `import "CortexGC.fin";` on its own cannot change how
memory is freed. Declaring by attribute lets the compiler build the whole subscription table during
analysis before it runs anything, which imperative-only registration would forbid; arming by call
keeps the change opt-in and gives the user's one-line initialiser something to do.

The read/write asymmetry is the load-bearing part. Read power has to be broad because a tracing
collector must ask which types contain pointers, and nothing weaker lets it trace. Write power is
narrow because the alternative — letting a handler replace or delete the node — makes every event
point a place the program can silently change meaning, and makes two handlers on one event
impossible to reason about. With injection-only, handlers compose by construction: none can
invalidate another's view of the program, so chaining them needs no conflict resolution.

## A handler that injects nothing is still a handler

A handler may return an empty quote, and doing so is not a degenerate case for the compiler to
diagnose or optimise away. A library that only wants to *observe* — to check an invariant, to count, to
read a finalised layout without contributing to it — is a legitimate subscriber, and read-broadly is
what it needs.

## The question this decision delegated has been answered "neither"

The per-type question — "which words of this type are pointers" — was left open here between being an
event and being a plain query under `compiler.types`. The compiler API design, which this decision gave
the choice to, has answered that both spellings are wrong, and the argument is strong enough to record.

A query is pulled by the library, so the library must decide when to ask, and a collector needs the map
of every type reaching the heap. Making a query sufficient requires a "list all types" operation and a
loop over it — and a loop is precisely what the interpretability line does not have (ADR 0006). An event
is worse: the handler would accumulate per-type answers in mutable compile-time state across
invocations, which makes the metaprogram order-sensitive, and it would then need to write those answers
somewhere other than an event point, which is the one thing this decision forbids.

D shows the third shape, and D is the only language that has shipped a precise collector this way.
DMD neither fires an event nor answers a query: it *pulls* `object.RTInfo!T` once per type, CTFE-evaluates
it, and stores and emits the returned value itself (`semanticRTInfo` in `compiler/src/dmd/semantic3.d`,
emitted at `glue/toobj.d`; the entire hook is `if (sc._module.ident == Id.object) { if (tempdecl.ident ==
Id.RTInfo) Type.rtinfo = tempdecl; }` in `templatesem.d`). The compiler enumerates, the library answers,
the compiler keeps the answer. The library writes nothing at all, so this sits *inside* the
read-broadly-write-narrowly rule rather than needing an exception to it.

That mechanism has been ratified under the name **provider**, in ADR 0014, which names all three
mechanisms and the distinction between them. What is settled *here* is the negative half: the per-type
pointer map is not an event handler, and this decision should not be read as promising it will be one.

## Consequences

Two garbage collectors armed at once will both free the same variable. No compiler check catches
this, because injection-only is exactly the property that makes handlers independent. It is the
libraries' problem, and the language says so rather than pretending otherwise.

There is no exclusivity mechanism *for events*, and there is deliberately none: an event that genuinely
admits only one subscriber is not an event. ADR 0014 supplies the two mechanisms that are exclusive —
protocol for replacing an operation, provider for answering a question — so an extension point needing
one claimant is expressed by choosing a different mechanism rather than by qualifying this one.

Handler order is observable, so reordering imports can change program behaviour in a program that arms
two handlers on one event. The ordering rule above makes that behaviour reproducible; it does not make
it insensitive.

An ownership model that wants to rewrite a move into a copy cannot be written as an event handler.
Whatever mechanism serves that is a separate decision; events do not serve it. That decision is ADR 0014,
and the mechanism is a protocol claiming the `move_or_copy` slot.

Whatever a handler is handed to read, the size of the answer must not scale with the size of the type.
D's pointer bitmap is one bit per pointer-sized word, so `struct S { int[200000000] x; }` costs about
0.3 s of semantic analysis to produce a bitmap of zeroes, and DMD is being changed to return the size
alone for pointer-free types (dmd PR 22289, open). Fin's equivalent has to be counted in fields, not in
bytes, or a large array makes compilation quadratic for no information.
