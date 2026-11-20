# Stage coverage: what the self-hosted compilers build, refuse, and get wrong

This file records the state of stage coverage over the fourteen corpus samples the
run-agreement harness (`tests/test_stage_agreement.cpp`) has to skip, and the three
genuine stage miscompiles found while closing that gap. It is a measurement, not a
plan: every row below was produced by running `build/finc`, `build/finc_stage2` and
`build/finc_stage3` on the sample and reading their exit codes, stdout and stderr.

The executable half of it lives in `tests/test_stage_coverage.cpp`
(`ctest -R 'StageCoverage|KnownDefect_StageCoverage'`).

## Why these samples were skipped, and why that was a harness limit

`tests/test_stage_agreement.cpp` compares the compilers by building every normative-ok
sample to an executable and running it. Thirteen of the fourteen corpus files in its
skip list declare **no `fun main`**, so both compilers refuse them identically with
`error: this program has no 'main'` — and the harness can only record that as a skip.
Nothing about the stage was being measured for those files.

They are libraries and function collections, and every one of them is legal `-c`
input. The fix is in the harness, not the language: append a `main` to the sample's own
text, build with all three compilers, and require the reference's fate, stdout and
stderr byte-for-byte on both stages. That is what `test_stage_coverage.cpp` does.

The fourteenth, `deeptest4.fin`, is the held ruling in ADR 0036 and is pinned rather
than driven.

## The table

`ref` / `stage2` / `stage3` are the verdict on a `-c` compile of the **sample file
alone**. "driven" marks the ones this suite now actually builds *and runs*.

| sample | ref | stage2 | stage3 | reason | owner |
| --- | --- | --- | --- | --- | --- |
| `deeptest3.fin` | ok | ok | ok | **(c)** no `main`; 7 `test_*_main` entry points run identically on all three | closed here |
| `extern_as.fin` | ok | ok | ok | **(c)** no `main`; namespace/enum/typedef paths driven. **The `extern <const> as <name>` alias line is a stage defect** — see D4 | closed here / D4 |
| `literal_interface.fin` | ok | ok | ok | **(c)** no `main`; the `@implements` half is driven. Its `pick_interface` returning `$interface` is a **reference-side** gap (`Type '$interface' is not a struct`), same on both | closed here (partial) |
| `macro_definitions.fin` | ok | ok | ok | **(c)** no `main`; the entire file is inside a `/* */` block, so driving it proves the block comment lexes the same | closed here |
| `macros.fin` | ok | ok | ok | **(c)** no `main`; `magic_add!(10, 20)` expands to `30` on all three | closed here |
| `macros2.fin` | ok | ok | ok | **(c)** no `main`; entirely commented out, like `macro_definitions` | closed here |
| `struct_methods.fin` | ok | ok | ok | **(c)** no `main`; methods, the generic method and both statics run identically. `Point<int>{...}` is refused by **both** compilers (reference parser gap) | closed here (partial) |
| `stdlib/error.fin` | ok | ok | ok | **(c)** no `main`; field layout and the member default are driven. Its constructor is **D1** and its `format()` body is **D2** | closed here (partial) / D1, D2 |
| `stdlib/hashmap.fin` | ok | ok | ok | **(c)** no `main` — but not drivable by *either* compiler: `__set` is refused by the reference (`an assignment to this target is not lowered yet`). Module-level parse/declaration driven only; instantiating `self.vals[i]` in a method is **D3** | closed here (module) / D3 |
| `stdlib/memory.fin` | ok | **refused** | **refused** | **(a)** stage backend: `codegen: the compile-time call '@GET_MEMORY_LIMIT' is not lowered yet` | refusal pin |
| `stdlib/outcome.fin` | ok | ok | ok | **(c)** no `main`; both `Ok`/`Err` arms and all four methods run identically | closed here |
| `stdlib/prototypes.fin` | ok | **refused** | **refused** | **(a)** stage backend: `prototype key buffers cannot escape through calls (ADR 0028)` | refusal pin |
| `stdlib/typing.fin` | ok | ok | ok | **(c)** no `main`; both `Result` arms construct identically. `cast<int>` on the payload enum is a **stage-lenience divergence** — see D5 | closed here / D5 |
| `deeptest4.fin` | **refused** | **refused** | **refused** | **(a)/(d)** held ruling: ADR 0036, struct `==` never synthesized. Reference: `an undeclared operator '==' on struct 'Data' is not lowered yet`; stage refuses earlier, at the generic `HashMap` constructor it cannot key | ADR 0036 / refusal pin |

Bucket counts (all fourteen samples, each counted once):

| bucket | count | samples |
| --- | --- | --- |
| (a) a construct the stage backend refuses | 3 | `stdlib/memory.fin`, `stdlib/prototypes.fin`, `deeptest4.fin` |
| (b) `lib/std` module not on the stage's search path | **0** | — |
| (c) harness limitation (no `main`, so `-o` skips) | 11 | everything else |
| (d) a stage miscompile inside the skip list | **0** | — |

