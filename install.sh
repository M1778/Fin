#!/usr/bin/env sh
# Fin compiler installer: clones M1778/Fin and builds it with ./build.sh.
# Usage: curl -fsSL https://raw.githubusercontent.com/M1778/Fin/master/install.sh | sh
# POSIX sh on purpose: the one-liner above pipes to `sh`, not `bash`.
set -eu

REPO="https://github.com/M1778/Fin"
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
if ! command -v llvm-config >/dev/null 2>&1; then
  echo "install.sh: llvm-config not found; the LLVM backend cannot link without it." >&2
  echo "install.sh: install LLVM (Debian/Ubuntu: https://apt.llvm.org, then re-run)" >&2
  exit 1
fi

# --- 2. Fetch, with retries for transient network failures. ---
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
  fetch git -C "$DIR" pull --ff-only || {
    echo "install.sh: 'git pull --ff-only' in $DIR failed." >&2
    echo "install.sh: the checkout may have local changes or a diverged branch;" >&2
    echo "install.sh: stash or reset it, or point FIN_DIR elsewhere." >&2
    exit 1
  }
elif [ -e "$DIR" ]; then
  echo "install.sh: $DIR exists but is not a git checkout; refusing to overwrite." >&2
  echo "install.sh: remove it or set FIN_DIR to another path." >&2
  exit 1
else
  fetch git clone --depth 1 "$REPO" "$DIR" || {
    echo "install.sh: cloning $REPO failed after $CLONE_ATTEMPTS attempts." >&2
    echo "install.sh: check network access to github.com and re-run." >&2
    exit 1
  }
fi

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
