# Licensing

Fin — the `finc` compiler, its standard library (`lib/std/`), and the tooling in
this repository — is licensed under the **GNU General Public License, version 3
or (at your option) any later version**. The SPDX identifier for that grant is
`GPL-3.0-or-later`.

This page records what that means, where it is declared, and what a downstream
packager has to do. `tests/tools/check_licensing.py` enforces every claim below.

## Why "or later", and the evidence for it

GPLv3 §14 is the only clause that permits a licensee to follow a later version:
a work gets that option **only if it carries a notice granting "either version
3 of the License, or (at your option) any later version"**. The license text
alone — the 674-line `LICENSE` every GPL program copies verbatim — does not
grant it; the boilerplate under "How to Apply These Terms" is a template, not a
statement about this work.

Until 2026-11-20 `LICENSE` in this repository *was* only that verbatim text. The
218 source files that already carried a header said `GPL-3.0-or-later` (issue
#85), the packaging said `GPL-3.0-only`, and neither was backed by anything: no
notice meant the file granted version 3 only, which made those 218 headers
false.

The intent is not a guess. The commits that added those headers say what they
were doing, on the record:

- `7f064ea` ("SPDX license headers for every src source", 2026-10-18):
  "GPL-3.0-or-later, per the LICENSE's 'or any later version' variant."
- `4522668` ("SPDX headers for the shipped Fin surface"): the same identifier
  across `finc/*.fin`, `lib/std/*.fin` and `tools/*/main.cpp`.

The author believed LICENSE carried the notice; it did not, because the notice
above the GPL text had never been written. `README.md` says only "GNU General
Public License v3", which is compatible with either reading and settles nothing.
So the declared intent was "or later" and the file was the half that was wrong.
Adding the notice is the smaller change and the one that matches what every
source file already said. It is not retroactive cover: `v0.4.0` (tagged
2026-10-20, two days after the headers) ships those headers and the notice-less
LICENSE together, so its binaries were conveyed under the headers' claim while
`LICENSE` in the same archive granted version 3 only. Closing that gap now is
the point of this change; it does not rewrite how v0.4.0 shipped.

The reverse — deciding "version 3 only" now — is not a neutral relabel: it would
narrow the grant the tree has been advertising since 2026-10-18, for every
recipient after the change, and would need 254 headers rewritten to match. See
the alternative below; only the copyright holder can make that call, and it is
not something a licensing fix should decide by convenience.

So the notice was added rather than the headers rewritten:

- `LICENSE` now opens with a grant notice naming the copyright holder, the SPDX
  identifier, and the §14 "or any later version" paragraph, followed by the
  unmodified GPLv3 text. Adding a notice is how a GPL work states the "or later"
  option; it does not alter the license text itself.
- The packaging metadata (`GPL-3.0-only` in the RPM spec and PKGBUILD) was
  corrected to `GPL-3.0-or-later`. No source file was changed for this.

**Confirm this with the copyright holder before the release ships.** Adding the
notice grants more than the bare GPLv3 text did, which the holder may do at any
time, and it cannot take back rights already granted to recipients of published
archives. It is still a licensing decision recorded in a legal document, so it
wants an explicit yes rather than an inference from commit messages.

The alternative — `GPL-3.0-only`, with the notice deleted and 218 headers
rewritten — was rejected because it would have narrowed a grant that shipped
releases already made. It is also the option that would require a mass rewrite:

```sh
git ls-files -z 'src/*.cpp' 'src/**/*.cpp' 'src/*.hpp' 'src/**/*.hpp' \
  'src/lexer/lexer.l' 'src/parser/parser.y' 'finc/*.fin' 'lib/std/*.fin' \
  'tools/*/main.cpp' 'tests/*.cpp' 'tests/*.hpp' \
  | xargs -0 sed -i \
      's|SPDX-License-Identifier: GPL-3\.0-or-later|SPDX-License-Identifier: GPL-3.0-only|'
```

(254 headers today; the substitution leaves `lexer.l`'s line inside its `%{
block, so the diff is one line per file). Then change `SPDX` in
`tests/tools/check_licensing.py`, `License:` in `build-rpm.sh`, `license=()` in
`PKGBUILD`, delete the §14 paragraph from `LICENSE`, and rewrite this section.
If a future owner decides version 3 only is what they want, that is the change
to make — and it is a narrowing, so it needs their explicit decision rather than
a maintainer's convenience.

## Where it is declared

| Place | Declaration |
| --- | --- |
| `LICENSE` | Grant notice (`SPDX-License-Identifier: GPL-3.0-or-later` plus the §14 paragraph) and the full GPLv3 text |
| Every shipped source file | `// SPDX-License-Identifier: GPL-3.0-or-later` as the first line |
| `tools/packaging/build-rpm.sh` | `License: GPL-3.0-or-later` in the generated spec |
| `tools/packaging/PKGBUILD` | `license=('GPL-3.0-or-later')` |
| `CMakeLists.txt` | `install(FILES LICENSE DESTINATION lib/finc)` |
| `tools/packaging/finc.iss` | `LicenseFile={#RepoRoot}\LICENSE` (shown by the Windows installer) |

The header set the checker covers is `src/**` (`.cpp`, `.hpp`, `lexer.l`,
`parser.y`), `finc/*.fin`, `lib/std/*.fin`, `tools/*/main.cpp`, and
`tests/*.cpp` / `tests/*.hpp`. Documentation, `tests/samples/**`,
`tests/contract-corpus/**` and `fintest/**` are not part of the licensed program
surface and carry no header; a new source file added to the tree belongs in that
list in `tests/tools/check_licensing.py` and needs the same one-line header.

## Where the license text ships

GPLv3 §4 requires that every recipient of a conveyed copy also receive a copy of
the license. Every artifact therefore carries `LICENSE`. Verified by running
each packaging script against a stage built with `cmake --install`:

| Artifact | Destination |
| --- | --- |
| `cmake --install` staging tree | `lib/finc/LICENSE` |
| Portable archive (`.tar.gz`, `.zip`) | `lib/finc/LICENSE` inside the archive |
| `finn` toolchain install | `<toolchains>/<version>/lib/finc/LICENSE` (the whole staged tree is moved) |
| `.deb` | `/usr/share/doc/finc/copyright` |
| `.rpm` | `/usr/share/licenses/finc/LICENSE` (declared `%license`) |
| `.dmg` | `finc-<version>-<target>/LICENSE` beside `bin/` and `lib/` |
| Windows `.zip` | `lib/finc/LICENSE` inside the archive |
| Windows setup `.exe` | shown in the wizard (`LicenseFile=`), not installed into `{app}` — see the open item below |

The staging destination is `lib/finc/`, not `share/doc/finc/`, because the
portable archive is assembled as `tar -C stage bin lib` — a `share/` path would
be installed and then silently left out of the archive. `lib/finc/` is inside
the tree that travels, and no library search path reads it, so the compiler's
`<exe-dir>/../lib/std` rule and the module lookup order are unchanged.

The portable-archive layout is otherwise what it was (`bin/finc[.exe]` plus
`lib/std/**`, no version directory); `.github/workflows/release.yml` asserts
that layout, and `finn`'s extraction checks `bin/finc` and `lib/std/` before it
installs a toolchain. `lib/finc/LICENSE` is additive to both: the archive step is
`tar -C stage bin lib`, which carries it, and neither assertion forbids extra
entries.

`finc.iss` (Windows) and `PKGBUILD` (Arch) consume the same staged tree, so a
packager who rebuilds with these scripts gets the license without a second
copy to maintain; the .deb and .rpm relocate it because Debian and RPM policy
name a different path.

## Flagged, not fixed here

- **The Windows installer does not install the license text.** `finc.iss` shows
  it (`LicenseFile={#RepoRoot}\LICENSE`, a wizard page) but its `[Files]` section
  copies only `bin\finc.exe` and `lib\std\*.fin` into `{app}`, so an installed
  tree has no `LICENSE` on disk. Showing the license satisfies GPLv3 §4's
  "give all recipients a copy" only arguably — the safe fix is one `[Files]`
  line: `Source: "{#StageDir}\lib\finc\LICENSE"; DestDir: "{app}\lib\finc"; Flags: ignoreversion`.
  `finc.iss` is outside this change's scope (it is owner-reviewed with the
  release workflow), so it is recorded rather than edited.
