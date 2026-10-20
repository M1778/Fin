# enum_from! answers an enum value's member name

## Context

Issue #53's Scope B item 2: the one `!` helper the reflection refusals promise --
reflect's payloaded-enum refusal says "the member ops come with enum_from" (ADR
0057), the issue text says the member ops come with it. `getkeyid` answers the
tag of a value, `keyidof` reads a member spelling's id; neither names a member.
Value ⇄ name needs the missing leg, and it is not the format! family's
shape-for-shape twin: the answer depends on the enum's member list, which an
expander without types and without a scope cannot consult (ADR 0023's reason
format! is a builtin) -- and it is not known at expansion time at all. A runtime
variable's member changes between two reads of the same call site, so unlike
the reflect constants this answer is a RUNTIME dispatch, not a fold.

Member-name quoting as a runtime call is also exactly the room a registered
macro cannot occupy: ADR 0023's step 8/9 renames expand through qualified paths
and carry only shape; the expander has no enum tables. ADR 0023's step 6 is the
precedent that fills the same room -- a compiler-implemented `!` helper, its
signature declared by the module that owns the question and its lowering in the
passes. format! sits beside `snprintf`; enum_from! sits beside the enum tables
both backends already lower matches from.

## Ruling

`enum_from!(value) <string>`. The builtinmacros table row is
(`value: EnumType`, exactly one, `<string>`) -- non-variadic, so the arity check
is the analyzer's table check, and the sign of that row is what makes the
expander leave the invocation standing. lib/std/enums.fin owns the declaration
the way stdio.fin owns `@define format!(...)`: a bodiless `@define`, exported by
being a builtin (it resolves with no import at all, like format!, because a
builtin is in no scope to export from), beside the two intrinsics whose module
it completes.

Both backends lower the call the same three ways:

- A member spelling folds. `enum_from!(Fruit::Plum)` and the bare
  `enum_from!(Plum)` both answer "Plum" as a constant: the spellings resolve
  through the tables the file's own member reads already use (the C++
  `enumMembers_`/`enums_`, the stage `find_enum_member_global`), and the
  qualified spelling draws the same name the bare one drew, which is the fact
  ADR 0041's `Fruit::Plum` shape stands on (`MyEnum::B` and `B` name one
  member).
- A value dispatches at run. The concrete enum is read from the value's type
  (fieldless enums ride the C++ Int kind with `enumInfo` attached -- cgDisplay's
  own split -- and the stage reads the local's written annotation); the tag
  selects among the names in declaration order. The C++ emits a switch over the
  members with an unreachable default; the stage emits an icmp/select chain
  that degrades a tag outside the member set to the empty name. The chains
  answer the same names for every value the language can construct: a value is
  built only out of a member, so no constructed tag can reach either
  disagreement, and the two shapes are the backends' own IR habits (the stage's
  own match arms use the same two-limb machinery -- `LLVMBlockArgs` holds two
  limbs, so an N-arm phi is not available there to begin with).
- A payloaded enum answers its tag's name. The tag is element 0 of the tagged
  union (the getkeyid shape, ADR 0041's layout), the payload stays out of view,
  and both members of `R { Ok(int), Err(int) }` answer Ok and Err. The
  "member ops" promise covers both enum shapes because the tag is what this
  op reads.

The refusals, by the ADR 0057 family's wording convention (`'X' on a Y`,
backend-display of the kind): an `int` refuses -- `getkeyid` passes a raw id
through because a raw id IS a tag, and a name has no member to name without an
enum, which is the one place the two intrinsic families honestly part ways; a
string, or any other kind with no enum behind it, refuses the same way. A
genuinely boxed `EnumType` (an any) refuses like getkeyid's own box refusal
today ('getkeyid' on a struct) -- the shared gap, not a new one; ADR 0034's
value-semantics box is the storage story, and the argument arrives concrete
where a value exists.

The stage's arity check lives in its mx pass-through gate: the self-host
checker never sees a macro invocation, and the mx knows the count it just
split, so the gate answers the analyzer's own wording (`Macro 'enum_from!'
expects exactly 1 argument, got N`) before the invocation rides through. The
C++'s analyzer answers the same sentence from the table row. The codegen count
lanes on both sides (`an 'enum_from!' that is not one value`) are the
impossible-pass-disagreement refusals, never a program's first answer.

## Consequences

Sample: `tests/samples/stdlib/enums_from.fin` (`//@ ok`), byte-identical across
finc, stage1, stage2 and stage3: getkeyid(Fruit::Plum) is 2 and keyidof(Pear) is
1 (the first runner pins for the draft pair), the spelling answers Plum, a
variable re-assigned between two prints answers Pear then Plum (a fold could
not answer both), and a payloaded `R::Ok(5)`/`R::Err(17)` pair answers Ok and
Err.

Tests: `Soundness_EnumFrom` in tests/test_soundness.cpp (6 cases: the two
prints, the spelling constant, both payloaded members, the int and the string
refusal each with one error and the exact wording, and the table's arity at
0 and 2).

The stage's empty-name degradation for a non-constructible tag is a recorded
limitation, not a silent choice: it is reachable only from a value no Fin
expression builds (the C++ makes the same state unreachable), and if boxed
any-dispatch over enums ever lands, the owner of that ruling owns this default
too.

Relationships: ADR 0057 (the reflected intrinsics; this is their promised
member op); ADR 0023 step 6 (the format! shape the compile side borrows) and
ADR 0041 (`Fruit::Plum` names the member the bare spelling names, tags are the
ids this op dispatches on); ADR 0034 (EnumType as any-implements: the value
argument arrives concrete, no runtime box is read -- the boxed dispatch is the
gap that stays recorded beside getkeyid's).