`deeptest4.fin` sits in (a) with a second reason: the reference refuses it too, for the
held ruling (ADR 0036, struct `==` is declared-only), while the stage refuses it
earlier, at the generic `HashMap` constructor it cannot key. Two different named
refusals at two different places, so the pin checks both.

Bucket (b) is empty and that is a real finding: the stage resolves `package::std`
through the working directory, and every one of these samples builds from the repo
root with no `--fin-libs` flag. The only harness need was the working directory, which
`ScopedRepoRoot` in the agreement suite already handles.

Bucket (d) is empty *inside the skip list* because every skip-list refusal really is a
refusal. The genuine stage miscompiles are in the next section: they were found by
driving these samples, and they live in constructs the samples use rather than in the
samples' own top level — which is exactly why a compile-only verdict never saw them.

## What was converted

Eleven samples went from "skipped, unmeasured" to "built and run by all three
compilers, stdout and stderr compared byte-exact":

`deeptest3`, `extern_as`, `literal_interface`, `macro_definitions`, `macros`,
`macros2`, `struct_methods`, `stdlib/error`, `stdlib/hashmap` (module-level),
`stdlib/outcome`, `stdlib/typing`.

Four of those eleven are **partial**, and the tests say so in their comments rather
than pretending otherwise: `extern_as` does not drive its const-alias line (D4),
`literal_interface` does not drive `pick_interface` (a reference-side gap),
`struct_methods` does not drive `Point<int>{...}` (a reference parser gap), and
`stdlib/hashmap` drives the module, not the map.

Two samples got refusal pins instead of coverage, because the stage genuinely cannot
lower them yet: `stdlib/memory.fin` and `stdlib/prototypes.fin`. Each pin asserts the
refusal still happens, still exits nonzero, and still **names** the construct — the
backend invariant this repository states as non-negotiable. A regression to a silent
drop fails the pin.

## Genuine defects found

Five stage defects came out of this sweep. Three are silent miscompiles (the program
builds and prints the wrong thing), which is the worst class: the backend invariant is
that it must refuse instead. Two are refusals or lenience, and are pinned as such.

### D1 — a constructor whose body is `return new S{...}` is dropped on the stage

**Repro:** `/home/M1778/fin-tmp/err/c5.fin` (also pinned in
`KnownDefect_StageCoverage.AConstructorReturningNewIsDroppedOnTheStage`).

```
@define printf(fmt: string, ...) <noret>;
struct E {
    pub message <string>,
      pub code <int>,
      E(msg: string, c: int) { return new E{message: msg, code: c}; }
}
fun main() <noret> {
    let e <E> = E("boom", 42);
    printf("[%s] [%d]\n", e.message, e.code);
}
```

| compiler | output |
| --- | --- |
| `build/finc` | `[boom] [42]` |
| `build/finc_stage2` | `[(null)] [0]` |
| `build/finc_stage3` | `[(null)] [0]` |

The call site allocates a zeroed receiver box and passes its address as the implicit
`self`; the body evaluates the returned expression and then returns it out of a
function whose result the call site ignores, so every field reads back zero. The
reference implements the other half of the same convention: `CodeGen_LLVM.cpp`'s
`emitConstructedValue` stores the returned object through the receiver before the void
return. The stage has the receiver, the box and the returned expression — it is missing
the store into `self`.

Confirmed against a stage1→stage2→stage3 bootstrap of the current tree
(`build-stage/`, fixed point holds), so this is not stale-binary drift.

**Reach:** `lib/std/error.fin`'s constructor is this shape, so every `Error("...")` call
in the standard library returns an uninitialized struct on a stage. Driving the corpus
sample's constructor and its `format()` together prints `Error: 0` where the reference
prints `Error: boom`; the shipped `lib/std/error.fin` separately loses its `error_id`
and message, printing `(null)` and `0`.

### D2 — `format!` prints a string *value* as its pointer

**Repro:** `/home/M1778/fin-tmp/fmt/f1.fin` (pinned in
`KnownDefect_StageCoverage.FormatBangPrintsAStringValueAsItsPointer`).

```
let s <string> = "boom";
printf("[%s]\n", format!("Error: {}", s));   // ref: [Error: boom]   stage: [Error: 4202500]
```

The reference lowers `format!` directly and picks `%s` for a string value. The stage
expands `format!` in `finc/driver.fin` into a generated helper that calls `snprintf`
with a conversion chosen by `mx_fmt_conv`, which reads the argument's **source text**:
a literal gets `%s`, `&x` gets `%p`, a numeric literal gets `%d`, and *anything else
defaults to `%d`*. A named `string` local is "anything else", so its pointer is printed
as a decimal. The comment on `mx_fmt_conv` states the choice is deliberate as a
safe-direction default and that "full type direction needs checker integration"; the
consequence measured here is a silent wrong answer, not a refusal.

`format!("{}", "literal")` and `format!("{}", 42)` are both correct — only a
non-literal string argument is wrong.

### D3 — indexing a fixed-array field from inside a method is refused

