# Plan: finish the Fin compiler

Goal: `finc hello.fin -o hello && ./hello` prints. Everything below is ordered toward that.

Governing constraints from the project owner: tests come before implementation, or alongside it via
parallel agents; the sample corpus is the specification; four tracks run concurrently — compiler,
standard library, `finn`, `finn-registry` — and they sync through written contracts, not conversation.

The measured starting point is `docs/baseline.md`. The two numbers that shape this plan: **11 of 50
samples survive the compiler**, and **exactly one sample reaches the semantic analyzer**. So the
frontend is the bottleneck for everything, and the ~1,940-line analyzer has no evidence behind it at
all.

## Soundness defects — wrong code compiles clean

These are not missing features. Each one accepts a program it should reject, silently, and each is
verified by running the binary. They are called out separately because a missing feature fails loudly
and costs a user an afternoon, while these cost them a wrong program. All of them are invisible today
only because almost nothing reaches the analyzer. (No count here on purpose: this list was written with
four items and has grown every time anyone probed for more, and a number in the sentence above is a
claim nobody updates. It had one — "and has six" — which went stale within the day, which is the
argument for the rule rather than against it.)

**Interface fields are not checked at all.** A struct declaring `: <I>` where `I` requires `x <int>`,
carrying no `x`, compiles successfully. Omitting a required *method* is caught; omitting a required
field is not. `Analyzer_Decl.cpp:367` resolves interface member types without ever calling
`defineField`, so `StructType::implements` (`StructType.cpp:114-130`) has no fields to compare and
checks methods and constructors only. Every interface contract in the standard library is therefore
decorative — this is the most serious item in the plan.

```fin
interface I { x <int>; }
struct S : <I> { y <int>, }     // Build Successful.
```

**Integer widths are a lie.** `uint{8}` and `uint{64}` are the same type; assigning one to the other
succeeds. `resolveTypeFromAST` (`Analyzer_Core.cpp:142-146`) walks the width annotations for side
effects and returns the *unannotated* type. `lib/std` defines `i64`, `i128`, `u64`, `u128` and
`size_t` on top of this, so all of them currently collapse onto `int`/`uint`. Harmless until codegen
exists, then it is wrong machine code.

**Field declaration order is destroyed.** `StructType::fields` is
`std::unordered_map<std::string, FieldInfo>` (`StructType.hpp:28`) and `defineField` (`:37`) is its only
writer, so the order a struct's fields were declared in is lost at the moment the semantic type is
built — the AST has it, the type throws it away. Struct layout, and therefore ABI, offsets and every
pointer map, is a function of declaration order. Latent exactly as long as codegen does not exist, and
worse than it looks: iteration order over an `unordered_map` is not even stable across runs of the same
binary, so this cannot be relied on to fail consistently once it does matter.

**The raise half of `blame` is rejected.** `Analyzer_Stmt.cpp:113-129` requires the first operand to be
`bool` and the second `string`, so `blame SomeError("...")` fails with `expected 'bool', got 'E'`.
`CONTEXT.md` ratifies one keyword doing two jobs and the analyzer implements one. Per ADR 0008 the
normative samples convict the compiler, so the analyzer changes — but note this is a *semantic* gap,
not a parser one, and it appears eight-plus times in the standard library.

**Conditions are not type-checked, and the check is sitting in the file commented out.** `if (1)`,
`if ("s")`, `if (n)` for an `int` `n`, `while (1)` and a `for` header's `1` all compile clean. What
makes this a defect rather than a design choice is that the analyzer has already answered the question
twice in the other direction, once in the same file: `let b <bool> = 1;` is rejected, and `blame 1;` is
rejected by the bool check at `Analyzer_Stmt.cpp:116` — the assert half of the item above. The same
check is *written* for `if` at `Analyzer_Stmt.cpp:34` and commented out, justified as "(optional, C++
allows int)", a C++ premise the language refuses twenty lines away. `WhileLoop` (`:39`) and `ForLoop`
(`:47`) do not even carry the commented-out line, which is why the fix is three places and not one —
measured, by uncommenting line 34 and watching the `while` and `for` cases stay green.

The corpus does not license the loose reading: every condition in all fifty samples is boolean-valued,
and nothing writes `if (ptr)` or `if (n)` — nullability is always spelled `== null`, in both `//@ ok`
samples that have a condition at all. One sample writes `if (0)`, twice, and is normative:
`undefined_behavior.fin`. It licenses nothing, for a reason that belongs to the harness rather than to
that file and is recorded in 1a — its expectation is `//@ error`, which this runner reads as *at least*
this diagnostic, so an `//@ error` sample can never make anything legal. `if (0)` there is the C idiom
for a provably-dead branch, written to demonstrate return-path analysis.

**This one needs a ruling before it can be fixed**, which is why it is pinned by
`KnownDefect_Conditions.*` rather than closed. Enabling the check makes a *normative* sample emit two
diagnostics its annotation does not mention, and under ADR 0008 a normative sample changes only by a
deliberate language decision — so enabling the check and rewriting that sample's `if (0)` to
`if (false)` is one decision, not two edits. Measured cost either way: the sample's test keeps passing
regardless, because of the same *at least* semantics, so nothing forces the question and it will sit
here until someone rules. If the ruling admits truthiness, delete the tests *and* the commented-out
line, so the next reader does not find the contradiction and reopen it.

**Operators are checked against each other rather than against themselves — two defects, one shape.**
`Analyzer_Expr.cpp:176` closes `visit(BinaryOp&)` with `checkType(node, rightType, leftType)`: it asks
whether the two operands agree and never whether the operator accepts them. So `"a" - "b"`,
`"a" % "b"`, `true + false` and `true / false` all compile and take the operand's type, while
`1 + "s"` is rejected for the wrong reason — the operands disagree, not that `+` is undefined on
strings. `Analyzer_Expr.cpp:207` is the same hole in `visit(UnaryOp&)`: `&` and `*` are handled and
everything else falls into a bare `else { lastExprType = type; }`, so `!` on an integer yields `int`
and `-` on a string yields `string`.

The unary half hides behind whatever binding it appears in, which is why it survived the first sweep.
`let a <bool> = !1;` *is* rejected, with "expected 'bool', got 'int'" — from the initialiser, because
`!1` was typed `int`. Write `let a <int> = !1;` and it compiles. Every test for this states the type
it expects the expression to have, because an exit-code assertion here measures the binding instead of
the operator.

**Unlike the conditions defect, neither of these needs a ruling, and that was measured rather than
assumed.** A sketch fix for both was compiled into a private binary and run over all fifty samples:
**no sample's result changes**. Nothing in the corpus licenses the loose reading either — there is no
`+` on strings anywhere in the fifty, string building is `format!(...)` throughout (`deeptest2.fin:26`,
`error.fin:19`, `stdio.fin:36`). And the same function already does this correctly twice, at `:158`
for `&&`/`||` and `:148` for struct overloads, so the fix is a third case of a shape the file
contains, not new machinery.

**How much that zero proves, stated rather than implied:** of the 16 `//@ ok` samples, 4 contain binary
arithmetic and **0 contain a unary operator at all**. So the corpus is thin evidence for the binary
fix and *no* evidence for the unary one — a mistake in `visit(UnaryOp&)` cannot be detected by any
sample, and `KnownDefect_UnaryOperators.*` plus `Soundness_UnaryOperators.*` are the only guard there
is. Worth knowing before trusting a green run on that half.

Deliberately not asserted: comparisons at `:168` share the shape and accept `"x" < "y"` and
`true < false`. Ordering strings is a real operation in many languages and C++ orders `bool`, so "the
operator accepts these operands" may be the right answer for `<`. The defect is that the question goes
unasked; where the answer would be yes, no test should pretend otherwise.

**Redeclaring a name in one scope silently replaces the first, and the corpus already ruled on it.**
`Scope.hpp:30` is the entire defect — `void define(Symbol sym) { symbols[sym.name] = sym; }`, where
`operator[]` assigns over an existing key. Both `let` and `fun` reach it, variables at
`Analyzer_Decl.cpp:27` and functions at `Analyzer_Decl.cpp:86` registering into the parent scope, so
one line produces both symptoms and the later declaration wins with no diagnostic. A second `fun f`
with a different return type silently makes the first unreachable.

This one needed no owner ruling because the corpus answered it in writing. `stdlib/stdio.fin:33`
declares a second `printf` under `#[overwrite(printf)]`, annotated in the sample itself as *"this
tells the compiler we are overwriting printf and it will ignore the current definition of it and
doesn't raise an error"*. An attribute whose stated job is to suppress the error presupposes the
error, and `stdio.fin` is normative — so a bare duplicate must be rejected, and silently taking the
second is exactly what `#[overwrite]` exists to have to ask for. This is the one place so far where a
soundness question was settled by a hand-written comment in a sample rather than by a ruling, which is
an argument for reading the comments as part of the spec and not as decoration.

**Two defects currently cancel in that sample, and the fix has to land in the right order.** Nothing
interprets an attribute yet (see below), so `#[overwrite]` does nothing — and the duplicate it guards
is accepted anyway, because no duplicate is ever refused. Fix `Scope.hpp:30` alone and `stdio.fin`
starts failing on the very construct it was written to demonstrate. So this repair is blocked on
attribute interpretation in wave 4, which is the reason it is recorded here rather than filed as the
one-line fix it otherwise is.

Not defects, probed and dismissed: shadowing in a nested scope, and a `let` shadowing a parameter.
Both are accepted, neither is a redeclaration in *one* scope, and `Soundness_Duplicates.ShadowingInANestedScopeStillWorks`
pins the first so that the cheap repair — reject any name that already resolves — cannot pass for a fix.

**Characterised and dismissed: a call must follow its declaration, and that is the design, not a bug.**
A probe found `fun main() <void> { g(); } fun g() <void> {}` rejected with `Undefined function or type
'g'`, and mutual recursion between two plain definitions impossible for the same reason —
`Analyzer_Core.cpp:195` walks the top level once in source order, and a function registers itself
partway through its own visit (`Analyzer_Decl.cpp:86`), after its predecessors and before its body.
That reads like a serious defect and is not one: declaration-before-use with an explicit prototype is
C's design, **zero** of the fifty samples call a top-level function declared later, and eight of them
open with `@define printf(fmt: string, ...) <noret>;` (`letssee.fin:3`, `enums.fin:5`, `complex.fin:5`,
`readonly.fin:5`, `hashmap.fin:10`, …). The mechanism works, mutual recursion included, and
`Soundness_Declarations.*` now pins both — nothing asserted them before, though eight samples depend
on them. Hoisting every top-level signature would make `@define` decorative and change what those
eight samples demonstrate, so it is a language decision under ADR 0008, not a repair. Recorded because
a finding characterised and dismissed is worth as much as one confirmed and cheaper to write down than
to re-derive — and because the probe alone is persuasive enough to send someone off to "fix" it.

**`cast` is wrong in both directions at once — too broad, and too narrow exactly where the corpus needs
it.** `Analyzer_Expr.cpp:361` admits any primitive to any other, and `Analyzer_Core.cpp:21` registers
`string` as a `PrimitiveType`, so `cast<int>("s")`, `cast<string>(1)` and `cast<bool>("s")` all pass.
Turning a string into an int is parsing and the reverse is formatting; neither is a reinterpretation of
a value, which is what a cast is.

The same rule refuses **both** casts written anywhere in the fifty samples, and that is the half that
matters. `cast<[char]>("SomeData for testing")` (`stdio.fin:156`) fails with "Invalid cast from
'string' to '[char]'" because `[char]` is an `ArrayType` and the pair is therefore not two primitives;
`cast<&auto>(base_val)` (`deeptest2.fin:26`) fails identically for a `PointerType`. A rule permissive
enough to convert an int to a string while refusing the only two real uses in the corpus is not erring
towards permissive — it is not looking at the question.

**Both corpus casts are masked**, which is why this sat unseen: those two samples are `//@ unimplemented`
for unrelated reasons, so neither cast is reached and nothing goes red. Whoever fixes this should expect
those two samples to be the acceptance criteria rather than expecting to find them in a failing list.
`KnownDefect_Casts.TheCorpusOwnStringToCharArrayCastIsRejected` and `.TheCorpusOwnPointerCastIsRejected`
therefore assert a *rejection* — the opposite shape to every other test in that file, because here the
defect is that a legitimate cast fails, so the assertion that must eventually flip is `EXPECT_NE`. All
four cast defects were mutation-verified: a sketch fix flips the two too-broad tests to rejected and the
two too-narrow ones to accepted, both controls hold, and no sample's result changes.

**One grammar gap found underneath it, which belongs to wave 2 and is reported there rather than fixed
here:** `cast<&int>(p)` is a syntax error — "unexpected TYPE_INT, expecting IDENTIFIER" — while
`cast<&auto>(n)` parses. The cast's type argument accepts `&` before an identifier but not before a
builtin type keyword, so the pointer half of this defect can only be written down with `&auto` today.

**A generic bound is never enforced, and on a function it is never even read — `debugLog` is the only
consumer of a resolved constraint in the compiler.** Two sites, two different failures.
`Analyzer_Decl.cpp:40-42` registers a function's generic parameters without mentioning
`gen->constraint` at all, so `fun f<T: NoSuchI>(x: T)` compiles and a typo in a bound is silent.
`Analyzer_Decl.cpp:117-122` and `:560-567` do read it — they resolve the bound, which is why the same
text *is* rejected on a struct, and then pass the resolved type to a debug message and drop it. It is
never stored on the `GenericType` and never consulted at an instantiation, so `S<int>` satisfies
`<T: I>` for every `I`.

The asymmetry is the useful part: one construct rejected on a struct and accepted on a function
localises the first fix to three lines that already exist twenty lines away in the same file. **Verified
by mutation, including that the split between the tests is real** — adding just that resolve call flips
`KnownDefect_GenericBounds.ABogusBoundOnAFunctionIsAccepted` to rejected and leaves the other two
passing, which is what the tests predict in their own comments. Resolving a bound is not checking one,
and the second fix — storing it and checking arguments against it — is real work that wave 3's
monomorphisation needs regardless.

**This is the standard library's largest exposure after interface fields**, and for the same reason: a
signature documents a guarantee the compiler does not make. `stdio.fin` declares `<X: Any<Printable>>`
three times and `<T: Strict<Stream>>` once, and the erasure markers `Any` and `Castable` are *spelled as
bounds* (ADR 0018) — so "bounds are decorative" means the erasure machinery has nothing to read, and
the decision of whether a generic is monomorphised or erased is currently being made on information
that is thrown away. Worth weighing when wave 3 sequences generics: the bound has to be stored before
either path can branch on it.

**`any` is not a registered type.** `Analyzer_Core.cpp:17-34` never registers it, so `fun f(x: any)`
gives `Undefined type 'any'`. It lexes and parses fine. This is the single highest-leverage fix in the
tree for the standard library: all 25 interfaces in `operators.fin` take `other: any`, plus
`types.fin`, `prototypes.fin` and `enums.fin`.

**Attributes are inert** — but only in the second half now, and the halves are worth separating
because they belong to different waves and different owners. The AST half is **done**:
`Attribute::accept` dispatches (`Attribute.cpp:8`), `Visitor.hpp:109` declares `visit(Attribute&)`,
`StructuralWalk` and `CloneVisitor` traverse them, and `parser.y:50` has the `ClassDeclaration` branch
the `dynamic_cast` chain was missing, so `#[export] class C {...}` no longer drops its attribute
silently. What remains is that **nothing interprets one**: `attributes` appears nowhere in
`src/semantics/` or `src/driver/`, so `#[export]`, `#[llvm_name=...]` and `#[use(compiler)]` reach the
AST and stop there. That is wave 4's problem rather than a parser one, since `#[use(compiler)]` is the
gate on the component API and there is currently nothing behind it — and it is the reason the parser
half being closed is not progress on the component API.

**Every defect above is pinned by a test, and the tests are the reason this section can be trusted.**
`tests/test_soundness.cpp` holds them under two suite names that mean opposite things. A `Soundness_*`
test asserts a property that must hold and must never be allowed to fail. A `KnownDefect_*` test passes
today *by asserting the defect* — `KnownDefect_Interfaces.AMissingFieldIsAccepted` fails when a missing
interface field is finally rejected. So a red `KnownDefect` is good news, and the response is to invert
the assertion and rename it into `Soundness_`, never to relax it. A `KnownDefect` is not a licence:
each one accepts a wrong program, so each is worth more than whatever feature work is queued behind it.

**The tests were verified to bind, not merely to pass.** A green `KnownDefect` proves nothing on its
own: a test that asserts `exitCode == 0` also passes when it is asserting the wrong thing, or nothing
at all. So for each of the three defects added most recently, a sketch fix was compiled into a private
binary in `/tmp` — a symlinked source tree so agent C's `build/` is never raced — and every assertion
run against both. All seven `KnownDefect` cases flipped to rejected and all nine `Soundness_` controls
held. The controls are the half that costs the fix something: `Soundness_BinaryOperators.LogicalAndStillRequiresBooleanOperands`
fails if the repair went in by deleting the working case at `:158` instead of joining it, and
`Soundness_Duplicates.ShadowingInANestedScopeStillWorks` fails if `Scope.hpp:30` was fixed by refusing
any name that already resolves. Neither failure mode is visible from the defect tests alone.

Two rules came out of writing them, both learned the hard way. **Probe the binary before writing the
test.** Doing that caught one defect in this list that had already been fixed, two reproducers that
failed for a different reason than the one claimed (a width test that actually failed on signedness, a
`blame` test that failed on enum member access), and it surfaced a seventh defect the census could not
see — see the boolean-literal note in wave 2. **And bind the test to the documented cause, not just to
the wrong exit code**, so it cannot keep passing for a new reason after the old one is gone: the width
tests assert `expected 'uint'` is present and `uint{8}'` is absent, `KnownDefect_Blame` asserts the
`expected 'bool'` text, and `KnownDefect_AnyType` additionally asserts *no* `syntax error`, which is
what stops the fix from being attempted in the lexer. Two of these defects cannot be observed from the
CLI at all — a dropped attribute and a scrambled field order change no exit code — so those tests read
the AST and the semantic type directly. Anything asserted only through `finc`'s exit status would have
missed them.

## Ownership map

File-level ownership, so parallel agents never edit the same file. This is the mechanism that makes
the waves below safe.

| Owner | Files | Agent |
| --- | --- | --- |
| Harness | `tests/**` — including every `.fin` sample | A |
| Contract | `src/main.cpp`, `src/driver/**`, `src/diagnostics/**`, `src/utils/ModuleLoader.*` | A |
| Build/CI | `.github/**`, `CMakeLists.txt`, `conanfile.py`, `build.sh`, `.gitattributes` | B |
| AST infra | `src/ast/ASTNode.hpp`, `src/ast/Visitor.hpp`, new `NodeKind.hpp` / `StructuralWalk.*` | B |
| Frontend | `src/lexer/lexer.l`, `src/parser/parser.y`, `src/ast/nodes/**`, `src/ast/decls/**` | C |
| Analyzer | `src/semantics/**`, `src/types/**` | C |
| Expansion | `src/macros/**`, `src/preprocessor/**` | C |
| Stdlib | `lib/std/**` | standing track |
| finn | `~/finn` | standing track |
| finn-registry | `~/finn-registry` — not ours, another agent owns it | — |

Five agents across the whole plan rather than eight, at most three alive at once. A and B run
concurrently in wave 1; C takes waves 2 and 3 and then hands `src/ast/**` back for wave 5's codegen
owner, which is the only handoff in the plan.

Two ownership rules earn their keep. **The Harness owner owns the sample files**, because annotating
all fifty and normalising their line endings touches every one of them and nobody else may edit them
concurrently. That job is one-time and it is now done, so **the `//@` expectation lines transfer to the
Frontend owner for waves 2 and 3**. They have to: a construct that starts parsing turns its
`unimplemented` expectation into a lie, the harness catches exactly that, and an agent who may fix the
parser but not the line recording what the parser does cannot leave the suite green. The transfer is
safe only because the Harness owner has finished — it is not a general licence for two owners on one
file.

The transfer covers the `//@` lines and nothing else. **Sample code changes only through a ratified
language decision**, never to make a failing test pass: a normative sample convicts the compiler
(ADR 0008), so editing the sample to agree with the compiler destroys the only evidence the plan runs
on. The ratified edits are listed at the end of wave 2 and that list is exhaustive. **The Frontend owner owns `parser.y` including its `%token` declarations**, because a
lexer change and a grammar change to the same token are one change, and splitting them across two
agents produces a build that neither can fix alone.

Two narrow exceptions to the map, both in wave 1, both granted because the Frontend owner does not exist
until wave 2 and so there is no concurrent writer to collide with. **Agent A may edit `src/lexer/lexer.l`
in the error-reporting path only** — 1b requires routing the raw `cerr` write at `:214` through the
`DiagnosticEngine`, which touches no rule, token or action, and the rule above is about token
declarations rather than about how errors are reported. **Agent B may edit the clone-bug root only** — which is `src/ast/types/Attribute.cpp`,
`src/ast/types/GenericParam.cpp`, `src/ast/Visitor.hpp` and the `src/ast/cloning/**` plumbing that reaches
them. This exception named `src/ast/nodes/**` until it was checked against the tree: `src/ast/` has
`nodes/`, `types/`, `cloning/`, `decls/`, `exprs/` and `stmts/`, and the two inert `accept` bodies are in
`types/`. The grant follows the defect, not the guess, since fixing it in `CloneVisitor` instead is
precisely what ADR 0004 says not to do. Both agents must report every file touched under the exception,
so wave 2's agent inherits a known state rather than discovering one.

## Agent economy

The table above partitions *files*. It does not follow that each row gets its own agent, and this
section is the correction: **one agent owns several rows and several tasks.**

The cost of an agent is not what it writes, it is what it reads before writing anything. Every agent
starts by reading `CONTEXT.md`, the ADRs its work touches, this file and `docs/baseline.md` — on the
order of fifteen hundred lines before the first useful edit, and that cost is the same whether the
agent then does one task or five. Four agents on one wave pay it four times for one wave's work. So
tasks are grouped until each agent has a coherent theme and a bounded scope, and only then split.

Three rules, all of them cost rules rather than taste:

**Sequential waves share an agent.** Waves 2 and 3 cannot overlap, so splitting them buys no
parallelism and costs a second startup plus a handoff document. The agent that adds a grammar
production already knows what the AST now contains, which is exactly what wave 3 needs, and handing
that across an agent boundary means writing it down and re-reading it.

**No research agents.** Nothing in this plan is unblocked by reading another language's
documentation. Comparative work happens once, inline, at one paragraph per language, and an
unresolved comparative claim is recorded as an open question in the document that needs it rather
than spent an agent on. This rule is retrospective: the D survey returned one genuinely load-bearing
finding — a working precise garbage collector whose entire compile-time component is one per-type
template in the standard library, now recorded in ADR 0007 — and six sibling surveys returned
nothing that changed a decision.

**Verification is `bash`, not a second opinion.** Checking a claim by grepping for it costs a command;
checking it by spawning a reviewer costs a context. Every load-bearing claim in this plan that turned
out to be wrong was caught by the former: the `Build Successful.` reproducer, the corpus census run
against the wrong glob, `pyprototype` being cited as authority, and a contract reply that answered the
previous revision.

Concurrency cap is **three**, counting the standing tracks. `finn-registry` is not ours and does not
count against it.

## Wave 1 — foundations, two agents in parallel

Nothing here depends on anything else here, so the split is by theme rather than by dependency.
**Agent A owns 1a and 1b** — the harness and the machine contract are one agent because the harness
*consumes* the contract: 1a reads a `//@` expectation and compares it against what `finc` printed, and
1b decides what `finc` prints. Split across two agents, each guesses the other's half and they
disagree at the seam. **Agent B owns 1c and 1d** — build/CI and AST infrastructure share no files at
all, which is why they are safe to merge: both are prerequisite-shaped, both are bounded, and neither
needs the other's context, so one startup covers both.

**1a. Harness and corpus annotation.** Build the expectation runner: read the `//@` line, run `finc`,
compare. Three states — `ok`, `error <line>:<col> "<msg>"`, `unimplemented "<reason>"` — and a
missing annotation is a harness failure, not a skip (ADR 0008). Delete the existing
`FileParserTest`, which asserts all fifty samples parse and therefore cannot express the corpus's own
authority model. Fix `GetFinFiles()` being called during static initialisation, where a wrong working
directory silently registers zero tests and reports success. Annotate all fifty samples from the
census in `docs/baseline.md`. Normalise the seventeen CRLF files to LF in the same pass. One `.fin`
file lives outside the samples tree — `tests/addits/plor.fin` — and must be ruled on rather than left
ambiguous: either it is a sample and moves under `tests/samples/` with an expectation, or it is scratch
and the harness excludes it by an explicit rule. Silently globbing it in is the ADR 0008 failure.
Extend the suite past parsing — `parseString` currently stops at the parser, so `SemanticAnalyzer`,
`ModuleLoader` and `MacroExpander` have no unit tests whatsoever.

**One defect in the harness itself, found and fixed here, with a maintenance rule attached.** The census
test asserted `ok == 11` and `unimplemented + error == 39`, with a comment saying the numbers would be
updated deliberately. Wave 2's entire job is to move samples from `unimplemented` to `ok`, so that
assertion was engineered to go red on every single unit of progress — in a harness file the agent making
the progress does not own, which turns each improvement into a cross-owner edit and trains everyone to
read a red census as noise. It was red at 16 passing when it was found. An equality there protects no
property; it taxes the work.

It is now two tests over an assertion-free tally. `Census.EverySampleIsAnnotatedAndClassified` asserts
the invariants — fifty samples, every one labelled normative or aspirational, every one carrying at least
one expectation, and every expectation classified as exactly one of `ok`/`unimplemented`/`error` — and
records the tallies as test properties rather than asserting them. "At least one" rather than "exactly
one" is deliberate: all fifty carry exactly one today, but a sample demonstrating two errors is a
reasonable thing to write and the harness should not be what forbids it.
`Census.ThePassingSampleCountNeverFalls` holds the ratchet: `constexpr int kFloor` with `EXPECT_GE`, and
a note to stderr when the live count is above the floor. **The rule is that whoever raises the passing
count raises `kFloor` in the same change** — the floor is the only part that needs maintenance, and it
is safe to lag, because a stale floor is merely weaker than it could be while a stale equality is
outright wrong. It is at 16.

