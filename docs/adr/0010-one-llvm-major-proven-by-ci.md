# One LLVM major for every platform, and CI is what proves it

`finc` is built against a single LLVM major version — 18 — supplied the same way on every platform,
and a GitHub Actions matrix builds and tests it on every supported platform on every change. The
release archives the package manager downloads are produced by that same matrix.

Today `conanfile.py:10` comments out `llvm-core/19.1.7` with "DO NOT USE FOR LINUX" and `:17-19`
requires it on Windows only, so Linux builds against whatever system LLVM is present — 18.1.3 in the
environment where the build was first made to work — and Windows builds against 19. The two are
different compilers wearing the same version number.

That is tolerable for a hobby build and fatal for the design already committed to elsewhere. `finn`
pins a compiler version per project so that a project compiles the same way everywhere, and
`lib/std/` ships inside the versioned toolchain directory for the same reason. If `finc 0.4.0` means
LLVM 18 on Linux and LLVM 19 on Windows, the pin is a lie on one of them, and it is a lie that only
surfaces once codegen exists — long after the mechanism has been built and trusted.

18 rather than 19 because 18 is what the build is verified against. Changing LLVM majors is a real
change with its own breakage, and doing it in the same step as unifying the platforms would make it
impossible to attribute a failure to either.

CI is part of this decision rather than a separate one because a single-version rule that nothing
checks decays the first time someone builds on a machine with a different LLVM installed. The matrix
is the enforcement mechanism, and it is also where the per-platform release archives and their
sha256 sums come from, so the thing that proves the compiler builds is the thing that ships it.

## Consequences

Every contributor's build acquires a dependency on a specific LLVM, obtained through the manifest
rather than through whatever the system has. That is slower to set up than `apt install llvm-dev` and
it is the property that makes the build reproducible.

Release archives must be named with **both** OS and architecture. `finn`'s `download.rs:62-65`
currently matches assets by OS substring alone, so an arm64 user silently receives an x86_64 build —
a bug that exists today and that the naming scheme has to make impossible rather than merely
discouraged.

CI becomes load-bearing before there is much to test. The suite it runs is four hand-written unit
tests and an auto-discovered corpus where 39 of 50 files fail, so the matrix will start out proving
little beyond "it compiles on this platform". That is still the thing most worth proving, since the
build was broken in this environment until now.

Windows is the platform with no verified build at all. Bringing it into the matrix will surface
breakage that has been invisible — `FIN_LIBS` was split on a hardcoded `':'` in `configureLoader`,
which cannot work for `C:\...` paths — and that breakage has to be fixed rather than excluded, or the
matrix re-establishes the split it exists to remove.

That one is now fixed: the separator is `src/driver/SearchPaths.hpp`'s `kSearchPathSeparator`, chosen per
platform. It is recorded here because of how it was tested. A test that runs `finc` cannot catch this
defect from a POSIX host, where splitting on a colon is correct — so the assertion that fails on the old
code is a direct unit test of the splitter whose Windows branch only executes on the Windows runners this
ADR adds. The matrix is what makes that assertion run at all, which is the argument of this ADR arriving
in the first defect it predicted.

## Amendment, 2026-08-27: the pinned major is 22

The rule is unchanged. The number it names is 22, and `FIN_LLVM_MAJOR` in `CMakeLists.txt` says so.
The 18 above is left standing rather than edited out, because the reasoning that chose 18 is the
reasoning that replaced it: *"18 rather than 19 because 18 is what the build is verified against."*
What this compiler is now developed and verified on is LLVM 22.1.8, so read against the environment
the same sentence selects 22.

Not because 18 is unobtainable. Artix packages `llvm18` at 18.1.8-2 (`pacman -Ss '^llvm'`), so it
could be installed and pointed at with `LLVM_DIR`; a decision resting on "18 cannot be had here"
would be resting on something false. What is true is narrower and enough: 18 is not installed, 22 is,
and the criterion this ADR set is *verified*, not *available*.

The sharper argument is what the 18 pin actually enforced, and it is measured rather than argued. A
pin naming a version that is not present is not obeyed; it is bypassed. The configure check above
hard-errors on a mismatched major, so the only way to get a build on this machine was
`FIN_WITH_LLVM=OFF` — the escape hatch described a few lines above it, meant for a platform that
cannot have the pinned LLVM. With it off, `src/codegen/CodeGen_Stub.cpp` takes the backend's place
and every `BACKEND_TEST` skips. The suite read:

