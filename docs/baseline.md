# Compiler baseline, 2026-08-22

First successful build of `finc` in this environment, and the first measurement of the compiler
against the whole sample corpus. Every number here was produced by running the commands in
[Reproducing](#reproducing) on 2026-08-22; none was inferred from reading the code.

**This is a dated snapshot, not a live status page.** It is kept as the record of where the compiler
stood when work started, because the value of a baseline is that it does not move. Two things follow.
Numbers here go stale by design — check the section on line endings for which have been re-derived
since and which have not — and a number here being measured is not the same as it being measured
*correctly*: the line-ending census in this file was wrong when written, and was caught by rechecking
rather than by anything in the method.

## Headline

| Measure | Result |
| --- | --- |
| Samples that parse | 12 of 50 |
| Samples that survive the full pipeline | 11 of 50 |
| `fin_tests` | 16 passed, 38 failed of 54 — measures a suite that no longer exists, see below |

The one file that parses but does not survive is `interfaces.fin`, which is the **only** sample that
reaches the semantic analyzer at all. Every other failure is the parser or the lexer. The practical
meaning: `SemanticAnalyzer` (~1,940 lines) is almost entirely unexercised by the corpus, and no
statement about its correctness has any evidence behind it yet.

Passing: `arrays_enums.fin`, `basic.fin`, `deeptest1.fin`, `deeptest3.fin`,
`generics_interfaces.fin`, `macros.fin`, `struct_methods.fin`, `structs.fin`,
`type_annotations.fin`, `stdlib/networking.fin`, `stdlib/somelib.fin`.

Note that two of the eleven passes are hollow: `stdlib/networking.fin` is a single comment reading
`// NOT IMPLEMENTED YET` and `stdlib/somelib.fin` reads `// Empty lib`. Nine samples exercise the
compiler.

**Since moved: 16 of 50 now survive the full pipeline, not 11.** Recorded here as a pointer, not a
rewrite — the eleven above is what was true when work started and it stays. The live number is
`Census.ThePassingSampleCountNeverFalls` in `tests/test_expectations.cpp`, which prints it and holds a
floor under it; re-derive the list itself with the command in [Reproducing](#reproducing).

### The `fin_tests` row measured the parse column twice

At the time of this snapshot the suite was one file, `tests/test_parser.cpp`: four hand-written
`TEST_F` cases plus one `TEST_P` instantiated once per `.fin` file under `tests/samples`. So its three
numbers are arithmetic on the row above it — 54 is 4 + 50, 16 is 4 + the 12 that parse, and 38 is
50 - 12. It carried no information the parse column did not already carry, which is worth knowing
because a suite whose count is derivable from another measurement cannot disagree with it, and a suite
that cannot disagree cannot catch anything.

That suite has since been replaced by an expectation-driven harness (`tests/test_expectations.cpp`,
`tests/Corpus.cpp`, `tests/Pipeline.cpp`, and the CLI and unit files beside them), which registered 168
tests at the last configure. The two columns now measure different things: a sample that fails to parse
*passes* its test when its `//@` line says it should fail. Re-derive with
`ctest --test-dir build/tests` rather than reading this row, and expect the number to move — the
harness is under active change.

## Failures grouped by the fix that unblocks them

39 failures cluster into eight groups plus thirteen singletons. The groups matter because eight
changes clear 26 files.

### A. `::` in module paths — 7 files

`import { Collection } from collection::std;`

`complex.fin:1:13`, `const.fin:1:28`, `deeptest4.fin:1:32`, `enums.fin:1:28`, `lambdas.fin:1:34`,
`prototype_test.fin:1:32`, `readonly.fin:1:28`

> `unexpected DOUBLE_COLON, expecting SEMICOLON or DOT`

`DOUBLE_COLON` lexes. The path nonterminal only accepts `.`. Single largest cluster in the corpus.

### B. `%{ ... }%` attribute block — 4 files

`#[stdimport] %{ ... }%` — one attribute applied to every statement in the block.

`stdlib/collection.fin:1:14`, `stdlib/hashmap.fin:1:14`, `stdlib/stdio.fin:1:14`,
`stdlib/typing.fin:1:14`

> `unexpected MOD`

`%` lexes only as the modulo operator. `%{` and `}%` are not tokens.

### C. Attribute before `import` — 4 files

`#[stdimport]` on its own line, `import ...` on the next.

`stdlib/memory.fin:2:1`, `stdlib/prototypes.fin:2:1`, `stdlib/stdptr.fin:2:1`,
`stdlib/types.fin:2:1`

> `unexpected KW_IMPORT`

Attributes attach to declarations but not to imports.

### D. `namespace` — 3 files

`namespace std { ... }`

`stdlib/enums.fin:1:11`, `stdlib/error.fin:1:11`, `stdlib/operators.fin:1:11`

> `unexpected IDENTIFIER`

`namespace` is not a keyword; it lexes as an identifier, so `namespace std` reads as two identifiers
in a row.

Groups B, C and D together account for **every** file under `tests/samples/stdlib/` that contains
code. The standard library does not parse at all today.

### E. `/* ... */` block comments — 2 files

`macro_definitions.fin:1:1`, `macros2.fin:3:1`

> `unexpected DIV, expecting end of file`

`lexer.l:70` handles `"//".*` and nothing else. Block comments are entirely unimplemented, so `/*`
lexes as a division operator. Reproduces in two lines:

```fin
/* hello */
fun main() <noret> {}
```

### F. Nested generics closing with `>>` — 2 files

`blame_assert.fin:19:24` (`pub fun pp(m: M<M<int>>) <noret>{}`), `functions.fin:14:1`
(`callback: fn(Result<int>) -> Result<Result<int>>`)

> `unexpected RPAREN, expecting COMMA or GT or SHIFTRIGHT`

`SHIFTRIGHT` is not split back into two `GT`s in type context, so a doubly-nested generic can never
be closed. `blame_assert.fin:19` carries the comment `// TODO: This should be allowed`, so the
corpus already knows.

### G. `@define` parameter types in angle brackets — 2 files

`@define printf(fmt: <string>, ...) <int>;`

`operators.fin:2:21`, `preprocessor.fin:1:21`

> `unexpected LT`

Ordinary `fun` parameters are `name: type`; `@define` parameters are `name: <type>`. Whether that
difference is intended is a language decision, not a bug report.

### H. Nullable declarations — 2 files

`nullifier.fin:2:10`, `undefined_behavior.fin:7:4`

> `unexpected QUESTION, expecting LPAREN or LT` / `unexpected QUESTION, expecting IDENTIFIER or LPAREN`

`?` on a declaration — `fun? make_A(...)`, `b? <int>` — is unimplemented. `QUESTION` exists as a
token only for the conditional expression.

### Singletons

| File | Position | Construct | Message |
| --- | --- | --- | --- |
| `arrays.fin` | 2:21 | `type ArrayType<T> = [T];` — array type on the right of `type` | `unexpected LBRACKET, expecting KW_ANY or LT` |
| `deeptest2.fin` | 38:13 | `Person():age(10), name("Hello")` — C++-style constructor member-init list | `unexpected COLON, expecting LBRACE or LT` |
| `extern_as.fin` | 4:7 | `const int myglobv <int> = 10;` — type named twice | `unexpected TYPE_INT, expecting IDENTIFIER` |
| `implements_block.fin` | 12:5 | `pub` on a member inside a top-level `X implements <...> { }` block | `unexpected KW_PUB` |
| `importing.fin` | 9:8 | `import * from somelib;` | `unexpected MULT, expecting IDENTIFIER or STRING_LITERAL or LBRACE` |
| `letssee.fin` | 8:5 | `priv:` visibility label inside a struct body | `unexpected COLON` |
| `literal_interface.fin` | 3:24 | `$interface` and `$struct` as parameter types | `unexpected KW_INTERFACE, expecting KW_TYPE` |
| `literal_struct.fin` | 2:25 | `<T: any implements Struct>` — `implements` in a generic constraint, with no angle brackets | `unexpected KW_IMPLEMENTS, expecting COMMA or GT` |
| `loops.fin` | 6:10 | `for (i : int = 0; ...)` — colon form in a `for` header | `unexpected IDENTIFIER, expecting KW_LET or KW_CONST` |
| `simple_pointers.fin` | 21:14 | `*x = new int*;` — `new` of a pointer type | `unexpected TYPE_INT, expecting IDENTIFIER or KW_SELF_TYPE or LBRACKET or AMPERSAND` |
| `useful_macros.fin` | 3:33 | `map!{ "alex" => 10 }` — `=>` inside a `name!{ }` macro call | `unexpected EQUAL, expecting GT` |
| `variables.fin` | 17:7 | a bare `{ }` block used as a statement to open a scope | `unexpected KW_LET` |
| `interfaces.fin` | 15:22 | *semantic*, not syntax | `Type 'T' does not have methods` |

`variables.fin` reproduces in six lines, and shows the cause is not `let` but the block: a bare
`{ }` is being parsed as an expression, so the statement inside it is rejected.

```fin
fun main() <noret> {
  let a <int>;
  {
    let b <int>;
  }
}
```

## Two facts the corpus revealed that were not previously recorded

`$struct` and `$interface` exist alongside `$type`. The grammar accepts `$` followed by `KW_TYPE`
only, which is why `literal_interface.fin` fails. So the meta-type is a family, not a single thing.

Macro invocation is `name!{ ... }` with `=>` pairs inside, Rust-style — not keyword arguments. The
`!{` part parses; `=>` does not lex.

## Driver and CLI defects, each with a reproducer

### `Build Successful.` is printed while errors are on the terminal, and exit is 0

Confirmed. `src/driver/Driver.cpp:119` prints the success line, and the only gate before it is
`analyzer.hasError` at `:105`. `DiagnosticEngine::hasErrors()` — declared at
`DiagnosticEngine.hpp:13` — is **never called by the driver at all**; its only caller in the tree is
`tests/test_parser.cpp:55`. So any error that does not set the analyzer's own flag passes straight
through to success.

The lexer is exactly such an error. `src/lexer/lexer.l:214` is the catch-all rule:

```
.           { std::cerr << "Lexer Error: " << yytext << std::endl; }
```

It writes raw to `std::cerr`, never touches the `DiagnosticEngine`, and returns no token — so the
parser never sees the character either. Put an unlexable byte where the parser will not miss a
token, i.e. between two declarations, and the compiler reports success:

```
$ printf 'fun a() <noret> {}\n\302\247\nfun main() <noret> {}\n' > f4.fin
$ ./build/finc f4.fin
Build Successful.          # stdout
Lexer Error: <?>           # stderr, twice — once per byte of the UTF-8 character
$ echo $?
0
```

An earlier attempt to reproduce this tested the semantic-error path and concluded the bug was not
real. That was the wrong path: a semantic error *does* set `analyzer.hasError`, so it exits 1
correctly. The bug lives in every error path that doesn't.

### Unknown flags are silently discarded and any bare argument overwrites the input file

`src/main.cpp:39` is `else if (arg[0] != '-') opts.inputFile = arg;`, with no `else` for unrecognised
flags.

```
$ ./build/finc tests/samples/basic.fin --bogus -o prog
[ERROR] Could not read file: prog
```

`--bogus` vanished, and `prog` — the operand of an unsupported `-o` — became the file to compile. A
version-pinned toolchain whose flags fail silently is the worst possible failure mode for `finn`,
which constructs argv programmatically.

### An empty source file is an error

`readFile` returns `""` for both "file does not exist" and "file is empty", and the driver conflates
them, so a legitimately empty `.fin` file cannot compile:

```
$ : > empty.fin && ./build/finc empty.fin
[ERROR] Could not read file: empty.fin      # exit 1
```

### Every diagnostic goes to stdout

`fmt::print` throughout `DiagnosticEngine` and `Driver`. Redirecting stderr to `/dev/null` loses
nothing; the sole writer to stderr in `src/` is the raw lexer rule above. Anything that captures
`finc`'s output cannot separate diagnostics from program output, and there is no `NO_COLOR` or
`isatty` check anywhere in `src/`, so captured diagnostics carry raw ANSI escapes.

### A missing module is reported three times

`ModuleLoader::loadModule` prints `[ERROR] Module not found: {}` at
`src/utils/ModuleLoader.cpp:104` and `[ERROR] Circular dependency detected: {}` at `:113` with
`fmt::print`, bypassing the `DiagnosticEngine`. The caller then raises a proper diagnostic. Observed
output for one bad import is the raw line **twice** followed by the real diagnostic. Exit is 1 in
this case, because the analyzer flags it — but that is the caller's doing, not the loader's.

### The corpus is worse than predicted

A static estimate of "at least 5 failures" was made before the build worked. The measured number is
39.

## Where the test suite disagrees with the ratified model

`tests/test_parser.cpp` is the whole suite: 157 lines, four hand-written parser tests, plus
`FileParserTest` which auto-discovers every `.fin` under `samples` and asserts each one parses.
Three problems:

1. It asserts **all** samples parse. The normative/aspirational split says some samples are design
   sketches whose failure means nothing, so this suite cannot express the corpus's own authority
   model and is red for reasons that are not bugs.
2. It never constructs `SemanticAnalyzer`, `ModuleLoader` or `MacroExpander`. `parseString` runs
   `Preprocessor` then the parser and returns. Everything after parsing is untested by any test.
3. `INSTANTIATE_TEST_SUITE_P` calls `GetFinFiles()` during static initialisation, so if the working
   directory is wrong the suite silently registers zero file tests and reports success.

## Environment

Established by getting the build to work; recorded because none of it was documented.

- `cmake` 3.28.3, Unix Makefiles generator
- `conan` 2.31.2 with a `fin-debug` profile — the default profile's `compiler.cppstd=gnu17` is wrong,
  the project needs `gnu20`
- `bison` 3.8.2, `flex` 2.6.4, `g++` 13.3.0
- LLVM 18.1.3 — requires the `llvm-18-dev` package, not just the runtime, and
  `-DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm`
- `build.sh` does not work as committed: it calls `uvx conan`, and `uvx` is not installed

Three build artifacts were committed to git — `CMakeCache.txt`, `CMakeFiles/CMakeConfigureLog.yaml`,
`CMakeFiles/cmake.check_cache` — and the cache recorded
`CMAKE_HOME_DIRECTORY=/mnt/c/Users/m1778/Desktop/Fin`, which made CMake refuse to configure
anywhere else. They are now untracked and ignored.

**Seventeen** samples have CRLF line endings: `blame_assert.fin`, `collection.fin`, `error.fin`,
`generics_interfaces.fin`, `hashmap.fin`, `implements_block.fin`, `interfaces.fin`, `lambdas.fin`,
`letssee.fin`, `macro_definitions.fin`, `nullifier.fin`, `operators.fin`, `prototype_test.fin`,
`stdio.fin`, `type_annotations.fin`, `types.fin`, `useful_macros.fin`.

This paragraph said *eleven* until it was rechecked, and the six it missed — `collection.fin`,
`error.fin`, `hashmap.fin`, `operators.fin`, `stdio.fin`, `types.fin` — are all under
`tests/samples/stdlib/`. So the census that produced this number globbed `tests/samples/*.fin` and
never descended, which is the same error that once put the corpus size at 37 instead of 50. **Any other
count in this document is suspect for the same reason and should be re-derived before it is relied
on**, with `git ls-tree -r --name-only HEAD -- tests/samples` rather than a single-level glob. The
line endings are not currently causing any of the 39 failures, but they will bite the column numbers in
an expectation-based test harness, so they need settling before `//@ error <line>:<col>` annotations are
written.

Three counts have since been re-derived against the binary. The eleven-sample passing list above is
byte-identical to what `finc` produces today, `--no-check` still reaches twelve, and `interfaces.fin`
is still the only file between the two — so the headline survived the recheck that this paragraph
failed. The `fin_tests` row did not, for the reason given in its own section.

The failure-group counts above are internally consistent — the eight groups' file counts sum to 26, plus
thirteen singletons is 39, plus eleven passing is 50 — but consistency is not verification: the
arithmetic would balance just as well with a file filed under the wrong group. Group *membership* has not
been rechecked, and it is the part most likely to be stale, because the frontend work in flight is aimed
directly at these groups.

## Reproducing

```sh
conan install . --output-folder=. --build=missing -s build_type=Debug --profile=fin-debug
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=Debug/generators/conan_toolchain.cmake \
         -DCMAKE_BUILD_TYPE=Debug \
         -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm -G "Unix Makefiles"
cmake --build . -j"$(nproc)"
./fin_tests
```

Per-file census:

```sh
find tests/samples -name '*.fin' | sort | while read -r f; do
  out=$(./build/finc "$f" 2>&1 | sed 's/\x1b\[[0-9;]*m//g')
  if echo "$out" | grep -q '^error:'; then
    echo "FAIL|$(echo "$out" | grep -m1 -- '-->' | sed 's|.*tests/samples/||')|$(echo "$out" | grep -m1 '^error:' | sed 's/^error: *//')"
  else
    echo "PASS|${f#tests/samples/}"
  fi
done
```
