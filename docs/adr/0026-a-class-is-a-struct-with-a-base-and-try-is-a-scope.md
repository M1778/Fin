# A class is a struct that may name a base, and `try` is a scope

Two rulings, recorded together because both were made on 2026-08-28 and both are about
`tests/samples/readonly.fin`, which needed each of them.

## 1. A `class` is a value, copied on assignment

`class X { ... }` lowers exactly as `struct X { ... }` does. It is a value type. The only
thing `class` buys is permission to name a base struct — "stronger inheritance support",
in the corpus's own words.

### The corpus says this twice

* `tests/samples/readonly.fin:16` introduces `class MyClass` with the comment
  **"Readonly in classes (same with struct)"**. Not "similar to"; the same.
* `tests/samples/stdlib/error.fin:7` writes `#[class]` on a `struct` with the comment
  **"turns structs into classes (for stronger inheritance support)"**. An attribute that
  converts one into the other is an attribute between two things that differ in
  inheritance and nothing else.

### The one line that reads the other way does not survive

`lib/std/stdptr.fin:29-30` says `own()` returns `&Self` rather than `Self` because
*"returning it by value would copy the very counter that makes it a smart pointer."*
Read as a claim about class semantics, that would argue for a reference type.

It is not such a claim. It is about **that method's return type**, and the counter it
names is declared `counter <&uint>` (`tests/samples/stdlib/stdptr.fin:39`) — a
**pointer**. Copying the class copies the pointer, and every copy still refers to one
count. The sentence is true and it is about `own()`.

### Why this was a ruling and not a measurement

**No corpus site copies or assigns a class value.** `rptr` is constructed
(`const.fin:80`, `:98`) and its fields are read; `MyClass` (`readonly.fin:17`) and
`TypeInfo` (`stdlib/types.fin:12`) are never instantiated at all. So ADR 0008 has nothing
to say here — there is no witness either way, which is exactly the case where the owner
decides rather than the corpus.

What the ruling buys is that `class` needs no separate lowering. What it costs is that if
Fin later wants reference semantics, this is the ADR to supersede, and the cost of doing
so scales with how much code assumes value semantics by then.

### What is not decided

* **Destructors.** `stdlib/stdptr.fin:34` writes `~Self();` and `lib/std/stdptr.fin:26-27`
  records that it does not parse. A destructor is refused at its declaration regardless of
  `class` or `struct`, and ADR 0016 has the composition rule for when it lands.
* **A vtable, or any dynamic dispatch.** Nothing here makes a method virtual. ADR 0019
  rules that an *interface reference* is two words; no corpus site takes one.
* **Whether a `&Derived` may be passed where a `&Base` is expected.** Refused by the
  analyzer today (`expected '&Base', got '&Derived'`), and left refused deliberately — see
  §3 below.

### The implementation this needs, which is not yet written

`class X { ... }` parses to a **`ClassDeclaration`**, not to a `StructDeclaration` with a
flag (`parser.y:556`). The two nodes carry identical fields, and `ClassDeclaration` does
not derive from `StructDeclaration` — a fact `parser.y:36-41` records as having already
cost one bug, where an attribute-dispatch chain with no `ClassDeclaration` branch silently
dropped every `#[...]` on a class.

So the backend cannot simply stop refusing. `declareStructs`, `StructInfo::decl` and
`declareStructMethods` all key on `StructDeclaration*`. Booked as its own unit.

`StructDeclaration::is_class` is **not** the hook: nothing sets it. The parser leaves it
false on every `StructDeclaration` it builds, and it survives only because
`CloneDecls.cpp:54` copies it. A refusal on it was dead code and has been removed.

## 2. `try` lowers as its block, and `catch` emits nothing

`try { A } catch (E as e) { B }` emits `A` and emits nothing for `B`.

### Because nothing in Fin raises anything a `catch` can receive

`blame`'s assert form prints a location and message to stderr and calls `abort` — it does
not unwind. Its raise form is still refused by the backend. Those are the only two
statements in the language that could produce a catchable event, and neither does.

The corpus has **exactly one** `try`: `readonly.fin:48`, wrapping `a.v1 = 5` with
`catch (Error as err)`. The statement it guards cannot raise — assigning to a `readonly`
field is a **compile-time** error, which is what the rest of that sample is about. So the
handler is unreachable.

Emitting nothing for an unreachable handler is not a dropped statement. Emitting landing
pads and a personality function instead would be machinery no corpus site exercises.

### The catch body is still analysed

`Analyzer_Stmt.cpp:148-156` walks the catch block, defines the catch variable in a fresh
scope, and type-checks the body. Only *code generation* is skipped, so an undefined name
inside a `catch` is still a diagnostic — from the front end, carrying no `codegen:` prefix.
`Soundness_Codegen.ACatchBlockIsStillAnalysedEvenThoughItIsNotEmitted` pins that split as
deliberate.

### What fails the day this becomes wrong

`Soundness_Codegen.ATryBlockRunsAndItsCatchDoesNot` asserts that the catch body's `printf`
never runs. When a raise form lowers, that test fails, and **the failure is the signal to
build a real mechanism** — not a thing to relax. Three tests sit beside it: that the try
block's side effects outlive the block (a discarded scope would pass a weaker test), that
an unlowerable statement inside a `try` is still refused (`try` is a scope, not a
suppression), and the analysis split above.

Those four tests are new because there were none. The refusal was asserted nowhere, which
is why lowering it broke nothing — and also why a *wrong* lowering would have gone
unnoticed.

## 3. A derived pointer is not a base pointer, deliberately

The ABI now makes the upcast free: a base struct's fields splice in at offset 0, so a
pointer to the derived struct already is a pointer to the base and an upcast would emit no
instruction.

It stays refused anyway. **No corpus sample passes a derived pointer where a base pointer
is expected** — measured across all fifty — so there is no evidence for what the rule
should be, and the neighbouring cases are not obvious: whether `&&Derived` converts to
`&&Base` is almost certainly *no* (it would let a `Base*` be stored through a `Derived**`),
and a rule invented now would have to guess at that variance.

ADR 0008's discipline applies: the layout makes it *possible*, and a witness is what makes
it *ruled*. Booked in `docs/HANDOFF.md` §8 with the measurement, so the next reader
inherits the finding rather than rediscovering it.
