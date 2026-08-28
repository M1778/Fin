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

## 4. Current state, measured at `91721d7` (2026-08-28)

| Measure | Value | How |
| --- | --- | --- |
| `fin_tests`, `FIN_WITH_LLVM=ON` | **1344 / 1344 pass**, 0 skipped | `./build/tests/fin_tests` |
| `fin_tests`, `FIN_WITH_LLVM=OFF` | **991 pass / 346 skip / 0 fail** | a second build dir |
| Samples that lower to an object | **14 of 50** | see below |
| Samples blocked in codegen | **15** | see below |
| Samples that never reach codegen | **21** | see below |

The ceiling is **49**, not 50: one sample is a negative test that must keep failing.

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

**That list is first refusals only, and a first refusal is not a count.** `finc -c` stops
where it stops, so a sample with one line here may have two rulings behind it. Demonstrated
twice: `variables.fin`'s single `&"..."` refusal hid a second, `#[slaveof(x)]` on a local
(`:27`, `:35`), which appeared only when the first was lowered; and a prediction of "10 → 14
from three features" delivered 10 → 11 for the same reason. Measure after landing, never before.

Recommended order — cheapest first, and each one unblocks the next:

1. ~~**Generic methods**~~ — **done.** Instantiated at the call site, both substitutions
   composed, keyed `Struct<args>.method<margs>`, `linkonce_odr`. See below and the commit.
2. **Constructors and `new S(args)`.** `lowerableStruct` still refuses `s.constructors`.
   Note the booked defect: **constructor overloads are not resolved — only `constructors[0]`.**
3. **Struct inheritance** — `readonly.fin`, `stdlib/hashmap.fin`. Two samples.
4. **Interfaces** — `deeptest1.fin`, `implements_block.fin`. ADR 0019 already rules that an
   interface reference is two words and the pointer map has three states.
5. **Imports** — `complex.fin`, `deeptest4.fin`. **Measured 2026-08-28, and the fix is not in
   codegen.** `complex.fin:14` writes `stdio.printf("Big")` against `import stdio::std as stdio;`
   on `:3`, and the front end already resolves it correctly —
   `Soundness_Modules.AModuleFunctionIsCallableThroughADot` passes, and
   `Analyzer_Expr.cpp:1154`/`:1609` have the `NamespaceType` branch. What fails is codegen,
   which reports *"the receiver of a call to the method 'printf' on a value with no address"*
   because `visit(MethodCall&)` goes straight to `baseAddress(object, Kind::Struct)` and
   `stdio` is a module, not a struct.
   **Do not teach codegen about namespaces.** It has zero references to `NamespaceType` and
   `visit(ImportModule&)` refuses any import that reaches it at all — the backend deals in
   symbols, and that is the design. The analyzer should **rewrite** a namespace-qualified
   `MethodCall` into a plain `FunctionCall` on the symbol it already resolved, so the qualifier
   never reaches the backend. The precedent for erasing module machinery in the front end is
   `dropConsumedImports` (`Analyzer_Core.cpp`), which deletes spent imports for the same reason
   and explains itself in those terms.
   One thing to look at before starting: `complex.fin` declares `@define printf(fmt: string,
   ...) <int>;` on `:5` **and** imports a `stdio` that exports `printf`, and calls the plain one
   on `:16`. Two `printf`s in one file is the `#[overwrite]` question in miniature, and the
   rewrite must not silently pick one.
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
  `stdlib/hashmap.fin` then needs a constructor on a struct. Parent *methods* are **not** ruled
  — report rather than invent.

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
