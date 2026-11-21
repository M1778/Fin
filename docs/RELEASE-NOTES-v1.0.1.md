# Fin 1.0.1

Released 2026-11-20. `finc --version` answers `finc 1.0.1 (contract 3)`.

This is the first 1.0.x release that is safe to install on every platform it
ships for. [1.0.0](https://github.com/M1778/Fin/releases/tag/v1.0.0) stays
published and marked prerelease: its glibc Linux x86_64 binaries crash on
startup, for a reason explained below.

## Read this first if you installed 1.0.0 on Linux

The `x86_64-unknown-linux-gnu` archives, `.deb` and `.rpm` from 1.0.0
segfault before `main`. Not on the machine that built them — on yours.
The musl build, and every aarch64, macOS and Windows artifact, was fine.

The cause is a link-time property, not a Fin bug:

- Referencing data owned by a shared library (`libLLVM`) from a non-position-
  independent object produces `R_X86_64_COPY` relocations. The executable
  reserves writable storage and the loader copies the library's initial value
  in.
- Whether that storage lands inside `PT_GNU_RELRO` depends on how the library
  was built. When it does, the page is made read-only before libLLVM's own
  static initializers write to it, and the process dies during dynamic linking.
- A binary built against one `llvm-22` package therefore breaks on a host
  carrying another. Release CI runs the artifact on the machine that built it,
  which is the one machine where this cannot happen.

`fin_core` is now built as position-independent code, so those symbols are
reached through the GOT. COPY relocations went from 29 to 2, and both survivors
are libc's `stdout` and `stderr`, whose layout is a stable ABI.

Proof, in both directions: a build inside an Ubuntu 24.04 chroot runs on the
build host, and a build here runs inside that chroot. A no-PIC control from the
same tree still segfaults on the build host, so the test can detect the defect.
Full write-up: `docs/known-issues.md`.

The release pipeline now refuses to ship an ELF carrying a non-libc COPY
relocation, which is a property of the binary rather than of the machine
running it.

## Standard library correctness

Four defects, all reachable from public API, all found by writing the
assertions the modules' headers already promised.

- **`math::isqrt` looped forever** on large inputs. `(guess + 1) * (guess + 1)`
  overflowed, the negative product compared `<= value`, and the loop never
  terminated. It now compares `guess + 1 <= value / (guess + 1)` — the same
  predicate without the overflow.
- **`math::gcd` returned a negative value** for `gcd(-2147483648, 0)`,
  contradicting its own header. `gcd` and `lcm` compute in `long` and blame when
  the result cannot be represented.
- **`argparse::parse` could crash three ways**: a count larger than the array
  read past it, a negative count was accepted as a successful empty parse, and
  a null element reached `strncmp`. Each now refuses by name.
- **`json_encode_float` emitted `inf` and `-nan`**, which are not JSON —
  `json.loads` rejects both. A non-finite double now blames and names the
  value instead of substituting `null`, because a null would silently turn a
  computed number into absence.

Behaviour changes worth noting: `lcm` now blames where it previously wrapped,
and non-finite floats now blame where they previously produced a document no
parser accepts.

## Diagnostics you can key on

The contract documented a `code` field that was null for almost everything:
only a codegen refusal and a type mismatch were identified. Coverage is now
**34 codes** across seven passes — backend, types, names, declarations,
operations, modules, ownership — numbered by the pass that raises them.

`finc --explain <code>` prints the long form; `finc --explain list` prints the
inventory; an unknown code is a command-line error with the contract's JSON
shape. `docs/diagnostics.md` is generated from the same registry the compiler
emits from, and every documented example is compiled by the test suite.

Families whose messages overlap another family's phrasing were **left
uncoded**, because a code that steals another's messages is worse than none.

## Editor support

TextMate, highlight.js and Pygments grammars under `editors/`, all generated
from one word list derived from the lexer, so a new keyword fails generation
rather than silently losing its colour.

This release also fixes a defect that had been live since the grammar was
written: `repository.comment` was declared as an array, which the real TextMate
engine silently ignores. **Comments and strings never highlighted in any editor
consuming the shipped grammar.** The generator, the grammar and the validator —
which had mirrored the same wrong assumption — are all corrected.

Two editors ship separately and are built on the diagnostic contract above rather
than on a second analyser, so a hover and a squiggle cannot disagree:

- **`fin-vscode`** — highlighting, live diagnostics, formatting, hover and
  completion from the standard library's own sources, an outline, and snippets.
  Packs to a `.vsix`; not on the Marketplace yet, which needs a publisher token.
- **`fin-lsp`** — the same support as a standalone Language Server, so Neovim,
  Helix, Emacs and Zed get it too. Dependency-free, one `npm pack` tarball.

Both were verified against this compiler rather than against a description of it,
and the work found two contract facts that had never been written down:

- `--diagnostics=json` writes its JSONL to **stderr**, not stdout. The contract
  says so; the guidance circulating around it did not, and an editor that reads
  stdout sees zero bytes.
- `column` is a **UTF-8 byte** offset, not a character offset. After a non-ASCII
  character every naive `column - 1` is wrong, and the squiggle lands to the left
  of the word. The contract now states this and a test pins it.

`finfmt`, which both tools use, was not in any release before this one — see
below.

## Toolchain and operations

- `install.sh` resolves the newest **published release** rather than the newest
  tag. Previously it would happily compile `v0.4.0`, a tag whose release never
  published and whose build the notes call a failed revision.
- `INSTALL.md` rewritten around commands that were executed, with the
  prerequisites the scripts actually check and the PATH step whose absence is
  the most common first failure. `docs/troubleshooting.md` is built from
  failures reproduced on a real host, with the real text pasted in.
- Release artifacts gain CycloneDX SBOMs and SLSA build provenance.
- A compiler threat model (`docs/threat-model.md`) states what is untrusted and
  what is mitigated, saying "no mitigation today" where that is true. Two
  findings it records rather than hides: comptime evaluation has a depth bound
  but no expansion budget, and import resolution is unconfined.
- `LICENSE` now carries the GPLv3 section 14 notice granting
  `GPL-3.0-or-later`, matching what 254 source headers already said. Every
  archive and package carries the license text, which none did before.
- The performance job compares against a checked-in baseline with a machine
  fingerprint, and **skips rather than inventing a verdict** when the hardware
  differs. The threshold is 20%, derived from a measured 10.3% worst-case
  deviation on an idle machine.

## Compiler fixes this release carries

Four defects, each found by an ecosystem repository trying to use the compiler for real work
and each reported with a reproduction rather than a complaint.

- **A null `[char]` handle segfaulted before `main`.** `let a <[char]> = null;` lowered
  through the string-to-array conversion, which measured the bytes with `strlen(s)`. The
  emitted object called `strlen` with `%edi` zero. A security library that validates input
  could not so much as declare a null handle. A constant null now returns the zero pair with
  no call emitted; a non-constant one is measured inside the non-null branch, because LLVM is
  free to hoist a call above the compare that guards it and does — which is why the first
  version of this fix still crashed.
- **`-lLLVM-22` was named on every link.** A reference-compiled object references no LLVM
  symbol (`nm -u hello.o` shows `printf` alone), so the flag bought nothing and broke
  `finc -o app` on any machine without the LLVM *development* package — most machines. The
  library is now named only when an object being linked actually mentions it. Proven with a
  `FIN_CC` wrapper that rejects `-lLLVM*` and exits 1, which is what such a machine does.
- **An imported `const` could not be read.** `declareGlobals` walked only the root program,
  so a module-scope `let`/`const` never became an LLVM global and importing one failed with
  `the name 'PREC_OR' is not lowered yet`. That made 168 `pub const` declarations in
  `lib/std` unusable outside their own file — `parse`'s precedence table, `token`'s kinds,
  `ascii`'s classes. The root is walked first, then each module, first-wins.
- **`finfmt` was never installed.** The formatter was built and tested and
  `install(TARGETS ...)` named only `finc`, so no archive, package or installer has ever
  contained it. It now installs to `bin/` beside `finc`.

Two more are documented rather than fixed, because both are language gaps rather than defects
in a 1.0.x patch's reach. Neither is new in this release; both are recorded here because a
reader deserves to know they exist:

- **A `&char` parameter cannot be indexed with a literal.** `info[4]` on a `[char]` lowers;
  the same index on a `&char` parameter is refused with `this index expression is not lowered
  yet`, while `&info[0]` on an array works. It pushed a cleanup in `fin-ssg` into a worse
  signature, which is how it was found. See `docs/known-issues.md`.
- **Hex literals do not exist.** `0x10` lexes as `0`, then `x10`, and the parser reports
  `expected ';'`. Code that reads naturally with hex constants has to spell them in decimal
  and keep the hex form in a comment.

If you maintained a workaround for any of the first three — a `FIN_LDFLAGS` override, a null
guard at every call site, a module-level value passed as an out-parameter — it can go.

## Known limitations

Unchanged from 1.0.0 unless listed above.

- `fields_of` / `FieldInfo` is not shipped; the module is a signature-only
  marker. See ADR 0057.
- `derive_eq` and `derive_encode` are gated on an owner decision (plan doc §4).
- Struct `==` stays declared-only and is never synthesized (ADR 0036).
- The self-hosted stage backend has three known silent miscompiles, pinned by
  tests and documented in `docs/STAGE-COVERAGE.md`: a constructor returning
  `new S{...}` drops to null, `format!` on a string value prints the pointer,
  and a fixed-array field read inside a method is refused. A fourth is recorded
  in this release: passing `main`'s args to a callee loses the array length.
- Self-hosted stage agreement is proven on Linux only. macOS and Windows prove
  the reference compiler; the stage driver has no portable link line yet.
- On macOS, a NUL byte in source is reported as the following syntax error
  rather than being named. Observed, not root-caused; see `docs/known-issues.md`.
- Release artifacts are **not signed**. Integrity rests on per-asset `.sha256`
  sidecars and `index.json`; authenticity rests on GitHub TLS and the tag's
  commit. `SECURITY.md` says so plainly.

## Installing it with `finn`

`finn 1.0.1` installs this release, and this release fixes two defects that
made that impossible to trust before:

- `finn download` with no version read `/releases/latest/`, which GitHub never
  resolves to a **prerelease** — so it reported "No finc release has been
  published yet" about a release anyone could download in a browser. It now
  asks the releases API for the newest published release, prefers a stable one
  when both exist, and still reads a named version's tag index directly, which
  needs no API call.
- `finn download` used to exit 0 after unpacking the 1.0.0 glibc archive, a
  binary that segfaults. It printed a warning and left the toolchain installed,
  where the next `finn build` would select it and die in a subprocess with no
  explanation. A toolchain that cannot answer `--version` is now refused and
  removed, and the error names the broken target.

`finn download --source [--git-ref <ref>]` builds the compiler from this
repository with `build.sh` for a triple with no published artifact, and installs
it in the same layout so nothing downstream can tell the difference.

If you already have a 1.0.0 toolchain from `finn`, remove it — it is the broken
artifact, and `finn download` will not replace it while it is the newest one
installed. Ask for the version explicitly:

```sh
rm -rf ~/.finn/toolchains/1.0.0
finn download 1.0.1
```


## Install

Prefer the artifact for your platform from this release's assets. Every one has
a `.sha256` sidecar; `index.json` is the authoritative list and is what `finn`
reads. Verify before running:

```sh
sha256sum -c finc-1.0.1-<target>.tar.gz.sha256
```

Or build from source with a pinned ref, after reading the script:

```sh
curl -o install.sh https://raw.githubusercontent.com/M1778/Fin/master/install.sh
sh install.sh                     # newest published release, or it refuses
```

Platform details, prerequisites and failure modes: [`INSTALL.md`](../INSTALL.md).
