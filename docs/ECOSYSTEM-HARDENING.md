# Fin ecosystem: production hardening

A record of what was changed, what was proven, and what is still open, across the compiler,
the package manager, eight standard-library projects, and two editor tools.

Written to be read by someone who was not here. Every claim names its evidence; where there
is no evidence, the entry says so.

---

## The compiler and its releases

| | State |
| --- | --- |
| `finc 1.0.1`, contract 3 | Source complete; local suite **3,269/3,269** |
| v1.0.0 | Published, **marked prerelease**, kept as a record |
| v1.0.1 | Gate-first: tagged only after CI is green on the commit |
| v1.0.0's defect | glibc x86_64 segfaulted on every host but the builder |

v1.0.0 is not being repaired. Rewriting a published artifact is how two different binaries
end up with one checksum. Its release page names the broken assets and points at 1.0.1.

### What 1.0.1 fixes that was found by running the artifacts, not by reading the code

Both defects below passed every check the release pipeline had, because the pipeline runs the
binary on the one machine where its dependencies exist.

1. **The glibc x86_64 artifacts segfaulted.** Referencing libLLVM's data from a
   non-position-independent object produced `R_X86_64_COPY` relocations, some landing inside
   `PT_GNU_RELRO`; the loader made the page read-only before libLLVM's static initializers
   wrote to it. `fin_core` is now `POSITION_INDEPENDENT_CODE ON`. COPY relocations went 29 → 2,
   and both survivors are libc's `stdout`/`stderr`.

   Proven in both directions, with a no-PIC control that still crashes, and the pipeline now
   refuses to ship an ELF carrying a non-libc COPY relocation.

2. **The macOS artifacts link LLVM by absolute Homebrew path.**

   ```
   aarch64-apple-darwin -> /opt/homebrew/opt/llvm@22/lib/libLLVM.dylib
   x86_64-apple-darwin  -> /usr/local/opt/llvm@22/lib/libLLVM.dylib
   ```

   On a Mac whose Homebrew is elsewhere, `finc` does not start: dyld aborts before `main`.
   The same is true of the glibc artifacts, which need `libLLVM.so.22.1`.

   **This is documented, not fixed.** The site and `INSTALL.md` now carry a table saying which
   artifact needs what; the static musl build (93 MB, `static-pie`) and the Windows builds need
   nothing. A new release gate, `.github/scripts/check_runtime_deps.py`, reads `DT_NEEDED` out
   of the shipped binary and refuses a non-system dependency the documentation does not promise
   — verified to exit 1 on a binary patched to need `libbadlib.so.9`.

   Making the macOS builds self-contained is a real piece of work (static LLVM on darwin) and
   is **not** done. Naming it is the honest interim state.

### Other compiler-repo work

- The Windows `test_stdlib` failure was `nul.bin`: `nul` is a reserved device name in every
  directory, with any extension, so the file was never created and the cleanup assertion failed
  for a reason unrelated to the test. Renamed; both Windows lanes went green.
- The MSVC build rejected `HANDLE out` redeclared as `const std::string out` in one scope.
  MSVC was right and GCC accepting it was the bug in our favour.
- `docs/stdlib-index.json`: 49 modules, 543 public members with signatures and doc comments,
  generated from `lib/std/*.fin` by `tests/tools/gen_stdlib_index.py`, `--check`ed in CI. This
  is what editor tooling reads instead of embedding a Fin parser.
- Site: added a downloads section (the site previously had **no link to the releases page at
  all**), an honest "what is verified" callout, an editor section, and a correction to the
  stdlib page, which described itself as the whole library while documenting 20 of 49 modules.
- Issue templates for bugs and proposals; security routed to private advisories.
- 9 open PRs → **0**; 9 remote branches → **2**.

---

## `finn`, the package manager

Two user-facing defects, both found by pointing it at the real release instead of a fixture.

1. **`finn download` said no release had been published while one was downloadable.**
   It read `https://github.com/M1778/Fin/releases/latest/download/index.json`, and GitHub's
   `latest` shortcut never resolves to a prerelease — which every Fin release is. Now:

   - no argument → asks the releases API for the newest **published** release, sorted by
     version, preferring stable over prerelease;
   - an explicit version → reads that tag's index directly, so no API call and no rate limit;
   - a release that exists without an `index.json` is named as incomplete rather than reported
     absent.

2. **`finn download` exited 0 after installing a compiler that segfaults.**
   It unpacked the broken v1.0.0 glibc binary, warned, and returned `Ok`. `find_compiler` then
   selects the newest toolchain directory without asking it anything, so the next `finn build`
   died in a subprocess with nothing pointing at the toolchain.
   A toolchain that cannot answer `--version` is now **refused and removed**, and the error names
   the broken target and the version to try instead. Verified against the real v1.0.0 artifact.

Also added: `finn download --source [--git-ref REF]`, which clones the compiler repository and
runs its own `build.sh` — proven end to end, producing `finc 1.0.0 (contract 3)` that compiles
and runs a program with the stdlib. Header on all 43 source files, checked in CI. `CHANGELOG.md`.
A `release-smoke` CI job that installs the newest published release and builds a program with it
on Linux, macOS and Windows — the chain a user actually walks, and the one that would have
caught v1.0.0 at its first step.

