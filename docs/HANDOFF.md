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

1. ~~**Generic methods**~~ — **done.** Instantiated at the call site, both substitutions
   composed, keyed `Struct<args>.method<margs>`, `linkonce_odr`. See below and the commit.
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

- **The representation of a dynamic `[T]`** — *blocks `arrays_enums.fin`.*
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
- **What the no-backend diagnostic should name as the required LLVM** — *not blocking.*
  `src/codegen/CodeGen_Stub.cpp:30` prints `= help: configure with -DFIN_WITH_LLVM=ON and an
  LLVM 18 development install` — a string compiled into `finc` and shown to users, still naming 18
  after the pin moved to 22, so it sends a reader to install the one version `CMakeLists.txt` will
  then reject with a `FATAL_ERROR`. Deliberately **not** fixed with the `conanfile.py` ruling of
  2026-08-27 (§7): that one turned on the LLVM-18 text there being comments and not a build input,
  and this text is the other side of that line. What is owed is not the number but the mechanism —
  spell `22` in the stub, or have CMake pass `FIN_LLVM_MAJOR` in as a compile definition. The stub
  is compiled exactly when `FIN_WITH_LLVM=OFF`, and on that path `find_package(LLVM)` never runs,
  so nothing has checked that `FIN_LLVM_MAJOR` means anything at all. No test asserts the string
  (`grep "development install" tests/` is empty), so nothing currently holds it to either answer.
- **Whether the checker-only build is required to pass its suite** — *not blocking.*
  ADR 0010 keeps `FIN_WITH_LLVM=OFF` so a platform that cannot get LLVM still gets a checker, but
  measured on 2026-08-27 that configuration is **not** green: `./build.sh --release --no-llvm` gives
  1257 tests, 961 passed, 295 skipped, **1 failed** — `MachineContract
  .DashOProducesTheNamedExecutable` (`tests/test_cli.cpp:182`), which asserts `-o` writes an
  executable and is the one backend-dependent test not behind `BACKEND_TEST`. ctest then exits 8.
  Either that test belongs behind the macro (and the checker-only build is a supported, green
  configuration), or the OFF path is a build nobody is meant to run the suite on and should say so.
  Left as measured, because fixing it silently would remove the only thing that currently turns a
  backend-off CI job red — and note that this failure is why the "no codegen test went quiet" step
  in `ci.yml` carries `if: ${{ !cancelled() }}`: without it the step never runs on the very build it
  exists to diagnose.
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
