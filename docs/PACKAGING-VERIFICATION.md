# Packaging verification

What a `finc` release artifact is proved to do, where, and what it is only
proved to *be*. Three verbs, used strictly:

* **built** — a compiler produced bytes.
* **produced** — the release workflow wrote a file with the right name.
* **installed and executed** — the artifact was unpacked the way a user's
  package manager would, and the resulting binary compiled a program that
  imports the bundled standard library and ran it to the expected output.

Only the third is evidence that a download works. `release.yml` reaches it for
some artifacts and only the second for others; this document says which, so a
green run is not read as more than it is.

Run `tests/tools/verify_artifacts.py <directory>` against a downloaded set to
re-check all of it on your host. Every line is `PASS`, `SKIP` (naming the
capability your host lacks) or `FAIL`, and any `FAIL` is a nonzero exit.

## The matrix

Legend: **CI** = proved inside `release.yml` on the release runner;
**local** = re-proved by `verify_artifacts.py` on the host that ran it;
**—** = not proved, only produced.

| Artifact | Layout | `--version` | Compiles + runs a bundled-`lib/std` program | Dynamic deps resolved | ELF layout (`relro-copy`) |
| --- | --- | --- | --- | --- |
| `finc-<v>-x86_64-unknown-linux-gnu.tar.gz` | CI + local | CI, local **FAIL**¹ | local **FAIL**¹ | CI + local | local **FAIL**¹ |
| `finc-<v>-aarch64-unknown-linux-gnu.tar.gz` | CI + local | CI | — | CI | local (no COPY) |
| `finc-<v>-x86_64-unknown-linux-musl.tar.gz` | CI + local | CI + local | **local only** | CI (no `INTERP`) + local | local (no COPY) |
| `finc-<v>-x86_64-apple-darwin.tar.gz` | CI + local | CI | — | — | n/a (Mach-O) |
| `finc-<v>-aarch64-apple-darwin.tar.gz` | CI + local | CI | — | — | n/a (Mach-O) |
| `finc-<v>-x86_64-pc-windows-msvc.zip` | CI + local | CI | — | — | n/a (PE) |
| `finc-<v>-aarch64-pc-windows-msvc.zip` | CI + local | CI | — | — | n/a (PE) |
| `finc-<v>-x86_64-unknown-linux-gnu.deb` | CI + local | CI, local **FAIL**¹ | CI (after this change), local **FAIL**¹ | CI (Depends read off `ldd`) + local | local **FAIL**¹ |
| `finc-<v>-x86_64-unknown-linux-gnu.rpm` | CI + local | CI, local **FAIL**¹ | CI (after this change), local **FAIL**¹ | CI + local | local **FAIL**¹ |
| `finc-<v>-<win>-setup.exe` | CI (silent install) | CI (installed) | — | — | n/a (PE) |
| `finc-<v>-<mac>.dmg` | CI (attach) | CI (from the image) | — | — | n/a (Mach-O) |

"Compiles + runs" means: a program that imports `println_int`/`println_str`
from `stdio::std` is compiled with `-o` **from a directory outside the
install**, executed, and its stdout compared against the expected two lines.
That combination is what makes the check mean something. `-c` alone would
not: it never links. `tests/samples/basic.fin` — what the deb step compiled
before this change — declares two variables and imports nothing, so it passes
equally against a package containing no standard library at all. The
2026-11-20 rehearsal run (44652904633) shows exactly that: its deb step ran
`basic.fin` with `-c`, produced no executable, and its only output about the
installed tree is two `unused variable` warnings and `Build Successful.`

The 2026-11-20 rehearsal predates the deb/rpm proof tightening in this
document; those two "CI (after this change)" cells are proved by the same
command run locally and have not yet run on a release runner.

¹ For 1.0.0 the x86_64-gnu artifacts fail these checks on Arch Linux: the
binary segfaults before `main`. CI passes them because the build host's
libLLVM does not reproduce it. See the open defect below. The rest of the
table records **where a check runs**; the verifier's own output is the
verdict.


## Per artifact class, in detail

### Portable Linux, gnu (`tar.gz`)

Proved in CI: the staged tree is `bin/finc` + `lib/std/**.fin` and nothing
else; the machine field of the binary matches the target triple the archive is
named for (`.github/scripts/check_arch.py`); the binary answers `--version`
and passes `tests/tools/check_docs.py`.

Proved locally: unpack, `bin/finc --version`, then compile and run the smoke
program from a foreign directory, which forces `<exe-dir>/../lib/std`, and
check `ldd` resolves every `NEEDED` entry.

Not proved anywhere: that the distro on which a user unpacks it has the
`libLLVM` major the binary was linked against. `Depends: libllvm22` is a
Debian fact; a tarball user on another distro gets a `ldd` failure at run
time and no earlier warning. This is a real, named limitation rather than a
defect in the archive.

**Open defect, verified 2026-11-20:** the `x86_64-unknown-linux-gnu` binary in
`finc-1.0.0-x86_64-unknown-linux-gnu.tar.gz`, `.deb` and `.rpm` segfaults
before `main` on Arch Linux with `llvm22-libs 22.1.8-2`:

```
$ bin/finc --version
Segmentation fault (core dumped)     # exit 139
```

The cause is the executable's own ELF layout. The link put
`R_X86_64_COPY` relocations for `llvm::OptimizationLevel::O1`, `O2` and `O3`
in `.data.rel.ro`, which `PT_GNU_RELRO` makes read-only at startup. Those are
not constants: a libLLVM that initialises them writes through the executable's
interposed copy, lands on the read-only page, and the process dies before
`main`. Arch's libLLVM 22 does exactly that, and disassembling the faulting
instruction shows the store (`movq $0x1,(%rax)`). Neutralising `PT_GNU_RELRO`
in a scratch copy of the same bytes makes it print `finc 1.0.0 (contract 3)`,
which places the fault in the layout rather than in a missing dependency.

