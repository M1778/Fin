# On MSVC, finc links LLVM's component static libraries instead of the monolithic dylib

MSVC cannot build LLVM's monolithic `libLLVM` shared library at any version, so
the Windows tarball exports no `LLVM` CMake target. On that platform finc links
the component static libraries via `llvm_map_components_to_libnames` instead:
`Core Support BitWriter BitReader MC` plus the native target's codegen and asm
parser, which is everything the backend uses (IR building, native object
emission, nothing else). The component list is closed on purpose: a new LLVM
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