**One open question about what `//@ error` means, with its cost measured.** The runner reads
`//@ error` as *at least this diagnostic*: it searches stderr for the message and for a diagnostic at
the position, and never asserts that it was the only one. Two consequences, and the second is a real
gap. An `//@ error` sample cannot **license** anything, so it is not evidence that the rest of the file
is legal Fin — that is what makes `undefined_behavior.fin` silent on `if (0)`. And an `//@ error` sample
cannot **detect** a new unrelated error appearing: enabling the condition check at
`Analyzer_Stmt.cpp:34` makes that sample emit two extra diagnostics and its test goes on passing. A
spurious diagnostic is a real defect that misleads a user, and today no sample can catch one.

Making the form exhaustive would close it, and the migration cost is measured rather than guessed:
exactly one sample uses `//@ error`, it emits two errors against one expectation, so the whole change is
one added `//@ error` line. It is not done here because it changes what the specification format means,
which is ADR 0008's authority model and the owner's call rather than the harness's to take on its own.
The current semantics is now pinned by `ExpectationRunner.AnUnrelatedExtraErrorIsTolerated` so that it
is at least deliberate — it was accidental until now, asserted in neither direction.

**A second harness defect, and this one would have been blamed on the platforms.** The suite failed
eleven tests under `ctest -j8` and passed all eleven on rerun. Four temp-file helpers — one per test
file, each grown independently — named their files `<prefix>_<counter>` from a per-process
`static int counter`. `ctest -j` runs every test in its own process, so every worker restarts the
counter at 0, two concurrent tests get the same path, and each one's destructor deletes the other's
file. There is now one `uniqueTempPath()` in `tests/Corpus.hpp` and every temporary path in the suite
goes through it; a hardcoded `/tmp/fin_o_flag_target` went with it, which asserted its target is *not*
created and so would have failed permanently on one stale file from any earlier run.

Which part of the name does the work was measured rather than assumed, because the first fix credited
the wrong half. Rebuilding the helper three ways and running `ctest -j8` three times each:
`<prefix>_<counter>` fails 27, 30 and 31 tests — *the count varies per run, which is the signature to
recognise*; adding the pid is green three times; adding the test name instead is green three times. So
either discriminator closes the hole, neither is load-bearing over the other, and both are kept for
different second cases: the test name makes a file leaked by a crash name its own leaker, and the pid
makes two concurrent `ctest` invocations safe — a real case here, where more than one agent runs the
suite against one build directory.

This is worth more than the eleven tests. Wave 1's exit criterion is *CI green on all six
platform/arch combinations*, CI runs the suite in parallel, and a race like this surfaces there as
different tests failing on different runners — which reads exactly like a platform problem and would
have been debugged as one, on six platforms, by whoever owns 1c rather than by whoever owns the
harness.

The other half of the census fix is subtler and is the reusable part: **the shared `census()` helper
asserts nothing.** It collects malformed samples into a `faults` vector and the classification test adjudicates
them. With `EXPECT_*` inside the helper, one malformed sample failed *both* census tests, which made "a
sample regressed" and "a sample is malformed" produce the same failure signature — the exact confusion
a census exists to prevent. Confirmed by mutation afterwards: stripping one sample's expectation,
corrupting a label, and deleting a sample each kill exactly one census test, the right one, with the
other green.

**1b. The machine contract** (ADR 0009). Exit codes `0/1/2/3`. Diagnostics to stderr, stdout
reserved. Strict argv: unknown flag → `2`, second positional → `2`. Gate success on
`diag.hasErrors()` as well as `analyzer.hasError` — the driver never calls `hasErrors()` today, which
is half of why `Build Successful.` can print with errors on screen. Route `lexer.l:214` and
`ModuleLoader.cpp:104,113` through the `DiagnosticEngine`. Distinguish an empty file from a missing
one so an empty `.fin` compiles. Honour `NO_COLOR` and non-tty. Accept and store `-o`, ignore it until
codegen. Add `finc --version` as `finc <semver> (contract <int>)`. Then `--diagnostics=json`: JSONL on
stderr, one object per diagnostic, trailing `{"kind":"summary",...}`.

Library search paths land here too, because the code is `src/driver/**`. `FIN_LIBS` was split on a
hardcoded `':'`, so a Windows path became a relative `C` and a rootless `\libs`; the separator is now
`kSearchPathSeparator` in `src/driver/SearchPaths.hpp`, `;` on Windows and `:` elsewhere, with empty
entries dropped rather than read as the working directory. And `--fin-libs` now exists at all — it did
not, although `tests/samples/importing.fin:9` states that `finn` passes it. It replaces `FIN_LIBS`
rather than extending it, for the reason recorded in ADR 0009: a pinned build must not pick up a path
from the caller's shell.

This uncovered one defect, now **fixed** here rather than deferred. `configureLoader` ended by adding
`tests/samples/stdlib` and `.` to every compilation's search paths — a directory from this repository's
test tree, compiled into the shipped binary and resolved against the working directory. `finc` now
resolves `<exe dir>/../lib/std` instead, which is one rule for both the release layout (`bin/finc` beside
`lib/std`) and the build tree (`build/finc` beside the source `lib/std`), and an invocation that names
library paths gets exactly those — no bundled stdlib and no working directory.

The reason this moved out of wave 5 is that the sentence here that deferred it was wrong. It said *the
harness currently depends on it*. It does not: the harness runs `finc` from `build/tests`, where that
relative path does not exist. Measured rather than argued — running all 50 samples from the repository
root and again from `build/tests` gives byte-identical exit codes, so the hardcoded path never affected
any sample, and removing it could not change a test outcome. The claim had made a ten-line change look
like packaging work.

The working-directory half was going to be left alone as a language question until the `finn` track
reported it from the other side: a pinned build that still searches the project root is not hermetic, and
no flag `finn` can send removes a path the compiler adds unconditionally. That makes it this function's
defect, not a matter of taste, so naming library paths now displaces the working directory too. A bare
`finc foo.fin` still searches it, because nothing there promised hermeticity.

**One missing module produced three errors, against an invariant the code itself states.**
`ModuleLoader.hpp:23` says the loader reports through the caller's engine "so one bad import produces
one diagnostic in one format". It produced three, and `--diagnostics=json` summarised them as
`"errors":3` — the number `finn` would show a user who has one typo. The cause is that only successes
were remembered: `loadModule` resolved, and on failure reported and returned `nullptr` *before* the
`moduleCache` lookup on the next line, so nothing recorded that an import had already failed. Both the
macro expander (`ExpanderDecls.cpp:21`) and the analyzer ask for every import, and each call
re-resolved and re-reported. The expander returns silently on `nullptr` — `if (!moduleScope) return;` —
precisely because it trusts the loader to have reported, so the loader could not simply go quiet
instead. **Fixed** by remembering failures the way successes were already remembered.

The dedup key is `(module, search kind)` rather than the module name, and that is deliberate: a file
import tries the importing file's own directory first and a package import does not (`resolvePath`
CASE B and CASE A), so the same name imported both ways is two genuinely different searches and the two
help strings differ. Keying on the name alone is the obvious simplification and it suppresses the
second, claiming the one search that ran covered both. Per *import site*, the analyzer's located error
still fires once each, so no `import` line loses the diagnostic that points at it — one message
explaining the search, one marker per place the user has to go and fix. Both halves are pinned, and
both were verified by mutation: dropping the search kind from the key kills
`TheTwoSearchKindsAreExplainedSeparately`, and deduplicating the located error kills
`EveryImportSiteKeepsItsOwnLocatedError`. Neither test had ever been seen to fail before that, which
is the only reason to run the mutation — a test written after its fix is unproven.

**A search path that does not exist was discarded without a word**, which made a misspelled
`--fin-libs` or `FIN_LIBS` indistinguishable from a module that is genuinely not installed:
`addSearchPath` (`ModuleLoader.cpp:28`) kept a directory only if it already existed, so a typo left no
trace anywhere in the output. The failure now names every place it looked, in the order it looked,
marking the ones that are not there — `searched: /tmp/x, /nonexistent/typo (does not exist)`.

A warning where the bad path is accepted would have been the wrong instrument, and the contract is what
says so: ADR 0009 has exit `0` imply zero diagnostics, so warning about a bogus entry on a compile that
then succeeded would break it. Putting the information in the failure's `help` costs nothing on success
and puts the typo next to the error it caused. Requested paths are recorded separately from searched
ones, so the message can name a directory the search itself skipped; empty entries are still dropped
before either, because an empty entry resolves against the working directory.

