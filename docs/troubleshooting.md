# Troubleshooting `finc`

Every entry here is a failure **reproduced on a Linux x86_64 host on
2026-11-20**, with `finc 1.0.0 (contract 3)` built against LLVM 22.1.8, `cc`
16.2.1, CMake 4.4.4, Conan 2.33.0. The error text is pasted verbatim, because a
paraphrase does not match what the reader has on screen.

Each entry is: **symptom** (what you see), **cause**, **confirm** (the exact
command that tells the two possible causes apart), **fix**.

Platforms are marked:

- **verified here** — run on the host above.
- **mirrored from CI** — the text and steps come from a real CI log on the
  platform named, and were not run here. The run is cited.

Install steps themselves are in [INSTALL.md](../INSTALL.md).

---

## `finc: command not found`

**Symptom.** After a successful install, the shell cannot find the compiler:

```
$ finc --version
bash: line 1: finc: command not found
```

`sh` says the same thing, and the exit status is `127`.

**Cause.** Nothing installs `finc` onto `PATH`. `install.sh` ends at
`finc ready at: <dir>/build/finc` and stops there; release archives unpack to
`bin/finc` and stop there; `.deb`/`.rpm` package a binary but only a package
manager registering `/usr/bin` gets it into your shell. This is deliberate — no
script here edits `~/.profile` or `~/.zshrc` behind your back.

**Confirm.** Ask the shell what it would run, then check the two usual
locations:

```sh
command -v finc                 # prints nothing when it is not on PATH
ls "$HOME/Fin/build/finc"       # source install (install.sh default)
ls "$HOME/.local/opt/bin/finc"  # release archive, if you unpacked it there
```

**Fix.** Put the directory containing `finc` — not the file — on `PATH`, in your
shell's startup file so it survives a new terminal:

```sh
export PATH="$HOME/Fin/build:$PATH"   # install.sh layout
finc --version
```

