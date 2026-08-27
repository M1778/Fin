# `finc` has a machine contract, and the package manager is its first consumer

`finc`'s command line, exit codes and diagnostic streams are a stable interface with a second program
on the other end of it, not a convenience for whoever is typing. Concretely: diagnostics go to
stderr and stdout is reserved; exit codes are `0` success, `1` diagnostics, `2` usage or CLI misuse,
`3` internal error; unknown flags and a second positional argument are errors rather than being
ignored; exit `0` implies zero diagnostics; and `--diagnostics=json` emits one JSON object per line
on stderr, ending with a summary object.

The forcing function was `finn`, which constructs `finc`'s argv programmatically and branches on its
exit code. Every one of those guarantees is currently violated, and the violations are silent:

- `main.cpp:39` is `else if (arg[0] != '-') opts.inputFile = arg;` with no `else`, so
  `finc a.fin --bogus -o prog` discards `--bogus` and compiles `prog`.
- `Driver.cpp:119` prints `Build Successful.` gated on `analyzer.hasError` alone.
  `DiagnosticEngine::hasErrors()` exists and the driver never calls it, so an unlexable byte between
  two declarations — reported by `lexer.l:214` writing raw to `std::cerr`, never reaching the engine,
  returning no token — produces `Build Successful.` and exit `0`.
- Every diagnostic goes to stdout via `fmt::print`. The only writer to stderr in `src/` is that raw
  lexer rule. There is no `NO_COLOR` or `isatty` check anywhere, so anything capturing `finc`'s
  output receives ANSI escapes.
- `ModuleLoader.cpp:104` and `:113` print raw `[ERROR]` lines outside the engine, so a bad import is
  reported three times in two formats.

Library search paths are part of this interface too, and the corpus says so explicitly:
`tests/samples/importing.fin:9` records that `finn` tells the compiler which environment to use "using
`--fin-libs` flag". So `--fin-libs <paths>` exists alongside the `FIN_LIBS` environment variable, and
**the flag replaces the variable rather than extending it**. That direction is the whole point: `finn`
passes the flag to pin the set of libraries a build compiles against, and an ambient `FIN_LIBS` in the
caller's shell silently adding a path to a pinned build is precisely the drift the flag exists to
prevent. `--fin-libs=` with an empty value is therefore meaningful — it says "no library paths" — and is
distinct from omitting the flag.

Both spellings are accepted (`--fin-libs <paths>` and `--fin-libs=<paths>`) and the flag is repeatable,
which matters for a caller assembling argv programmatically: repeating it is the only way to pass a path
that itself contains the list separator. That separator is the platform's — `;` on Windows, `:`
elsewhere — because a Windows path starts `C:\`, and splitting one on a colon yields a relative `C` and
a rootless `\libs` without saying anything. An empty entry in a list is dropped rather than read as the
working directory, so a stray or doubled separator cannot quietly change where modules come from.

JSONL was chosen over a single wrapping JSON array because a compiler that dies mid-run still leaves
a parseable prefix, where a truncated array leaves nothing. The trailing summary object exists because
exit codes cannot distinguish "finished with no errors" from "died before reporting any" — a
distinction the package manager has to make to decide whether it may claim success.

The rejected alternative was to leave the human format as the only output and have `finn` parse it.
That makes the diagnostic renderer a wire format, so improving how a diagnostic reads becomes a
breaking change for the package manager.

## Consequences

The four-way exit split reclassifies existing failures. `main.cpp:19,36,44` and `Driver.cpp:59` are
usage-class and become `2`, so anything already scripted against "non-zero means diagnostics" changes
meaning.

An empty source file must compile. `readFile` returns `""` for both "missing" and "empty" and the
driver treats both as fatal, so the two cases have to be told apart before this contract holds.

Two output renderers now exist over one diagnostic model, and the JSON one has a compatibility
promise the human one does not: keys may be added, never removed or retyped, and `severity` may only
gain values. `code` ships as `null` so diagnostics can be named and suppressed later without a schema
change.

In JSON mode no non-JSON byte may reach stderr. A single stray `fmt::print` on that path breaks the
consumer's parser, which makes the raw-print sites above correctness bugs rather than cosmetic ones.

That obligation starts **before the command line is understood**, which is not where it was first
implemented. Every argv-level usage error — unknown flag, second positional, missing operand — was
written as plain text even under `--diagnostics=json`, because argv is parsed before any engine exists.
The argv error a programmatic caller is most likely to hit is a `finn` built against a newer `finc`
passing a flag this one does not have, so the format was being abandoned at exactly the moment the
consumer needed to explain a version mismatch. `--diagnostics=` and `--color=` are therefore read in a
pre-pass over the whole command line: the flag may be written *after* the mistake it has to render, and a
consumer cannot be asked to order argv to get parseable errors. A format *value* the compiler does not
recognise is the one case that cannot be honoured, and the error about it is reported in whatever format
the pre-pass settled.

The same held for exit `3`, and worse. An internal error was a raw line with **no summary**, so in JSON
mode a crash was indistinguishable from a truncated stream — the one distinction the summary object exists
to make. It is now a diagnostic plus a summary like any other, carrying the help text "this is a bug in
finc, not in the source being compiled", because `3` is precisely the code that tells a consumer not to
blame the user. Nothing reachable throws today, so this path has no test that runs; it was verified by
temporarily injecting a throw, and that is worth admitting rather than implying coverage that does not
exist.

An argv diagnostic emits `file` as null and `line` as 0 — it is about the invocation, not about a place
in a file. That needed no schema change, because `file` already went through the nullable path. The
`note: run finc --help` line became the existing `help` key for the same reason: a loose note is a
non-JSON line, and a key was already there to carry it.

Moving diagnostics to stderr means `finc`'s own progress chatter — `[INFO] Running Semantic
Analysis...`, `Build Successful.` — has to be classified as one or the other. Reserving stdout only
pays off if nothing writes there by default.
