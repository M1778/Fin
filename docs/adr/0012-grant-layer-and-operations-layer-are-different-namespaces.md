# `compiler.components.X` and `compiler.X` are different namespaces with different jobs

`compiler.components.<name>` is a **component reference** — a compile-time value naming one component,
plus questions *about* that component. `compiler.<name>.<member>` is the **use** of that component —
its operations and its constants. Grant and describe on one side, do work on the other.

The deciding test for any future member, so this never needs re-litigating: **is it about a component,
or through one?** `compiler.components.types.version()` is about the types component.
`compiler.types.gettype::<T>()` is through it.

## Why this is not the inconsistency it looks like

The corpus uses both spellings and it was read twice — by the standard library agent and by me — as one
namespace spelled two ways, with a recommendation to unify them. That reading was wrong, and the
evidence against it is that the corpus is perfectly consistent. Mechanically extracted, the entire
compiler API surface in `tests/samples/` is thirteen paths:

```
compiler.components.enums      compiler.components.system     compiler.components.types
compiler.enums.InBytes         compiler.enums.resolve_id
compiler.structs.select_field
compiler.system.get_available_memory  compiler.system.get_memorycard_model  compiler.system.get_total_memory
compiler.types.cmp_types       compiler.types.ct_any          compiler.types.gettype
compiler.types.typefrom_typeid
```

All three `compiler.components.<name>` uses appear **only inside `#[use(...)]`**, are never called, and
never take a third segment. All ten `compiler.<name>.<member>` uses appear **only in function bodies**.
Fifty samples, thirteen paths, zero counterexamples. The one apparent counterexample —
`compiler.components.exit(1)` — is in `pyprototype/stdlib/builtins.fin:80`, which is not evidence
(ADR 0013).

A specification that consistent across two distinct uses is stating a distinction, not repeating a
typo. The project owner confirmed it: the two "have different stuff in them different methods and
jobs".

## What each layer holds

**`compiler.components`** — capability negotiation, and nothing else. A component reference, and
questions about it: is it present in this compiler, what contract version does it speak, did the
enclosing function declare it, what is it called. This is the layer that lets a library survive a
compiler it was not written against: it can ask whether the events component exists and degrade
instead of failing to compile.

**`compiler.<name>`** — everything else. The type system under `compiler.types`, aggregate structure
under `compiler.structs`, enum structure under `compiler.enums`, the host machine under
`compiler.system`, and whatever else the compiler API design adds.

Within that layer, **a constant lives under the component whose operations take it or return it**. This
sentence replaces an ambiguity in the first draft, which said `compiler.enums` holds "the compiler's own
enum members" — read broadly that puts every enum-shaped constant in the API under one component, and a
grant that nearly every `@special` needs has stopped carrying information. `compiler.enums` holds enum
*reflection*: operations about a program's enums, and the constants those operations take and return.
`compiler.enums.resolve_id` (`enums.fin:11`) belongs there. `compiler.enums.InBytes` does not, because
its only consumer is `compiler.system.get_available_memory`, and a user asking how much memory the host
has should not have to grant the enums component to do it.

`InBytes` therefore relocates to `compiler.system.InBytes`, editing `memory.fin:30`, `:31` and `:39`.
That is a change to the specification and it is made deliberately: the sample is already being edited at
`:26` and `:37` for the `#[use(...)]` violations below, and three more lines in a file already open is a
smaller price than one permanent exception to the only placement rule the layer has.

A component's leaves are **functions or constants**, not functions only. `compiler.enums.InBytes` is
read as a value at `memory.fin:30,31,39` and never called, so any design that assumes every leaf is
callable is already contradicted by the standard library.

## The consequence that makes it work

**Nothing under `compiler.components` requires a grant.** Asking whether a component exists must not
itself need permission, because otherwise capability negotiation is impossible: you would have to
declare a component in order to ask whether it is there, and declaring an absent component is an
error. So `#[use(...)]` enforcement applies to the operations layer only.

That asymmetry is the load-bearing part of this decision. Collapse the two namespaces into one and it
disappears — every query about the API becomes a use of the API, and forward compatibility goes with
it.

## An unknown component name is an error; a known absent one is false

`present()` answering false for an absent component is what makes capability negotiation possible, and
it is also a hole: `compiler.components.evnets.present()` answers false as well, so a misspelling
compiles, reports the feature unavailable, and degrades silently for the life of the library. That is the
same defect class as the `geykeyid` typo in the standard library — a name nothing checks, wrong in a way
nothing runs into. Rust has this hole: `cfg!(feature = "typo")` is silently false, and Cargo shipped
`check-cfg` in 1.80 specifically to close it.

So `finc` carries the set of every component name it has **ever** defined, and resolution of
`compiler.components.<name>` has three outcomes rather than two. A name this build ships resolves. A name
in the set that this build does not ship resolves to a reference whose `present()` is false. A name
outside the set is a hard error at the point of use.

Two obligations follow, both cheap now and expensive after libraries exist. The set is **append-only**:
retiring a component deletes its operations and keeps its name, because a retired name has to go on
answering "absent" rather than becoming "unknown" and breaking the libraries that correctly degraded
around it. And a name may never be **reused** for a different component, since nothing would distinguish
the new component from the retired one whose name it took. In exchange the error gets did-you-mean for
free: the full set of names is exactly what a suggestion needs, and a misspelled capability check is the
one place where the suggestion is the entire value of the diagnostic.

## A meta-type is opaque, and that is what keeps layout reads inside the grant

Member access on a meta-type — `t.size`, `enum_member._keyid` — is not a component call, so no amount of
`#[use(...)]` enforcement reaches it. Permitting it puts the layout surface, which is the most
safety-critical part of the API, outside the only mechanism built to govern the API. So a meta-type is
opaque: its structure is read through component operations and never off the value.

The corpus pays two lines for this. `keyidof` (`enums.fin:20-22`) is a plain `pub fun` returning
`enum_member._keyid`, and across fifty samples it is the only place a meta-type's internals are read
directly. Every other meta-type function in the corpus already follows one idiom — a `@special` does the
component work, and a plain `pub fun` wraps it and carries the public name — and `enums.fin` contains that
idiom in the two functions immediately above the offending one (`@special(priv) getenumkeyid` at `:10`,
wrapped by `getkeyid` at `:15`), with `types.fin:88` and `:95` repeating it. So `keyidof` becomes a
`@special` reaching `compiler.enums.keyid_of` plus a wrapper: the file agreeing with itself rather than
anything new being introduced.

## Consequences

A component reference is a new kind of compile-time value. The meta-type family is `$type`, `$struct`,
`$interface`, `$enum_member`; this adds a fifth member or an adjacent non-member, and which one is not
settled here. The standard library agent's measured value model already requires "an opaque component
handle", so the value exists regardless of what it is called.

Any third segment under `compiler.components` is an operation on a reference, never a component name.
That is what keeps `compiler.components.available` from being ambiguous between "the availability
component" and "ask if this is available", which is the failure a flat registry namespace would have
walked into.

`#[use(...)]` becoming enforceable convicts the standard library in three places. `types.fin:21-23`
declares only `types` and reaches `compiler.structs.select_field`; `memory.fin:26` and `:37` declare
only `system` and reach `compiler.enums.InBytes`. Three of seven `@special` bodies are wrong, including
`typeid` — the single most important function in the library. Those are header edits, and finding them
before the checker existed is the argument for building the checker.