Use `$HOME/.local/opt/bin` for an archive unpacked as in
[INSTALL.md](../INSTALL.md#route-1-a-released-asset), and nothing at all for a
`.deb`/`.rpm`, which already install to `/usr/bin`.

**Do not** copy just the binary somewhere on `PATH`. That is the next entry.

---

## Standard library not found after moving `finc`

**Symptom.** The compiler runs and compiles files, but every `::std` import
fails, and so does ambient `printf`:

```text
$ ./finc use_std.fin
error: module not found: strings
   = help: searched: .
error: Failed to load module 'strings'
   --> use_std.fin:1:1
   |
 1 | import { equals } from strings::std;
   | ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ here
error: Undefined function or type 'printf'
```

**Cause.** `finc` finds its bundled standard library at exactly one place —
`<directory of the executable>/../lib/std` (`src/driver/SearchPaths.hpp`,
`bundledLibraryPathsFor`). It is resolved from the executable's own location,
**never** from the working directory, and it is skipped when that directory
does not exist. A binary alone in `~/.local/bin` has no `../lib/std`, so there is
no bundled library, and the search path collapses to `.`.

Note the help line: `searched: .` is the whole story. When the bundled library
is in place it does not print at all on success, but the path it added is
`<exe>/../lib/std`; when it is missing, `.` is all that is left.

**Confirm.** Print the two paths and compare:

```sh
readlink -f "$(command -v finc)"   # where the binary really is
ls "$(dirname "$(dirname "$(command -v finc)")")/lib/std" 2>&1 | head -3
```

The `ls` must list `.fin` files. `No such file or directory` means the layout is
broken.

**Fix.** Keep `bin` and `lib` siblings, as every archive and package ships
them:

```
<root>/bin/finc
<root>/lib/std/*.fin
```

So either move the whole tree, or re-point the library explicitly. Precedence
below matters; read the next entry before choosing `--fin-libs`.

```sh
finc use_std.fin --fin-libs /path/to/lib/std     # explicit, survives moving the binary
FIN_LIBS=/path/to/lib/std finc use_std.fin       # same, from the environment
```

A symlink to the binary is fine (`/proc/self/exe` resolves it), so
`ln -s .../bin/finc ~/.local/bin/finc` works where `cp` does not.

---

## `module not found` — how `--fin-libs` / `FIN_LIBS` precedence actually works

**Symptom.** An import fails with a searched-paths list that does not contain
what you expected:

```
error: module not found: strings
   = help: searched: /does/not/exist (does not exist)
```

or

```
error: module not found: strings
   = help: no library search paths were given; pass --fin-libs or set FIN_LIBS
```

**Cause.** Naming library paths is a **pin, not an addition**. From
`configureLoader` in `src/driver/Driver.cpp`, in this order:

1. `-I` paths are always added.
2. Then **either** `--fin-libs` (if the flag appeared at all) **or** a non-empty
   `FIN_LIBS` — the flag *replaces* the environment variable, it does not extend
   it.
3. **Only if neither named anything**: the bundled `<exe>/../lib/std`, then `.`.

So the failure modes people hit are:

| What you wrote | What you got |
| --- | --- |
| `--fin-libs /typo` | Your path **replaces** the bundled library. One typo and every `::std` import breaks, with the typo shown as `(does not exist)`. |
| `--fin-libs=` (empty) | Valid and distinct: "no library paths". Suppresses the bundled library **and** `.`. The help says `no library search paths were given`. |
| `FIN_LIBS=` (empty) | **Unset.** Treated as if you never set it, so the bundled library still resolves. This is the shell's "clear this variable" spelling and is deliberately not a pin. |
| `-I /typo` | Does **not** suppress the bundled library. `-I` adds a path and changes nothing else. |
| `--fin-libs /nope` with `FIN_LIBS` set | `--fin-libs` wins; `FIN_LIBS` is ignored entirely. |

An entry that does not exist is dropped from the search and shows up in the
failure text as `(does not exist)`, which is why the help line is the fastest
diagnosis: *it prints the paths that were actually asked for and whether each
one exists.*

**Confirm.** Reproduce both failure shapes with no flags, then with the pin:

```sh
finc use_std.fin                          # bundled library: succeeds
finc use_std.fin --fin-libs /does/not/exist   # pin replaces it: fails, path listed
finc use_std.fin --fin-libs=                  # pin to nothing: fails, no paths listed
```

The middle one prints `searched: /does/not/exist (does not exist)`; the last
prints `no library search paths were given; pass --fin-libs or set FIN_LIBS`.

**Fix.** Pass a path that exists, and pass *every* path you need, because the
pin displaces the defaults:

```sh
finc app.fin --fin-libs "$PWD/vendor:$HOME/Fin/lib/std"
```

The separator is the platform's — `:` on Unix, `;` on Windows, where a leading
`C:\` would otherwise split into `C` and `\libs`. Repeating the flag accumulates
(`--fin-libs=a --fin-libs=b` == `--fin-libs=a:b` on Unix), which is the way to
pass a path that itself contains the separator. Full rules:
[docs/finc-interface-contract.md](finc-interface-contract.md).

---

## No C linker driver (`cc` / `FIN_CC`)

**Symptom.** Frontend checking passes, object generation passes, then linking
fails:

```
error: link failed: cc exited with 127
   = help: the object file was emitted, so this is the C toolchain rather than the Fin program; set FIN_CC to name a working compiler driver
```

The line above it names the real cause, and it comes from the shell, not from
`finc`:

```
env: ‘cc’: No such file or directory
```

**Cause.** `finc` does not link executables itself. It shells out to a C driver —
`cc` on Unix, `clang` on Windows (`linkCommand` in `src/utils/Process.cpp`),
overridable with `FIN_CC`. Exit `127` means "command not found"; `126` means
"found but not executable". The object file was already written successfully, so
this is never a Fin-code error, and the help line says so.

**Confirm.** Two separate questions — does `finc` see a driver, and is it a
working one:

```sh
command -v cc          # nothing printed => no driver on PATH
cc --version           # the driver exists; does it run?
echo "$FIN_CC"         # an override here replaces cc entirely
```

**Fix.** Install a C toolchain, or point `FIN_CC` at one. `FIN_CC` names one
**executable**, never a command plus flags:

```sh
FIN_CC=/usr/bin/cc finc hello.fin -o hello       # works: names one executable
FIN_CC="cc -m64" finc hello.fin -o hello         # fails: env: 'cc -m64': No such file or directory
```

The second form fails with exit `127` too, because the whole string is looked up
as one filename. Generated programs also link `libLLVM` and `libm` by default, so
a driver that exists but cannot find the LLVM runtime produces
`/usr/bin/ld: cannot find -lLLVM-22` with exit `1` instead of `127` — fix that by
putting the LLVM library directory on the linker's path, not by setting
`FIN_CC` to a flag-bearing string.

---

## LLVM missing at build time

**Symptom.** `./build.sh` warns, then CMake stops:

```
[WARN] No llvm-config on PATH; leaving LLVM_DIR to find_package to resolve.
...
CMake Error at CMakeLists.txt:95 (find_package):
  Could not find a package configuration file provided by "LLVM" with any of
  the following names:

    LLVM.cps
    llvm.cps
    LLVMConfig.cmake
    llvm-config.cmake

  Add the installation prefix of "LLVM" to CMAKE_PREFIX_PATH or set
  "LLVM_DIR" to a directory containing one of the above files.  If "LLVM"
  provides a separate development package or SDK, be sure it has been
  installed.
```

`install.sh` fails earlier and more legibly:

```
install.sh: no llvm-config (any major) found; the LLVM backend cannot link without it.
install.sh: install LLVM (Debian/Ubuntu: https://apt.llvm.org, then re-run)
```

**Cause.** LLVM is found through `find_package(LLVM CONFIG)`, which searches the
default prefixes. A second LLVM under a versioned prefix (`/usr/lib/llvm-22`,
Homebrew's keg, an extracted LLVM archive) is not on that path and has to be
pointed at. `build.sh` asks `llvm-config` where its cmake directory is and
passes `-DLLVM_DIR=...`; with no `llvm-config` it leaves it to CMake, which does
not find a non-default prefix on its own.

**Confirm.** Check which `llvm-config` exists and what major it reports — both
matter, because the next entry is what catches the "present but wrong major"
case:

```sh
command -v llvm-config llvm-config-22
llvm-config-22 --version
llvm-config-22 --cmakedir
```

**Fix.** Install the development package for the pinned major (the pin is
`set(FIN_LLVM_MAJOR 22 ...)` in `CMakeLists.txt`). Where the distribution puts
`llvm-config-22` on `PATH`, `build.sh` finds it and passes `-DLLVM_DIR` for you —
that is the whole fix, and it is the case CI is in. Where it does not, point
CMake at the config directory *and* keep the Conan toolchain CMake needs for
`fmt`, because a bare `cmake -S . -B build` fails on `fmt` before it ever looks
at LLVM:

```sh
LLVM_DIR="$(llvm-config-22 --cmakedir)"      # what build.sh computes for you
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=build/build/Release/generators/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR="$LLVM_DIR" -DFIN_WITH_LLVM=ON
```

That configures (`-- Using LLVM 22.1.8 from /usr/lib/llvm22/lib/cmake/llvm`, then
`-- Configuring done`); building is the same `cmake --build build --parallel` the
script runs. Mostly, though, put `llvm-config-22` on `PATH` and let `build.sh`
do it: with `llvm-config-22` present it printed
`[INFO] LLVM 22.1.8 via /usr/bin/llvm-config-22 -> /usr/lib/llvm22/lib/cmake/llvm`
and `[SUCCESS] Backend: LLVM 22 (FIN_WITH_LLVM=ON)`.

On Debian/Ubuntu the distribution's LLVM is usually the wrong major; use
<https://apt.llvm.org>. On macOS use `brew install llvm@22` and take the keg's
`llvm-config`. `.github/actions/setup-toolchain/action.yml` has the exact
commands CI runs on Linux, macOS and Windows. A `clang` executable alone is not
the LLVM development package.

---

## Wrong LLVM major at build time

**Symptom.** Configure fails with the pin named explicitly:

```
CMake Error at CMakeLists.txt:112 (message):
  ADR 0010: finc is built against exactly one LLVM major, 21.  Found 22.1.8
  at /usr/lib/llvm22/lib/cmake/llvm.
```

Or, from the installer, before any compilation:

```
install.sh: llvm-config reports 21.1.0 but this tree pins LLVM 22.
install.sh: install LLVM 22 (Debian/Ubuntu: https://apt.llvm.org) and re-run.
```

The message's first number is the major you asked for and the second is the one
that was found. An LLVM that exports no monolithic `LLVM` target also fails
configure — that is the same ADR on a build with no `libLLVM`:

```
CMake Error at CMakeLists.txt:144 (message):
  LLVM 22.1.8 at /home/M1778/fin-tmp/nodylib/lib/cmake/llvm does not export
  the monolithic `LLVM` target that fin_core links, and this is not MSVC, so
  ADR 0031's component fallback does not apply -- this is a version mismatch
  or a dylib-less build where one was expected.
```

(The `at <path>` is whatever `LLVM_DIR` named.)

**Cause.** ADR 0010 pins one LLVM major, and the pin is enforced rather than
documented: a mismatched LLVM is a configure error, not a warning. Two LLVMs
with the same name and different majors are two different compilers, so a
warning would produce a binary nobody can reason about.

**Confirm.** Read the pin out of the tree and the major `llvm-config` reports,
rather than trusting either alone:

```sh
sed -n 's/^set(FIN_LLVM_MAJOR \([0-9][0-9]*\) CACHE STRING.*/\1/p' CMakeLists.txt
llvm-config-22 --version; llvm-config --version 2>/dev/null
```

**Fix.** Install the pinned major and make sure the `llvm-config` that is found
first is that one. `build.sh` prefers `llvm-config-22` over bare `llvm-config`
for exactly this reason, so a stray bare `llvm-config` on `PATH` from another
install is not automatically fatal — but an explicit `-DLLVM_DIR` pointing at
the wrong one always is. There is no backend-less fallback: `--no-llvm` builds a
frontend-only compiler whose every `-o` refuses, which is not an install.

---

## `install.sh` refuses to run or refuses a ref

Before it clones anything (from `need_hint` and the preflight checks):

| Message (verbatim) | Cause and fix |
| --- | --- |
| `install.sh: required tool 'bison' not found.` | Missing build dependency; the next line names the install command for your package manager. `build.sh` on its own says only `[ERROR] bison could not be found. Please install it.` |
| `install.sh: 'conan' is on PATH but does not run.` | A `conan` on `PATH` that exits nonzero — typically another account's `pip install --user`. Reinstall for yourself: `pipx install conan`. |
| `install.sh: a C and a C++ compiler are required (cc and c++).` | Neither `cc` nor `c++` is present. `sudo apt-get install build-essential`, `sudo dnf install gcc gcc-c++`, `sudo pacman -S base-devel`, or `xcode-select --install`. |
| `install.sh: neither ninja nor make found; one build driver is required.` | Install one of them. |
| `install.sh: no llvm-config (any major) found; the LLVM backend cannot link without it.` | See the LLVM entries above. |
| `install.sh: no published finc release found at https://github.com/M1778/Fin.` | No **release** is published (a tag alone is not one). Pass `FIN_REF=<vX.Y.Z tag or full 40-hex commit>` naming a ref you have inspected. At `1aa4a8f` (2026-11-20) both existing tags, `v0.4.0` and `v1.0.0`, are in this state. |

After the clone, before the build:

| Message (verbatim) | Cause and fix |
| --- | --- |
| `install.sh: /some/dir exists but is not a git checkout; refusing to overwrite.` | `FIN_DIR` points at a directory that is not a checkout. Remove it or choose another `FIN_DIR`. |
| `install.sh: checking out v1.0.0 in <dir> failed.` | The existing checkout has local changes that the target ref would overwrite. Stash, reset, or pick another `FIN_DIR`; the installer never force-discards your work. |
| `install.sh: requested commit master but the checkout resolved to <sha>; refusing.` | `FIN_REF=master` is neither a tag nor a 40-hex commit and is refused by design — a moving branch is what the pin exists to prevent. Use a tag or a commit. |
| `install.sh: fetching commit <sha> from <repo> failed.` | The ref does not exist, or the network failed (it retries 3 times). Check the commit hash, then re-run. |

`install.sh: compiling Fin at ref ...` followed by silence for a few minutes is
normal: that is the build, and it takes about four minutes on 16 cores.

One scary-looking line that is **not** an error, seen with an annotated tag:

```
warning: refs/tags/v1.0.0 00294b100d1fe7d245f969e3536ce27c024b5bfd is not a commit!
```

That is `git clone --branch <annotated-tag>` reporting that the ref it was given
names a tag object, not a commit; the clone then peels it and checks out the
commit the tag points at, which the next line prints. The install completes
(`finc ready at: ...`, exit `0`). Only a nonzero exit or a missing
`finc ready at:` line is a failure.

---

## Windows: MSVC vs `clang`, and `%lld`

**Verified on:** nowhere here. Everything in this section is **mirrored from
CI** — the error text is from the real Windows runner log of workflow run
[40026847877](https://github.com/M1778/Fin/actions/runs/40026847877), and the
current `ci.yml` green rows are `windows-x86_64` and `windows-arm64` in run
[44652781810](https://github.com/M1778/Fin/actions/runs/44652781810).

### The compiler builds with MSVC; your programs link with `clang`

Two different toolchains, on purpose. `finc` itself is built by MSVC (CI uses
`ilammy/msvc-dev-cmd` to enter the Developer shell), but `runLinker` runs
`clang` on Windows — not `cl`, which does not accept the GCC-style argument
list `finc` emits. So a program you compile needs:

- a **Visual Studio Developer shell** (or equivalent `INCLUDE`/`LIB`
  environment), because `clang` needs the SDK's `LIB` paths to find
  `kernel32.lib` and friends;
- the LLVM Windows archive, because that is where `clang` comes from.

**Symptom when the SDK paths are missing** (real log text):

```
# clang: error: no such file or directory: 'ws2_32.lib'
# error: link failed: clang exited with 1
#    = help: the object file was emitted, so this is the C toolchain rather than the Fin program; set FIN_CC to name a working compiler driver
```

**Cause.** The link line names libraries (`-lws2_32`, and on other paths the
LLVM import library) that `clang` resolves by searching `LIB`. Outside a
Developer shell, `LIB` is unset, so a bare library name is treated as a filename
in the current directory and is not found.

Two distinct faults produce this shape, and the fix differs:

- **Wrong directory.** `cl`/`clang` run outside the Developer shell. Fix: run in
  "x64 Native Tools Command Prompt for VS 2022" (or `arm64`), or the Developer
  PowerShell, then set your LLVM/winflexbison paths inside it.
- **A literal `.lib` argument.** The compiler must pass `-lws2_32` (let the
  linker search) rather than `ws2_32.lib` (a local input filename). That was
  the `40026847877` bug, fixed in `5360b05`; if you are building an older
  commit, that is what you are looking at. Confirm the fix is present with
  `git log --oneline -1 -- src/utils/Process.cpp`.

**Confirm, on Windows:**

```powershell
clang --version           # exists and runs
echo "$env:LIB"           # empty outside a Developer shell
echo "$env:FIN_CC"        # an override replaces clang entirely
```

**Fix.** Enter the Developer shell first, then run `finc`. If `clang` is not
the driver you want, `FIN_CC` names one executable — use a full path with
forward slashes, no flags:

```powershell
$env:FIN_CC = 'C:/tools/llvm/bin/clang.exe'
```

### `%lld` / `%llu`, not `%ld` / `%lu`

Fin's `int` is 32-bit and `long`/`ulong` are **64-bit on every platform**. C's
`long` is 32-bit on Windows (LLP64) and 64-bit on Linux. So a format string that
is correct on Linux silently truncates on Windows.

Verified here on Linux, which is exactly why it is a trap — **both** spellings
print correctly on Linux:

```text
$ cat fmt.fin
fun main() <noret> {
    let big <long> = 5000000000;
    printf("%ld\n", big);
    printf("%lld\n", big);
}
$ finc fmt.fin -o fmt && ./fmt
5000000000
5000000000
```

On Windows the `%ld` line prints the low 32 bits. The same trap in the other
direction, which truncates on Linux too — `%d` against a `long`:

```text
$ cat trunc.fin
fun main() <noret> {
    let big <long> = 5000000000;
    printf("%d\n", big);
}
$ finc trunc.fin -o trunc && ./trunc
705032704
```

No warning is printed for either: there is no format-string checking.
**Fix:** use `%lld`/`%llu` for every Fin `long`/`ulong`, `%d`/`%u` for `int`/`uint`,
and match each `@define` to the actual C ABI of the function you are declaring.
`docs/agent-guide.md` states the same rule.

---

## A check-only run passed but the build fails

**Symptom.** `finc file.fin` prints `Build Successful.`, and the very next
command is refused:

```
$ finc refused.fin
Build Successful.
$ finc refused.fin -o refused
error: codegen: an undeclared operator '==' on struct 'Data' is not lowered yet
   --> refused.fin:6:11
   |
 6 |     blame x == y;
   |           ^^^^^^ here
```

**Cause.** Two different promises. Without `-o`/`-c` the compiler stops after
semantic analysis; with `-o` it must also lower the program and link it. The
frontend accepts constructs the backend cannot generate yet — that gap is
stated in the README and is not a bug report against the file you are
compiling. Struct `==` is the visible one: ADR 0036 keeps it declared-only.

**Confirm.** Always build before treating code as done, and read the refusal,
which names the construct:

```sh
finc file.fin            # type checking only
finc file.fin -o file    # what "it compiles" means
```

`finc --debug-codegen file.fin -o file` additionally prints the object path and
the exact link command, including the driver it chose (the number in the object
name is a process id):

```text
[codegen] declared main
[codegen] declared printf
[codegen] wrote .file.154550.fin.o
[codegen] [cc] [.file.154550.fin.o] [-o] [file] [-Wl,--as-needed] [-lLLVM-22] [-lm]
```

**Fix.** Rewrite the construct the refusal names, or check the file with no
`-o` if it is a library. A program with no `main` body is refused for the same
reason, with a different message — that one is not a codegen gap:

```
error: this program has no 'main'
   = help: '-o' builds an executable and an executable starts somewhere: write 'fun main() <noret>'. A file that is a library has nothing to build -- check it with 'finc <file>' and no '-o', or compile it to an object with '-c'
```

---

## Where the diagnostics go

Diagnostics, `Build Successful.`, and warnings go to **stderr**; stdout carries
only `--help` and `--version` output. That is the machine contract
(`docs/finc-interface-contract.md`) and it means:

```sh
finc file.fin -o file 2>/dev/null    # silence everything, still get the exit code
finc file.fin --diagnostics=json     # newline-delimited JSON instead
```

Exit codes: `0` success, `1` source rejected, `2` usage error (bad flag, missing
input), `3` internal error. A missing input file is `2` with
`error: could not read file: <path>`; an unknown option is `2` with
`error: unknown option '--bogus'`.

## Still unverified on this host

- **macOS and Windows steps.** Every macOS/Windows command above is mirrored
  from CI and cited by run id; none was executed here. In particular the
  Developer-shell `LIB` fix, `FIN_CC=C:/.../clang.exe`, and the truncating
  `%ld` are reasoned from the ABI and the CI log, not observed.
- **A published release.** At the time of writing no release is published
  (tags `v0.4.0` and `v1.0.0` exist; `gh release list` is empty). The asset
  commands in [INSTALL.md](../INSTALL.md) were run against locally built
  artifacts of the same layout, and `check_install_docs.py --assets <dir>`
  re-runs them.
- **`rpmbuild`.** Not installed here and not needed to read an `.rpm`
  (`bsdtar` was used); building one was not exercised.
- **A native `.deb`/`.rpm` install to `/usr/bin`.** `dpkg`/`rpm` are not on
  this host; both packages were unpacked into a scratch directory, which is
  what proves the `<exe>/../lib/std` layout and nothing about a package
  database.