**Repro:** `/home/M1778/fin-tmp/min2/f1.fin` (pinned in
`KnownDefect_StageCoverage.IndexingAFixedArrayFieldInsideAMethodIsRefused`).

```
struct S {
    pub vals <[int, 4]>,
      S() { self.vals = [1, 2, 3, 4]; }
    pub fun get(self: &Self, i: int) <int> { return self.vals[i]; }
}
```

| compiler | result |
| --- | --- |
| `build/finc` | `3` |
| `build/finc_stage2` / `stage3` | `error: Type mismatch: expected 'int', got 'int,4'` |

The stage's index lowering reads the array annotation `[int, 4]` and hands the whole
string to the element-type position instead of splitting off the extent. The same index
on a **local** fixed array (`a[i]`) and on a field read from **outside** a method
(`s.vals[i]`) both lower correctly on the stage, which is what localises it.

This is a refusal, so it does not violate the silent-drop invariant — but it is
stage-only and it blocks `stdlib/hashmap.fin`'s entire mutating half, which is why the
hash map row above can only be driven at module level.

### D4 — `extern <const> as <name>` does not resolve on the stage

**Repro:** `/home/M1778/fin-tmp/alias_min.fin` (pinned in
`StageCoverageRefusal.ExternConstAliasIsRefusedByName`).

```
const myglobv <int> = 10;
extern myglobv as myglobv_diffname;
fun main() <noret> { printf("%d %d\n", myglobv, myglobv_diffname); }
```

`build/finc` prints `10 10`. Both stages report `use of undeclared identifier` at the
alias use site — a refusal, so no wrong code runs, but the diagnostic names nothing and
the `extern_as.fin` construct it exists to demonstrate is unimplemented. The pin
asserts the current refusal so that implementing the alias flips it.

### D5 — `cast<int>` on a payload enum: the stage accepts what the reference refuses

**Repro:** `/home/M1778/fin-tmp/ec/a.fin` (pinned in
`KnownDefect_StageCoverage.CastingAPayloadEnumToIntIsRefusedOnlyByTheReference`).

```
enum Payload { Empty, Holds(int) }
let p <Payload> = Payload::Holds(7);
printf("%d\n", cast<int>(p));     // ref: refuses. stage: prints 1
```

The reference refuses with `this conversion (from 'a struct' to 'an integer') is not
lowered yet`. The stage emits the tag word and prints it. This is the mirror image of
the divergence the agreement suite exists to catch, and worse than a refusal: a program
the reference rejects builds and runs on the stage. `stdlib/typing.fin`'s
`Result<T, U>` is the corpus instance.

Separately, and **not** a defect in either compiler: the reference's
`stdlib/hashmap.fin` `__set` refusal is pinned by
`StageCoverageRefusal.HashMapSetIsRefusedByTheReferenceToo` so the stage's hash-map
coverage is not overstated.

## What a non-Linux lane does not cover, and what that means for 1.0

CI bootstraps the stages on Linux only (`.github/workflows/ci.yml`: the step is
`if: runner.os == 'Linux'`). The reason is in the stage driver: `finc/driver.fin` links
by shelling out to `cc -no-pie ... -Wl,--as-needed -lLLVM-22 -lm`, which cannot work on
macOS (Apple `ld` rejects `--as-needed`, and Homebrew installs no `libLLVM-22`) or on
Windows (no `cc` on `PATH`).

So on a macOS or Windows lane:

- `build/finc_stage2` never exists, and `tests/test_stage_agreement.cpp`'s
  `StageAgreement`, `Stage3Agreement`, `StageDiagAgreement` and
  `Stage3DiagAgreement` suites all skip with an explicit reason (never silently).
- `tests/test_stage_coverage.cpp` skips the same way: every test in it checks for the
  stage binaries first and calls `GTEST_SKIP` with the missing path.

Consequences for the "real compiler" claim in a 1.0 release:

1. **Self-hosting is proven on one platform.** The stage2 == stage3 fixed point, and
   every stage agreement result in this file, is Linux-only. A macOS or Windows build
   of the same commit is verified against the C++ compiler only.
2. **The counts above do not transfer.** A green non-Linux lane says nothing about
   these fourteen samples — it does not even skip *loudly* in ctest output, because
   ctest prints `Skipped` without the reason the test recorded. The reason lives in the
   test's own message and in this file.
3. **The gap is a driver-link problem, not a backend one.** Nothing in the sampled
   lowering is platform-specific in a way these tests exercise; the blocker is the
   `cc` command line in `finc/driver.fin`. Making the stage driver link per-platform is
   a compiler change owned by the self-host wave, and it is the single change that
   would bring every row in this file onto macOS and Windows.
4. **The three silent miscompiles are platform-independent.** They are decided in the
   stage's own code generation, so a macOS or Windows stage (once it can link) would
   reproduce them.

Until the driver links off Linux, the honest statement is: *the self-hosted compiler
is a real compiler on Linux, and on macOS and Windows it is untested by CI.*