- **`PKGBUILD` still needs a real checksum per release.** `sha256sums` now holds
  the placeholder `REPLACE_WITH_THE_SHA256_FROM_THE_RELEASE_SIDECAR` instead of
  `SKIP`, so makepkg refuses to build until a maintainer pastes the value from
  the release's `.sha256` sidecar (the `curl` command is in the PKGBUILD). No
  v1.0.0 release asset exists yet (`releases/download/v1.0.0/...` returns 404),
  so there was no authoritative hash to compute here and inventing one would
  have been worse than the placeholder.
- **The RPM was proven by stubbing `rpmbuild`.** `rpmbuild` is not installed on
  this host, so the spec text (`License:`, `%license`, the `/usr/share/licenses`
  install line) was verified from the generated spec; the built `.rpm` itself
  remains proven only by the release job's `rpm -qlp`/`rpm2cpio` steps.
- **One untracked sibling checker now fails on the added entry.**
  `tests/tools/verify_artifacts.py` (new, not tracked yet, owner: release/
  supply-chain) asserts an *exact* tree in `check_layout()`: every path that is
  not `bin/finc` or `lib/std/*.fin` is reported as "unexpected". Both new entries
  are exactly that, so `verify_tarball`, `verify_zip`, `verify_deb`, `verify_rpm`
  and `verify_dmg_tree` go red on the next release set — reproduced here:

  ```
  FAIL  finc-1.0.0-x86_64-unknown-linux-gnu.deb              layout  unexpected: share/doc/finc/copyright
  FAIL  finc-1.0.0-x86_64-unknown-linux-musl.tar.gz          layout  unexpected: lib/finc/LICENSE
  ```

  (`share/doc/finc/copyright` is relative to the `usr/` root that checker passes
  for the .deb and .rpm; the .rpm's `share/licenses/finc/LICENSE` will report the
  same way.) The fix belongs in that file, not here: allow the license entry,
  e.g. extend `check_layout` with an `expect_license` parameter and add it to
  the two exclusions. It was not edited because it is outside this change's
  ownership and its "exactly" wording is deliberate; the license text is the
  thing that must be allowed, not the assertion that must be loosened wholesale.
