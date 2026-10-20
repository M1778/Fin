# The reflected intrinsics fold the compiler's own facts through the nearest literal spelling

**Status:** Accepted

> 2026-10-20 status: shipped (commit 47ceb96 — `name_of`, `size_of`, `field_count`, `offset_of` fold from their own facts; `fields_of`/`FieldInfo` remain blocked — a runtime owner for the field-name string is still needed); issue #53 CLOSED (ledger comment 6280766517: scopes A+B shipped; full suite 2931/2931).

## Context

Issue #53's Scope A asks for read-only introspection that runs, and the resolving
component ops (`name_of`, `size_of`, `field_count`, `offset_of`) never answer one: the
analyzer resolves their signatures and a grant, and stops (`CompilerApi.cpp`). The
substrate was measured before this tree wrote any of it (docs/reflect-derive-plan.md):
the sizes and offsets the answers need already exist in `src/types/Layout.cpp`, the
field lists exist backend-side in `StructInfo`, `@Alloc` already showed a
compile-time `@`-call lowering into runtime code, and design §2.2's spine says the
interesting work runs at run time while the compiler hands over projections.

## Ruling

The four reflected intrinsics ship as `@`-builtins, evaluated by the backend from the
facts it already lowers with. `@type_name(T)` answers the subject's declared name; a
scalar answers the spelling written (which is its name). `@type_size(T)` answers the
DataLayout's allocation size for the type it resolves. `@field_count(T)` answers the
declared field count, zero for anything fieldless. `@field_offset(T, f)` answers a
field's offset, and its field argument must be the *literal* -- the one form a
compile-time fold can read -- in the declaration order the backing store lays out.

Every answer is a constant lowered through the nearest existing literal path: a name
is one `internedString` global, a measure an `i32` integer constant. No metadata blob
is born, no runtime call is made, and a runtime loop still works because its subject
is a real array. The subject is an argument's *spelling* -- a type's name, the same
contextual reading Soundness_TypeAsValue gives `@implements`' struct argument. Inside
a generic function that spelling may be the enclosing function's own type parameter:
the C++ backend binds it at the instantiation, through the same substitution the
template body is already mapped with (`Soundness_Reflect.
ATypeParameterBindsThroughTheWrapper`). The self-host stage keeps one erased body, so
a parameter spelling refuses there by name (`no type named 'T'`) until the Phase B
monomorphization work gives both compilers the same wrapper route. `%type` values are
untouched by this ruling (R1 stays open, `types.fin:26`).

Result types are `string` and `int` deliberately, and *not* the component table's
`size_of: uint`: a count and a byte measure are not layout widths, and the corpus
rounds counts to `int` (`getenumkeyid` answers `int`; the aspirational `typeid`
chose `<int>` too). The alias spelling answers what it aliases -- the
representation the program lays out -- which is the same rule the annotations
resolve by.

Refusals name the construct, per the standing backend invariant: a value argument
(`expects a type name`), an unknown name (`no type named`), an unknown field
(`type 'X' has no field 'f'`), a non-literal field argument (`expects a string
literal`), an interface (its fat value has no owning layout to reflect yet), and a
payloaded enum (it carries members, not fields; the member ops come with
`enum_from!`). Nothing silently answers zero.

`@Alloc`, `@Free`, `@implements` and `@defined` reserve their names as builtins and
these four join that family: a user declaration of the same name does not shadow the
`@`-spelling. The plain (un-`@`ed) spelling falls to the ordinary call path exactly
as it does for `@defined`, so a user `fun type_size(...)` keeps meaning its own
function when called without the `@` -- the corpus declares none today.

## Considered options

- **Evaluating the component calls in place** (`compiler.layout.size_of(t)` returns a
  comptime constant). The long-term home of the component surface, and delayed
  because every consumer then needs a comptime struct value with runtime strings,
  the `$type`-word identity question, and -- per §2.2 -- folding that has no
  interpretability line to run on. The builtins do not block this later move; they
  are the projections it would ride.
- **A runtime metadata table per type, emitted beside its code.** Same layering, more
  backend: a global blob family, loads, and a linker identity to keep alive. The
  unrolled, call-site shape carries no ABI and answers every fact the surface names.
- **Wrappers over a `$type` value** (`name_of(t: $type)`). Needs R1's ruling and
  family; the spelling rule above answers the same question without it.

## Consequences

Sample: `tests/samples/stdlib/reflect.fin` (`//@ ok`, pins the 16/2/8 layout facts
and the enum-as-int answer across finc, stage2 and stage3). Surface: lib/std/reflect.fin
is the documented home and ships no declared functions yet -- the builtins are `@`-
spelled at the use site and nothing resolves against an import, so a wrapper
(`name_of<T>()` as generic sugar) waits until the monomorphization route exists in
both compilers; the R4 spellings ride the same landing. Why the missing shape:
`fields_of` stays unshipped until a `FieldInfo`'s name string has an owner at run
time (issue #53 Scope B, docs/reflect-derive-plan.md). Two stage-side edges are
deliberately narrower than the C++ answers and say so instead of guessing: a
struct of more than 64 fields refuses (the LLVMTypeArgs bridge carries 64 slots),
and an alias spelling answers on the C++ side only (the stage names no type for
one yet).