198 tests pass; `clippy --all-targets -D warnings` and `fmt --check` are clean.

---

## The eight libraries

Each was hardened in its own repository. The pattern is the same: fix what is actually broken,
prove it by compiling and running, header every file, and make the README say what the library
does **and does not** do.

| Repository | The work that mattered |
| --- | --- |
| `finson` | Bounded parsing: depth limits enforced rather than documented, malformed input rejected, owned values leak-free on error paths |
| `xtest` | Portable regression tests; JUnit output checked for well-formedness; the framework made to fail when it should |
| `zhttp` | Socket reads/writes at an offset, loopback tests served from a thread, bounded reads against hostile input |
| `fin-crypto` | Windows secure randomness; test vectors; a corrected claim about a compiler blocker (see below) |
| `fin-kv` | Safe Linux file flags on x86-64 and ARM64; POSIX-only status stated honestly |
| `fin-validate` | Precise, reusable validation with explicit result ownership; 10,000 failure/success reuse cycles |
| `fin-ssg` | Deterministic output, escaping, portable smoke suite |
| `logg` | Windows/macOS logging regression runs without POSIX assumptions; stream-safe escaping |

### One agent found a bug that did not exist, and that is worth recording

The `fin-crypto` agent reported that the compiler "does not predefine `FIN_TARGET_WINDOWS`",
concluding Windows support was blocked upstream. It measured Fin at `1f7afd9`, which predates
`cc7f166` — the commit that added the target macros. On the shipped compiler:

```
$ finc tm2.fin -o tm2 && ./tm2
linux defined
x86_64 defined
```

The claim was corrected rather than shipped. A false upstream-blocker is worse than silence:
it tells a user not to try a platform that works.

The general lesson, which applies to the whole ecosystem: an agent measuring "the current
compiler" needs to say **which revision**, because an ecosystem repository can be pinned to a
compiler older than the one in the working tree.

---

## Editor tooling

Two new repositories. Neither is merged, published, or claimed as done.

| | Scope |
| --- | --- |
| `fin-vscode` | TextMate grammar (from the canonical `editors/` copy), diagnostics from `finc --diagnostics=json`, `finfmt` formatting, hover/completion/document symbols from `lib/std`, snippets, settings, packaging to `.vsix` |
| `fin-lsp` | A standalone Language Server, so the same support reaches Neovim, Helix, Emacs and Zed |

The contract both build on is fixed and verified:

```
$ finc e.fin --diagnostics=json --color=never
{"kind":"diagnostic","severity":"error","code":"E0301","message":"Undefined function or type 'undefinedname'","file":"e.fin","line":2,"column":5,"endLine":2,"endColumn":21,"help":null,"attribution":null}
{"kind":"summary","errors":1,"warnings":0,"exitCode":1,"status":"failed"}
```

`line`/`column` are 1-based, `endColumn` is exclusive, stdout carries only the JSON stream, and
CRLF files report the same numbers. Both tools were given these facts, the `--explain` mechanism
for all 34 diagnostic codes, and the new stdlib index as the hover source.

The most important requirement in that brief: **a linter that silently reports zero problems
when its compiler is missing is worse than no linter.** Both were told to fail loudly instead.

---

## CI for the ecosystem

`tests/tools/install_finc.py` (deployed as `.github/scripts/install_finc.py`) installs a pinned
release instead of building the compiler from source: it reads the release index, verifies the
sha256, unpacks `bin/finc` + `lib/std`, and refuses a binary that cannot answer `--version`. On
Linux it takes the **static musl** build, so no LLVM runtime is needed on the runner.

Verified on a real release: sha256 matched, `finc 1.0.0 (contract 3)` installed, and it compiled
and ran a program importing `strings::std`.

`FINC_VERSION` in each repository pins it. Until v1.0.1 is published it must say `1.0.0`; a
version that does not exist is a 404 at the first CI step, which is at least a loud failure.

---

## Open, and deliberately not settled here

- **macOS/glibc artifacts are not self-contained.** Documented and gated; making them static is
  real work and an owner decision about artifact size (the musl build is 93 MB).
- **Imported `const` lowering.** 168 constants across `lib/std` are unusable because
  `declareGlobals` iterates only `program.statements`. Needs a ruling on duplicate-name
  precedence (ADR 0048).
- **The `strings::parse_float` stage defect.** Documented in `docs/known-issues.md`; the stage
  invents type errors after a `stdio` import. Ships documented, not fixed.
- **The fuzzer does not build under Clang** (`ComptimeInterp.hpp` needs `Value` complete), filed
  as #159. "We fuzz the parser" is currently false.
- **`fin-vscode` and `fin-lsp` are unmerged.** Neither is installable yet, so neither is
  documented on the site as existing.
- **v1.0.1 is not tagged.** The gate is a green CI run on `1e6273c`; the tagging command is in
  the release notes' handoff section.
