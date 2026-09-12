# An interface reference is `{data, vtable}`, and the vtable carries field offsets

An interface used as a runtime type is **two words**: a pointer to the implementor's
storage, and a pointer to a per-`(struct, interface)` vtable.

```
value:   { data: i8*, vtable: i8** }        16 bytes on a 64-bit target

vtable:  [0 .. F-1]   one i64 per required field, its byte offset in the implementor
         [F .. F+M-1] one function pointer per required method
```

A **field** read through the reference loads its offset from the vtable, adds it to
`data`, and loads through the result. A **method** call loads the function pointer and
calls it with `data` as the receiver.

This ADR is the layout half. ADR 0019 fixed the two-word shape and the pointer map's
three states; what it did not say is how a *field* declared in an interface is reached,
because at the time no corpus site read one.

## Why field offsets in the vtable

`tests/samples/love.fin` is the witness, and it is why methods alone are not enough. It
declares `interface Person { readonly name <string>, }` — a **field**, not a method —
and reads it through a value of interface type (`self.hate.name`), and stores an
interface value in a field (`readonly hate <Person>`).

Two implementors, and the offset is not the same fact for both:

```fin
struct Fin: <Person,Beautiful> {
    readonly hate <Person>,     // 16 bytes  -> name is at 16
    name <string>,
}

struct M1778: <Person,Loser> {
    readonly love <Person>,     // 16 bytes  -> name is at 16
    name <string>,
}
```

They happen to agree here. They will not in general, and an interface has to promise a
read that works for *any* implementor — including one compiled in another translation
unit, which is the case that rules out computing the offset at the call site.

So the offset travels with the vtable, one slot per required field, and the read costs
one extra load. That is the price of an interface being a real runtime type rather than
a compile-time bound.

### Why not require a fixed prefix layout

The alternative was to force every implementor to place the interface's fields first, in
declaration order, so `name` sits at a known offset and the read is direct.

It is faster, and it would reuse the base-splice rule already implemented for
inheritance (ADR 0026's neighbourhood: a base struct's fields splice in at offset 0). It
was rejected because **it is unsatisfiable for a struct implementing two interfaces that
both require fields.** `love.fin` already has structs implementing two interfaces each
(`: <Person,Beautiful>`, `: <Person,Loser>`), and only the accident that `Loser` and
`Beautiful` are empty keeps it from being the failing case. A rule that works only while
the second interface stays empty is a rule that breaks on the next sample.

It also constrains a struct's layout for the benefit of a type that is not its own,
which is the thing a struct's field order should not depend on.

## What a vtable is keyed by

One vtable per **(implementor, interface)** pair, not per implementor and not per
interface. `Fin` implementing both `Person` and `Beautiful` has two, because the slot
order is the *interface's* — a reader of a `Person` reference knows only `Person`'s
required members and must find them at fixed indices.

Emitted where the conversion is emitted, `linkonce_odr`, which is what the generic
instantiations already use: the same pair may be converted in two translation units, and
the linker keeping one is correct because the contents are a function of the pair alone.

## Slot order is the interface's declaration order, fields first

Fields before methods, each group in the order the *interface* declares them. Two
reasons, and the second is the load-bearing one:

* A reader needs a fixed index per member, and the interface is the only declaration
  both sides share.
* Fields first means a vtable can be read by a consumer that knows only about fields —
  which is what a collector is (ADR 0003 puts layout in the compiler for exactly this),
  and what ADR 0019's three-state pointer map is for.

## What this decides and what it does not

**Decided:** the shape, the slot order, the keying, that a field costs one indirection,
and that the conversion is by value from any addressable value (the owner's ruling of
2026-08-28, recorded in `Type::isAssignableTo`).

**Not decided:**

* **A temporary with no address.** `f(make())` has nothing for `data` to point at.
  Refused, consistently with `&make()` and `make()[0]`, until an owner ruling says where
  such a value lives.
* **Whether a `readonly` field is writable through the reference.** `love.fin` declares
  `readonly hate <Person>` and assigns `self.hate = someone` inside the struct's own
  method, which is the one place the corpus permits it. Through an interface reference
  from outside, nothing writes one.
* **Reference equality.** Whether two references to the same object compare equal, and
  whether the vtable participates. No corpus site compares them.
* **Interface-to-interface conversion.** A `Person` reference to a hypothetical
  narrower interface would need a vtable derived from a vtable. No witness.
* **Whether an implementor is *required* to carry a declared field.** It is not, today:
  `StructType::implements` walks methods, operators, constructors and the destructor and
  never fields — `KnownDefect_Interfaces.AMissingFieldIsAccepted`. That is a hole in
  conformance, and this layout makes it a *live* hole rather than a latent one: a vtable
  needs an offset for a field the implementor does not have, so the backend must refuse
  what the front end accepted. **Fixing that defect is a prerequisite for emitting a
  vtable**, and it is the first thing the implementing unit has to do.

## Consequences

`love.fin:57` refuses today with `a struct field of type 'Person' is not lowered yet`,
which is a field whose type is an interface — the first thing this needs. The order the
implementing unit will want:

1. Close `KnownDefect_Interfaces.AMissingFieldIsAccepted`, per above. Without it a
   vtable has no offset to emit and the failure surfaces as a backend refusal for a
   program the front end blessed.
2. Map an interface type to the two-word struct in `TypeMapper`, so a field of interface
   type has a representation.
3. Emit a vtable per converted pair, and lower the conversion to a two-field aggregate.
4. Lower a field read through a reference: load offset, GEP, load.
5. Lower a method call through a reference: load pointer, call with `data`.

Steps 4 and 5 are what `love.fin` needs to run; steps 1–3 are what it needs to compile.

The two-word value is passed and returned like any other two-word aggregate, so nothing
about the calling convention is new — which is the argument for `{data, vtable}` over a
heap-allocated box, and the reason ADR 0019 chose it before there was a user.
