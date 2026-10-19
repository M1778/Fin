#!/usr/bin/env sh
# Fin compiler installer: clones M1778/Fin and builds it with ./build.sh.
# Usage: curl -o install.sh https://raw.githubusercontent.com/M1778/Fin/master/install.sh
#        (read it), then sh install.sh ; FIN_REF=<ref> pins what is compiled.
# POSIX sh on purpose: the one-liner above is copy-paste into `sh`.
#
# What this script does and does not verify (issue #88):
#   * It never compiles a moving branch. Without FIN_REF it clones the newest
#     released tag (vX.Y.Z); it refuses when none exists. Compiling master
#     moved under the user's feet; a release tag does not.
#   * It prints the resolved commit so "what did I just install?" answers.
#   * It assumes GitHub hosting: the bytes are as published by this repo, and
#     the tags' contents are frozen at release time. Signatures are a
#     maintainer-level decision (see SECURITY.md); for verification commands,
#     read INSTALL.md.
set -eu

REPO="${FIN_REPO:-https://github.com/M1778/Fin}"
# FIN_REPO exists for tests and mirrors only; it defaults to upstream.
CLONE_ATTEMPTS=3

if [ -z "${FIN_DIR:-}" ] && [ -z "${HOME:-}" ]; then
  echo "install.sh: FIN_DIR is unset and HOME is unset; set one of them." >&2
  echo "install.sh: e.g. FIN_DIR=/opt/Fin sh install.sh" >&2
  exit 1
fi
DIR="${FIN_DIR:-$HOME/Fin}"

# Package names differ per platform; detect one manager for hints only.
PKG_MANAGER=""
if command -v apt-get >/dev/null 2>&1; then PKG_MANAGER="apt";
elif command -v dnf >/dev/null 2>&1; then PKG_MANAGER="dnf";
elif command -v pacman >/dev/null 2>&1; then PKG_MANAGER="pacman";
elif command -v brew >/dev/null 2>&1; then PKG_MANAGER="brew";
fi

# suggest <apt-pkg> <dnf-pkg> <pacman-pkg> <brew-pkg>
suggest() {
  case "$PKG_MANAGER" in
    apt) echo "install it with: sudo apt-get install $1" ;;
    dnf) echo "install it with: sudo dnf install $2" ;;
    pacman) echo "install it with: sudo pacman -S $3" ;;
    brew) echo "install it with: brew install $4" ;;
    *) echo "install it with your package manager ($1 on Debian/Ubuntu)" ;;
  esac
}

# need_hint <tool> <apt-pkg> <dnf-pkg> <pacman-pkg> <brew-pkg>
need_hint() {
  tool="$1"; shift
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "install.sh: required tool '$tool' not found." >&2
    echo "install.sh: $(suggest "$@")" >&2
    exit 1
  fi
}

# --- 1. Preflight: every build dependency, before any network or clone. ---
need_hint git git git git git
need_hint cmake cmake cmake cmake cmake
need_hint conan "conan (pipx install conan)" "conan (pipx install conan)" \
  "conan (pipx install conan)" "conan (pipx install conan)"
need_hint bison bison bison bison bison
need_hint flex flex flex flex flex
need_hint python3 python3 python3 python3 python3
# Presence is not enough: a conan installed into another account's ~/.local
# (pip install --user) is found on PATH but fails to import anywhere else.
# Fail here with a fix, not mid-build with a traceback.
if ! conan --version >/dev/null 2>&1; then
  echo "install.sh: 'conan' is on PATH but does not run." >&2
  echo "install.sh: reinstall it for this user (e.g. pipx install conan) and re-run." >&2
  exit 1
fi
if ! command -v cc >/dev/null 2>&1 || ! command -v c++ >/dev/null 2>&1; then
  echo "install.sh: a C and a C++ compiler are required (cc and c++)." >&2
  case "$PKG_MANAGER" in
    apt) echo "install.sh: install with: sudo apt-get install build-essential" >&2 ;;
    dnf) echo "install.sh: install with: sudo dnf install gcc gcc-c++" >&2 ;;
    pacman) echo "install.sh: install with: sudo pacman -S base-devel" >&2 ;;
    brew) echo "install.sh: install with: xcode-select --install" >&2 ;;
    *) echo "install.sh: install gcc/g++ or clang (build-essential on Debian/Ubuntu)" >&2 ;;
  esac
  exit 1
