# Changelog

All notable changes to the Fin compiler (`finc`) and the shipped standard
library (`lib/std`) are recorded here.

The format is [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
The compiler's semver and the machine-contract integer move independently;
`finc --version` prints both.

## [Unreleased]

Nothing yet.

## [1.0.1] - 2026-11-21

The 1.0.0 release published broken glibc Linux x86_64 binaries and was marked
prerelease as soon as that was found; this release is the first 1.0.x that is
safe to install on every platform it ships for.

### Fixed

- Fixed `finc 1.0.0`, `.deb` and `.rpm` for `x86_64-unknown-linux-gnu`
  segfaulting before `main` on any host whose `llvm-22` package differed from
  the CI runner's. The binaries carried 25 `R_X86_64_COPY` relocations inside
  `PT_GNU_RELRO` covering LLVM data symbols, which libLLVM's own static
  initializers then wrote to after the page became read-only. `fin_core` is now
  built as position-independent code, which reaches those symbols through the
  GOT: 29 COPY relocations became 2, both libc's `stdout`/`stderr`. Proved in
  both directions across an Ubuntu 24.04 chroot and the build host, with a
  no-PIC control that still crashes. See `docs/known-issues.md`.
- Fixed `math::isqrt` looping forever on large inputs: the overflow in
  `(guess+1)*(guess+1)` made the loop condition permanently true.
- Fixed `math::gcd` answering a negative value for `gcd(-2147483648, 0)`,
  contradicting its own header. `gcd` and `lcm` now compute in `long` and
  blame when the result cannot be represented instead of wrapping.
- Fixed `argparse::parse` reading past its array for an over-large count,
  accepting a negative count as a successful empty parse, and reaching
  `strncmp` with a null element. All three now refuse by name.
- Fixed `json_encode_float` emitting `inf` and `-nan`, which are not JSON. A
  non-finite double now blames and names the value rather than substituting
  `null`, which would have silently turned a computed number into absence.
- Fixed `let a <[char]> = null;` segfaulting before the program's first
  statement. The string-to-array conversion emitted `strlen` with a null
  pointer, and no Fin-level guard could run first because the crash was in the
  conversion. A constant null now returns the zero pair with no call emitted;
  a non-constant one is measured inside the non-null branch, which is where the
  call has to live -- LLVM hoists it above a comparison that guards it.
- Fixed `-lLLVM-<major>` being named on every link. A reference-compiled object
  references no LLVM symbol, so the flag broke `finc -o app` on any machine
  without the LLVM *development* package while buying nothing. The library is
  now named only when an object being linked mentions it.
- Fixed an imported `const` never becoming a global, so importing one failed
  with `codegen: the name '...' is not lowered yet`. That made 168 `pub const`
  declarations in `lib/std` unusable outside their own file -- `parse`'s
  precedence table, `token`'s kinds, `ascii`'s classes. The root is walked
  first, then each module, first-wins.
- Fixed `finfmt` never being installed. `install(TARGETS ...)` named only
  `finc`, so no archive, package or installer had ever contained the formatter
  this project documents as the way to format Fin code.
- Fixed the shipped TextMate grammar declaring `repository.comment` as an
  array. TextMate requires one rule object, so the real engine silently
  ignored it and comments and strings never highlighted. The generator, the
  grammar, and the validator that had mirrored the same wrong assumption are
  all fixed, and the validator now asserts the shape before its replay.
- Fixed `install.sh` selecting the newest *tag* rather than the newest
  published *release*, which made a default install compile `v0.4.0` — a tag
  whose release never published. It now asks the releases API and refuses when
  nothing is published, deriving the API host from `$REPO` so mirrors work.
- Fixed the published site's exit-code table, which claimed `2` was a semantic
  failure and `3` a codegen refusal. Verified: both exit `1`, `2` is an
  invocation failure, `3` is an internal compiler failure.
- Fixed the published site's `rptr` example, which promised `Refs: 1` and
  `Refs after alias: 2` where the program prints `2` and `3`.
- Fixed the contract cross-check's scheduled job, which failed in `sed` on an
  undefined capture group before comparing anything.

### Added

- Added `finc --explain <code>` and `finc --explain list`, backed by a
  registry where a code cannot be emitted without an explanation and an
  explanation cannot describe a code nothing emits. Every documented example is
  compiled by the suite.
- Added editor grammars for TextMate, highlight.js and Pygments under
  `editors/`, generated from one word list derived from the lexer.
- Added a compiler threat model (`docs/threat-model.md`), a supply-chain guide
  with per-artifact verification commands, Dependabot, and OpenSSF Scorecard.
- Added `docs/troubleshooting.md`, built from failures reproduced on a real
  host, and rewrote `INSTALL.md` around commands that were executed.
- Added a measured performance baseline and a fingerprint-checked gate that
  skips rather than inventing a verdict when the machine differs.
- Added stage-coverage tests for eleven samples the run-agreement harness had
  been skipping, plus named refusal pins for the three genuine refusals.
- Added `tests/tools/verify_artifacts.py`, and made the deb and rpm release
  lanes compile and run a program that resolves the stdlib from a foreign
  directory instead of compiling a sample that imports nothing.

### Changed

- Changed the license metadata to match what the source headers already said.
  `LICENSE` now carries the GPLv3 section 14 notice granting
  `GPL-3.0-or-later`, packaging agrees, and every archive and package carries
  the license text. Previously the tree said `-or-later` in 254 files and
  `-only` in every package, and shipped no license text at all.
- Changed `lcm` to blame instead of silently wrapping when its result exceeds
  `int`. Behaviour change for any caller that relied on the wrapped value.
- Changed the release pipeline to attest build provenance and generate SBOMs,
  and to refuse to ship an ELF carrying a non-libc `COPY` relocation.

## [1.0.0] - 2026-11-20

First 1.0 release; `finc --version` prints `finc 1.0.0 (contract 3)`.

### Added

- Added native Windows support to the standard library: `fs`, `log`, `env`,
  `process`, `stdio`, `sync`, `networking`, `time`, `cli` and `path` now select
  Win32 and UCRT declarations, where 0.4.x had no Windows branch at all.
- Added predefined `FIN_TARGET_*` preprocessor names (three operating systems,
  two architectures) so a module can select its native ABI declarations, in the
  file itself and in every imported module (ADR 0051 addendum).
- Added Winsock 2 support to `networking::std` on Windows, with one Winsock
  startup reference per connected socket; `Socket.fd` carries the native handle.
- Added `Command::parse(args)` to `cli::std`, which parses an argument list the
  caller already holds; `parse_args()` now reads native argv on Linux, macOS and
  Windows instead of Linux `/proc/self/cmdline`.
- Added `--` to the `cli::std` command line as the end of option recognition;
  later tokens are positional.
- Added explicit disposal for the containers and parser state that own memory:
  `free_collection`, `free_hashmap`, `free_deque`/`free_queue`/`free_stack`,
  `free_mutex`/`free_lock`/`free_channel`/`free_stream`, `free_command` and
  `free_parse_result`.
- Added `wptr::release()` and a released flag to weak smart-pointer handles, so a
  weak handle can be cleaned up explicitly before the counter cells are freed
  (ADR 0039 amendment).
- Added `time::Instant` on Windows through `QueryPerformanceCounter`, plus
  `sleep` and `usleep` implementations there.
- Added foreign-function callbacks: a non-capturing Fin function value can be
  passed where C expects a function pointer, and unsupported captures or
  aggregate parameters are refused by name.
- Added self-hosted aggregate ownership: struct bindings get reusable storage,
  returns detach, fixed arrays copy recursively, and an uninitialized dynamic
  array is a null handle.

### Changed

- Changed the version to 1.0.0. The machine contract remains at 3.
- Changed `random_int` to cover the full inclusive `int` range with 32-bit
  sampling, rejecting the incomplete final bucket (issue #138).
- Changed `random::std::rand` to export the C runtime function directly instead
  of defining a second Fin symbol of the same name (issue #136).
- Changed `Instant::deadline_after_ms` to blame for an offset that leaves the
  signed 64-bit nanosecond range instead of wrapping into an expired deadline
  (issue #139).
- Changed `base64_decode` to reject leading, discontinuous, excess and
  nonterminal padding before allocating output (issue #140).
- Changed `BTreeMap` to blame by name when an ordering operation runs on a
  nonempty map with no comparator, instead of calling a null pointer
  (issue #135).
- Changed `Collection` growth and `reserve` to blame on capacity overflow and on
  a negative reservation instead of over-allocating.
- Changed macro expansion to check the keyword table first, so `blame !(x)` and
  `return !(x)` stay unary negation, and to allow comments between a macro name
  and its bang (issues #146, #147).
- Changed `path::basename` and `path::dirname` to publish `fin_path_basename`
  and `fin_path_dirname`, so a libc symbol of the same name cannot preempt them
  and hand back borrowed storage.
- Changed Windows paths to recognize `\` as a separator, along with
  drive-relative and root-relative prefixes and complete UNC shares; joins and
  normalization keep emitting `/`.
- Changed the stage compiler to preserve the signedness of an explicit
  `cast<long>` from a negative `int` instead of zero-extending it.

### Fixed

- Fixed `walk_dir` to use `opendir`/`readdir`/`closedir` and branch per record
  shape, removing the `getdirentries*` dependency that failed to link on musl
  and misread the records macOS fills.
- Fixed `fs::copy_file` to copy byte for byte including embedded NUL bytes, to
  support copying a file onto itself, and to refuse an incomplete read or write
  instead of reporting success.
- Fixed `{K, V}` prototypes with string keys: lookup, store, `delete p[key]`
  and `p.rm(key)` now compare key contents, so separately allocated equal strings
  address the same entry (issue #148).
- Fixed the Windows filesystem and logging gaps: the two `Soundness_Fs` and five
  `Soundness_Log` platform skips are gone and those tests now execute on
  Windows.
- Fixed Windows links to pass `-lws2_32` rather than a bare `ws2_32.lib`
  filename, and `sync` cleanup to select the SRW-lock path instead of calling
  `pthread_mutex_destroy`.
- Fixed the Windows installer step to pass Inno Setup switches unrewritten, and
  the macOS disk image to place `finc-<version>-<target>/` at the volume root,
  so both native installers are produced and proved by running what they install.

### Removed

- Removed the named Windows refusals from `fs`, `log`, `sync` and `time`, whose
  headers declared the platform unimplemented and let the link fail; they now
  carry native declarations instead.
- Removed the fixed-size tables of the Linux-only `cli::std` argument reader
  (a 4 KiB argv copy, 256 arguments, 32 options, 64 positionals); the tables now
  grow with the input.

### Security

- Weak smart-pointer handles now keep the shared counters alive after the last
  strong release, so `wptr::is_alive()` can no longer read freed memory; released
  handles blame instead of trusting a stale counter (ADR 0039 amendment).

### Upgrading from 0.4.0

No language-level breaking change was found between `v0.4.0` and 1.0.0: the
C++ grammar (`src/parser/parser.y`, `src/lexer/lexer.l`) is unchanged, no sample's
`//@` expectation changed, and the machine contract is 3 on both tags. A
public-API comparison of `lib/std` between the two tags adds names and removes no
importable one; `rand` changed from a Fin wrapper to a direct export of the C
function but is still imported as `rand`. One library *type* change can break
source:

- `networking::std::Socket.fd` is now `SocketHandle`, which is `int` on POSIX and
  `ulong` on Windows, so code that assumes `int` may need a cast there.

Behavior, but not compilation, also changed: `random_int` draws three 32-bit
words instead of one, so a seeded sequence of `random_int` results differs from
0.4.x; `cli::std` now treats `--` as the end of options; and `BTreeMap`,
`Collection` and `Instant::deadline_after_ms` blame where 0.4.x called a null
pointer, over-allocated, or wrapped a deadline.

[Unreleased]: https://github.com/M1778/Fin/compare/v1.0.1...HEAD
[1.0.1]: https://github.com/M1778/Fin/releases/tag/v1.0.1
[1.0.0]: https://github.com/M1778/Fin/releases/tag/v1.0.0