**A third diagnostic shape exists and needs a ruling**, pinned by
`KnownDefect_ModuleDiagnostics.ALocationlessErrorStillNamesAFile` rather than guessed at. ADR 0009:86
documents two: located (`file` set, `line` >= 1) and argv (`file` null, `line` 0, "about the invocation,
not about a place in a file"). The loader's is neither — `file` set and `line` 0, a diagnostic that
claims a file and names no place in it, which any consumer formatting what it is given renders as
`foo.fin:0:0`. `reportError(const std::string&)` (`DiagnosticEngine.cpp:333`) sets `d.file = filename`
and leaves `d.line` at its default, and the loader has no location to give it: `loadModule` takes the
import as a string while the `ImportModule` node carrying the location stays with the caller. Either
the shape is legal and ADR 0009 gains a sentence, or it is not and the location threads into
`loadModule` — which touches `src/macros/`, outside this wave. One line of ADR either way, and the
test inverts to whichever it is.

A three-deep chain sharpens what the ruling has to cover, and makes the shape worse than "no location".
`semantic errors in module: /tmp/chain/b.fin` renders as `--> /tmp/chain/main.fin`: the message is about
`b.fin` and the location names `main.fin`. That is not a diagnostic missing a location, it is one
carrying a *wrong* one, because `reportError` takes `d.file` from the engine's own `filename` — which is
whatever file the driver opened, never the module the message is about. A consumer that groups
diagnostics by file files this under the wrong file, and a user reading `--> main.fin` looks in the wrong
place. So the ruling is not only "is `file` set with `line` 0 legal" but "may `file` disagree with the
message", and the honest third option is that this diagnostic should carry `b.fin` with no line at all.
The pinned test asserts today's `file` non-null and `line` 0 and is unaffected by which way that goes.

**Every diagnostic inside an imported module pointed at a line that does not exist, and printed a blank
snippet under it.** This is the largest defect found in this section, and no sample could have caught it,
because a sample is one file. `lib.fin` with an undefined call on line 2, compiled directly, reports
`lib.fin:2:26` and prints the line. Reached through `import "lib";` from a two-line `main.fin`, it
reported `./lib.fin:4:26` with nothing under the caret — and `6:26` the next time it was parsed.

The arithmetic is the diagnosis: two lines of `main.fin` plus two. The lexer's location is a file-scope
`loc` (`lexer.l:13`) that does not reset between buffers, so every module was numbered from wherever the
previous parse stopped, and the renderer then had no source to print because the line was past the end of
the file it was blaming. The column was correct throughout, which is what pointed at a line counter
rather than at location tracking in general. `fin::reset_lexer_location()` already existed
(`lexer.l:34`), was already declared (`lexer.hpp:14`), and was already called for the file named on the
command line (`Driver.cpp:203`) — `ModuleLoader` simply never called it before `yy_scan_string`. **Fixed**
by calling it, which is safe there because a parse is never interrupted by another parse: a module is
parsed at step 5 and only expanded, which is what recurses, at step 6.

**A module that failed was re-parsed by every pass that asked for it**, which repeated all of its
diagnostics and advanced the line counter again, so the repeats landed on a *different* wrong line.
`moduleCache` stores successes only. **Fixed** the same way as the unresolvable-import case, and the two
were then confirmed to be separate defects rather than one: a build with the caching but not the reset
still reports `lib.fin:4:26` while already reporting the module's own error exactly once. So
`AnImportedModulesErrorPointsAtItsOwnLine` binds to the reset and
`AFailedModuleIsAnalysedOnceNotOncePerPass` binds to the caching, and neither is a second test for the
other's fix.

A two-file import cycle is what surfaced all of this, and it produced **21 error diagnostics**. It now
produces six, and they trace the cycle instead of burying it: `circular dependency detected: a.fin`,
then `Failed to load module 'a'` at `b.fin:1:1`, then `'b'` at `a.fin:1:1`, then `'a'` at `main.fin:1:1`
— each at a real location in a file the user wrote. The cycle message itself is now stated once
(`reportedCycles`), and the inner attempt deliberately does *not* mark the module failed: that attempt
sees a module whose outer load is still in progress, and the outer load is the one entitled to decide how
it ends. What is still missing is the cycle as a cycle — the message names one file, not `a -> b -> a` —
which needs the loading stack rendered as a path and is left for whoever finds it worth a line.

Two properties of the multi-file case that were checked and hold, since they are the ones the contract
turns on: a cycle terminates rather than recursing, and it exits `1` with stdout empty.

Worth noting what this does *not* fix, since the remaining message is weak exactly where it will be
read most: a bare `finc foo.fin` with no `lib/std` reports `searched: .`, which is accurate and
uninformative — the real answer is "this build ships no standard library". The driver knows that and the
loader does not. It is left alone because the case cannot reach a user: `release.yml:100` refuses to
build an archive whose `lib/std` is missing or holds no `.fin`, so a published `finc` always has one,
and the only builds that hit this are ours, where CMake already warns at configure time.

Dropping the working directory was necessary and is not sufficient. `Driver.cpp:83-93` removes `.`
from the search list as soon as any library path is named, and says why: "leaving it in would let a file
in the project shadow a module the build pinned". The code does exactly what the comment claims. But
`ModuleLoader::resolvePath` CASE B step 1 resolves a quoted import against `rootBasePath` — the
*importing file's* directory — before it consults any search path, unconditionally. In a real project
every source file has the project's other files as siblings, so the hole closed against the working
directory stays open against the directory the source itself lives in.

Measured, with the winner made visible to the type checker rather than guessed at: one pinned
`mylib.fin` whose `libfn` returns `int`, one same-named sibling of the source whose `libfn` returns
`float`, and `let x <int> = libfn();`. With `--fin-libs` naming the pinned directory, `import { libfn }
from mylib;` exits `0` and `import { libfn } from "mylib";` reports `Type mismatch: expected 'int', got
'float'`. Same module name, same directory, same flags, two spellings, two different files — and the
package form is the one that honours the pin, because CASE A consults only `searchPaths` and never
`rootBasePath`. Two controls keep that from being an artefact: with the sibling removed the quoted form
reaches the pinned library and exits `0` (so this is precedence, not reachability), and with the pinned
library itself returning `float` the type error appears (so the check bites).

The shadowing is silent — the build succeeds against the wrong module. A warning is the obvious answer
and the contract forbids it, for the third time now: ADR 0009 has exit `0` imply zero diagnostics, and a
shadowed import compiles fine.

The fix is a language decision rather than a loader detail, so it is pinned and left to the owner:
`KnownDefect_PinnedLibraries.AQuotedImportIsShadowedByASiblingFile` asserts today's behaviour, and
`Soundness_PinnedLibraries.APackageImportIsNotShadowedByASiblingFile` locks the half that is already
right. **Ruling needed:** does a quoted import mean one thing or two? The corpus documents both meanings
for the one syntax — `importing.fin` calls `import "tests/samples/macros.fin"` "a normal file import",
and says of `import "somelib"` that it is given no path so the compiler must find it through
`FIN_LIBS`/`--fin-libs`. A rule keyed on whether the string looks like a path (a separator or a `.fin`
suffix ⇒ resolve beside the importing file and never through search paths; a bare name ⇒ the reverse)
reads straight off those two lines and closes the hole exactly. It changes how every import in every
project resolves, which is why it is not being taken unilaterally.

`importing.fin` is the sample this came out of, and it is a **partially masked annotation** — the fourth
found. Its reason, "`module not found: stdio` … the standard library is on no search path", is accurate:
that is the first error, verified. What it hides is how much sits behind it. Put a stdlib on the search
path and four further blockers appear, in three different lanes: `stdlib/stdio.fin` fails to parse
(`syntax error, unexpected KW_IMPLEMENTS`) and `stdlib/hashmap.fin` fails to parse (`syntax error,
unexpected KW_CAST, expecting LT`) — both wave 2, and both new to that lane since `networking.fin` and
`somelib.fin` already parse clean; module-namespace member access is unimplemented (`Type
'module<mylib>' does not have methods`), which is wave 3 and is what the corpus spells as
`macros.magic_add!(...)` and `networking.invoke()`; and the quoted-import precedence question above.
The annotation is doing its job by naming the current blocker, but it reads like "put the stdlib on the
path and this passes", and that is false. Nothing here argues for editing the reason — it argues for not
treating `lib/std` content as the last thing between this sample and green.

### The bytes of a source file are bytes the compiler writes to a terminal

Four defects in the contract lane came out of one question — what does `finc` do with a byte it did
not expect — and they share a shape: a `const char*` where a length was needed, or a source byte
treated as inert. All four are fixed, each behind a test that was seen red first.

**Source after a NUL byte was silently discarded.** `yy_scan_string(s)` is defined as
`yy_scan_bytes(s, strlen(s))` (`build/lexer.cpp:2465`), so handing the lexer `source.c_str()` ended
the translation unit at the first embedded NUL — while the `std::string` read from disk held the
whole file. A file whose second half could not compile reported `Build Successful.` and exit `0`.
This is the worst class of defect available to a compiler: it accepted a prefix and claimed the
whole. There were two independent call sites, `Driver.cpp:204` and `ModuleLoader.cpp:211`, so
fixing one would have left an imported module truncated — hence two tests, not one
(`SourceAfterANulByteIsNotDiscarded`, `…InAnImportedModuleIsNotDiscarded`). Both now use
`yy_scan_bytes(source.data(), source.size())`, and a NUL reaches the lexer's catch-all like any
other unknown byte: located, with a snippet, and the rest of the file still scanned.

**The message that rejected a byte could not name it.** `reportLexerError` built a `std::string`
from a `const char*`, so the one byte whose name mattered most read as `unrecognised byte in
source: ''`. The catch-all matches exactly one byte, so a non-null `text` that reads empty held a
NUL — which is now inferred and named. Locked by `TheUnrecognisedByteMessageNamesTheByteItRejected`,
whose ESC case is the control that makes the NUL case a defect rather than a missing feature.

**A source file could rewrite the compiler's own output.** A rendered snippet *is* the source line,
so every byte of the source was a byte written to a terminal, and a control byte is not inert there.
An interior CR returns the cursor to column 0, so text the source placed after it overwrites the
diagnostic just printed — a `.fin` file could choose what its own diagnostics appear to say. An ESC
begins a sequence the terminal executes; measured with `NO_COLOR=1`, so the compiler emitted none of
its own, two ESC bytes from the source reached stderr. `splitLines` now replaces every byte below
`0x20` (except tab) and `0x7F` with `?`. Doing it there rather than at each print site also covers
`help` text, which quotes words lifted out of those same lines. Two tests, because a fix aimed at CR
alone would leave ESC: `AnInteriorCarriageReturnCannotOverwriteARenderedDiagnostic`,
`AnEscapeByteFromSourceCannotReachTheTerminal`. Note that the existing CRLF test did not cover this:
`splitLines` strips a CR at end of line, and the interior CR is the dangerous one that survives it.

The replacement is one byte wide on purpose. `printContext` writes the caret as `column - 1` spaces,
so the caret does not move with the line's content — which makes the risk the opposite one: render a
control byte as more or fewer than one byte and the line shifts out from under a caret that stayed
put. Rendering the byte properly *and* keeping the caret on it means teaching the caret about display
width, which tabs already need (a tab today is emitted raw and the caret is already misaligned for
it) and which neither has. **That is the follow-up task**: display-width-aware caret placement, which
would let control bytes be escaped as `\xNN` and fix tabs in the same change.

**A first attempt at the caret test was vacuous, and mutation is what said so.** It asserted that the
caret's reported column equalled the column the compiler reported — which can never fail, because
that column is where the caret came from. A mutant that deleted control bytes outright passed it. The
replacement, `TheCaretStillPointsAtTheCharacterItIsAbout`, reads the character out from under the
caret in the rendered line and puts the control byte *before* the error, and it kills both a mutant
that drops the byte and one that escapes it to `\xNN`. This is the clearest instance yet of why the
"never seen red is unproven" rule has to cover tests that guard rather than convict.

### The summary contradicted the stream it summarised

`ModuleLoader.cpp:196` gives each module its own `DiagnosticEngine`, correctly, so that a module's
diagnostics can point into the module's own source — that is what keeps every file's own lines and
snippets at depth. But that engine counted errors into itself and was then destroyed, while
everything it printed had already gone to the shared stream. The trailing JSON summary therefore
undercounted by exactly the number of diagnostics the modules reported: measured at 0, 1 and 2 broken
modules the gap was 0, 1, 2. A second program reading the diagnostic objects and a second program
reading the summary would disagree about how many errors the build had — precisely the failure ADR
0009 exists to prevent. Fixed with `DiagnosticEngine::absorbCountsOf` and a scope guard in
`loadModule`, a guard rather than a call at the end because the steps below it return early on four
separate failures. `TheSummaryCountsDiagnosticsThatModulesReported` sweeps all three shapes, so no
constant can pass it.

### Two invariants swept and found sound, now locked

`--fin-libs=` names an empty library set rather than leaving the set unspecified: it overrides a
`FIN_LIBS` that is set in the environment, and it drops `.` the way any named library set does. That
is what lets `finn` force a hermetic build from a developer's shell, so it is worth a test rather
than an assumption (`AnExplicitlyEmptyLibraryFlagOverridesTheEnvironment`). The exit-code taxonomy
also holds across the whole argv surface probed — missing file, a directory, unknown flag, repeated
and valueless flags, two input files, flags after the file — and the JSON summary's `exitCode` field
agreed with the real process exit status in every case, including the `2` paths.

**One portability defect was fixed on reasoning rather than on a red test, and is flagged as such.**
`Driver::readFile` opened with `std::ios::binary`; `ModuleLoader::readFile` did not. On Linux these
are identical, so no test here can convict it — but on the Windows CI target the module path would
translate CRLF to LF while the command-line path would not, making the two disagree about columns on
exactly the CRLF sources the encoding tests cover. It is now binary in both places. This is the one
change in this batch not backed by a test that was seen red, because on this platform no such test
can exist; CI on Windows is the only thing that could confirm it, and until that runs the fix is
argued, not demonstrated.

### A module that could not be read was reported as a module with no exports

Reading that function turned up a second defect in it, and this one is observable here. `readFile`
returned `""` for a file it could not open — the same value it returns for a file that is genuinely
empty. The two then went down the same path, so a module the compiler was not permitted to read was
reported as:

```
error: Module 'm' does not export 'exported'
```

which is what an empty module correctly reports. Every word of it is misleading. The reader is sent
to audit an export list in a file the compiler never opened, and the one fact they need — that the
open failed — is the one fact withheld. The driver already distinguishes these for the file named on
the command line (`could not read file:`, via `std::optional`), which is what makes this a defect
rather than a missing feature: the compiler knows how to say this, and says it one level up.

`readFile` now returns `std::optional<std::string>`, and the call site reports `could not read
module: <path>` with a help line naming permissions as the likely cause. The bookkeeping copies the
parse-failure branch directly below it — recorded in `failedPaths` so the second pass does not repeat
the diagnostic, and lifted off `loadingStack`, which it was pushed onto two steps earlier.

`Soundness_ModuleDiagnostics.AnUnreadableModuleIsNotReportedAsAMissingExport` asserts the two cases
do not produce the same diagnostic, rather than asserting one exact string: it requires the empty
module to still say `does not export` and the unreadable one not to. Both halves are proven. The
convicting half was seen red. The guard half was mutation-tested, because the obvious over-broad fix
is to treat an empty read as a failed one — a mutant doing exactly that makes the empty case say
`could not read module` and kills the guard. The test skips rather than passes where permissions
cannot stop the user, since a run as root would satisfy it vacuously.

The new diagnostic carries a file with no line or column, the shape `failed to parse module` already
uses. It therefore falls under the open owner ruling on whether that is a legal third diagnostic
shape, and does not widen it.

### One module reached by two names was two modules

`resolvePath` builds its result by concatenating the import text onto a base directory and
returned it unnormalised, and `moduleCache`, `failedPaths` and `loadingStack` were all keyed
on that text. So `import "m"` produced `./m.fin` and `import "./m"` produced `././m.fin` —
two keys for one file. The module was parsed twice, every diagnostic in it was reported
twice, and it arrived under two different `file` values, which splits one file into two for
anything that groups diagnostics by file. `finn check` is exactly such a consumer. Measured
spellings and what each resolved to: `m` → `./m.fin`, `./m` → `././m.fin`, `sub/../m` →
`./sub/../m.fin`, `.//m` → `././/m.fin`, `./././m` → `././././m.fin`.

The cache itself was never wrong: a diamond — two modules importing a third by the same
spelling — reported the third module's error exactly once. Only the key was wrong.

The fix separates the two jobs the path was doing. `resolvePath` now returns a lexically
normalised path, which is the spelling shown to a reader; all five spellings above now
display as `m.fin`. Identity is answered separately by `identityOf`, which is
`fs::canonical` — absolute and symlink-resolved — with a fallback to the lexically normal
path when the file cannot be interrogated. Every cache is keyed on that and never on the
display text. `fs::canonical` is deliberately not used for display: substituting an
absolute path into every diagnostic would be a regression of its own.

Two tests, and the split is the point. `Soundness_ModuleIdentity.OneModuleImportedUnderTwoSpellingsIsStillOneModule` and `.AModuleReachedThroughASymlinkIsNotASecondModule` are
separate because lexical normalisation alone fixes the first and leaves the second green —
a symlink is a second name for one file and no amount of lexical work notices. Both assert
against the compiler's own single-import behaviour rather than a fixed count: importing a
module a second time under another name must not change what the compiler says about it.
Both were seen red at 2 versus 1.

An unrelated observation while reading that function, recorded and not acted on:
`rootBasePath` is the directory of the file named on the command line and is fixed for the
whole run, so a module's own imports resolve relative to the root file rather than relative
to the importing module. That is a design question for whoever owns nested module layouts,
not a defect with a test yet.

### 99 diagnostics point at this suite's own expectation comments

A type-resolution failure in `src/semantics` carries no source location, so it renders at
1:1 — and line 1 of every sample is its `//@` label. `Undefined type 'Any'` in
`stdlib/operators.fin` is reported at `1:1` with an empty span while `Any` is used on line
6. An editor consuming this points the user at a comment.

Corpus-wide census, measured through the same invocation the corpus runner uses: **99
misattributed diagnostics across 15 of 50 samples** (95 across 13 with the stdlib
resolvable). An earlier figure of 430 was wrong and is corrected here: it counted
diagnostics attributed to *imported modules* against the *sample's* line numbering, which
invents misattributions wherever a module reports anything near its own line 1. The
detector that produced the 99 filters on `file` matching the sample under test, which is
the check the inflated count was missing.

The cause is in semantics, which is a later wave's lane, so this is booked as
`KnownDefect_DiagnosticAttribution.DiagnosticsAreAttributedToExpectationComments` — it
passes today by asserting the defect exists, and the day it fails is the day the fix lands.
It carries its own anti-vacuity guard: it asserts that *not every* sample is dirty, because
a broken line lookup would report every sample and satisfy the main assertion for the wrong
reason. Inverting it is mechanical — assert the census is empty, rename to `Soundness_`,
change nothing in the detector.

### Nine samples killed the compiler, and the suite was green

Wave 2's closing report said five samples "exit 139 after a failed module load — a segfault,
not a diagnostic. Five samples, one shape." Both halves were wrong in the same direction:
measured, **nine** samples segfaulted with the standard library resolvable (five without it,
which is how the corpus runner invokes `finc`), and there was not one shape but four faulting
sites. Symbolising the crash rather than believing the summary is what produced the fix.

The fault addresses came from an `LD_PRELOAD` handler calling `backtrace_symbols_fd`, then
`addr2line` against the already-`-g` binary — `gdb` is not installed on this machine, and the
trace is what turned "a segfault after a failed module load" into a one-line cause.

**One root cause, four doors.** `resolveTypeFromAST` (`Analyzer_Core.cpp`) reports
`Undefined type 'X'` and returns `nullptr` when a name does not resolve — correct, and every
caller checks it. But each *composite* branch wrapped whatever the recursive call returned
without looking: `PointerType(inner)`, `ArrayType(inner, fixed)`,
`FunctionType(pTypes, rType)`, `PrototypeType(keyType, valueType)` and the generic argument
list. A failed child therefore came back inside a **non-null** composite, every caller's
`if (!type) return;` passed, and the first `toString()` dereferenced the null child.
`PointerType.cpp:6`, `FunctionType.cpp:9` and `PrototypeType.hpp:17` are where it landed;
none of them is where it was caused.

The fourth door is the same failure without a composite: `Analyzer_Decl.cpp` stored an
unresolved return type in `context.currentFuncReturnType` and dereferenced it to ask whether
it was `void`. Six of the nine died there. `Analyzer_Stmt.cpp:15,21` already guard that exact
pointer, so "null means unknown, stop asking" was the codebase's own convention and this was
the one place that forgot it. A fifth site, `Analyzer_Expr.cpp:538`, repeats the composite
mistake for a lambda's return type independently of `resolveTypeFromAST`, so fixing the
branches left `lambdas.fin` crashing until it was fixed too.

Fixed by propagating the failure from every branch, resolving all children first so that a
type naming two undefined types reports both, and guarding the two derefs. All fifty samples
now exit within the contract in both invocations. The corpus suite also went from 5302 ms to
536 ms, because each crash was spending about 1.2 s writing a core dump.

**The harness accepted it, which is the more serious defect.** 262 tests were green while
nine samples segfaulted. `runFinc` shells out through `std::system`, so a child killed by a
signal arrives as the shell's `128 + signal` — 139 — and `WIFEXITED` is true of the *shell*.
The only check on a failing sample's status was `EXPECT_NE(exitCode, 0)`, which 139 satisfies,
so a crash was indistinguishable from an orderly rejection and `//@ unimplemented` ratified it
in silence. Worse, five of those samples *documented the segfault in their own expectation
text* — the harness read the label, never the claim.

Closed in two places. `test_expectations.cpp` now checks, before consulting the expectation at
all, that the exit code is one of the four the machine contract defines (ADR 0009) — no `//@`
line can license a fifth. And `Soundness_MachineContract.NoSampleTerminatesTheCompilerBySignal`
sweeps the corpus in **both** invocations, because the per-sample runner never sets `FIN_LIBS`
and four of the nine crashed only when the standard library was resolvable, which is the
configuration a user compiles in. A contract that holds in the harness's own invocation and
nowhere else is not a contract.

The five stale `//@ unimplemented` reasons were rewritten to say what the compiler does now,
measured rather than assumed. None of them claims a segfault any more.

**Mutation-proved, five mutants, each killing only its own tests.** Reverting the composite
guards killed the six composite tests and left the function-return and lambda tests green;
reverting `Analyzer_Expr.cpp:538` killed only the lambda test; reverting the
`currentFuncReturnType` guard killed only the function-return test; dropping the generic-param
registration killed only the generic-lambda tests; registering an unresolved signature killed
only the arity test. The first mutant also turned the per-sample corpus tests red, which is
the proof that the harness hole is actually closed rather than merely written about. Every
source file was restored and checked by `sha1sum -c`.

### A generic lambda's type parameters were undefined in the signature that declares them

Found by probing what remained of `lambdas.fin` once it stopped crashing. `visit(LambdaExpression&)`
resolved the return type and the parameter types *before* `enterScope()`, and nothing registered
the lambda's `<T>` at all — so `<T>(m: T) <T> => m` reported `Undefined type 'T'` twice and
`Undefined variable 'm'`, the parameter being untyped. `visit(FunctionDeclaration&)` had had this
right all along; the lambda visitor simply never copied it.

The same defect a second time in the *type annotation*: `resolveTypeFromAST`'s `FunctionTypeNode`
branch ignored the node's own `generic_params`, so `fn<T: Castable>(m: T) -> T` also reported T
undefined. `lambdas.fin:69` writes both halves on one line — the annotation and the value each
declare `<T>` — which is why fixing the lambda alone left the sample reporting the same message.
Both fixed; both spellings on that line now compile, and `lambdas.fin` is down from a segfault to
two diagnostics, both about the search path.

`generic_params` already existed on `LambdaExpression` and on `FunctionTypeNode` — wave 2 put them
there and nothing consumed them. Worth noting as a pattern: a field added by one wave and read by
none is invisible to every test that does not name it.

Two things found alongside and booked rather than built, each with a test that passes by asserting
the defect:

- `KnownDefect_GenericLambdas.CallingOneDoesNotBindItsTypeParameters` — `c(1)` on a generic lambda
  compares the argument against the uninstantiated `T` instead of binding T to int. Named types get
  this right through `StructType::instantiate`; a lambda's `FunctionType` has no equivalent path.
- `KnownDefect_TypeResolution.ACallToAFunctionWithAnUnresolvedSignatureCascades` — a function whose
  signature did not resolve is not registered, so calling it adds `Undefined function or type 'f'`
  to the one real error. `f` is defined; only its type is unknown. The previous behaviour was worse
  and is now a `Soundness_` test: unresolved parameters were dropped from `paramTypes` and the
  function registered anyway, so `fun f(p: NoSuchType)` called as `f(1)` was told it "expects 0
  arguments, got 1" — a false claim about a signature the program does not contain, and the kind of
  diagnostic that sends someone to edit the call site instead of the type name.

Both cascades have one proper fix, and it is the same fix: a **sentinel error type** returned by
resolution instead of `nullptr`, assignable to and from everything, suppressing further complaint
about any expression it reaches. That would let a function be registered with its correct arity,
silence the call sites, and retire the hand-rolled "null means unknown" convention that
`Analyzer_Stmt.cpp:15,21` and `Analyzer_Decl.cpp` step 7 each implement separately. It touches every
`Type` subclass, so it is wave 3 work and not a detour inside a crash fix.

One parser note for whoever owns it: a non-generic `fn` type cannot name its parameters —
`fn(m: int) -> int` is a syntax error while `fn<T>(m: T) -> T` parses. `param_names` on
`FunctionTypeNode` documents the generic form as the only one in the corpus that names them, so this
may be deliberate; it is recorded here rather than changed.

**Lane note, stated plainly.** The ownership map gives `src/semantics/**` and `src/types/**` to agent C,
and every fix in these two sections is in those files. It was taken by the harness/contract owner, not by
C, because the crash made wave 3 unmeasurable: eighteen per cent of the spec corpus died before the
analyzer produced a diagnostic, so any semantics census taken first would have been a census of a
partially-run analyzer. No analyzer agent was running at the time and nothing was in flight to conflict
with. The next agent to hold that lane should read these two sections before touching
`resolveTypeFromAST` — the branch-by-branch nullptr propagation and the two `currentFuncReturnType`
guards are load-bearing for eleven `Soundness_` tests, and the error-type sentinel above is the change
that would replace them wholesale.

### Every integer type except `int` rejected a decimal literal

`let x <long> = 1;` did not compile. Nor did the same line with `short`, `char`, `uint`, `ulong`,
`ushort`, `float` or `double` — nor a constant as a struct field default, a `const` initialiser, a call
argument, a `return` value, an assignment, a compound assignment, a `for` counter, either side of a
comparison, or a ternary branch. Ten positions, seven types, one cause, and it had been sitting under
every `Type mismatch` census taken so far without being named.

The cause is two lines that are individually defensible. `visit(Literal&)` gives an `INTEGER` the type
`int` (`Analyzer_Expr.cpp:67`), because it has to give it something. `PrimitiveType::isAssignableTo`
admits exactly one conversion, `int` → `float`. Between them, the only integer type a written constant
can ever have is `int`, and `let p <ulong> = 0;` — `stdlib/stdio.fin:97`, in a normative sample — is
unwritable.

**The fix is a rule about constants, not a widening of the type lattice.** `constantFitsType(node, target)`
asks whether an integer constant *written as this expression* may take a target type, and it answers
from the syntax alone: a `Literal` with kind `INTEGER`, or a `UnaryOp` over one, since Fin spells `-1`
as a unary minus and the lexer never produces a signed `INTEGER` token (`parser.y:2139`). It lives in
`SemanticAnalyzer` and not in `PrimitiveType` because the answer depends on the expression and not only
on the two types: `1` and `i` both have type `int`, and only one of them may become a `uint`.

Widening `isAssignableTo` to admit `int` → unsigned was the smaller diff and it was rejected on purpose,
because it would have answered a live language question by accident — *does an `int`-typed expression
convert to an unsigned type, C-style, or require a cast, Rust- and Zig-style?* That question is now
pinned open by `KnownDefect_IntegerConstants.AnIntTypedExpressionIsNotUnsigned`, which names the five
`stdlib/stdio.fin` sites that will change the day it is answered. `1 + 1` is deliberately not a constant
here for the same reason: folding arithmetic would answer the question for the foldable subset and leave
the rest inconsistent.

Two places needed more than a call to the new predicate, because neither has an "expected" side.
`visit(BinaryOp&)` treated its left operand as the expectation, so `blame 0 == a` for a `uint` `a`
reported *the variable* as the error — "expected 'int', got 'uint'". A constant is now looked for on both
sides, and only a constant: whether two differently-typed variables may be compared at all is a separate
question, so when neither side is a constant that fits, the check runs exactly as before, at the same
operand, with the same message. `visit(TernaryOp&)` had the same shape plus a second symptom — it took
the true branch's type as the result, so `true : 1 ? a` produced a diagnostic *and* handed `int` to
whatever consumed the expression. When one branch is a constant the other branch's type is now the
result.

**Measured on the corpus, per file, before and after:** `arrays.fin` 4 → 3 `Type mismatch` diagnostics,
`complex.fin` 10 → 8, `importing.fin` 16 → 14, `stdlib/stdio.fin` 10 → 8. `expected 'uint', got 'int'`
1 → 0; `expected 'ulong', got 'int'` 9 → 3. Seven diagnostics removed, no other line of corpus output
moved, and no sample expectation went stale. (An earlier census of this cluster said "30 total" and was
wrong: its `grep` filtered type names through `'[a-z0-9_{}]*'`, which silently excluded every
capitalised, `&`-prefixed and `[`-prefixed type. Per-file counts are used above because they are exact.)

**Sign is checked; magnitude is not, and that hole is booked in the same change.**
`KnownDefect_IntegerWidths.AConstantTooLargeForItsTargetIsAccepted` records that `let x <char> = 300;`
compiles, because Fin has not said how wide `char` or `short` is and the `{N}` annotation that would say
is erased by `resolveTypeFromAST` before anything can read it. Rejecting every constant instead was
strictly worse — it is what made `let p <ulong> = 0;` unwritable in the first place. **Ruling needed** on
the widths of `int`, `long`, `short` and `char`; the range check goes where that test is and the test
inverts into `Soundness_IntegerConstants.AConstantMustFitItsTarget`.

**A normative sample contradicts one of the new soundness tests, and the conflict is recorded rather
than decided.** `Soundness_IntegerConstants.ANegativeConstantIsNotUnsigned` holds that `-1` is not a
`ulong`, which is what the compiler already did. But `stdlib/stdio.fin:107` writes
`fun read(nbytes: ulong = -1)` and `:110` compares `nbytes == -1` — a normative sample saying in its own
hand that a negative constant on an unsigned type is legal and wraps, C-style. Nothing depends on the
answer today, because the rejection predates this change and the sample's own expectation is prose. So
the rejection stands, both possible resolutions are written into the test's comment, and: **Ruling
needed** — is `-1` a legal unsigned constant, or must a maximum be spelled explicitly? **Ruling needed**
— is `char` signed? It is the one integer type this rule does not put in either camp, because deciding
would be inventing the answer.

**Twelve tests, written first; seven confirmed red for their stated reasons before the rule existed.**
The type loop; `0` split out from it because it is the corpus's actual case and a fix keyed on nonzero
would leave the loop green; the ten-position table; either-side comparison; float and double; the
negative rejection; and a guard that a constant is still not a `bool` or a `string`.

Mutation proofs, five run, each restored and verified against `sha1sum`:

- Negative constants allowed for unsigned types → killed `ANegativeConstantIsNotUnsigned` and also the
  pre-existing `KnownDefect_IntegerWidths.TheWidthIsAbsentFromDiagnosticText`, which needs
  `let a <uint{8}> = -1;` to keep being rejected. Not a leak: the negative path is load-bearing for an
  assertion written before it, which is why it kept emitting the identical message.
- Any primitive accepts a constant → killed only `AConstantIsStillNotABoolOrAString`, the guard that had
  never been seen red. It binds.
- `PrimitiveType::isAssignableTo` widened to `int` → unsigned → killed four tests, every one for a
  stated reason, and incidentally demonstrated that the C-style ruling and
  `ANegativeConstantIsNotUnsigned` cannot both hold. The tests detect which ruling is in force, which is
  what they are for.
- `ArrayType::isAssignableTo` forced true was planned as a fifth mutant and **not run**: the widening
  mutant above already killed the array test through the element-type path, so it would have proved
  nothing new. Recorded rather than dropped silently.

**A mutant that survived, and what it found.** Adding the missing `checkType` inside
`SemanticAnalyzer::visit(Parameter&)` changed no test result at all — because nothing ever calls that
visitor. `visit(FunctionDeclaration&)` iterates `node.params` by hand to resolve their types and define
them (`Analyzer_Decl.cpp:53-66`) and never calls `param->accept(*this)`, so `visit(Parameter&)` is dead
code for every function in the language and a parameter default is **not analysed at all** — an
undefined name or a call to an undefined function in a default is accepted in silence, while the same
expression one line lower in the body, or in a struct field default, is caught. The struct path both
walks and checks (`Analyzer_Decl.cpp:181`, `:183`), which is the evidence that this is a missing call
and not a missing capability.

Held as `KnownDefect_Declarations.AParameterDefaultIsNotAnalysedAtAll` and
`…IsNotCheckedAgainstItsType`, split because two mutants proved the split is real: adding
`param->accept(*this)` alone kills the first and leaves the second still asserting its defect; adding
the `checkType` as well kills both. Not fixed in this unit, because the moment it lands it convicts
`stdlib/stdio.fin:107` — the `-1` ruling above. Worth knowing for the harness: that sample's corpus test
stayed green under both mutants, because its expectation is prose (`//@ unimplemented "…"`) and prose
expectations do not notice a new diagnostic. These two tests are the only thing watching it.

**One claim in this workstream was wrong and is corrected here.** The array KnownDefect originally said
`expected '[uint]', got '[int; fixed]'` named two defects — the element type and fixed-versus-dynamic.
Measured: `let a <[int]> = [1,2];` compiles clean, so a fixed list already converts to a dynamic array of
the same element type. The `fixed` in the text is only how the right-hand side prints, the element type
is the whole defect, and the fact that made it single-cause has been promoted out of a comment into
`Soundness_Arrays.AFixedListInitialisesADynamicArrayOfTheSameElementType` — because a claim asserted only
in prose cannot notice when it stops being true.

Suite: 293 tests, all passing. Same lane note as the section above — this is `src/semantics/**` and
`src/types/**`, taken by the harness owner with no analyzer agent in flight.

### 430 diagnostics pointed at the comments describing them

`Undefined type 'X'` is the largest single error class in the corpus — 99 occurrences, more than twice
the next — and every one of them was reported at line 1, column 1. Line 1 of every sample is its `//@`
expectation comment. So the compiler was attributing its errors to the sentences that describe those
errors, and in a project where the samples *are* the specification, that is not a misplaced caret: it is
the evidence being overwritten by the thing it is evidence about. Counting every diagnostic the corpus
emits about the file being compiled whose line resolves to a `//@` comment: **430, across 21 of the 50
samples.**

The cause is two lines apart in two files. `SemanticAnalyzer::error(node, msg)` reports at `node.loc`,
and `resolveTypeFromAST` raises the undefined-type error at `Analyzer_Core.cpp:204` using the
`TypeNode`'s own location — which the parser never set. Of `base_type`'s fifteen productions in
`parser.y`, five called `setLoc(@$)` (the five wave 2 added) and eight did not, `IDENTIFIER` among them.
The remaining two, `fn_type` and `LPAREN type RPAREN`, are pass-throughs and are left alone deliberately:
the location that matters is the inner type's, because it is the type that failed to resolve and not the
parentheses around it. `parser.y` calls `setLoc` 280 times elsewhere, so this was an omission local to
types, not a convention the file declines to follow — which is also why `pointer_type` and `array_type`
were already correct, and why `[Nope]` misreported through its *element* rather than its brackets.

**430 → 5 → 0.** The five survivors are the part worth recording. After the eight `base_type` productions
were fixed, re-running the corpus census — not re-reading the grammar — surfaced two further causes:
`new IDENTIFIER { ... }` built its `TypeNode` in the `new` production itself (`type->setLoc(@2)`, the name
rather than the whole expression, because the name is what fails to resolve), and the two *top-level*
`FunctionDeclaration` productions at `parser.y:465` and `:470` never set a location while the in-struct
method forms at `:894` and `:1614` always had — so `Function 'add' is missing a return statement on some
paths` pointed at line 1 for a free function and at the right line for a method. Neither would have been
found by inspecting `base_type` again. That is the argument for keeping the census corpus-wide instead of
replacing it with per-production unit tests: the unit tests say a production is fixed, and only the census
says the *class* of defect is gone.

**A segfault found by reading a test's wall-clock.** The new-expression location test took 2412 ms for two
compiles. 1.2 s per compile is the core-dump signature from the earlier crash workstream, and `new Nope{}`
did exit **139**. Diagnosed without a debugger — a `SIGSEGV` handler `LD_PRELOAD`ed into `finc`, its
`backtrace` addresses fed to `addr2line` — to `PointerType::toString()` at `PointerType.cpp:6`, called from
`checkType` at `Analyzer_Core.cpp:276`, from `visit(VariableDeclaration&)`. `visit(NewExpression&)` wrapped
a failed type resolution in `std::make_shared<PointerType>(nullptr)`: the composite is non-null, so every
downstream `if (!type) return;` waves it through, and the first `toString()` — `checkType`'s own error
message — dereferences the null. This is the same shape fixed inside `resolveTypeFromAST` earlier, and the
sweep this time went the other way: every remaining construction of a `PointerType`, `ArrayType`,
`FunctionType` or `PrototypeType` in `src/semantics/**` and `src/types/**` was checked, the other six are
guarded, so `visit(NewExpression&)` was the last site rather than the next one.

The test for it asserts `EXPECT_EQ(exitCode, 1)`, not `EXPECT_NE(exitCode, 0)`, and that is the general
lesson rather than a detail of this crash: 139 is nonzero, so `NE 0` is not an assertion that the compiler
*ran*. ADR 0009 admits exactly `{0, 1, 2, 3}`. Every crash test in the suite now pins the code.

Tests first, in the usual order. Three `KnownDefect_DiagnosticLocation` cases plus a control that pinned an
*already-located* diagnostic — the control being the part that matters, because without it a green
"locations are wrong" test is equally consistent with a broken location extractor in the harness. The
fix then inverted them into `Soundness_DiagnosticLocation`, now seven tests: a 13-case table walking every
syntactic position a named type can occupy (let annotation, parameter, return type, generic base, generic
argument, struct field, array element, pointer target, prototype element, `const`, `fn`-type parameter,
alias target, cast target), the caret's exact span, `any`, `Self`, `new`, and the missing-return case with
its method control. The corpus-wide `KnownDefect_DiagnosticAttribution` became
`Soundness_DiagnosticAttribution.NoDiagnosticPointsAtAnExpectationComment`.

That inversion needed two guards it did not need before, because "the census is empty" is also what a
detector that has stopped working returns — the failure mode a `KnownDefect` asserting *non*-emptiness was
structurally immune to. First, the number of diagnostics that reached the `//@` test at all: 322 today,
floored at 100, and the floor falls as the compiler improves, so it is to be lowered deliberately and never
deleted. Second, that line 1 of all 50 samples is still a `//@` expectation — if that ever stops being
true the test stops being a net, and it should fail rather than pass quietly.

Three mutants, and the first one's kill *pattern* is the useful result:

- **`base_type: IDENTIFIER` loses `setLoc` again** (the original cause, put back). Kills the corpus census
  and exactly two of the seven location tests — the named-type table and the caret-span test. The other
  five pass, and each one passing names its own production: `new` sets its location in the `new` rule,
  `any` in `KW_ANY`, `Self` in `KW_SELF_TYPE`, missing-return in `FunctionDeclaration`, and the control
  depends on no fix at all. That matrix is the proof the split is real: one production regressing fails
  the tests bound to it and no others.
- **The detector pointed at a path that does not exist.** `considered` goes to 0 and the floor fails. This
  is the shape a silently broken detector has, and it now cannot pass.
- **One sample loses its line-1 expectation.** Fails by name, on that sample, without a rebuild.

Four sample files changed, and only their expectations: `undefined_behavior.fin`'s pinned `//@ error 1:1`
became `3:1`, and the `//@ unimplemented` prose in `nullifier.fin`, `implements_block.fin` and
`stdlib/operators.fin` was updated to name the real positions — `any` at 12:30, `Any` at 6:15, and
`Generic count mismatch` at 13:22 *and* 28:19, two distinct sites that were indistinguishable when both
rendered at 1:1. Under ADR 0008 sample *code* changes only through a ratified language decision; a pinned
position is not code, it is the harness's record of what the compiler does, and when the compiler starts
doing the right thing the record is what is stale. `stdlib/operators.fin`'s own prose had already named
6:15 as the true location, which is the clearest statement that this was the record catching up.

Suite: **301 tests, all passing**, corpus 50/50. The corpus suite also went from 5302 ms to 519 ms, which
is not an optimisation — it is the crash fixes, at roughly 1.2 s per core dump.

**Lane note, stated plainly, and one crossing more than the section above.** This is `src/semantics/**`
again, and also **`src/parser/parser.y`** — the Frontend agent's lane. Fifteen locations in it: eight
`base_type` productions, `type: ELLIPSIS`, four `new` productions and two `FunctionDeclaration`
productions. No frontend agent was in flight, the grammar still builds with **zero conflicts**, and the
change is additive (`setLoc` calls, 280 → 295, plus comments). It is recorded here because a lane crossing
that is only in the diff is a lane crossing nobody agreed to.

**Two things booked, not fixed.** `parser.y` carries the whole `new` production block *twice*, at roughly
line 2301 and line 2531 — both copies needed the same `setLoc`, and a future fix applied to one will be
silently absent from the other. And `undefined_behavior.fin:9` writes `fun? add2() <int>`, whose own comment
says it "compiles but compiler will be strict about its return type which can be either int or null"; the
compiler reports `missing a return statement on some paths` for it, at the right line now, exit 1. What `fun?`
means is a language question — whether the `?` makes the declared `<int>` nullable, and therefore whether
falling off the end is a legal `null` return — so it needs a ruling before it needs a fix. The sample
documents it in prose and no expectation pins it, which is exactly the state a ruling is owed for.

### An import of every symbol imported none of them

`tests/samples/importing.fin:11` writes `import * from somelib;` and its comment says "this will import
ALL symbols from the somelib library". It imported nothing, and then blamed the user: `visit(ImportModule&)`
looked `*` up in the module's scope as if it were an identifier, reported `Module 'm' does not export '*'`
(`Analyzer_Decl.cpp:475`), and every use of a symbol the import was supposed to bind produced a second
`Undefined function or type ...`. One unimplemented form, a cascade of diagnostics, and the loudest of them
pointing away from the cause.

The frontend half was already done and this is worth noting, because it is the second time in this wave
that the grammar was ahead of the analyzer: `parser.y:740` records the star as a `*` entry in the target
list, with a comment explaining why a `*` entry rather than a new flag (an empty target list already means
"bind the module name" for `import m;`, which is a different thing). Nothing in `src/parser/` needed to
change.

The rule implemented is **explicit beats wildcard** — a name is taken from the module only if the importing
scope does not already have one. Python, Java and C# all do this, but here it is not only convention. A
module's scope *is* an analyzer's global scope (`ModuleLoader.cpp:308`), so it carries the fourteen builtin
types every analyzer registers (`Analyzer_Core.cpp:79-94`) alongside the module's own declarations, and it
is read through its own `symbols`/`types` maps rather than through `resolve`, which walks parents. Without
the guard a library can rename a type out from under the file importing it; the builtins are the lesser half
and would be harmless anyway, since types compare by name and not by identity.

Six tests, of which three were red before the fix, one is a control, one is a must-not-break, and one was
written after the fix and is honest about it. The control (`ANamedImportBindsTheSymbolItNames`) exists
because a red `import *` test is equally consistent with the search path being wrong, the module not
parsing, or the helper writing files where the compiler never looks. The must-not-break
(`ImportStarFromAMissingModuleReportsTheModule`) is the case where the old message was almost right, and
the risk of the fix is that the module-load failure stops being reported at all once `*` no longer goes
through a symbol lookup. The value and type cases are separate tests because `Scope` keeps values in
`symbols` and types in `types`, and copying one map is a complete-looking fix that leaves the other broken.
The cascade is a third, because what a user reports is not "the feature is missing" but "the compiler says
my symbol is undefined".

Three mutants, and the matrix is the proof rather than the individual kills. Dropping the shadowing guard
fails exactly `ImportStarDoesNotShadowTheImportersOwnDeclaration` — which is what makes that
written-after-the-fix test trustworthy, since before the fix it passed for the wrong reason (`import *`
bound nothing, so it could not shadow anything). Copying symbols but not types fails exactly the type test;
copying types but not symbols fails exactly the value test and the cascade test. No mutant kills everything
and none survives, so the split is real in both directions.

**Two holes in the same function, booked with rulings rather than fixed.**

*The namespace on an import is discarded.* `module_path` in `parser.y:776` splits `m::ns` into the module
and the namespace tail and `ImportModule::namespace_path` holds it; `visit(ImportModule&)` never reads the
field. `from m::nonexistent` and `from m::a::b::c` compile clean, and so would a namespace no module ever
declared. This is not fixable as it stands, because `namespace std { ... }` currently has no effect on
anything — dropping the block from a module changes no import, which was measured, not assumed. **Ruling
needed:** what does a namespace do to a module's symbol table, and is naming one the module does not
declare an error or a no-op? Twelve stdlib samples open with `namespace std` and eleven corpus imports name
`::std`, so the answer is load-bearing for the standard library and cheaper to settle before it is written
than after.

*Visibility is not enforced on an import.* A symbol declared without `pub` imports fine, and the diagnostic
for a name the module really does not have says "does not export", which is a promise the code does not
keep: it means "does not declare". The AST carries the fact — `is_public` on every declaration node — and
`Symbol` (`Scope.hpp:12`) has nowhere to put it, so it is dropped when the module scope is built; `Scope::types`
is worse off, a bare name-to-type map with no visibility at all. **Ruling needed, and the corpus is why it
cannot be a straight fix:** `tests/samples/structs.fin:3` declares `struct Vector3` with no `pub`, and
`importing.fin:3` imports it with the comment "This is allowed and ONLY imports the symbol (Vector3)". So
either `pub` is required to export and that sample is wrong, or a quoted-path file import is exempt and only
library imports are gated, or `pub` is advisory. The first changes sample code and needs a ratified decision
under ADR 0008; the other two change the compiler. Note that `#[export]` is a third spelling and applies to
a whole `%{ ... }%` block — `stdlib/operators.fin:5-136` exports twenty-odd interfaces that way, which is
why `Index` and `IndexAssign` are importable without carrying `pub` individually, and why "is it `pub`?" is
the wrong question to answer alone.

Four things measured on the way through, recorded because each one would otherwise be rediscovered:
`X::std` and plain `X` resolve to the same file, so the namespace suffix costs nothing today and will cost
everything the day it means something; a named import **already** re-exports transitively (`import { inner_fn }
from m` works when `m` merely imported `inner_fn` itself), which is what makes copying the module's scope for
`import *` the consistent choice rather than a new invention, and is itself worth a ruling; the standard
library's search path is **not** the blocker people assume — `bundledLibraryPathsFor` in
`src/driver/SearchPaths.hpp:107` is complete and correct, and returns nothing only because there is no `lib/`
directory at all, so the ~54 module-resolution diagnostics in the corpus are blocked on the stdlib track's content
and not on `src/driver/**`; and whether an import carries macros could not be measured at all, because no
macro *declaration* syntax currently parses and `macro_definitions.fin:8` says of the form it uses "NOT
DECIDED YET".

No corpus expectation moved: `importing.fin` is still blocked on `somelib` not existing, so the fix is not
yet visible there. Suite: **309 tests, all passing**, corpus 50/50. Lane note: `src/semantics/**` again,
`src/utils/ModuleLoader.*` read but not changed, and no parser change this time.

One field in that JSON schema is owed to a wave 4 requirement and must be reserved now rather than
added by a contract bump later. A diagnostic can arise inside code a handler injected, which means it
points at a source location the user never wrote — the failure Rust's derive macros are notorious for.
So a diagnostic carries an optional attribution naming the handler responsible and the event point that
fired it, empty for every diagnostic the compiler raises on its own. `finn check` consumes this file
format, so adding the field after `finn` ships against the schema costs a coordinated release across
two repositories; reserving it costs one key.

**1c. Build and CI** (ADR 0010). Pin LLVM 18 via Conan on every platform; delete the Windows-only
`llvm-core/19.1.7` branch. Set `compiler.cppstd=gnu20` in the committed profile — the Conan default
is `gnu17` and the project needs 20. Fix `build.sh`, which calls `uvx` and cannot run as committed.
GitHub Actions matrix building and testing on Linux x86_64/arm64, macOS arm64/x86_64, Windows
x86_64/arm64. Add `*.fin text eol=lf` to `.gitattributes`. (`FIN_LIBS` was listed here and has moved to 1b: the fix is
in `src/driver/**`, which this agent does not own, and listing a task under an owner who cannot perform
it is how it came to be skipped by both agents in wave 1.)

Release archives are named `finc-<semver>-<rust-target-triple>` — `.tar.gz` everywhere, `.zip` on
Windows — so: `finc-0.4.0-x86_64-unknown-linux-gnu.tar.gz`, `-aarch64-unknown-linux-gnu`,
`-x86_64-unknown-linux-musl`, `-x86_64-apple-darwin`, `-aarch64-apple-darwin`,
`-x86_64-pc-windows-msvc.zip`. Rust target triples specifically, even though `finc` is C++, because
`finn` constructs the expected name from its own build-time target constant; any other convention
needs a hand-maintained mapping table, which is exactly how `download.rs:62-65` came to match on OS
alone and hand arm64 users an x86_64 build. Each archive unpacks to `bin/finc[.exe]` plus `lib/std/**`
with **no top-level version directory** — `finn` supplies the versioned parent. sha256 published
twice: a `<asset>.sha256` sidecar for humans, and the value inside the version index, with the index
authoritative. **The index is generated by the release job**, never hand-maintained, and is fetched by
`finn` at runtime rather than compiled into it — a compiled-in manifest would freeze the set of
installable compiler versions at each `finn` release, so a `finc` released on Tuesday would be
uninstallable until `finn` shipped again.

**Two defects in the index, found from the `finn` side and still unfixed.** Both are in
`.github/workflows/release.yml`, which 1c owns, and neither can bite until a release is published — so
they are recorded rather than fixed, and they need a ruling on one question: *are prereleases
installable?*

The tag pattern accepts them (`v[0-9]+.[0-9]+.[0-9]+*`) and `preflight` marks any hyphenated version a
prerelease (line 84), so today the answer is "published but not installable, silently":

1. `finn` fetches the index from `releases/latest/download/index.json` (line 16). GitHub's `latest`
   alias resolves to the newest release that is *not* a prerelease, so the moment `v0.5.0-rc.1` is
   published, that URL still serves the previous stable index — which does not list `0.5.0-rc.1`.
   `finn download 0.5.0-rc.1` then fails with "not in the index" while the archives sit there.
2. Worse, and separately: the index is built by chaining onto the previous one, and
   `gh release download --pattern index.json` (line 369) is called with **no tag**, so it also resolves
   through `latest` and skips prereleases. Publish `v1.1.0-rc.1` and then `v1.1.0`, and the stable
   release's index is chained from `v1.0.0` — the rc's entry is dropped from the accumulated history for
   good. The index is therefore not the complete record the comment above it claims.

Defect 2 should be fixed whichever way the ruling goes, because an index that silently loses entries is
wrong under either answer, and chaining is what makes it fragile: building the index from the repository's
full release list removes the failure mode instead of moving it. Defect 1 is the actual decision — either
publish the index somewhere version-independent that includes prereleases, or state in
`docs/finc-interface-contract.md` that prereleases are not installable and have `finn` say so by name
rather than reporting "not in the index".

**1d. AST infrastructure** (ADR 0004). `NodeKind` and a structural walk, replacing the 55-method
pure-virtual `Visitor` for traversal. Fix the `Attribute`/`GenericParam` clone bug at the root rather
than in `CloneVisitor`. This lands first among the `src/ast/` work because the Frontend agent adds
node types on top of it in wave 2.

**Exit criteria:** CI green on all six platform/arch combinations; `fin_tests` reports zero
*unexpected* results, with every known failure recorded as an explicit expectation rather than as
silence (the count was written here as 39 and is now 34, which is why it is not written here any more —
see the census note in 1a); `finc --version` parseable; the three-line `Build Successful.` reproducer exits non-zero.

## Wave 2 — make the corpus parse

Frontend agent, after 1d lands. This agent continues into wave 3 rather than handing off. This is the
largest single body of work and it unblocks every other track. Ordered by files-per-change, from
`docs/baseline.md`:

Eight changes clear 26 files: `::` in module paths (7 files) · `%{ ... }%` attribute blocks (4) ·
attribute-before-`import` (4) · `namespace` as a keyword (3) · `/* ... */` block comments (2, and
entirely absent from the lexer) · splitting `>>` back into two `>` in type context (2) · `@define`
parameters keeping their bracketed `name: <type>` form (2) · nullable `?` declarations and postfix
denullify (2).

**The payload-carrying enum member cannot land in this wave at all**, and that is a lane-ordering fact
rather than a difficulty. `Ok(T),` inside an enum body is `syntax error, unexpected LPAREN, expecting
RBRACE or COMMA` (`stdlib/typing.fin:15:7`), and the reason it cannot simply be parsed is that
`EnumDeclaration::values` is a `vector<pair<string, Expression>>`: there is nowhere to put the payload
type. Widening it is read by `src/semantics` (wave 3) and `src/codegen` (wave 5), neither of which has an
owner yet, so the change spans two lanes ahead of the one doing the work. The Frontend owner surfaced
this rather than reaching across, which is the right call and is recorded here so the next owner does not
rediscover it.

It matters more than one sample. `CONTEXT.md` names the generic `enum` as Fin's sum type — the thing a
constraint set deliberately is not — so this is a core language feature sitting behind a representation
change, not a corner of the grammar. The instruction stands: leave it unparsed. Parsing `Ok(T)` into a
representation that drops `T` would be worse than the syntax error, because the error is honest and the
silent drop would not be, and a dropped payload resurfaces as a wrong answer in wave 3 with nothing
pointing back here. Whoever owns the representation widening owns the parse, and the two land together.

One defect in the lexer's error recovery belongs here rather than in wave 1, because fixing it means
changing a rule and not just where its message goes. The catch-all rule reports **one diagnostic per
unrecognised byte**, so a single non-ASCII character produces two: pasting `§` into a source file yields
`'\xc2'` at 2:1 and `'\xa7'` at 2:2, the second pointing at a column inside a character that has no
column 2. Any accented letter, dash or quote a user's editor inserts costs them one diagnostic per byte,
and the count scales with the character rather than the mistake. The catch-all should consume a whole
UTF-8 sequence and report the character. Exit code and error count are already correct — this is about
what one diagnostic covers, which makes it a rule change and so the Frontend owner's.

**`true` and `false` are not literals**, which is the smallest change in this wave and probably the
one that unblocks the most. The lexer has no rule for either word — `"bool"` at `lexer.l:209` is the
*type* — so both fall through to the identifier rule and the analyzer reports `Undefined variable
'true'` in an initialiser, a condition and a return alike. Everything behind it is already built:
`ASTTokenKind::BOOL` exists at `src/ast/nodes/ASTNode.hpp:16` and `Analyzer_Expr.cpp:72` already types
that kind as `bool`. So the whole gap is two lexer rules and one production each, with no analyzer
change, and until it closes no Fin program can write a boolean down — including every `@special`
predicate in the standard library. Found while probing the `blame` defect, which is why it is not in
the census: no sample fails *only* on this, so counting files could not surface it.

**Done** — `KW_TRUE`/`KW_FALSE` at `lexer.l:159-160`, two `literal` productions at `parser.y:1735-1736`
building `Literal(text, ASTTokenKind::BOOL)`, and nothing downstream needed changing, as predicted. The
four tests that asserted the defect were inverted into `Soundness_BooleanLiterals.*` by the change that
fixed it, which is the response the convention asks for and the first time it was exercised end to end:
a `KnownDefect` went red, and the agent whose work turned it red inverted it rather than relaxing it.
They bind, and not merely by exit code — `let b <bool> = 1;` and `let b <int> = true;` are both rejected,
so `let b <bool> = true;` exiting 0 is evidence that `true` is typed `bool` and not merely that it
parses. The condition and return positions are weaker on their own and say so in the test: `return` is
type-checked, but a condition is not, which is the defect two sections up.

Then the thirteen singletons: bare `type X = T;` and array types on the right of `type` · `pub`/`priv`
inside an `X implements <...>` block · visibility labels (`pub:` / `priv:`) · `implements` in a generic
constraint (`<T: any implements Struct>`) · `$struct` and `$interface` alongside `$type` · `import *`
· the `for (i : int = 0; ...)` colon header form · `new &int` · `name!{ k => v }` macro arguments — and
note that the parser is not the whole of that one: **`MacroExpander` never substitutes arguments at all**,
so `twice!(3)` expands to a body still mentioning `a` and fails with `Undefined variable 'a'`. Agent A
found this while building the first tests `src/macros/**` has ever had. Giving the grammar the argument
syntax without fixing substitution produces a macro that parses and then silently expands wrong, which is
worse than one that refuses. Both halves belong to the Expansion owner · a
bare `{ }` opening a scope (ADR 0011) · `else if` chains with `KW_ELSEIF` deleted · `do…while` (and
note that this one is **masked in the annotation that should have recorded it**: `loops.fin` is normative,
writes `do { ... } while (false);` at line 35, and carries
`//@ unimplemented "the 'for (i : int = 0; ...)' colon header form"` — one reason for two unbuilt
constructs. Fixing the colon header will not turn that sample green, and the stated reason will then be
wrong rather than merely incomplete, which is the failure mode that makes a green suite untrustworthy.
`do…while` does not parse at all, whatever the condition — `do { } while (false);` and
`do { } while (a == 1);` both give `syntax error, unexpected KW_WHILE` — so it is the grammar, not the
condition expression. The `//@` lines are the Frontend owner's for waves 2–3, so the annotation fix goes
with the parser fix; the point of recording it here is that the census could not have found it, because a
sample already failing for one stated reason looks identical to a sample failing for two) ·
`#for`/`#index` · `blame m1778;` given a production · widened `attr_id` · `TYPE_ID` deleted ·
C-order ternary productions at `parser.y:1174` and `:1300` deleted (ADR 0005).

CRLF handling belongs here too: `\r\n` is one terminator and `\r` is never a column, independent of
what the corpus contains.

**Turbofish on a dotted path**, which this plan had missed and which is a hard blocker rather than a
singleton. Every turbofish production in the grammar begins with a bare `IDENTIFIER` —
`parser.y:1202`, `:1213`, `:1360`, `:1388`, `:1416` — so `foo::<T>()` and `mod::<T>::bar()` parse and
`a.b.c::<T>()` does not. That makes `types.fin:23` a syntax error, and `types.fin:23` is `typeid`, the
function everything else in the standard library is built on. It needs the missing construct twice in
one expression, on both `compiler.structs.select_field::<int>` and `compiler.types.gettype::<T>`, with a
postfix denullify on the result. Nothing in wave 4 is testable until this parses, so it is wave 2 work
and not compiler-API work.

**Additional grammar work the standard library needs**, found by auditing it against the C++ rather
than against the corpus. Groups B, C and D get all thirteen stdlib files past line 2; they do not get
any file to the *end*. Also required: union type aliases (`type X = A | B | C` — there is no `PIPE`
production inside a type, which kills `Number` in `types.fin:51` and `ErrorLike` in `typing.fin:8`);
enum payloads, generics and typed members (`EnumDeclaration` at `src/ast/decls/StructDecl.hpp:66-74`
holds `vector<pair<string, Expression>>` and has no field that *could* hold a type, which deletes
rather than degrades `Result<T,U>` and `IOResult<T>`); the three absent `implements` forms
(`implements cast<...>(fn)`, `pub implements a = b`, `@implements X { }`); `@special` invocation as an
expression (`AT` appears in only four grammar places, all declaration headers, so `@foo(...)` cannot
be called at all); `p.0` positional access; and the empty `{}` prototype literal.

The `dynamic_cast` chain that dropped a `class` declaration's attributes is **done** — `parser.y:50`
now has the `ClassDeclaration` branch. Locked by `Soundness_Attributes.AnAttributeOnAClassReachesTheAST`,
which reads the AST rather than the exit code, because a silently dropped attribute changes no exit code
and so cannot be caught from the CLI. What remains is that nothing *reads* an attribute: `attributes`
appears nowhere in `src/semantics/` or `src/driver/`, so `#[export]`, `#[llvm_name=...]` and
`#[use(compiler)]` reach the AST and stop. That is wave 4's problem, since `#[use(...)]` is the gate on
the component API, but it is worth separating from the parser half now that the parser half is closed.

**Approved sample edits**, all previously ratified: delete `m1778 { break; }` at `loops.fin:47` ·
`variables.fin:37` → `let kilo <auto> = 19;` · `enums.fin:4` → `= any implements <Enum>;` ·
`arrays.fin:14` → `let temp <int>;` · brace `stdio.fin:108` · `readonly.fin:46-49` → an `error`
expectation · `extern_as.fin:4` → `const myglobv <int> = 10;`, dropping the doubled type ·
`simple_pointers.fin:21` → `new &int` · `deeptest2.fin:38` stray `(`, in a file now marked
aspirational.

**Exit criteria:** every normative sample parses; all fifty expectations met; the stdlib agent is
told the parser is ready and begins writing.

## Wave 3 — semantic analysis

The wave 2 agent, continuing; it owns `src/semantics/**` from here. Only `interfaces.fin` has ever reached
this code, so wave 2 finishing will expose the analyzer to forty-one normative samples for the first
time and the failure count will jump before it falls. That is expected and is the point.

Known from the one sample that gets there: `Type 'T' does not have methods` on a generic parameter,
and `printf` rejected as undefined because `Analyzer_Core.cpp:16-34` registers types but no functions.

Scope, in this order: generic parameter method resolution against constraints; then
**monomorphisation**; then **erasure** via erasure markers; then the **fat pointer**, and only for
`any`. That order is not arbitrary. Nine of the eleven currently-passing samples are monomorphic, and
`generics_interfaces.fin` holds a `Castable` function and a bare-`T` function in one file, making it the
natural first differential test. The decisive argument is that erasure-first would make Fin's first
working generics **leak by construction** — an erased call boxes its value-type arguments, and nothing
frees them until a memory library is armed, which needs events, which are two waves out.

`any` is `{i8*, i64}` — payload plus typeid — and an erased generic is raw `i8*`. Emit that layout from
a Fin declaration in `lib/std/` rather than hardcoding it in codegen, which is ADR 0003's "library not
compiler feature" stance applied to itself. There is to be exactly one erasure marker spelling per
behaviour: a marker that changes calling convention by a trailing digit is undiscoverable, so no
`Castable2`. The marker set is `Castable` and `Any`; `Object` and `VoidPointer` appear in no sample and
do not exist. One erased parameter erases the whole function. The comment at `deeptest2.fin:11` calling
`Castable` a "FatPointer type" is wrong and the comment gets fixed — the file is aspirational, so it
convicts nothing (ADR 0008).

Also: `implements` in all four ratified roles; nullable and denullify typing; `readonly` enforcement;
visibility; and the four-member meta-type family — `$type`, `$struct`, `$interface`, `$enum_member` — as
real analyzer types rather than one type with a tag. `std::ops` needs to exist for operators to resolve,
which is the stdlib track's dependency on this one.

**The map is both a prototype and a nominal type, at a stated boundary.** `{K,V}` is the builtin
structural prototype: `src/types/PrototypeType.hpp` is already a complete `Type`, the literal infers at
`Analyzer_Expr.cpp:38-65`, and `let p <{string,int}> = {"a":1};` type-checks end to end today — the only
stdlib-relevant construct that fully works. `HashMap<K,V>` is an ordinary nominal library struct that a
prototype converts into by an explicit call. The boundary has teeth: the prototype carries no hashing or
ordering guarantee and lives on the interpretable side, `HashMap` carries the performance contract and
lives at runtime. `map!{}` expands to a `HashMap`; a bare `{…}` stays a prototype. The deciding evidence
is `memory.fin:27`, where a `@special` returns `{string,string}` from inside the interpreter — collapsing
both into a nominal `HashMap` would drag monomorphisation and vtables across the interpretability line
and land them on `number2str`, which is currently the entire interpreted closure.

Conversion must be an explicit hook, not a widening. `PrototypeType::isAssignableTo`
(`PrototypeType.hpp:27-33`) returns false for everything but itself and `auto`, with the comment "For
now, let's keep it simple." Left as a widening, every prototype would silently convert to every
map-shaped struct.

**Exit criteria:** every normative sample passes semantic analysis with no diagnostics.

## Wave 4 — compile-time execution

The language's central bet, and the reason the standard library can be what it is. Tree-walking
interpreter over the AST during semantic analysis (ADR 0006). A value model spanning Fin values and
compiler-side objects. Compiler components as native C++ bound into the interpreter environment,
reachable only from `@special`. `#[on(...)]` declaration plus `compiler.events.enable(h)` arming,
handlers reading anything and injecting only at the event point (ADR 0007).

A library extends the compiler through one of **three** mechanisms and not one (ADR 0014): an event
injects code at a point the compiler chose and admits many subscribers; a protocol replaces one operation
and admits exactly one claimant; a provider answers a question the compiler asks, once per subject, and
the compiler stores and emits the answer. Layout is settled in two named moments, and a query asked in
the wrong one refuses rather than answering (ADR 0015). Destructors compose, which is what keeps the
event system from having to walk field trees (ADR 0016).

Four more decisions are now recorded, and they bound this wave in ways worth reading before writing any of
it. Compile-time code may read the host machine but may not branch on what it reads, so builds stay
reproducible and the restriction travels through `@special` calls (ADR 0017). A constraint set bounds a
generic and never stores a value (ADR 0018) — written against the alternation form specifically, because the
broader wording would have convicted `enums.fin:10`, where `EnumType` is a perfectly good parameter type. An
interface reference is two words, and the pointer map a provider supplies has three states per word rather
than two, because a vtable pointer is a real pointer into static data and both of the other answers are
wrong (ADR 0019); `Castable` needs that third state today, before any interface reference exists. And
injected code mints fresh identifiers and may spell only module-qualified paths, so a handler can neither
shadow a caller's local nor capture one (ADR 0020) — which lands on the same event payload requirement ADR
0003 reached from the other direction.

One defect surfaced while recording ADR 0017 and is **not** ruled: `GET_MEMORY_LIMIT()` (`memory.fin:38-40`)
returns the *build* machine's available memory into ordinary code, so a compiled binary carries the build
machine's free RAM as its allocation ceiling. The `if` guarding it (`:12-14`) has an empty body and `falloc`
is `fun?`, so the intent was sketched and never finished. Repairing it needs an owner decision about whether
`falloc` consults the host at run time and through what, and it is flagged here rather than folded into the
approved sample-edit list.

### The interpretability line, measured

The stdlib agent has delivered it, and it is far smaller than this wave was scoped for. Seven
`@special` bodies exist across the standard library — `types.fin:22 typeid`, `types.fin:81 tftid`,
`types.fin:88 _resolve_type`, `error.fin:24 is_error_type`, `enums.fin:10 getenumkeyid`,
`memory.fin:38 GET_MEMORY_LIMIT`, `memory.fin:27 mem_info` — and their transitive closure is **exactly
one interpreted Fin function**: `std::number2str` at `types.fin:106`. Three further functions
(`enums.fin:15 getkeyid`, `types.fin:95 resolve_type`, `memory.fin:11 falloc`) sit on the boundary and
fold to constants rather than needing interpretation.

So the interpreter needs **five statement forms and no control flow at all**: `let x <T> = e;`,
`let x <T>;`, `const x <T> = e;`, `p[k] = e;`, `return e;`. No `if`, no loop, no `match` — none appears
in any reachable body. Expressions are limited to literals, identifiers, meta-type identifiers, dotted
component traversal, turbofish on a component call, `==`, the `c : a ? b` conditional, postfix
denullify, and nested calls. **`==` is the only operator**: no arithmetic is reachable, which removes
integer promotion, overflow semantics and operator resolution from this wave entirely.

The value model must span `int`, `uint`, `bool`, `string`, the four-member meta-type family, `{K,V}`
prototypes, `any`, nullable, and an opaque component handle. Explicitly deferred, because nothing
reachable uses them: `new`/`delete` and pointers, `blame`, `try`, `m1778`, lambdas, interfaces as
values, struct instances, arrays, floats, varargs, string concatenation, and every arithmetic operator.

Two constraints fall out of it. **A `@special` call whose argument is not compile-time-known must be a
diagnostic naming that argument** — `resolve_type` and `getkeyid` both pass runtime values to
compile-time parameters today, so the first two library functions written against this rule already
violate it, which is a fair estimate of how often it will be got wrong. And **an `extern` may not be
called from a `@special` body**: `pyprototype/stdlib/builtins.fin:78` has `@special panic` calling
`printf`, which would make compile-time behaviour depend on the host's libc.

`number2str` is `<T: Number>` over the union alias at `types.fin:51`, so it cannot be instantiated
until union type aliases parse (wave 2) and until `compiler.system.get_total_memory`'s return type is
pinned. Its body is currently the placeholder `return "10";`. The entire compile-time story therefore
rests on one function that has never been written — which is good news for this wave's size and worth
stating plainly rather than discovering in month two.

**`#[comptime]`** is the stdlib agent's one ask and I support it: an attribute marking a function as
reachable from compile time, so violating the line is a diagnostic at the edit rather than a failure at
a call site in another module. It depends on the attribute machinery existing at all, which is listed
above as a soundness defect, so the two are one piece of work.

**The measurement was not the whole line, and ADR 0006 has been amended.** Five statement forms and no
control flow is what the *standard library's* reachable closure needs; the corpus needs more.
`literal_interface.fin:4` is `if (@implements(struct_, iface) == true)`, `:17` is
`if (option == IFaceOptions::First)` returning an anonymous `interface { ... }` literal from either arm,
and `literal_struct.fin:27` is `if (!@defined("printf"))` guarding an `@define`. So this wave admits
`if`/`else`, unary `!`, comparison, calls to `@special` functions, and quote-and-splice — and refuses
recursion and every loop form.

That costs nothing of the no-hang guarantee, which is the reason it is affordable rather than a
concession. Compilation can only fail to terminate through iteration or recursion, and everything admitted
above is bounded by the size of the program text. What it does add to this wave is exactly one thing: a
**call-graph cycle check over `@special` functions**, since recursion becomes the only remaining route to
non-termination and nothing else refuses it. Without that check Fin inherits D's documented hang and needs
Zig's `@setEvalBranchQuota` to climb back out; with it, `finc` needs no fuel counter at all.

### The API itself is designed before it is built, by a dedicated owner

The project owner's instruction: the compiler API is *"going to be a strong compiler API that surpases
any other compiler we need plans and even a separate agent to implement and design the compiler API
before implementing it"*. So the five events listed below are a floor and not a ceiling, and the design
is a deliverable in its own right with its own owner, writing to `docs/compiler-api.md`.

Two namespaces, and they are different things rather than two spellings of one (ADR 0012).
`compiler.components.<name>` is a component reference — the grant layer, where a component is named and
asked about. `compiler.<name>.<member>` is the use of it. The test for any future member is whether it
is *about* a component or *through* one. Nothing under `compiler.components` requires a grant, because
a library that must declare a component in order to ask whether it exists cannot negotiate capability
at all; `#[use(...)]` enforcement therefore applies to the operations layer only.

Enforcing `#[use(...)]` convicts the standard library in three places — `types.fin:21-23` declares only
`types` and reaches `compiler.structs.select_field`, `memory.fin:26` and `:37` declare only `system` and
reach `compiler.enums.InBytes`. Three of seven `@special` bodies, including `typeid`. They are header
edits, and catching them before the checker existed is the case for the checker.

Two placement rules settled since (ADR 0012) add three more sample edits and remove a fourth cause of
them. A constant lives under the component whose operations consume it, so `InBytes` relocates to
`compiler.system.InBytes` at `memory.fin:30`, `:31` and `:39` — a user asking how much memory the host has
should not have to grant the enums component to do it. And a meta-type is opaque, read through component
operations rather than off the value, because member access is unreachable by grant enforcement and would
put the layout surface outside the mechanism built to govern it; so `keyidof` (`enums.fin:20-22`) becomes a
`@special` reaching `compiler.enums.keyid_of` plus a plain wrapper, which is the idiom the two functions
directly above it already use. Also here: the two `geykeyid` typos at `stdio.fin:63` and `typing.fin:35`.

A misspelled component name is a hard error, not a silent false. `present()` has to answer false for an
absent component or capability negotiation is impossible, which makes
`compiler.components.evnets.present()` false too and turns a typo into a permanent silent degrade. So
`finc` carries the set of every component name it has ever defined: a name in this build resolves, a name
in the set that this build lacks answers false, a name outside the set is an error at the point of use.
The set is append-only and names are never reused, and it gives did-you-mean for free. Rust shipped this
hole and Cargo needed `check-cfg` in 1.80 to close it.

`compiler.system.get_total_memory` returns `u64`, so `mem_info` instantiates `number2str` at `u64` —
unenforceable until the integer-width defect above is fixed, so it is documentation until then.

Events, as a floor: `variable_scope_exit`, carrying the variable's name, its `$type`, whether the exit
was normal or via `blame`, and whether it was moved out of first — a collector that frees a moved-from
variable double-frees, and the owner's ownership model uses `@move()`. `assignment` on a pointer-typed
target, which `stdptr.fin`'s own/borrow/release *discipline* needs because it is about *rebinding* and not
about scope; "protocol" is no longer available for that informal use, now that ADR 0014 gives the word a
precise meaning. `struct_layout_finalised`, read-only. `function_entry`/`function_exit`.
`allocation_site` on `new`. And `loop_back_edge`, which was deferred for want of a consumer and has one:
a tracing collector needs safepoints, and a loop containing no call is exactly where a program runs
unboundedly without reaching one. Leaving it out rules out every concurrent and incremental collector,
which is a limit worth choosing rather than inheriting by omission. Still deferred for having no consumer
in the corpus: field access, and cast.

ADR 0007's empty-quote amendment stands; the argument this plan gave for it does not, and the retracted
version should not survive here. A handler returning an empty quote *is* a handler, because a library that
only wants to observe — check an invariant, count, read a finalised layout — is a legitimate subscriber and
read-broadly is what it needs. What was wrong was calling that the primary shape on the grounds that a
collector's first need is a per-type pointer map. The pointer map is not a handler at all; it is a provider
(ADR 0014), so it supports nothing either way about the shape of handlers.

`struct_layout_finalised` is one of two moments rather than the only one (ADR 0015). Terra splits the
concern into `__getentries`, which runs while the type is still incomplete and *decides* the layout, and
`__staticinitialize`, which runs "after the type is complete but before the compiler returns to
user-defined code" and whose documented use is building vtables and reading field offsets. So this wave
builds both moments and enforces which questions are legal in each — and an illegal query **refuses and
names the moment** instead of returning zero. That rule is not fastidiousness: D's `getTypePointerBitmap`
returns a pointer-sized span with an all-zero bitmap for an interface reference that is itself a GC
pointer, and a confidently wrong answer to "which words are pointers" is a corrupted heap with no
diagnostic anywhere, ever. The decide moment carries `request_header_words(n)`, additive across claimants
and **returning the offset the caller's words landed at** — without the offset a second claimant knows it
has words and not which are its own. A type with a foreign ABI cannot grow a header, so a request against
one is a diagnostic rather than a silent no-op. This is what gives ADR 0003's collector its mark bit
without an address-keyed side table, which would allocate during collection against an allocator that is
itself the subject of collection.

**Exit criteria:** `typeid` works from `lib/std/types.fin`; `@implements` answers correctly for a
struct against an interface; a `@special` given a runtime argument is rejected with that argument
named; a `@special` that calls itself is rejected by the cycle check; a misspelled component name is
rejected with a suggestion; a provider supplies a pointer map that the compiler stores and emits with the
library writing nothing; a garbage collector written in Fin frees a variable at scope exit, armed by one
line in user code.

## Wave 5 — backend

Fresh LLVM backend (ADR 0002). `runCodeGen` at `Driver.cpp:143` currently returns `true` without
emitting anything, so no artifact has ever been produced. `pyprototype` emits LLVM IR and is worth
reading for lowering shape, but is not being ported.

**Exit criteria:** `finc hello.fin -o hello && ./hello`.

## The other tracks

**Compiler API design.** A dedicated owner, writing `docs/compiler-api.md`, designing the component
surface and the event system *before* wave 4 implements either. Its brief was to beat what Rust proc
macros, Zig `comptime`, Nim macros, C++ P2996 reflection, D `__traits` and Racket's phase system can
do, and to establish that by reading them. **This track is complete.** It delivered roughly 1,220 lines —
the measured substrate, a one-page survey, a four-tier component inventory, the event system, fourteen
design questions, and a twenty-step wave-4 sequence — and it wrote no code and no other document. Its
headline result is a mechanism the brief did not ask for: provider (ADR 0014), which took the motivating
collector from seven of nine jobs needing no amendment to any existing decision, to eight, and let the
owner *withdraw* an amendment it had itself requested earlier. The comparative reading returned exactly
two findings that changed a decision, and both are recorded: D's `object.RTInfo` in ADR 0014, and Terra's
two-moment type protocol in ADR 0015. Of its fourteen questions, **nine** are now answered, two were
resolved without spending an owner's question — one by fixing an ambiguity in ADR 0012, one by finding zero
corpus occurrences — two are deferred with their dependency named, and one is open. The two that closed most
recently are its Q6, answered by ADR 0020 (fresh identifiers, qualified paths only), and the interface half
of its Q12, answered by ADR 0019 (two words, three-state pointer map); neither is still outstanding and
neither should be re-asked. Both problems this plan
had assigned to it are solved: exclusivity exists as protocol and provider, and move-rewriting is a
protocol claiming `move_or_copy`.

**Standard library.** Ten files, none of which parse today; moves to `lib/std/`. Blocked on wave 2 for
writing, unblocked now for planning. Has delivered the measured interpretability line and the five
design questions, all five now answered. Owes its ordered work plan. Once it can compile it becomes the
compiler's largest real test — and it is already the compiler's best source of defects, having found
three of the five soundness items above by reading the C++ rather than running it.

**finn.** Is **waiting on us** for the `finc` interface contract, which is the opposite of what this
line said until it was checked. `finc` appears in exactly one file across all of `~/finn`, and what it
says is that the machine interface contract is "now being implemented compiler-side"
(`docs/REGISTRY-CONTRACT-REPLY.md:324`). There is no 44-item contract document; the only "44" in that
repository is a line range in `validator.rs`. So wave 1b's source is ADR 0009 and this plan, and `finn`
is the consumer that must be told what shipped. Now building: `rustls-tls-native-roots` replacing system OpenSSL, which unblocks
eight clean wheels from four CI jobs; offline and mirror support shipped with auto-install rather than
after it; `~/.finn/` root with independent `FINN_*` overrides; `finn doctor` self-repair; store
cleanup split into project versus store commands. Distributed as `finn-lang` on PyPI providing the
`finn` command, since the name `finn` is definitively unavailable.

The version alias is built as the owner directed — partial pins like `finc = "0.3"` resolve to the
newest installed `0.3.x` — with the mitigation that the exact resolved version is written to the
lockfile, so a second machine reads `0.3.7` and reproducibility survives the convenience.

HTTP retry is hand-rolled rather than taken from a crate: `reqwest-middleware`, `reqwest-retry` and
`url` are all deleted, ratified twice. The deciding argument was that those crates pin to reqwest 0.11
and would block a 0.12 bump in order to add backoff to a single client. Policy: max 3 attempts, retry
only on `429`/`5xx`/connect timeouts, never other `4xx`, honour `Retry-After`, exponential with jitter,
hard overall deadline, plus a concurrency cap on the transitive dependency walk — because the registry
runs on a free tier where a burst that trips the rate limit fails the whole sync, and retry is what
happens *after* you trip it rather than what stops you.

Subcommand naming is settled: **`finn check` inspects your code** by invoking `finc`, **`finn doctor`
inspects your installation** with `--fix`, and `healthcheck` folds into `doctor`. `finn check` is
therefore the first consumer of the machine contract wave 1b builds, and it reads
`--diagnostics=json` rather than parsing the human renderer — ADR 0009 is why that distinction exists.

**finn-registry.** Another agent owns it, and it has delivered a 372-line contract at
`~/finn-registry/docs/REGISTRY-CONTRACT.md`, relayed to `finn`. The model is narrower than its own
published docs claimed: the registry **hosts no code** — GitHub serves the bytes, and the registry owns
only names, publisher identity and trust signals. It deliberately does not resolve semver ranges; that
stays in `finn` with the lockfile. Package names are bare and globally unique, because
`finn/src/commands/add.rs:195-199` treats any slash as GitHub shorthand *before* the registry lookup,
so a scoped name was structurally unresolvable — the docs were corrected rather than the resolver. A
name claim requires proven push access to the repository it points at, checked against the GitHub API.
Trust collapses to a single `trust.level` the CLI switches on, so adding a signal later is not a CLI
change. There is no `finn login` and no token: publishing is a browser flow.

One item in it is ours and is now settled: `finn/src/commands/install.rs:33` invokes the compiler as
`Command::new("python").arg(compiler)` against `~/Fin/pyprototype`. Per ADR 0013 that is not the
compiler and not a fallback — `finn install` invokes `finc`, and the Python path is deleted. The
registry's optional `GET /api/health` is the registry half of the health check the owner asked for, so
reachability belongs in `finn doctor`'s report.

## Sync points

Five contracts, each written down and each with one owner:

1. `finc` ↔ `finn` — the 44-item interface contract. Wave 1b implements it. ADR 0009 records why.
2. `finc` ↔ stdlib — the interpretability line. Delivered; scopes wave 4 and is recorded there.
3. `finc` ↔ compiler API design — `docs/compiler-api.md`. Delivered; scopes wave 4 and is recorded
   there. ADR 0012, 0014 and 0015 fix the namespace split, the three mechanisms and the two layout
   moments it designs within.
4. `finn` ↔ `finn-registry` — delivered as `~/finn-registry/docs/REGISTRY-CONTRACT.md` and relayed.
   Three items are open on `finn`'s side: whether `recognized` prompts, whether a publisher-attested
   checksum is worth a submission step, and how to handle a download count the registry cannot observe.
5. Release artifacts — wave 1c produces what `finn` consumes: archive naming with architecture,
   sha256 per archive, the version index.

## What this plan does not do

`match` lands after the enum layer works end to end, and not before. It appears in **zero** of the
fifty samples — the only occurrence of the word is inside a comment at `functions.fin:41` — and there
is no `MATCH` token, no production and no AST node. So it is not a gap in the compiler, it is a new
feature with no specification to build against. The corpus's actual discriminant idiom is
`if (getkeyid(enum_) == keyidof(Ok))` (`typing.fin:27`, `stdio.fin:55`), and `match` would be sugar
over a mechanism that does not exist: `EnumDeclaration` (`src/ast/decls/StructDecl.hpp:66-74`) holds
`vector<pair<string, Expression>>` with no field capable of holding a payload type. It also needs
`$enum_member` to exist first, since `keyidof` takes one and a `match` arm *is* an enum member. The
sharpest argument against landing it early: `stdio.fin:63` and `typing.fin:35` both call **`geykeyid`**,
a typo in two files that nobody has caught because nothing runs. Exhaustiveness checking would hide
that entire class of defect behind nicer syntax instead of exposing it. Both typos are in the approved
sample edits.

Constructor member-initialiser lists are aspirational and deliberately unbuilt, so `readonly` members
have no way to be initialised with a constructor argument — a real gap, accepted for now.

Event exclusivity and move-rewriting are solved, and this is no longer where to look for them. ADR 0014
names three mechanisms instead of one: an event admits many subscribers, a protocol replaces one operation
and admits exactly one claimant, and a provider answers a question the compiler asks. An event admitting
exactly one handler remains inexpressible, and deliberately so — an extension point that wants a single
claimant is not an event, and it says so by being a different mechanism.

Optimisation passes are out of scope until wave 5 produces something to optimise.

### Seven copies of one loop, three of which read the bound

`fun f<T: NoSuchType>(x: T)` compiled clean. `struct S<T: NoSuchType>` did not. Same construct, same
file, twenty lines apart, opposite results -- which is the kind of asymmetry that names its own cause. Seven
declaration forms can carry generic parameters and each had its own copy of the registration loop: function,
struct, class, interface, an operator requirement inside an interface, an operator definition inside an
implements block, and the `fn<T>(...)` type node, plus the lambda expression. Struct and class resolved the
bound and then passed it to a debug log; the other six never looked at `gen->constraint` at all.

All eight sites now call `SemanticAnalyzer::declareGenericParams`. It runs two passes rather than one --
every parameter name is declared before any bound is resolved -- because the single-pass order the struct
site used cannot accept `<T: Comparable<T>>` or `<T: Castable, U: T>`, where a bound names a sibling
parameter or the one it constrains. Nothing in the corpus needs that yet; it costs one extra loop and
removes a whole class of future bug report.

**The line that mattered was an assignment nobody had ever written.** `GenericType::constraint`
(`src/types/GenericType.hpp:9`) is read in exactly two places, both behind a null guard:
`checkConstraint` (`Analyzer_Core.cpp:222`) and `getStructType` (`Analyzer_Expr.cpp:27-33`, whose comment
reads "If T : Interface, treat T as Interface"). A grep for `->constraint =` across all of `src/` returned
nothing. Both readers had therefore been unreachable since the day they were written, and the field they
read defaulted to `nullptr` forever. Assigning the resolved bound to it un-deadened both at once, and the
second is a feature rather than a check: a method call on a bounded type parameter resolves now. That is the
defect `tests/samples/interfaces.fin` was pinned on -- `item.to_string()` at `:17` reported `Type 'T' does
not have methods`, and the sample's own comment at `:15` says "T must implement Printable (checked in
Semantics)". Its expectation now records the printf gap instead, which is the stdlib and not the analyzer.

