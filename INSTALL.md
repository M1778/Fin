# Installing `finc`

Two routes. A **released asset** needs no compiler on your machine. **Building
from source** needs a full C++ toolchain and takes a few minutes. Both end the
same way: `bin/finc` sitting beside `lib/std`, because the compiler finds its
standard library at `<directory of the executable>/../lib/std` and nowhere else
(`src/driver/SearchPaths.hpp`). Moving `finc` alone breaks every `::std` import
— see
[troubleshooting](docs/troubleshooting.md#standard-library-not-found-after-moving-finc).

After either route the binary is not on `PATH`: nothing in this repository edits
your shell profile. Add the directory it lives in yourself; that is the
`finc: command not found` case in
[troubleshooting](docs/troubleshooting.md#finc-command-not-found).

Every shell block below carries an HTML-comment marker saying how it was
checked, and `tests/tools/check_install_docs.py` executes the ones marked
`fin-verify` against a real compiler, so the marker cannot rot into just another
claim. A block marked `fin-unsupported` names why it was not run; a `.sh` block
with no marker fails the checker.

## Supported platforms

"Supported" means CI **builds the compiler there and runs the whole test suite
there**. The definition is the matrix in
[`.github/workflows/ci.yml`](.github/workflows/ci.yml), not a prose claim: six
rows, `fail-fast: false`, and a `Test` step with no `|| true`.
`.github/workflows/release.yml` adds one more platform — a static
`x86_64-unknown-linux-musl` compiler built and tested inside Alpine — and ships
the native installables.

| Platform | Where it is proven | Status |
| --- | --- | --- |
| Linux x86_64 (glibc) | `ci.yml` row `linux-x86_64`, `ubuntu-24.04` | supported |
| Linux arm64 (glibc) | `ci.yml` row `linux-arm64`, `ubuntu-24.04-arm` | supported |
| macOS arm64 | `ci.yml` row `macos-arm64`, `macos-15` | supported |
| macOS x86_64 | `ci.yml` row `macos-x86_64`, `macos-15-intel` | supported |
| Windows x86_64 | `ci.yml` row `windows-x86_64`, `windows-2022` | supported |
| Windows arm64 | `ci.yml` row `windows-arm64`, `windows-11-arm` | supported |
| Linux x86_64 (musl, static) | `release.yml` job `build-musl`, inside Alpine | supported for released artifacts; not a `ci.yml` row |
| Anything else (BSD, 32-bit, other architectures, other libcs) | nowhere | should work, unverified |

Two honest qualifications:

- **The self-hosted bootstrap is Linux-only.** `ci.yml` gates the three-stage
  fixed-point build on `if: runner.os == 'Linux'`, because the stage driver
  links by shelling out to `cc -no-pie ... -lLLVM-22`. On macOS and Windows that
  step is skipped, so their green rows prove the C++ compiler, not the Fin
  compiler compiling itself.
- **The table is the commitment, not the last run.** The newest run observed
  while writing this file was
  [44652781810](https://github.com/M1778/Fin/actions/runs/44652781810) at
  `1aa4a8f` (2026-11-20): five of the six rows green, `macos-arm64` red on one
  test,
  `Soundness_SourceEncoding.TheUnrecognisedByteMessageNamesTheByteItRejected`.
  Check the run for the commit you are installing.

## Route 1 — a released asset

Each release carries a `finc-<version>-<target-triple>` archive per platform
plus the native installables, each next to its `.sha256` sidecar. The
authoritative list is `index.json` in the same release (it is what `finn`
reads); `release.yml` writes it from the bytes it just built. Nothing in it is
published by hand:

| Asset | Unpacks or installs to |
| --- | --- |
| `finc-<v>-<triple>.tar.gz` / `.zip` | `bin/finc` beside `lib/std` |
| `finc-<v>-x86_64-unknown-linux-gnu.deb` | `/usr/bin/finc`, `/usr/lib/std` |
| `finc-<v>-x86_64-unknown-linux-gnu.rpm` | same FHS layout |
| `finc-<v>-<triple>-setup.exe` | the same `bin`/`lib` tree, optional `PATH` entry |
| `finc-<v>-<triple>.dmg` | the same `bin`/`lib` tree, one folder |
| `finc-<v>-<triple>.cdx.json` | nothing — the CycloneDX SBOM for that binary, for auditors |

Verify a download before unpacking it: the sidecar is one line in `sha256sum`
format, so `sha256sum -c` is the whole check.

<!-- fin-verify-asset: finc-*-x86_64-unknown-linux-gnu.tar.gz -->
```sh
# expect: OK
sha256sum -c finc-*-x86_64-unknown-linux-gnu.tar.gz.sha256
```

<!-- fin-verify-asset: finc-*-x86_64-unknown-linux-gnu.tar.gz -->
```sh
# expect: bin/finc
tar -tzf finc-*-x86_64-unknown-linux-gnu.tar.gz | head -2
```

Keep `bin` and `lib` as siblings. Unpacking to `$HOME/.local/opt` and putting
that `bin` on `PATH` is the whole install — replace the `tar` line with
`unzip <archive>` on Windows, run the `setup.exe`, or open the `.dmg` on macOS.
The `.deb` and `.rpm` put `finc` in `/usr/bin` and the library in `/usr/lib/std`,
which is the same `<exe>/../lib/std` rule, so they need no wrapper.

<!-- fin-verify-asset: finc-*-x86_64-unknown-linux-gnu.tar.gz -->
```sh
# expect: finc 1.
mkdir -p "$HOME/.local/opt"
tar -xzf finc-*-x86_64-unknown-linux-gnu.tar.gz -C "$HOME/.local/opt"
export PATH="$HOME/.local/opt/bin:$PATH"
finc --version
```

A `.deb` needs neither `dpkg` nor root to be inspected: it is an `ar` archive
holding `data.tar.xz`. Unpack it into the current directory and run the binary
straight out of `usr/bin/`, so the bundled `usr/lib/std` has to resolve through
the installed layout:

<!-- fin-verify-asset: finc-*.deb -->
```sh
# expect: finc 1.
ar x finc-*-x86_64-unknown-linux-gnu.deb
tar -xf data.tar.xz
./usr/bin/finc --version
```

A `.rpm` payload is cpio behind a header only `rpmbuild` writes, so *building*
one needs rpm tooling — but *reading* one does not, if `bsdtar` is installed
(`libarchive`; present on macOS, packaged as `bsdtar` on Debian/Arch):

<!-- fin-verify-asset: finc-*.rpm -->
```sh
# expect: finc 1.
bsdtar -xf finc-*-x86_64-unknown-linux-gnu.rpm
./usr/bin/finc --version
```

The musl archive is a static binary with no dynamic loader, so it is the one to
pick on a distribution whose glibc is older or absent — Arch Linux and Alpine
included.

<!-- fin-verify-asset: finc-*-x86_64-unknown-linux-musl.tar.gz -->
```sh
# expect: static
# expect: finc 1.
tar -xzf finc-*-x86_64-unknown-linux-musl.tar.gz
file bin/finc
./bin/finc --version
```

## Route 2 — build from source with `install.sh`

`install.sh` clones this repository at a pinned ref, builds it with
`./build.sh --release --with-llvm --no-test`, then proves the result by
compiling and running `tests/samples/basic.fin`. A successful install ends with
`finc ready at: <dir>/build/finc`; anything else is a failure and the script
says what to fix.

What it needs, checked before it clones anything: `git`, `cmake`, `conan`
(2.x, and it must actually run — a `pip install --user` copy belonging to
another account is caught here), `bison`, `flex`, `python3`, `cc` **and** `c++`,
`ninja` or `make`, and some `llvm-config` (`llvm-config` or the Debian spelling
`llvm-config-22`). The LLVM major is then read out of `CMakeLists.txt` and
enforced: a wrong major fails in the installer, not deep inside CMake. There is
no backend-less fallback — without the pinned LLVM there is no install.

It takes about four minutes on a 16-core machine: measured `4m10s` on
2026-11-20, `x86_64` Linux, LLVM 22.1.8, `FIN_REF=14547b4`.

The destination defaults to `$HOME/Fin`; `FIN_DIR` moves it. What gets compiled
is the newest **published** release; `FIN_REF` names a tag or a full 40-hex
commit instead:

<!-- fin-unsupported: clones and builds over the network (about 4 minutes); run by hand, not by the docs gate -->
```sh
curl -o install.sh https://raw.githubusercontent.com/M1778/Fin/master/install.sh
sh install.sh                          # newest published release, or refuse
FIN_REF=v1.0.0 sh install.sh           # a specific tag
FIN_DIR=/opt/Fin sh install.sh         # somewhere other than $HOME/Fin
```

At `1aa4a8f` (2026-11-20) the no-`FIN_REF` form refuses, because the tags
`v0.4.0` and `v1.0.0` exist but no release is published yet. That is the script
working as designed, not a bug:

```
install.sh: no published finc release found at https://github.com/M1778/Fin.
install.sh: run with FIN_REF=<full commit or vX.Y.Z tag> naming a ref
install.sh: you have inspected (e.g. by reading the source at that commit).
```

With `FIN_REF=v0.4.0`, run on this host, the same script printed:

```
install.sh: compiling Fin at ref v0.4.0
install.sh: Fin is at commit 14547b429dbd4c7c599211685e7b63744c769a18 (ref v0.4.0).
install.sh: LLVM 22.1.8 matches pinned major 22.
...
finc ready at: /home/.../Fin/build/finc
```

Then put it on `PATH` — the installer does not:

<!-- fin-unsupported: needs an install.sh run to $HOME/Fin to exist -->
```sh
export PATH="$HOME/Fin/build:$PATH"
finc --version
```

### What the installer verifies, and what it does not

- **No moving branch.** Without `FIN_REF` the installer asks the GitHub
  releases API for the newest released tag (`vX.Y.Z`) and clones exactly that;
  with no published release it refuses rather than compiling `master`. With no
  `curl` it falls back to `git ls-remote --tags` and says the release state is
  unverified.
- **The compiled commit is printed.** `install.sh: Fin is at commit <sha>` is
  the answer to "what did I just install?" — compare it against the release it
  came from.
- **`FIN_REF` is an even stricter pin.** A full commit ref is checked against
  the checkout it produced, and a mismatch exits; a `vX.Y.Z` tag prints the
  commit the tag names.
- **`master` is refused as a `FIN_REF`**, because it is neither a tag nor a
  40-hex commit: `install.sh: requested commit master but the checkout resolved
  to <sha>; refusing.`
- **No signature check, no self-checksum (yet).** The script trusts TLS to
  github.com like any fetch and does not verify its own bytes; reading it
  before running is the verification level this repo maintains. Released assets
  (`.deb`, `.rpm`, archives, ...) do get `.sha256` sidecars, and each release's
  notes carry the `sha256sum` of `install.sh` at that tag — compare it after
  fetching `https://raw.githubusercontent.com/M1778/Fin/<tag>/install.sh`.
  `release.yml` separately refuses to publish from a lightweight tag (an
  annotated tag is required, and its signature is verified when the runner has
  the key), so a tag you can install from is at least an annotated one — but the
  installer does not re-check that. What the distribution chain does and does
  not verify is written out in [SECURITY.md](SECURITY.md).

Failure modes — `required tool '<t>' not found`, `'conan' is on PATH but does
not run`, `no llvm-config (any major) found`, `llvm-config reports X but this
tree pins LLVM Y`, `<dir> exists but is not a git checkout`, a checkout that
will not move because it has local changes — are all quoted with their fixes in
[troubleshooting](docs/troubleshooting.md).

## Route 3 — build this checkout yourself

The same build the installer runs, minus the clone. This is also the only route
that lets you run the test suite, which is what "supported" means in the table
above.

Linux and macOS prerequisites: a C++20 compiler, CMake 3.20 or newer, Conan 2,
Bison 3.2 or newer, Flex, Python 3, a C driver named `cc` for linking generated
programs, and the LLVM development package for the major pinned in
`CMakeLists.txt` (currently 22 — a `clang` executable alone is not that
package). On Debian/Ubuntu the LLVM packages come from
<https://apt.llvm.org>, because the distribution's own LLVM is usually the
wrong major; `.github/actions/setup-toolchain/action.yml` has the exact
commands CI uses for Linux, macOS and Windows.

<!-- fin-unsupported: full build of the commit you are reading (342s with the test suite here); run by hand, not by the docs gate -->
```sh
git clone --depth 1 --branch v0.4.0 https://github.com/M1778/Fin
cd Fin
./build.sh --release --no-test        # configure + build, no test suite
./build/finc --version
```

Drop `--no-test` to run the suite. On this host the `--release` script took
`8m17s` wall, of which `342.37 sec` was `ctest` running `3219` tests with
`100% tests passed`. `--clean` discards `build/` first. `--no-llvm` builds a
frontend-only compiler whose every `-o` refuses — do not judge the compiler by
that build.

Then prove the backend, not just the frontend. A check-only run proves much less
than it looks like it does:

<!-- fin-verify -->
```sh
# expect: Hello, Fin!
cat > hello.fin <<'EOF'
fun main() <noret> {
    printf("Hello, Fin!\n");
}
EOF
finc hello.fin          # syntax and type checking only
finc hello.fin -o hello # object generation and linking
./hello
```

The gap between those two commands is real: this file passes the check and is
refused by code generation (ADR 0036 keeps struct `==` declared-only).

<!-- fin-verify -->
```sh
# expect-exit: 1
# expect: an undeclared operator '==' on struct 'Data' is not lowered yet
cat > refused.fin <<'EOF'
struct Data { pub a <int>, }

fun main() <noret> {
    let x <Data> = Data{a: 1};
    let y <Data> = Data{a: 1};
    blame x == y;
}
EOF
finc refused.fin                       # prints "Build Successful."
finc refused.fin -o refused            # refused here
```

A bundled standard-library import works with no flags at all, as long as `bin`
and `lib` are still siblings:

<!-- fin-verify -->
```sh
# expect: stdlib ok
cat > use_std.fin <<'EOF'
import { equals } from strings::std;

fun main() <noret> {
    blame equals("a", "a");
    printf("stdlib ok\n");
}
EOF
finc use_std.fin -o use_std
./use_std
```

To install a hand-built tree where a release archive would put it:

<!-- fin-unsupported: needs a configured build directory from the block above; run against this checkout -->
```sh
cmake --install build --prefix "$PWD/prefix"
./prefix/bin/finc --version
```

`cmake --install` writes `bin/finc` and `lib/std` only — the same two-directory
layout the archives carry, so the `<exe>/../lib/std` rule holds for it and
`prefix/` can be moved as a unit. Route 2's plain build tree works only by
accident of the same rule: `build/finc` finds `../lib/std` in the source tree,
which is why a `build/finc` copied away from the checkout loses the library.

Windows is a separate story: the C++ compiler is MSVC from a Visual Studio
Developer shell, LLVM comes from the official
`clang+llvm-22.1.8-*-pc-windows-msvc` archive (not the `LLVM-*.exe` installer,
which omits the libraries), and generated programs link through `clang`, which
needs that Developer shell's SDK `LIB` paths. The exact PowerShell sequence CI
runs is in
[the quick start](docs/guide/01-quick-start.md#native-windows-with-uv); it is
mirrored from CI and was not executed on this host.
