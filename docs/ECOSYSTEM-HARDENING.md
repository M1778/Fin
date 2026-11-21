# Fin ecosystem: production hardening

A record of what was changed, what was proven, and what is still open, across the compiler,
the package manager, eight libraries, two editor tools, and the two downstream consumers.

Written to be read by someone who was not here. Every claim names its evidence; where there is
no evidence, the entry says so.

**Status: v1.0.1 is published**, and every repository below is green. The four compiler defects
found along the way are fixed and shipped; five platform limitations are documented rather than
fixed, and the sections say which is which.

---

## The compiler and its releases

| | State |
| --- | --- |
| `finc 1.0.1`, contract 3 | **Published**, 41 assets, signed tag, 3,278 tests green |
| v1.0.0 | Published, **marked prerelease**, kept as a record |
| v1.0.1 artifacts | Verified by download, not by the build machine |
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

### Verified from the downloads

Each row is a download of the published asset and a command run against it. This is the check
that would have caught 1.0.0, which was green because the only machine that ran it was the one
that built it.

| Artifact | Result |
| --- | --- |
| `x86_64-unknown-linux-musl` | `finc 1.0.1 (contract 3)`, static-pie; compiles a program importing `strings::std` and `parse::std`, so the imported-const fix is exercised in a release binary; `bin/finfmt` present |
| `x86_64-unknown-linux-gnu` | **The artifact that segfaulted in 1.0.0** answers `--version` and compiles the same program; `ldd` shows the documented `libLLVM.so.22.1` |
| `.deb` | `Depends: libllvm22` — the runtime is pulled in rather than assumed |
| `aarch64-apple-darwin` | Load command is `/opt/homebrew/opt/llvm@22/lib/libLLVM.dylib`, exactly what `INSTALL.md` says |
| `finn download` (no arguments) | installs 1.0.1; `finn init` → `finn build` → the program runs |
| `finn`'s release-smoke CI | green on Linux, macOS and Windows against the published release |

The macOS artifacts were **inspected, not executed** here: this host cannot run a Mach-O
binary. Their dependency list matches the documentation, and finn's release-smoke job on a real
macOS runner is what runs them — which is why that job exists.

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

All eight are green on Linux, macOS and Windows, and each lane executes tests rather than
type-checking.

| Repository | The work that mattered |
| --- | --- |
| `finson` | Bounded parsing: depth limits enforced rather than documented, malformed input rejected, owned values leak-free on error paths; three segfaults fixed; 7,508 differential cases against Python's `json` |
| `xtest` | Returned invalid XML 1.0 text in JUnit attributes and segfaulted on null input; now 6 lanes executing, 224 external checks, and proven to fail when the framework is weakened |
| `zhttp` | Headers could be injected through a value, reads could be short, and there was no request deadline; 118 protocol cases over real loopback traffic |
| `fin-crypto` | FIPS 180-4 and RFC 4231 vectors, Windows `rand_s`, and a corrected claim about a compiler blocker (see below) |
| `fin-kv` | Boundary tests at every frame, key and value limit; byte cap enforced and measured |
| `fin-validate` | An untested `max_len` was the one proven correctness hole (see below); 10,000 reuse cycles; email shape limits |
| `fin-ssg` | Two platform branches that had never been parsed, both with real syntax errors (see below); deterministic output; portable smoke suite |
| `logg` | O0/O2 runs, JSON round-trip parsing of every emitted record, fatal and error subprocess checks |

### fin-ssg: a platform branch nobody had ever parsed

The Windows branch of `src/vendor/platform.fin` had never been compiled by any lane, and it
did not compile. Three defects, all in code that only Windows can see:

- `@define platform_rename(from: string, ...)` -- `from` is a hard keyword, so the lexer emits
  `KW_FROM` before the parser reads a parameter list.
- `if ((attributes & 0x10) != ...)` -- Fin has no hex literals at all. `0x10` lexes as `0`,
  then `x10`, and the parser says `expected ';'`.
- `open_in_directory` and `platform_open_directory` were missing an argument every call site
  passes, and `DirHandle` (a `long` on Windows, an `int` on POSIX) was stored in `int` locals.

Then the macOS branch turned out to name glibc's `__errno_location` for the `errno` accessor,
where Darwin has `__error`, so the whole module failed to link; and `struct stat` offsets were
hardcoded to Linux's layout, which would have answered the wrong kind for every name on macOS.

That last pair is a wrong-value bug rather than a crash, which is the worse kind: a directory
walk that quietly stops early looks like an empty site, not a broken one.

The fix that keeps this from recurring is a script, `tests/tools/check_foreign_branches.py`,
which flips the guards so the other platform's branch becomes the live one, runs the front end
on any host, and restores the file in a `finally`. It runs in CI on every Linux lane. What
used to cost a ten-minute round trip that reported one error at a time is now a local check.

### fin-validate: the one proven correctness hole

A mutation adding 100 to the `max_len` comparison passed the entire suite. `min_len` had a
test and `max_len` did not, so the upper bound of a validation library was never exercised --
the one thing an upper bound exists for. Four boundary cases were added (exactly at the limit,
one over, well over, and a field declared `-1` accepting 5,000 bytes), and the mutation now
fails by name.

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

### A consumer can install and use them

The packaging question the audit raised was whether `finn add` produces a package a program can
actually import. Verified end to end, against the published `finn 1.0.1` and a released
`finc 1.0.1`:

```sh
finn init proj --yes
cd proj && finn add --yes M1778/finson        # --yes because no registry resolves the name
```

then, with `import { ParseResult, json_parse, json_object_get, json_free_result } from
finson::fin;`:

```sh
finn build   -> [OK] Built executable: out/proj
./out/proj   -> ok=1
```

`finn build --verbose` shows what it passes: `--fin-libs <toolchain>/lib/std .finn/packages
.finn/packages/finson`. The package root carries the module, so `finson::fin` resolves without
the consumer naming a path — which was not true before the libraries moved their module out of
`src/`.

One defect found on the way: my first attempt used `parse`, which is not an exported name. The
README's example is correct (`json_parse`); I had guessed. Worth stating because it is the
common failure — an import that resolves to the right module and the wrong symbol reports
`Module 'finson' does not export 'parse'`, which is a good error.

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

## The downstream consumers

`fin-sdk` and `Linf` both work against `finc 1.0.1`, verified rather than assumed:

- **`fin-sdk` re-vendored to 3.3.0** from the v1.0.1 tree
  (`bf7fe48b72babba31aa6ba302bc3e3d395b4ad99`), 23 modules byte-exact with every sha256 in
  `MANIFEST.md` checked against the copied file. The snapshot gained `read_line`, `read_all`,
  `strings::parse_float`, and `math::floor_div`'s named blame. Its `proof/` still reports
  exactly three findings and exits 1.
- **`Linf` 3.0.0** builds and passes **all 55 regression checks and all self-checks**. It was
  already current; a local checkout from October made it look broken, and chasing that was a
  wasted hour worth recording as a trap: check `git fetch` before believing a downstream
  failure.

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