```
1247 tests from 130 test suites ran.  961 PASSED.  1 FAILED.  285 SKIPPED.
```

285 skipped is every codegen test in the repository. The one failure was
`MachineContract.DashOProducesTheNamedExecutable`, and it failed with `finc`'s own words: `error:
codegen: this finc was built without a backend`. So the compiler had no backend, the tests of the
backend did not run, and one test in 1247 said so out loud. That is the decay this ADR was written to
stop, and it arrived *through* the enforcement rather than around it — the configure error worked
exactly as specified, and the option absorbed the consequence. **An escape hatch from a pin nobody
can satisfy is not a fallback, it is the new default.**

The port was measured before the decision, not after: five compile errors in three files, plus one
deprecation. Four of the five are one API change. LLVM took `Target::createTargetMachine` and
`Module::setTargetTriple` from a triple *string* to a parsed `llvm::Triple` — 18 took the string, 22
takes the triple — at one site in `src/codegen/CodeGen_LLVM.cpp` and two in `tests/test_codegen.cpp`.
The two in the tests matter more than their count suggests: they are `TheLayoutTableAgreesWithLLVM`
and `AStructsLayoutMatchesWhatLLVMWouldChoose`, the only two assertions in this repository that ask a
third party whether `finc` is right, so a version port that left them uncompilable would have moved
the pin and switched off the check that the pin is safe. `CreateGlobalStringPtr` is deprecated for
`CreateGlobalString`, which under opaque pointers returns the identical value. Neither call site is
version-forked, and that is this ADR's doing: one major is what makes the current major's spelling the
only one that has to compile.

The fifth error was not LLVM's, and recording it corrects a claim made while the port was being
scoped. `src/macros/expander/ExpanderExprs.cpp` calls `fmt::format` having included only
`<fmt/core.h>`, which from fmt 11 declares only the base API. It was said that a stale object file was
hiding this and that the pre-amendment configuration therefore did not reproduce. That is not the
reason: `conanfile.py` pins `fmt/10.2.1`, whose `core.h` does declare `fmt::format`, so the sanctioned
build never had the error and a from-scratch rebuild of it does not either. The error belongs to a
configure that resolves fmt from a system carrying 11 or newer. Measured four ways — `core.h` +
`fmt::format` fails under the host's fmt 12.2.0 and compiles under Conan's 10.2.1; `<fmt/format.h>`
compiles under both — so the file now includes `format.h`, which is right regardless of which fmt
answers. It is worth a paragraph in *this* ADR because it is the same class of hazard one level down:
the version this project builds against is one fact per dependency, and only LLVM's has an
enforcement mechanism.

After the port, on the machine the pin is now verified against: **1247/1247, 0 failed, 0 skipped**,
zero build warnings; the corpus unchanged at 27 `ok` / 83 diagnostics with 10 of 50 samples lowering
to an object; and `finc hello.fin -o hello && ./hello` runs, which is wave 5's exit criterion and had
never once been met.

### What this amendment does not do

CI is this ADR's enforcement mechanism and CI does not know the pin moved. Until it does, the
single-version rule is enforced on one machine and merely *documented* everywhere else, which is the
condition the ADR was written against. Three sites, none of them changed here:

- **`.github/workflows/ci.yml`** installs bison and flex on every runner and **installs no LLVM at
  all**, and never passes `-DFIN_LLVM_MAJOR`. Since `FIN_WITH_LLVM` defaults to `ON`, each of the six
  jobs configures against whatever LLVM its runner image happens to carry — which is not the
  single-version rule but the "two different compilers wearing the same version number" this ADR
  opens by rejecting. It was already so at 18. The pin's hard error means the matrix now goes red
  instead of quiet, which is the better failure and still not enforcement.
- **`build.sh:27`** defaults `WITH_LLVM=OFF`, and CI's `build-script` job runs `./build.sh --release`.
  The one job whose purpose is to prove the committed build script works proves it for a compiler with
  no backend. It is also the most likely way this working copy came to be configured that way.
- **`conanfile.py:49-67`** still states the decision as "a single LLVM major -- 18 -- on every
  platform", and records that ConanCenter publishes no LLVM 18 and has no `llvm-core` binary for Linux
  armv8, macOS x86_64 or Windows armv8. That is a measurement, and it was taken for 18. It has not
  been re-taken for 22, so this amendment claims no answer for it.

Where the six runners obtain LLVM 22 is the open half of this decision, and it is the half that
decides whether the amended pin is enforced or only written down.