fi
if ! command -v ninja >/dev/null 2>&1 && ! command -v make >/dev/null 2>&1; then
  echo "install.sh: neither ninja nor make found; one build driver is required." >&2
  echo "install.sh: $(suggest ninja ninja ninja ninja ninja)" >&2
  exit 1
fi
# An LLVM of the pinned major must exist; the exact major is read from the
# clone in step 3 and checked there. Here, absence of any llvm-config already
# means the backend cannot link, so fail now instead of at CMake configure.
# Debian ships only the suffixed spelling (llvm-config-22), so a bare PATH hit
# is not the whole test (issue #88 probe).
pathdirs="$(printf '%s' "${PATH:-}" | tr ':' ' ')"
have_llvm_config() {
  if command -v llvm-config >/dev/null 2>&1; then return 0; fi
  for d in $pathdirs; do
    for c in "$d"/llvm-config-*; do
      [ -x "$c" ] && return 0
    done
  done
  return 1
}
if ! have_llvm_config; then
  echo "install.sh: no llvm-config (any major) found; the LLVM backend cannot link without it." >&2
  echo "install.sh: install LLVM (Debian/Ubuntu: https://apt.llvm.org, then re-run)" >&2
  exit 1
fi

# --- 2. Pin what gets compiled, or refuse. ---
# A release tag (vX.Y.Z, newest) by default; `master` is never the default,
# because a moving branch compiles whatever it points at when the user runs
# this script. FIN_REF may name a tag or a full commit; the resolved commit is
# printed so a user can check it against the release it came from.
REF="${FIN_REF:-}"
if [ -z "$REF" ]; then
  REF="$(git ls-remote --tags "$REPO" \
    | sed -E 's|.*refs/tags/||' \
    | grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' \
    | sed -E 's/^v//' \
    | sort -t. -k1,1n -k2,2n -k3,3n \
    | tail -1 \
    | sed -E 's/^/v/')"
fi
if [ -z "$REF" ]; then
  echo "install.sh: no released finc tag found at $REPO." >&2
  echo "install.sh: run with FIN_REF=<full commit or vX.Y.Z tag> naming a ref" >&2
  echo "install.sh: you have inspected (e.g. by reading the source at that commit)." >&2
  exit 1
fi
echo "install.sh: compiling Fin at ref $REF"

fetch() {
  n=0
  while [ "$n" -lt "$CLONE_ATTEMPTS" ]; do
    if "$@"; then return 0; fi
    n=$((n + 1))
    if [ "$n" -lt "$CLONE_ATTEMPTS" ]; then
      echo "install.sh: attempt $n/$CLONE_ATTEMPTS failed, retrying in 5s..." >&2
      sleep 5
    fi
  done
  return 1
}

if [ -d "$DIR/.git" ]; then
  # A previous install: move it to the same pinned ref; never a pull of a
  # branch, which is how a pinned checkout becomes a moving one again.
  fetch git -C "$DIR" fetch --depth 1 origin "$REF" || {
    echo "install.sh: 'git fetch $REF' in $DIR failed." >&2
    echo "install.sh: check network access and that $REF exists at $REPO." >&2
    exit 1
  }
  git -C "$DIR" checkout --detach FETCH_HEAD || {
    echo "install.sh: checking out $REF in $DIR failed." >&2
    echo "install.sh: the checkout has local changes; stash, reset, or set FIN_DIR." >&2
    exit 1
  }
elif [ -e "$DIR" ]; then
  echo "install.sh: $DIR exists but is not a git checkout; refusing to overwrite." >&2
  echo "install.sh: remove it or set FIN_DIR to another path." >&2
  exit 1
