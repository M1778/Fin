# 1. Quick start

## Get a compiler that can generate code

If `finc` is already installed, run `finc --version` and `finc --help`. Keep the
release layout intact: `bin/finc` sits beside `lib/std`. Moving the binary alone
can make standard-library imports disappear.

To build this repository on Linux or macOS, install:

- A C++20 compiler and CMake 3.20 or newer.
- Conan 2, Bison 3.2 or newer, and Flex.
- LLVM development headers and libraries for the major pinned in
  [CMakeLists.txt](../../CMakeLists.txt), currently 22. A `clang` executable alone
  is not the LLVM development package.
- A C compiler driver, normally `cc`, to link generated programs.

Then run from the repository root:

```sh
./build.sh --release
./build/finc --version
```

`build.sh` installs the Conan dependencies, configures CMake, builds, and runs the
compiler tests. `--no-test` skips tests; `--no-llvm` creates a frontend-only compiler
that cannot build applications. `--clean` deletes the existing build directory.

The commands below use `finc` on `PATH`; for a source build, substitute
`./build/finc` on Unix or `.\build\finc.exe` on Windows.

### Native Windows with uv

Use PowerShell in a Visual Studio Developer shell with the C++ desktop workload,
CMake, and [uv](https://docs.astral.sh/uv/getting-started/installation/) installed.
Install winflexbison 2.5.25 and extract the full LLVM 22.1.8 development archive
for your architecture. The `LLVM-*.exe` installer does not include the LLVM
libraries needed to build Fin. The exact download and checksum steps are in
[the shared CI setup](../../.github/actions/setup-toolchain/action.yml).

Set these paths to your extracted tools, then run from this repository:

```powershell
$env:LLVM_DIR = 'C:/tools/llvm/lib/cmake/llvm'
uv tool install ninja
$env:PATH = "$(uv tool dir --bin);C:/tools/llvm/bin;C:/tools/winflexbison;$env:PATH"
uvx --from 'conan>=2.4,<3' conan profile detect --force
uvx --from 'conan>=2.4,<3' conan install . --output-folder=build --build=missing -pr:a=conan/profiles/fin -c tools.cmake.cmaketoolchain:generator=Ninja -s build_type=Release
$toolchain = (Get-ChildItem build -Filter conan_toolchain.cmake -Recurse | Select-Object -First 1).FullName
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DLLVM_DIR=$env:LLVM_DIR" -DFIN_WITH_LLVM=ON -DFIN_LLVM_MAJOR=22
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure --no-tests=error
uv run --no-project tests/tools/check_docs.py --finc build/finc.exe
```

For native ARM64, use an ARM64 Developer shell and the ARM64 LLVM archive.
The Visual Studio installation must include the DIA SDK. `uv` runs Python and Conan natively; CMake
builds the C++ compiler. Keep compilation and execution in the same environment.

Windows links generated programs with `clang` by default, using the Visual Studio
SDK and libraries available in the Developer shell. Use `hello.exe` as the output
name and `.\hello.exe` to run it.

## First program

Save this complete file as `hello.fin`:

```fin
fun main() <noret> {
    printf("Hello, Fin!\n");
}
```

```output
Hello, Fin!
```

`fun` declares a function. Its return type follows the parameters in `<...>`;
`noret` means no value. Statements end in semicolons. The bundled library makes
`printf` available without an import.

```sh
finc hello.fin             # syntax and type checking only
finc hello.fin -o hello    # object generation and linking
./hello                   # run the program
```

**A successful first command does not prove the second will succeed.** Fin's
frontend supports some forms the backend cannot generate yet.

## Commands for application work

| Command / option | Meaning |
| --- | --- |
| `finc main.fin` | Check one root file and its imports |
| `finc main.fin -o app` | Build an executable; requires a top-level `main` body |
| `finc library.fin -c -o library.o` | Emit an object without linking or requiring `main` |
| `-I ./src` | Add an import search directory; repeat for more paths |
| `--fin-libs <paths>` | Pin library paths, replacing `FIN_LIBS` and suppressing bundled-library fallback |
| `--diagnostics=json` | Newline-delimited JSON diagnostics on stderr |
| `--color=never` | Plain diagnostic output |
| `--debug-codegen` | Show backend and linker details |
| `-O0` through `-O3` | Optimization level; default `-O0` |

A library path list uses `:` on Unix and `;` on Windows. `-I` alone does not remove
the bundled standard library. See [module resolution](10-modules-and-imports.md).
The full option and diagnostic contract lives in
[finc-interface-contract.md](../finc-interface-contract.md).

## Diagnose the correct stage

This program deliberately fails type-checking:

```fin error
fun main() <noret> {
    let value <string> = 10;
}
```

The diagnostic names a type mismatch and points at the integer expression.
Fix the reported expression or declaration, then check again.

| Failure | Next action |
| --- | --- |
| `syntax error` | Check punctuation, parameter colons, and angle-bracket annotations |
| `Undefined variable` / missing export | Check spelling and the actual imported declaration |
| `module not found` | Read the listed search paths; verify `-I` and `FIN_LIBS` |
| `codegen: ... not lowered yet` | Reduce the unsupported construct; type-checking alone cannot validate it |
| `link failed` | Check the C driver and foreign symbols; object generation already succeeded |

`FIN_CC` selects a GCC-compatible C driver when the default (`cc` on Unix,
`clang` on Windows) is unavailable:

```sh
FIN_CC=clang finc hello.fin -o hello
```

Use a driver executable path, not a command plus arbitrary flags.

Compiler exit codes are `0` for success, `1` for source rejection, `2` for command
line or input errors, and `3` for compiler failure. Diagnostics go to stderr;
stdout is reserved for help/version output. Do not use `--no-check` to make an
application appear to compile: it bypasses semantic validation.

Next: [variables and types](02-variables-and-types.md).