Why CI's `--version` passed: GNU ld mirrors the placement of the symbol in the
library it links against, and Ubuntu's `llvm-22` declares these three
read-only, so the copy landed in `.data.rel.ro` and Ubuntu's libLLVM never
writes them. Measured on the two binaries here, same compiler flags: the
release binary's copies are in `.data.rel.ro`, a locally linked one's are in
`.bss`. So the shipped binary is sound against exactly the libLLVM that
produced it and unsound against any 22.x libLLVM that initialises those
globals — which the `Depends: libllvm22` / `Requires: llvm-libs` line
promises does not matter.

`verify_artifacts.py` reports this twice, deliberately: `relro-copy` reads the
layout statically (so it fires on any host, whether or not that host's libLLVM
reproduces the crash), and `run --version` observes the crash where it happens.
Both fire on the 1.0.0 x86_64 artifacts and agree.

The `aarch64-unknown-linux-gnu` and both musl binaries carry no COPY
relocation at all, so they cannot be bitten by this. The lesson the check
encodes is that "the binary answers `--version` on the machine that built it"
is not a portability proof.

### Portable Linux, musl (`tar.gz`)

Proved in CI: the build happens inside Alpine, `readelf -l` shows no `INTERP`,
the archive layout holds, the arch matches, and `--version` answers. Locally:
no interpreter, no `NEEDED` entries, `--version`, and the smoke program
compiles and runs.

Not proved: that the compiler works on a *musl-native* host. The verifier runs
it on a glibc host, where a static binary executes fine; it exercises the
binary, not the platform. `finc -o` on a musl host would need a musl C driver
for the link step, and no workflow row provides one.

### Linux `deb` / `rpm`

Proved in CI: the package is assembled from the staged tree, its metadata
names the same version and architecture as the file, the payload unpacks to
`/usr/bin/finc` + `/usr/lib/std/*.fin`, and the extracted binary compiles and
runs the bundled-`lib/std` program from a foreign directory. `Depends:` is
read off `ldd` rather than guessed, so a statically linked build gets no
dependency line.

Not proved: an actual `apt install` / `dnf install`, and therefore dependency
resolution, the post-install state, and uninstall. Both are proved by
extraction, which is what a maintainer without root can do; the `dpkg -i`
path needs a root shell on a matching distro and is not in CI.

An `rpm` unpack needs a reader. `rpm2cpio | cpio` is the documented one; the
verifier falls back to `bsdtar` then `7z` and says which it used, or `SKIP`s
with the reason if none is present. On Arch Linux `bsdtar` reads the payload
directly, so the checks are not Linux-distro-specific.

### Windows `zip`

Proved in CI: the archive holds exactly `bin/finc.exe` + `lib/std/**`,
`finc.exe` answers `--version`, its PE machine field matches the triple, and
the docs gate passes.

Proved locally: the zip's CRCs validate, that member set is exact, and the PE
machine field matches the name. Not proved: that `finc.exe` runs, and that a
Windows program it builds links. Neither can happen off Windows.

### Windows `setup.exe`

Proved in CI: Inno Setup compiles the script, then the installer is run with
`/VERYSILENT /DIR=<scratch>` and the installed `bin/finc.exe --version`
executes. That is a real install-and-run proof, and the strongest of any
native installable here.

Proved locally: only that the file is a PE and that its bytes hash to the
published sum. The Inno stub is 32-bit on every architecture by design, so
its own machine field says nothing about the payload; the payload is the
`bin/finc.exe` inside the zip from the same job, which is checked separately.

Not proved anywhere: that the `Path` entry the optional task adds is correct
after an uninstall-and-reinstall cycle, and that a non-silent install works.
The `[Code]` section is checked statically
(`.github/scripts/check_installer.py`), not exercised.

### macOS `dmg`

Proved in CI: the image is built, attached, and `bin/finc --version` runs from
inside the mounted volume, so the `<exe-dir>/../lib/std` rule holds wherever
the volume is mounted.

Proved locally, on a non-Mac host: the image's directory contents can be read
with `7z`; the volume carries one folder holding `bin/finc` + `lib/std/**`,
and the binary is Mach-O for the right architecture. The `dmg` and the
`tar.gz` from the same job carry byte-identical binaries and identical
`lib/std`, which the verifier reports by comparing trees.

Not proved off macOS: the image's own checksum (`hdiutil verify`) and
execution. Both `SKIP` with that reason rather than passing.

## Checksums and the index

`<asset>.sha256` sidecars and `index.json` are checked on every run:

* every sidecar matches its artifact's bytes;
* `index.json` carries the same `sha256` and `size` for every **portable**
  archive, and does not list a native installable (ADR 0010: `finn` must keep
  matching exactly the portable set);
* every portable artifact present is listed, and nothing listed is absent.

`index.json` is generated from the archive bytes, so a sidecar that disagreed
with it would be a release-workflow failure rather than something a reader
has to notice.

## What no artifact here proves

* **Code signing.** No artifact is signed, and neither Windows installer nor
  macOS image is notarized. A user's OS will warn. Signing certificates are
  the reason, and an absent signature is not detected by any check above.
* **A real package-manager install.** Extraction is proved; `apt`, `dnf`,
  Homebrew and `winget` integration are not.
* **The compiler's own correctness.** Every check here compiles one small
  program. The test suite does that work, and a release artifact passing these
  checks does not re-prove it.
* **Anything the host cannot run.** A `SKIP` line is never a pass. Read them
  as the list of what this host could not test.