**A false rejection found by probing for false acceptances.** `visit(ImplementsBlock&)` resolved each
method's parameter types in the block's scope without opening one for the method or declaring its
`generic_params`, so `pub fun m<T>(item: T)` inside an implements block reported `Undefined type 'T'` --
a method's own type parameter, undefined inside its own signature. The signature now resolves in a scope of
its own, which is also what stops one method's parameters leaking into the next.

**Four meta-types registered, and deliberately not a prefix rule.** `$type`, `$struct`, `$interface` and
`$enum_member` are the compile-time reflection types, and the corpus defines them rather than leaving them
to be guessed: `stdlib/types.fin:33` writes `fun cast<_Type: $type>(...)` and comments "$type == literal
type"; `:83` returns one from `tftid(tid: uint) <$type>`, "returns a type from typeid";
`stdlib/enums.fin:22` takes `$enum_member` with the example `keyidof(Ok)`; `literal_interface.fin:5` takes
`$interface` and `$struct` in one signature. The grammar has known all four since wave 2 -- `parser.y:1776`
builds them from `DOLLAR` plus the keyword and `:1783` accepts any `$name` -- and the analyzer knew none, so
all four died at `Analyzer_Core.cpp:204`. They are registered as four listed names because that `DOLLAR
IDENTIFIER` production means a `$`-prefix rule would silently accept every misspelling as a type;
`PrimitiveType` rather than a new class because its assignability is name equality plus one `int`->`float`
rule, which is exactly the behaviour a meta-type needs today, and because nothing treats "is a
PrimitiveType" as "is a number" -- the numeric predicates above it are explicit allowlists. **What a `$type`
value can do belongs to wave 4 and registering the name does not decide it.**

