# A declaration marked `#[global]` needs no import, and only `std` may mark one

A declaration carrying the attribute `#[global]` is visible to **every file in the compiler
session**, with no import written anywhere. The attribute is **opt-in per declaration** and
**legal only inside `namespace std`**. Written anywhere else it is a **diagnostic**, not a
warning and not a silent no-op.

The marked set is exactly two names: **`printf` and `format!`**. Nothing else in `lib/std`
carries it, and nothing outside `lib/std` can.

## What question this answers, and what question it does not

The corpus called this "the prelude question" in seven sample notes, and that name hid two
different questions with two different answers. They are:

* **Invention** — a name used bare that no file in the corpus declares. In fifty samples
  that is `printf` and only `printf`. **`#[global]` answers this one, yes.**
* **Ambience** — whether a `pub` declaration in `namespace std` is visible to a sibling file
  that does not import it. **Answered no.** `Any` (`lib/std/types.fin:35`, `pub type Any = any;`),
  `Enum` (`lib/std/enums.fin:17`), `getkeyid` (`:24`) and `keyidof` (`:31`) are each `pub`
  inside `#[export]` inside `namespace std`, and every one of them stays import-only. A file
  that uses one owes an import.

The canonical statement of the split, with both halves argued in full, is the `//@` note on
`tests/samples/enums.fin`. Every other corpus note points at it rather than restating it.

Neither half reaches a name written **nowhere** in the tree. `Offer`, `Strict`, `PathLike`,
`Struct` and `pointer_type` are declared in no corpus file and no `lib/std` file, and ADR 0008
forbids inventing them: ambience cannot make visible what nobody wrote, and `#[global]` marks
a declaration rather than creating one. Those stay refused, and the owner has to say what they
are.

## The measurement that warrants it

`printf` is the whole case, so the case is a count. Measured at `b11bcc8`, over
`tests/samples/**/*.fin` with comment lines excluded:

| | count |
| --- | --- |
| samples that call `printf` | 21 |
| samples that declare or import it | 19 |
| samples that call it **bare** | **2** — `const.fin:68`, `interfaces.fin:18` |

There is no builtin `printf` in the compiler: `grep printf src/semantics` finds nothing, and
`lib/std/stdio.fin:6-9` says so in its own words. So those two sites are undefined names, and
the only three ways to make them resolve are to edit the samples, to invent a builtin, or to
let a declaration reach them without an import. The corpus is the specification (ADR 0008), so
editing it to suit the compiler is backwards; a builtin `printf` would put a C function's
signature inside the compiler, which ADR 0003 rules against for memory and the same reasoning
carries; the third is this ADR.

Earlier notes recorded the first two figures as 16 and 14, and one as 18. Those disagree with
each other and with the table above, and the reason is that "calls `printf`" can be counted
with or without commented-out lines, gated declarations and the two stdlib drafts. **The
number that every independent measurement has agreed on is the two**, and it is the only one
the decision rests on.

## Why an attribute rather than a prelude

A prelude is a file the compiler injects into every translation unit. It would make the
visible set a property of the *compiler's configuration* rather than of the declaration, so
reading `lib/std/stdio.fin` would not tell you whether `printf` is ambient — you would have to
know what the driver did. `#[global]` is written on the declaration, so the file that declares
the name is the file that says how far it reaches.

It also keeps the "beginner" case the owner asked for expressible at the granularity they
asked for it, quoted:

> very good for having a std/io lib but then only making the printf function global for
> beginners but everything else

A prelude is all-or-nothing per file. An attribute is per declaration, so `printf` is ambient
and the eleven other `#[export]`ed names in the same file are not.

## Why `std` only, and why a diagnostic

`#[global]` is a hole in the module system. One name that resolves everywhere is a convenience;
an arbitrary library's ability to mint them is a name-collision generator that no import graph
can explain, and the collision surfaces in a *third* file that imported neither party. Confining
it to `namespace std` means the set of ambient names is bounded by a library that ships with the
compiler and is reviewed with it.

The enforcement is a diagnostic because **an attribute the compiler quietly drops is the same
class of fault as a statement it quietly drops** — the founding rule of this backend, applied
one layer up. A user who writes `#[global]` in their own module and gets silence has been told
their name is ambient when it is not, and will find out from a resolution failure in an
unrelated file.

## The one mechanism this has to span

`src/semantics/Scope.hpp` keeps **three** maps, not one: `symbols`, `types` and `macros`, with
`resolve`, `resolveType` and `resolveMacro` beside them (`define`/`defineType`/`defineMacro` at
`:29`, `:30`, `:32`; the resolvers at `:34`, `:41`, `:47`). The marked set straddles two of
them — `printf` is a symbol and `format!` is a macro — so `#[global]` cannot be implemented in
the symbol table alone.

That is the joint most likely to be got half right, and it is why the marked set is two names
rather than one: with only `printf` marked, a symbol-table-only implementation would look
complete and would fail the day `format!` arrived.

## Consequences

