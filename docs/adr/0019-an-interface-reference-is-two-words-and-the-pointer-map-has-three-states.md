# An interface reference is two words, and the pointer map has three states per word

Where an interface is used as a runtime type, the value is `{i8* data, vtable* }` — two words. And the
per-type pointer map that a provider supplies (ADR 0014) distinguishes **three** states for each word, not
two: traced, meaning the collector must follow it; not a pointer; and **a pointer the collector must not
follow**.

## The representation half is forward-looking, and the corpus says so

Interface-as-a-runtime-type does not exist in the corpus. Across fifty samples every interface appears either
as a bound — `T: Castable` (`deeptest2.fin:11`, `generics_interfaces.fin:6`, `lambdas.fin:67`,
`nullifier.fin:8`), `X implements <Iface>` (`implements_block.fin:11`, `:26`, `stdio.fin:52`,
`typing.fin:25`, `collection.fin:101`) — or as the `$interface` meta-type (`literal_interface.fin:3`, `:16`).
There is no `let p <Printable>;` and no `fun f(p: Printable)` anywhere. Interfaces in Fin today are resolved
statically, by monomorphisation or by erasure.

So this half is a decision made ahead of its first user, and it is recorded rather than deferred because the
other half cannot wait.

## The three-state map has a consumer today

`Castable` is an erasure marker, and an erased generic is a fat pointer whose second word points at a vtable
— which lives in static data, not on the heap. The corpus names this outright at `deeptest2.fin:11`:
"A more complicated interface uses Generics (Specially Castable FatPointer type)".

A two-state map has to call that second word either traced or not-a-pointer, and both answers are wrong.
Traced sends a collector walking into static data. Not-a-pointer throws away the fact that the word *is* a
pointer, which matters to anything that moves objects, verifies a heap, or reports what a value refers to.
The third state is the only correct answer, and `Castable` needs it now, before any interface reference
exists.

D supplies the evidence for what happens without it: `getTypePointerBitmap` returns an all-zero bitmap for an
interface reference — a value whose first word is a GC pointer, reported as containing none. A precise
collector built on that map does not trace through interfaces. Fin is deciding the shape of the map before
the collector is written, so the cost is one extra state in an enum rather than a class of leak found later.

## Why two words rather than one

A one-word interface reference means the vtable has to be found from the object, so every participating heap
object carries a header the *compiler* mandated. ADR 0015 gives header words to libraries to request, during
the decide moment, specifically so that the compiler does not mandate them. A fat pointer keeps the object's
layout the library's business and puts the dispatch cost on the reference instead.

## Consequences

An interface reference does not fit where a single machine word is expected, so handing one across the
foreign ABI needs a decision this ADR does not make. ADR 0015 already makes a foreign-ABI type with requested
header words a diagnostic; this is the neighbouring case and should get the same treatment.

`any` is `{i8*, i64}` and an interface reference is `{i8*, vtable*}` — the same shape with a different second
word. They are therefore not interchangeable, and converting between them is a real conversion rather than a
reinterpretation. Any code that assumes "two words means `any`" is wrong.

The provider that answers the pointer map must be able to express the third state for a word it did not
choose the meaning of, so the map's element type is an enum in the API surface, not a bit. That also settles
the sizing concern in ADR 0007 — the answer is counted in fields, and a three-state element makes the "one
bit per word" shortcut unavailable anyway.
