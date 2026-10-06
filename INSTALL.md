# Installing Fin with `install.sh`

One-liner (from the site hero):

```sh
curl -fsSL https://raw.githubusercontent.com/M1778/Fin/master/install.sh | sh
```

What it does: checks build tools, clones the repo (default `$HOME/Fin`,
override with `FIN_DIR=/somewhere sh install.sh`), verifies the LLVM major
pinned in `CMakeLists.txt` against your `llvm-config`, runs
`./build.sh --release --with-llvm --no-test`, then proves the result by
compiling and running one sample program. A successful install ends with
`finc ready at: <dir>/build/finc` — anything else is a failure, and the
script says what to fix.

You need a C and C++ compiler, CMake, Conan 2, Bison, Flex, Python 3, and
the LLVM development package for the pinned major (currently 22). Platform
details are in [the quick start](docs/guide/01-quick-start.md). The script
names the missing piece and the install command before cloning anything.

Failure modes you may hit:

- `required tool '<t>' not found` — install it with the printed command and re-run.
- `'conan' is on PATH but does not run` — that conan belongs to another
  account's `pip install --user`; reinstall it for yourself
  (`pipx install conan`) and re-run.
- `llvm-config reports X but this tree pins LLVM Y` — install LLVM Y
  (Debian/Ubuntu: https://apt.llvm.org) and re-run. There is no
  backend-less fallback: without the pinned LLVM there is no install.
- `cloning ... failed after 3 attempts` — check network access to
  github.com and re-run (clone retries automatically).
- `<dir> exists but is not a git checkout` — remove it or set `FIN_DIR`
  to another path. A previous install in the same directory is updated
  with `git pull --ff-only`; if that fails, the checkout has local
  changes — stash, reset, or pick another `FIN_DIR`.
