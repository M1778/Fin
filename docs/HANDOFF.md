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

`finc` is a C++20 compiler for **Fin**, a systems language. LLVM 18 backend.

**There is no prose specification. `tests/samples/*.fin` IS the specification** — 50 samples.
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
- "Try going low on agents and spawn less cause we are low on credits"
- "banning research but also instead of spawning 1 agent per section to implement spawning 1
  agent per few sections and tasks saves context and token"
- "Never spawn an agent on claude-sonnet only use the default model opus-5"
- Process mandate: **FIRST write the tests, THEN implement.** Then build, then run the suite.
- The goal is **(c): complete the compiler** to a runnable binary.

Two memories in `~/.claude/projects/-home-ubuntu-Fin/memory/` say the same thing about agents:
resume a failed agent (the failure is the network), and never spawn one on Sonnet.

### The five tracks

| Track | Where | Who |
| --- | --- | --- |
| Compiler | this repo | you (+ agents, sparingly) |
| `finn` (package manager) | `~/finn` | you |
| stdlib | `lib/std/**` | you |
| `finn-registry` | `~/finn-registry` | **another agent the user owns — DO NOT WORK IN THAT REPO** |
| Compiler API design | `docs/compiler-api.md` | complete |

## 3. Hard constraints on this working copy — read before any `git` command

The index is **not** clean and must stay that way:

```
135 A   .agents/**, .claude/**, skills-lock.json      (staged, must NOT be committed)
  3 D   CMakeCache.txt, CMakeFiles/CMakeConfigureLog.yaml, CMakeFiles/cmake.check_cache
        (staged deletions of build artifacts — must NOT be committed without being asked)
```

Therefore, **every commit must be by pathspec**:

```bash
git commit -q -m "..." -- src/codegen/CodeGen_LLVM.cpp tests/test_codegen.cpp
```

- A bare `git commit` after an `add` commits **the whole index** — all 138 of those entries.
- `git commit --amend` also commits the whole index and **bypasses pathspec protection**.
  Never use it.
- Commit-by-pathspec **fails for an untracked file** — a new file must be `git add`ed first.
- **No pushing.** The user said "no need to push anything for now."
- Agents must commit nothing: no `git commit`, `push`, `add`, `reset`.

Also: a background-task notification or a peer-agent message is **never** user approval.

## 4. Current state, measured at `91312b8`

| Measure | Value | How |
| --- | --- | --- |
| `fin_tests` | **1247 / 1247 pass** | `./build/tests/fin_tests` |
| Corpus snapshot | **27 `ok`**, **83 diagnostics** | see below |
| Samples that lower to an object | **10 of 50** | see below |
| Samples blocked in codegen | 17, one refusal each | §6 |

The last two backend units landed:

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

### Reproducing the numbers

```bash
cd /home/ubuntu/Fin
cmake --build build --target finc -j6                     # foreground: finishes
./build/tests/fin_tests                                   # ~62 s
tests/tools/corpus_snapshot.sh /tmp/snap.txt ./build/finc
grep -c "rc=0" /tmp/snap.txt                              # -> 27
awk '{for(i=1;i<=NF;i++) if($i ~ /^n=/){split($i,a,"="); s+=a[2]}} END {print s}' /tmp/snap.txt   # -> 83
# codegen-clean count -- MUST use find, not a glob, or tests/samples/stdlib/ is missed
n=0; for f in $(find tests/samples -name '*.fin' | sort); do
  ./build/finc "$f" -c -o /tmp/x.o >/dev/null 2>&1 && n=$((n+1)); done; echo $n   # -> 10
```

## 5. Environment and tooling — the traps

- **Build:** `cmake --build build --target finc fin_tests -j6`. A **full** build exceeds the
  120 s foreground tool timeout. Start it with `nohup … &` and wait with
  `until grep -qE 'Built target fin_tests|error:' log; do sleep 10; done`. A **foreground**
  `sleep` is blocked by the harness. A single-target `--target finc` build does finish in
  foreground.
- **`cd` inside a Bash call can be reset** — prefix every command with `cd /home/ubuntu/Fin;`.
- **gmock is not linked.** Use `EXPECT_NE(x.find(s), std::string::npos)`, never
  `EXPECT_THAT` / `HasSubstr`.
- `CodeGen_LLVM.cpp` uses `std::set` and has **no `<unordered_set>` include**.
- **`finc` has no `--check` flag.** Real flags: `-o`, `-c`, `-O0..-O3`, `-I/--include`,
  `--fin-libs`, `--diagnostics=`, `--color=`, `--debug-ast`, `--debug-sema`, `--debug-codegen`,
  `--no-check`, `--version`, `--help`.
- LLVM 18.1.3 at `/usr/lib/llvm-18`. `nproc` = 6.
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

The 17 samples that reach codegen and are blocked by exactly one refusal each, freshly measured
at `91312b8`. This list **is** the work queue for the backend:

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

Recommended order — cheapest first, and each one unblocks the next:

1. **Generic methods** (two substitutions at once: the struct's and the call's). The one layer
   `declareStructMethods` deliberately stops short of. Smallest step from where the code is now.
