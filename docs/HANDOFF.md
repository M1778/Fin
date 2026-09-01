# Handoff — finishing `finc`

Written 2026-08-26, at commit `91312b8` on branch `wave3-semantics`.

This is a live handoff for a fresh agent picking up the work. It records the things that are
**not** derivable from the repo: the user's standing instructions, the hard constraints on this
working copy, the environment's traps, and the design decisions already made for the units that
have not been written yet. Everything that *is* in the repo is referenced by path, not copied.

Read, in order: this file, then `docs/plan.md` (approved by the user; §"Wave 5 — backend" and
§"Rulings owed" are the live parts), then `git log` — the commit messages are the real design
record and each one states the rule it implemented and why.

---

## 1. What the project is

`finc` is a C++20 compiler for **Fin**, a systems language. LLVM 22 backend (ADR 0010, amended 2026-08-27).

**There is no prose specification. `tests/samples/*.fin` IS the specification** — 51 samples
(50 until 2026-08-28, when the owner contributed `love.fin`; see §8's interface-conversion entry).
Authority is per-expectation, in the `//@` comment at the top of each sample (ADR 0008,
`docs/adr/0008-sample-authority-is-per-expectation.md`). When the compiler and a sample disagree,
the sample wins unless the sample is a typo — and a typo gets **booked, never fixed** (§7).

The founding backend rule, which every commit since has followed:

> **A construct the backend cannot lower must be _refused_, never skipped. A silently dropped
> statement is a miscompile.**

Corollary established during the struct-methods unit: a construct only reachable by *naming* it
(a method, an operator) may be left undeclared and refused at the **use site** instead of at its
declaration. That is what lets `operators.fin` declare a generic operator it never applies and
still be `//@ ok`.

## 2. Standing user instructions — all still binding, quoted verbatim

- "Continue *Don't stop until a big goal is achieved&"
- "Remember: to commit your work as you are progressing"
- "no need to push anything for now. for now keep developing"
- "No whenever an agent is failing just keep resuming it, its the internet connection its fine"
  — and the later "if agents stop for any reason just resume them cause sometimes i get
  connection issues its no big deal". **Read the error before resuming.** On 2026-08-28 both Fin
  agents died on `403 pre-consume quota failed, user quota: $0.803210, need quota: $1.204032`,
  which is *not* a connection issue: a resume spends the remaining balance on requests that fail
  before doing any work. A connection error is worth an immediate retry; a **quota** 403 means
  stop and hand the unit back. Check `git status` and build before respawning onto a tree an
  agent left — §15.8 of MACHINE-NOTES is 380 lines of green work nearly discarded that way, and
  `fe356bd` is a unit recovered rather than lost by doing it.
- "Try going low on agents and spawn less cause we are low on credits"
- "banning research but also instead of spawning 1 agent per section to implement spawning 1
  agent per few sections and tasks saves context and token"
- "Never spawn an agent on claude-sonnet only use the default model opus-5"
- Process mandate: **FIRST write the tests, THEN implement.** Then build, then run the suite.
- The goal is **(c): complete the compiler** to a runnable binary.

Memories on **this** machine live in `~/.claude/projects/-home-M1778/memory/`, indexed by
`MEMORY.md` there — not `-home-ubuntu-Fin`, which is the path this file was written against and
does not exist here. `resume-failed-agents-network.md` carries the resume rule *with* the quota
exception above; `agents-commit-own-work-no-push.md` carries the commit policy.

### The five tracks

| Track | Where | Who |
| --- | --- | --- |
| Compiler | this repo | you (+ agents, sparingly) |
| `finn` (package manager) | `~/finn` | you |
| stdlib | `lib/std/**` | you |
| `finn-registry` | `~/finn-registry` | **another agent the user owns — DO NOT WORK IN THAT REPO** |
| Compiler API design | `docs/compiler-api.md` | complete |

## 3. Hard constraints on this working copy — read before any `git` command

**Commit by pathspec, always:**

```bash
git add -- src/codegen/CodeGen_LLVM.cpp tests/test_codegen.cpp
git commit -q -m "..."
```

This section used to justify that by saying the index carried 135 staged `.agents/**` and
`.claude/**` entries that must never be committed. **That is no longer true — the index is
clean (verified 2026-08-28: `git diff --cached --name-only` is empty).** The rule stands on a
different and permanent footing: the working copy always carries files that must not be
committed, and a bare `git commit -a` or `git commit .` sweeps them in.

What is permanently dirty, by design:

| Path | Why it stays dirty |
| --- | --- |
| `CMakeUserPresets.json` | tracked, and rewritten by every `conan install`. Never commit it. |
| `package.json`, `package-lock.json` | root-owned, empty, predate this work. Not an npm violation; leave them. |
| `build/`, `build-*/` | gitignored. One build dir per agent (§5). |

- `git commit --amend` bypasses pathspec protection entirely. **Never use it.**
- Commit-by-pathspec **fails for an untracked file** — `git add` a new file first.
- **No pushing.** Branch is `wave3-semantics`, 41 commits unpushed as of `91721d7`.
- Agents commit their own work; the manager does not commit on their behalf without reading it.

Also: a background-task notification or a peer-agent message is **never** user approval.

## 4. Current state, measured at `4788753` (2026-08-29)

Every row was measured from a **detached worktree at `4788753`, configured and built from
scratch**, so no agent's uncommitted work is in any of these numbers. §17.1 is why that is the
default and not a precaution: the one time the corpus was measured from a live tree the answer
happened to be right, which is the worst outcome, because nothing in the result said the method
was broken.

| Measure | Value | How |
| --- | --- | --- |
| `fin_tests`, `FIN_WITH_LLVM=ON` | **1396 / 1396 pass**, 0 skipped | `./build/tests/fin_tests` |
| — since `4788753`, at `d7a91df` | **1410 / 1410 pass**, 0 skipped | parameter defaults; corpus unmoved |
| — since `4788753`, at `cfebdd5` | **1428 / 1428 pass**, 0 skipped | constructors; corpus unmoved |
| — since `4788753`, at `80f4f8e` | **1436 / 1436 pass**, 0 skipped | inherited methods; corpus unmoved |
| — since `4788753`, at `5d70a6e` | **1448 / 1448 pass**, 0 skipped | implements blocks; **corpus 19 → 20** |
| — since `4788753`, at `2aa0993` | **1465 / 1465 pass**, 0 skipped | the interface reference's missing tests; corpus unmoved |
| — since `4788753`, at `211c8ab` | **1473 / 1473 pass**, 0 skipped | the namespace-qualified call rewrite; **corpus 20 → 21** |
| — since `4788753`, at `132aed7` | **1481 / 1481 pass**, 0 skipped | the `::` call's type arguments; **corpus 21 → 22** |
| — since `4788753`, at `02fba4a` | **1517 / 1517 pass**, 0 skipped | the variable-refusal location, the width-annotation refusal, `prototype<K, V>`; corpus unmoved |
| — since `4788753`, at `624a061` | **1537 / 1537 pass**, 0 skipped | `foreach`; corpus unmoved, `loops.fin` refuses 27 lines later |
| — since `4788753`, at `418bca0` | **1564 / 1564 pass**, 0 skipped | the nested function declaration; **corpus 22 → 23** |
| — since `4788753`, at `55674d7` | **1566 / 1566 pass**, 0 skipped | the `Error` surface's two tests; corpus unmoved (docs only) |
| — since `4788753`, at `HEAD` | **1573 / 1573 pass**, 0 skipped | the erasure marker moved to the use; **corpus 23 → 24** |
| `fin_tests`, `FIN_WITH_LLVM=OFF` | **1391 ran: 1022 pass / 369 skip / 0 fail** | a second build dir |
| Samples that lower to an object | **20 of 51** | see below |
| Samples blocked in codegen | **11** | see below |
| Samples that never reach codegen | **20** | see below |
| Samples whose front-end expectation is `//@ ok` | **31 of 51** | `Census.ThePassingSampleCountNeverFalls` |

The ceiling is **50**, not 51: one sample is a negative test that must keep failing —
`undefined_behavior.fin`, whose expectation is `error 3:1 "Function 'add' is missing a return
statement on some paths"`.

The last two rows count different things and are both worth keeping. The object column is the
whole pipeline under `-c -o`; the `//@ ok` column is the front end only, which is all the corpus
harness runs (ADR 0008). A sample can be `//@ ok` and still be refused by the backend, and eleven
are.

The twenty that reach an object: `arrays.fin`, `arrays_enums.fin`, `basic.fin`,
`blame_assert.fin`, `deeptest1.fin`, `deeptest3.fin`, `extern_as.fin`, `functions.fin`,
`implements_block.fin`, `love.fin`, `macro_definitions.fin`, `macros.fin`, `macros2.fin`,
`operators.fin`, `simple_pointers.fin`, `struct_methods.fin`, `structs.fin`, `variables.fin`,
`stdlib/networking.fin`, `stdlib/somelib.fin`. The last two are hollow — one is a comment, the
other an empty module kept so directory resolution has a subject.

### The corpus at `cfebdd5` (2026-08-30) — unmoved, and the refusals rewritten

Measured from the live tree with no agent in it, which §17.1 tolerates only because the result was
checked against the `4788753` worktree numbers and matched them row for row at the time:
**19 / 12 / 20**, the same three buckets and the same members. It is **20 / 11 / 20** since
`5d70a6e` — this subsection is kept because its per-sample refusal text is still the queue's, minus
one line. The constructor unit unblocked no sample, for the reason the generic-methods unit
unblocked none — the corpus declares constructors on structs whose *other* refusals sit in front,
so the refusal that went was not any sample's first.

What did change is the queue's own text. Twelve first refusals, re-measured at `cfebdd5`, against
the seventeen §6 then listed — five samples have since moved out of codegen refusal entirely,
and **six of the twelve that remain report something other than what §6 said they did**:

```
complex.fin               the receiver of a call to the method 'printf' on a value with no address
deeptest4.fin             a call with explicit generic arguments
generics_interfaces.fin   the erasure marker 'Castable' on 'T' of a generic function
implements_block.fin      an implements block
interfaces.fin            a call to the method 'to_string' on struct 'User'
lambdas.fin               a variable of type 'fn<...>(T) -> T'
letssee.fin               a '::' call to 'from_angle' on the generic struct 'Vec2' with no type arguments
loops.fin                 a 'foreach' loop
readonly.fin              the attribute 'debug' on field 'v1' of struct 'MyClass'
stdlib/hashmap.fin        struct 'HashMapError' inheriting 'Error', which is not a struct this file lowered
stdlib/prototypes.fin     a return of type '$type'
type_annotations.fin      a variable of type 'prototype<int, float>'
```

Four of those differences are the section's own warning coming true a third time.

- **`readonly.fin` is not gated on inheritance.** Its first refusal is `#[debug]` on a field, at
  `readonly.fin:20`, which sits *in front* of `ChangableSomehow` inheriting anything. Item 3 does
  not unblock this sample by itself; the field-attribute ruling does, and only then does the
  inheritance behind it become the count.
- **`deeptest1.fin` is gone from the list** — it reaches an object now, and the interface
  declaration §6 blames it for is no longer its blocker.
- **`interfaces.fin` is new to the list**, blocked on a method call through an interface. It was
  a front-end error when §6 was written.
- **`implements_block.fin`** refuses the `@implements` block itself, not the interface
  declaration §6 named. **Superseded at `5d70a6e`**: it is OBJECT_CLEAN, and it is the one
  sample the implements-block unit moved. See the next subsection.
- **`arrays_enums.fin`, `blame_assert.fin`, `extern_as.fin`, `functions.fin` and `variables.fin`**
  are all OBJECT_CLEAN and off the queue.

Reproduce with the loop in "Reproducing the numbers", then per sample:
`./build/finc -c "$f" -o /dev/null 2>&1 | sed -r 's/\x1b\[[0-9;]*m//g' | grep 'codegen:' | head -1`.

### The inheritance neighbourhood, measured 2026-08-30 — what item 3 actually is

Item 3 below is titled "struct inheritance" and names `stdlib/hashmap.fin`. Measurement says the
title covers **two unrelated units**, and only one of them was doable. Both halves are recorded
here so the next reader does not re-derive them.

**Doable, and done at `80f4f8e`: a method a struct inherits is callable through it.** In-file
inheritance already spliced the base's fields at offset 0 — that landed with the field work — but
`d.get_a()` for a `get_a` declared on the base reported `codegen: a call to the method 'get_a' on
struct 'Derived' is not lowered yet`, because the lookup walked only `info.decl->methods`. That is
a soundness gap with nothing to do with modules: the corpus writes it at `deeptest2.fin:67-79`
(`Student : <Person>`, with an override) and `love.fin:63,73` (both structs get `name` from
`Person`). Now resolved by a breadth-first walk over the parents, which also serves operators
(`operatorKey` instead of `methodKey`, one template) and static methods reached as `Derived::tag()`.
The base's function is called with the derived pointer unchanged — no thunk, no second body — and
the two cases where that would be wrong are refused rather than misread:

- **A second base's method.** `Both: <P, Q>` puts `Q`'s fields after `P`'s, and `Q.get_q` GEPs at
  the index `q` has in `Q`, which in a `Both` is `p`. The offsets are compared against the data
  layout (`baseSharesLayout`) and the call refuses when they disagree. What a two-base object
  should look like is deeptest2.fin:83's own open question.
- **One name from two bases at the same distance.** Which `f()` `x.f()` means is a language
  question; answering it by declaration order would answer it silently. Refused naming both bases,
  the same way `declareStructs` already refuses a second inherited *field* of one name.

Six tests, all asserting a value: `Soundness_Codegen.AnInheritedMethodIsCalledThroughTheDerived-`
`Struct`, `.AnInheritedMethodWritesThroughTheDerivedObject`, `.AnOverrideWinsOverTheMethodIt-`
`Overrides`, `.AMethodIsInheritedThroughTwoLevels`, `.AnInheritedStaticMethodIsCalledThroughThe-`
`DerivedType`, `.AnInheritedOperatorIsAppliedThroughTheDerivedStruct`, plus the two refusals
`.AMethodOfASecondBaseIsRefusedRatherThanMisread` and `.AMethodInheritedFromTwoBasesIsRefused-`
`RatherThanChosen`.

One thing the walk reaches that the corpus cannot yet: an interface satisfied by an **inherited**
method. `interfaceVtable` now resolves a slot through the hierarchy instead of leaving it null (a
call through a null slot is a jump to address zero rather than a diagnostic), but the analyzer
reports `Struct 'Talker' does not implement interface 'Speaker'` when the implementing method is
the base's — measured — so the shape stops in the front end today. Fixing that is the interface
unit's or the analyzer's, not this one's.

**Not doable, and not at `80f4f8e`: `stdlib/hashmap.fin`.** Its refusal reads like inheritance and
is not one. It is **three stacked blockers, none about the field splice**, and each was measured
directly:

1. **An imported struct is not lowerable at all.** A module's AST lives in
   `ModuleLoader::astStorage` and never reaches the backend; only ambient `#[global]` `@define`
   prototypes are spliced into the root program (`appendAmbientPrototypes`), and deliberately only
   externs — the header's own reason is that copying a Fin function *with a body* "would emit a
   second definition of a symbol the module's own object already publishes, and that is separate
   compilation rather than a splice." So `let e <Error> = …` against `import error::std` gives
   `codegen: a variable of type 'Error' is not lowered yet`, and `len("hello")` against
   `import strings::std` gives `codegen: a call to 'len'`. Inheriting from `Error` is that same
   wall one step earlier. **This is a separate-compilation decision, not a lowering.**
2. **An imported interface is misread as a base struct.** `parentIsInterface` consults
   `interfaceNames_`, which `declareInterfaces` fills from the root program's own
   `InterfaceDeclaration`s only. `hashmap.fin:15`'s `Index` and `IndexAssign` come from a module,
   so with `HashMapError` removed the next refusal is `struct 'HashMap' inheriting 'Index', which
   is not a struct this file lowered` — an interface being counted as a base. Fixed by whatever
   answers (1), because the interface names have the same provenance problem the types do.
3. **`hashmap.fin`'s base is a class.** `lib/std/error.fin`'s `Error` is `#[class]`, refused on its
   own as `the attribute 'class' on struct 'Error'`. ADR 0026 books `class` as its own unit and
   states the backend "cannot simply stop refusing", because `class X {}` parses to a
   `ClassDeclaration` that does not derive from `StructDeclaration`, and that
   `StructDeclaration::is_class` is not the hook because nothing sets it.

Commit `e2e166f` predicted (1) and (3) already: "stdlib/hashmap.fin -> its base `Error` is declared
`#[class]` (stdlib/error.fin:8)… So both now wait on the `class` ruling." So **item 3 unblocks no
sample**, and the corpus was unmoved at `80f4f8e` — 19 / 12 / 20 with the same twelve first
refusals listed above — for the third time in a row, and for the same reason each time: the
corpus's uses of a newly lowered construct sit behind other refusals. The streak broke at the
next unit; see below.

### The implements-block unit at `5d70a6e` — the corpus moved, 19 → 20

The first unit in four to move a sample. `implements_block.fin` is OBJECT_CLEAN, and it was the
one sample the scoping predicted: the analyzer already did **all** of the semantics of an
`implements` block — resolves the target, refuses a non-struct, binds `target_generics`,
`defineMethod`/`defineOperator`/`addConstructor` against the target's `StructType`, then pushes the
interface onto `parents` and runs the conformance check (`Analyzer_Decl.cpp:1384-1630`) — while
codegen refused the whole construct in one line. So this was never interface lowering. It was
**making a block's members reach the code that already declares a struct's own members.**

What was built, all in `CodeGen_LLVM.cpp`:

- **`struct StructExtras`** — a struct's `methods`, `operators`, `constructors` and the `blocks`
  that contributed them, all borrowed pointers, for the reason `StructInfo::decl` is borrowed. It
  is deliberately **not** merged into the `StructDeclaration`: mutating the AST from the backend
  would make the analyzer's view of a program depend on whether it had been lowered.
- **`collectImplementsBlocks(program)`, a new phase before `declareStructs`** — because a block
  writes members *of* a struct, so the declaration `declareStructs` walks does not contain them,
  and both the lowerability checks and the method pass have to see them at the same time they see
  the struct's own. It collects and does not check: a block whose target is not a struct this file
  lowered stays in the map and is refused later by `visit(ImplementsBlock&)`, where the block's own
  line can be blamed — the same division `declareInterfaces` uses. Keyed by the **written** target
  name, so `Result<T, U> implements <IResult>` (`stdlib/typing.fin:27`) is filed under `Result`.
- **`StructInfo::extras`** — attached in `declareStructs`' first pass, because the method pass reads
  it through `StructInfo`; and in `instantiateGeneric`, where `live.extras = extrasFor(tmpl.name)`
  makes the template's blocks that instantiation's, declared once per instantiation.
- **Three helpers out of `declareStructMethods`** — `declareStructMethod`,
  `declareStructConstructor`, `declareStructOperator`, each holding the original body verbatim, run
  over the struct's own members and then the extras. Not a pass of their own, because they are this
  struct's members: same `Struct.name` key, same receiver, same `linkonce_odr`, same deferred body.
- **`findMethod` / `findOperator` consult the extras** after the struct's own declaration, which is
  what makes `s.get_val()`, `p1 + p2`, `S::made()`, `S()` and dispatch through an interface's
  vtable all work through lookups that already existed.

**Four things refuse by name rather than answering wrongly:**

- A block method whose name the struct already declares. `lowerableMethods` and
  `lowerableOperators` were split into per-member helpers sharing **one** `seen` set across the
  struct's own members and the blocks', so the collision is refused as a second definition of one
  symbol instead of quietly losing to whichever was declared first.
- A **second constructor** across the two places one may be written. Two constructors are two
  definitions of `Collection.constructor` however they are spread over the file, and the analyzer
  registers both against the same `StructType` and then selects `constructors[0]` — so the
  ambiguity is real on both sides, and it is the booked `cfebdd5` defect one file wider.
- A block whose target is not a struct this file lowered — an enum, an interface, an undeclared
  name, or a struct that itself refused.
- The **single-member overwrite** form (`X implements <I> = expr`), not collected at all, because
  its right-hand side is a value and not a declaration.

Twelve tests, ten asserting a value and two a refusal message:
`Soundness_Codegen.AMethodFromAnImplementsBlockIsCallable`,
`.AMethodFromAnImplementsBlockWritesThroughTheReceiver`,
`.AnOperatorFromAnImplementsBlockIsApplied`, `.AStaticMethodFromAnImplementsBlockIsCallable`,
`.AConstructorFromAnImplementsBlockRuns`,
`.AMethodFromAnImplementsBlockSatisfiesTheInterfacesVtable`,
`.AnImplementsBlockOnATemplateIsDeclaredPerInstantiation`,
`.AnOverwriterImplementsBlockAddsItsMethodsToo`,
`.AMethodInABlockOfANameTheStructDeclaresIsRefused`,
`.AConstructorInABlockBesideTheStructsOwnIsRefused`, `.AnImplementsBlockOnAnEnumIsRefused`,
`.ASingleMemberOverwriteIsRefused`. Two of them exist because a weaker assertion would have
passed on broken code: a declared-but-never-called constructor prints `0` and still compiles, and
a vtable slot that used to hold null makes a call through it a jump to address zero.

**Two front-end gaps measured beside this unit, neither of them its work:**

1. **A generic argument on the *interface* of a generic-target block does not resolve.**
   `Box<T> implements <IBox<T>>` gives `Undefined type 'T'` at the interface's own type argument,
   then `Unknown interface 'IBox'`. The analyzer binds `target_generics` before it resolves
   `interface_type`, but the interface's arguments are resolved outside that binding. Isolated by
   using a non-generic interface on a generic target, which works — that is what
   `.AnImplementsBlockOnATemplateIsDeclaredPerInstantiation` writes.
2. **A block written above its target** gives `Unknown type 'S' in implements block`. Ordering in
   the analyzer, unchanged by this unit.

The eleven remaining first refusals, measured at `5d70a6e`, are the twelve above minus
`implements_block.fin` and otherwise identical, refusal for refusal.

### The interface reference at `2aa0993` (2026-08-30) — item 4's third bullet was wrong

**It is built.** The queue said no corpus site takes an interface reference as a value and that
nothing measured it, and both halves are false: `love.fin` is such a site, it runs, and commit
`aea960e` (2026-08-28, "Lower interface values and dispatch", 136 lines in `CodeGen_LLVM.cpp`)
implemented all five of ADR 0027's steps. It shipped with **no test asserting a value through a
reference** and its subject line contains a literal `\n\n`, so the commit has no body and left no
design record. That is why this subsection exists and why the seventeen tests below do.

Nothing was fixed here. Nothing needed to be. What follows is the measurement, and the tests are
the part that was owed.

The five sites in `CodeGen_LLVM.cpp`, for the next reader:

- `TypeMapper::map` (505-562) — the interface branch, which answers a two-word `{i8*, i8**}`.
- `declareInterfaces` / `parentIsInterface` (1179-1206) — an interface declares nothing.
- `interfaceVtable` (2884-2935) — the table, `linkonce_odr`, keyed per (implementor, interface):
  one i64 **byte offset** per required field first, then one function pointer per method. It walks
  base structs for a provider it cannot find on the implementor itself, guarded by
  `baseSharesLayout`.
- `convert` (2961-2972) — builds the pair from the implementor's **address** and the table.
- `visit(MemberAccess&)` (6026-6046) — a field read: extract data, extract table, GEP the slot,
  load the offset, byte-GEP, bitcast, load. `emitAddress` (3048-3150) has **no** interface case,
  which is the whole reason a write refuses.

**Measured working, each by compiling, linking and running** — twelve shapes, now twelve
`Soundness_Codegen` tests: an interface-typed parameter; an interface-typed local; a struct
**field** of interface type; assignment to an interface variable; a required **field** read; a
required field at a **shifted** offset; two field-requiring interfaces live on one struct at once;
a write through a method reaching the caller's object; an **inherited** field satisfying a
requirement, and one at a non-zero offset in the parent; a reference passed on to a second
function unchanged; and a **generic struct** as the implementor (`G<T> implements <Speaker>`,
which is where this unit meets `5d70a6e`'s extras).

**The four edges ADR 0027 lists as undecided, measured as they stand today.** Three refuse and one
is a front-end type error; none is miscompiled, and three now have tests:

1. **A temporary with no address** — `hear(make())` gives `codegen: an interface conversion from a
   value without an address`. Correct as it stands: the data word needs an address, and a spilled
   copy would silently break the write-through-a-method case above.
2. **Is a required field writable through the reference** — `x.a = 12` gives `codegen: an
   assignment to this target`. `emitAddress` has no interface-member case. Refused, not dropped,
   which is the founding rule; it stays refused until the ruling lands, and `readonly` is the half
   that has to be decided first, since a field requirement says nothing about mutability today.
3. **Interface-to-interface conversion** — `let n <Narrow> = w;` where `Wide` requires everything
   `Narrow` does gives the front end's `Type mismatch: expected 'Narrow', got 'Wide'`. No corpus
   witness. The test asserts it is the **front end** and specifically not a codegen refusal,
   because a `Wide` that reached `convert` as if it were a struct would take the *pair's* address
   for the data word and build a reference to a reference.
4. **Reference equality** — untested, unwitnessed, unchanged.

**Two defects booked, both `KnownDefect_Codegen`:**

- **A generic interface as a value type refuses.** `let b <Box<int>>` gives `codegen: a variable of
  type 'Box<int>' is not lowered yet`. Root cause located: `TypeMapper::map` tests
  `!node->generics.empty()` **before** `interfaces_->count(node->name)`, so the name is sent to
  `instantiateGeneric` as a struct template, finds none, and refuses — the interface branch is
  never reached. The fix is to ask "is this an interface" first. Booked rather than done because no
  corpus site needs it: `IResult<T, U>` (`stdlib/typing.fin:27`), `rptr_iface<T>` and `GetVal<T>`
  are all written as bounds or in `implements` clauses, never as the type of a value, and ADR 0008
  makes the corpus the specification. A *non-generic* interface whose fields have generic types is
  a different path and already works — that one is conformance, not a value.
- **An escaping reference is accepted and reads a dead frame.** `make` converts a local and returns
  the pair; the caller prints garbage, and `objdump` shows `lea -0x8(%rsp),%rax` — the data word
  points into the frame `make` just left. Booked, and **not as an interface defect**: the same
  program with a plain `&D` in place of the interface is accepted just as happily (measured four
  ways, and a `noise()` call in between makes the plain pointer print `222`). There is no lifetime
  analysis anywhere in the pipeline, which is the same fact §8's `#[slaveof]` ruling turns on (ADR
  0003 — nothing frees implicitly, so nothing today can state how long anything lives). The test
  holds **both** halves and asserts only that each compiles, because a dead frame's contents are
  not a specification: if the interface half ever refuses while the pointer half still compiles,
  the refusal was written in the wrong place.

**One front-end gap, already booked in §7:** an interface satisfied by an **inherited method**
gives `Struct 'Talker' does not implement interface 'Speaker'` (`Analyzer_Decl.cpp:537`). The
backend's `interfaceVtable` already resolves such a provider through the hierarchy — the test
`.AnInheritedFieldSatisfiesARequirementThroughTheReference` proves the field half works — so this
is the analyzer's half only.

**Corpus unmoved: 20 / 11 / 20, refusal for refusal.** No compiler source changed in this unit,
only `tests/test_codegen.cpp` and this file, so it could not have moved. Suite **1448 → 1465**.

The seventeen tests, all in `tests/test_codegen.cpp` under the section "An interface as the type of
a value: the two-word reference": `Soundness_Codegen.AnInterfaceTypedParameterCallsTheImplementorsMethod`,
`.AnInterfaceTypedLocalCallsTheImplementorsMethod`, `.AStructFieldOfAnInterfaceTypeHoldsTheReference`,
`.AssigningToAnInterfaceVariableRebindsBothWords`,
`.AFieldRequiredByAnInterfaceIsReadThroughTheReference`,
`.ARequiredFieldAtANonZeroOffsetIsReadThroughTheReference`,
`.TwoInterfacesOnOneStructGetTheirOwnTables`, `.AMethodCalledThroughAReferenceWritesToTheOriginal`,
`.AnInheritedFieldSatisfiesARequirementThroughTheReference`,
`.AnInheritedFieldAtANonZeroOffsetIsReadThroughTheReference`,
`.AnInterfaceReferenceIsPassedOnUnchanged`, `.AGenericStructConvertsToAnInterfaceItImplements`,
`.AConversionFromAValueWithNoAddressIsRefused`,
`.AWriteToAFieldThroughAnInterfaceReferenceIsRefused`, `.AConversionBetweenTwoInterfacesIsRefused`,
`KnownDefect_Codegen.AGenericInterfaceAsAValueTypeIsRefused` and
`.AnEscapingInterfaceReferenceIsAcceptedLikeAnyEscapingAddress`. Three of them exist because a
weaker version would pass on broken code: the shifted-offset read (a table storing `0` prints
`99`), the inherited field at a non-zero offset (`a` sits at 0 in both layouts, so the plain
inherited test cannot tell "found in a parent" from "at the right offset"), and the write through a
method (a conversion that spilled the struct into a fresh slot would compile, link, run and print
`0`).

### The namespace-qualified call at `211c8ab` (2026-08-31) — item 5 is done

**`complex.fin` lowers to an object, and the corpus is 21 / 10 / 20.** It is the only sample that
moved; the other 50 kept their bucket line for line. `deeptest4.fin`, which item 5 named as the
second import sample, **was already past the import** — see the correction at the end.

The queue said the fix is not in codegen and that the analyzer should rewrite the call. That is what
this is, in four parts:

- **`SemanticAnalyzer::lowerModuleCall`** (`Analyzer_Expr.cpp`, immediately above
  `visit(MethodCall&)`) builds a `FunctionCall` on the member's own Fin name and leaves it on the
  node. The argument list and the generic arguments are **moved** out of the `MethodCall`, not
  copied: two owners of one argument expression would be walked twice by anything structural, and
  the arguments have already been checked in place.
- **`MethodCall::resolved_call`** (`src/ast/exprs/FunctionCall.hpp`) is the slot it goes in. The
  qualifier is *spent* there, in the same sense `dropConsumedImports` spends an import.
  Recorded on the node rather than replacing the node in its parent's `unique_ptr` slot, because the
  tree has no traversal that yields slots — `forEachChild` yields `ASTNode&`, and the two ways to
  build one (a second exhaustive switch beside `forEachChild`, or a second copy of
  `SubstitutionVisitor`'s 55 overrides) each duplicate the tree's shape with nothing to keep the
  copies in step, which is the failure ADR 0004 exists to remove. `StructuralWalk.cpp`'s
  `MethodCall` case emits it — and `args` is empty in that case, so every argument is still emitted
  exactly once, through the resolved call — and `CloneExprs.cpp` clones it, because cloning happens
  after semantics.
- **`CodeGen_LLVM::visit(MethodCall&)`'s first statement** delegates to it and returns, ahead of the
  `generic_args` refusal and ahead of `baseAddress`. Delegated rather than re-implemented, so a
  rewritten call goes through exactly the same argument conversion, vararg promotion and template
  selection a written one does.
- **The gate is `Symbol::is_ambient`**, set in `Analyzer_Decl.cpp`'s `visit(DefineDeclaration&)`
  where the prototype was retained.

**Why that gate and not "the member resolved".** Exactly one mechanism puts a module's declaration
into the root program the backend walks: ADR 0021's `#[global]`, retained by
`ModuleLoader::retainAmbientPrototype` and spliced by `appendAmbientPrototypes`. Rewrite anything
else and the plain name the backend is handed is bound to nothing, so a codegen refusal is traded
for a **link failure** — which is the worse failure, because it arrives after a compile that
exited 0.

**The mark is not the fact, which is why three signatures changed.** `#[global]` is what *asks*;
the retention is what the backend will actually be given, and the two differ when two modules
publish one name. That is legal — `publishIfGlobal` refuses a second `#[global]` of a name only when
the two *types* differ — and retention is first-wins in import order, so the second module's
declaration is marked and is **not** the one the root program will declare. So
`retainAmbientPrototype` now returns whether `decl` is the declaration the splice will carry (false
for a second publication under a different symbol, true for an identical redeclaration), the
analyzer re-defines the symbol with `is_ambient = true` only when it returns true, and
`ModuleLoader::symbolOf` reads `#[llvm_name]` the way `CodeGen_LLVM::symbolNameOf` does —
deliberately a second copy of three lines rather than a dependency from the loader on the backend.
`Soundness_Modules.AQualifiedCallReachesTheSymbolTheRetainedPrototypeNames` is what goes red if the
two readings ever drift apart.

**Why the fact lives on `Symbol`.** It is needed from the *other* side: a file that writes
`stdio.printf(...)` resolves the module's symbol through the module's scope, and whether that call
can be rewritten turns on whether the root program will declare the plain name for *that*
declaration. A `Symbol` carries a name, a type and two flags — no declaration pointer — so nothing
else could answer it, and it is defaulted `false` so the twenty-two aggregate initialisations
elsewhere mean what they meant.

**The two `printf`s are held apart by ordering, and neither is picked.** `checkCallArguments` runs
before `lowerModuleCall`, always — a rewrite performed first would hand a bare `printf` to the
check, which would find the file's own declaration and pass. Measured in one file that declares
`@define printf(fmt: string, ...) <int>;` and imports the bundle's `<noret>` one:
`let a <int> = stdio.printf("hi\n")` gives `Type mismatch: expected 'int', got 'void'`, and
`let a <int> = printf("hi\n")` in that same file is clean. Each spelling keeps its own declaration.
`complex.fin` writes both (`:14` and `:16`), builds, and prints `Big`.

**Measured, each by compiling and running the program:**

- `complex.fin` itself: `-c` clean, and the built executable prints `Big`.
- A value through a rewritten call in two expression positions — initialising a local and as an
  argument to another call: a module publishing `#[llvm_name="abs"] #[global] @define c_abs(v: int)
  <int>;` gives `5 9` for `am.c_abs(0 - 5)` and `am.c_abs(0 - 9)`. The symbol is what makes those
  answers evidence: a rewrite that dropped its argument prints something else.
- `import stdio::std as stdio; stdio.printf("%d\n", 41 + 1);` prints `42`.
- Arity is still checked through a qualifier: `am.c_abs()` gives `Function 'am.c_abs' expects 1
  arguments, got 0`, still naming the qualifier the program wrote.
- The gate holds in **both** directions. An imported extern that is not `#[global]`
  (`stdio.io_fflush(null)`) and an imported Fin function (`stdio.println("x")`) keep today's
  refusal, `the receiver of a call to the method '…' on a value with no address`. The
  discriminating case: a root file that declares `@define io_fflush(handle: &void) <int>;`
  **itself** and calls `stdio.io_fflush(null)` **still refuses** — so the gate is not "the root
  program binds this name", it is "binds this name to this declaration".
- Two modules publishing `twin` under different symbols (`abs`, and `fin_no_such_symbol` so the
  loser cannot answer plausibly): the first imported rewrites and prints `7`, the other refuses
  rather than silently reaching `abs`. Swapping the two imports swaps which, which is what says
  retention is first-wins rather than alphabetical.

**What did not change.** Codegen still holds zero references to `NamespaceType`, and
`visit(ImportModule&)` still refuses any import that reaches it. The only new backend line is the
delegation.

**Two gaps stay refused and are booked in §7**, both with `KnownDefect_Modules` tests: an imported
extern that is not ambient, or any imported Fin function, is not lowered through a dot; and a file
that redeclares an ambient name under a symbol of its own breaks the qualified spelling and the
plain one **alike**, both at the link. The second is `#[overwrite]`'s question and must not be fixed
on the qualified side alone — making `a.twin(…)` link while `twin(…)` in the same file does not
would be worse than both failing.

Ten source files: `Scope.hpp`, `ModuleLoader.hpp`/`.cpp`, `Analyzer_Decl.cpp`, `Analyzer_Expr.cpp`,
`SemanticAnalyzer.hpp`, `FunctionCall.hpp`, `StructuralWalk.cpp`, `CloneExprs.cpp`,
`CodeGen_LLVM.cpp`. Suite **1465 → 1473**: eight tests in `tests/test_stdlib.cpp` under "Lowering a
call written through a module qualifier" —
`Soundness_Modules.AModuleCallIsCheckedAgainstTheModulesSignatureAndNotTheFilesOwn`,
`.AnAmbientMemberCalledThroughAQualifierIsStillCheckedForArity`,
`.TheModulesPrintfLowersAndRunsThroughAQualifier`, `.AQualifiedCallLowersWhereverAnExpressionGoes`,
`.AQualifiedCallReachesTheSymbolTheRetainedPrototypeNames`,
`KnownDefect_Modules.AnImportedExternThatIsNotAmbientIsNotLoweredThroughADot`,
`.AnImportedFinFunctionIsNotLoweredThroughADot` and
`.RenamingAnAmbientNameInTheRootFileBreaksBothSpellingsAlike`. The last four are the reason the
first two are not decoration: each of them passes against a wider gate only by failing at the
linker instead.

**The correction item 5 was owed.** `deeptest4.fin` does not refuse an import and has not for some
time: its first refusal is `codegen: a call with explicit generic arguments is not lowered yet` at
`:11`, `let a <auto> = HashMap::<string, Data>();`. That is item 6's neighbourhood, not item 5's,
and behind it sits the imported-declaration gap above — `HashMap` is declared in
`lib/std/hashmap.fin` and nothing puts a module's struct into the root program — so what it refuses
*after* explicit generic arguments lower is unmeasured.

### The `::` call's type arguments at `132aed7` (2026-08-31) — item 6 is done

**`letssee.fin` lowers to an object, and the corpus is 22 / 9 / 20.** It is the only sample that
moved; the other 50 kept their bucket. The suite is 1481.

**Item 6's premise was incomplete, and the correction is the whole design.** It said the missing
piece is "inferring `T` from the arguments, the same inference a free generic call needs". That is
true of exactly one of `letssee.fin`'s three `::` calls:

| Site | Call | What says which `Vec2` |
| --- | --- | --- |
| `:73` | `Vec2::normalize(scaled)` | the argument — `normalize(ptr: &Self)`, so `Self` unifies with `&Vec2<float>` |
| `:59` | `Vec2::from_angle(0.7854)` | the **annotation** — `from_angle(angle: float)` mentions no `T`, so the `0.7854` binds nothing |
| `:77` | `Vec2::zero()` | the **annotation** — the call names no type at all |

An annotation is not something codegen has. `hintFor` is a front-end mechanism, the analyzer already
runs it, and two of these three sites have nothing else. So the fix could not be argument inference
in the backend, and it is item 5's division instead: **the front end resolves, the backend lowers
what was resolved.**

- **`SemanticAnalyzer::recordResolvedTarget`** (`Analyzer_Expr.cpp`, immediately above
  `visit(StaticMethodCall&)`) takes the instantiation `checkGenericCall` already computed and leaves
  a `TypeNode` for it on the call.
- **`checkGenericCall` gained a trailing `std::shared_ptr<Type>* ownerInstanceOut`**, reported
  *before* the substitution that follows it, so its one early return still hands the instantiation
  over. The caller cannot reconstruct the receiver from the return value: `Vec2::normalize(scaled)`
  returns `noret`.
- **`StaticMethodCall::resolved_target`** (`src/ast/exprs/FunctionCall.hpp`) is the slot, beside
  `target_type` and not written into it. `target_type` is what the source says and is what a
  diagnostic about the target points at; an inferred argument has no source spelling to point at, so
  overwriting the written node would move a caret onto text nobody wrote. `StructuralWalk.cpp` emits
  both and `CloneExprs.cpp` clones it — the same treatment `MethodCall::resolved_call` gets.
- **`CodeGen_LLVM::visit(StaticMethodCall&)` chooses between the two nodes in one line** and then
  reads only its choice, refusal messages included. Null means the written target, which is every
  spelling that already worked — a non-generic struct, `Self`, `Box::<int>::zero()` — so those go
  through the same `map()` of the same node they always did.

**Why recording a type on an AST node is sound here.** Codegen does not clone a template's body:
`Emitter::instantiateGeneric` and `instantiateGenericMethod` emit **the same nodes** once per
instantiation, under a `ScopedBindings` substitution in the `TypeMapper`. So a *concrete* type
stamped on a node inside a template would be right for at most one of the emissions that read it.
What is recorded is a `TypeNode`, and `spellType` (same file) spells a type **parameter** as its own
bare name — after which the recorded node is indistinguishable from a written one: `TypeMapper::
boundBinding` resolves the name through whichever substitution is live, and `Emitter::displayName`
keys the instantiation on what it was bound to, exactly as it does for a hand-written `Box<T>`.

Two guards keep that from becoming a licence to record anything:

- **`spellType` returns null for a type it cannot spell** — a `SelfType`, a function type, a
  prototype, `any`, the error sentinel, the type of `null`. Null records nothing and the backend
  refuses as before. The distinction it draws is worth keeping straight: *null* means "this analyzer
  could not say what the type is", and a node the **mapper** rejects means "the type is this, and the
  backend does not lower it yet" — so `auto`, a `$`-meta-type and a dynamic `[T]` are spelled and
  refused downstream, where their own messages are.
- **`everyGenericParamResolvesHere` compares a parameter by identity, not by name.** This is the one
  real hole and it is closed:

  ```fin
  struct Box<T> { static fun zero() <&Self> { return new Self{}; } }
  fun bad<T>(x: T) <noret> { Box::zero(); }
  ```

  Nothing binds `Box`'s `T`, and `bad`'s `T` is a *different parameter that shares its spelling*. On
  names alone this records `Box<T>`, the mapper binds it to whatever `bad` was instantiated at, and
  `Box::zero()` lowers as `Box<int>` — an instantiation nobody in the program asked for. Compared by
  identity against `Scope::resolveType` at the call, it records nothing and refuses.
  `Soundness_Codegen.AStaticCallOnAGenericTargetDoesNotBorrowItsCallersTypeParameter` calls `bad`,
  because a template nobody instantiates is never emitted and would pass for the wrong reason.

**Eight tests, five of them asserting a value** (`tests/test_codegen.cpp`):
`Soundness_Codegen.AStaticCallOnAGenericTargetTakesItsTypeArgumentFromTheAnnotation`,
`.InfersItsTypeArgumentFromAnArgument`, `.InfersItsTypeArgumentFromASelfArgument`,
`.InsideATemplateResolvesAtEachInstantiation`, `.ReachesAMethodOfTheInstantiationItResolved`,
`.WithNothingToInferFromIsRefused`, `.DoesNotBorrowItsCallersTypeParameter`, and
`Soundness_Codegen.AStaticCallsTurbofishAfterTheMethodIsStillRefused` (each of the middle names is
prefixed `AStaticCallOnAGenericTarget`). The fourth is the one that measures the soundness claim:
one `Box::of(x)` node inside `fun wrap<T>`, two instantiations, `7` and `A` out. Before the
parameter-spelling arm existed it refused **twice** — once per emission of the one node — which is
the safety property stated as a measurement.

**Three refusals in this neighbourhood are untouched, and none of them is this unit's.** All three
are the analyzer binding nothing, not the backend failing to read it:

```
Box::make::<int>(9)     a '::' call to 'make' with explicit generic arguments   nullifier.fin:28
HashMap::<string, Data>()   a call with explicit generic arguments               deeptest4.fin:11
Box(7)                  a call to 'Box'                                          (no corpus site)
```

The first is the turbofish *after* the method name, which binds the **method**'s parameters and not
the target's; `StaticMethodCall::generic_args` is not read by the inference that fills
`resolved_target`. The second is a generic **constructor** call, which is a `FunctionCall` whose name
is a struct's — item 5's rewrite neighbourhood, with the imported-struct decision behind it. The
third is the same shape with inference instead of a turbofish. `nullifier.fin` is FRONTEND_ERROR, so
its line is unreached in the corpus; `deeptest4.fin`'s is a first refusal and still counts.

**`letssee.fin` prints wrong numbers, and the cause is in the sample.** It reaches an object under
`-c`; linked (it needs `-lm`) it runs and prints all six lines, but `Length of a` is
`-76854900708868096.000000` and `Vec2::normalize` appears to do nothing. Neither is a `::` fault.
The sample declares `@define sqrt(f: float) <float>` at `:6` against libm's
`double sqrt(double)` — an **ABI mismatch in the sample's own prototype**. Measured with two probes:
the `float` spelling reproduces that exact value, and `@define sqrt(f: double) <double>` prints
`5.000000`. `normalize` follows from it — `len` is negative garbage, so its
`if (len > cast<float>(0))` guard is false and it returns having changed nothing. **The corpus is the
specification (ADR 0008), so this is a measurement to record and not a sample to edit**; what it
wants is either implicit float→double promotion at a vararg/extern boundary or a ruling that
`@define` must match the C declaration, and both are §8's.

**The corpus at `132aed7`, all 51 measured** — 22 OBJECT_CLEAN, 9 CODEGEN_REFUSED, 20 FRONTEND_ERROR.
The nine, with their first refusal re-measured here:

```
deeptest4.fin             a call with explicit generic arguments
generics_interfaces.fin   the erasure marker 'Castable' on 'T' of a generic function
interfaces.fin            a call to the method 'to_string' on struct 'User'
lambdas.fin               a variable of type 'fn<...>(T) -> T'
loops.fin                 a 'foreach' loop
readonly.fin              the attribute 'debug' on field 'v1' of struct 'MyClass'
stdlib/hashmap.fin        struct 'HashMapError' inheriting 'Error', which is not a struct this file lowered
stdlib/prototypes.fin     a return of type '$type'
type_annotations.fin      a variable of type 'prototype<int, float>'
```

Every one of those lines is unchanged from the `cfebdd5` re-measurement except that `complex.fin`
and `letssee.fin` are no longer on it. **Nothing regressed:** no sample moved to a worse bucket.

### The prototype and the width annotation at `02fba4a` (2026-08-31) — item 7 is done

**No sample moved, and the corpus is still 22 / 9 / 20.** The suite is 1517. `type_annotations.fin`
is the sample item 7 named and it is still CODEGEN_REFUSED, but **for a different reason and one line
earlier**: its first refusal was `a variable of type 'prototype<int, float>'` at `:14` and is now
`a variable of type 'int{64}'` at `:5`. The prototype at the bottom of that file lowers; the width
annotation four declarations above it does not, and now says so.

**Item 7 was stale in both halves.** Its `[int]` half is recorded as "blocked on an owner ruling for
the representation of a dynamic `[T]`" — §8 records that ruling as made on 2026-08-27, ADR 0025's
`{ptr, len}` has been implemented end to end since, and `arrays_enums.fin` has been OBJECT_CLEAN for
several commits. So the only work under item 7 was the prototype, and it is three separate things,
done in this order because the second is a correctness fix that had to precede the feature.

**(a) A variable's refusal was reported at `1:1`.** `declaration_body`'s six variable productions
never called `setLoc(@$)` while `variable_declaration`'s six did, and `annotated_declaration` masked
it — so a bare `let v <any>;` blamed the top of the file while `#[slaveof($Fin)] let v <any>;` on the
same line reported correctly. Three tests in `Soundness_DiagnosticLocation` hold it: the bare local,
the attributed one that already worked (kept, so a regression localises), and a global on a line that
is deliberately not line 1. `any` is the type they use, because there is no representation for a
value whose type is unknown at compile time and so the test cannot quietly stop testing a location
the way `ARefusalNamesTheLine` did three times.

**(b) `int{64}` built an object and emitted an i32.** The annotation is written in exactly one place
(`parser.y`'s `base_type LBRACE expression_list RBRACE`) and read in five, none of them in codegen;
`Analyzer_Core.cpp` walks the expressions and hands back the *unannotated* type. So the backend never
saw the width and lowered the base type. **That is a different kind of defect from the front end's**,
which is why this was fixed rather than booked next to `KnownDefect_IntegerWidths`: a front end that
does not narrow gives a program a value it did not ask for, and a backend that lowers `int{64}` as an
i32 gives it a *machine* it did not ask for, on a compile that exits 0. `TypeMapper::map` now returns
`nullopt` for any type carrying an annotation, and `spell` renders it — `int{64}`, `int{...}` for a
non-constant one, `&int{32}`, `[int{64}, 2]` — so the reader is told which spelling was refused. One
check covers every role because every role goes through that one function; eleven tests measure the
eight (variable, parameter, return, struct field, pointee, array element, cast target, global), the
positive without the annotation, the non-constant spelling, and that no object is written.

Note the asymmetry this uncovered: a width **alias** (`type u64 = uint{64}`) already refused, for an
unrelated reason — codegen does not resolve aliases. So the alias sites in `lib/std/types.fin` and
`stdlib/memory.fin` were never the miscompile. The **direct** spellings were.

**(c) `prototype<K, V>` lowers as `{ [K], [V] }`.** Derived, not chosen:
`tests/samples/stdlib/prototypes.fin` is normative and says `prtp.0` is the keys and `prtp.1` the
values; the analyzer already types those two as `[K]` and `[V]` (`Analyzer_Expr.cpp`, the positional
path); and a dynamic `[T]` is ADR 0025's `{ptr, len}`, which lowers. Anything else would make `.0`
*build* an array at a size only a run time knows. Each half is built through the same
`llvm::StructType::get` shape `mapArray` produces, so LLVM uniques it and `p.0` assigns to an
`[int]` — which is what `APrototypeHalfIsADynamicArrayAndAssignsToOne` measures, and what would fail
if the two ever built the pair differently.

What is in: `CgType::Kind::Prototype` with `keys`/`values`, `mapPrototype`, the literal (two
`buildDynamicArray` calls, all keys then all values, in written order), the `.0`/`.1` projection both
through an address and out of a value with no home, and the variable, parameter, return, struct-field
and bare-global roles. `visit(ArrayLiteral&)`'s dynamic branch was factored into
`buildDynamicArray(node, type, elements)` so the prototype literal builds its halves through the same
code — two copies would be two allocation rules and two pair layouts that agree only today. That
factoring also fixed a nested literal: a `prototype<int, [int]>`'s values are array literals and were
refused for want of a hint, in a program whose author wrote no array declaration to be missing.

Every uncovered case refuses with its open question named:

| Refused | The question behind it |
| --- | --- |
| `p[10]`, `p[11] = 2.5` | an equality over an arbitrary key type and a search — prototype *access*, `prototype_test.fin`'s own note; the store also has to grow both buffers |
| `{int}`, `{int, float, char}` | the analyzer accepts both and defaults a missing half to `any`; which of "the value is `any`", "it is a set" and "it is an error" Fin means is unruled |
| `{object, object}`, `{int, any}` | no representation for an erased value, so none for an array of them — the same refusal a bare `let v <any>;` gets |
| a literal with no declared type | `{ 10: 1.5 }` is not `prototype<int, float>` by inspection; `<{long, double}>` accepts the same text |
| an extern parameter, a C variadic argument | there is no C type the pair-of-pairs is the ABI of |
| a global with a literal initialiser | the initialiser is a malloc and two stores, and where a global's run-time initialiser runs is open; the bare declaration lowers, which is the pair that says so |
| `p.2` | the backend checks the position against 2 as well, so the two ends agree without depending on each other |

`prototype_test.fin` and `stdlib/prototypes.fin` both stay CODEGEN_REFUSED and neither is refused for
storage any more: the first is `{object, object}` and `a.rm("b")`, the second `a return of type
'$type'` (item 8). Adding a `Kind` was safe to do: exactly two switches over it exist (`cgDisplay`,
`describe`), both were updated, and there is no `-Wswitch`/`-Werror`.

**The corpus at `02fba4a`, all 51 measured** — 22 OBJECT_CLEAN, 9 CODEGEN_REFUSED, 20 FRONTEND_ERROR.
The nine, with their first refusal re-measured here:

```
deeptest4.fin             a call with explicit generic arguments
generics_interfaces.fin   the erasure marker 'Castable' on 'T' of a generic function
interfaces.fin            a call to the method 'to_string' on struct 'User'
lambdas.fin               a variable of type 'fn<...>(T) -> T'
loops.fin                 a 'foreach' loop
readonly.fin              the attribute 'debug' on field 'v1' of struct 'MyClass'
stdlib/hashmap.fin        struct 'HashMapError' inheriting 'Error', which is not a struct this file lowered
stdlib/prototypes.fin     a return of type '$type'
type_annotations.fin      a variable of type 'int{64}'
```

Only the last line changed from `132aed7`, and it changed *within* the same bucket. **Nothing
regressed:** no sample moved to a worse bucket, and the same 22 reach an object.

### `foreach` at `624a061` (2026-08-31) — one of item 8's seven

**No sample moved, and the corpus is still 22 / 9 / 20.** The suite is 1537. `loops.fin` is the
sample this unit was named for and it is still CODEGEN_REFUSED, **for a different reason and 27 lines
later**: its first refusal was `a 'foreach' loop` at `:19` and is now `a call to 'recursive'` at
`:46`. Both `foreach` sites lower; the **nested function declaration** at `:40` does not, and that is
a construct no §6 item names — it is booked below.

**The lowering is derived from two corpus sites and one library note.** `tests/samples/loops.fin:19`
and `:24` are the only `foreach` in the corpus, both over the fixed `[int, 5]` declared at `:12`, one
in each binding form. `:20` is `blame element == a[idx]`, and that single line fixes three things: the
element the loop binds is the element at `idx`, the index counts from 0 in step with it, and the
binding is a **copy** of the element rather than an alias (an `int` compares equal either way, but
only a copy makes the one-binding and two-binding spellings the same loop). `lib/std/collection.fin`
:59 and `lib/std/hashmap.fin`:316 both record that **there is no iteration protocol** and that
index-based iteration is what those types support — so an array is the whole of what is iterable, and
that is a ruling already recorded rather than a gap here.

So the shape is `visit(ForLoop&)`'s, because that is what this is: a counter, a comparison against a
bound, an indexed load, and an increment `continue` reaches. What it *adds* is that the counter and
the bound are the loop's own, so the two cannot disagree the way `for (i: int = 0; i < a.length - 1;
i++)` (`loops.fin:14`, which walks four of five elements) demonstrably can.

Four details are decisions and are worth naming:

- **The iterable is reached once, before any block exists, and for its address.** Once because it is
  an expression and may be a call — emitting it in the condition would turn a walk of one array into
  a walk of N fresh ones. Through `baseAddress(iterable, Kind::Array)` because indexing needs a home
  (LLVM's `extractvalue` takes a constant index, so an array that is only a value cannot be walked at
  all) and because that one call is what makes **every home an array has** iterable with no case of
  its own: a local, a global, a parameter, a struct field, a prototype half, and a pointer to an
  array — `deeptest3.fin:111`'s rule that the base is dereferenced first, the same door `a[i]` and
  `a.length` already use.
- **The bound is read once, before the first iteration.** A fixed array's is the constant extent; a
  dynamic array's is the length word out of ADR 0025's `{ptr, len}`. A body that replaced the array it
  is walking would keep walking the one it started with. The corpus writes no such body, and the day
  it does is the day that is a ruling rather than a consequence.
- **The counter is a signed `int`** — the type `.length` answers with
  (`Soundness_Members.ALengthIsAnIntAndNotAnotherIntegerWidth`) and the type the pair's length word
  already is, so the comparison needs no conversion and cannot acquire a signedness this file did not
  choose. An index binding of any other integer width is converted **from** it, which is why
  `long`, `uint`, `ulong` and `char` all work.
- **The binding's written type has to *be* the element type.** Nothing before the backend checks it:
  the analyzer defines both bindings from what was written and never asks the iterable what it yields
  (`KnownDefect_Foreach.ABindingTypeIsNeverCheckedAgainstTheIterable`). So `foreach (e <string> in a)`
  over an `[int]` arrives here as a well-typed program, and *converting* it would read four bytes of
  an integer as a pointer. Refused — including where a conversion exists, because `<long>` over an
  `[int]` would make `e` a different value from `a[idx]` and `loops.fin:20` asserts they are the same.

Every uncovered case refuses with its open question named:

| Refused | The question behind it |
| --- | --- |
| `foreach (e <int> in 5)`, over a `bool`, a `string`, a struct, a prototype, an `&int` | there is no iteration protocol (`collection.fin`:59): nothing but an array says what it yields. A prototype *contains* two arrays, so walking one would be a choice this file may not make — `p.0` is how a program says which, and it lowers |
| `foreach (e <int> in mk().xs)` | an array with no home; `extractvalue` takes a constant index, the same gap `give()[0]` has. Named apart from "not an array" because it sends a reader elsewhere |
| a binding of another type than the element | nothing checks it before here (`KnownDefect_Foreach`), and converting would be a front-end rule invented in the backend |
| `foreach (e <auto> in a)` | a `let` infers from its initialiser and a binding has none; inferring the element type is the front end's rule to make |
| an index binding that is not an integer (`float`, `bool`, `string`, `int{8}`) | a position converted to a float compares equal against an `int` index only by conversion; a `bool` would be true for every element but the first |
| a `foreach` at module scope | no frame for the counter and no module initialiser to run it in — the answer every statement outside a function gets here |
| an extent past what an `int` counts | `.length` already misreports such an array (one defect); a loop that ran the wrong count would be a second and a worse one |

**Twenty tests, and five existing ones had to change their probe.** Five `Soundness_Codegen` cases
used `foreach (e <int> in a) { }` as a *construct the backend refuses* while testing something else
entirely — that refusals are collected across siblings, that each names its own line, that no object
is written, that a cascade is suppressed. All five went red the moment `foreach` lowered, which is the
system working: a probe that stops being a refusal stops being a probe. They now use
`let a <fn<T>(m: T) -> T>;` where the test is about a **declaration** and `p[1]` on a prototype where
it is about a **statement that declares no name** — the second chosen because its refusal is a ruling
waiting on an answer (what equality over an arbitrary key type means) rather than a feature about to
land. The same reasoning `m1778` was chosen by when `blame` lowered, and it is written next to both.

**The corpus at `624a061`, all 51 measured** — 22 OBJECT_CLEAN, 9 CODEGEN_REFUSED, 20 FRONTEND_ERROR.
The nine, with their first refusal re-measured here:

```
deeptest4.fin             a call with explicit generic arguments
generics_interfaces.fin   the erasure marker 'Castable' on 'T' of a generic function
interfaces.fin            a call to the method 'to_string' on struct 'User'
lambdas.fin               a variable of type 'fn<...>(T) -> T'
loops.fin                 a call to 'recursive'
readonly.fin              the attribute 'debug' on field 'v1' of struct 'MyClass'
stdlib/hashmap.fin        struct 'HashMapError' inheriting 'Error', which is not a struct this file lowered
stdlib/prototypes.fin     a return of type '$type'
type_annotations.fin      a variable of type 'int{64}'
```

Only `loops.fin`'s line changed from `02fba4a`, and it changed *within* the same bucket. **Nothing
regressed:** no sample moved to a worse bucket, and the same 22 reach an object.

**A nested function declaration is not lowered, and nothing booked it.** `loops.fin:40` declares
`fun recursive(a: int) <int>` *inside* `main` and calls it at `:46`; the call refuses with `a call
to 'recursive'`, because a function declared inside a body is never declared to the module. That is
now `loops.fin`'s only remaining blocker and it is a unit of its own — the question is whether a
nested function is a plain module-scope function under another name or a closure over the enclosing
frame, and the corpus writes one that captures nothing, so the cheap answer is available but is a
ruling. **Done at `418bca0` (2026-09-01): it is a plain function with internal linkage, the
analyzer's own step 6 is what says so, and `loops.fin` reaches an object — the corpus is 23 / 8 /
20.** See the next subsection.
Added to §6 item 8.

### A nested function declaration at `418bca0` (2026-09-01) — one of item 8's seven

**`loops.fin` reaches an object for the first time, so the corpus is 23 / 8 / 20.** The suite is
1564. This was the sample's only remaining blocker after `foreach` landed, and it is the construct
the `foreach` unit found and booked at the bottom of the previous subsection.

**The lowering is derived from the front end, not chosen.** The open question was whether a nested
`fun` is a plain function under a generated name or a closure over the enclosing frame. The
analyzer answers it: `Analyzer_Decl.cpp`, `visit(FunctionDeclaration&)` step 6 defines the name in
`currentScope->parent`, which for a declaration inside a body is the **enclosing body's** scope and
not the module. So the measured behaviour of the name is that it is visible from the declaration to
the end of that scope and nowhere else — a call written *above* it is `Undefined function or type`
from the front end, a sibling function cannot reach it, it shadows a module-scope function or a
global of the same spelling inside that scope, and two bodies may each declare `helper`. A body
reached by name from one scope and by nothing else **is** a function with internal linkage; there is
nothing else it could be. It lowers as `fin.nested.<n>.<name>`, internal, uniqued by a counter that
only goes up and spelled with dots so no Fin program can write it — the bargain `fin.lambda.<n>`
and `Box<int>.get` already strike.

`loops.fin:40`'s `recursive` is the corpus's only nested function and it **captures nothing**: it
reads its own parameter and calls itself. So a capture is not derived, and it is refused with the
machinery a lambda already had (`enclosingNames_` + `refuseIfCapture`, with `captureKind_` the only
difference between the two messages). That refusal is what entitles the representation to be a bare
code pointer, exactly as it does for a lambda: a nested function can be handed around as an `fn`
value, and with nothing closed over there is no second word for a pair to hold.

Four details are decisions and are worth naming:

- **The body is emitted at the declaration, not queued.** A queue would work and is what a method
  uses. But a nested function may call the enclosing body's *other* nested functions, and which
  ones those are is a property of **where it was written** — `fun early() { return later(); }` above
  `later` is an undefined name and must stay one. Emitting here makes the visible set be the set at
  this point in the walk.
- **The name is registered before its own body is emitted.** That is what makes `return
  recursive(a - 1)` find the function it is inside rather than start a second one. `loops.fin:44` is
  that call and it is the whole of why the order matters.
- **Two tables over one scope stack, walked in lockstep.** A local is a frame slot and a nested
  function is a symbol, so they are kept apart; `nestedFor` walks both innermost-first and answers
  only when the nested declaration is at least as inner as any local of the name. Reading either to
  exhaustion first would resolve a name in a scope the analyzer did not resolve it in, which is a
  call to the wrong thing — and it is why the nested lookup sits *before* the locals in both
  `visit(FunctionCall&)` and `visit(Identifier&)` rather than after them.
- **The enclosing names accumulate down the nesting.** A nested function two bodies deep cannot
  reach `main`'s frame any more than it can reach the frame it sits directly inside, so both sets
  are captures. Dropping the outer set would only change the *message*: `the name 'n'`, which reads
  as a front-end bug, in place of the boundary this is.

Every uncovered case refuses with its open question named:

| Refused | The question behind it |
| --- | --- |
| a body reading an enclosing local | a bare code pointer has nowhere to put it, and the frame is gone by the time an `fn` value calls it. The day the corpus writes one is the day the closure pair has to be designed — the same boundary a lambda sits behind |
| `fun h() <int>;` with no body, inside a body | at module scope this is a prototype for a definition elsewhere; here "elsewhere" is a scope that ends with this one. Treating it as an extern would emit a call to a symbol no object contains |
| a nested **generic** function | a template is not code until a call binds its parameters, and `fnTemplates_` is keyed by the written name with no scope in it — so a nested template of a name the module also uses would silently be one or the other |
| any attribute, `#[llvm_name]` included | that attribute names the symbol a declaration *publishes*, and a nested function publishes none. Honouring it would put an externally visible name on a function only one scope can call; ignoring it would drop an attribute the writer expected to change the object |
| `pub` on one | `pub` says what a **module's** scope hands to an import, and a nested function is not in one |
| a nested function whose name a local in the **same** scope already has | the analyzer *overwrites* the symbol, so `let h; fun h()` leaves the variable unnameable while its storage is live. One name over a slot and a symbol in one scope is a state the two tables cannot both hold. One scope apart is fine and lowers, in both directions |
| a second nested function of one name in one scope | `declareFunction` keeps the first, so the second body would silently not be the one that runs — the reason a second method of one name is refused, one level in |

**A pre-existing invalid-IR bug went with it.** `ScopedEmission` saved the builder's insert point,
`currentFn_`, `scopes_` and `poisoned_`, but **not** `loops_` — so a `break` written inside a body
emitted from inside a loop branched to the *enclosing function's* exit block. LLVM said `Referring
to a basic block in another function!` and the driver reported `emitted invalid IR for
'fin.lambda.0'`, naming a generated symbol instead of the construct the program wrote. The analyzer
permits the spelling because it sees the enclosing loop, so this pass is where it stops: `loops_` is
now cleared and restored with the rest, and the same program says `a 'break' outside a loop`. Four
spellings are covered by the one fix (`break`/`continue`, in a nested function and in a lambda) and
`BreakInsideANestedFunctionInsideALoopIsRefusedAsOutsideALoop` runs all four.

**Twenty-seven tests, no existing one changed.** The suite went 1537 → 1564. Positives assert
**values**: recursion (`15`), a factorial so a wrong answer is a wrong number (`120`), two bodies'
own `helper` (`1 2`, plus a trace check that both symbols are `fin.nested.<n>.helper`), arguments
and a `<noret>` body, an earlier sibling called and a later one refused, three levels of nesting
(`11`),
an `fn` value both into a variable and as an argument (`12 42`), a module function and a global
shadowed (`4 9`, `1`), block scoping and shadowing in both directions (`2 1`, `9 1`), a nested
function inside a struct method (`42`), one inside a template emitted **once per instantiation**
(`6 8`, counted in the trace), one inside a lambda (`4`), a lambda calling one beside it (`42`), and
a body reading a global and calling a module function (`14`). Each refusal is paired with the
supported spelling beside it.

**The corpus at `418bca0`, all 51 measured** — 23 OBJECT_CLEAN, 8 CODEGEN_REFUSED, 20
FRONTEND_ERROR. The eight, with their first refusal re-measured here:

```
deeptest4.fin             a call with explicit generic arguments
generics_interfaces.fin   the erasure marker 'Castable' on 'T' of a generic function
interfaces.fin            a call to the method 'to_string' on struct 'User'
lambdas.fin               a variable of type 'fn<...>(T) -> T'
readonly.fin              the attribute 'debug' on field 'v1' of struct 'MyClass'
stdlib/hashmap.fin        struct 'HashMapError' inheriting 'Error', which is not a struct this file lowered
stdlib/prototypes.fin     a return of type '$type'
type_annotations.fin      a variable of type 'int{64}'
```

`loops.fin` left the list and no line changed. **Nothing regressed:** no sample moved to a worse
bucket, and the 22 that reached an object still do.

**Two module-scope findings, booked and not fixed, both pre-existing.** Neither is this unit's and
both were confirmed against a build of `8f69ad5` with this unit's work stashed.

- **An expression statement at module scope crashes the compiler.** `printf("x\n");` or `let a
  <int> = 1; a = 2;` written outside any function segfaults `finc` under `-c` (exit 139, no
  diagnostic). `visit(ExpressionStatement&)` has no `!currentFn_` guard, unlike the ten statements
  that do, so the call is emitted with no insert point. A bare `1 + 2;` exits 0 and emits nothing,
  which is why the guard is a **ruling** rather than a one-line fix: whether a statement outside a
  function is an error or a no-op is the same question `foreach`-at-module-scope answered one way,
  and answering it here would change what `1 + 2;` does too.
- **A block at module scope parses and is silently dropped.** `{ printf("ran\n"); }` outside any
  function compiles clean and emits nothing at all — the statement never reaches `visit(Block&)`,
  because `visit(Program&)` walks the top-level statements and a `Block` among them is accepted with
  no function to emit into. A `fun` written inside such a block is invisible to `main`
  (`Undefined function or type`) but visible to the block's own statements, so the front end treats
  it as a scope while the backend treats it as nothing. Same ruling as above: silence and a refusal
  are both defensible and the corpus writes neither.

### The erasure marker at `HEAD` (2026-09-01) — one of item 8's seven

**`generics_interfaces.fin` reaches an object, so the corpus is 24 / 7 / 20.** The suite is 1573.
Nothing else in that sample was ever a blocker: an interface-typed local already lowers to ADR
0027's two-word shape, and `normal_generics<T>` is monomorphic. Its only block was an *uncalled*
`fun using_erasure_generics<T: Castable, U: Castable>` at `:8`.

**Nothing about the representation changed.** ADR 0002 still stands, unaltered and unimplemented:
erasure is selected by an erasure-marker constraint on any one parameter, and an erased generic is
a raw pointer. A monomorphised body for one would compile and be a *different program* — it
happens to agree wherever the argument is a scalar and disagrees wherever the erased pointer is
what the code is about. That is still refused. What moved is **where**.

**The old refusal answered a question nobody asked.** `hasErasureMarker` fired from
`lowerableTemplate` and from `declareTopLevel`, so a marked template failed the build whether or
not anything used it. The argument for that placement was that a marker is a property of the
template — `maybe<int>` is not going to stop being erased — and it is true and beside the point.
Monomorphisation means a template is a recipe and not code: `fun ident<T>(a: T)` that nothing calls
emits nothing (`AGenericFunctionNobodyCallsLowersToNothing`, and the same inversion this unit is),
and `struct M <T> {}` that nothing instantiates is a whole sample's worth of evidence
(`blame_assert.fin:19`) that an uninstantiated template is not an error either. An erasure marker on
one of those is a representation decision for code the object file does not contain. Refusing it
withholds a correct object for a question the program never poses — which is precisely what
`generics_interfaces.fin` was.

**So the marker is checked where a representation is first needed, at three sites.** Each is a
point at which this pass would otherwise have to lay something out:

- **`emitGenericCall`** — the call to a function template, before the arguments are emitted, so a
  refused call leaves no instructions behind for operands nothing will consume. Both spellings
  reach it: inferred (`erased(5)`) and turbofish (`erased::<int>(5)`).
- **`instantiateGeneric`** — the struct template's instantiation, ahead of the argument-count check
  so that a marked template with the wrong arity names the marker rather than the arity. Placed
  here and not at the `let`, which is what makes every path that needs the layout go through it: a
  field of `maybe<int>`, a parameter of it, `Box<maybe<int>>` as another template's argument, and a
  `::` call on `maybe::<int>` all refuse, and none of them is a variable declaration.
- **`instantiateGenericMethod`** — the call to a generic *method*, which **had no check at all**
  before this unit and is the hole the move exposed. See below.

**A silent miscompile was found and fixed, not booked.** A method's type parameters are not in the
struct's `generic_params`, so `lowerableTemplate` never saw them and neither did anything else:

```fin
struct Box {
   val <int>,
   pub fun peek<U: Castable>(u: U) <int> { return self.val; }
}
```

`b.peek(3)` compiled, linked and printed `7`. It reached `declareFunction` through
`instantiateGenericMethod`, was monomorphised at the argument's type, and ran — the exact different
program ADR 0002 names, with none of the declaration-site refusal's accidental cover. Measured
against the tree before this unit, so `AnErasureMarkedGenericMethodIsRefusedAtTheCall` is a
regression test for a wrong answer that was really there rather than for a hypothetical one. Fixed
rather than booked for the reason the width annotation was: an exit-0 compile that emits a machine
the program did not ask for is not a missing feature.

**Nine tests, of which two replace the two that existed rather than sitting beside them.**
`AnErasureMarkedGenericFunctionIsRefused` and `AnErasureMarkedGenericStructIsRefused` asserted only
that `Castable` appeared somewhere in the output, which passes for a declaration-site refusal and
for a use-site one alike — so they said nothing about the thing this unit changed. They now assert
the whole phrase (`the erasure marker 'Castable' on 'T' of the generic function 'erased'`) at the
call and at the instantiation, and each has a negative half asserting that the *uncalled* template
emits nothing and reaches no refusal, read off `--debug-codegen` rather than off the exit code.
`AnUncalledErasureTemplateDoesNotCostTheRestOfTheModule` is the sample's own shape and asserts a
**value** — `42` from the monomorphic template beside the erased one — so a build that emitted the
wrong body fails there instead of passing for compiling.
`OneMarkedParameterOfTwoIsEnoughAndTheFirstIsNamed` covers ADR 0002's "any one parameter" with `<T,
U: Castable>`, where the *unmarked* parameter comes first: that is what says the check searches for
a marker rather than looking at parameter zero.

**What is not covered, and why each is out of scope rather than missed.** An erasure-marked
*interface* (`interface Addable<T: Castable>`, `deeptest2.fin:13`) compiles clean today because an
interface declaration reaches no layout and no vtable until a struct implements it, and
`implements <Addable<int>>` is a **syntax error** in the parser — so there is no way to write the
use that would need the check. An erasure-marked *operator* is a syntax error too
(`operator +<U: Castable>` does not parse). An erasure-marked *lambda* (`lambdas.fin:69`) refuses
first for want of a monomorphisation key, which is `AGenericLambdaIsRefused`'s boundary and not
this one.

**The corpus at `HEAD`, all 51 measured** — 24 OBJECT_CLEAN, 7 CODEGEN_REFUSED, 20 FRONTEND_ERROR.
The seven, with their first refusal re-measured here:

```
deeptest4.fin             a call with explicit generic arguments
interfaces.fin            a call to the method 'to_string' on struct 'User'
lambdas.fin               a variable of type 'fn<...>(T) -> T'
readonly.fin              the attribute 'debug' on field 'v1' of struct 'MyClass'
stdlib/hashmap.fin        struct 'HashMapError' inheriting 'Error', which is not a struct this file lowered
stdlib/prototypes.fin     a return of type '$type'
type_annotations.fin      a variable of type 'int{64}'
```

`generics_interfaces.fin` left the list and no line changed. Its object links and runs, exiting 0
with no output — `main` declares an interface-typed local and does nothing else. **Nothing
regressed:** no sample moved to a worse bucket, and the 23 that reached an object still do.

**Item 8's remaining list is smaller than it reads, and this is the re-scoping it needs.** Four of
the samples it names are already OBJECT_CLEAN and were before this unit: `variables.fin`,
`blame_assert.fin`, `extern_as.fin` and `functions.fin`. So the address-of-a-value-with-no-home
ruling, the empty-struct ruling and type aliases no longer block the samples cited for them — those
rulings may still be worth making, but not on the corpus's evidence. `lambdas.fin` has **two**
refusals rather than the one recorded (`a variable of type 'fn<...>(T) -> T'` at `:69`, then `a
generic lambda` at `:71`), so it will not move on one fix. What is left of item 8 with a sample
behind it is `stdlib/prototypes.fin`'s `$type` return and `lambdas.fin`'s two.

### Movement since `43b3324`

`43b3324` measured 14 / 15 / 21 of 50 with a suite of 1344. The five commits between it and
`4788753` moved five samples out of codegen refusal and one out of front-end error, and the corpus
grew by one (`love.fin`, ADR 0008 ratified). Nothing regressed: no sample moved to a worse bucket.

### Reproducing the numbers

**Read `finc`'s own exit code, never a pipeline's.** `ec=$?` after `finc … | sed` captures
`sed`'s status and silently merges OBJECT_CLEAN with FRONTEND_ERROR — that error has been made
here. Redirect to a file, read `$?`, *then* filter. And use `find`, not a glob, or
`tests/samples/stdlib/` is missed.

```bash
cd /home/M1778/Fin
cmake --build build -j"$(nproc)"
ls -l --time-style=+'%Y-%m-%d %H:%M:%S' build/finc   # date AND size: see below
./build/tests/fin_tests

for f in $(find tests/samples -name '*.fin' | sort); do
  ./build/finc -c "$f" -o /tmp/x.o > /tmp/e 2>&1; ec=$?
  cg=$(sed -r 's/\x1b\[[0-9;]*m//g' /tmp/e | grep -c 'codegen:')
  if [ "$ec" = 0 ]; then echo "OBJECT_CLEAN $f"
  elif [ "$cg" -ge 1 ]; then echo "CODEGEN_REFUSED $f"
  else echo "FRONTEND_ERROR $f"; fi
done | awk '{c[$1]++} END {for (k in c) print k, c[k]}'
```

**Check the binary's size, not only its date.** A host reset once left `build/finc` at
**8388608 bytes — 8 MiB exactly**, against 28 MB for a real link. It ran, it exited 0 and 1,
and it produced a whole corpus table that was wrong. A round size is a torn write. Print the
date too: `--time-style=+%H:%M:%S` alone made yesterday's 09:00 read as later than today's 05:43.

### The two backend units that opened this stretch

- `848fde1` — **struct methods.** `Struct.method` / `Box<int>.method` symbols, pointer receiver,
  `Self` as a *binding* (not a name lookup), bodies deferred to `pendingBodies_`, `linkonce_odr`,
  generic methods declared by nobody and refused at the call.
- `91312b8` — **struct operators.** `V.operator+`, `spellOperator` turns an `ASTTokenKind` back
  into characters, the **left** operand decides, the lookup is gated on that operand being a
  struct (so `1 + 2` reaches exactly the code it always did), a left operand with no address
  refuses, and a generic operator or one bound by `implements` refuses where it is written.

Read both commit messages in full before touching `CodeGen_LLVM.cpp` — they explain the
machinery (`StructInfo::decl`, `StructInfo::methodBindings`, `FnInfo::hasReceiver`, `PendingBody`,
`declareStructMethods`, `drainPendingBodies`, `emitCallArgs`/`argList`) that the next several
units all build on.

## 5. Environment and tooling — the traps

- **Build:** `cmake --build build --target finc fin_tests -j6`. A **full** build exceeds the
  120 s foreground tool timeout. Start it with `nohup … &` and wait with
  `until grep -qE 'Built target fin_tests|error:' log; do sleep 10; done`. A **foreground**
  `sleep` is blocked by the harness. A single-target `--target finc` build does finish in
  foreground.
- **`cd` inside a Bash call can be reset** — prefix every command with `cd /home/M1778/Fin;`.
- **gmock is not linked.** Use `EXPECT_NE(x.find(s), std::string::npos)`, never
  `EXPECT_THAT` / `HasSubstr`.
- `CodeGen_LLVM.cpp` uses `std::set` and has **no `<unordered_set>` include**.
- **`finc` has no `--check` flag.** Real flags: `-o`, `-c`, `-O0..-O3`, `-I/--include`,
  `--fin-libs`, `--diagnostics=`, `--color=`, `--debug-ast`, `--debug-sema`, `--debug-codegen`,
  `--no-check`, `--version`, `--help`.
- LLVM 22.1.8 at the default prefix: `llvm-config --cmakedir` is `/usr/lib/cmake/llvm`, and
  `find_package(LLVM CONFIG)` finds it with no `LLVM_DIR` passed. There is no `llvm-config-22`.
  `nproc` = **16**. (Both figures were 18.1.3-at-`/usr/lib/llvm-18` and 6 until 2026-08-27; they
  described a different machine.)
- `ExitCode`: Success 0, Diagnostics 1, Usage 2, Internal 3 (ADR 0009).
- Temp files: `$CLAUDE_JOB_DIR/tmp`.
- **`std::unordered_map` is node-based**, which is load-bearing: `StructInfo&` / `FnInfo&`
  references into `structs_` / `functions_` survive later insertions. That is what makes
  `declareStructs`' extra passes, `instantiateGeneric`'s `StructInfo& live` and
  `PendingBody::bindings` (a pointer into a `StructInfo`) safe. Do not change either map to a
  flat one.

### Test conventions in `tests/test_codegen.cpp`

- **Two suites.** `Soundness_*` must always pass. `KnownDefect_*` records a boundary. When a
  boundary moves, **invert and rename the test, never relax it.**
- Harnesses: `Built build(code)` → `.compileExit`, `.compileErr` (ANSI-stripped), `.ran`,
  `.runExit`, `.out`, `.why()`. `Compiled compileOnly(code, objectPath = {})` → `.exitCode`,
  `.err`, `.object`, `.why()` — **with no `objectPath` it writes `<stem>.o` into the cwd and does
  not clean up, so the test must `fs::remove(c.object, ec);`**.
- `BACKEND_TEST(suite, name)` skips when `FIN_WITH_LLVM=OFF`.
- `kPrintf` = `"@define printf(fmt: string, ...) <noret>;\n"`.
- `std::string codegenTrace(code)`; `size_t occurrences(haystack, needle)`.
- Link tests: `uniqueTempPath`, `shellQuoteLocal`, `readWholeFile`, `std::system`, `FIN_CC` env.

### Fin syntax reminders (they bite)

`fun` is the function keyword; void return is `<noret>`; types go in angle brackets
(`let x <int> = 1;`); a pointer is `&T`; a generic struct literal needs the **turbofish**
(`Box::<int>{ val: 100 }`); struct **fields are comma/newline-separated with no semicolons**;
enum members are bare names; `pub static fun` for statics; `Point::method()` parses as a
`StaticMethodCall`. `Self{x: 1}` as a struct literal **does not parse** (`new Self{...}` does).

### Editing idiom that has not lost work yet

A Python heredoc with a `one(old, new)` helper that asserts `s.count(old) == 1` **before** any
write, and writes the file only at the very end — so a failed assertion changes nothing. Use
`r'''…'''` for C++ snippets containing `\n`. Anchors must start at a line boundary.

## 6. What to do next

The 17 samples that reach codegen and are blocked by exactly one refusal each, measured at
`91312b8`. This list **is** the work queue for the backend, but **read §4's re-measurements first**:
it is **eight** samples at `418bca0`, and every one of them reports something other than what the
block below says. The numbered items keep their old titles for continuity; the corrections are in
their text.

```
arrays_enums.fin          a variable of type '[int]'
blame_assert.fin          an empty struct 'M<int>'
complex.fin               an import (the module loader did not consume it)
deeptest1.fin             an interface declaration
deeptest4.fin             an import (the module loader did not consume it)
extern_as.fin             a type alias
functions.fin             a parameter of type 'fn'
generics_interfaces.fin   the erasure marker 'Castable' on 'T' of a generic function
implements_block.fin      an interface declaration
lambdas.fin               a parameter of type 'fn'
letssee.fin               a '::' call on the generic struct 'Vec2' with no type arguments
loops.fin                 a 'foreach' loop
readonly.fin              struct 'ChangableSomehow' inheriting another type
stdlib/hashmap.fin        struct 'HashMapError' inheriting another type
stdlib/prototypes.fin     a return of type '$type'
type_annotations.fin      a variable of type 'prototype<int, float>'
variables.fin             the address of a value with no home
```

**That list is first refusals only, and a first refusal is not a count.** `finc -c` stops
where it stops, so a sample with one line here may have two rulings behind it. Demonstrated
twice: `variables.fin`'s single `&"..."` refusal hid a second, `#[slaveof(x)]` on a local
(`:27`, `:35`), which appeared only when the first was lowered; and a prediction of "10 → 14
from three features" delivered 10 → 11 for the same reason. Measure after landing, never before.

Recommended order — cheapest first, and each one unblocks the next:

1. ~~**Generic methods**~~ — **done.** Instantiated at the call site, both substitutions
   composed, keyed `Struct<args>.method<margs>`, `linkonce_odr`. See below and the commit.
2. ~~**Constructors**~~ — **done at `cfebdd5`.** One convention, three sites agreeing: the
   caller allocates the object, zeroes it, passes its address as parameter 0, and reads the value
   back out of its own slot; the constructor returns void and `return new S{...}` / `return S{...}`
   both store through the receiver first. `Point(7)` is a `FunctionCall` whose name is a struct's,
   rewritten to the `Point.constructor` symbol at the call. The booked defect stands —
   **constructor overloads are not resolved, only `constructors[0]`** — and a second `constructor`
   is now refused *by name* at its declaration rather than losing silently to the first
   (`KnownDefect_Codegen.ConstructorOverloadsAreRefusedRatherThanResolved`).
   **`new S(args)` is not part of this and was not skipped: it does not parse.** `let p <&P> = new
   P(3, 4);` is `syntax error, unexpected LPAREN, expecting LBRACE or DOUBLE_COLON`, and no line in
   the corpus writes that form. So that half of this item's old title is a grammar question for the
   owner, not a lowering. `visit(NewExpression&)` still refuses `new` of a struct with arguments,
   by name, for the day it does parse.
3. ~~**Struct inheritance**~~ — **split at `80f4f8e`, and the doable half is done.** See §4, "The inheritance
   neighbourhood". An **inherited method, operator or static method is now callable through the
   derived struct**, resolved breadth-first over the parents so an override wins, with the two
   wrong-answer cases refused (a second base's method, and one name from two bases). What is left
   under this title is *not* inheritance: `stdlib/hashmap.fin` needs (a) an imported struct type to
   be lowerable at all — a **separate-compilation decision**, since a module's AST never reaches the
   backend and only ambient `@define` externs are spliced — (b) `parentIsInterface` to know about
   imported interfaces, which the same decision settles, and (c) the **`class` unit** (ADR 0026),
   because its base `Error` is `#[class]`. `readonly.fin` remains gated behind the `#[debug]` field
   attribute in front of its inheritance. **No sample is unblocked by any of this**, measured.
4. **Interfaces** — **the implements-block half is done at `5d70a6e`**, and it moved the corpus
   for the first time in four units: `implements_block.fin` is OBJECT_CLEAN. See §4, "The
   implements-block unit". A block's methods, operators and constructor are now declared by exactly
   the code that declares a struct's own, so a call, an operator, a static method, a constructor and
   dispatch through an interface's vtable all work.
   **The reference half is done too — it was already built when this item said it was not.** See
   §4, "The interface reference": `aea960e` implemented all of ADR 0027 on 2026-08-28 and shipped no
   test for it; seventeen tests now measure it, twelve of them asserting a value through a
   reference. **Neither sample this item named is still evidence for it.** `deeptest1.fin` was
   already OBJECT_CLEAN when §4 was re-measured at `cfebdd5` — the interface declaration it was
   blamed for is not its blocker and has not been for two commits — and `implements_block.fin` is
   now clean too. What is left under this title is **two unrelated things**, and neither is an
   interface-lowering gap:
   - **`interfaces.fin` is not blocked on interface lowering.** Its refusal is `a call to the method
     'to_string' on struct 'User'`, and `User` **declares no `to_string`** — not in the struct, not
     in a block. The sample's own comment says "we don't check impl yet, just syntax". So this is a
     ruling about a call to a method nothing declares, not a vtable.
   - **`hashmap.fin:50-51` writes a bodiless forwarding operator**:
     `operator[] implements cast<fn(Self, T)>(__get)`. That is an operator whose implementation is a
     cast of another symbol, which is neither a body nor an extern — and it sits behind hashmap's
     three separate-compilation blockers anyway (item 3).
   ~~**ADR 0019's interface reference as two words**~~ — **built at `aea960e`, measured
   2026-08-30.** The bullet that stood here said no corpus site takes an interface reference as a
   value and that nothing measures it. Both are false: `love.fin` is such a site and it runs, and
   the reference works for a parameter, a local, a struct field, an assignment, a field read
   (including at a shifted offset and through an inherited field), a write through a method, a
   re-pass, and a generic implementor. §4 has the sites, the four ADR-0027-undecided edges as they
   measure today, and the two booked defects — a **generic interface as a value type** refuses
   (`TypeMapper::map` tests generics before interfaces), and an **escaping reference** reads a dead
   frame, which is the pipeline's general lack of lifetime analysis and not an interface defect.
   Also booked from the two units' measurements: the analyzer does not resolve a generic argument
   written on the *interface* of a generic-target block (`Box<T> implements <IBox<T>>` →
   `Undefined type 'T'`), and an interface satisfied by an **inherited** method is still reported
   unimplemented (`Analyzer_Decl.cpp:537`) even though the backend's table already resolves such a
   provider through the hierarchy. Both are front-end work.
5. ~~**Imports**~~ — **done at `211c8ab` (2026-08-31), and `complex.fin` is OBJECT_CLEAN: the corpus
   is 21 / 10 / 20.** The queue's own instruction is what was built. The analyzer rewrites a
   namespace-qualified `MethodCall` into a plain `FunctionCall` on the member's own name
   (`SemanticAnalyzer::lowerModuleCall`, left on the node in `MethodCall::resolved_call`), and
   codegen delegates to it in the *first statement* of `visit(MethodCall&)` — so the qualifier never
   reaches the backend, codegen still holds zero references to `NamespaceType`, and
   `visit(ImportModule&)` still refuses any import that reaches it.
   **The rewrite is gated on the member being ambiently published** (`Symbol::is_ambient`, set where
   `ModuleLoader::retainAmbientPrototype` retained the prototype), because ADR 0021's `#[global]`
   splice is the one mechanism that puts a module's declaration into the root program the backend
   walks. A wider gate trades a codegen refusal for a link failure, and a link failure arrives after
   a compile that exited 0. §4's "The namespace-qualified call" carries the mechanism, why the mark
   is not the fact when two modules publish one name, and the shapes measured.
   **The two `printf`s are answered by ordering, not by choosing.** `checkCallArguments` runs before
   `lowerModuleCall`, so `stdio.printf` is typed by the module's `<noret>` declaration and the plain
   `printf` by the file's own `<int>` one; `complex.fin` writes both and prints `Big`.
   **Neither of the two gaps left under this title is a namespace fault**, and both are booked in §7
   with tests: an imported extern that is not ambient, or any imported Fin function, is still not
   lowered through a dot (that is separate compilation — item 3's (a)); and a file that redeclares an
   ambient name under a symbol of its own breaks the qualified spelling and the plain one alike, at
   the link, which is `#[overwrite]`'s question and must not be fixed on one side.
   **`deeptest4.fin` was not item 5's, and had not been for some time.** Its first refusal is
   `codegen: a call with explicit generic arguments is not lowered yet` at `:11`,
   `let a <auto> = HashMap::<string, Data>();` — item 6's neighbourhood, with the imported-struct
   decision behind it.
6. ~~**`::`-call type-argument inference**~~ — **done at `132aed7` (2026-08-31), and `letssee.fin` is
   OBJECT_CLEAN: the corpus is 22 / 9 / 20.** See §4, "The `::` call's type arguments".
   **This item's premise was wrong about two of its three sites.** It said the missing piece is
   inferring `T` from the arguments; `letssee.fin:73` does infer from an argument (`&Self`), but
   `:59` and `:77` take `T` from the **annotation** on the left, which codegen cannot see. So the
   answer is not argument inference in the backend: the analyzer records the instantiation it
   already computed (`recordResolvedTarget` → `StaticMethodCall::resolved_target`) and the backend
   maps that node instead of the bare template. A type **parameter** is recorded as its own name, so
   one node inside a template body still resolves per instantiation; a parameter that is not in
   scope at the call is compared by **identity** and refuses rather than borrowing the caller's.
   **The three refusals next to it are still standing and are not this item's**: a `::` turbofish
   after the method name (`Box::make::<int>(9)`), a generic **constructor** call with a turbofish
   (`deeptest4.fin:11`, `HashMap::<string, Data>()`), and the same with inference (`Box(7)`). All
   three are the analyzer binding nothing — `StaticMethodCall::generic_args` is not read by this
   inference — and the second is the only one a sample's first refusal counts.
   **`letssee.fin`'s printed numbers are wrong for a reason in the sample**: `@define sqrt(f: float)`
   against libm's `double sqrt(double)`. §4 has the two probes that isolate it; it is a ruling
   (§8), not a lowering.
7. ~~**Variable types**~~ — **done at `02fba4a` (2026-08-31), and no sample moved: the corpus is still
   22 / 9 / 20.** See §4, "The prototype and the width annotation". **Both halves of this item's text
   were stale.** The `[int]` half says it is blocked on an owner ruling for the representation of a
   dynamic `[T]`; §8 records that ruling as made on 2026-08-27, ADR 0025's `{ptr, len}` is implemented
   end to end, and `arrays_enums.fin` has been OBJECT_CLEAN for several commits. So only the prototype
   was left, and `prototype<K, V>` now lowers as `{ [K], [V] }` — two dynamic arrays side by side,
   derived from `stdlib/prototypes.fin`'s normative `prtp.0`/`prtp.1` and from the types the analyzer
   already gives them. Storage, construction, both projections and the variable, parameter, return,
   struct-field and bare-global roles are in; key **lookup** is not, and refuses rather than answering
   with element 0 — that is prototype *access* (`prototype_test.fin`'s note) and a unit of its own.
   **A third thing landed with it and belongs to item 9, not here:** a **bit-width annotation now
   refuses** instead of being dropped. `int{64}` used to build an object and emit an i32, which is a
   *machine* the program did not ask for on a compile that exited 0, so it was fixed rather than
   booked. That is why `type_annotations.fin` is still CODEGEN_REFUSED: its first refusal moved from
   the prototype at `:14` to `int{64}` at `:5`. Real widths remain item 9's.
8. Then, in any order: the address-of-a-value-with-no-home ruling (`variables.fin`); the
   empty-struct ruling (`blame_assert.fin`'s `M<int>`); type aliases (`extern_as.fin` — also the
   blocker for the corpus's own `<T: Number>` spelling, and the reason a width *alias* refuses
   independently of item 9); `[T]`/`$type` returns (`stdlib/prototypes.fin` — its first refusal, and
   the last thing between that sample and an object now that its `{any, any}` parameters are not the
   block); ~~`foreach` (`loops.fin`)~~ — **done at `624a061` (2026-08-31); no sample moved, the
   corpus is still 22 / 9 / 20, and `loops.fin`'s first refusal moved from `a 'foreach' loop` at
   `:19` to `a call to 'recursive'` at `:46`** (see §4, "`foreach`"); ~~**a nested function
   declaration**~~ — **done at `418bca0` (2026-09-01), and `loops.fin` moved: the corpus is 23 / 8 /
   20 and the suite is 1564** (see §4, "A nested function declaration"). It is a plain function with
   internal linkage under a generated name, derived from the analyzer defining a nested `fun` in the
   enclosing *body's* scope, and a capture is refused because the corpus's one instance captures
   nothing; **two pre-existing module-scope findings were booked next to it and not fixed** — an
   expression statement outside a function *segfaults* `finc`, and a block outside one is silently
   dropped; lambdas and `fn` parameter types (`functions.fin`, `lambdas.fin`);
   ~~the erasure marker (`generics_interfaces.fin`, ADR 0002)~~ — **done at `HEAD` (2026-09-01), and
   `generics_interfaces.fin` moved: the corpus is 24 / 7 / 20 and the suite is 1573** (see §4, "The
   erasure marker"). ADR 0002's representation is untouched and still unimplemented; the refusal
   moved from the declaration to the three places a representation is first needed (the call, the
   instantiation, the generic-method call), because a template nobody uses emits nothing and so
   poses no question to refuse. **A silent miscompile was found and fixed on the way:** an
   erasure-marked *method* type parameter had no check anywhere, so `fun peek<U: Castable>` on a
   non-generic struct was monomorphised and ran.
   **Item 8's remaining list needs re-scoping and §4 does it:** four of the samples it names above
   are already OBJECT_CLEAN (`variables.fin`, `blame_assert.fin`, `extern_as.fin`,
   `functions.fin`), so the first three rulings no longer block the samples cited for them, and
   `lambdas.fin` has two refusals rather than one. What is left with a sample behind it is
   `stdlib/prototypes.fin`'s `$type` return and `lambdas.fin`'s two.
9. After the corpus: the struct ABI classifier, `blame`/`try`/`catch`, the payload-carrying
   tagged-union enum, **real** bit-width annotations (`int{64}`) — which is now a narrowing to
   implement rather than a miscompile to stop, because the annotation refuses as of `02fba4a`; it is
   also `type_annotations.fin`'s first refusal and so the sample's remaining blocker.

### The generic-methods unit — landed, and what it did not do

Both halves are in: `instantiateGenericMethod` infers the method's own type parameters from the
argument values, composes them **onto** the struct's `methodBindings`, keys the instance
`Box<int>.set_x<int>` (and `MyInt.operator+<char>`), emits it `linkonce_odr` and queues the body on
`pendingBodies_`. A method type parameter that reuses a name the struct already binds is **refused**,
because `TypeMapper::boundBinding` returns the first match and the corpus rules the shape out
(`struct_methods.fin:14`: "it's separated from the struct generic itself so it cant have the same
name as `T`").

**It unblocks no sample, and that was foreseeable.** `struct_methods.fin` and `operators.fin` were
already codegen-clean — they *declare* a generic method and a generic operator and never reach one —
so the refusal this unit removed was at a use site the corpus does not write. The 17-item list above
is unchanged, refusal for refusal. What the unit bought is the corollary in §1 coming good: a
construct left undeclared and refused at the use site now works at the use site, and the machinery
(two live substitutions, an instance keyed on both) is what constructors, `::`-call inference and
interfaces each need next.

## 7. Booked, not to be fixed

These are all **deliberate**. Do not "fix" them without a ruling; do add a `KnownDefect_*` test
if one is missing.

**Front-end gaps with tests already:** `KnownDefect_Codegen.AStructTypeDoesNotHoist`,
`.AnEnumMemberDoesNotHoist`, `.AGlobalDoesNotHoist`. No implicit widening float→double. No
conversion between integer types. `cast<int>` of a float is not lowered.

**Corpus typos — book, never fix:** `geykeyid` at `stdlib/typing.fin:37` and
`stdlib/stdio.fin:65`; `literal_interface.fin:24`'s missing `;`; `arrays.fin`'s `let temp <int>`;
`collection.fin:55`'s `let i <int> = i`; `enums.fin:29`'s `blame enum_.1`; `enums.fin:26`'s
`Ok(T)`.

**Analyzer/AST defects carried:** `StructType::substitute` leaks the outer struct as a nested
struct's `Self`; an interface cannot inherit an interface; **an interface satisfied by an
*inherited* method is reported unimplemented** (`Analyzer_Decl.cpp:537` walks the struct's own
methods, so `struct Talker: <Base, Speaker>` where `Base` declares `speak` gives `Struct 'Talker'
does not implement interface 'Speaker'` — measured 2026-08-30; the backend's vtable now resolves
that slot through the hierarchy, so the front end is the only thing in the way); `StructType::implements()` compares
names only; **operators have no arity check**
(which is why the backend's own `too few arguments` refusal is where a wrong-arity operator
lands); interface-typed pointer assignability; `Scope::resolve` leaks non-exports through a
namespace; prototype methods; index assignment never consults `operator []=`; the two
`KnownDefect_TypeAliases` cases; the `isCastableTo` family is dead; `CloneVisitor` drops several
flags; `namespace_path` read by nobody; **constructor overloads are not resolved (only
`constructors[0]`)** — the backend declares one symbol per struct to match, and refuses a second
`constructor` by name at its declaration rather than letting a call reach the wrong body
(`KnownDefect_Codegen.ConstructorOverloadsAreRefusedRatherThanResolved`);
`ImplementsBlock::is_overwriter` read by nobody; a generic free function's
turbofish binds nothing (worked around in the backend); a member assignment is never
mutability-checked; **a `StructInstantiation` does not infer its generic arguments from the
annotation** — `let p <Pair<int>> = Pair{one: 11};` is `Type mismatch: expected 'Pair<int>', got
'Pair<A>'` (measured 2026-08-31 while probing item 6), so the turbofish is mandatory in a struct
literal even where a `::` call on the same type now infers; a **method call chained onto a call's
result** refuses in the backend, `the receiver of a call to the method '…' on a value with no
address` for `outer.get().get()`, which is the temporary-with-no-address ruling (§8) and not a
generics gap.

**Unbooked parse gaps** (need `KnownDefect_*` tests written): hex literals; `fn(m: int) -> int`;
`std::Error` in type position; `Box<int> { v: 1 }`; `{ 1: S{v:1} }`; `{}` as an empty prototype
literal; `let s <module.Type>`; `Box<int>()` in a call; `new int;`; an empty `implements <>`;
`struct B : A`; `<T?>` as a return type; `new T(p)` as a `<&T>` return expression. Also: binary
`|`, `^`, `&` and unary `~` have precedence but no production.

**Codegen residuals:** the `baseAddress`-then-`emit` double-emit for `(*get()).field` — and now,
narrowly, for a struct-typed left operand of an operator (one dead aggregate load; `-O1` removes
it). A flat pointer map for a very large fixed array is a size problem.

**The interface reference's two, booked 2026-08-30** (§4, "The interface reference"):
**a generic interface as the type of a value refuses** — `TypeMapper::map` tests
`!node->generics.empty()` before `interfaces_->count(name)`, so the name goes to
`instantiateGeneric` as a struct template and the interface branch is never reached; no corpus site
writes one, so it is booked rather than reordered
(`KnownDefect_Codegen.AGenericInterfaceAsAValueTypeIsRefused`). And **an escaping reference reads a
dead frame** — accepted, garbage at run time, `lea -0x8(%rsp)` in the disassembly. That one is
**not** an interface defect and must not be fixed as one: a plain `&D` returned from a function is
accepted identically, because there is no lifetime analysis anywhere in the pipeline — the same fact
§8's `#[slaveof]` ruling turns on.
`KnownDefect_Codegen.AnEscapingInterfaceReferenceIsAcceptedLikeAnyEscapingAddress` holds both halves
and asserts only that each compiles, so the day escape analysis lands they go red together.

**The module qualifier's two, booked 2026-08-31** (§4, "The namespace-qualified call"): **an
imported extern that is not `#[global]`, and any imported Fin function, is not lowered through a
dot.** `stdio.io_fflush(null)` and `stdio.println("x")` both keep `the receiver of a call to the
method '…' on a value with no address`. Only an ambiently-published declaration reaches the root
program the backend walks, so there is nothing for a plain call to be a call *to* — the Fin-function
half is separate compilation (§6 item 3's (a)), and rewriting either would move the refusal onto a
different name rather than remove it. A root file that declares the same name *itself* still gets the
refusal, which is what says the gate is "binds this name to **this declaration**" and not "binds this
name". And **a file that redeclares an ambient name under a symbol of its own breaks the qualified
spelling and the plain one alike** — both fail the link, because the file's declaration wins in
`declareFunction` (first wins, and the splice is appended) and after the rewrite both spellings ask
for its symbol. **Do not repair the qualified side alone:** `a.twin(…)` linking while `twin(…)` in
the same file does not would be worse than both failing. It is `#[overwrite]`'s question — which of
two declarations of a name wins, and under whose symbol — and `printf` never shows it because the
corpus samples that redeclare it write no `#[llvm_name]`, so their symbol and the bundle's are the
same string. `KnownDefect_Modules.AnImportedExternThatIsNotAmbientIsNotLoweredThroughADot`,
`.AnImportedFinFunctionIsNotLoweredThroughADot` and
`.RenamingAnAmbientNameInTheRootFileBreaksBothSpellingsAlike` hold all three; the last holds both
spellings, so the day `#[overwrite]` is ruled on they invert together.

**`conanfile.py`'s LLVM block still says 18 — booked by ruling, 2026-08-27.** The docstring
(lines 12–14) and the `requirements()` comment (lines 48–67) say "a single LLVM major -- 18", quote
`conan search llvm-core` finding no 18, claim "Nothing in src/ includes an LLVM header yet … so the
pin buys nothing today", and count the cost as "three of its six platforms". All four are now false:
the pin is 22, `src/codegen/CodeGen_LLVM.cpp` includes LLVM headers and links the `LLVM` target, and
the ConanCenter gap is five of six platforms, not three. It is left alone deliberately, and the
reason is that **none of it is a build input**: the whole block is comments, `requirements()` asks
for `fmt/10.2.1` and `gtest/1.14.0` and nothing else, and no LLVM package is ever resolved through
Conan. A stale comment there cannot break a build or move a version. Where the LLVM decision *is*
executable is `CMakeLists.txt` (`FIN_LLVM_MAJOR`, checked against what `find_package` found) and
`.github/workflows/ci.yml` (installed per platform, passed on the command line, read back out of the
cache) — and both were corrected. `conanfile.py` is left as a witness to how the 18 pin was reasoned
about, for the same reason ADR 0010 was amended in place rather than rewritten.

**Other:** `parser.y` carries the whole `new` production block twice; display-width-aware caret
placement; the stale `foreach` comment in `parser.y`; the stdlib track has no ordered plan yet;
CI green on six platform/arch combos — no run has ever been observed from here. As of 2026-08-27
`ci.yml` does install the pinned LLVM per platform and does pass `-DFIN_LLVM_MAJOR` explicitly, so
the six rows are now claims the file tries to make rather than claims it merely asserts. **None of
the six has been run**; what follows is what each was checked against offline, and what would
actually settle it:

| Row (runner) | State | What would settle it |
| --- | --- | --- |
| `linux-x86_64` (`ubuntu-24.04`), `linux-arm64` (`ubuntu-24.04-arm`) | expected to link | checked offline: the `llvm-22-dev` deb from apt.llvm.org carries `LLVMConfig.cmake` (`LLVM_VERSION_MAJOR 22`, `LLVM_LINK_LLVM_DYLIB ON`) and `LLVMExports.cmake` has `add_library(LLVM SHARED IMPORTED)`; apt.llvm.org publishes `llvm-toolchain-noble-22` for both `amd64` and `arm64` |
| `macos-arm64` (`macos-15`) | expected to link | Homebrew `llvm@22` is 22.1.8 and sets `LLVM_LINK_LLVM_DYLIB=ON`, so the same `LLVM` target exists |
| `macos-x86_64` (`macos-15-intel`) | **unverified** | `brew info --json=v2 llvm@22` on an Intel Sequoia runner. LLVM publishes no macOS x86_64 asset at all and `llvm@22` lists no Intel `sequoia` bottle (only `sonoma`, plus `cellar: :any`) while `cmake` does list one — so this row rides on Homebrew's older-tag fall-back, and if that misses, brew builds LLVM from source and the job times out instead of failing cleanly |
| `windows-x86_64` (`windows-2022`), `windows-arm64` (`windows-11-arm`) | **cannot link** | nothing available here — see §8. `find_package(LLVM CONFIG)` succeeds against the official tarball and the major check passes; then `LLVM.lib` does not exist, because LLVM will not build the monolithic library under MSVC at any version |

The local run is the only measured one: `./build.sh --release` on a clean checkout of this tree
gives `FIN_WITH_LLVM:BOOL=ON`, `FIN_LLVM_MAJOR:STRING=22`, LLVM 22.1.8 from `/usr/lib/cmake/llvm`,
and 1257 / 0 failed / 0 skipped.

## 8. Owner rulings queued

`docs/plan.md` §"Rulings owed" (line 2967) is the canonical list. Added since, and **blocking**
where marked:

- ~~**The representation of a dynamic `[T]`**~~ — **RULED 2026-08-27, and the corpus
  ruled it.** The owner declined to choose between a fat pointer and a growable vector and
  ruled that ADR 0008 applies. It does, and it answers: **`[T]` is `{ptr, len}` — a
  length-carrying handle to heap memory, with no capacity field.** It carries its length
  (`.length` is read off one at `stdlib/stdio.fin:114`, `:130`, `:135` and `arrays.fin:12`
  through an `&[T]`; stdio's own note argues it — "a `new [char, n]` is a `[char]`, not a
  pointer to a fixed-size one … `_temp.length` reads a length off the allocation, which a
  pointer does not have"). It owns heap memory, so **it needs an allocator**: every one is
  born from `new [T, n]` with a *runtime* extent (`stdio.fin:112`) and freed by `delete`
  (`stdlib/collection.fin:46` frees what `:54` allocated — "one buffer at both ends"). And
  it takes **no capacity field**, because nothing in the corpus grows a `[T]` in place: the
  only three growth sites are `Collection<T>` methods (`stdlib/hashmap.fin:37`
  `self.keys.push`, where `:16` declares `priv keys <Collection<T>>`, and
  `macro_definitions.fin:16` `temp.add`, where `:15` declares `new Collection::<int>{}`),
  and `Collection` *wraps* `_arr <[T]>` and grows it in Fin code by allocating a fresh
  buffer and copying. Growth is built **on** `[T]`, not **into** it. It is also passed by
  value (`const.fin:61`) and by reference (`arrays.fin:11`), returned (`stdio.fin:87`,
  `stdlib/prototypes.fin:10`), stored in fields (`collection.fin:16`, `stdio.fin:83`/`:98`,
  `deeptest2.fin:111`), and used as a generic argument (`const.fin:98` `rptr<[int]>`).
  Blocks `arrays_enums.fin` and `deeptest1.fin`, but **not** the top of the queue: both
  carry suppressed statements that `fn` parameters and struct inheritance do not.
- **Is `&string` the same representation as `string`, or a pointer to a cell holding one?**
  — *blocks `variables.fin`, and it is the whole of what blocks it there.*
  `variables.fin:11` writes `let Complex <&string> = &"Hello world";`. A `string` already
  lowers to a pointer to bytes, so `&"..."` reads two ways and both compile:
  **(A)** it is that same pointer, making `&string` and `string` one representation and
  `*Complex` a **char**; **(B)** it is the address of a cell holding the pointer, making
  `*Complex` the **string**. `variables.fin:11` is the **only** `&"..."` and the only
  `&string` in `tests/samples/` and `lib/std/` — measured — and nothing reads `Complex`, so
  no corpus evidence separates them and none can be obtained without you.
  **What is already settled, so you do not have to weigh it:** the *lifetime* half. A
  literal's value exists before the program starts, so a holder can be static, which cannot
  dangle under any later use; at module scope static is forced, since there is no function to
  hold an alloca. That was implemented and worked — compiled, ran, and a write through one
  `&"..."` did not reach another — and was reverted, because representation is the objection
  that survives and because the check confirming it was circular (it printed `*G` with `%s`,
  which only makes sense under reading B). `Soundness_Codegen.TheAddressOfAStringLiteralIs-`
  `Refused` carries the argument; the refusal comment in `CodeGen_LLVM.cpp` carries the
  narrowing.
- **What `#[slaveof(x)]` does to a local's lifetime** — *blocks `variables.fin` behind the
  above.* Found by lowering the `&"..."` refusal and seeing what appeared: `variables.fin:27`
  `#[slaveof(z)]` on `let m <&int> = new int(5);` and `:35` `#[slaveof($Fin)]` on
  `const invincible <&int> = new int(1778);`, both **locals** inside `main`. The sample's own
  comments say `slaveof` ties a variable's lifetime to another variable's, and `$Fin` means
  until the program exits. `declareGlobals` already refuses an attribute on a *global* for the
  same reason, with the note that `#[slaveof($Fin)]` on a global asks for what a global already
  has. So `variables.fin` needs two rulings, not one, and the second was invisible while the
  first refusal stopped the pass — which is §17's rule that yield cannot be read off a first
  refusal, demonstrated again.
- ~~**What a `class` is at run time**~~ — **RULED 2026-08-28: a struct that may name a base.**
  A value, copied on assignment; `class` buys inheritance and nothing else. `readonly.fin:16`
  says "Readonly in classes (same with struct)" and `stdlib/error.fin:7` says `#[class]`
  "turns structs into classes (for stronger inheritance support)". The line that reads the
  other way — `lib/std/stdptr.fin:29-30`, "returning it by value would copy the very counter"
  — is about `own()`'s return type, and `rptr`'s counter is a `&uint`, so a copy shares it.
  **No corpus site copies or assigns a class value**, which is why this needed the owner.
  → **ADR 0026.** Still owed in code: `class X {}` parses to a `ClassDeclaration`, which does
  **not** derive from `StructDeclaration` (`parser.y:556`; `:36-41` records a bug that cost),
  while `declareStructs`/`StructInfo::decl`/`declareStructMethods` all key on
  `StructDeclaration*`. `StructDeclaration::is_class` is not the hook — nothing sets it.
- ~~**What `try`/`catch` does**~~ — **RULED 2026-08-28: `try` is a scope, `catch` emits
  nothing.** Nothing in Fin raises anything a `catch` can receive: `blame`'s assert form
  aborts without unwinding and its raise form is refused. The corpus has one `try`
  (`readonly.fin:48`) and the `a.v1 = 5` it guards is a *compile-time* error elsewhere. The
  catch body is still analysed (`Analyzer_Stmt.cpp:148-156`), so only codegen is skipped.
  → **ADR 0026.** `Soundness_Codegen.ATryBlockRunsAndItsCatchDoesNot` is what fails the day a
  raise form lowers, and that failure is the signal to build a real mechanism.
- **May a `&Derived` be passed where a `&Base` is expected?** — *blocks nothing today.*
  The ABI makes it free now (base fields at offset 0, so an upcast emits nothing), and the
  analyzer refuses it: `expected '&Base', got '&Derived'`. **Left refused deliberately.**
  Measured across all fifty samples: no corpus site writes such a call, so there is no
  witness for the rule — and the neighbouring case is not obvious, since `&&Derived` → `&&Base`
  is almost certainly *no* (it would let a `Base*` be stored through a `Derived**`). ADR 0008:
  the layout makes it possible, a witness makes it ruled.
- ~~**Does a struct convert to an interface it implements?**~~ — **RULED 2026-08-28: yes, by
  value, from any addressable value.** One rule in `Type::isAssignableTo`, gated on the target
  being an interface and the source being a struct that is not one, and on `implements()` --
  the same predicate the declaration-site check uses, so the two passes agree by construction.
  One direction only: an interface does not convert back to a struct. → **ADR 0027** for the
  layout. Four tests in `Soundness_Interfaces`, including the two controls that matter (two
  unrelated structs still do not convert; a non-implementor still refuses).
- ~~**How is a field declared in an interface reached at run time?**~~ — **RULED 2026-08-28:
  field offsets in the vtable.** `{data, vtable}`, with one `i64` offset slot per required
  field ahead of the method pointers; a field read loads the offset, adds it to `data`, loads.
  The alternative -- forcing every implementor to place the interface's fields first -- was
  rejected because it is **unsatisfiable for a struct implementing two interfaces that both
  require fields**, and `love.fin` already has two such structs (only the accident that
  `Loser` and `Beautiful` are empty keeps it from failing). → **ADR 0027**, which also lists
  the five implementation steps in order.
  **Prerequisite, and it is not optional:** `KnownDefect_Interfaces.AMissingFieldIsAccepted`
  must be closed first. `implements()` never checks fields, so a struct missing a required
  field converts today -- and a vtable then needs an offset for a field the implementor does
  not have. This layout turns that latent hole into a live one.
  **All five steps landed at `aea960e` the same day, and the prerequisite is closed** (the test is
  now `Soundness_Interfaces.AMissingFieldIsRejected`). What did *not* land with them is a single
  test asserting a value through the reference; that debt was paid 2026-08-30 — §4, "The interface
  reference", has the seventeen tests and the four edges this ruling left open.
- ~~**Is `&string` the same representation as `string`?**~~ — **RULED 2026-08-28: no, it is a
  pointer to a cell holding the string**, so `*Complex` is the string and `&string` behaves
  like `&T` for every other T. The lifetime half was never the blocker: a literal's value
  exists before the program starts, so its holder can be static, and at module scope static is
  *forced*. A fresh holder per occurrence, because LLVM may merge the character data but the
  holder is mutable. `&make()` stays refused. Unblocked `variables.fin` (`81f2d05`).
- ~~**What does `#[slaveof(x)]` do to a local's lifetime?**~~ — **RULED 2026-08-28: nothing,
  today.** Both corpus forms ask for a lifetime at least as long as something else, and neither
  can be violated because **nothing in this backend frees anything implicitly** (ADR 0003:
  memory management is a library) -- measured, an object built from a scope that allocates
  references `malloc` and not `free`. So emitting nothing *satisfies* both requests.
  `ASlaveofAttributeKeepsItsAllocationAlive` asserts the consequence, not the no-op, so it goes
  red the day scope-based freeing arrives; `AnUnreadAttributeOnAVariableIsStillRefused` holds
  that no other attribute became acceptable.
- **How `fin_core` links LLVM on Windows** — *blocks `windows-x86_64` and `windows-arm64`.*
  `CMakeLists.txt` links the monolithic libLLVM (`target_link_libraries(fin_core PUBLIC LLVM)`),
  which LLVM cannot build under MSVC at **any** version: `llvm/CMakeLists.txt` sets
  `CAN_BUILD_LLVM_DYLIB` OFF when `MSVC` (lines 907–910 at `llvmorg-22.1.8`) and
  `llvm/tools/llvm-shlib/CMakeLists.txt` raises `"Generating libLLVM is not supported on MSVC"`. So
  the official `clang+llvm-22.1.8-*-pc-windows-msvc` tarball does satisfy
  `find_package(LLVM CONFIG)` and does pass the major check — and then the link fails for want of
  `LLVM.lib`. The alternative is `llvm_map_components_to_libnames` and the component static
  libraries, i.e. a second link strategy for one platform, which is a decision and not a fix. Not a
  version problem: it would read identically at 18.
- ~~**What the no-backend diagnostic should name as the required LLVM**~~ — **CLOSED, and
  this entry was stale when written.** `src/codegen/CodeGen_Stub.cpp:37` says "an LLVM **22**
  development install", not 18, and the mechanism the entry asked for exists: the major is
  spelled in the stub deliberately (that file is compiled exactly when `FIN_WITH_LLVM=OFF`,
  and on that path `find_package(LLVM)` never runs, so a `FIN_LLVM_MAJOR` compile definition
  would be a number nothing had checked against anything). What holds it in step with ADR
  0010 is `Soundness_Codegen.TheNoBackendHelpNamesThePinnedLlvmMajor`
  (`tests/test_codegen.cpp:5718`), which reads the major out of `CMakeLists.txt` and the
  string out of the stub, and is **not** a `BACKEND_TEST`, so it runs in either build. The
  entry's `grep "development install" tests/` is empty claim is false — it matches
  `test_codegen.cpp:5711` and `:5737`.
- ~~**Whether the checker-only build is required to pass its suite**~~ — **RULED
  2026-08-27: it must be green, and it now is.** `FIN_WITH_LLVM=OFF` measures **1291 tests,
  0 failed, 320 skipped**, ctest exit 0. The single failure was
  `MachineContract.DashOProducesTheNamedExecutable`, which asks for an executable in a build
  with nothing to produce one. Fixed in `25d6a0c` by splitting the assertion into the half
  each configuration owes rather than relaxing it: ON keeps what it had, and OFF gets
  `MachineContract.DashOWithoutABackendRefusesAndWritesNothing`, asserting the load-bearing
  half — `CodeGen_Stub.cpp` returns false rather than doing nothing precisely because
  `return true` would make `finc x.fin -o x` exit 0 having written no file. Deliberately
  **not** a `BACKEND_TEST`: that macro skips, and a skip is the quiet this ruling rejected;
  both configurations assert and neither is excused. **Caveat for anyone re-measuring:** finc
  finds its bundled stdlib at `<exe dir>/../lib/std` (`src/driver/SearchPaths.hpp:108`), so a
  build directory *outside* the repository fails 13 module and corpus tests for want of
  `lib/std` and **none of those failures is about the backend**. Symlink the repo's `lib`
  beside the build directory. `ci.yml`'s `if: ${{ !cancelled() }}` on the "no codegen test
  went quiet" step is now belt-and-braces rather than load-bearing.
- The tagged union's layout; whether `p++` advances by element or byte; the four nullability
  edges; what `&"Hello world"` means as a `&string`; `#[slaveof(...)]` lifetimes; struct `==`;
  whether a `class` is a value or a reference; which passes `-O`
  runs; namespaces; `pub` export; macros in imports; `Any<Printable>`; `cast<auto>`; variance;
  interface-member defaults; `GET_MEMORY_LIMIT`.
- On a **template**, whether `#[llvm_name="vec2_f32"]` names the template or one instantiation
  (the code reads it as the template's; nothing observable rides on it — see the comment on
  `llvmNameOf`).
- Whether `Point::make(1).get()` should copy to a temporary for a read-only method (written into
  the test that refuses it).
- Whether `a += b` on a struct should compose the declared `+` with a store, or want an
  `operator +=` of its own (written into
  `Soundness_Codegen.ACompoundAssignmentToAStructIsStillRefused`).
- `macro_rule`'s `LPAREN STRING_LITERAL RPAREN` keeps the quotes in `MacroRule::pattern`.
- A bare `null` binding a generic parameter is keyed and displayed as `string`.

- ~~**The size of an empty struct**~~ — **RULED 2026-08-27: one byte.** It matches C++,
  which finc is written in and interoperates with, so two distinct empty-struct values get
  distinct addresses and `&a != &b` holds. LLVM's zero-size `{}` was rejected precisely
  because it lets two empty-struct values share an address, and that surprise surfaces far
  from its cause. Unblocks `blame_assert.fin` (`an empty struct 'M<int>'`), which has no
  suppressed statements behind it.

- ~~**Whether an unimported `pub` name in `namespace std` resolves (the "prelude question")**~~ —
  **RULED 2026-08-27, and the question was two questions.** The invention half (`printf` is
  declared in 18 samples, used bare in exactly two, and is no builtin — `grep printf
  src/semantics` finds nothing) is answered by a **new attribute, `#[global]`**: a declaration
  marked `#[global]` is visible to every file in the compiler session with no import, opt-in per
  declaration, and **usable only inside the `std` namespace**. The owner's intent, quoted: "very
  good for having a std/io lib but then only making the printf function global for beginners but
  everything else". So `printf` in `lib/std/stdio.fin` is marked and **nothing else is**.
  `#[global]` written outside `namespace std` must be **refused, not ignored** — a silently
  ignored attribute is the same class of fault as a silently dropped statement. The ambience half
  is answered **no**: `Any` (`lib/std/types.fin:35`), `Enum` (`lib/std/enums.fin:17`),
  `getkeyid`/`keyidof` (`lib/std/enums.fin:24,31`) are all declared `pub` inside `#[export]` and
  all stay import-only. Note `getkeyid`/`keyidof` were never refusals; and
  `literal_struct.fin:30` is a third category, declaring `printf` gated inside
  `if (!@defined("printf"))`. `#[global]` appears nowhere in the tree — `.fin`, `.cpp`, `.hpp`,
  `.y`, `.l` and `docs/` all checked — so it is invention by instruction, the one case where
  inventing is correct. **Every corpus note that books something as "the prelude question" is now
  wrong in one of two ways and should say which half it means.** → ADR 0021.

- ~~**Integer conversions**~~ — **RULED 2026-08-27: implicit widening where no value is lost.**
  `int`→`ulong`, `int`→`long`, `ushort`→`int` are allowed; `ulong`→`int` is refused. The corpus is
  the evidence and the owner accepted it: `stdlib/stdio.fin` writes `ulong` lengths against `int`
  indices across eleven sites between `:110` and `:135` with **no cast anywhere**, and a language
  intending explicit conversion would have shown casts at those sites. Widths come from
  `src/types/Layout.hpp`, the compiler's single scalar table — a second list is how two answers
  come apart. Expected to clear 5 of `stdlib/stdio.fin`'s 19 diagnostics with no sample edit. →
  ADR 0022.

- ~~**The `@macro` declaration form**~~ — **RULED 2026-08-27: design it now.** Chosen over
  keeping `format!`/`map!`/`coll!`/`magic_add!` refused and over declaring `format!` alone.
  `tests/samples/macro_definitions.fin` is the only file that declares an `@macro` and it sits
  entirely inside a `/* [WIP] … (NOT DECIDED YET) */` block — the corpus saying in its own words
  that the form is undecided, which is also why that sample measures OBJECT_CLEAN. **ADR 0020
  already exists** on macro hygiene ("injected identifiers are fresh and only qualified paths are
  spelled") and this builds on it rather than replacing it. `format!` has 6 sites and may belong
  as a builtin rather than a declared macro; `importing.fin:23`'s `macros.magic_add!` raises
  whether a macro exports across a module boundary. Expected: 8 diagnostics, 3 samples. →
  **ADR 0023, design and plan only, no code.**

- ~~**What a `$type` value can do (wave 4)**~~ — **RULED 2026-08-27: open it properly.** Chosen
  over the narrow two-case fix, so the boundary at `src/semantics/impl/Analyzer_Core.cpp:161-163`
  is being **replaced rather than crossed** — it was drawn deliberately and says in as many words
  that comparison, instantiation and passing to `compiler.types.*` are wave 4. Two pieces of
  evidence for the narrow version survive as inputs: `implements` **is** already registered at
  `src/semantics/CompilerApi.cpp:32` matching `docs/compiler-api.md:490` and nothing binds
  `@implements` to it, and that same doc line asserts a `$struct` is accepted where a `$type` is
  asked (only argument *order* is checked). A third: `compatible` is a plain `fun` rather than a
  `@special`, so a `#[use(...)]` grant is not expressible there — evidence that a builtin
  `@`-special is not grant-gated. Also owed: why `implements` is registered when `defined`
  (`docs/compiler-api.md:643`) and `Alloc`/`Free` (`:668-669`) are declared and not registered,
  since all three are the same shape. Affects `literal_interface.fin` (flips),
  `stdlib/types.fin:26:74` and `literal_struct.fin:5:27`. → **ADR 0024, design and plan only.**

- ~~**What a failed `blame` does at run time**~~ — **RULED 2026-08-27: print and abort.** The
  location and message go to stderr, then `abort` with a non-zero status —
  `blame_assert.fin:5: assertion failed: Value must be positive`, and without the optional
  string, `blame_assert.fin:8: assertion failed`. `fprintf` + `abort`, no runtime and no
  unwinding. `llvm.trap` was rejected because it discards a message the corpus wrote on
  purpose. **What made this rulable is that this entry's own blocker was about the wrong half
  of the feature:** it read "the runtime shape of a raised value is not settled and there is no
  runtime", but **every `blame` in the codegen-refused corpus is the *assert* form** — a
  boolean condition with an optional string (`blame_assert.fin:5,:8`, `deeptest4.fin:16,:17`,
  `arrays.fin:34`, `readonly.fin:56`). The raise form appears only in samples already blocked
  in the front end (`stdlib/collection.fin:63,:70`, `stdlib/typing.fin:32,:38`), and
  `blame_assert.fin:15`'s raise form is commented out. Catchability was rejected for want of a
  witness: **no assert-form `blame` sits inside a `try`** — `readonly.fin`'s `try/catch` wraps
  `a.v1 = 5` and its `blame` at `:56` is outside it. Unblocks 5 refusals across
  `blame_assert.fin`, `deeptest4.fin` and `arrays.fin`, and is far smaller than `[T]`, which
  drags in the allocator.

- ~~**Struct inheritance layout**~~ — **RULED 2026-08-27: the parent's fields splice in at
  offset 0**, the child's follow, so a pointer to the child is a valid pointer to the parent.
  `src/types/Layout.cpp:439-440` already reads a base and starts the child's offset at the
  parent's size, so the type layer already half-implements it. **Deliberately queued behind
  `blame` anyway**, on measurement: permitting inheritance clears **zero** samples, because
  `readonly.fin` then needs a class declaration, `try`/`catch` and `blame`, and
  `stdlib/hashmap.fin` then needs a constructor on a struct. Parent *methods* were **not** ruled
  here — "report rather than invent" — and that is now the one open joint in this entry, because
  they are implemented. **Flagged for ratification, not presented as settled:** a call to a base's
  method, operator or static method through a derived value is lowered as a call to the base's own
  function with the derived pointer passed unchanged. Nothing new was invented to do it — the
  pointer identity *is* this ruling ("a pointer to the child is a valid pointer to the parent"), so
  no thunk, no upcast and no second body are needed, and the derived struct's own method wins over
  the base's by being found a level earlier. Everything that would have required an invention is
  still refused by name: a second base's method (its fields are not at the offsets its body
  indexes), one method name inherited from two bases at the same distance, and a `super::<P>::`
  qualified call (unparsed, untouched). If the owner wants a different rule — a thunk, an implicit
  copy, no inheritance of operators — the whole of it is `findProvider` and its three callers in
  `src/codegen/CodeGen_LLVM.cpp`, and eight tests name the behaviour.

- ~~**`format!`'s visibility**~~ — **RULED 2026-08-27: marked `#[global]`, alongside `printf`.**
  ADR 0023 ruled `format!` a compiler builtin rather than a declared macro, because
  `stdlib/stdio.fin:36` makes the format string a *runtime parameter*. Its visibility was the
  one joint that ADR could not settle: `deeptest2.fin` and `stdlib/error.fin` import nothing at
  all, so the name must resolve bare. Making the macro namespace ambient was the alternative
  and was rejected — it would put `format!`'s signature in two places, and one mechanism beats
  two. **This supersedes ADR 0021's "mark `printf` and nothing else"**: the marked set is
  `printf` and `format!`. Note `src/semantics/Scope.hpp:32,46` keeps macros in a separate
  namespace from symbols, so `#[global]` must span both; if it cannot sensibly, that is a
  finding to report rather than force.

- **Does an `@define` have to match the C declaration it names?** — **new 2026-08-31, and it now
  has a witness that runs.** `letssee.fin:4-6` declares `sin`, `cos` and `sqrt` as
  `(f: float) <float>`; libm's are `double(double)`. Since `132aed7` that sample lowers, links against
  `-lm` and **prints wrong numbers** — `Length of a: -76854900708868096.000000`, and
  `Vec2::normalize` then silently does nothing because its `if (len > cast<float>(0))` guard reads
  that garbage. Isolated with two probes: the `float` spelling reproduces the value exactly, and
  `@define sqrt(f: double) <double>` prints `5.000000`. So it is the prototype, not the lowering.
  Three ways out and they are not equivalent: **(a)** promote `float` to `double` at an extern
  boundary the way C's default argument promotions do — but the corpus has no implicit widening
  anywhere (§7) and this would be the first; **(b)** rule that an `@define`'s types are the C
  types and a mismatch is the author's error, which makes this sample's own lines the bug and ADR
  0008 then says the corpus is right and the compiler must not silently "fix" it; **(c)** leave it,
  and accept that a sample in the corpus produces wrong output while exiting 0. **The corpus is the
  specification, so nobody may edit those three lines to settle it.** Blocking nothing today: the
  sample is OBJECT_CLEAN either way, and this is about what it *prints*.

### A warning about this section itself

Two entries here were wrong in a way that cost real work, and both failures were the same
shape: **the entry described a state the tree had already left.**

`docs/HANDOFF.md` listed *the size of an empty struct* among the unresolved rulings while
`Soundness_Layout.AnEmptyStructHasNoBytes` had already decided it, in a test carrying an
argument. The question was put to the owner as open, was ruled the other way, and the two
passes then disagreed about a size until `191a59a` — which is a miscompile, not a
discrepancy. **Check whether a test already rules on a question before calling it open.**

The no-backend LLVM-major entry asserted that `grep "development install" tests/` is empty
and that the stub still said 18. Neither was true when it was written: the stub said 22 and
two lines of `test_codegen.cpp` matched. **Re-take a citation rather than trusting the entry
that carries it.**

A third, smaller: `blame` was booked as blocked on the runtime shape of a raised value, which
is true of the raise form and true of no sample that was actually blocked by it.

## 9. Documentation still owed

Write the prelude ruling into `const.fin`, `interfaces.fin`, `enums.fin`, `useful_macros.fin`,
`stdlib/typing.fin`, `stdlib/stdio.fin`, `stdlib/operators.fin`, `stdlib/enums.fin`,
`deeptest2.fin`, `stdlib/error.fin`. `importing.fin`'s note still opens with a stale
"module not found". Re-verify `stdlib/stdio.fin`'s `keyidof` / `getkeyid` references at lines
57, 65, 71.

**Two guide debts are paid at `55674d7` (2026-09-01), both by re-measuring rather than by
re-reading the note that created them.**

`docs/guide/12-standard-library-tour.md` said `Error`'s "constructor takes one argument, not
the draft's two, because a defaulted parameter is still required at the call site". That was
true when written and stopped being true at `d7a91df`; `lib/std/error.fin:65` has carried
`Error(msg: string, err_code: int = -1)` since, and its own header says so. Both arities are
calls, three arguments and none are `expects between 1 and 2 arguments`, and `describe()` and
`has_code()` were undocumented. Two tests now hold it —
`Soundness_BundledStdlib.TheErrorSurfaceResolves` and
`TheErrorConstructorTakesOneArgumentOrTwoAndNoOther` — because seven samples import this
struct and the arity was already wrong once in a comment nothing measured. The suite is 1566.

The `stdptr` sections of chapters 12 and 9 still said "the counter is never incremented and
ownership is not enforced against aliases", quoting a header `82cc8a8` replaced. The counters
are `&int` handles shared between every handle over one value, so `refs()`, `borrows()`,
`alias()`, `readonly_view()` and `weak()` exist and answer about the value; both chapters now
describe that and keep the two claims that are still true — a raw `&rptr<T>` copied past a
`release()` is invisible to the library, and an `rptr` does not reach an executable. The
existing `TheSmartPointerSurfaceResolves` already covered the surface, so this half needed no
new test, only the correction.

What is still owed there: `lib/std/stdptr.fin` was rewritten at `82cc8a8`, so
`tests/samples/stdlib/stdptr.fin`'s line 3 ("this file needs rewriting") is answered for the
bundled module and **not** for the draft — the draft's own three remaining blockers
(`pointer_type`, `own`'s missing return, the two constructor requirements) stand, and per ADR
0008 the sample is not to be edited to match.

## 10. Suggested skills for the next agent

Call the `Skill` tool for:

- **`tdd`** — matches the user's process mandate exactly (tests first, then implement).
- **`diagnosing-bugs`** — when a `Soundness_*` test goes red and the cause is not obvious.
- **`domain-modeling`** — when a unit needs a new ADR (`docs/adr/`, format in
  `.agents/skills/domain-modeling/ADR-FORMAT.md`). 20 ADRs so far, `0001`–`0020`.
- **`code-review`** — before a large unit lands, reviewing since `91312b8`.

Do **not** use `research` (the user banned research). Keep agent use minimal — "we are low on
credits" — and bundle several sections per agent rather than one agent per section.

---

## Commit-message style

The repo's commit messages are the design record. They are declarative sentences stating the
rule, then the reasoning, then the measurement. Look at `91312b8` and `848fde1`. Every message
ends with the numbers (`1247/1247. 27 ok / 83 diagnostics unchanged. Codegen-clean samples
9 -> 10`) and:

```
Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
```