`const.fin:68` and `interfaces.fin:18` are expected to resolve **with no edit to either
sample**. A repair was prepared for each — adding the fourteen-times-verbatim
`@define printf(fmt: string, ...) <noret>;` to a blank line — and both were withdrawn, because
adding text to the specification that the language then makes unnecessary is churn on the
specification, and this corpus is under an obligation not to churn.

`literal_struct.fin:30` is a **third** category that this ADR does not cover: a `printf`
declaration gated inside `if (!@defined("printf"))`, so whether it is declared at all depends
on evaluating the guard. It waits on `defined` becoming a registered operation — the wave-4
shape, not this one.

Loading has to become eager. A name is only ambient if the declaration carrying the mark has
been seen, so the session must load `lib/std` before it analyses the user's file rather than on
demand at an import. The cost objection to that was raised and measured wrong: **a loaded module
is analysed, never code-generated.** All ten modules under `lib/std` are check-only clean
(`finc <module>`, no `-c`, exits 0 for every one), and a probe importing all of `stdio` exits 0
even though `finc -c lib/std/stdio.fin` refuses `IOResult`. Ten analyses, not ten codegens.

`format!`'s visibility was ruled here rather than in ADR 0023 because ADR 0023 ruled `format!`
a compiler **builtin** rather than a declared macro — `tests/samples/stdlib/stdio.fin:35-36`
makes the format string a runtime parameter (`pub fun printf<X: Any<Printable>>(fmt: string,
...objects: [X])` then `format!(fmt, ...objects)`), which a macro taking a literal cannot
serve. Its visibility was the joint that ADR could not settle: `deeptest2.fin` and
`stdlib/error.fin` import **nothing at all** — measured, zero import lines in either — so the
name has to resolve bare. Making the whole macro namespace ambient was the alternative and was
rejected: it would put `format!`'s signature in two places, and one mechanism beats two.

**This ADR was written after the ruling and after the code.** `#[global]` was cited by number
as "ADR 0021" in `docs/HANDOFF.md` and in `docs/adr/0023`'s own text for a day before this file
existed, which is the failure this file closes. A number cited with nothing behind it is a
dangling reference, and the next reader spends their time discovering that rather than reading
the decision.

## The macro half is met by a table, not by `#[global]`, and the joint held anyway

The prediction above was right about the joint and wrong about which mechanism spans it. A
symbol-table-only `#[global]` would indeed have failed the day `format!` arrived — but so would
a three-map one. `#[global]` is read by `publishIfGlobal`
(`src/semantics/impl/Analyzer_Decl.cpp:1023`), which runs during **semantic analysis**, and macro
invocations are resolved by the expander, which runs **before** it (`src/driver/Driver.cpp:174`
against `:189`). A macro published into an ambient scope by the analyzer would be written after
the only pass that had a use for it, so `resolveMacro` would still fail and the call would still
report `Undefined macro`. Extending `publishIfGlobal` to `defineMacro` would have compiled,
passed a test that asserted the map entry, and changed nothing about a call.

So ADR 0023 step 6 spans it the other way: a table of compiler-implemented macros
(`src/semantics/BuiltinMacros.cpp`), and a name in the table needs no scope at all. The expander
treats a failed `resolveMacro` of a table name as not-an-error and leaves the invocation
standing; the analyzer answers it. `format!` therefore resolves in a file that imports nothing
*and* in a compilation where `lib/std` is not on the search path at all — measured: `finc f.fin
--fin-libs <empty dir>` over a file whose only content is `return format!("{}", 1);` exits 0,
where the same conditions give `printf` an `Undefined function or type 'printf'`. That is a
stronger property than ambience, and it is the honest one: nothing about `format!` depends on a
library being present, because nothing about `format!` is in a library.

This does not weaken "one mechanism beats two" — it is that argument applied one level down.
`format!`'s signature lives in one place, the table, and the `@define` line ADR 0023 step 8 puts
in `lib/std/stdio.fin` is documentation for a reader rather than the definition the compiler
consults. The check that keeps them honest is partial and should be described as such: the
declaration's *name* is verified against the table and an unlisted one is refused where it is
written, but its signature is not compared, because the parser keeps a macro parameter's name
and not its declared type. So `@define format!(fmt: int) <int>;` in `lib/std` would be accepted
and would say something false. ADR 0023 named that risk and named the mitigation — a test
comparing the two texts — and it is owed with the line that creates it.

What remains true of the three-map observation is the part about `#[global]`'s *scope*. The
attribute is legal on a macro declaration and is enforced there — `#[global] @define
format!(fmt: string, ...) <string>;` outside `namespace std` is refused by
`refuseMisplacedGlobals` (`Analyzer_Core.cpp:882`), which walks attributes rather than declaration
shapes and so caught the macro form without being told about it. Two `Soundness_GlobalAttribute`
tests carry that exact line, inside `std` and outside it. The mark on a macro is inert as
publication and live as a placement rule, which is a coherent position: the rule about who may
mint an ambient name does not stop applying to a declaration whose ambience comes from elsewhere.