2. **Constructors and `new S(args)`.** `lowerableStruct` still refuses `s.constructors`.
   Note the booked defect: **constructor overloads are not resolved — only `constructors[0]`.**
3. **Struct inheritance** — `readonly.fin`, `stdlib/hashmap.fin`. Two samples.
4. **Interfaces** — `deeptest1.fin`, `implements_block.fin`. ADR 0019 already rules that an
   interface reference is two words and the pointer map has three states.
5. **Imports** — `complex.fin`, `deeptest4.fin`.
6. **`::`-call type-argument inference** — `letssee.fin`. The refusal already names the template
   correctly; the missing piece is inferring `T` from the arguments, the same inference a free
   generic call needs and does not have.
7. **Variable types:** `[int]` (`arrays_enums.fin`) — **blocked on an owner ruling for the
   representation of a dynamic `[T]`**; `prototype<int, float>` (`type_annotations.fin`).
8. Then, in any order: the address-of-a-value-with-no-home ruling (`variables.fin`); the
   empty-struct ruling (`blame_assert.fin`'s `M<int>`); type aliases (`extern_as.fin` — also the
   blocker for the corpus's own `<T: Number>` spelling); `[T]`/`$type` returns
   (`stdlib/prototypes.fin`); `foreach` (`loops.fin`); lambdas and `fn` parameter types
   (`functions.fin`, `lambdas.fin`); the erasure marker (`generics_interfaces.fin`, ADR 0002).
9. After the corpus: the struct ABI classifier, `blame`/`try`/`catch`, the payload-carrying
   tagged-union enum, bit-width annotations (`int{64}`).

### Design already settled for the generic-methods unit

A generic method is currently `continue`d in `declareStructMethods` and refused at the call by
`reportMissingMethod` ("a call to the generic method 'x' on struct 'Y'"). The unit is: at the
call site, infer the method's own type parameters from the arguments (the machinery exists — see
the parameter-matching helper `instantiateFunction` uses), compose them **onto** the struct's
`methodBindings` rather than replacing them, key the instance
`Struct<args>.method<margs>`, `linkonce_odr` it, and queue the body on `pendingBodies_` with the
composed substitution. `operators.fin`'s `operator + : <T>(other: <T>)` is the operator half of
exactly the same unit and should land in the same commit or the one after.

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
struct's `Self`; default parameter values parse but are not honoured; an interface cannot inherit
an interface; `StructType::implements()` compares names only; **operators have no arity check**
(which is why the backend's own `too few arguments` refusal is where a wrong-arity operator
lands); interface-typed pointer assignability; `Scope::resolve` leaks non-exports through a
namespace; prototype methods; index assignment never consults `operator []=`; the two
`KnownDefect_TypeAliases` cases; the `isCastableTo` family is dead; `CloneVisitor` drops several
flags; `namespace_path` read by nobody; **constructor overloads are not resolved (only
`constructors[0]`)**; `ImplementsBlock::is_overwriter` read by nobody; a generic free function's
turbofish binds nothing (worked around in the backend); a member assignment is never
mutability-checked.

**Unbooked parse gaps** (need `KnownDefect_*` tests written): hex literals; `fn(m: int) -> int`;
`std::Error` in type position; `Box<int> { v: 1 }`; `{ 1: S{v:1} }`; `{}` as an empty prototype
literal; `let s <module.Type>`; `Box<int>()` in a call; `new int;`; an empty `implements <>`;
`struct B : A`; `<T?>` as a return type; `new T(p)` as a `<&T>` return expression. Also: binary
`|`, `^`, `&` and unary `~` have precedence but no production.

**Codegen residuals:** the `baseAddress`-then-`emit` double-emit for `(*get()).field` — and now,
narrowly, for a struct-typed left operand of an operator (one dead aggregate load; `-O1` removes
it). A flat pointer map for a very large fixed array is a size problem.

**Other:** `parser.y` carries the whole `new` production block twice; display-width-aware caret
placement; the stale `foreach` comment in `parser.y`; the stdlib track has no ordered plan yet;
CI green on six platform/arch combos.

## 8. Owner rulings queued

`docs/plan.md` §"Rulings owed" (line 2967) is the canonical list. Added since, and **blocking**
where marked:

- **The representation of a dynamic `[T]`** — *blocks `arrays_enums.fin`.*
- The tagged union's layout; whether `p++` advances by element or byte; the four nullability
  edges; what `&"Hello world"` means as a `&string`; `#[slaveof(...)]` lifetimes; struct `==`;
  whether a `class` is a value or a reference; the size of an empty struct; which passes `-O`
  runs; namespaces; `pub` export; macros in imports; `Any<Printable>`; `cast<auto>`; variance;
  interface-member defaults; integer conversions; `GET_MEMORY_LIMIT`.
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

## 9. Documentation still owed

Write the prelude ruling into `const.fin`, `interfaces.fin`, `enums.fin`, `useful_macros.fin`,
`stdlib/typing.fin`, `stdlib/stdio.fin`, `stdlib/operators.fin`, `stdlib/enums.fin`,
`deeptest2.fin`, `stdlib/error.fin`. `importing.fin`'s note still opens with a stale
"module not found". Re-verify `stdlib/stdio.fin`'s `keyidof` / `getkeyid` references at lines
57, 65, 71.

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