Seven new tests, seven mutants, and each mutant killed exactly the test bound to it. Five tests were seen
red before their fix. The other two were written after, and are the two the matrix is really for:
**M16 stores nothing** -- resolve the bound so every bogus-bound test still passes, then delete the
assignment -- and kills only `ABoundIsVisibleToMethodResolution`. Without that one test the one line that
made two dead readers live could be deleted and the suite would stay green. **M18 gives an unbounded
parameter a bound anyway** and kills only `AnUnboundedParameterStillHasNoMethods`, which is the assertion
that resolving methods *through* a bound did not become resolving methods on anything shaped like a generic.
**M15 skips the second pass** and kills all four bogus-bound tests plus the method-resolution one --
including the pre-existing struct control, which is the evidence that the refactor really did subsume the
struct site instead of leaving it on a path of its own. **M13 registers `$type` as `auto`**: every other
meta-type test still passes, and only `AMetaTypeIsNotAssignableFromAnOrdinaryValue` dies, which is the
difference between a name that resolves and a name that swallows anything. **M12** drops one of the four
registrations and kills one test; **M14** applies the `$` prefix shortcut and kills the guard against it;
**M17** removes the implements-block scope and kills the false-rejection test.

Corpus effect, measured sample by sample before and after: no sample changed exit code, and the diagnostic
count fell by seven overall -- `interfaces.fin` 2 to 1, `stdlib/types.fin` 20 to 13, three others down one
each, against `lambdas.fin` 2 to 3 and `stdlib/stdio.fin` 33 to 36. Every added diagnostic is one the
compiler previously owed and did not pay: `Undefined type 'Addable'` at `lambdas.fin:71:40` and three
`Undefined type 'Any'` in `stdio.fin`, all of them bounds naming types that genuinely do not resolve. Four
sample expectations were updated where they had gone stale, `interfaces.fin` because its pinned defect is
fixed and three because they named `$type` as a thing that fails.

**Two KnownDefects survive this fix and their comments were rewritten, because the reason they survive is no
longer the reason they were written for.** `AStructInstantiationViolatingItsBoundIsAccepted` used to say the
bound was resolved and discarded; the bound is stored now and `checkConstraint` does run, and it returns
true because the argument is a `PrimitiveType` and `checkConstraint` reports only on a `StructType`
argument. **Ruling needed:** does a primitive satisfy an interface bound? `Castable` and `Any` are erasure
markers (ADR 0018) spelled as bounds in the standard library, and every primitive must satisfy those, so
"reject every non-struct" is not obviously right and the three-line fix waits on the answer.
`AnArgumentViolatingAFunctionBoundIsAccepted` survives for a different reason again: nothing on the path
through `visit(FunctionCall&)` looks at the callee's generic parameters at all, so there is no site where
the bound would be consulted even now that it is there to consult. A stale comment on a KnownDefect is
worse than no comment, because it sends the next fixer to a function that is already correct.

Suite: **321 tests, all passing**, corpus 50/50. Lane note: `src/semantics/**` and `src/types/**`, the
crossing already disclosed, plus one comment in `tests/samples/**`; no parser change.

### Twenty parser sites set a flag nobody read

