# The `finc` interface, contract 1

This is what `finn` may rely on when it runs `finc`. Everything here was **measured against the binary**,
not read off a plan — every claim below was produced by running `finc` at version `0.4.0 (contract 1)`.
ADR 0009 is the reasoning; this document is the surface.

The half of this document that matters most is the last section. `finc` does not yet produce an
executable, and a contract that only lists what works invites a consumer to build against vapour.

## Discovering the contract

```
$ finc --version
finc 0.4.0 (contract 1)
```

Format: `finc <semver> (contract <int>)`, one line, on **stdout**, exit `0`. The two numbers move
independently. The semver is the release; the **contract integer is what `finn` should branch on**, and it
changes only when something in this document changes incompatibly. Parse the integer, do not compare
semver ranges.

`--help` is also stdout and exit `0`. These two are the only things `finc` writes to stdout, and they are
written because the caller asked for them.

## Exit codes

| Code | Meaning | `finn` should |
| --- | --- | --- |
| `0` | Compiled, **zero** diagnostics | proceed |
| `1` | The source was rejected | show the user diagnostics |
| `2` | Bad command line, or the input could not be read | treat as its own bug or a version mismatch |
| `3` | The compiler itself failed | report a compiler bug, never blame the user's source |

A `3` is a diagnostic and a summary like any other, in whatever format was requested, carrying the help
text "this is a bug in finc, not in the source being compiled". `finn` can therefore report a compiler
crash through the same parser it uses for everything else. Note that nothing reachable in `finc` throws
today, so this path is verified by injection rather than by a test that runs in CI.

Exit `0` implies zero diagnostics — there is no "succeeded with errors". A `2` from a flag `finn` sent is
a version-skew signal, not a user error, which is why it is a separate code from `1`.

## Streams

**stdout is reserved.** Nothing but `--help` and `--version` is ever written there — not progress
chatter, not `Build Successful.`, not diagnostics. If `finn` ever needs to pass compiler output through
to a user, that is stderr. This holds on every exit path, including `2` and `3`.

## `--diagnostics=json`

JSONL on stderr: one object per line, each with a `kind`. Nothing else appears on stderr in this mode —
including for a mistake in the command line, and including when `--color=always` is also passed.

One diagnostic:

```json
{"kind":"diagnostic","severity":"error","code":null,"message":"syntax error, unexpected SEMICOLON, expecting IDENTIFIER","file":"bad.fin","line":1,"column":26,"endLine":1,"endColumn":27,"help":null,"attribution":null}
```

The run always ends with exactly one summary, on every exit path:

```json
{"kind":"summary","errors":1,"warnings":0,"exitCode":1,"status":"failed"}
```

**The summary is how `finn` tells "finished cleanly" from "died before reporting".** An exit code cannot
make that distinction; a missing summary means the compiler did not reach the end. `status` is `ok` or
`failed`.

JSONL rather than one wrapping array, so a compiler that dies mid-run still leaves a parseable prefix.

### Compatibility promise

Keys may be **added**, never removed and never retyped.

`kind` is exactly `diagnostic` or `summary` today. It may gain values; a consumer that does not recognise
a `kind` must **keep the line and carry on**, not fail and not discard it — a future kind will be
something a new `finn` reads and an old one has no business interpreting.

`severity` is exactly `error`, `warning` or `note` today, and may gain values. Treat an unknown severity
as an error rather than crashing: the safe direction is to over-report, because a value invented later
will more likely be *worse* than a note than better than an error. Those three are enumerated here
because "treat unknown as an error" is unimplementable without knowing the known set — every value is
unknown to a consumer that was never told any.

Two keys are deliberately present and always null today:

- `code` — diagnostics will be named and suppressible later without a schema change.
- `attribution` — reserved for wave 4. A compile-time library can inject code, so a diagnostic will be
  able to point at a source location the user never wrote; when that happens this names the handler
  responsible and the event point that fired it. It is `null` for every diagnostic `finc` raises on its
  own, which today is all of them. **Do not** assume a diagnostic's `file`/`line` is somewhere the user
  can edit once this is populated.

  Its shape is settled, and is documented here rather than left to be discovered because the emitting
  code already exists — `DiagnosticEngine::emitJson` writes it the moment anything populates it, and
  nothing does yet:

  ```json
  "attribution":{"handler":"<@special function name>","event":"<event point>"}
  ```

  Both members are themselves nullable. A consumer should treat "attribution is non-null" as the whole
  signal that a location is generated, and read `handler`/`event` only to say so in a message — that way
  a third member added later cannot break it.

`file` is nullable and `line` is `0` when a diagnostic is about the invocation rather than a place in a
file — a bad flag, a file that could not be read. Check for null; do not print `null:0`.

`help` carries what a human would read as a note. For command-line errors it holds the usage hint.

## Flags

```
-o <path>              Output path — ACCEPTED AND IGNORED, see below
-I, --include <path>   Add a module search path
--fin-libs <paths>     Library search paths, platform-separated
--diagnostics=<fmt>    human (default) or json
--color=<when>         auto (default), always or never
--debug-ast            Print the parsed AST
--debug-sema           Print semantic analysis details
--no-check             Skip semantic analysis (unsafe)
--version / --help
```

**An unknown flag is an error (`2`), never ignored.** A toolchain whose flags fail silently is the worst
case for a caller that builds argv programmatically. A second positional argument is also `2`.

`--diagnostics=` and `--color=` are honoured for command-line errors too, and **may be written after the
mistake they render** — `finn` does not need to order argv to get parseable errors.

## Library search paths

`--fin-libs <paths>` is how `finn` names the environment a build compiles against. It is also accepted
glued (`--fin-libs=<paths>`) and it is **repeatable**.

- **It replaces `$FIN_LIBS`, it does not extend it.** This is the important one. `finn` pins an
  environment; an ambient `FIN_LIBS` in the user's shell adding a path to a pinned build is precisely the
  drift the flag exists to prevent.
- `--fin-libs=` with an empty value means **no library paths**, and is distinct from omitting the flag.
- The separator is the **platform's**: `;` on Windows, `:` elsewhere. A Windows path starts `C:\`, so a
  fixed `:` would split one path into a bogus `C` and a rootless `\libs`. Repeating the flag is the way
  to pass a path that itself contains the separator.
- Empty entries are dropped, not read as the working directory.
- **Repeating the flag accumulates.** `--fin-libs=a --fin-libs=b` is the same as `--fin-libs=a:b` on a
  POSIX host. It is not "last one wins", which is why repeating it is a sound way to pass a path that
  contains the separator character.
- Because repetition accumulates, `--fin-libs=` contributes **nothing** rather than resetting: after
  `--fin-libs=a --fin-libs=`, the library paths are `a`. The empty value means "no library paths" when it
  is the only occurrence — which is still distinct from omitting the flag, because omitting it falls
  through to `FIN_LIBS` and to the default below, and naming nothing does not.
- **Naming library paths displaces every default, not just `FIN_LIBS`.** When `--fin-libs` or `FIN_LIBS`
  supplies paths — including when it supplies none — the search path is exactly the `-I` paths plus those,
  and nothing else: no bundled standard library, and not the working directory. A pinned build is
  hermetic, and does not become less so on a machine that happens to have the compiler's source tree or a
  same-named file in the project root.

### Defaults, when nothing named a library path

- The standard library that shipped with the binary, found at `<directory of the executable>/../lib/std`.
  One rule covers both layouts: a release archive unpacks to `bin/finc` beside `lib/std`, and a build tree
  puts `finc` in `build/` beside the source `lib/std`. It is resolved from the executable's own location,
  never from the working directory, and it is skipped when the directory does not exist — which is **every
  build today**, because `lib/std` does not exist yet.
- Then the working directory, for the convenience of a bare `finc foo.fin` typed by hand.

