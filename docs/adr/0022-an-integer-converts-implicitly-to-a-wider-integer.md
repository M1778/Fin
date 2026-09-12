# An integer converts implicitly to a wider integer, and never to a narrower one

`int` → `long`, `int` → `ulong`, `ushort` → `int` are implicit and need no cast. `ulong` → `int`
is a diagnostic. The rule is **width**, taken from the compiler's one scalar table, and it is
about integers only.

Three consequences fall out of the same rule rather than being three rules: a **subscript** may
be any integer, a **comparison** happens in the wider of its two operands, and **arithmetic** on
two widths yields the wider.

## The corpus is the evidence, and the evidence is the absence of casts

`tests/samples/stdlib/stdio.fin` declares `stream_length <ulong>` and `pointer <ulong>` (`:81`,
`:82`, `:96`, `:97`) while every buffer length and loop index around them is an `int`. So
`self.stream_length = _temp.length` (`:130`) and `self.stream_length = data.length` (`:135`) hand
an `int` to a `ulong`, and `nbytes > self.stream_length` (`:110`) compares one against the other.

Eleven such sites sit between `:110` and `:135`, and **not one of them writes a cast.** A language
that intended conversion to be written would have shown eleven casts here, in the one file whose
whole job is to be the standard library's own I/O. `lib/std`'s derived copy has none either. The
corpus is the specification (ADR 0008), and this is what it says.

The owner was asked and accepted this reading rather than supplying it, which matters: the rule is
derived from the corpus and would have been derivable by anyone who counted the casts.

## Why the width and not the sign

`int` → `ulong` loses a negative. The alternative rule — widen only where the sign also agrees —
was rejected because both directions of sign are permitted **when the target is wider**:
`ushort` → `int` loses nothing at all, and `int` → `ulong` loses only a negative, which the corpus
writes deliberately (`stdio.fin:109`'s `nbytes: ulong = -1` is a sentinel the body replaces).

Equal widths pass only when the sign agrees, so `int32` → `int` is the identity it actually is,
while `int` → `uint` stays refused: reinterpreting a sign is not a widening, and no corpus line
writes one.

## One table, not two

Widths come from `scalarByName` (`src/types/Layout.hpp:108`), which is the compiler's **only**
scalar table. Restating them beside the assignability rule is how two answers to one question come
apart. It is also what makes the rule mean *one scalar* rather than *one name*: the aliases resolve
to a width, so `int64` widens to nothing and `int` widens to it, without a second list that could
disagree.

`ScalarKind` (`Layout.hpp:79`) keeps `bool` out on its own — a bool is one bit and its own kind —
and `float`/`double` are left alone, because the ruling is about integers. The one float rule this
corpus needs already exists and is `int` → `float`.

## A negative constant is still not an unsigned value

Widening makes `int` → `ulong` succeed, and the check that would have caught `let x <ulong> = -1`
is `constantFitsType`, which runs only when assignability **fails**. So the rule as stated would
have quietly turned `-1` into a very large unsigned number.

It does not, and the fix is ordering rather than an exception: `checkType` reads the constant
*before* it asks about assignability (`src/semantics/impl/Analyzer_Core.cpp:467`). Two tests hold
it, and they hold it in both the places it has to hold:

* `Soundness_IntegerWidening.WideningDoesNotAdmitANegativeConstantToAnUnsignedTarget` — the
  declaration, across `uint`/`ulong`/`ushort`, with a signed *variable* still widening in the
  same test so the guard cannot be read as revoking the rule.
* `Soundness_IntegerWidening.AComparisonDoesNotAdmitANegativeConstantToAnUnsigned` — the
  comparison, both operand orders and both operator families, because refusing
  `let x <ulong> = -1` while accepting `n == -1` would be the compiler disagreeing with itself
  about one line of one file.

## The disagreement that turned out to be real, and is not about signs

`src/types/PrimitiveType.cpp`'s comment predicted exactly that disagreement, and named
`stdio.fin:109` — `fun read(nbytes: ulong = -1)` — as the site where `-1` is accepted against a
`ulong`. **It still is**, measured at `b11bcc8`, after the two tests above landed.

The reason is not the sign rule. It is that **a default argument is not type-checked against its
parameter's type at all**: `fun f(a: int = "hello")`, `fun f(a: string = 5)` and
`fun f(a: bool = 7)` all compile with exit 0, while the same values in an initialiser are refused.
Booked as `KnownDefect_DefaultArguments.ADefaultArgumentIsNotCheckedAgainstItsParameterType`,
which asserted the defect in both directions so the asymmetry could not be read as two unrelated
facts. **Fixed 2026-08-29** and inverted into
`Soundness_DefaultArguments.ADefaultArgumentIsCheckedAgainstItsParameterType`: the check is
`checkInitializer` in `visitParameterDefaults`, so a default follows this ADR's rules like any
other initialiser, and `stdio.fin:87` and `:109` now carry the same diagnostic `:110` already
did. Nothing in this ADR changed to make that happen — the check was missing a layer up, exactly
as the comment above predicted.

Recorded here because this ADR is where a reader will come looking after reading that comment, and
because it is the second time in this project that a **source comment described a state the tree
had already left**. The comment's other claim — that narrowing the negative case back "is a
separate ruling with no corpus site asking for it yet" — was true when written and is not now; the
two tests above are that ruling.

## Consequences

The three downstream rules are one rule, and the code says so in one place rather than three. An
index is not an assignment, so a `ulong` index into a buffer is admitted by width even though the
narrowing direction stays refused (`src/semantics/impl/Analyzer_Expr.cpp:379`). A comparison and an
addition resolve to the wider operand (`Analyzer_Expr.cpp:620`), which is what lets
`nbytes > self.stream_length` type at all.

`stdlib/stdio.fin` was expected to shed 5 of its 19 diagnostics **with no sample edit**, which is
the point: this ruling moves the compiler toward the corpus rather than the corpus toward the
compiler.

A narrowing still needs a cast, and Fin has no cast syntax ruled on yet. Until it does, a program
that genuinely needs `ulong` → `int` cannot say so — a real gap, and one with no corpus site asking
for it, so it stays open rather than being invented (ADR 0008).

**This ADR was written after the ruling and after the code.** `src/types/PrimitiveType.cpp:15`,
three sites in `src/semantics/impl/`, and five in `tests/test_soundness.cpp` all cited "ADR 0022"
by number while `docs/adr/` jumped from 0020 to 0023. A number cited with nothing behind it is a
dangling reference, and the next reader spends their time discovering that rather than reading the
decision.