`fun? make_A(n?: int) <A>` parsed. `let x? <int> = null` parsed. `pub v? <int>` parsed. Every nullable
spelling in `nullifier.fin` parsed, and every one of them meant exactly what the same line without the `?`
meant. `TypeNode::is_nullable` is assigned in twenty places in `parser.y`; a grep for it across
`src/semantics/` and `src/types/` returned nothing. The grammar had learned the whole feature and the
analyzer had never been told, which is the third time this shape has turned up -- after
`ImportModule::namespace_path` and `GenericType::constraint` -- and a fourth turned up inside this same wave
(a parameter's default value, below). **Wave 2 taught the grammar; wave 3 owes the analyzer, and the debt is
always a field that is written and never read.**

**A wrapper type, not a flag on `Type`.** The obvious implementation is a `bool` on `Type`, and it is wrong
for a measurable reason: `Scope::resolveType("int")` hands back *one shared `PrimitiveType` object* for every
`int` in the module. Setting a flag on it makes every `int` in the program nullable. `PointerType` is the
precedent already in the tree -- same shape, same reason -- so `NullableType` holds a `TypePtr inner` and
`int?` is a distinct object wrapping the shared `int`. It also gets the nesting right for free: `p? <&int>`
is `(&int)?`, not `&(int?)`.

**A distinct `NullType`, not `&void` and not `NullableType(void)`.** `null` was typed `&void`, which bought
pointer assignability for free and cost every diagnostic that mentioned it: `let x <int> = null` reported
`expected 'int', got '&void'` -- naming a pointer type in a program containing no pointers. `NullableType(void)`
was the other candidate and would have required sniffing `inner->toString() == "void"` at every site that
wanted to know whether a value was the literal, plus it makes a writable `void?` behave as a bottom type.
`NullType` prints `null`, and `isNullLiteral()` is the one predicate that asks.

**What `= null` means, settled by the corpus rather than by argument.** `nullifier.fin:4` calls `b? <int>`
"equavelant to `b <int> = null,`", which reads two ways: either the initialiser is what makes a slot
nullable, or it is merely permitted on any slot. Two normative samples decide it. `deeptest4.fin:6` writes
`integer <int> = null` and line 16 then compares `a["Hi"].integer` with `10`; `stdlib/error.fin:11` writes
`err_code: int = null` and line 14 passes `err_code` straight into an `<int>` field. Neither denullifies, so
**the initialiser is permitted and the declared type is unchanged** -- `let x <int> = null` is legal and `x`
is still an `int`. That reading lives in `checkInitializer`, which is deliberately *not* folded into
`checkType`: `checkType` is also the assignment check, and `x = null` on the next line must still be
refused.

**One place reads the flag, for all twenty sites.** Reading the twenty assignments before designing anything
was what made the fix small: the flag is set on the *type node* in every case, `fun?` included. So
`resolveTypeFromAST` was split into a wrapper plus `resolveTypeUnwrapped` -- the old body, unchanged, with
its eight `return` points -- and the wrapper is the single line that wraps. Patching the eight returns
instead would have reproduced the exact bug this feature already was: a new arm that silently forgets. `fun?`
needed no return-type handling of its own as a result; it needed only an exemption from the missing-return
check.

Six sites in all: the `resolveTypeFromAST` wrapper; `KW_NULL` retyped to `NullType`; a `QUESTION` arm in
`visit(UnaryOp&)` that strips one level of nullability; the arity check in `visit(FunctionCall&)`; the
missing-return condition in `visit(FunctionDecl&)`; and `checkInitializer` at five declaration sites (two
struct member defaults, two class member defaults, one `VariableDeclaration`). `Type::isAssignableTo` gained
the nullable-target widening that lets a plain `int` flow into an `int?`, guarded to step over
`NullableType` and `NullType` sources -- which is both the correct rule and the recursion guard.

**Three scope limits, each pinned by a test rather than left to be discovered.** The null-comparison
exemption covers `==` and `!=` only, so `null < 3` is still an error (`NullIsNotOrdered`). The arity minimum
is one past the last non-nullable parameter, which falls out of positional binding and invents no ordering
rule of its own; when it differs from the maximum the diagnostic says "expects between 1 and 2 arguments"
rather than lying about a single figure. Postfix `?` on a never-nullable value is the identity rather than an
error, because the only thing the corpus says about that case is `nullifier.fin:36`, and it says it about
`any`, which has no ruling yet.
**A defect found while fixing another.** `visit(Parameter&)` resolves the declared type and visits the
default expression without ever comparing the two, so `fun g(n: string = 3)` is accepted. That is why
`ANullDefaultOnAParameterIsAccepted` was green before this wave began, and it is booked as its own ruling
below rather than fixed here, because it shares a visit with the parameter-assignability question and the two
should be answered together.

**Mutation testing found three things and none of them was in the compiler.** Nineteen mutants were applied
to the six touched files, each built and run against the suite; eighteen died and one survived, and six tests
were killed by nothing. Every one of the three problems that exposed was in the *verification* rather than in
the implementation, which is the outcome worth writing down: a green suite over a correct implementation is
exactly the situation in which a weak test is invisible.

*One test asserted a rule it never reached.* M5 -- "`NullType` is not assignable to a pointer" -- removed the
pointer arm from `NullType::isAssignableTo` outright and **survived**. `APointerStillAcceptsNull` was written
as `let p <&int> = null`, a *declaration*, so `checkInitializer` waves the null through before assignability
is ever consulted. The test named a rule, and the program it compiled could not reach it. Rewritten to use
an argument (`deeptest3.fin:75`'s `print_if_exists(null)`), it now dies with M5, and `APointerMayBeAssignedNull`
covers the third position -- assignment -- that no exemption touches. `ANullableSlotAcceptsNull` had the same
defect against M6, and is a more interesting case: it spells `nullifier.fin:34` exactly as the sample spells
it, so it is worth keeping as-is. Its comment now says what it does and does not prove, and
`ANullableSlotMayBeAssignedNull` reaches the rule it cannot.

*The matrix was one-sided.* Three of the six unkilled tests -- `ANonNullableParameterRejectsNull`,
`ANonNullableReturnRejectsNull`, `APlainParameterIsStillRequired` -- were unkilled not because they were weak
but because **every mutant in the set made a rule stricter**. A test that asserts a rejection cannot be
killed by a mutant that rejects more. The two inverses were missing: M20 (`NullType` assignable to anything)
kills six, M21 (arity has no lower bound) kills one. A mutation matrix that only tightens measures half of
what it looks like it measures.

*The driver lied about the state it left behind.* Each mutant restored its sources and verified them by
sha1 -- and left the last mutant's **binary** in `build/`. Two conclusions were drawn from it before the cause
was spotted: that `int?` printed as `int` (it was M19, "prints without its question mark") and that a green
test was failing (M21, "arity has no lower bound"). Verifying the input you restored is not verifying the
artifact you produced. The driver now rebuilds from the pristine sources as its last act and says so.

The matrix now runs 21 mutants against 36 tests with **no survivors**, and two tests that no mutant kills.
`ANullableSlotAcceptsNull` is one, described above. `ANullDefaultOnAParameterIsAccepted` is the other, and it
stays that way on purpose:
the code that would compare a parameter's default against its declared type does not exist, so there is
nothing to mutate. Its comment records that it passes because the check is absent, and what must happen to it
when the check arrives.

**The corpus effect, measured sample by sample.** 348 diagnostics before, 335 after. Five samples improved --
`deeptest4` 15 to 11, `nullifier` 10 to 5, `undefined_behavior` 2 to 1, `stdlib/collection` 26 to 24,
`stdlib/error` 4 to 3 -- **no exit code changed and no sample gained a diagnostic.** Two expectations were
rewritten to match: `nullifier.fin` now names its five remaining diagnostics individually instead of gesturing
at "nullability is unimplemented", and `stdlib/error.fin` records that lines 11 and 14 type-check now and that
what is left on line 13 is `Cannot assign to immutable variable 'err_code'` -- a normative sample and the
compiler disagreeing about whether a parameter may be assigned in the body.

Two spellings turned out not to exist, which is worth recording so that a later wave does not "fix" them:
`n: int?` does not parse (the corpus writes `n?: int`, `nullifier.fin:16`) and `<T?>` does not parse (the
corpus never writes it). Neither is a gap; both are simply not Fin.
Four questions came out of the work and are booked below under "Nullability's four remaining edges": what a
postfix `?` on a never-nullable value means, whether `nullifier.fin:39`'s `let _? <int> = make_A()` is a slip
or a rule about `_`, whether a parameter may be assigned inside the body, and the unchecked parameter default.
One earlier ruling was **struck** rather than answered: "does `fun?` make the return type nullable, or only
permit a `null` return" was on the list, and the corpus answers it without an owner. `nullifier.fin:23`
returns `A{...}` from a `fun?` with no wrapping, and `undefined_behavior.fin:9` has a `fun?` fall off the end
of the body entirely, so `fun?` is a nullable return type and nothing more.

Suite: **357 tests, all passing**, corpus 50/50. Lane note: `src/types/**` and `src/semantics/**`, the
crossing already disclosed, plus `CMakeLists.txt`, `tests/test_soundness.cpp`, and two expectation comments in
`tests/samples/**`; no parser change.

### A visitor nobody called

`fun g(n: int = nosuchvar)` compiled clean. The byte-identical struct-member spelling,
`struct S { pub v <int> = nosuchvar, }`, reports `Undefined variable 'nosuchvar'`. A parameter default was
the only expression in the language that no pass ever visited.

The cause is not a missing check. `SemanticAnalyzer::visit(Parameter&)` exists, resolves the parameter's type
and walks the default -- and nothing dispatches to it. Every parameter list in the analyser is iterated by
hand: eleven loops that read `param->name` and `param->type` and never `param->default_value`. So the visitor
was correct and dead, and a check added inside it would have changed nothing. That is the same shape this wave
has now hit three times -- `declareGenericParams` had eight copies, the parameter loops have eleven -- and the
same lesson: in this codebase, "where is this handled?" and "where is this reached from?" are different
questions, and the second one is the one that matters.

The fix splits along a ruling, so it was built as two halves and only one landed.

**The walk.** Ruling-free and now done: a helper, `visitParameterDefaults`, called from eight of the eleven
loops. The three exclusions are deliberate. A struct constructor's parameters are walked twice, once to
register the signature and once for the body, and a class constructor's likewise; the call belongs to the
body pass only, or the diagnostic appears twice. The interface-constructor loop is unreachable for this
purpose because `I(n: int = ...)` does not parse. `visit(Parameter&)` was left in place -- the Visitor
interface requires it -- with a comment at its definition saying it is dead, so the next reader does not spend
what this cost to find out.

**The type check.** Written as `KnownDefect_ParameterDefaults` rather than as code, because it is blocked on
the integer ruling: `stdlib/stdio.fin:87` and `:109` write `nbytes: ulong = -1`, and checking a default would
put `Type mismatch: expected 'ulong', got 'int'` on two lines of a normative sample. That is ruling #1, and it
now blocks two things.

Twelve mutants over the unit's tests. The eight per-site mutants each delete one call, and each is killed by
exactly the tests belonging to that site and no others -- which is the useful result, because it proves the
ten walk tests map onto the eight sites with nothing redundant and nothing unreached. Making the helper a
no-op restores the original defect and kills all ten at once. Adding the call to both constructor *signature*
passes kills exactly the two constructor tests, which is what proves their "reported once" assertions bite.
Dropping the helper's null guard kills four. And applying the blocked half naively -- `checkType` instead of
`checkInitializer` -- kills three, of which one is the interesting one: `ANullDefaultIsStillAccepted`, because
`stdlib/error.fin:11` writes `err_code: int = null` and a plain `checkType` has no null exemption. So the
eventual shape of the blocked half is known before it is written rather than discovered after: it must be
`checkInitializer`.

Two of the twelve found problems, and for the second unit running, neither was in the compiler.

The pair of tests asserting the diagnostic appears *once* went red against a correct compiler. They counted
occurrences of `nosuchvar` in stderr, and the caret snippet echoes the offending source line -- which contains
`nosuchvar`. Every correct single report looks like two. Count the message, not the identifier.

And a mutant survived: moving the walk above the loop that defines the parameters in scope changed no test
result. `ADefaultMayNameAnEarlierDeclaration` claimed to establish what a default may name, but it named a
*global*, which is visible from the enclosing scope too, so it could not tell where the walk happened. The
sibling-parameter case is what pins it -- `fun g(a: int, b: int = a)` -- and with that test added the mutant
dies. Both findings are the same kind as the nullability wave's: over a correct implementation a weak test is
invisible, and mutation is the only thing that looks.

One pre-existing `KnownDefect` went red, which is the convention working. `AParameterDefaultIsNotAnalysedAtAll`
had diagnosed this defect earlier, reached the wrong remedy (it proposed adding `checkType` to the dead
visitor), and left an instruction for whoever fixed it: invert this, and check that the diagnostic points
inside the default rather than at the function. It does -- column 16 in `fun f(x: int = nosuchthing)` is where
the default expression starts -- so the inverted test asserts the column, in both the bare-name and the call
spelling, since those take different paths and report different messages. Its two type-half siblings moved to
`KnownDefect_ParameterDefaults` so the unit's evidence sits together.

A second defect turned up in passing and was measured rather than fixed. A default does not make a parameter
optional: the arity check computes `required` as the index of the last non-nullable parameter plus one, so
nullability makes a parameter optional and nothing else does, and a default has no observable effect at a call
site at all. Fixing it is not a line -- the arity check reads a `FunctionType`, which records only
`param_types`, `return_type` and `is_vararg`, so it means a new field through eleven construction sites plus
`substitute` and `clone`. By measurement it also ranks last: the corpus declares exactly three defaulted
parameters (`stdlib/stdio.fin:87`, `:109`, `stdlib/error.fin:11`) and calls none of them, and the one call that
would need this, `Error("The answer is forbidden")` at `blame_assert.fin:15`, is commented out. Corpus effect
of the fix: zero diagnostics. Two `KnownDefect` tests hold it open with that reasoning attached.

Corpus effect of what did land: none. 335 diagnostics before, 335 after, snapshot byte-identical -- which is
the expected answer and worth confirming rather than assuming, since the three defaults the corpus does write
are `-1` and `null` and both walk clean.

Suite: **375 tests, all passing**, corpus 50/50. Lane note: `src/semantics/**` and `tests/test_soundness.cpp`;
no parser change, no type-system change, no sample change.

### One unresolved type, twenty-seven diagnostics

`const.fin` reported 26 errors. One of them was true. The other 25 followed from a single unresolvable
annotation, `rptr`, in a single parameter: the parameter's type failed to resolve, so the analyser never
defined the parameter, so every one of the nine mentions of it in the body reported `Undefined variable 'a'`,
and every member access on those undefined values reported again.

The rule the analyser was following is the one the codebase writes twenty-one times over: *if a declaration's
type does not resolve, drop the declaration.* Written out, it is the wrong trade in every instance --
suppressing one diagnostic about a type in exchange for one diagnostic per use of the thing it typed. But it
is not obviously wrong at any single site, which is why it is at twenty-one of them:

```cpp
auto t = resolveTypeFromAST(param->type.get());
if (t) currentScope->define({param->name, t, false, true});   // four sites
if (t) paramTypes.push_back(t);                               // six sites
if (memberType) structType->defineField(...);                  // two sites
if (retType) structType->defineMethod(...);                    // two sites
if (retType) structType->defineOperator(...);                  // five sites
if (type) currentScope->defineType(node.name, type);           // one site -- a type alias
if (!retType) return;                                          // one site
```

The last two are the ones that cost the most and read the worst, and neither was found by reading: the
alias site was found by ranking the corpus after the other nineteen were fixed, and the operator sites by
mutation. Both are below.

The first measurement was the useful one, and it was misleading. Defining the unresolved parameter as `auto`
removed all nine `Undefined variable 'a'` from `const.fin` -- and left the sample's total at 26. The cascade
had not gone away; it had changed shape, into five `does not have methods` and four `is not a struct`. That is
what settled the design: suppressing the *name* lookup is not enough, because the cascade continues through
every operation performed on the value. What is needed is a type that absorbs.

`src/types/ErrorType.hpp` is that type. It substitutes and clones to itself, and -- the properties that do the
work -- member access on it yields itself, a method call on it yields itself, indexing it yields itself, and a
cast of it yields the cast's target type. `checkType` returns true the moment either side is one, at any depth
of pointer, array, nullable or function wrapping. It prints as `<error>`, and a test asserts that string never
reaches a user.

It cannot be `auto`, and this is worth recording because `auto` is the obvious candidate and it is available:
`auto` is a *target-side wildcard* (`Type.cpp:16`) that means "infer from the initialiser" in a declaration.
Give a variable with an unresolvable annotation the type `auto` and the inference branch adopts the
initialiser's type and the program compiles clean. The sentinel has to be a type the language cannot spell.

The rule is now written once, in `resolveTypeOrError`, and called from all twenty sites. Its comment carries
the precondition that makes the substitution safe: the grammar admits no untyped declaration -- `fun f(self)`
is a syntax error, "expecting COLON" -- so a null return always means a type was written and failed, never
that none was written. Without that, the sentinel would silently type an inferred parameter as `<error>` and
absorb every check on it.

**Ten defects found while fixing it, six fixed, four booked.**

*An abandoned extern.* `visit(DefineDeclaration)` opened with `if (!retType) return;`, which left the extern
unregistered -- so every call to it reported an undefined name -- and, because the bare `return` sat above the
parameter walk, also skipped the analysis the previous unit had just built. One line, two features. It is the
site that matters most of the twenty: the standard library is a wall of `@define`, and one unresolved type in
one of them silenced every use of it.

*Two live nulls in an interface.* `ifaceType->defineMethod(name, retType)` and
`ifaceType->defineOperator((int)op->op, retType)` were both unguarded while their struct and class
counterparts were guarded, so an unresolved return type on an interface requirement handed a `nullptr` to a
map whose every reader dereferences it -- `StructType.cpp:65` clones what is stored, `:86` substitutes it.
Nothing in the corpus instantiated an interface with a broken requirement, which is the only reason this had
never crashed.

*A dropped operator.* The operator return type is the same rule at five more sites, and it produced the
worst-reading cascade of any of them: with the operator undeclared, `s + 1` fell through to the built-in rule
for `+` and reported `Type mismatch: expected 'S', got 'int'` -- which reads as though the program had not
declared the operator it plainly did declare.

*An alias that named nothing.* `type Output = Any<...>;` on line 6 of `stdlib/operators.fin` was charged once
for the annotation and then thirty more times, because `visit(TypeDefinition&)` gated `defineType` on the
target resolving -- so the alias's *name* was undefined, and each of the thirty operator requirements that
returns `Output` reported `Undefined type 'Output'`. Thirty diagnostics about a declaration written six lines
above them, none of them about the one that failed. An alias is a declaration like any other: the name was
written, so the name exists, and what it names is the sentinel. This one site was worth more than the other
twenty put together on a per-sample basis -- `stdlib/operators.fin` went from 57 diagnostics to 27, the largest
single reduction in the corpus so far -- and it was worth nothing at all until the sentinel existed to be what
the name resolves to.

*The sentinel leaking into a message.* Two holes, each for a different reason. `visit(CastExpression&)`
hand-rolls its own validity table and consults neither `isErrorType` nor the `isCastableTo` family, so
`cast<float>(x)` on an untypeable `x` reported `Invalid cast from '<error>' to 'float'`. And `isErrorType` did
not look inside a `FunctionType`, so a function whose return type failed -- registered as
`fn(int) -> <error>`, deliberately, so that its arity survives -- printed that spelling the moment the
function was named as a value. Both are now in the leak test, which is the test that had to grow: it is easy
to write a sentinel that suppresses cascades and still shows the user a type they cannot write.

*A generic operator's own parameter, undefined in its own signature.* Booked, not fixed.
`visit(StructDeclaration&)`'s signature pass resolves each operator's and method's return type from a scope
holding the *struct's* generic parameters and not the member's own, so `operator + : <T>(other: <T>) <T>`
reports `Undefined type 'T'` about a parameter it declares one token earlier. The parameter list escapes only
because those annotations are resolved a second time, later, in a scope that has `T`. Two hundred lines below,
`visit(ImplementsBlock&)` already does the right thing -- a scope per method, `declareGenericParams` before the
signature, with a comment saying why -- so the fix is those three lines in two more places. It is booked
because the honest version is the single copy that the eight existing `declareGenericParams` calls should
collapse into, and that is a refactor with its own tests.

*A generic type alias that is never declared at all.* Also booked, and not the drop-the-entity rule but worse:
`visit(TypeDefinition&)` returns early for any alias carrying generic parameters, resolving the target for its
diagnostics and never calling `defineType` -- so `type ArrayType<T> = [T];` leaves `ArrayType` undefined
whether or not `[T]` resolved. The debug log calls it "partially supported"; nothing is supported. It costs the
corpus nothing today because `arrays.fin:4` writes one and never names it, and it will cost a diagnostic per
use the first time anyone does. The fix is a `TypeAlias` in the type system that can be instantiated.

*A diagnostic reported once per pass.* `visit(StructDeclaration)` walked every member default twice -- once
registering, once analysing -- and both walks reported, so `pub v <int> = nosuchvar` said
`Undefined variable 'nosuchvar'` twice. The registration pass's walk is deleted: the analysis pass has
`currentStructContext` set and reads the field type back from the struct, so it is the well-formed one. The
implements block had the same duplication for a worse reason and it is also deleted -- see the matrix below.
What remains is constructor parameters and method return types, where the fix is structural rather than a
deletion, and it is booked as `KnownDefect_ErrorRecovery` with the measurement that ranks it last: its corpus
footprint is *zero*. Exactly one pair of diagnostics in the 50 samples shares a message and a location, and it
is a module path.

**A location bug the sentinel exposed.** Unhiding errors moved the corpus census red:
`Soundness_DiagnosticAttribution` found two diagnostics landing on `//@` expectation comment lines in
`prototype_test.fin`. Both were newly surfaced -- `HashMap::from_prototype(...)` had never been analysed,
because the unresolved annotation on the declaration it initialised used to be fatal to the whole statement --
and both came out at `1:1`, because `static_method_call` builds a `TypeNode` for the qualified name and set a
location on the call but not on the type inside it. It is the type that fails to resolve. Five productions,
five one-line fixes, `@1` rather than `@$` so the caret lands on the type name. This is the second time that
census has found an unlocated node this way and the fifth production family to need it; the pattern is that a
node built inside an action, rather than reduced from a rule, is the one that gets forgotten.

*Lane note.* Those five fixes are in `src/parser/parser.y`, which is agent C's lane. Crossed deliberately and
recorded here: the failing test's own message names the file and the production, no agent was alive to hand it
to, and the change adds no production and alters no grammar -- six `setLoc` calls inside existing actions.

**Measured.** 335 corpus diagnostics before, **310** after the first nineteen sites, **280** after the alias.
27 cascades removed by the first pass, 2 real errors surfaced --
`Undefined type 'HashMap'` and `Undefined type 'Collection'` in `prototype_test.fin`, which had been invisible
for as long as the sample has existed. No sample's exit code changed. The reductions:

| sample | before | after |
|---|---|---|
| `const.fin` | 26 | 11 |
| `stdlib/hashmap.fin` | 24 | 19 |
| `stdlib/collection.fin` | 24 | 22 |
| `stdlib/prototypes.fin` | 11 | 9 |
| `stdlib/types.fin` | 13 | 12 |
| `nullifier.fin` | 5 | 4 |
| `useful_macros.fin` | 6 | 5 |
| `prototype_test.fin` | 15 | **17** |

and then, from the twenty-first site alone:

| sample | before | after |
|---|---|---|
| `stdlib/operators.fin` | 57 | **27** |

Every ranking-by-diagnostic-count in the sections above was measured against the 335 and is now inflated by
whatever share of its count was cascade. The next unit's ranking is re-derived from what is left.

**A correction to every count in this document.** All of them, this section's 335 / 310 / 280 included, came
from `grep -c 'error:'`, which also matches the source line echoed under the caret -- so `stdlib/error.fin`, a
sample whose subject is error handling, was inflating the total by six. The exact figure after this unit is
**274**. The snapshot script now anchors at `^error:` and lives in the repository rather than in `/tmp`, at
`tests/tools/corpus_snapshot.sh`, with the histogram command that produces the rankings in its header comment
and a note saying which figures above predate it. Numbers from here on are exact; numbers above read about six
high, uniformly enough that no ranking changes.

The operator, cast and function-type fixes moved the corpus by exactly **nothing** -- 310 before them and 310
after, identical per sample. They were found by probe and by mutation, not by the corpus, which is the useful
thing to record about them: the corpus is a specification, not a test suite, and it exercises what the samples
happen to say rather than what the analyser happens to do. The alias fix is the other half of that lesson --
found by ranking the corpus, worth thirty diagnostics, and invisible to every probe and mutant that had been
written up to that point, because nothing in the unit's thirty tests had thought to write an alias.

**Tests.** 41 in the unit: 37 `Soundness_ErrorRecovery` and four booked defects. Twenty-one for the sites,
each its own test rather than a loop, because they are twenty-one kinds of entity and a fix to one is not a fix
to another -- the eight-site matrix in the previous section is the evidence for that, not a guess, and this
unit's matrix confirmed it site by site. Seven controls, which are the half that matters for a sentinel: a type
that absorbs too much is a worse compiler than one that cascades, and nothing in a "this no longer reports"
test can tell the two apart. A member access on a primitive, a method call on a primitive, an index on a
non-array, a real type mismatch, an undefined name, a missing struct member and a cast whose target is wrong
all still report, and the sentinel's own spelling is asserted absent from seven programs designed to provoke
it.

**Mutation: thirty-three mutants, and what the zero-kill ones were actually telling us.**
Every test in this unit was written after the fix it covers, so not one of them has been seen red. The matrix
is `/tmp/ermut.py`; it takes two shapes. Half the mutants put the drop-the-entity gate back at one site, which
the "does not cascade" tests must kill. The other half make the sentinel *more* absorbing -- `isErrorType`
answering true for every type, for a primitive, `isAssignableTo` accepting everything, `ErrorType::equals`
matching anything, a cast answering the sentinel instead of its target -- which the controls must kill. A
one-sided matrix proves nothing about a sentinel: absorbing too much is the failure mode a suppression
mechanism actually has, and no "this no longer reports" test can see it.

Six anchors were wrong on the first run, and both ways of being wrong were the same mistake: a substring of
something else. A nine-space-indented anchor is a substring of the twelve-space copies of the same line, and
the comment `// 2. Methods` is a prefix of `// 2. Methods (Signatures)`. Both matched more places than
intended and the count check caught it, which is the argument for the count check: an anchor that silently
matched twice would have produced a mutant nobody could interpret.

Six mutants then killed nothing, and the resolutions fell into three kinds rather than the usual two:

*The test was missing.* `R-cfield` and `R-ctorbody`. Every member test in the unit used a `struct`, so the
class copy of the member loop was uncovered; no test used a constructor *body* parameter, only a signature. Two
tests added.

*The test was weak.* `R-iface`. It declared an interface with an unresolved return type and never read the
stored value back, so a live `nullptr` in the map was invisible to it. Strengthened with a bounded generic that
calls the requirement -- `fun use<T: I>(v: T) <int> { let s <int> = v.m(); ... }` -- which is the path through
`StructType::substitute` that dereferences what was stored.

*The code was dead.* `R-impl`. The implements block resolved every method parameter into a local
`std::vector<TypePtr>` and then dropped it: `StructType::methods` maps a name to a return type and nothing
else, so the signature that loop built had no reader. It was also reporting every parameter annotation a second
time. Deleted -- and one `KnownDefect` case went green as a result, which is the convention working: the case
was split out into `Soundness_ErrorRecovery.AnImplementsBlockMethodParameterIsReportedOnce` rather than the
expectation relaxed.

*The consumer does not exist yet.* This is the third kind, and it is the one worth writing down, because the
reflex from "the code is dead" is to delete. `R-impl`'s deletion is only correct because method calls are
checked against nothing today -- probes confirmed a method call checks neither its arity nor its argument
types -- so the parameter types genuinely have no reader. The moment they do, that loop's work is needed again,
in a different place. `KnownDefect_MethodCalls.AMethodCallIsNotCheckedAgainstItsSignature` books exactly that,
with three cases and a free-function control that proves the arity check exists and simply is not reached; when
it is inverted, the signature belongs on `StructType`, built once, not rebuilt per implements block. A booked
defect is what makes a deletion reversible.

That inversion has since happened -- see *A method call was checked against nothing* below -- and it landed
where the booking said it would: `StructType::methods` holds a whole `FunctionType`, built once by
`buildMethodSignature`, and the implements block calls that instead of rebuilding a vector it drops. The
deletion was reversible because the booking said what would reverse it.

*Three mechanisms hiding each other.* `R-checktype`, `R-assign` and `I-castall` all killed nothing, and the
reason was not any test: `ErrorType` overrode both assignability directions, `Type::isAssignableTo` had a
branch for the sentinel, and `checkType` short-circuited before either could run. Any two covered for the
third, so no single mutant was observable. The structural argument settles which to keep -- `isAssignableTo`
has exactly one caller outside `src/types/`, and it is `checkType`, which short-circuits first; `isCastableTo`
has none at all, because `visit(CastExpression&)` hand-rolls its own table. Both type-level branches deleted,
the short-circuit kept as the only mechanism, because it is also the only one of the three that suppresses the
*message*. `R-checktype` now kills eighteen tests. Their comments say why they are absent, so the next person
does not reinstate them: a second caller of `isAssignableTo` must short-circuit the same way.

*Two mechanisms that are not redundant.* `R-opret` and `R-opdecl` also killed nothing each, and this pair
resolved the other way. An operator is registered twice -- once in the struct's signature pass, once when its
body is walked -- and dropping either alone leaves the other to declare it. Deleting one would have been wrong:
the signature pass is what an *earlier sibling's* body sees, and the body pass is the only one with the
operator's own generic parameters in scope. So the resolution was three things rather than a deletion. A test
whose method is declared *above* the operator, which only the signature pass can satisfy, kills `R-opret`. A
combined `R-opboth` covers the pair. And the generic case that makes the body pass independently observable is
the `KnownDefect_Generics` above -- with a note in its failure message telling whoever fixes it to check
whether the second registration has become redundant, and delete it if so.

Two further findings came from the never-killed list rather than from a zero-kill mutant:

*A control no mutant could reach.* Three tests assert that an unrelated undefined name, a missing struct
member and a real type mismatch still report when some *other* type failed -- the guard against one bad
annotation muting a file. Nothing in the sentinel's machinery can kill them, because a name with no declaration
has no type that could have failed. The mutant has to be the wrong fix itself: `I-undefname` answers the
sentinel from `visit(Identifier&)`'s undefined-variable path, `I-nomember` from the missing-member path. Both
are plausible bad fixes for a cascade, both now kill their control, and both are named in the tests' comments.

*A mutant that could not tell two copies apart.* The struct and class member loops are textually identical, so
one substitution mutated both and the struct's test killed first -- leaving the class test in the never-killed
list, looking redundant when it is not. The fix was to the matrix: `patch()` takes an occurrence index, and
`R-methodS`/`R-methodC` and `R-opretS`/`R-opretC` patch one copy each. The class copies are separate code --
`R-cfield` exists precisely because the class field loop has a line the struct's does not -- and now the matrix
can say so.

Splitting the mutants was necessary and not sufficient, which is worth separating: with `R-methodC` patching
only the class occurrence it *still* killed nothing, because the class test asserted a diagnostic count over a
program whose `main` was `return 0;`. Undeclaring a method changes nothing a program that never calls it can
notice. So the matrix limitation was hiding a weak test rather than excusing one, and both had to be fixed --
the test now calls the method, as its struct sibling always did. That is the second time in this unit the same
sentence applied: a count assertion is only worth writing over a program that would notice. Each of the four
split mutants is now killed by exactly one test.

Two tests were also changed for failing to be killable. The anti-`auto` test asserted only that the program
exited 1, which stays true when the sentinel *is* `auto`, since the annotation reports either way; it now
asserts what `x` becomes. And the operator-parameter count test had a body that never mentioned the parameter,
so dropping the definition changed nothing to count; it was folded into the sibling whose body uses it twice
and deleted. A count assertion is only worth writing over a program that would notice.

One `KnownDefect` went red and was inverted per its own note, which had specified this exact fix in
advance: "a sentinel error type -- one that resolution returns instead of nullptr, that is
assignable to and from everything ... would let the function be registered with its correct arity and let this
call go quiet." Both objections that had kept the registration gated turned out to be answered by the sentinel
rather than argued away, and the inverted test now asserts the thing that distinguishes a sentinel from a
shrug: `f(1, 2)` against `fun f(p: NoSuchType)` still reports `expects 1 arguments, got 2`.

Final matrix: **33 mutants, 32 lethal, 0 anchor failures**, and the one that was not -- `R-methodC` -- was the
weak test above, lethal once the test called the method. **51 of the 84 tests** under the matrix's filter are
killed by some mutant; the 33 that are not are the ones no mutant in this unit's machinery reaches (parameter
defaults, diagnostic locations, the booked defects), and they belong to other units' matrices. The tree is
rebuilt from verified-pristine sources at the end of every run, so a matrix that dies half way cannot leave a
mutant in the tree.

Suite: **417 tests, all passing**, corpus 50/50. Lane note: `src/semantics/**`, `src/types/**`,
`tests/test_soundness.cpp` and the new `tests/tools/corpus_snapshot.sh`; plus the six `setLoc` calls in
`src/parser/parser.y` disclosed above. No sample changed.

### `any` was not a ruling, it was a reading

The plan asked the owner three questions about the type system -- what `any` is, what `object` is, what
`Any<C>` means -- and put 46 of the corpus's 274 diagnostics behind the answers. Two of the three were
already answered, in the corpus, by evidence the plan had not looked for: **which files use the name, and
what they import.**

`stdlib/operators.fin` writes `any` and imports nothing. Not "imports something that might re-export it" --
the file has no `import` statement. `nullifier.fin:12` writes `fun unpack(v: any)` in a file whose only
import is nothing at all. A name that a file uses without importing is either a builtin or a bug in the
file, and it appears in five files that way. So `any` and `object` are builtins, and registering them is a
two-line change to the `SemanticAnalyzer` constructor.

The same test says `Any` is *not*: `stdlib/types.fin:69` declares `pub type Any = any;` inside an
`#[export]` block, which is a library declaration with an export marker on it. Handing `Any` out as a
builtin would compile files here that a real standard library rejects, which is the worst kind of green, and
`Soundness_DynamicTypes.AnUnknownTypeNameIsStillUndefined` is the control that keeps it out -- along with
`anyy`, `objekt`, `Object` and `AnyType`, because the wrong fix here is a rule that shrugs at unknown names
rather than two registrations.

`any` and `object` are two types and not one, and the corpus is what distinguishes them. `types.fin:97`
takes an `any` and casts on the way out: erasure, resolved at compile time. `prototype_test.fin:40`
annotates `<{object, object}>` for a prototype whose members are heterogeneous: a box, present at runtime.
Nothing in the analyser needs the difference yet -- both accept everything -- but codegen will, and a single
registration would have to be split later, so both names are registered now.

What remains genuinely owed is narrower than the three questions it replaces: whether a primitive satisfies
an `implements` bound. That one the corpus does not answer, and it cannot be guessed, because
`stdlib/types.fin:78` writes `type nullptr = any implements <&void>;` -- a bound that is a pointer type and
not an interface at all. A check that only understood interfaces would reject a line the standard library
writes.

**Three fabrications, one mistake.** Registering the names turned up something the missing registration had
been hiding. Three sites answered a shape they did not understand by constructing a `StructType` whose name
was the text the program had written:

```cpp
// visit(TypeDefinition&), for any alias with an `implements` bound:
auto anyType = std::make_shared<StructType>("any"); // Placeholder
// resolveTypeUnwrapped, for any resolved non-struct given generic arguments:
type = std::make_shared<StructType>(node->name, args);
```

A `StructType("any")` has no fields, no methods, no relation to the interface, and a `toString()` of `any`.
It is the worst answer available *because it is an authoritative one*: `type E = any implements <I>; let x
<E> = 5;` reported `Type mismatch: expected 'any', got 'int'`, naming as a concrete type something the
program never wrote as one, and `v.nosuch` reported `Struct 'any' has no member 'nosuch'` about a struct
that does not exist. The second site is the one the corpus actually reaches: `Any<...>` is how the standard
library spells its escape hatch, so every use of it became a struct that accepted nothing.

The third was quieter. An `implements` bound was resolved by nobody, so `type E = any implements
<NoSuchInterface>;` was accepted in silence. An unenforced bound that also fails to reject a misspelling is
not a partial implementation, it is a blind spot -- and closing it is the only change in this unit that
*added* a diagnostic to the corpus: `stdlib/enums.fin:6` bounds on `Enum`, and no file in the corpus
declares `Enum`. Three diagnostics became four, and the fourth is true.

**Where the assignability rules live, and why it is not obvious.** Every type is assignable to a dynamic
type. The rule is four tokens; its placement took the reasoning:

* Not as an override on `DynamicType`. An override speaks for the *source*, and this rule is about the
  *target* -- it is every other type that has to accept this one.
* Not in `checkType`, which is `isAssignableTo`'s only caller outside `src/types/` and therefore the
  tempting place. `isAssignableTo` is reached **recursively**: `ArrayType::isAssignableTo` asks its element
  types, so `[int]` fits `[any]` through this line. A copy in `checkType` would have answered for `any` and
  not for `[any]`, and `stdlib/types.fin:102` takes `const &arr: [any]`.
* With `NullType` excluded, because `nullifier.fin:36` says so in the sample: "this should be an error since
  type any cannot be null". `null` is not a value of a type, it is the absence of one, so a rule stated as
  "a dynamic target accepts anything" accepts null too, silently.

That last exclusion is only observable in *assignment* position, and finding out why cost a test. A
declaration initialised to `null` is accepted whatever it is annotated -- `checkInitializer` short-circuits
on it, because `deeptest4.fin:6` writes `integer <int> = null` and `stdlib/error.fin:11` writes `err_code:
int = null` and neither is nullable. So `let x <any> = null;` is clean for a reason that has nothing to do
with `any`, and the first version of the test asserted on exactly that program: it would have tested
`checkInitializer` while claiming to test this rule. `x = null` on the next line is where the rule is
decidable, and that is what the test asserts.

**A defect found sideways.** `ArrayType::isAssignableTo` checked its element types, and then threw the
answer away:

```cpp
if (!element_type->isAssignableTo(*otherArr->element_type)) return false;
if (is_fixed_size && !otherArr->is_fixed_size) return true;
return false;                                    // same shape -> not assignable
```

Two arrays of the same shape only ever matched through `equals` in the base class, which is exact. So
`[int]` did not fit `[auto]`, and would not have fitted `[any]` either -- every permissive element rule in
the type system stopped at the array boundary. The element check above is the whole rule; reaching it and
discarding its answer is the bug.

**Corpus: 274 diagnostics -> 235.** `stdlib/operators.fin` alone goes from 27 to 1, and the one that is left
is `Undefined type 'Any'` -- correctly, per the reading above. `stdlib/types.fin` 12 to 7,
`stdlib/prototypes.fin` 9 to 5, `nullifier.fin` 4 to 1, `prototype_test.fin` 17 to 16, `arrays.fin` 6 to 5,
`stdlib/enums.fin` 3 to 4. Seven `//@ unimplemented` notes rewritten to say what each file reports now.

**Two Soundness tests lost their vehicle**, which is worth recording because a Soundness test is not
something this suite removes lightly. Both used `any` to demonstrate that an undefined type reports where it
was written -- one of them said so explicitly: "40 of the corpus's 99 `Undefined type` diagnostics name it".
Registering `any` retired that diagnostic. `TheCaretSpansTheTypeNameAndNothingElse` is repointed at a
different three-character undefined name, since the assertion is about the caret's *width*.
`TheAnyTypeReportsWhereItWasWritten` is removed: `any` now resolves in every position, `S implements <any>`
is accepted, and `new any{}` fails in the parser with a token location rather than a node one, so there is
no diagnostic left that can point at a `KW_ANY` node. Keeping it would have meant giving it a different
vehicle, which makes it a second copy of the test above it. The removal and its reason are in the file, where
a reader looking for it will be.

Two `KnownDefect_AnyType` tests were inverted rather than relaxed, as the convention requires; one became
`Soundness_DynamicTypes.AnyIsRegistered`, keeping its distinctive program (a declaration with *no*
initialiser, since registration and inference are separate mechanisms) and its assertion that the fix did
not belong in the lexer. The other is deleted, because its one program -- a parameter of type `any` -- is one
of the seven positions in `AnyIsSpellableInEveryPositionTheCorpusWritesIt`.

A dead predicate was written and then deleted in the same unit. `isDynamicType` was added by analogy with
`isErrorType` and had no consumer, and the analogy is false: the sentinel must be *absorbed* wherever it is
buried, so its predicate looks through Pointer/Array/Nullable, whereas `[any]` is an ordinary array whose
assignability is answered element by element. The two sites that ask whether a type is dynamic ask with
`as<DynamicType>()`, about that type and nothing inside it. The reasoning is kept as a comment where the
function was, because the next person to reach for the analogy deserves it.

### Mutation matrix: the dynamic types

Eighteen mutants over six files, two-sided by construction. The (a) half puts back what the unit removed --
the three `StructType` fabrications, the two missing assignability rules, the registrations themselves -- and
a named test has to kill each. The (b) half pushes a rule past what it should accept, and for a type that
accepts everything that half is the load-bearing one: "every type fits `any`" is one word from "`any` fits
every type", and only a control can tell the two apart. A one-sided matrix would have reported full lethality
on a `DynamicType` that also *fitted* every target, which would have type-checked the whole standard library
wrong and looked like success.

| mutant | kills | |
|---|---|---|
| `R-dyntarget` no type is assignable to a dynamic type | 9 | |
| `R-regany` `any` is not registered | 13 | |
| `R-regobj` `object` is not registered | 3 | |
| `R-fabalias` an alias with a bound is a fabricated struct again | 4 | |
| `R-arr` two arrays of the same shape are not assignable | 2 | |
| `R-bounds` a bound is discarded unread again | 2 | |
| `R-boundattach` the bound is resolved and then not recorded | 1 | |
| `I-dynsource` a dynamic type is assignable *to* every type | 3 | |
| `I-null` a dynamic type accepts null | 1 | |
| `I-regAny` `Any` is registered alongside `any` | 1 | |
| `I-regAnyObj` `object` is registered under the name `any` too | 1 | |
| `I-arrall` any array is assignable to any array | 1 | |
| `I-boundrender` the bound is dropped from the rendered name | 1 | |
| `I-autoisdyn` `auto` is treated as a dynamic type | 1 | |
| `R-arrdecay` the fixed-size decay runs the wrong way | **0** | finding, 5 on re-run |
| `I-eqname` two dynamic types are equal whatever their names | **0** | finding, ruling owed |
| `I-substname` `substitute()` replaces the name with a bound | **0** | no-op, 1 on re-run |
| `R-fabgeneric` `any<int>` is a fabricated struct again | -- | did not build, 2 on re-run |

**Fourteen lethal, three zero-kill, one that measured nothing** on the first pass. Three of the four are
resolved below and their re-run numbers are in the table; the fourth, `I-eqname`, is waiting on a ruling. Each of the last four is resolved below,
because a mutant that kills nothing is a finding and not a failure -- it says that some rule in the tree has
no test able to see it, and the useful work is finding out which of five things is true: the test is missing,
the test is weak, the code is dead, the consumer does not exist yet, or two mechanisms are hiding each other.

**`R-fabgeneric` did not build, which means it measured nothing.** This is worth stating plainly because a
build failure in a matrix reads like a kill and is the opposite: the mutant never ran, so the rule it targets
is still unmeasured. The mutant deleted the fabrication but left the variable it had initialised, and the fix
is to append the fabrication rather than replace the line. With the anchor fixed it **kills 2** --
`Soundness_DynamicTypes.AnyWithGenericArgumentsIsStillDynamic` and
`KnownDefect_DynamicTypes.GenericArgumentsOnADynamicTypeAreNotConstraints` -- so the second fabrication site
is measured after all, and by the two tests that were written for it.

**`R-arrdecay` was two copies of one rule.** The fixed-size array decay is written in
`ArrayType::isAssignableTo`, and it *was also written* in `Type::isAssignableTo`, which the override calls on
its first line. The base copy is reachable only from inside the override, and it accepts a strict subset of
what the override accepts -- `element_type->equals` where the override asks `isAssignableTo`, one direction
where the override handles both. A strictly narrower rule that answers first can never change an answer, so
the duplicate was invisible; and it was invisible in both directions, which is what made the mutant survive.
Break the override's copy and the base's answers. Break the base's copy and the override's answers.

The first diagnosis was wrong and is worth recording as such: the reverse direction (`[int]` into a
`[int; 4]` annotation) is genuinely unwritable, because `[int; 4]` does not parse in type position, and that
looked like enough of an explanation. It is not -- an array *literal* is constructed fixed-size
(`Analyzer_Expr.cpp:593`), so `let a <[int]> = [1, 2];` reaches the decay from the writable side, and
`Soundness_Arrays.AFixedListInitialisesADynamicArrayOfTheSameElementType` already asserted it. A test
existed, the rule was reached, and the mutation still killed nothing. Only the duplicate explains that.

The same duplication holds for the void-pointer rule, found by looking for the pattern rather than by a
mutant: `Type::isAssignableTo` carried `otherPtr->pointee == "void"` and `PointerType::isAssignableTo`
carries it plus the source direction plus the recursive case. Both base arms are deleted in the following
unit, and `R-arrdecay` re-run against the single remaining copy **kills 5**. Nothing about the tests changed
between the two runs.

**`I-eqname`: the consumer does not exist yet.** `DynamicType::equals` compares names, and nothing can
observe whether it does. The "every type fits a dynamic target" rule in `Type::isAssignableTo` answers before
equality is ever consulted in either direction, so `any` and `object` are mutually assignable whatever
`equals` says; the one remaining channel was the cast, which rejected both names alike. A test that killed
this mutant would have to assert that `object` does not fit `any` -- a *ruling*, not a fact about the tree,
and one the corpus does not settle (see **Rulings owed**). So the resolution is the fourth kind: the
comparison is right, the consumer is missing, and deleting the comparison to make the matrix green would
merge `any` and `object` into one type, which is a stronger claim than any evidence supports. The state is
booked in `KnownDefect_DynamicTypes.ObjectAndAnyAreMutuallyAssignable`, which asserts today's behaviour so
that the day a ruling arrives, the test fails and says where to go.

**`I-substname` was a no-op mutant, which is a fifth failure mode worth naming.** It replaced the
substituted type with the first *bound*, and no corpus type carries one -- so on every program the matrix
ran, `sub` was empty and the mutant was the pristine code. Zero kills measured nothing about
`DynamicType::substitute`; it measured that the mutation did not apply. The re-run replaced it with an
unconditional loud sentinel (`StructType("__subst__")`), which killed nothing either -- and then with
`std::abort()`, which also killed nothing. An abort separates the two remaining explanations, because a crash
is proof of arrival: `DynamicType::substitute` was **never called** by any test in the binary or any sample in
the corpus. Not unobserved -- unreached.

That is the first kind of finding, the plainest one: the test was missing. The path is perfectly writable, and
one program reaches it --

```fin
struct Pair<T> {
    pub k <any>
    pub v <T>
}
fun main() <noret> { let p <Pair<int>>; let n <int> = p.k; }
```

-- because instantiating `Pair<int>` substitutes over the struct's fields and `k`'s type is a `DynamicType`.
Two things must hold and they pull against each other: `T` must be replaced, or the instantiation did nothing,
and `any` must **not** be, or the erasure has silently become whatever the neighbouring parameter was bound to.
`Soundness_DynamicTypes.AnyInsideAGenericSurvivesInstantiation` asserts both, in the plain and the bounded
spelling (`type E = any implements <I>;`, the shape stdlib/types.fin:74 actually writes). With it in place
`I-substloud` and `I-substabort` each kill 1, and `I-substname` -- the no-op -- kills 1 as well, because the
bounded case finally gives it a bound to return.

### Five copies of one rule, and the container that had none of them

`let a <any> = [1, 2];` compiled clean. `let a <any> = { 1: 2 };` said
`Type mismatch: expected 'any', got '<{int, int}>'`. Same target, same shape of question, opposite answers --
and the difference was not about prototypes at all. `PrototypeType::isAssignableTo` was the only override in
`src/types` that did not open with `Type::isAssignableTo(other)`. Every other one does: `ArrayType`,
`PointerType`, `NullableType`, `NullType`, `PrimitiveType`. That first line is how each of them inherits the
three rules that are about the *target* rather than the source -- `-> auto`, `-> any`/`-> object`, and
`-> T?` -- and an override that skips it opts out of all three by omission.

It had not skipped them silently. It carried `if (other.toString() == "auto") return true;`, which is one of
the three, copied in. That is the tell: somebody hit the `auto` case, fixed the case, and left the other two
lost. A rule that is about the target cannot be restated per source, because the set of sources is open.

The same class of defect, three more times in the same file:

**Key and value were compared with `equals`.** So a prototype fitted only a prototype of exactly its own key
and value types, and `<{object, object}>` -- which `prototype_test.fin:40` writes as an annotation over a
literal of an `int`, a `float` and a struct -- accepted nothing at all. `ArrayType::isAssignableTo` asks its
element types for *assignability* and has a comment explaining why, ending "reaching the element check and
then discarding its answer meant every permissive element type stopped at the array boundary". The prototype
is the second container boundary and had not learned it.

The direction is pinned deliberately and the matrix holds it there. `<{int, int}>` does not fit
`<{int, any}>`, because a prototype is a mutable container and a covariant value type would let a write
through the wider alias put a `string` where the narrower one promises `int`. `[int]` into `[any]` has exactly
the same hole and is what the corpus asks for (`stdlib/types.fin:102` takes `const &arr: [any]`), so this is
not settled tree-wide -- but nothing asks for it here, and the narrow rule is the one that can be widened
later without breaking a program that already compiles.

**A heterogeneous prototype literal inferred `any`.** The sample says otherwise in its own voice.
`prototype_test.fin:14` writes `let a <auto> = { 10 : 10, "a": true };` and comments *"auto would resolve to
`<{object, object}>`"*. Line 40 annotates `<{object, object}>` over a mixed literal and explains the choice:
"object type is an expensive type but can fit any datatype in it at the cost of memory and speed". A cost paid
at run time is a box. `any` is the other half of the pair -- `stdlib/types.fin:97` calls it "any type that is
visible in compile time" -- and inferring it here claimed the compiler still knew a type it demonstrably did
not. It had a second, quieter cost: every mismatch about a mixed literal printed `<{any, any}>`, a claim about
representation that the diagnostic had no business making. This inverted
`KnownDefect_Prototypes.AHeterogeneousPrototypeLiteralInfersAnyNotObject` into a Soundness test, as the
convention requires, rather than relaxing it.

**A cast rejected every dynamic type on either side.** `nullifier.fin:12` writes `cast<int>(a)` where `a` is
`any`, and `stdlib/types.fin:33` declares `fun cast_to<T>(value: any) -> T`, whose body can only be that cast.
Casting *out* of a dynamic type is the entire reason to have one. The corpus site survived by accident:
`cast_to`'s target is the generic parameter `T`, and the validity check already admitted a `GenericType` on
either side -- so the sample never exercised the dynamic target, and the thing that would have shown the
defect was the standard library's own signature rather than any line of a sample. Both directions are now
valid and both are unchecked, deliberately: a cast is the program overriding the checker, and `cast<int>(x)`
on an `any` is an assertion about what the box holds that nothing at compile time can verify. The run-time
check belongs to codegen, which will need a tag to compare and has none yet.

**Two arms of the base class are deleted.** The array decay and the void-pointer rule were each written twice
-- once in `Type::isAssignableTo`, once in the override that calls it on its first line -- and the mutation
matrix for the previous unit could not kill either copy. See that matrix's resolution of `R-arrdecay` for the
full account; the short version is that the base copy is reachable only from inside the override and accepts a
strict subset of what the override accepts, so it can never change an answer, and its presence made the
override's copy unkillable. `R-arrdecay` re-run against the single surviving copy kills 5. The inverse mutant,
`R-basearr`, which puts the dominated arm back, kills 0 -- which is the proof that the deletion changed no
behaviour, stated as a measurement rather than as an argument.

`Soundness_Pointers.AVoidPointerIsAssignableInBothDirections` was written *before* the deletion and passed
immediately. That is unusual enough to say out loud: it exists to make a deletion safe rather than to catch a
defect, and it is the reason the deletion is a refactor and not a change.

**Corpus 235 -> 233**, both in `prototype_test.fin`, whose `//@ unimplemented` note now describes what is
left: prototype *access*. `a[10]`, `a["b"] = false` and `delete &a[10]` each report
`Type '<{object, object}>' is not an array or pointer`, and `a.rm("b")` reports `does not have methods` -- a
prototype reaches the array path and the struct path in the analyser and has no path of its own. The index at
`20:7` is checked against `int` before the object's shape is looked at, so that one construct reports twice
and the extra message is about a key type that is `object`. That is the next prototype unit, and it is a
larger one: it needs an index rule keyed on the container, a member-assignment rule that creates, `delete` on
a prototype member, and the two methods the sample calls (`rm`, and `HashMap`'s interface behind it).

### A container literal whose element did not type

Found by probe while checking whether the empty-prototype fallback was reachable, and it is the sentinel
unit's lesson arriving at a boundary that unit did not reach. Those five sites -- member access, method call,
index, cast, `checkType` -- were all about a *named entity* whose annotation failed. A container literal gets
there from the other direction: nothing was annotated, so nothing failed to resolve; an element simply has no
type because the expression inside it was already reported.

The two literals answered differently and both answers were wrong.

The prototype substituted `any`, so `let a <{int, int}> = { nosuchvar : 1 };` reported the undefined name and
then `expected '<{int, int}>', got '<{any, int}>'` -- a claim about a boxed key the program never wrote, which
is the exact shape the sentinel exists to prevent. It now substitutes `errorType()`, and `isErrorType` unwraps
a prototype the way it already unwrapped `[T]`, `&T`, `T?` and `fn(...) -> T`. One further rule was needed and
is the sort that only shows up when you write it down: **once a side is the sentinel it stays the sentinel**,
or `{ nosuchvar : 1, 5 : 2 }` sees `<error>` and `int` disagree, widens to `object`, and puts the cascade back
with a different type in it.

The array had the opposite bug. `if (!firstType) return;` was quiet, but the `return` skipped the loop that
visits the remaining elements, so `[n1, n2]` reported `n1` and never looked at `n2`. **Suppressing a cascade
and skipping a walk are one diagnostic apart and are opposites** -- the first drops a message that says
nothing new, the second drops a message about a different mistake. The parameter-defaults unit learned the
same thing about a default that was never visited. Typing the literal `[<error>]` and continuing does both
jobs at once: every element is walked, and each is compared against a sentinel that `checkType` absorbs.

Two fabricated types went with it. `if (!keyType) keyType = std::make_shared<PrimitiveType>("any");` stood
after the loop as a belt-and-braces fallback, and it had quietly become a trap: `any` is a registered
`DynamicType` now, so a hand-built `PrimitiveType` spelled the same way would *print* as `any` and behave as
none of it. The remaining guard is the sentinel, and it is unreachable while `{}` is a syntax error
(`unexpected RBRACE`) -- a guard rather than an assertion because the parser is the only thing keeping it that
way.

**The corpus moved by nothing**, which is the useful thing to record about this half: no sample writes an
undefined name inside a container literal. It was found by probe, like the operator, cast and function-type
fixes before it, and for the same reason -- the corpus is a specification, not a test suite, and it exercises
what the samples happen to say rather than what the analyser happens to do.

One gap is booked rather than fixed: `prototype_test.fin:45` writes `[]` as a prototype value inside
`<{[int], [{int, string}]}>` and gets `Empty array literal cannot infer type.` from an annotation that says
exactly what it is. An empty literal in a position with a known expected type is inferable, and nothing in the
`ArrayLiteral` visit can see that position. That is a *bidirectional* inference question, not a container one,
and it wants doing once for arrays, prototypes and struct literals together.

### Mutation matrix: prototypes, casts and the container boundary

Twenty-two mutants over seven files. Four of them are not new rules at all -- `R-arrdecay`, `R-arrsame`,
`R-ptrvoidtgt`, `R-ptrvoidsrc` re-test the array and pointer rules from the previous matrix against a tree
where the base class no longer carries a second copy of them. That re-test is the whole reason the deletion is
in this unit rather than filed as a tidy-up: a rule with two implementations is a rule with no measurement, and
the only way to show the deletion mattered is to run the same mutant twice.

| mutant | kills | |
|---|---|---|
| `R-arrdecay` the fixed-size decay runs the wrong way | 5 | 0 before the duplicate went |
| `R-arrsame` two arrays of the same shape are not assignable | 2 | |
| `R-ptrvoidtgt` a pointer is not assignable to `&void` | 1 | |
| `R-ptrvoidsrc` `&void` is not assignable to a pointer | 1 | |
| `R-protobase` `PrototypeType` does not consult the base target rules | 1 | |
| `R-protoeq` key and value compared by equality again | 1 | |
| `R-protoany` a heterogeneous prototype literal infers `any` again | 1 | |
| `R-protonowiden` a heterogeneous literal keeps its first element type | 1 | |
| `R-castdyn` a cast with a dynamic type on either side is invalid again | 1 | |
| `R-errproto` `isErrorType` stops unwrapping a prototype | 2 | |
| `R-protosent` an untyped prototype element becomes `any` again | 2 | |
| `R-arrbail` the array literal bails out before walking the rest | 1 | |
| `I-protoall` any prototype is assignable to any prototype | 1 | |
| `I-protorev` key and value assignability runs in the reverse direction | 1 | |
| `I-protokeyonly` only the key is checked | 1 | |
| `I-protovalonly` only the value is checked | 1 | |
| `I-ptrall` any pointer is assignable to any pointer | 1 | |
| `I-errall` `isErrorType` calls every prototype an error | 2 | |
| `I-castall` every cast is valid | 1 | 3 against the whole binary |
| `R-basearr` the dominated array arm comes back to the base class | **0** | finding, predicted |
| `R-protosticky` the sentinel does not stick across the widening | **0** | finding, 1 on re-run |
| `I-substloud` `substitute()` on a dynamic type returns a loud sentinel | **0** | the previous matrix's, 1 on re-run |

**Nineteen lethal, three zero-kill.** One of the three was predicted before the run, one was a real hole in a
test written in this unit, and one belongs to the previous section and is resolved there.

**`R-basearr` kills nothing, and that is what it is for.** It puts the deleted array arm back into
`Type::isAssignableTo`. If the argument for deleting it was right -- the arm was reachable only from inside the
override that also stated the rule, and accepted a strict subset of what the override accepts -- then restoring
it cannot change any answer, and a mutant that cannot change an answer cannot be killed. So this is the third
kind of finding, code that is dead, deliberately introduced to check a claim about the shape of the code rather
than about its behaviour. Read together with `R-arrdecay`'s 0 before the deletion and 5 after, with no test
changed between the two runs, the pair is the measurement: the rule was invisible because it was written twice,
and it is visible now because it is written once.

A mutant whose zero is predicted is worth building anyway. The prediction is the hypothesis; without the run,
"that code was dominated" is an argument, and arguments about which of two overlapping conditions answers first
are exactly the kind that turn out wrong -- this one already did once, when the first explanation for
`R-arrdecay` was an unparseable type annotation.

**`R-protosticky` kills nothing, and that was a hole.** The mutant deletes both arms that keep the error
sentinel from being widened away:

```cpp
else if (isErrorType(kType) || isErrorType(keyType)) keyType = errorType();
```

The prototype literal widens two elements of differing types to `object`, which is right for two real types and
wrong for the sentinel: widen it and the literal comes out `<{object, int}>`, a real type, which compares, which
mismatches, which is the cascade back by another route. The rule was written in this unit, from the reasoning,
and no test could see it -- because every existing test used `{ n1 : 1, n2 : 2 }`, where *both* keys are the
sentinel, and `<error>.equals(<error>)` is true, so the widening branch was never reached and the deleted arms
never ran. The programs that reach them mix one untyped element with one typed one, in either order and on
either side of the colon, and `Soundness_ErrorRecovery.ASentinelElementIsNotWidenedAwayByItsSiblings` now
writes all four. It kills the mutant.

This is the second kind of finding by the letter -- a test existed and was too weak -- but the useful way to
say it is that a test whose cases are all the *same* case is one test. Four programs in a loop looked like
coverage of the untyped-element rule and were four spellings of the branch that does not widen.

**`I-castall`'s single kill was the filter, not the tests.** It makes every cast valid, and it killed exactly
one test, when the suite plainly contains others that assert a cast is rejected --
`KnownDefect_Casts.TheCorpusOwnStringToCharArrayCastIsRejected` and `TheCorpusOwnPointerCastIsRejected`, both
of which assert a non-zero exit and the words `Invalid cast`. The reason is that `KnownDefect_Casts` was not in
the matrix's `--gtest_filter`. Re-run against the whole binary the mutant kills 3.

So the filter is part of the measurement, and the rule that follows is worth keeping: **an over-permissive
mutant is measured against every test, not against the suites the unit touched.** A restore mutant can be
scoped, because it puts back a behaviour whose tests are known. A permissive one cannot, because permissiveness
can break a test anywhere -- and the tests most likely to notice are the `KnownDefect_*` ones, which assert
that something is *rejected* today. Those are the assertions a permissive mutant flips, and they are the ones a
filter drawn around "this unit's suites" leaves out.

### A method call was checked against nothing

`KnownDefect_MethodCalls.AMethodCallIsNotCheckedAgainstItsSignature` had been booked for four sections, and the
booking was worse than it read. `s.m("x", "y")` on a one-parameter method reported nothing, which is the half
the name describes. The other half was that the call's *type* was the type of its last argument:
`StructType::methods` mapped a name to a return type, `visit(MethodCall&)` walked the arguments after reading
that map, and `lastExprType` was left holding whatever the last argument turned out to be. So a program could
be wrong in a way that produced a confidently wrong diagnostic somewhere else:

```fin
self.values[self.get_index(key)] = value;   // stdlib/hashmap.fin:39
// error: Type mismatch: expected 'int', got 'T'
```

`get_index` returns `int`. `T` is the type of `key`, its last argument. Two of the corpus's own diagnostics were
this, in two different samples, and neither had been recognised as one bug -- `letssee.fin` carried
`expected '&Vec2<float>', got 'float'` for `c.scale(2.0)` and `expected '&Vec2<float>', got 'Vec2<float>'` for
`a.add(*b)`, both the last argument, both looking like plausible unrelated `Self` problems.

Ten tests were written before any of it was implemented, and eight of them failed. The two that passed are
worth naming, because "write the test first" is usually justified by the failures: `AMethodParameterAnnotation-
IsStillReportedOnce` and `AMethodWithAnUnresolvedParameterKeepsItsWrittenArity` were predictions that the new
check would not *introduce* a duplicate diagnostic or lose a parameter, and they were green in the RED baseline
because there was no check yet. They are the tests that would have caught the implementation going wrong in the
two ways it nearly did.

**The signature had to be built somewhere, and the somewhere already existed six times.** Struct pass 1, class
pass 1, the implements block, and their three interface counterparts each resolved a method's parameters and
return type with slightly different code and slightly different bugs. `buildMethodSignature` is now the one
copy: open a scope, declare the method's own generics, resolve each parameter, skip a written `self`, resolve
the return type, close the scope. The implements block's hand-rolled version -- deleted as dead in the previous
unit because `StructType::methods` had nowhere to put a parameter list -- is that call now, which is what the
booking predicted.

**Skipping a written `self` is a corpus reading, not a convenience.** `struct_methods.fin` writes both
spellings in one struct: `fun area(self: &Self) <int>` and `fun describe() <noret>`, called the same way. The
receiver is injected whether or not it is written, so a signature that counted it would make one of those two
spellings wrong about its own arity. The stored `FunctionType` therefore excludes the receiver, and
`AWrittenSelfParameterIsNotAnArgument` asserts both spellings accept `s.m(1)` and reject `s.m()`.

**Resolving parameters in the signature pass tripled a diagnostic, and the fix was a rule, not a flag.** The
pass runs before bodies and resolves the same `TypeNode`s the body pass resolves, so `fun m(a: NoSuchType)`
went from two `Undefined type` diagnostics to three. `SemanticAnalyzer::QuietPass` makes the signature pass
report nothing -- and the legality condition is narrow enough to write down:

> A pre-pass may be quiet only when a later pass repeats every resolution it performed and reports.

The interface is the case that fixes the boundary and it is *not* quiet: an interface method has no body, so
nothing runs after its signature pass, so its signature pass is the only chance to report. Counting what the
quiet pass covers is also what found a separate bug: `visit(InterfaceDeclaration&)` opened a scope per method
and never called `declareGenericParams` for it -- the operator loop three lines below did -- so
`pub fun m<T>(a: T) <T>;` reported `Undefined type 'T'` twice, about a parameter it declares one token earlier.
`Soundness_GenericConstraints.AMethodInAnInterfaceDeclaresItsOwnGenerics` covers it, with a second case
asserting one method's `T` does not leak into the next.

Quieting the signature pass also turned `KnownDefect_ErrorRecovery.AnAnnotationInAStructSignatureIsReportedOnce-
PerPass` green -- five cases of it, including the class copies and an operator return type nobody had booked --
and it turned a *second* `KnownDefect` green by accident, which is the interesting one.

### Suppression is not a fix, and green is not proof

`KnownDefect_Generics.AnOperatorsOwnGenericParameterIsNotInScopeInPassOne` went green with the quiet pass. It
should not have. The defect it books is that the signature pass resolves an operator's return type without the
operator's own generic parameters in scope, so `operator + : <T>(other: <T>) <T>` reported `Undefined type 'T'`
*and registered the operator returning the error sentinel*. `QuietPass` removed the diagnostic. The wrong
registration stayed. Loud wrongness became silent wrongness and a test said "fixed".

Two things followed. The fix, which is the three lines the `KnownDefect`'s own comment had specified and
deferred -- a scope, `declareGenericParams`, resolve -- deferred until something collapsed the identical hole in
the method loop, which `buildMethodSignature` now is. And a test that a quiet pass cannot fake:

```
Soundness_Generics.AnOperatorsRegisteredReturnTypeIsWhatItsCallIsTyped
```

The sentinel is *silent by construction* -- `checkType` stops before it compares -- so a wrong registration
makes programs compile clean, and every assertion of the form "this diagnostic appears" is satisfiable by
registering nothing at all. The test asserts against a program that must report: `let r <string> = s - 1` on an
operator returning `int` is one mismatch, and on the generic operator the assertion is that *something real*
was registered (`errorCount == 1` and no `<error>` in the output) rather than which type. That second half is
what makes dropping the registration observable.

**And then the deletion.** The `KnownDefect` ended with an instruction: check whether `visit(Operator-
Declaration&)`'s second `defineOperator` is now redundant, and delete it if so. It is. All four containers --
struct, class, interface, implements block -- now declare the operator's generics before resolving its return
type, `Self` is defined before pass 1 in each, and every path that reaches `visit(OperatorDeclaration&)` comes
through one of them, so the second write could only repeat the first. That argument is a claim, so it was
measured: `D-opdecl` restores the deleted line and kills 0 of 128 tests. Zero kills is the result being sought
here, not a hole -- it is what "redundant" means, in the same matrix that treats zero kills as a finding
everywhere else. The difference is which direction the mutant runs: `D-opdecl` *adds* code back, and code that
adds nothing observable is code that was adding nothing.

The pair `R-opret`/`R-opdecl` had each killed nothing in the previous unit, and the resolution recorded there
was "two mechanisms that are not redundant, keep both." That was right then and is wrong now: what made the
second registration load-bearing was the first one being broken. Fix the first and the pair collapses. Dropping
the signature pass's registration (`D-opreg`) now kills five tests including a corpus-wide one, and dropping
just its generics (`D-opgen`) kills two. The mutant that measured nothing became two mutants that measure.

### The corpus moved, and the direction had to be checked

Total corpus diagnostics went 233 -> 231, and a count going down is not by itself good news -- a check that
stopped running looks exactly like a false positive that stopped firing. Diffing the snapshots per sample:
`letssee.fin` 4 -> 3 and `stdlib/hashmap.fin` 18 -> 17. Reading the actual text rather than the counts, three
false positives went away -- all three the last-argument bug -- and one new diagnostic appeared:

```
letssee.fin:73:21  error: Type mismatch: expected '&Self', got '&Vec2<float>'
```

`Vec2::normalize(scaled)`. A static call resolves `Vec2` by name, gets the uninstantiated template whose `Self`
is still `Self`, and now that arguments are checked, the mismatch shows up in argument position for the first
time. It is the same root cause as the two static *return types* the sample already books, so the finding is one
defect with a new symptom rather than a regression, and it is booked with the method call as its control --
`g.m(g)` and `g.r()` on the same generic struct are both clean, because a method call substitutes through the
instance receiver. `KnownDefect_MethodCalls.AStaticCallOnAGenericStructIsCheckedAgainstTheTemplate` holds all
of it, and `letssee.fin`'s `//@ unimplemented` note now says which three of its diagnostics are that defect and
that every method call in the file types correctly.

The generic hole underneath is the same one as `KnownDefect_Generics.AnOperatorsGenericParameterIsNotInferred-
FromItsOperand`, found by the same probe: `s + 1` on `operator + : <T>(other: <T>) <T>` reports
`expected 'string', got 'T'` -- a type name that exists only inside the operator's own declaration. There is no
inference from arguments to generic parameters anywhere yet, on any of the three call paths, and that is one
unification pass with its own tests rather than a patch in this unit.

### Mutation matrix: the method call

Twenty-three mutants over five files. The (a) half removes the check the unit added -- the arity comparison, the
argument-type comparison, the return type that replaced the last argument, the parents walk -- and a named test
has to kill each. The (b) half pushes a check past what it should accept, and here that half found the corpus:
`A-vararg` applies the arity check to a vararg function too, and it kills four *samples* and no unit test, which
is the corpus doing the job unit tests cannot. `@define printf(fmt: string, ...) <noret>` is in almost every
sample, so "arity is checked" and "arity is checked on varargs" differ by four files and by nothing else.

| mutant | kills | |
|---|---|---|
| `T-mcall` a method call is typed as its last argument again | 9 | |
| `S-notfn` the check is skipped when the stored type is a `FunctionType` | 9 | |
| `A-none` no arity check at all | 8 | |
| `A-toomany` the arity check ignores too many arguments | 8 | |
| `A-alloptional` every parameter is optional | 7 | |
| `D-opreg` the struct signature pass does not register its operators | 5 | |
| `T-fncall` a free call is typed as its last argument again | 5 | |
| `Q-loud` the signature pass reports again | 5 | |
| `S-self` a written `self` parameter counts as an argument | 4 | |
| `A-vararg` the arity check applies to varargs too | 4 | kills four samples, no unit test |
| `A-kind` the diagnostic calls every callee a Function | 3 | |
| `T-static` a static call is typed as its last argument again | 3 | |
| `Q-iface` the interface signature pass goes quiet as well | 3 | |
| `A-toofew` the arity check ignores too few arguments | 2 | |
| `A-argtype` arguments walked but not checked against the parameters | 2 | |
| `D-opgen` the signature pass resolves operator return types without its generics | 2 | |
| `A-nullreq` a nullable parameter is required like any other | 1 | |
| `S-generics` the signature pass drops the method's own generics | 1 | |
| `S-inherit` `getMethodType` does not walk the parents | 1 | |
| `D-opdecl` the deleted pass-2 `defineOperator` is put back | **0** | claim confirmed |
| `Q-haserror` quiet but still sets `hasError` | **0** | unkillable by construction |
| `Q-nesting` `QuietPass` sets a flag instead of counting | **0** | consumer does not exist yet |
| `S-retvoid` a method with no written return type gets the sentinel, not `void` | **0** | dead by grammar |

Nineteen of twenty-three killed something, and all nine `*_MethodCalls` tests are killed by some mutant. The four
zero-kill mutants are the section, and they resolve four different ways -- which is the argument for running the
matrix at all, because "0 kills" read as one thing would have produced one wrong action four times.

*A mutant can be a claim.* `D-opdecl` is the only mutant in any matrix so far that *adds* code: it puts back the
`defineOperator` call this unit deleted from `visit(OperatorDeclaration&)`. Zero kills is the result being
sought. "Redundant" is what a second registration is when removing it changes no test and putting it back
changes no test either, and that is a measurement rather than an argument. The path analysis said the same thing
-- all four containers (struct, class, interface, implements block) pre-register their operators in a signature
pass, `Self` is defined before PASS 1, and every operator body comes through one of the four -- but the path
analysis is what I would have written if I were wrong, and it had already been wrong once in this unit.

*The pair collapsed.* The previous matrix booked `R-opret` and `R-opdecl` as a zero-kill pair with the reading
"either registration alone suffices". That was true and it was not the whole truth: what made the second
registration load-bearing was the *first one being broken*. With pass 1's operator generics fixed, `D-opreg`
(drop pass 1's registration) kills five and `D-opgen` (resolve its return types without the generics) kills two.
A zero-kill pair can be two mutants covering for each other's defect, and the way to find out is to fix one and
re-run.

*Unkillable by construction.* `Q-haserror` makes the quiet pass suppress the diagnostic and still set
`hasError`, which is the shape of the worst bug in a compiler: exit 1 with nothing printed. No test kills it,
and the reason is the legality rule the quiet pass is allowed to exist under -- *a pre-pass may be quiet only
when a later pass repeats every resolution it performed and reports*. All three quiet sites satisfy it, so
`hasError` cannot diverge from the printed output: whatever pass 1 swallowed, pass 2 says out loud. The test that
would kill this mutant therefore cannot be written against method calls at all; it has to assert the rule
itself, as one global invariant -- **exit 1 if and only if at least one diagnostic was printed** -- over every
program in the corpus and every rejecting test in the suite. `tests/test_cli.cpp` has
`MachineContract.ExitOneOnADiagnostic` for the forward half on one program; the biconditional over the whole
corpus is owed, and it is what makes the legality rule enforced rather than merely stated.

*The consumer does not exist yet.* `Q-nesting` turns the `quietDepth` counter into a flag, so an inner
`QuietPass` closing would un-quiet the outer one. Nothing kills it because nothing nests: `buildMethodSignature`
opens no quiet pass, and implements blocks are top-level. The counter stays a counter anyway, and the reason is
the failure mode it guards -- a flag's failure is a *lost* diagnostic on the outer pass's remaining members,
which is the one failure a diagnostic-counting test cannot see. The mutant stays in the matrix as the tripwire:
the day a second quiet site nests inside the first, `Q-nesting` starts killing, and if it does not, the coverage
hole is real.

*Dead by grammar.* `S-retvoid` gives a method with no written return type the error sentinel instead of `void`.
Nothing kills it because no program can reach the branch: every function production in `parser.y` requires
`LT type GT`, so `FunctionDeclaration::return_type` is never null. Measured rather than argued -- replacing the
branch body with `std::abort()` and running the suite gives 456/456, and that suite contains all fifty samples.
The branch stays (deleting it would turn an impossible null into a *silent non-registration*, which is worse
than resolving to void), and what the mutant actually bought was the next finding: checking whether the
operator loop's identical `else` was equally dead turned up `parser.y:939`, the
`operator[] implements cast<fn(Self, T)>(__get)` form, where `return_type` genuinely is null and which
`stdlib/hashmap.fin:50` uses. Probing what that registers found three layers of the same hole --
`operator []` is registered and never consulted by `visit(ArrayAccess)`, a value receiver is reported as "not an
array or pointer" despite having one, the index is checked against `int` before the receiver is classified, and
`OperatorDeclaration::implements_expr` (which carries that form's whole signature) is written by the parser and
read by nobody. `KnownDefect_IndexOperator.AnIndexExpressionNeverConsultsOperatorBracket` books all of it with
an array control. A zero-kill mutant on dead code still paid for itself, one file over.

*And one test was unkillable, which is worse than a missing test.* Fixing pass 1's operator generics, I inverted
the `KnownDefect` that booked it into `Soundness_Generics.AnOperatorsOwnGenericParameterIsInScopeInPassOne` --
declare the operator, assert no diagnostic -- and it went green. `D-opgen` reverts exactly that fix and the test
stayed green, because the operator loop is *inside* the `QuietPass` block: the quiet pass removes the
diagnostic, so `errorCount == 0` holds whether the scope is right or not. The test asserted nothing and its name
claimed a guarantee. Two general reasons a "this compiles clean" assertion can be blind, and both apply
here: a quiet pass removes the diagnostic, and the error sentinel is **silent by construction** -- `checkType`
returns before comparing when either side is `<error>`, so "no diagnostic" is satisfied by registering nothing
at all. The fix is to assert the *registration* rather than the silence: call the operator and read the type of
the call. `Soundness_Generics.AnOperatorsRegisteredReturnTypeIsWhatItsCallIsTyped` does that in four cases, and
it is what kills `D-opgen` and `D-opreg` both. A name that overstates what a test verifies is worse than no
test, because it retires the suspicion that would have found the gap.

## Rulings owed

Every entry below is a question only the language owner can answer, discovered by measurement and blocking
at least one fix. They are collected here because they are scattered across the sections that found them and
because the cost of answering them rises with time: several are load-bearing for `lib/std`, which does not
exist yet and which will be written against whatever the answers turn out to be. Each names what is blocked,
so an answer converts directly into work rather than into more discussion.

**Integers.** Is `-1` a legal unsigned constant by C wraparound, or must a maximum be spelled explicitly?
This one now blocks two things and the evidence is exact. `stdlib/stdio.fin:87` and `:109` both write
`fun read(nbytes: ulong = -1)` -- a parameter default -- and `:110` compares `nbytes == -1`. A parameter
default is now *visited* (see below), but it is deliberately not *type-checked*, because the check would put
`Type mismatch: expected 'ulong', got 'int'` on two lines of a normative sample. `KnownDefect_ParameterDefaults`
holds the gap open with a test naming this ruling as the blocker. Does an
`int`-typed *expression* convert to unsigned implicitly as in C, or require a cast as in Rust and Zig?
(Five `int` <- `ulong` diagnostics in the corpus.) What are the widths of `int`, `long`, `short` and `char`,
and is `char` signed? (Nothing can range-check a literal until these are fixed, and the answer is ABI, so it
cannot be changed later without breaking every compiled artifact.)

**~~The type system's three unknowns.~~ Answered by the corpus** -- see "`any` was not a ruling, it was a
reading" above. `any` and `object` are builtins, because five files use them and import nothing; they are
distinct, because the corpus writes `any` where it casts on the way out and `object` where a value is boxed
at runtime; `Any` is a library alias behind `#[export]` and stays undefined without an import. What is left
of this entry is one question, and it is narrower than any of the three: **does a primitive satisfy an
`implements` bound?** `type EnumType = any implements <Enum>;` (`stdlib/enums.fin:6`) reads as a constraint,
and `KnownDefect_DynamicTypes.AnImplementsBoundOnADynamicTypeIsNotEnforced` holds the gap open. It cannot be
guessed, because `stdlib/types.fin:78` writes `type nullptr = any implements <&void>;`, whose bound is a
pointer type and not an interface, and `stdlib/typing.fin:10` writes one inside a union -- a check that only
understood interfaces would reject two lines the standard library writes. A second, smaller question rides
along: does a written `any<int>` narrow, or are the arguments decoration?
`KnownDefect_DynamicTypes.GenericArgumentsOnADynamicTypeAreNotConstraints` books it, and the corpus points
at "decoration", since the only spelling it writes is `Any<...>` with a literal ellipsis, which cannot be a
type.

**Erasure or a box: is `object` assignable to `any`?** The corpus settles that the two are distinct and says
what each *is*. `stdlib/types.fin:97` calls `any` "any type that is visible in compile time", which is
erasure of something the compiler still knows. `prototype_test.fin:40` calls `object` "an expensive type but
can fit any datatype in it at the cost of memory and speed", and a cost paid at run time is a box with a tag.
Read that way the two directions are not symmetric: a value the compiler knows the type of can always be
boxed, so `any -> object` should be legal, while unboxing is exactly what a *cast* is for, so `object -> any`
should not be. What the tree does today is accept both, because the rule "every type fits a dynamic target"
in `Type::isAssignableTo` answers before anything looks at which dynamic type it is.

The question blocks a real change and is not academic. `DynamicType::equals` already compares names and
nothing can observe that it does -- the mutation matrix above shows it, and the only channel that could have
shown it, the cast diagnostic, is one the corpus contradicts. Until this is answered, `equals` cannot be
tested and cannot be safely deleted either: deleting it makes `any` and `object` one type, which is a
stronger claim than the corpus supports. `KnownDefect_DynamicTypes.ObjectAndAnyAreMutuallyAssignable` asserts
today's behaviour and names this ruling, so an answer turns straight into a failing test.

**Modules.** What does a `namespace` block do to a module's symbol table, and is importing a namespace the
module does not declare an error or a no-op? (Twelve stdlib samples open with `namespace std`; eleven corpus
imports name `::std`; today the block has no effect and the tail is discarded.) Is `pub` required to export
— and if so, is `structs.fin:3` wrong, or are quoted-path file imports exempt from visibility while library
imports are gated, or is `pub` advisory? (`#[export]` on a `%{ ... }%` block is a third spelling of the same
intent and must be answered in the same breath.) Should a named import or `import *` carry macros? Does a
quoted import mean one thing or two — a path relative to the importing file, or to the working directory?
(The corpus documents both meanings.)

**Generic bounds.** Does a primitive satisfy an interface bound? `checkConstraint` now runs -- the bound is
stored on the `GenericType` at last -- and reports only when the argument is itself a struct, so `S<int>`
still satisfies `<T: I>`. `Castable` and `Any` are erasure markers (ADR 0018) spelled as bounds that every
primitive must satisfy, so rejecting every non-struct argument is not obviously the answer. Three lines
behind one ruling.

**Syntax not yet settled.** Is the `@macro name { (pattern) => { body } }` form final?
`macro_definitions.fin:8` says of it "NOT DECIDED YET", and it does not parse in any spelling tried, so no
macro can be declared at all and the macro-import question above cannot even be measured. In
`preprocessor.fin:23`, which operand order does the ternary `cond : then ? else` take under nesting?

*Struck:* what `fun?` means. `nullifier.fin:23` answers it — "Automatically returns null even without an
else statement" — and `undefined_behavior.fin:9` corroborates with "this function compiles". Both are
implemented; see the nullability section above.

**Nullability's four remaining edges.** Each was found by implementing the rest of the feature and each is
one or two lines behind an answer. (1) What does a postfix `?` on a value that was never nullable mean?
`nullifier.fin:36` says `mibombo?` on an `any` "should be an error", but `any` is the unresolved type above,
so the sample cannot be read as settling the general case; today the operator is the identity there, which
accepts a typo silently. (2) Is `nullifier.fin:39`'s `let _? <int> = make_A()` — a call returning `<A>` — a
slip for `<A>`, or does `_` suppress the check? It is the file's last diagnostic, and the line is about the
*omitted argument*, so the type looks incidental to what it is demonstrating. (3) May a parameter be
assigned inside the body? `stdlib/error.fin:13` writes `err_code = -1;` and finc answers "Cannot assign to
immutable variable 'err_code'"; a normative sample and the compiler disagree, so one of them is wrong.
(4) A parameter's default value is never checked against the parameter's type at all
(`visit(Parameter&)` resolves the type and visits the default without comparing them), so
`fun g(n: string = 3)` is accepted. That is a defect rather than a ruling, but it shares a site with (3) and
is worth fixing in the same visit; it is why `ANullDefaultOnAParameterIsAccepted` passed before this wave
started.

**Diagnostics.** Is there a legal third diagnostic shape beyond error and warning — a note, or a remark
attached to a parent? Two sites want one, and the JSON contract in ADR 0009 fixes the shape, so the answer
has to precede `finn` consuming it.
