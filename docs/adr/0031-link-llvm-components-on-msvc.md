# On MSVC, finc links LLVM's component static libraries instead of the monolithic dylib

MSVC cannot build LLVM's monolithic `libLLVM` shared library at any version, so
the Windows tarball exports no `LLVM` CMake target. On that platform finc links
the component static libraries via `llvm_map_components_to_libnames` instead:
`Core Support BitWriter BitReader MC Passes` plus `native`, which includes the
native target's codegen and assembler parser. `Passes` provides the optimization
pipeline. The component list is closed on purpose: a new LLVM
API use must add its component here rather than silently gaining one.

## Considered Options

- Keep refusing Windows builds: honest but leaves two CI rows and the release
  archive permanently red with no path forward.
- Link `LLVM` unconditionally and let the link fail: the failure names a
  missing `LLVM.lib` minutes later in another tool's output, saying nothing
  about why.

## Consequences

`llvm_map_components_to_libnames` expands the dependency closure, so this names
direct uses only. The backend uses LLVM-style casts throughout, so the static
libs' lack of RTTI is not a mismatch. If LLVM's MSVC story ever grows a dylib,
the `TARGET LLVM` branch below already prefers it with no edit.

The official Windows archive uses the static C runtime. The Conan profile sets
`compiler.runtime=static` for Fin and its dependencies so fmt and GTest agree with
LLVM's runtime selection.

The archive also embeds its build machine's absolute DIA SDK library path.
If that path is missing, CMake resolves the same architecture's `diaguids.lib`
from the active Visual Studio Developer shell. This supports installations in
other locations and newer Visual Studio versions without changing LLVM's pin.

## Amendment: static musl releases

The Alpine release uses `FIN_STATIC_LLVM=ON` and `-static`. It asks the pinned
LLVM installation's `llvm-config --link-static` for the same component list and
its system libraries. This avoids shared-library dependencies in exported CMake
targets. Release validation runs the compiler tests and documentation examples,
and rejects a musl executable containing a dynamic-loader (`INTERP`) segment.
The default Unix build continues to use the monolithic shared LLVM target.
