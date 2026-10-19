# Installing Fin with `install.sh`

The installer clones this repository at a pinned ref, runs
`./build.sh --release --with-llvm --no-test`, then proves the result by
compiling and running one sample program. A successful install ends with
`finc ready at: <dir>/build/finc` — anything else is a failure, and the
script says what to fix.

You need a C and C++ compiler, CMake, Conan 2, Bison, Flex, Python 3, and
the LLVM development package for the pinned major (currently 22). Platform
details are in [the quick start](docs/guide/01-quick-start.md). The script
names the missing piece and the install command before cloning anything.

What it checks first (`default $HOME/Fin`, override with
`FIN_DIR=/somewhere sh install.sh`): build tools, then the pinned-ref
resolution, then the LLVM major.

## What is verified, and what is not

- **No moving branch.** Without `FIN_REF` the installer picks the newest
  released tag (`vX.Y.Z`) it can see and clones exactly that; with no
  released tag it refuses and asks for `FIN_REF`. Compiling `master`
  unasked is not on offer.
- **The compiled commit is printed.** `install.sh: Fin is at commit <sha>`
  is the answer to "what did I just install?" — compare it against the
  release it came from.
- **`FIN_REF` is an even stricter pin.** A full commit ref is checked
  against the checkout it produced, and anything else exits; a
  `vX.Y.Z` tag prints the commit the tag names.
- **No signature, no self-checksum (yet).** The script trusts TLS to
  github.com like any fetch and does not verify its own bytes; reading it
  before running is the verification level this repo maintains. Released
  assets (`.deb`, `.rpm`, ... and their `.sha256` sidecars) get checksum
  verification today, and each release's notes carry the `sha256sum` of
  `install.sh` at that tag — compare it after fetching
  `https://raw.githubusercontent.com/M1778/Fin/<tag>/install.sh`. What the
  distribution chain does and does not verify is written out in
  [SECURITY.md](SECURITY.md).

Failure modes you may hit:

- `required tool '<t>' not found` — install it with the printed command and re-run.
- `'conan' is on PATH but does not run` — that conan belongs to another
  account's `pip install --user`; reinstall it for yourself
  (`pipx install conan`) and re-run.
- `llvm-config reports X but this tree pins LLVM Y` — install LLVM Y
  (Debian/Ubuntu: https://apt.llvm.org) and re-run. There is no
  backend-less fallback: without the pinned LLVM there is no install.
- `no released finc tag found at <repo>` — no release exists yet; pass
  `FIN_REF=<full commit or vX.Y.Z tag>` naming a commit you have read.
- `fetching commit <sha> from <repo> failed` — the ref is wrong or the
  network failed; check the commit, then re-run (fetches retry 3 times).
- `install.sh: <dir> exists but is not a git checkout` — remove it or set
  `FIN_DIR` to another path. A previous install is moved to the pinned ref
  (`git fetch --depth 1` then `checkout --detach`), not a branch pull; a
  checkout that fails to move has local changes — stash, reset, or pick
  another `FIN_DIR`.
- `requested commit <sha> but the checkout resolved to <other>; refusing` —
  a `FIN_REF=<commit>` run landed somewhere else, which is a broken repo or
  a spoofed mirror; investigate before running `./build.sh` by hand.