- **Two prose claims about the archive layout are now one entry short.** They are
  in `.github/workflows/release.yml:9` ("unpacks to exactly `bin/finc[.exe]` and
  `lib/std/**`") and `docs/plan.md:1270` (the same sentence, which was already
  loose: it describes a promise to `finn`, whose extractor requires those two
  entries and asserts nothing about others). The executable assertion at
  `release.yml:264-272` only checks that `stage/bin/finc[.exe]` exists and that
  `stage/lib/std` holds `.fin` files, and the archive step is `tar -C stage bin
  lib`, so `lib/finc/LICENSE` travels without breaking either. Both comments
  should gain `+ lib/finc/LICENSE` when their owners next touch them; this change
  did not edit files outside its scope to do it.

## For a downstream packager

- License the package as `GPL-3.0-or-later`. Do not write `GPL-3.0-only`, and do
  not write `GPL-3` alone if your format resolves that to "version 3 only" —
  the grant is version 3 *or later*.
- Include the license text with the package, in your format's usual place
  (`/usr/share/doc/<pkg>/copyright` on Debian, `/usr/share/licenses/<pkg>/` on
  RPM and Arch, `License` metadata plus a shown file on macOS/Windows). The
  text is already in the archive at `lib/finc/LICENSE`; copy it, do not
  re-derive it.
- If you patch the source, GPLv3 §5(a) requires prominent notices that you
  modified it and a date, and §5(b) requires you to state the license and any §7
  additional terms. Keep the SPDX headers intact.
- Distributing the binary (not just the source) requires conveying the
  Corresponding Source under §6. This repository is that source; keep your
  build scripts' provenance so recipients can rebuild the same binary.
- The bundled `lib/std/**` is part of the same work and the same license. Use of
  the compiler does not affect the license of programs it compiles; the GPL
  covers `finc` and its library, not a user's source or the output of compiling
  it (§2, last paragraph: output is covered only if it is itself a covered work).

## Checking it

```sh
python3 tests/tools/check_licensing.py
```

It fails on: a tracked source file with no SPDX header in its first five lines;
an identifier other than `GPL-3.0-or-later`; a `LICENSE` whose grant notice lost
the §14 paragraph or the identifier; an RPM/PKGBUILD license field that
disagrees; a package script that stopped copying the license text to its
destination; an RPM text entry not marked `%license`; a
`sha256sums=('SKIP')` in the PKGBUILD; or a missing
`install(FILES LICENSE ...)` rule in `CMakeLists.txt`. The CI job
`.github/workflows/ci.yml` runs it on every push, so any of those drifts is a red
build rather than a release.

When the grant itself changes (say `GPL-3.0-only`), the checker is the list of
places that must change together: `SPDX` in the checker, the 254 headers, the two
packaging fields, and the `LICENSE` notice. Anything left behind fails.