Search order overall: `-I` paths, then the `--fin-libs`/`FIN_LIBS` set, then (only if that set was never
named) the bundled standard library and the working directory.

### How a module name becomes a path

Inside any search path — `-I` or library alike — `finc` tries four spellings of a module `m`, in order:
`m` itself as a file, `m.fin`, `m/index.fin`, and `m/m.fin`. A directory is never itself the answer.

A dotted package import (`import std.io`) has its dots replaced with `/` and is looked up **only** in the
search paths, in the order above.

A quoted file import (`import "foo"`) is resolved first **relative to the directory of the importing
file**, then as an absolute path if it is one, and only then through the search paths. The
importing-file-relative step is why most of the corpus resolves its imports with no search path
configured at all, and it means a project's internal imports do not depend on what `finn` passes.

## Environment

`finc` reads exactly two environment variables, and this list is exhaustive as of contract 1:

- `FIN_LIBS` — library search paths, as above.
- `NO_COLOR` — any value disables colour, as the de-facto standard requires.

A caller pinning a build needs to neutralise only `FIN_LIBS`. If a third variable is ever added it is a
change to this document.

## What does not work yet — read this before building against the above

- **`finc` does not produce an executable.** There is no code generation. `-o` is parsed, validated and
  stored, and then ignored. A `0` from `finc` today means "accepted the source", **not** "wrote a binary".
  Anything in `finn` that runs the output of a build has nothing to run.
- **11 of 50 corpus samples currently compile clean.** The frontend, not the backend, is the bottleneck;
  most real Fin source is still rejected. Do not treat a `1` as evidence about the user's code yet.
- **There is no `finc check` subcommand.** `finn check` is expected to consume this JSON format, but the
  entry point it would call does not exist under that name; today it is `finc <file> --diagnostics=json`.
- **No standard library ships.** `lib/std/` does not exist in the repository yet, so a release archive
  cannot be produced: the release job asserts `bin/finc` plus `lib/std/**` and fails loudly rather than
  publishing a stdlib-less archive. Release archive names and the `index.json` shape are settled and in
  `.github/workflows/release.yml`, but **no release has been published against them**.
- **CI has never run.** The six-platform matrix is committed and unexecuted, so "builds on Windows" is
  currently a claim and not a fact.
- **The default standard library resolves to nothing.** This used to say that every compilation added
  `tests/samples/stdlib` — a directory from the compiler's own source tree — to its search paths. That is
  fixed: the default is now `<exe dir>/../lib/std`, and library paths named explicitly displace it
  entirely. But `lib/std` does not exist in any build yet, so the default finds nothing and a program that
  imports a standard-library module fails to resolve it however it is invoked. Passing `--fin-libs` is
  still the right thing for `finn` to do — now because it is the only thing that resolves, rather than
  because the default was wrong.

## Changing this document

Anything here changing incompatibly bumps `kFincContractVersion` in `src/driver/Version.hpp`. Adding a
JSON key, adding a flag, or adding a severity value does not. If `finn` needs something that is not here,
that is a request to this repository, not a local workaround — a hand-maintained assumption on the `finn`
side is how `download.rs:62-65` came to match on OS alone and hand arm64 users an x86_64 build.

### Why the search-path change did not bump the contract

Contract 1 gained a bundled-stdlib default, lost the `tests/samples/stdlib` default, and narrowed the
working-directory default to invocations that name no library paths. The version stayed at 1, deliberately:

- Nothing has been released against contract 1. No release is published and CI has never run, so no build
  of `finn` can be depending on the behaviour that changed.
- The behaviour that changed was documented, in this file, as something not to rely on.
- The one narrowing — the working directory no longer being searched by a pinned build — is a *fix to* the
  guarantee this section of the document makes, not a break in it. A caller that asked for hermeticity was
  not getting it.

That reasoning is recorded rather than assumed, because "it hadn't shipped yet" stops being available the
first time it ships, and the next such change will need a bump on an argument this one did not need.