else
  case "$REF" in
    v[0-9]*.[0-9]*.[0-9]*)
      # A tag: clone knows it.
      fetch git clone --depth 1 --branch "$REF" "$REPO" "$DIR" || {
        echo "install.sh: cloning $REPO at $REF failed after $CLONE_ATTEMPTS attempts." >&2
        echo "install.sh: check network access to github.com (or $REPO) and re-run." >&2
        exit 1
      }
      ;;
    *)
      # A commit cannot be given to clone --branch; fetch that one object.
      # The wrapper clears $DIR on entry so a failed attempt cannot leave a
      # half-initialized clone that breaks the next retry.
      fetch sh -c '
        set -eu
        rm -rf -- "$1"
        git init -q -- "$1"
        git -C "$1" remote add origin "$2"
        git -C "$1" fetch --depth 1 origin "$3"
        git -C "$1" checkout -q --detach FETCH_HEAD
      ' install-fetch "$DIR" "$REPO" "$REF" || {
        echo "install.sh: fetching commit $REF from $REPO failed." >&2
        echo "install.sh: FIN_REF must be a tag vX.Y.Z or a full 40-hex commit." >&2
        exit 1
      }
      ;;
  esac
fi

# What is on disk is what gets compiled; say it, and for an explicit commit
# ref, require it.
RESOLVED="$(git -C "$DIR" rev-parse HEAD)"
case "$RESOLVED" in
  "$REF")
    echo "install.sh: Fin is at commit $RESOLVED (ref $REF); this is what gets compiled."
    ;;
  *)
    case "$REF" in
      v[0-9]*.[0-9]*.[0-9]*)
        echo "install.sh: Fin is at commit $RESOLVED (ref $REF)."
        ;;
      *)
        echo "install.sh: requested commit $REF but the checkout resolved to $RESOLVED; refusing." >&2
        exit 1
        ;;
    esac
    ;;
esac

# --- 3. The LLVM pin lives in CMakeLists.txt (ADR 0010); enforce it here so a
# wrong-major llvm-config fails in the installer, not deep in CMake. ---
if [ ! -x "$DIR/build.sh" ]; then
  echo "install.sh: $DIR/build.sh missing or not executable; the clone looks incomplete." >&2
  exit 1
fi
LLVM_MAJOR="$(sed -n 's/^set(FIN_LLVM_MAJOR \([0-9][0-9]*\) CACHE STRING.*/\1/p' "$DIR/CMakeLists.txt")"
if [ -z "$LLVM_MAJOR" ]; then
  echo "install.sh: could not read FIN_LLVM_MAJOR from $DIR/CMakeLists.txt." >&2
  exit 1
fi
LLVM_CONFIG=""
if command -v "llvm-config-$LLVM_MAJOR" >/dev/null 2>&1; then
  LLVM_CONFIG="llvm-config-$LLVM_MAJOR"
else
  LLVM_CONFIG="llvm-config"
fi
LLVM_VERSION="$("$LLVM_CONFIG" --version)"
case "$LLVM_VERSION" in
  "$LLVM_MAJOR".*)
    echo "install.sh: LLVM $LLVM_VERSION matches pinned major $LLVM_MAJOR."
    ;;
  *)
    echo "install.sh: llvm-config reports $LLVM_VERSION but this tree pins LLVM $LLVM_MAJOR." >&2
    echo "install.sh: install LLVM $LLVM_MAJOR (Debian/Ubuntu: https://apt.llvm.org) and re-run." >&2
    exit 1
    ;;
esac

# --- 4. Build. --with-llvm is explicit so a future default change cannot ship
# a backend-less installer; --no-test keeps installs fast without touching the
# backend (tests still run in CI and in ./build.sh without flags). ---
cd "$DIR"
./build.sh --release --with-llvm --no-test

# --- 5. Prove the installer produced a working backend compiler, or fail. ---
FINC="$DIR/build/finc"
if [ ! -x "$FINC" ]; then
  echo "install.sh: build finished but $FINC is missing; install failed." >&2
  exit 1
fi
"$FINC" --version >/dev/null || {
  echo "install.sh: $FINC --version failed; the binary does not run." >&2
  exit 1
}
SAMPLE="$DIR/tests/samples/basic.fin"
if [ ! -f "$SAMPLE" ]; then
  echo "install.sh: smoke-test sample $SAMPLE missing from the clone." >&2
  exit 1
fi
SMOKE="$(mktemp -d)"
trap 'rm -rf "$SMOKE"' EXIT INT TERM
"$FINC" "$SAMPLE" -o "$SMOKE/smoke" || {
  echo "install.sh: $FINC could not compile $SAMPLE; backend check failed." >&2
  exit 1
}
"$SMOKE/smoke" || {
  echo "install.sh: compiled smoke program failed to run; backend check failed." >&2
  exit 1
}

echo "finc ready at: $FINC"
