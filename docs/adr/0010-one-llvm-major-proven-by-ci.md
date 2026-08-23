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
